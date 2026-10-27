# Architecture

## Layers

```
┌──────────────────────────────────────────────────────────────────────────┐
│ Standalone app (JUCE GUI)            Source/App, Source/UI                │
│   AppController  ─ owns everything below; UI actions → engine operations  │
│   AudioHost      ─ AudioIODeviceCallback → engine.process()               │
│   MainComponent, panels, dialogs, ForgeLookAndFeel                        │
├──────────────────────────────────────────────────────────────────────────┤
│ Core services (juce_core / juce_audio_formats, no GUI)                    │
│   Presets/  PresetCodec, PresetManager, PresetApplier                     │
│   IR/       IRLibrary (factory + user library, WAV/AIFF decode/validate)  │
│   MIDI/     MidiController (CC map, Learn, Program Change)                │
│   Platform/ FileUtils (data folders, atomic writes)                       │
├──────────────────────────────────────────────────────────────────────────┤
│ ampforge_dsp  — pure C++17, no JUCE, no OS calls                          │
│   Audio/AmpForgeEngine   the single engine API used by every host         │
│   Parameters/            parameter definitions + lock-free store          │
│   DSP/                   amps, tone stacks, power amp, EQ, convolution,   │
│                          effects, oversampling, FFT, RT utilities         │
└──────────────────────────────────────────────────────────────────────────┘
```

* `ampforge_dsp` is a normal static library. It has no dependency on JUCE, the UI, files, devices or
  MIDI, so the same engine can be wrapped by an AU/VST3 `AudioProcessor` later (see ROADMAP.md) and is
  unit-tested in isolation (`ampforge_dsp_tests`).
* Core services are compiled into each JUCE target (`ampforge_add_core()` in CMakeLists.txt) rather than
  into a JUCE static library, which avoids JUCE module duplication issues.
* Factory IRs and presets are embedded in the binary (`juce_add_binary_data`), so the app has no
  resource-path dependencies and works offline. Mutable user data lives in
  `~/Library/Application Support/AMP FORGE/` (`~/.config/AMP FORGE/` on Linux), never in the bundle.

## Engine API (`Source/Audio/AmpForgeEngine.h`)

| Call | Thread | Notes |
|---|---|---|
| `prepare(sampleRate, maxBlock)` | non-RT, audio stopped | allocates everything; rebuilds IR convolvers for the new rate |
| `process(in, outL, outR, n)` | audio | any `n`, mono in → stereo out, `in` may alias `outL`, `outR` may be null |
| `params()` | any | lock-free `ParameterStore` (atomics) |
| `setCabinetIR(slot, ir)` | non-RT | resamples/normalises/partitions off the audio thread, then hands over |
| `beginTransition()` / `endTransition()` | non-RT | soft mute for click-free preset changes |
| `collectGarbage()` | non-RT | frees objects the audio thread retired |
| `readMeters()` | any | peak-hold atomics, read-and-reset |

Internally `process()` works in control blocks of ≤ 32 samples: parameters are read once per block,
smoothed, and turned into coefficients; host block size and call pattern do not affect the output
(tested: identical output for block sizes 1…4096).

## Thread ownership

| Thread | Owns / does | Never does |
|---|---|---|
| **Audio** (device callback) | `engine.process()`: all DSP state, active convolvers, smoothers | allocate, free, lock, log, file I/O, touch UI |
| **Message** (UI) | components, `AppController`, preset list edits, IR import, settings, Program Change handling, garbage collection timer | DSP state |
| **Worker** (1 thread, `juce::ThreadPool`) | applying presets, decoding IR files, building convolvers | DSP state (only publishes) |
| **MIDI input** | `MidiController::handleMessage()` → atomics only | locks, file I/O, preset loading |
| **Device thread** (`audioDeviceAboutToStart`) | `engine.prepare()` | runs concurrently with audio (JUCE guarantees it does not) |

### Real-time communication mechanisms

* **Parameters:** `std::atomic<float>` per parameter; any thread writes, the audio thread reads once per
  control block and smooths (linear ramps in the knob/plain domain; coefficients are recomputed from
  smoothed values, never interpolated). A change counter lets the UI detect changes by polling.
* **Object hand-over (IR convolvers):** built completely off-thread, then published into a single-slot
  mailbox (`PendingSlot<T>`, one atomic pointer). The audio thread takes ownership with one
  `exchange`. A newer publish simply replaces an unconsumed one (the audio thread never saw it, so the
  publisher deletes it). The audio thread accepts a new convolver only between crossfades.
* **Retirement:** the audio thread never deletes. Finished objects go into a bounded lock-free SPSC
  queue (`GarbageCollector`), drained and deleted by the message thread every 50 ms. If the queue were
  ever full the pointer is parked in a fixed overflow array; as a last resort it is leaked and counted
  (never freed on the audio thread). An atomic pointer alone is not treated as lifetime management.
* **Discrete changes:** amp-model switches fade out (12 ms), swap with fresh state, fade in. Preset
  changes use `beginTransition()`: the audio thread ramps to silence and acknowledges; the worker
  writes all parameters and publishes IRs; `endTransition()` makes the audio thread snap all smoothers
  to the new values, reset filter states and fade back in. If audio is not running, the wait times
  out after 60 ms and changes apply directly.
* **Meters / status:** atomics written by the audio thread, read-and-reset by the UI timer.
* **Non-RT mutual exclusion:** `prepare()` and `setCabinetIR()` share a `std::mutex` that the audio
  thread never touches.
* **Denormals:** FTZ/DAZ enabled for the duration of `process()` (SSE MXCSR on x86, FPCR.FZ on arm64).

Verified with ThreadSanitizer (`Scripts/run_sanitizers.sh`), including a test that publishes IRs and
changes models from another thread while audio runs.

## UI ↔ engine

* Every control is bound to exactly one parameter index (`Controls.h`). There is no UI-owned audio
  state: a 30 Hz timer pulls values from the `ParameterStore`, so MIDI, presets and undo show up in
  the UI automatically and nothing is ever pushed from the audio thread.
* Edits go through `AppController` (undo grouping per drag gesture / click).
* The EQ response curve is drawn with the same `GraphicEQDesigner` code the engine uses.

## Standalone-only pieces (not needed by plugins)

`AudioHost` (device callback, input-channel choice, mono-out sum), audio-device management,
first-launch flow, settings file, MIDI device enabling, window and menus — all in `Source/App` and
`Source/UI`. The engine knows nothing about them.

## Source map

```
Source/
  Audio/AmpForgeEngine.*        engine
  Parameters/Parameters.*       parameter table + store
  DSP/Utilities/                DspCommon, Filters (biquad/one-pole), FFT, Oversampler, RealtimeExchange
  DSP/ToneStacks/               ToneStack (analytic TMB model)
  DSP/Amplifiers/               TubeStage, AmpModel (+ voicings)
  DSP/PowerAmp/                 PowerAmp
  DSP/GraphicEQ/                GraphicEQ (+ designer)
  DSP/Cabinet/                  Convolver, IR preparation, CabinetSection
  DSP/Effects/                  Overdrive, NoiseGate, Chorus/Delay, Reverb, OutputLimiter
  Presets/ IR/ MIDI/ Platform/  core services
  App/                          Main, AppController, AudioHost
  UI/                           look and feel, controls, panels, dialogs, main component
Tools/                          IR generator, renderer/calibration, measurement, benchmark
Tests/                          doctest suites (DSP, Audio, Presets, IR, Integration/MIDI)
```

## Key decisions

| Decision | Reason |
|---|---|
| JUCE 8.0.9 (pinned) + CMake | stable cross-platform audio/GUI framework with first-class AU/VST3 support later |
| Engine without JUCE | portability to plugin wrappers, isolated testing, no hidden allocations from framework classes |
| Own partitioned convolver instead of `juce::dsp::Convolution` | zero added latency (direct-form head), explicit ownership/crossfade control, testable without JUCE |
| IIR half-band oversampling | a few samples of group delay instead of the larger delay of linear-phase FIR half-bands |
| Standalone uses `AudioIODeviceCallback` directly | full control of guitar-input channel selection and device recovery; plugins will use `AudioProcessor` |
| C++17 | widest compiler/SDK compatibility; nothing in the code needs C++20 |
| macOS 11 minimum | first release with Apple Silicon; well within JUCE 8 support |

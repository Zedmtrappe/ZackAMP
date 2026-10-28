# Test plan

Two kinds of checks: **automated** (run on every build, deterministic synthetic signals) and
**manual listening / hardware** checks that need a person, a guitar and an audio interface.
Only the automated checks have been run so far.

## Running

```bash
cd build/release && ctest --output-on-failure   # both suites
./ampforge_dsp_tests                              # DSP + engine (no JUCE)
./ampforge_core_tests_artefacts/Release/ampforge_core_tests   # presets, IR library, MIDI
Scripts/run_sanitizers.sh                         # ASan+UBSan and TSan builds of both suites
```

## Automated checks

### DSP (`Tests/DSP`)
| Area | Checks |
|---|---|
| FFT | agreement with a naive DFT (sizes 4…2048), exact round trip, rejects non-power-of-two sizes |
| Oversampling | flat passband (< 0.05 dB at 1 kHz, < 0.1 dB at 15 kHz, 2x/4x/8x); image rejection < -90 dB; content above base Nyquist does not alias back (< -80 dB) |
| Tone stack | digital vs analogue model ≤ 0.05 dB (≥ 96 kHz); passive TMB behaviour (mid scoop, bass/treble/mid ranges); stable, decaying impulse response at extreme settings with 1000× input |
| Graphic EQ | each band hits its value at its centre (±0.35 dB, 4 sample rates, ±6/±12 dB); combined curves ±1 dB; flat is flat; coefficients stable; processed sine response matches design (±0.2 dB) |
| Convolution | matches direct convolution (≤ 2·10⁻⁵) for IR lengths 1…4801 with irregular host blocks (1, 7, 33, 500…) — zero latency; stereo IR independence; resampler accuracy; truncation/fade/normalisation finite |
| Micro-pitch | ±13 cents (average of seven notes within 0.6 cents, each note within 1.3); zero mix leaves the signal untouched |
| Flanger | stable with feedback, different from chorus, chorus↔flanger switch without clicks |
| Cabinet | IR switch (phase-inverted, delayed IR) produces no discontinuity; bypass is exact dry signal |

### Engine (`Tests/Audio`)
| Area | Checks |
|---|---|
| Lifecycle | unprepared engine outputs silence; zero-length blocks |
| Robustness | finite, ≤ 0 dBFS output for silence, guitar-like plucks, ±1000 square waves, NaN/Inf input — every model × 44.1/48/88.2/96/192 kHz with all effects at extreme settings; recovers after NaN input |
| Block-size independence | identical output (≤ 10⁻⁵) for host blocks 1, 7, 32, 33, 128, 512, 4096 |
| Bypass | exact trimmed dry signal on both channels |
| Distinct models | pairwise correlation of the six models' outputs < 0.97 on the same input |
| Controls | gain, bass, mid, treble, presence, master each change the audio measurably |
| Transitions | amp-model switch has no step larger than 2× normal; `beginTransition` times out cleanly without audio |
| Concurrency | IR publishing + model/param changes from another thread while audio runs (also under TSan) |
| Invalid IR | empty / NaN IR rejected, previous IR kept |
| Gate | hiss between notes > 50 dB below the note (measured ≈ 81 dB); gate off lets it through |
| Delay sync | 1/4 at 120 bpm repeats at 500 ms |
| Aliasing | inharmonic energy of a 3.1 kHz tone into high gain decreases 2x → 4x → 8x; 8x < -60 dB; automatic mode < -60 dB for a 1 kHz lead note |
| DC | |mean| < 2·10⁻³ for every model with half-wave input at full gain |
| Smoothing | 10 %→90 % jumps of gain, master, bass, output, EQ band produce no click (second difference) |
| Valves / Pull / Sag / Variac | each changes the audio measurably and stays finite; default valves reproduce the calibrated amp bit-exactly |
| I/O layouts | mono output (null right channel); in-place processing (input aliases left output) |

### Presets, IR library, MIDI (`Tests/Presets`, `Tests/IR`, `Tests/Integration`)
| Area | Checks |
|---|---|
| Presets | JSON round trip of all parameters + IR refs; invalid JSON / wrong format / empty rejected; partial presets use defaults, unknown params and bad values warned, clamping; newer schema warning; presets without valve settings get the model's usual valves; every parameter enum index matches its ID; all factory presets parse, have descriptions, valid categories, existing IRs, ≥ 8 categories |
| Preset files | save / collision refused / explicit overwrite / factory names reserved / rescan from disk / rename / duplicate / delete; corrupt files skipped and reported; filters; atomic writes leave no temp files |
| Applying | `applyState` + `captureState` round trip is exact; missing IR falls back to the model default with a warning naming the IR |
| IR library | all factory IRs decode and load at 44.1 kHz (resampled); import 16/24/32-bit × mono/stereo × 44.1/48/96 kHz with copy; copy never overwrites; rejects garbage, empty, silent, 4-channel, too long, too large, nonexistent files with specific messages; reference imports persist, deduplicate, survive restart; removal never deletes referenced originals; deleted managed copy → flagged missing, load fails with message; damaged index preserved and reported |
| MIDI | CC mapping, omni and channel filter; latching and toggle footswitch modes; range-limited output level; Learn; Program Change (on/off); malformed messages, running status, SysEx, null data ignored; mapping serialisation round trip ignores junk |

### Tools-based measurements (run on demand)
* `ampforge_measure` — THD, H2/H3 balance and output level vs gain for every model at single-coil and
  humbucker levels (table in DSP_DESIGN.md).
* `ampforge_render --calibrate` — loudness of every factory preset over the synthetic DI set; factory
  levels were set so presets read ≈ -20 dB on this loudness proxy and peak ≤ -4 dBFS (no limiting).
* `ampforge_bench` — mean / p99 / worst processing time per block size, model, IR length, oversampling.
* Application smoke test — `ZackAMP --smoke-test 5` launches the full GUI app, runs, and quits; run
  headless on Linux with Xvfb, including with no audio device present.

### What the automated tests do **not** prove
* That anything sounds good. They prove stability, correctness of filters/convolution, absence of
  clicks/NaNs/races in the tested scenarios, and measurable model differences.
* Real-time safety under all conditions: the code follows the rules (no locks/allocations/I/O on the
  audio thread), sanitizers cover races, but worst-case timing must be measured on the target Mac.
* Behaviour of real Core Audio devices and real MIDI hardware.

## Manual checks (not yet performed)

Record: date, Mac model, macOS version, interface, sample rate, buffer size, guitar/pickups, tester.

### Setup and devices
- [ ] Clean macOS build with `Scripts/build_macos.sh` (Apple Silicon and, if available, Intel)
- [ ] First launch: setup guide; microphone permission prompt; no sound until an input is chosen
- [ ] Select interface, guitar input channel, 44.1 / 48 / 96 kHz, 32 / 64 / 128 / 256 sample buffers
- [ ] Loopback round-trip latency at 64 and 128 samples (AUDIO_LATENCY.md); note the values
- [ ] Unplug the interface while playing → warning, no crash; replug → reconnects automatically
- [ ] Interface missing at launch → no fallback to built-in mic/speakers
- [ ] Mono-output and stereo-output interfaces
- [ ] Input clip lamp lights when the interface gain is too high
- [ ] 1-hour continuous playing session: no dropouts, no drift in CPU, memory stable (Activity Monitor)

### Tone and feel (for each amp model)
- [ ] Clean electric guitar, single-coil: sparkle without harshness on the clean model
- [ ] Humbucker: earlier breakup on tweed/Plexi; no unusable fizz on high gain
- [ ] Open chords: notes stay distinguishable at crunch settings
- [ ] Low-string palm muting: tight, not flubby, on Albion Lead / Cascade Heavy
- [ ] High-gain power chords: defined, not mush
- [ ] Lead bends: sustain, smooth top, no warble/aliasing ("dirty" whistle on bends)
- [ ] Soft vs hard picking: audible dynamic and timbral difference, especially on tweed/Plexi
- [ ] Guitar volume rolled back to ~6: cleans up convincingly (tweed, Plexi, Albion Lead)
- [ ] Tone stack: knobs interact musically; no knob dead over most of its travel
- [ ] Bright switch strongest at low volume settings
- [ ] Presence/Depth useful, not exaggerated
- [ ] Valve swaps: audible character change without a big level jump; model change resets valves
- [ ] PULL adds saturation; Sag gives "bloom" at high settings; Variac below 100 % browns out
- [ ] Graphic EQ: classic V-curve on Cascade Lead sounds as expected; pre vs post position difference
- [ ] Noise gate on high gain: silent between phrases, notes not chopped, sustain intact

### Cabinets and IRs
- [ ] Every factory IR with every family: usable, sensible variety, no IR overly boomy or harsh
- [ ] Import commercial / free mono and stereo IRs at 44.1/48/96 kHz; compare to another IR loader
- [ ] Audition by clicking while playing: no clicks; Revert works
- [ ] Dual cab blend and panning; cab bypass into a real cabinet / FRFR
- [ ] Move a referenced IR file → preset warns and uses default cab

### Effects, presets, MIDI
- [ ] Drive as boost in front of high gain tightens the low end
- [ ] Delay/reverb tails ring out when switched off; no clicks when switching
- [ ] Chorus depth/rate ranges musical
- [ ] Preset switching while playing: no clicks, no level jumps between factory presets
- [ ] Save, Save As, rename, duplicate, delete; quit and relaunch restores the last sound
- [ ] MIDI footswitch: Program Change switching while playing; CC toggles for effects
- [ ] MIDI Learn on a knob with an expression pedal; mapping persists after relaunch
- [ ] MIDI control with the window minimised
- [ ] Retina display rendering; window at minimum size (960×640) on a laptop; keyboard navigation (Tab, arrows on knobs)
- [ ] VoiceOver reads control names and values

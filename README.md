# Z.AMP

A standalone guitar amplifier application for macOS, built around a shared, framework-independent
real-time engine that is designed to become AU and VST3 plugins later.

Three amplifier families with genuinely different circuit topologies, a five-band guitar graphic EQ,
a zero-latency cabinet IR engine with dual-cab blending and third-party IR import, a small set of
studio-quality effects, presets and live MIDI control.

> **Status: version 0.1 (first complete build).** Everything listed under *Features* is
> implemented and covered by automated tests, which pass on macOS (GitHub's build machines) and
> Linux. It has **not yet been played through a real audio interface** — see
> [Known limitations](#known-limitations). No listening tests have been carried out by a
> human yet; the DSP has been calibrated by measurement only.

---

## Features

| Area | What is implemented |
|---|---|
| **Amp models** | 6 original models in 3 families: *Lacquer Clean*, *Lacquer Tweed* (American); *Plexi*, *Albion Lead* (British); *Cascade Lead*, *Cascade Heavy* (modern high gain). Each has its own preamp topology (triode stages with grid-conduction bias shift, cathode-bypass shelving, coupling/Miller filtering, cathode followers), a passive TMB tone stack at its model-specific position, and a power amp with phase-inverter saturation, supply sag, presence/depth NFB shaping, output-transformer band limits and speaker-load interaction. One fixed row of controls for every model: a detented MODEL knob, Gain (with PULL for extra saturation), Bass, Middle, Treble, Presence, Resonance, Master and Bright, plus a POWER section with Sag and Variac, and preamp (V1) and power valve selectors that reset to each model's usual valves when the model changes. |
| **Oversampling** | Low-latency polyphase IIR half-band oversampling around all nonlinear stages. Automatic: 8x at 44.1/48 kHz, 4x at 88.2/96 kHz, 2x above; selectable 2x/4x/8x. |
| **Graphic EQ** | 80 / 240 / 750 / 2200 / 6600 Hz, ±12 dB, band-interaction-corrected so the response hits the slider values; positioned between preamp and power amp (default, classic position) or post-amp. The curve shown is computed from the actual filter coefficients. Factory curves, flat reset. |
| **Cabinet** | Zero-latency partitioned convolution (direct 64-tap head + FFT tail). 12 original factory cabinet IRs + a flat utility IR. Dual-cab mode with level/pan. Click-free crossfaded IR switching, IR auditioning, cab bypass. |
| **IR import** | WAV (16/24/32-bit PCM, 32-bit float) and AIFF, mono or stereo, any common sample rate (resampled offline with a Kaiser-windowed sinc). Copy into a managed library or reference in place. Drag and drop. Validation with clear errors; missing files handled gracefully. |
| **Effects** | Noise gate (input-keyed, applied after the amp), overdrive/boost pedal, micro-pitch doubler (±13 cents, stereo), stereo chorus/flanger, stereo delay (tempo sync, filtered feedback, spill-over), stereo FDN reverb (spill-over), output level and a -1 dBFS safety limiter. |
| **Presets** | 20 original factory presets in 9 categories, level-calibrated by measurement. User presets: save, save as, rename, duplicate, delete (to Trash), search, category/amp filters, prev/next, modified indicator, undo/redo of edits. Versioned JSON schema with atomic writes. |
| **MIDI** | All MIDI inputs auto-enabled; channel filter; Program Change → presets (with offset); CC mapping with Learn (right-click any control), range limits, footswitch toggle mode, sensible defaults; works with the window closed or minimised. |
| **Standalone app** | Audio device/input/output/sample-rate/buffer selection, guitar input channel choice, meters with clip lamps, CPU/limiter/gate/MIDI indicators, first-launch setup guide, device disconnect/reconnect handling (never silently falls back to a built-in mic + speakers), session restore, native menus on macOS. |

## Requirements

* **macOS 11 (Big Sur) or later**, Apple Silicon or Intel (universal binary). 11.0 is the first
  release with native Apple Silicon support; JUCE 8 itself supports this deployment target.
* An audio interface with an instrument (Hi-Z) input is strongly recommended.
* To build: Xcode Command Line Tools (or Xcode), CMake ≥ 3.22, Git. Ninja optional (`brew install ninja`).
* Dependencies (fetched automatically, pinned): **JUCE 8.0.9** (`f72bad6`). doctest 2.4.11 is vendored.

## Installing / updating (no Xcode needed)

Every change pushed to `main` is built automatically by GitHub (universal app, tests run first)
and published as the **latest** release.

1. Open https://github.com/Zedmtrappe/ZackAMP/releases (signed in to GitHub) and download
   **Z.AMP-macOS.zip** from "Z.AMP - latest build".
2. Unzip it (double-click). You get a `Z.AMP` folder with the app, an installer script and
   `READ ME FIRST.txt`.
3. Open **Terminal**, type `bash ` (with a space), drag **Install Z.AMP.command** into the
   window and press Return. It quits a running Z.AMP, replaces `/Applications/Z.AMP.app`,
   removes macOS's "downloaded from the internet" block and starts the new version.

Updating is the same three steps. Presets, imported IRs, MIDI mappings and audio settings live in
`~/Library/Application Support/ZackAMP` and are kept.

**Why the Terminal step:** the app is ad-hoc signed, not signed/notarised with an Apple Developer ID
(that costs $99/year). Without the installer, macOS would refuse to open the downloaded app until
you approve it under *System Settings → Privacy & Security → Open Anyway*.

**Microphone permission:** macOS treats any audio input (including an interface) as "microphone".
Approve the prompt; because each unsigned build looks new to macOS it may ask again after an
update. If you declined, enable Z.AMP under *System Settings → Privacy & Security → Microphone*.

## Building on macOS yourself (optional)

```bash
git clone https://github.com/Zedmtrappe/ZackAMP.git
cd Z.AMP
Scripts/build_macos.sh            # fetches JUCE, builds a universal Release app, runs the tests
open "build/macos/ZackAMP_artefacts/Release/Z.AMP.app"
```

Faster Apple-Silicon-only build: `ARCHS=arm64 Scripts/build_macos.sh`.
Xcode project: `cmake --preset xcode && open build/xcode/Z.AMP.xcodeproj`.

**macOS build status:** builds and passes all automated tests on GitHub's macOS runners
(Intel `macos-15-intel`, cross-compiled universal arm64 + x86_64) and launches/quits cleanly there.
Not yet tested with a real audio interface, guitar or MIDI controller.

## Building on Linux (development / CI)

```bash
sudo apt-get install ninja-build libasound2-dev libx11-dev libxrandr-dev libxinerama-dev \
     libxcursor-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev
Scripts/build.sh
```

## Tests

```bash
cd build/release && ctest --output-on-failure      # or: build/macos on a Mac
Scripts/run_sanitizers.sh                          # AddressSanitizer+UBSan and ThreadSanitizer
build/release/ampforge_bench                       # real-time performance on this machine
build/release/ampforge_measure                     # THD / level vs gain for every amp model
Scripts/render_listening_set.sh                    # renders a synthetic DI set through every preset
```

See [docs/TEST_PLAN.md](docs/TEST_PLAN.md) for what is checked automatically and the manual
listening checklist.

## First launch and audio setup

1. Plug the guitar into an instrument / Hi-Z input on your interface.
2. **Audio** (top right): choose the interface as input and output device, tick the guitar's input
   channel, choose the sample rate (44.1 or 48 kHz is ideal) and a **64 or 128 sample** buffer.
   Pick which active input carries the guitar under *Guitar input*.
3. Set the **interface's** gain so hard picking peaks around **-12 dBFS** on the **IN** meter.
   If the IN clip lamp lights, the converter clipped: turn the interface gain down (the
   *Input Trim* knob cannot undo converter clipping).
4. Start with monitors low. Presets are levelled to similar loudness, peaking around -4 to -10 dBFS.
5. **Bypass** passes your dry signal, useful for checking routing and latency.

On first launch Z.AMP only opens audio outputs: inputs stay off until you choose the
interface, so a laptop's built-in microphone and speakers can never start howling at you.
If the saved interface is missing, Z.AMP waits for it (and reconnects automatically) rather than
switching to another device. Latency: see [docs/AUDIO_LATENCY.md](docs/AUDIO_LATENCY.md).

## Impulse responses

* Click the IR name in the cabinet panel to open the browser. Clicking an IR loads it
  immediately so you can audition while playing; **Revert** goes back, **Keep** keeps it.
* **Import IR…** or drag WAV/AIFF files onto the cabinet panel. Choose:
  * **Copy** (recommended): the file is copied into Z.AMP's library, so presets keep working
    if you move or delete the original.
  * **Reference**: Z.AMP remembers where the file is. If you move it, the entry shows as
    *missing* and presets that use it fall back to the amp's default cabinet with a warning.
* Removing an IR from the library never deletes your original file (only Z.AMP's own copy).
* Limits and formats: [docs/IR_FORMATS.md](docs/IR_FORMATS.md).
* **Dual Cab**: enable it to blend cabinets A and B with independent level and pan.
* **CABINET off** bypasses the cab simulation for a real cabinet / external cab sim.

## Presets

* Click the preset name (top bar) to browse: search, filter by category or amp family; click to load.
* `*` after the name means unsaved changes. **Save** updates your own preset; factory presets are
  read-only, so Save becomes **Save As**. Name collisions are refused, never silently overwritten.
* Rename / Duplicate / Delete are in the browser (delete moves the file to the Trash where available).
* Undo/Redo (⌘Z / ⇧⌘Z) cover knob, switch and EQ edits; loading a preset clears the history.
* User presets live in `~/Library/Application Support/ZackAMP/Presets` (*File → Show User Data Folder*).
  Format: [docs/PRESET_FORMAT.md](docs/PRESET_FORMAT.md).
* Keyboard: ⌘S save, ⇧⌘S save as, ⌘P presets, ⌘← / ⌘→ previous/next preset, ⌘, audio settings.

## MIDI

* MIDI inputs are enabled automatically (you can switch individual ones off in *Audio* settings).
* **MIDI** button: channel filter (default omni), Program Change on/off and offset, mapping list.
* **Learn:** right-click any knob, switch or EQ fader → *MIDI Learn*, then move the controller.
* Defaults: CC80–85 = Drive, EQ, Delay, Reverb, Chorus, Gate on/off (≥ 64 on, < 64 off);
  CC11 = amp gain; CC12/13 = delay/reverb mix; CC7 = output level limited to -40..0 dB.
* For momentary footswitches that send 127 on each press, tick *Footswitch toggle mode* before learning.
* **Program Change numbering:** MIDI sends program numbers 0–127; many controllers display them as
  1–128. With offset 0, program 0 (“1” on most pedals) loads the first preset in the list
  (factory presets in their curated order, then your presets alphabetically). Raise the offset to
  shift the mapping.
* MIDI-controlled parameters are smoothed by the audio engine and work with the UI closed.

## Known limitations

* **Not yet used on a real Mac with an audio interface.** The macOS build passes its tests and
  launches on GitHub's build machines, but Core Audio device behaviour, the microphone permission
  flow, Retina rendering and the macOS menu bar have not been checked by a person.
* **No human listening tests yet.** The amp models are calibrated by measurement (THD/level vs gain,
  aliasing, loudness); they need ears, guitars and iteration. Expect voicing changes.
* **Factory IRs are synthetic** (procedurally generated, original, freely redistributable). They are
  plausible approximations, not captures of real cabinets. For the most realistic tone, import
  good commercial or free IRs. See [docs/IR_FORMATS.md](docs/IR_FORMATS.md).
* IRs longer than 1 s are truncated (with a short fade) for real-time use; files longer than 10 s are rejected.
* Fixed effect order (gate → drive → amp → EQ → cab → chorus → delay → reverb); no re-ordering yet.
* Undo does not cover IR changes or preset load.
* Tempo sync uses the app's tempo (Tempo knob in the Global section, or the tap-tempo button in the
  delay); MIDI clock sync is not implemented.
* No separate "direct monitoring" path: **Bypass** gives you the dry signal through the same
  low-latency path; use your interface's own direct monitoring for zero-latency dry sound.
* Performance was measured on a 2.1 GHz cloud Xeon VM, not a Mac; worst-case figures from a shared VM
  are noisy. Run `ampforge_bench` on your Mac.
* Not signed or notarised; AU/VST3 not built yet (architecture ready — see [docs/ROADMAP.md](docs/ROADMAP.md)).

## Documentation

[Architecture](docs/ARCHITECTURE.md) · [DSP design](docs/DSP_DESIGN.md) · [Latency](docs/AUDIO_LATENCY.md) ·
[IR formats](docs/IR_FORMATS.md) · [Preset format](docs/PRESET_FORMAT.md) · [Test plan](docs/TEST_PLAN.md) ·
[Roadmap](docs/ROADMAP.md) · [Third-party notices](docs/THIRD_PARTY_NOTICES.md) · [Licences](LICENSES.md) ·
[Changelog](CHANGELOG.md)

Z.AMP's amplifier models are original designs that take inspiration from the general design
principles of classic American, British and modern high-gain amplifiers. They are not affiliated
with, endorsed by, or claimed to reproduce any manufacturer's products.

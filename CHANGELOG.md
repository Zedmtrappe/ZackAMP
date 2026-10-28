# Changelog

## Unreleased

### Added
- Micro-pitch doubler: copies shifted +13 cents (left) and -13 cents (right), post amp/cab, with
  on/off and Mix.
- Chorus / Flanger mode switch.
- Single fixed row of amp controls: a detented MODEL knob, then Gain, Bass, Middle, Treble,
  Presence, Resonance, Master — every control and Bright now works on every model.
- PULL (pull-for-saturation) switch on the Gain knob: about +10 dB more preamp gain.
- POWER section: Sag (supply sag amount) and Variac (60–110 % supply voltage).
- Preamp (V1) and power valve selectors (12AX7/5751/12AY7/12AT7; 6L6/EL34/6V6/EL84/KT88).
  Changing the model resets them to that model's usual valves; the default valves reproduce the
  calibrated sound exactly. Older presets load with the model's usual valves.

### Added (effects)
- **Smooth**: dynamic 2 kHz tamer after the cabinet (3:1, up to 9 dB of cut); the knob sets the
  threshold (clockwise = more smoothing); a process light glows with the amount of cut.
- Drive types **Screamer** (the original mid-hump overdrive) and **RAT** (op-amp distortion with
  hard diode clipping; Tone acts as its Filter).
- Delay types **Studio** and **Tape** (wow/flutter, saturated and darkening repeats).
- Reverb types **Room**, **Hall** (the original), **Spring** and **Chamber**; type changes fade
  the tail out and back in.

### Added (amp head)
- Photo-realistic amp interior behind the glass: valves, transformers and chassis rendered in
  Blender Cycles as light layers that the app mixes live (heat colour per model, valve type,
  transformer glow, LED light). Falls back to the drawn interior if the images are missing.

### Changed
- The app is now called **Z.AMP** (download `Z.AMP-macOS.zip`, installer `Install Z.AMP.command`).
  Presets, IRs and settings stay where they were (`~/Library/Application Support/ZackAMP`).
- "Albion Plexi" is now called "Plexi"; every model uses the black faceplate.

### Fixed
- Audio from the interface now actually reaches the amp (the audio callback was not registered).
- macOS audio-input ("Microphone") permission is explained before the prompt; a banner with an
  "Open System Settings" button appears while access is denied; audio restarts when it is granted.
- Choosing an input device now switches its inputs on.

## 0.1.0 — first complete build (unreleased)

Built and tested on Linux; macOS build configured but not yet verified.

### Added
- Framework-independent real-time engine (`ampforge_dsp`) with control-block processing, smoothing,
  click-free model and preset transitions, lock-free parameter store and object hand-over.
- Six original amp models in three families (American, British, modern high gain) with circuit-inspired
  preamp topologies, analytic tone stacks, power amp with sag/NFB shaping and speaker-load interaction.
- Polyphase IIR oversampling (automatic 8x/4x/2x by sample rate, selectable).
- Five-band graphic EQ with band-interaction correction, pre-power-amp or post-amp position.
- Zero-latency partitioned convolution, dual cab, crossfaded IR switching, IR library with
  copy/reference import, validation, missing-file handling; 12 procedurally generated factory IRs.
- Noise gate, overdrive/boost, chorus, delay (sync, spill-over), FDN reverb (spill-over), safety limiter.
- 20 level-calibrated factory presets; user preset management with atomic writes, undo/redo.
- MIDI: Program Change, CC mapping with Learn, ranges, footswitch modes, defaults.
- Standalone app: device setup, guitar-input choice, meters, status, onboarding, device recovery,
  session restore, native menus (macOS).
- Tests (51 cases), sanitizer configurations, measurement/render/benchmark/IR-generator tools,
  documentation, CI workflow for macOS and Linux.

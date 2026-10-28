# Changelog

## Unreleased

### Added
- Micro-pitch doubler: copies shifted +13 cents (left) and -13 cents (right), post amp/cab, with
  on/off and Mix.
- Chorus / Flanger mode switch.

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

# Impulse responses: formats, limits and processing

## Supported files

| Property | Supported |
|---|---|
| Containers | WAV (RIFF/RF64/WAVE_FORMAT_EXTENSIBLE as decoded by JUCE), AIFF/AIFC |
| Sample formats | 8/16/24/32-bit integer PCM, 32-bit float |
| Channels | 1 (mono) or 2 (stereo). More channels are rejected with a message. |
| Sample rates | 8 kHz – 384 kHz (any rate; resampled to the engine rate) |
| Length | any up to **10 s** (longer files are rejected — they are almost certainly not cabinet IRs); only the first **1 s** is used for convolution (with a 5 ms fade, and a warning) |
| File size | up to **64 MB** |

Rejected with a clear message: unreadable/corrupt files, zero-length audio, silent files (peak
< -120 dBFS), non-finite samples (NaN/Inf in float files), too many channels, out-of-range sample rates,
over-long or over-large files. A rejected file never replaces the current IR.

The limits are defined in `IRImportLimits` (`Source/IR/IRLibrary.h`) and can be changed in one place.

## Processing applied when an IR is loaded (off the audio thread)

1. **Decode** to 32-bit float.
2. **Resample** to the current engine sample rate with an offline Kaiser-windowed sinc
   (β = 9, 32 zero crossings per side at unity ratio, cutoff 0.97 of the lower Nyquist frequency).
   In-band error is < 0.1 dB (tested at 44.1→48, 96→48, 48→96, 192→44.1 kHz). The original file is
   kept, so a later sample-rate change re-prepares from the source.
3. **Truncate** to 1 s if needed (5 ms raised-cosine fade).
4. **Level-normalise:** one gain for all channels so that the average power response between
   100 Hz and 5 kHz is 0 dB. Switching IRs changes tone, not loudness. (Relative levels between
   your own IRs are therefore not preserved — use the cab level controls.)
5. **Partition** for zero-latency convolution and hand the finished convolver to the audio thread,
   which crossfades to it over 30 ms.

## Channel handling

* **Mono IR:** the guitar signal (mono) is convolved once; left and right are identical.
* **Stereo IR:** the guitar signal is convolved with the left and right IR channels → stereo cabinet.
  A stereo IR is one (stereo-captured) cabinet, not two cabinets.
* **Dual cab:** two independent slots (each mono or stereo) with their own level and equal-power pan,
  summed. Use this to blend two different cabinets or mics.

## User library

* Location: `~/Library/Application Support/ZackAMP/IR Library/` — `library.json` (index) and
  `Files/` (copies made by Z.AMP).
* **Copy** (recommended): the file is duplicated into `Files/` (never overwriting: name collisions get a
  numbered name). Presets keep working even if you move or delete the original.
* **Reference:** only the path is stored. If the file is moved or deleted the entry is shown as
  *MISSING*; presets using it fall back to the amp model's default cabinet with a warning.
* Each entry stores: display name, original filename, path, copy/reference status, channel count,
  original sample rate and duration.
* **Remove from Library** deletes only Z.AMP's own copy (and only if it really is inside the
  managed `Files/` folder); referenced originals are never touched.
* The index is written atomically. If it is damaged it is preserved as `library.damaged.json` and the
  library starts empty (your files are untouched); re-import to rebuild it.
* Presets store IR **IDs** (`factory:…` / `user:<uuid>`) and display names, never audio data.

## Factory IRs

13 IRs, 48 kHz mono 24-bit, 85 ms, embedded in the application binary:

| ID | Name |
|---|---|
| factory:am-1x12-open-cap | US 1x12 Open — Cap |
| factory:am-1x12-open-edge | US 1x12 Open — Edge |
| factory:am-2x12-open-cap | US 2x12 Open — Cap |
| factory:am-2x12-open-room | US 2x12 Open — Room |
| factory:br-4x12-v25-cap | UK 4x12 Vintage 25W — Cap |
| factory:br-4x12-v25-edge | UK 4x12 Vintage 25W — Edge |
| factory:br-4x12-h75-cap | UK 4x12 Heritage 75W — Cap |
| factory:br-4x12-h75-offaxis | UK 4x12 Heritage 75W — Off-axis |
| factory:md-2x12-closed-cap | Modern 2x12 Closed — Cap |
| factory:md-4x12-heavy-cap | Modern 4x12 Heavy — Cap |
| factory:md-4x12-heavy-edge | Modern 4x12 Heavy — Edge |
| factory:bt-1x12-closed-cap | Boutique 1x12 Closed — Cap |
| factory:util-flat | Utility — Flat (test IR, unit impulse) |

**Provenance and licence:** all are original works generated procedurally by `Tools/IRGenerator.cpp`
(`ampforge_irgen Resources/FactoryIRs`), deterministic (fixed seeds), with no recorded or third-party
audio. They are distributed under the same terms as the rest of Z.AMP (see LICENSES.md).
They are designed approximations of cabinet/microphone character (speaker resonance, open/closed
back, cone break-up, mic position, early reflections) — **not** captures of real cabinets, and their
names describe generic cabinet types, not specific products. For the most realistic sound, load
captured IRs from a licensed source; the factory set is meant to be a usable, legal starting point.

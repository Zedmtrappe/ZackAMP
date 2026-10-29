# DSP design

All DSP lives in `Source/DSP` and `Source/Audio` (pure C++17). This document describes what is
implemented, why, what was measured, and the known trade-offs. Nothing here claims to reproduce a
specific commercial circuit; values are original and chosen for musical behaviour.

## Signal chain (fixed order)

```
interface input (mono, chosen channel)
 → Input Trim → scale to amp units (0 dBFS ≈ 2 V into a 1.5 V-headroom triode)
 → noise-gate detector (sidechain on the clean input)
 → ↑ upsample (8x / 4x / 2x, IIR half-band)
 → overdrive / boost pedal (crossfaded bypass)
 → preamp (model-specific chain, incl. tone stack)
 → graphic EQ  [position "Pre Power Amp", default]
 → power amp + output transformer + speaker-load interaction
 → ↓ downsample
 → graphic EQ  [position "Post Amp"]
 → gate gain → amp-model switch fade
 → cabinet: IR slot A (+ slot B in dual mode), mono → stereo
 → micro-pitch → chorus/flanger → delay → reverb (stereo)
 → Output level → DC blocker → safety limiter (-1 dBFS, no look-ahead) → output
```

Bypass crossfades the whole chain against the trimmed dry input. Every on/off switch is a 20 ms
crossfade, never a hard cut. Delay and reverb gate their *input* when switched off so tails ring out.

## Amplifier models

### Units and gain structure

Inside the amp, signals are *grid-normalised*: 1.0 ≈ the input swing at which a 12AX7-type stage
starts to conduct grid current. A triode stage's small-signal gain is its `swing` (≈ 40, i.e. plate
swing relative to the next grid's headroom). Because of that, the pots, tone-stack insertion loss and
dividers between stages determine where a model breaks up — exactly like a real preamp — instead of
an arbitrary "drive" multiplier. Gain pots use an audio taper (≈ 10 % at noon) with a floor.

### Triode stage (`TubeStage.h`)

```
x → coupling HPF (input cap / grid leak) → [cathode-bypass low shelf] → bias shift
  → static transfer f(v) → invert → plate/Miller LPF
f(v) = tanhR(v)                      v ≥ 0   (grid conduction: firm limit)
       -L · tanhR(-v / L)            v < 0   (cutoff: softer, later; L ≈ 1.4–1.7)
```

* `tanhR` is a C¹-continuous rational tanh approximation (exact ±1 at |x| ≥ 3).
* The asymmetry creates even harmonics that grow with level (measured H2/H3 > 0 dB at low gain on
  the vintage models).
* **Bias shift / blocking:** grid conduction charges the coupling cap (2 ms attack, 80 ms recovery),
  pushing the operating point colder. It produces level-dependent asymmetry, "bloom" and the sag of
  sustained notes; bounded so it cannot run away.
* Stages invert, so asymmetric clipping alternates between half-waves through a cascade.
* Partially bypassed cathodes are modelled as a low shelf (less gain below the corner) — the classic
  way high-gain preamps keep lows out of the distortion.
* **Cathode follower** (British, tweed, heavy): unity-gain buffer that clips softly and early on
  positive swings (limited grid current) — the compressed, chewy upper-mid character of
  British-style crunch.

### Tone stack (`ToneStack.cpp`)

The passive treble/bass/middle network has a closed-form 3rd-order transfer function in terms of its
component values and pot positions (Yeh & Smith, DAFx 2006). It is evaluated per model from original
component sets and discretised by a generic bilinear transform of the 3rd-order polynomial.
Knob interaction is therefore physical: mid changes overall level, bass does little with treble at 0,
and noon is not flat (≈ -6 dB lows/highs, -11 dB mids for the vintage set). The bass pot uses an audio
taper. Coefficients are recomputed (cheap) whenever smoothed knob values change. Tests compare the
digital response with the analogue model (≤ 0.05 dB at ≥ 96 kHz) and check stability at extreme settings.

### Power amp (`PowerAmp.*`)

```
depth peak (≈ 80–90 Hz) → presence high shelf (≈ 3.5–4.5 kHz)      // reduced-NFB shaping
→ phase inverter: gentle symmetric saturation
→ sag: input scaled by 1/(1 + sag·env), env = rectified output (4 ms attack, 90–180 ms release)
→ push-pull output stage: soft knee y = v/(1+|v|^h)^(1/h) (tabulated), small offset → slight even content
→ output transformer HPF (40–55 Hz) and LPF (7.5–11 kHz)
→ speaker-load interaction: +4·k dB at ~105 Hz and +3.5·k dB high shelf, k = 0.3 (high NFB) … 0.7 (low NFB)
```

The knee hardness stands in for negative feedback (more NFB → harder, later clipping). Tube-rectifier
voicings sag more and recover more slowly than silicon ones.

**Sag and Variac (POWER section).** Sag scales the voicing's sag depth (`power.sag` 0.5 = calibrated,
0 = stiff supply, 1 = twice the sag). Variac `v` (0.6–1.1) models the supply voltage: the output
stage's clipping level follows the supply, so the input is scaled by `1/v^1.3` (earlier breakup),
the output swing by `v^1.3`, partly level-compensated by `v^-0.7`; a lower supply also sags more
(`sag / sqrt(v)`).

### Valve selection (`Valves.h`)

Every voicing has "usual" valves (Clean 12AX7/6L6, Tweed 12AY7/6V6, Plexi and Lead 12AX7/EL34,
Cascade Lead and Heavy 12AX7/6L6). A different choice is applied *relative* to those, so the default
selection is bit-identical to the calibrated amp (tested):
* **Preamp valve (V1):** scales the first triode's swing (gain ratio), grid headroom and cutoff
  softness; level compensated by `1/sqrt(gain ratio)`.
* **Power valve:** knee hardness ratio, drive ratio (earlier/later breakup), push-pull asymmetry,
  sag sensitivity and a gentle tilt (120 Hz shelf, 800 Hz peak, 3 kHz shelf, differences in dB);
  level compensated by `1/sqrt(drive ratio)`. All knee tables are built in `prepare()`, so switching
  is real-time safe; the engine still fades out/in around a valve change to avoid clicks.

**PULL:** multiplies the gain pot by up to ×3.16 (+10 dB), smoothed over 30 ms.

**Controls on every model:** each voicing has Presence, Resonance (Depth) and Bright. Voicings that
did not have them get a 100 Hz resonance peak up to +6 dB and a 2 kHz/+6 dB bright cap; their
factory presets keep them at neutral settings, so the calibrated sound is unchanged.

### Voicings

The owner's line-up (private build: the names say which classic amp each voicing is modelled on;
the circuits are built from this engine's blocks, not copied schematics).

| Model | Preamp chain | Power section | Character |
|---|---|---|---|
| Tweed | V1 → Volume → V2 (slightly cold) → CF → tweed TMB | 6V6, low NFB, tube-rectifier sag | Touch-sensitive early breakup, loose and warm |
| JC120 | Linear "V1" (5x headroom) → TMB → Volume → linear "V2" | Solid-state: very hard knee, no sag, high NFB, 15 kHz top | Huge clean headroom; clips abruptly only when cranked |
| AC30 | V1 → CF → Top Boost stack (1M/1M, 56 pF) → Volume → V2 | EL84, no NFB (soft knee, follows the speaker), rectifier sag | Chime, early sweet power-amp breakup |
| Plexi | V1 → Volume (bright cap) → V2 → CF → stack | Four EL34, modest NFB, driven hard (+6 dB) | Open, chewy power-valve crunch |
| JCM800 | V1 (cathode shelf) → Pre-amp → cold V2 → V3 → CF → British TMB | EL34 | Cold-clipped cascade, tight lows, bite |
| Coliseum | V1 → TMB (*before the gain*) → Drive → V2..V4 cascade | 6L6, stiff, lots of headroom | Singing lead, vocal mids; pairs with the graphic EQ |
| MP1 | V1 (110 Hz input HP) → Drive → V2..V3 → scooped stack → make-up stage | Clean, stiff 6L6 (rack power amp) | Tight, scooped, saturated 80s rack rhythm |
| Triple | V1 → Gain → cold V2..V4 → CF → scooped stack (bass reaches the gain stages; only the cold clipper is a little tight) | 6L6, low NFB, rectifier sag | Thick, saturated, huge low end, scooped mids |
| Dual | As Triple, with less mid scoop and a slightly tighter clipper stage | Four 6L6 (100 W): more sag, earlier phase-inverter and power breakup | Rawer, grindier, saggier than the Triple |

Every model has every control (Presence, Resonance and Bright included); valve selectors are applied
relative to each model's usual valves (Tweed 12AY7/6V6, JC120 12AT7/KT88, AC30 12AX7/EL84,
JCM800 12AX7/EL34, Coliseum/MP1/Triple/Dual 12AX7/6L6).

The default cabinet differs per model (used for a fresh start or a missing IR); switching models leaves the cabinet alone and resets the valve selectors to the new model's usual valves (loading a preset does not).

**Level balance.** `ampforge_render --models` plays the synthetic DI through every model at the
same knobs and cabinet. Each voicing's `outputTrimDb` puts Gain 5 at about -21 dB (loudness proxy)
for every model. Volume-knob amps (Tweed, AC30, JC120) also have `volumeMakeupDb`, an output-only
make-up that is full at Volume 0 and 0 dB at 10. It changes level, not breakup. The JC120 has
`gainRangeScale` 0.35 because a linear amp's 30 dB pot range would otherwise be a 30 dB level range.
Clean amps still get louder than high-gain ones with very hot input (they don't compress); that is
inherent, not a calibration error.

### Measured behaviour (220 Hz sine, cab off, `ampforge_measure`, 48 kHz, 8x)

THD (%) at two input levels — *-24 dBFS ≈ single coil, -12 dBFS ≈ hot humbucker, hard picking*:

| Model | gain 1 | 3 | 5 | 7 | 10 | (input) |
|---|---|---|---|---|---|---|
| Lacquer Clean | 0.0 | 0.0 | 0.1 | 0.4 | 7.3 | -24 dBFS |
|  | 0.7 | 0.7 | 1.4 | 7.4 | 31 | -12 dBFS |
| Lacquer Tweed | 0.4 | 1.0 | 3.0 | 9.2 | 24 | -24 |
| Plexi | 0.1 | 0.3 | 1.2 | 5.0 | 22 | -24 |
|  | 0.8 | 2.6 | 9.1 | 22 | 36 | -12 |
| Albion Lead | 0.4 | 2.5 | 11 | 23 | 33 | -24 |
| Cascade Lead | 0.4 | 1.5 | 6.0 | 22 | 40 | -24 |
| Cascade Heavy | 0.2 | 3.2 | 15 | 27 | 34 | -24 |

Reading: the clean model stays clean up to ~7 with single coils; tweed and Plexi break up
progressively with volume and much earlier with humbuckers (pick- and pickup-sensitive); the
modern models sweep from almost clean to saturated across the gain range (an early version of
Cascade Heavy was already 13 % at gain 1, so an interstage divider and lower pot floor were added).
Sine THD is only a proxy — **listening tests are still required** (TEST_PLAN.md).

## Oversampling (`Oversampler.*`)

Cascaded 2x stages, each a pair of first-order all-pass chains forming an elliptic half-band
(polyphase IIR, coefficient design after the classic method also used in HIIR). Stage 1: 8 coefficients,
transition 0.04 (≈ 100 dB stopband, flat to 20 kHz at 44.1 kHz); later stages 6 / 4 coefficients.
Phase delay at 1 kHz: **5.05 samples at 48 kHz/8x (0.105 ms)**, 4.5 samples at 96 kHz/4x.

Measured inharmonic (alias + noise) energy, Cascade Heavy at gain 8, no cab, 48 kHz:

| Input tone | 2x | 4x | 8x |
|---|---|---|---|
| 330 Hz | -70 dB | -69 dB | -69 dB (measurement floor) |
| 1 kHz | -34 dB | -54 dB | -64 dB |
| 3.1 kHz | -29 dB | -47 dB | -71 dB |

**Decision:** Automatic = 8x at 44.1/48 kHz, 4x at 88.2/96 kHz, 2x above (processing rate ≈ 350–400 kHz).
4x was the original plan but leaves high-gain lead notes' alias products only ~45–55 dB down; 8x
costs about +2.5 % CPU (benchmark below) for a 10–25 dB improvement. 2x/4x remain selectable.

## Graphic EQ (`GraphicEQ.*`)

Five RBJ peaking sections (Q 0.9 ≈ 1.5 octaves) in series at 80/240/750/2200/6600 Hz.
Because neighbouring bands overlap, raw slider gains would over-shoot when several bands move together.
The designer pre-computes the 5×5 band-interaction matrix (dB at each centre per dB of each band),
inverts it, solves for section gains that hit the slider values at the centres, then applies one
refinement pass for the non-linearity of dB responses. Tests: single bands within 0.35 dB, combined
curves (e.g. +12 on all, alternating ±12) within 1 dB at the centres, flat = exactly flat, measured
sine responses match the design within 0.2 dB. Section gains are clamped to ±20 dB.

Default position is between preamp and power amp (inside the oversampled section), the classic
place for this kind of EQ in a high-gain amp: it shapes what drives the power stage. "Post Amp"
places it before the cabinet. The UI curve is computed with the same designer.

## Cabinet convolution (`Convolver.*`, `CabinetSection.*`)

* **Zero latency:** the first 64 taps run as a direct-form FIR per sample (4 accumulators); taps
  64… are uniformly partitioned (64-sample partitions, FFT size 128, overlap-save with a
  frequency-domain delay line). Partition *j ≥ 1* only needs input at least one block old, so its
  contribution for the next block is computed as soon as a block of 64 inputs completes.
  Matches direct convolution within 2·10⁻⁵ for IR lengths 1…4801 and odd host block sizes.
* Spectra are stored as split real/imaginary arrays so the multiply-accumulate vectorises
  (SSE/NEON); this cut long-IR cost by about a third.
* **IR preparation (off the audio thread):** Kaiser-windowed sinc resampling (β = 9, 32 zero-crossings,
  cutoff 0.97 × min(Nyquists)) to the engine rate; truncation at 1 s with a 5 ms fade (reported to the
  user); level normalisation so the average power response over 100 Hz–5 kHz is 0 dB (one gain for both
  channels of a stereo IR), so switching IRs changes tone, not loudness.
* **Switching:** new convolvers are published to a mailbox and crossfaded in over 30 ms with an
  equal-gain raised-cosine (outputs of two cabinets fed by the same guitar are strongly correlated, and
  the flat start hides the new convolver's empty history). Tested for discontinuities with a
  phase-inverted, delayed IR.
* **Channels:** a mono IR gives identical L/R; a stereo IR convolves the mono guitar with each IR
  channel. Dual mode runs two independent slots (each mono or stereo) with level and equal-power pan.
  A stereo IR is not treated as two cabinets.
* Bypass crossfades to the dry signal.

### Factory IRs

Generated by `Tools/IRGenerator.cpp` from physically motivated magnitude responses (speaker/box
high-pass at resonance, open-back dipole roll-off or closed-back bump, cone break-up peaks, steep
break-up roll-off, smoothed irregularity, mic-position shelving, early reflections for the room
variant), converted to minimum phase via the real cepstrum. Honest summary: plausible cabinet
*approximations*, not measurements of real cabinets.

## Effects

* **Overdrive** (oversampled): clipping path high-passed at 720 Hz and limited by diode-like soft
  clipping at 0.6 V-equivalent, summed with the dry signal (keeps lows clean), then tone low-pass
  600 Hz–6 kHz and level ±12 dB. At drive 0 it is a tightening mid boost.
* **Noise gate:** peak detector on the clean input, 6 dB hysteresis, 40 ms hold, 0.8 ms attack,
  user release (10–600 ms), -80 dB floor; gain applied after the amp where the hiss is. Test: hiss
  between notes attenuated by > 80 dB relative to the note.
* **Micro-pitch:** two splice-type pitch shifters (+13 / -13 cents) fed with the mono cab output,
  panned left / right and added to the dry signal at the Mix level. Each shifter reads a delay line
  with two taps whose delays ramp across a 30 ms window (5–35 ms), crossfaded with sin² windows that
  sum to 1. Measured average shift over seven notes: +13.4 / -13.4 cents (individual notes within
  ~1 cent; splice shifters slip a fraction of a cycle at each tap hand-over). The dry path is
  untouched; only the pitched copies are delayed.
* **Chorus / flanger:** two cubic-Hermite modulated delay lines, LFOs in quadrature for stereo width.
  Chorus: 7–11 ms, no feedback. Flanger: 0.3–4.3 ms sweep with soft-limited feedback (0.6).
  Mode changes fade the wet path out and in over 10 ms (tested click-free).
* **Delay:** stereo, up to 2.5 s, tempo sync (8 note divisions), feedback through a variable
  low-pass (800 Hz–12 kHz) and fixed 90 Hz high-pass with soft limiting, time changes glide over 250 ms.
* **Reverb:** 4 series all-pass diffusers → 8-line FDN with orthonormal Hadamard feedback,
  per-line one-pole damping (tone), RT60-derived gains, slow modulation of two lines; 12 ms pre-delay.
* **Output limiter:** instant-attack peak gain computer (150 ms release, no look-ahead to add no
  latency) and a final hard clamp. Factory presets peak at -4 dBFS or lower so it should not act.

## Parameter smoothing

Linear ramps in the plain/knob domain (20–50 ms depending on the parameter), advanced per 32-sample
control block; gains are interpolated per sample within each block; filter coefficients are
recomputed from smoothed knob values (frequency/gain/Q are never interpolated as raw coefficients).
Test: jumping gain, master, bass, output level or an EQ band from 10 % to 90 % produces no larger
second-difference "click" than the steady signal after it.

## Performance (`ampforge_bench`)

Measured on the development machine: **Intel Xeon @ 2.1 GHz cloud VM, single thread, -O3 SSE2,
Linux**, final configuration (automatic oversampling: 8x at 48 kHz, 4x at 96 kHz), 64-sample blocks.
% of the real-time block budget (lower is better):

| Case | 48 kHz mean | 48 kHz p99 | 96 kHz mean | 96 kHz p99 |
|---|---|---|---|---|
| Clean, 85 ms IR, no FX | 4.3 % | 7.7 % | 7.1 % | 12.6 % |
| Plexi, 85 ms IR, no FX | 4.7 % | 7.4 % | 6.1 % | 9.2 % |
| Heavy, 85 ms IR, no FX | 5.4 % | 7.5 % | 7.5 % | 15.2 % |
| Heavy, 85 ms IR, all FX + gate + drive + EQ | 6.6 % | 8.3 % | 8.4 % | 12.3 % |
| Heavy, same, forced 2x | 3.0 % | 6.6 % | — | — |
| Heavy, dual **stereo** 500 ms IRs, all FX | 11.0 % | 15.4 % | 28.6 % | 47.8 % |
| Heavy, 1 s IR (max), all FX | 10.2 % | 17.9 % | 17.8 % | 29.4 % |

Typical use (one ≤ 200 ms cab IR) is therefore well under 10 % of one core on this VM; an Apple
Silicon core is expected to be faster, but this has not been measured.

Worst-case spikes on a shared VM (tens to hundreds of %) were dominated by scheduler noise and are
not meaningful; structural burstiness exists because the FFT tail work for each 64-sample partition
happens in one sample period (p99/mean ≈ 2 at 32-sample blocks). **Re-run on the target Mac.**

## Known trade-offs / limitations

* Models are "circuit-inspired, musically calibrated" — not component-level circuit simulation
  (no WDF/nodal solver, no plate-load dynamics beyond the bias-shift model).
* Power-amp feedback is approximated with filters + knee hardness, not a closed loop.
* The tone stack loads nothing (ideal source/load), and stage interactions are not loaded by the
  following stage.
* IR length cap 1 s; long IRs at 96 kHz in dual stereo cost ~25 % of a core on the test VM.
* Non-uniform partitioning (larger partitions for late IR parts) would reduce long-IR cost further.

## Studio Comp models (`Effects/ClassicCompressor.h`)

One stereo-linked gain computer, with the behaviour that distinguishes each classic unit:

| Model | Detector / topology | Ballistics | Character |
|---|---|---|---|
| Pristine | peak, feed-forward | attack 1–30 ms, release 30–750 ms | clean, soft knee, ratio 2–20:1 |
| LA-2A | peak, feedback (opto) | ~10 ms attack; two-stage release: half in ~60 ms, the rest over 0.5–5 s depending on how long it has worked | very soft knee; Compress / Limit; side-chain HF emphasis; even-harmonic valve make-up |
| 1176 | peak, feedback (FET) | 20–800 µs attack, 50 ms–1.1 s release | Input drives a fixed threshold; 4/8/12/20 and All (harder grab, more distortion); odd FET distortion grows with GR |
| SSL Bus | peak, feed-forward VCA | stepped 0.1–30 ms attack, 0.1–1.2 s release or Auto (program-dependent) | 2/4/10:1, firm knee, clean |
| Fairchild 670 | peak, feed-forward vari-mu | TC1–TC4 fixed (0.3–5 s release), TC5/TC6 program-dependent (slow store up to 10/25 s) | 22 dB knee (ratio grows with level), valve colour |
| dbx 160 | true RMS, feed-forward VCA | fast attack, constant 120 dB/s release | hard knee or OverEasy |

Amount 0 is bit-identical to bypass on every model; default-setting output levels are matched within
±1.5 dB; model changes fade out, switch while silent and fade back in. These are behavioural models
built from the units' published characteristics, not circuit simulations.

## Studio reverbs (`Effects/StudioReverbs.*`)

**Lexicon** — the figure-eight tank published by Lexicon's designer (Griesinger's structure as
documented by Dattorro): input band-limit and four diffusers, two cross-coupled branches of modulated
all-pass → delay → damping → decay → all-pass → delay, seven output taps per side. Spin/Wander
modulate the branch all-passes (sine, or smoothed random for Random Hall); Bass Mult/Crossover set
a separate low-frequency decay; Size scales every delay. Programs: Concert Hall, Random Hall, Rich
Plate, Chamber.

**Bricasti** — true stereo: a size-scaled 12-tap early-reflection pattern per channel feeds a
16-line late network (Householder feedback) with three-band decay (bass multiplier below the
crossover, treble decay as a fraction of Decay above 4 kHz) and only subtle modulation. Programs:
Large Hall, Small Room, Plate, Chamber.

Both are original implementations of well-documented structures voiced after the published
character of those units, not copies of their proprietary algorithms. Tests check stability, RT60
against the Decay setting (Schroeder decay curve), level against the Hall, and that Bass Mult
lengthens the low-frequency tail.

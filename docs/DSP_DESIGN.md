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

### Voicings

| Model | Family | Preamp chain | Character / controls |
|---|---|---|---|
| Lacquer Clean | American | V1 → TMB (250k/250k/10k/100k, 250p/100n/47n) → Volume (+bright cap) → V2 | Big headroom, scooped; NFB power amp; Volume, Treble, Middle, Bass, Master, Bright |
| Lacquer Tweed | American | V1 → Volume → V2 (slightly cold) → CF → TMB (250k/1M/25k/56k, 250p/20n/20n) | Early breakup, loose, warm; tube-rectifier sag; + Presence |
| Albion Plexi | British | V1 (cathode shelf) → Volume (+bright) → V2 → CF → TMB (220k/1M/25k/33k, 470p/22n/22n) | Crunch grows with volume, upper-mid push; EL34-style softer knee; + Presence, Bright |
| Albion Lead | British | V1 (tight shelf) → Gain → cold V2 → divider → V3 → CF → TMB | Cascaded classic-rock gain, tight lows |
| Cascade Lead | Modern | V1 → TMB (*before the gain*) → Drive → cold V2 → V3 → divider → V4 → [EQ] | Treble/bass shape what distorts (a hallmark of this design family); vocal mids; Presence, Depth |
| Cascade Heavy | Modern | V1 (tight) → Gain → cold V2 → divider → V3 → divider → V4 → CF → TMB → [EQ] | Modern high gain with firm lows, deep resonance (Depth up to +9 dB) |

The default cabinet differs per model; switching models moves the cab with it unless you chose a different IR.

### Measured behaviour (220 Hz sine, cab off, `ampforge_measure`, 48 kHz, 8x)

THD (%) at two input levels — *-24 dBFS ≈ single coil, -12 dBFS ≈ hot humbucker, hard picking*:

| Model | gain 1 | 3 | 5 | 7 | 10 | (input) |
|---|---|---|---|---|---|---|
| Lacquer Clean | 0.0 | 0.0 | 0.1 | 0.4 | 7.3 | -24 dBFS |
|  | 0.7 | 0.7 | 1.4 | 7.4 | 31 | -12 dBFS |
| Lacquer Tweed | 0.4 | 1.0 | 3.0 | 9.2 | 24 | -24 |
| Albion Plexi | 0.1 | 0.3 | 1.2 | 5.0 | 22 | -24 |
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

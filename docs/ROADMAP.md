# Roadmap

## Next (highest priority first)

1. **Build and verify on macOS** (Apple Silicon first): `Scripts/build_macos.sh`, fix any
   platform-specific compile issues, run the tests, smoke-launch, check the microphone permission
   flow and Core Audio device handling.
2. **Listening sessions with real guitars** (TEST_PLAN.md manual section) and iterate the voicings:
   gain taper, breakup points, presence/depth ranges, tone-stack values, power-amp sag, gate defaults.
   Use `ampforge_measure` / `ampforge_render` to keep changes measurable.
3. **Measure latency and CPU on the target Mac**; set performance budgets; if long IRs are needed at
   96 kHz, implement non-uniform partitioning (64-sample head, larger partitions for the tail computed
   across several blocks).
4. **Captured cabinet IRs:** license or record a real IR set (with documented rights) to complement the
   synthetic factory IRs.
5. Workflow: MIDI clock for delay sync, effect re-ordering, undo for IR changes,
   per-preset MIDI program assignment, preset import/export, IR folders.
6. Code signing and notarisation (Developer ID), installer/DMG, update mechanism.

## Requested features (owner)

Status: items 1–14 are implemented (see CHANGELOG); 15–17 are next.

1. **Single fixed row of amp controls** instead of the model buttons + per-model faceplate:
   * first knob: a **switched (detented) model selector** across the six models;
   * then Gain, Bass, Middle, Treble, Presence, Resonance/Depth, Master (and Bright) —
     **always visible and always active for every model** (e.g. Resonance also works on the
     clean model). Engine impact: every voicing must implement every control; today some
     controls are fixed per model (`AmpVoicing::controls` mask, `fixedPresence`, `depthMaxDb = 0`),
     so each voicing needs musically sensible presence/depth/mid/bright ranges.
2. **"Pull for saturation" on the gain knob** (Peavey-style pull-boost): a latching extra-gain /
   saturation mode, e.g. click-to-pull on the knob plus a visible "PULLED" state, mapped to a new
   stable parameter (`amp.pull`) so presets and MIDI can use it.
3. **Power-amp section with two knobs at the end of the amp row:**
   * **Sag** — user control over supply sag depth/recovery (today a fixed per-model value in
     `PowerAmpSpec::sag` / `sagReleaseMs`);
   * **Variac** — lowers the simulated supply voltage: less headroom, earlier and softer power-amp
     breakup, slightly lower output and a "browner" response (scale power-amp drive/headroom, knee
     and sag together; level-compensate so it is not just a volume control).
   New parameter IDs (`power.sag`, `power.variac`), added to presets with defaults that reproduce
   today's sound.
4. **Valve selectors (drop-downs in the amp section):**
   * **Preamp valve:** e.g. 12AX7 / ECC83 (high gain), 5751 (~30 % less gain), 12AY7 (vintage,
     lower gain, earlier "soft" compression), 12AT7 (low gain, more headroom).
   * **Power valve:** e.g. 6L6 (big clean headroom, tight lows), EL34 (mid-forward, earlier
     breakup), 6V6 (small-amp sweetness, early breakup), EL84 (chimey, compressed), KT88/6550
     (huge headroom, very tight).
   * **Changing the amp model resets both to that model's usual valves** (Lacquer Clean 12AX7/6L6,
     Lacquer Tweed 12AY7/6V6, Plexi and Albion Lead 12AX7/EL34, Cascade Lead and Cascade Heavy
     12AX7/6L6); after that the user can swap freely. Stored in presets (`amp.preampValve`,
     `amp.powerValve`).
   * Engine: a preamp valve sets the stages' gain (mu-like `swing` scaling), headroom and transfer
     asymmetry; a power valve sets the output stage's knee hardness, headroom, asymmetry, sag
     interaction and a gentle frequency tilt. Output level is compensated so a swap changes
     character, not just loudness. Implemented as parameter overrides of `TriodeSpec` /
     `PowerAmpSpec`, so it combines with Sag/Variac (item 3).

5. **Graphic EQ: sliders only.** Remove the response-curve display (and any other cut/boost
   graphic); the five sliders and their dB readouts are the whole EQ.
6. **Collapsible sections.** Only the main amp controls are always visible. The other sections
   (currently the EQ/cabinet row and the effects row) are collapsed by default and expand/collapse
   with an arrow on their header. The open/closed state is remembered between launches; the window
   shrinks/grows (or the space is reused) accordingly.
7. **"Old-school amp head" look** for the main window:
   * cabinet-style head (tolex/piping/handle styling) instead of the flat panel look;
   * a **glass front panel with "Z.AMP" etched into it**, **edge-lit with purple LEDs**, so the
     etching glows purple when the amp is on (dims with Bypass / when audio is off);
   * **behind the glass, visible valves and transformers**;
   * **valve glow follows the amp model**: a subtle warm orange on the low-gain models, rising
     through brighter orange/yellow to **white-hot on the high-gain models** (Lacquer Clean →
     Lacquer Tweed → Plexi → Albion Lead → Cascade Lead → Cascade Heavy); could also track
     the Gain knob and later the valve selectors (item 4);
   * **transformers glow slightly on the modern high-gain models** (artistic licence).
   Implementation notes: drawn in code and/or original rendered artwork (no third-party assets);
   glow, etched-glass and LED-edge effects via gradients and pre-rendered blurred layers so it stays
   cheap; any animation (gentle flicker, glow transitions when switching models) runs at a modest
   frame rate on the UI thread only, never affecting audio. Knobs keep their current size and
   behaviour. Works at the minimum window size and on Retina displays.

8. **Photo-realistic amp interior** (done): valves, transformers, caps and chassis modelled
   and rendered in Blender Cycles (`Tools/Render3D/amp_interior.py`) as separate light layers
   (base, preamp heaters, power heaters per valve type, transformer glow, purple LED spill) that the
   app mixes in linear light, so heat colour, valve type and on/off stay live.
9. **New amp line-up** replacing the six current models (owner's working names; each a new
   voicing modelled on the circuit family it refers to):
   *Tweed*, *JC120* (solid-state clean, built-in stereo chorus character), *AC30* (EL84 class-A
   chime, Top Boost), *JCM800* (single-channel British hot-rodded lead), *Coliseum* (Mesa/Boogie
   high-power Mark-series: cascading lead preamp, 5-band GEQ, big 6L6 power section), *MP1* (rack MOSFET/valve preamp), *Triple* (modern triple-channel high gain).
   Private build: the owner's names are used as-is (they are trademarks of their makers).
10. **Knob edge lighting follows the model:** the ring light around the control knobs shifts from
    cool green (cleanest model) through to orange (highest gain).
11. **Selectable drive types:** *RAT* (op-amp hard clipping, Filter control) and *Screamer*
    (mid-hump soft clipping), selected in the Drive module (`drive.type`).
12. **Selectable reverb types:** *Room*, *Hall*, *Spring*, *Chamber* (`reverb.type`).
13. **Delay type selector:** *Studio* (clean digital) and *Tape* (wow/flutter, saturation,
    darkening repeats) (`delay.type`).
14. **Compressors** — a *CS-3* style pedal before the amp and a *Studio* compressor after the
    cabinet — and an **auto-wah**.
15. **Interior perspective: parts close behind the glass.** Done: the chassis deck now ends at
    the glass, the valves stand ~9 cm behind it with the back wall ~10 cm behind them and side
    walls, the camera is ~45 cm in front of the glass with a 33.5 mm lens (real perspective), and
    the parts fill the window. A wide (short) window keeps the deck and valve bases and trims the
    top; a tall window zooms in.
16. **Deeper etched logo.** Done: "Z.AMP" is drawn as a V-groove cut into the glass — shaded upper
    walls, lit lower walls, a bright top lip and a frosted floor; with the LEDs on the cut surfaces
    glow.
17. **Valves look like the selected type.** Done: each power type has its own bottle and parts
    (6L6 tall straight; EL34 taller, brown base, black plates, halo getter with a clear dome; 6V6
    "coke bottle"; EL84 9-pin miniature with exhaust tip; KT88 big and shouldered with bright
    plates), and V1 follows the preamp selector (12AX7 / 5751 black plates, three micas / 12AY7
    long plates / 12AT7 ribbed bright plates, each with its name printed) as a patch over the base
    and glow layers, so the combinations do not multiply the renders.

## AU and VST3 plugins

The engine was built for this from the start; what exists today and what is still needed:

**Already in place**
* `AmpForgeEngine` is free of JUCE, device, file and UI code; it processes host blocks of any size,
  supports mono input/stereo output and in-place buffers, and has a non-RT `prepare()`.
* Stable string parameter IDs with ranges, defaults and normalised mappings (`Parameters.h`) — usable
  directly as plugin parameter IDs.
* Complete state serialisation (`PresetCodec` JSON + IR references), versioned.
* Click-free state changes (`beginTransition`), IR preparation off the audio thread, MIDI handling
  through `MidiController` (raw bytes, no device code).

**Steps to create the plugin targets**
1. In `CMakeLists.txt`, add `juce_add_plugin(AmpForgePlugin FORMATS AU VST3 IS_SYNTH FALSE
   NEEDS_MIDI_INPUT TRUE PLUGIN_MANUFACTURER_CODE … PLUGIN_CODE … COPY_PLUGIN_AFTER_BUILD TRUE …)`
   and link `ampforge_dsp`, the core sources and `juce_audio_processors`.
2. Write `Source/Plugin/PluginProcessor` (`juce::AudioProcessor`):
   * create one `juce::AudioParameterFloat/Bool/Choice` per `ParamDef`, **same IDs**, using
     `normalisedToPlain`/`plainToNormalised` via a `NormalisableRange` with custom conversion lambdas;
     group them like the UI;
   * in `processBlock`: copy host parameter values into the engine's `ParameterStore` (or have the
     parameters write to it via listeners — it is lock-free), feed MIDI bytes to `MidiController`
     (CC/PC), call `engine.process(in, outL, outR, n)` with input channel 0 (or a mono sum) and the
     host's output buffers;
   * `prepareToPlay` → `engine.prepare(sampleRate, maxBlock)`; `releaseResources` → `releaseResources()`;
   * `getStateInformation` / `setStateInformation` → `PresetCodec::toJson(captureState(engine))` /
     `fromJson` + `applyState` (IR loading on a background thread; never in `processBlock`);
   * report `getLatencySamples() = 0` (the engine adds no buffering; IIR phase delay is not latency
     a host can compensate meaningfully);
   * buses: mono→stereo (default), stereo→stereo (use left or sum), mono→mono (sum output).
3. Editor: reuse the panels (they talk to `AppController`). Factor an `EngineContext` interface out of
   `AppController` (params, undo, presets, IR library, MIDI) so the plugin editor can implement it
   without standalone device code; drop the audio/MIDI device dialogs in the plugin.
4. Host automation: notify the host when the UI changes a parameter
   (`beginChangeGesture/setValueNotifyingHost/endChangeGesture`) instead of writing the store directly.
5. Garbage collection: call `engine.collectGarbage()` from a message-thread timer in the editor *and*
   from `setStateInformation`/a background timer so it runs without an editor.
6. Validate: `auval -v aufx Afge Afge` (AU), Steinberg VST3 validator, pluginval; test in Logic Pro,
   Ableton Live, Reaper, Cubase; automation, state recall, sample-rate changes, offline bounce,
   variable block sizes, mono/stereo tracks.
7. Licensing: VST3 SDK is bundled with JUCE under its own terms (GPLv3 or Steinberg's proprietary
   licence — sign the Steinberg VST3 licence agreement for closed-source distribution); AU needs no
   extra licence.

Nothing above has been built yet; plugin support should not be claimed until step 6 passes.

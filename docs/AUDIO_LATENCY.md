# Audio latency

What you feel when you play is the **round-trip latency**: the time from the string moving to the
sound leaving your monitors. Z.AMP controls only a small part of it.

## Contributors

| Stage | Typical size | Who controls it |
|---|---|---|
| Interface A/D converter (anti-alias filter) | 0.2–0.7 ms | hardware |
| Driver / USB / Thunderbolt transfer and safety buffers | 0.5–3 ms | driver (Core Audio) |
| **Input buffer** | buffer size / sample rate (64 @ 48 kHz = 1.33 ms) | you (Audio Settings) |
| **Z.AMP processing** | ≈ 0.1 ms phase delay (oversampling filters) + 0 ms buffering | Z.AMP |
| **Output buffer** | buffer size / sample rate | you |
| Interface D/A converter (reconstruction filter) | 0.2–0.7 ms | hardware |
| Distance to the speaker | ~0.03 ms per cm (1 m ≈ 2.9 ms) | you |

Z.AMP itself adds **no buffering**:

* The engine processes whatever block the driver delivers, sample-accurate, in internal control
  blocks of ≤ 32 samples (no internal FIFO).
* Cabinet convolution is zero-latency (direct-form head + partitioned tail, see DSP_DESIGN.md).
* The only delay inside the engine is the phase delay of the minimum-phase IIR oversampling filters:
  **≈ 5 samples at 48 kHz with 8x (≈ 0.1 ms)**, ≈ 4.5 samples at 96 kHz/4x (≈ 0.05 ms). Like an
  analogue filter this is frequency dependent (larger close to 20 kHz) and is not reported to hosts as
  plugin latency.
* Amp and tone-stack filters are ordinary IIR filters, like the analogue circuits they model.

The status bar shows the device, sample rate, buffer size (and its duration) and — when the driver
reports it — the driver's own input+output latency added to two buffers. Core Audio drivers usually
report their converter and safety-offset latencies; this estimate is only as accurate as the driver.

## Recommendations

* 48 kHz (or 44.1 kHz) with **64 samples** is a good starting point on a modern Mac: roughly
  3–5 ms round trip on a good interface. Try 32 if your interface supports it and there are no
  dropouts; use 128 if you hear clicks.
* Higher sample rates reduce buffer time per sample count but double the CPU load; Z.AMP lowers
  its oversampling factor automatically at 88.2/96 kHz so total processing rate stays similar.
* Close other audio applications; disable Wi-Fi/Bluetooth scanning if you get occasional dropouts.

## Measuring the real round-trip latency

The only trustworthy number is a loopback measurement on your own hardware:

1. Connect a cable from an interface **output** to the guitar **input** (keep levels low).
2. In Z.AMP, enable **Bypass** (dry path) — or use a clean preset with the cab off.
3. Record or play a sharp click (e.g. with a DAW or `afplay` on a second output) and record both the
   output and the looped-back input in a DAW running on another interface/channel, then measure the
   offset between the two transients — or use a tool such as *RTL Utility* (Oblique Audio).
4. Subtract the DAW's own measured loopback latency if it shares the interface.

Do this at several buffer sizes. Z.AMP has **not** been measured on real hardware yet (the
development environment had no audio interface); please record your results and the interface model
when you test.

## Things that are not latency problems

* A "soft" or delayed attack on high-gain sounds is usually compression/sag, not latency.
* Monitoring through the interface's own direct-monitor mix *and* Z.AMP at the same time causes
  comb filtering (a phasey sound): turn the interface's direct monitoring off.

// ZackAMP — post-cabinet time-based effects: chorus and delay (stereo).
#pragma once

#include "../Utilities/DspCommon.h"
#include "../Utilities/Filters.h"

#include <vector>

namespace af::dsp
{
/** Fractional delay line with cubic Hermite interpolation. Allocation only in prepare(). */
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + 4)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        write = 0;
    }

    void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    void push (float x) noexcept
    {
        buffer[(size_t) write] = x;
        write = (write + 1) & mask;
    }

    /** Delay measured from the most recently pushed sample (>= 1). */
    float read (float delay) const noexcept
    {
        delay = clampv (delay, 1.0f, (float) (mask - 3));
        const int d = (int) delay;
        const float f = delay - (float) d;
        const int i1 = (write - d) & mask; // delay d
        const float y0 = buffer[(size_t) ((i1 + 1) & mask)];
        const float y1 = buffer[(size_t) i1];
        const float y2 = buffer[(size_t) ((i1 - 1) & mask)];
        const float y3 = buffer[(size_t) ((i1 - 2) & mask)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

private:
    std::vector<float> buffer;
    int mask = 0, write = 0;
};

/** Stereo modulation block (one effect at a time):
    Chorus: 7–11 ms delay, LFOs in quadrature, no feedback.
    Flanger: 0.3–4.3 ms sweep with soft-limited feedback.
    Script Phaser / Phase 90: four-stage all-pass phaser (the classic orange-pedal topology), the
    break frequency swept exponentially ~150 Hz – 2.6 kHz; "Script" without feedback (smooth,
    subtle), "Phase 90" with feedback (deeper notches, more pronounced "swoosh").
    Switching mode fades the wet signal out, changes the mode while silent, and fades back in. */
class Chorus
{
public:
    void prepare (double fs)
    {
        sampleRate = fs;
        for (auto& d : lines)
            d.prepare ((int) (0.05 * fs));
        fadeStep = (float) (1.0 / (0.010 * fs));
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : lines)
            d.reset();
        phase = 0.0;
        feedback[0] = feedback[1] = 0.0f;
        for (auto& stages : allpass)
            for (auto& st : stages)
                st = {};
        modeFade = 1.0f;
    }

    enum Mode { chorus = 0, flanger = 1, scriptPhaser = 2, phase90 = 3 };

    /** rate Hz, depth 0..1, mix 0..1 (block-constant; the engine smooths them). */
    void process (float* l, float* r, int n, float rateHz, float depth, float mix, int mode) noexcept
    {
        const double inc = rateHz / sampleRate;
        float* ch[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            // Mode change: fade the wet path out, swap mode with clean state, fade back in.
            if (mode != activeMode)
            {
                modeFade -= fadeStep;
                if (modeFade <= 0.0f)
                {
                    // Wet path is silent now: swap modes. The delay lines keep their continuous
                    // input history (clearing them would make the signal re-enter as a step).
                    modeFade = 0.0f;
                    activeMode = mode;
                    feedback[0] = feedback[1] = 0.0f;
                    for (auto& stages : allpass)
                        for (auto& st : stages)
                            st = {};
                }
            }
            else if (modeFade < 1.0f)
                modeFade = std::min (1.0f, modeFade + fadeStep);

            if (activeMode >= scriptPhaser)
            {
                // One LFO for both channels (the pedal is mono); exponential sweep.
                const float lfo = 0.5f * (1.0f + (float) std::sin (kTwoPi * phase));
                const float hz = 150.0f * std::pow (2.6f + 15.0f * depth, lfo);
                const float t = (float) std::tan (kPi * std::min ((double) hz, 0.45 * sampleRate) / sampleRate);
                const float a = (t - 1.0f) / (t + 1.0f);
                const float fb = activeMode == phase90 ? 0.45f : 0.0f;
                for (int c = 0; c < 2; ++c)
                {
                    float v = ch[c][i] + fb * feedback[c];
                    for (auto& st : allpass[c])
                    {
                        const float y = a * v + st.x1 - a * st.y1;
                        st.x1 = v;
                        st.y1 = y;
                        v = y;
                    }
                    feedback[c] = tanhRational (v);
                    // The classic phaser is a 50/50 blend of dry and phase-shifted signal.
                    ch[c][i] += 0.5f * mix * modeFade * (v - ch[c][i]);
                }
                phase += inc;
                if (phase >= 1.0)
                    phase -= 1.0;
                continue;
            }

            const bool activeFlanger = activeMode == flanger;
            const float baseMs = activeFlanger ? 0.3f : 7.0f;
            const float depthMs = 4.0f * depth;
            const float fbAmount = activeFlanger ? 0.6f : 0.0f;
            for (int c = 0; c < 2; ++c)
            {
                const float lfo = (float) std::sin (kTwoPi * (phase + 0.25 * c)); // quadrature: stereo width
                const float d = std::max (1.0f, (baseMs + depthMs * 0.5f * (1.0f + lfo)) * 0.001f * (float) sampleRate);
                lines[c].push (ch[c][i] + fbAmount * feedback[c]);
                const float wet = lines[c].read (d);
                feedback[c] = tanhRational (wet);
                // Equal-ish loudness dry/wet blend; these effects are normally used as a blend.
                ch[c][i] = ch[c][i] * (1.0f - 0.5f * mix * modeFade) + wet * mix * 0.7f * modeFade;
            }
            phase += inc;
            if (phase >= 1.0)
                phase -= 1.0;
        }
    }

private:
    double sampleRate = 48000.0, phase = 0.0;
    DelayLine lines[2];
    float feedback[2] {};
    float modeFade = 1.0f, fadeStep = 0.002f;
    int activeMode = chorus;
    struct AllPassState { float x1 = 0.0f, y1 = 0.0f; };
    AllPassState allpass[2][4];
};

class StereoDelay
{
public:
    static constexpr double kMaxSeconds = 2.5;

    void prepare (double fs)
    {
        sampleRate = fs;
        for (auto& d : lines)
            d.prepare ((int) (kMaxSeconds * fs) + 8);
        for (auto& h : hp)
            h.prepare (fs, OnePole::Type::highPass, 90.0);
        for (auto& t : tapeLp)
            t.setCoeffs (BiquadCoeffs::lowPass (fs, 4200.0, 0.6));
        for (auto& h : tapeHp)
            h.prepare (fs, OnePole::Type::highPass, 160.0);
        timeSmooth.prepare (fs, 0.25); // smooth time changes glide like tape rather than click
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : lines)
            d.reset();
        for (auto& f : lp)
            f.reset();
        for (auto& h : hp)
            h.reset();
        for (auto& t : tapeLp)
            t.reset();
        for (auto& h : tapeHp)
            h.reset();
        wowPhase = flutterPhase = 0.0;
        timeSmooth.snapToTarget();
    }

    void setTone (float hz)
    {
        if (std::abs (hz - lastTone) < 1.0f)
            return;
        lastTone = hz;
        for (auto& f : lp)
            f.setCoeffs (BiquadCoeffs::lowPass (sampleRate, hz, 0.6));
    }

    void snapTime (float ms) noexcept { timeSmooth.snap (ms); }

    /** Delay with feedback; wet signal is added to l/r. inputGain ramps to 0 when the
        effect is switched off so existing repeats decay naturally.
        tape = true: wow and flutter on the playback head, and each pass through the "tape" is
        band-limited and saturated, so repeats get darker, thicker and smear slightly. */
    void process (float* l, float* r, int n, float timeMs, float feedback, float mix, float inputGain, bool tape = false) noexcept
    {
        timeSmooth.setTarget (timeMs);
        feedback = clampv (feedback, 0.0f, 0.95f);
        const double wowInc = 0.45 / sampleRate, flutterInc = 6.3 / sampleRate;
        for (int i = 0; i < n; ++i)
        {
            float d = std::max (1.0f, timeSmooth.next() * 0.001f * (float) sampleRate);
            if (tape)
            {
                wowPhase += wowInc;
                flutterPhase += flutterInc;
                wowPhase -= std::floor (wowPhase);
                flutterPhase -= std::floor (flutterPhase);
                const float wow = (float) std::sin (kTwoPi * wowPhase), flutter = (float) std::sin (kTwoPi * flutterPhase);
                d = std::max (1.0f, d * (1.0f + 0.0022f * wow + 0.0006f * flutter) + 0.0004f * (float) sampleRate * wow);
            }
            const float wl = lines[0].read (d);
            const float wr = lines[1].read (d);
            // Feedback path is filtered (darker repeats) and soft-limited so it never runs away.
            const float fbL = tanhRational (hp[0].process (lp[0].process (wl)) * feedback);
            const float fbR = tanhRational (hp[1].process (lp[1].process (wr)) * feedback);
            float inL = l[i] * inputGain + fbL, inR = r[i] * inputGain + fbR;
            if (tape) // record head: band-limited, gently saturated
            {
                inL = 0.8f * tanhRational (1.25f * tapeLp[0].process (tapeHp[0].process (inL)));
                inR = 0.8f * tanhRational (1.25f * tapeLp[1].process (tapeHp[1].process (inR)));
            }
            lines[0].push (inL);
            lines[1].push (inR);
            l[i] += wl * mix;
            r[i] += wr * mix;
        }
    }

private:
    double sampleRate = 48000.0;
    DelayLine lines[2];
    Biquad lp[2], tapeLp[2];
    OnePole hp[2], tapeHp[2];
    LinearSmoother timeSmooth;
    double wowPhase = 0.0, flutterPhase = 0.0;
    float lastTone = -1.0f;
};

} // namespace af::dsp

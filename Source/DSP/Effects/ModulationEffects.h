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

/** Stereo chorus / flanger (one modulated delay line per channel, LFOs in quadrature).
    Chorus: 7–11 ms delay, no feedback. Flanger: 0.3–4.3 ms sweep with soft-limited feedback.
    Switching mode fades the wet signal out, changes the delay range while silent, and fades back in. */
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
        modeFade = 1.0f;
    }

    /** rate Hz, depth 0..1, mix 0..1 (block-constant; the engine smooths them). */
    void process (float* l, float* r, int n, float rateHz, float depth, float mix, bool flanger) noexcept
    {
        const double inc = rateHz / sampleRate;
        float* ch[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            // Mode change: fade the wet path out, swap mode with clean state, fade back in.
            if (flanger != activeFlanger)
            {
                modeFade -= fadeStep;
                if (modeFade <= 0.0f)
                {
                    // Wet path is silent now: swap modes. The delay lines keep their continuous
                    // input history (clearing them would make the signal re-enter as a step).
                    modeFade = 0.0f;
                    activeFlanger = flanger;
                    feedback[0] = feedback[1] = 0.0f;
                }
            }
            else if (modeFade < 1.0f)
                modeFade = std::min (1.0f, modeFade + fadeStep);

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
    bool activeFlanger = false;
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
        effect is switched off so existing repeats decay naturally. */
    void process (float* l, float* r, int n, float timeMs, float feedback, float mix, float inputGain) noexcept
    {
        timeSmooth.setTarget (timeMs);
        feedback = clampv (feedback, 0.0f, 0.95f);
        for (int i = 0; i < n; ++i)
        {
            const float d = std::max (1.0f, timeSmooth.next() * 0.001f * (float) sampleRate);
            const float wl = lines[0].read (d);
            const float wr = lines[1].read (d);
            // Feedback path is filtered (darker repeats) and soft-limited so it never runs away.
            const float fbL = tanhRational (hp[0].process (lp[0].process (wl)) * feedback);
            const float fbR = tanhRational (hp[1].process (lp[1].process (wr)) * feedback);
            lines[0].push (l[i] * inputGain + fbL);
            lines[1].push (r[i] * inputGain + fbR);
            l[i] += wl * mix;
            r[i] += wr * mix;
        }
    }

private:
    double sampleRate = 48000.0;
    DelayLine lines[2];
    Biquad lp[2];
    OnePole hp[2];
    LinearSmoother timeSmooth;
    float lastTone = -1.0f;
};

} // namespace af::dsp

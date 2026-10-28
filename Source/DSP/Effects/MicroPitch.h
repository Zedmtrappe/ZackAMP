// ZackAMP — "micro-pitch" doubler: copies of the signal shifted a few cents up and down.
//
// Each shifter is a delay line read by two taps whose delays ramp linearly (a sawtooth),
// which resamples the signal by a constant ratio. When one tap's ramp wraps around, its
// gain is zero (sin^2 windows offset by half a period sum to exactly 1), so the wrap is
// inaudible. For a few cents the ramp is very slow (a 13-cent shift sweeps a 30 ms window
// about every 4 s), so there is no audible modulation. Only the wet copies are delayed
// (5–35 ms, the classic doubler range); the dry signal is untouched.
#pragma once

#include "ModulationEffects.h"

namespace af::dsp
{
class PitchShifter
{
public:
    void prepare (double fs)
    {
        window = (float) (0.030 * fs);
        minDelay = (float) (0.005 * fs);
        line.prepare ((int) (window + minDelay) + 8);
        reset();
    }

    void reset() noexcept
    {
        line.reset();
        phase = 0.0;
    }

    /** Shift in cents (positive = up). Takes effect immediately; meant to be set once. */
    void setCents (double cents) noexcept
    {
        const double ratio = std::pow (2.0, cents / 1200.0);
        up = ratio >= 1.0;
        phaseInc = std::abs (ratio - 1.0) / (double) window;
    }

    float process (float x) noexcept
    {
        line.push (x);
        float y = 0.0f;
        for (double q : { phase, phase + 0.5 })
        {
            if (q >= 1.0)
                q -= 1.0;
            // Shorter delay over time = faster read = pitch up; longer = pitch down.
            const float d = minDelay + (float) (up ? (1.0 - q) : q) * window;
            const float s = (float) std::sin (kPi * q);
            y += s * s * line.read (d);
        }
        phase += phaseInc;
        if (phase >= 1.0)
            phase -= 1.0;
        return y;
    }

private:
    DelayLine line;
    float window = 1440.0f, minDelay = 240.0f;
    double phase = 0.0, phaseInc = 0.0;
    bool up = true;
};

/** Stereo micro-pitch: +cents on the left, -cents on the right, added to the dry signal. */
class MicroPitch
{
public:
    static constexpr double kCents = 13.0;

    void prepare (double fs)
    {
        shiftUp.prepare (fs);
        shiftDown.prepare (fs);
        shiftUp.setCents (kCents);
        shiftDown.setCents (-kCents);
    }

    void reset() noexcept
    {
        shiftUp.reset();
        shiftDown.reset();
    }

    /** Adds the pitched copies to l/r. mixFrom/mixTo ramp the wet level across the block. */
    void process (float* l, float* r, int n, float mixFrom, float mixTo) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float mix = mixFrom + (mixTo - mixFrom) * (float) (i + 1) / (float) n;
            const float mono = 0.5f * (l[i] + r[i]);
            l[i] += mix * shiftUp.process (mono);
            r[i] += mix * shiftDown.process (mono);
        }
    }

private:
    PitchShifter shiftUp, shiftDown;
};
} // namespace af::dsp

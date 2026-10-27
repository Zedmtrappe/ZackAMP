// ZackAMP — noise gate.
//
// The detector listens to the clean guitar input (where note decays are still clearly
// above the noise), while the gain is applied after the amp (where high-gain hiss is
// generated). Hysteresis between open/close thresholds and a hold time prevent chatter
// on decaying notes; release is user-controlled so sustain is not chopped.
#pragma once

#include "../Utilities/DspCommon.h"

namespace af::dsp
{
class NoiseGate
{
public:
    void prepare (double fs)
    {
        sampleRate = fs;
        detAttack = onePoleCoeff (0.0002, fs);
        detRelease = onePoleCoeff (0.025, fs);
        gainAttack = onePoleCoeff (0.0008, fs);
        holdSamples = (int) (0.040 * fs);
        setControls (-60.0f, 120.0f);
        reset();
    }

    void reset() noexcept
    {
        env = 0.0f;
        gain = 1.0f;
        open = true;
        holdCounter = 0;
    }

    void setControls (float thresholdDb, float releaseMs)
    {
        openThresh = dbToGain (thresholdDb);
        closeThresh = dbToGain (thresholdDb - 6.0f); // 6 dB hysteresis
        gainRelease = onePoleCoeff (std::max (5.0f, releaseMs) * 0.001, sampleRate);
    }

    /** Feed one detector sample; returns the gain to apply to the corresponding output sample. */
    float next (float detector) noexcept
    {
        const float a = std::abs (detector);
        env = a > env ? a + detAttack * (env - a) : a + detRelease * (env - a);

        if (env > openThresh)
        {
            open = true;
            holdCounter = holdSamples;
        }
        else if (env < closeThresh)
        {
            if (holdCounter > 0)
                --holdCounter;
            else
                open = false;
        }

        constexpr float floorGain = 0.0001f; // -80 dB
        const float target = open ? 1.0f : floorGain;
        const float c = target > gain ? gainAttack : gainRelease;
        gain = target + c * (gain - target);
        return gain;
    }

    bool isOpen() const noexcept { return open; }

private:
    double sampleRate = 48000.0;
    float env = 0.0f, gain = 1.0f;
    float openThresh = 0.001f, closeThresh = 0.0005f;
    float detAttack = 0, detRelease = 0, gainAttack = 0, gainRelease = 0;
    int holdSamples = 0, holdCounter = 0;
    bool open = true;
};

} // namespace af::dsp

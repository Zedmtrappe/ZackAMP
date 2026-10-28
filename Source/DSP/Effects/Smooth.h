// Z.AMP — "Smooth": a dynamic 2 kHz tamer (single-band compressor), after the cabinet.
//
// A constant-0-dB-peak band-pass at 2 kHz (Q 1) both detects and removes the harsh band:
//   y = x - (1 - g) * bandpass(x)
// so with g = 1 the signal is untouched and with g < 1 the 2 kHz region is cut by g (a smooth
// peaking-style dip) while the rest of the spectrum passes. The band level is followed by a
// peak envelope (3 ms attack, 120 ms release); above the threshold the band is compressed 3:1,
// at most 9 dB. The knob sets the threshold (clockwise = lower = more smoothing).
#pragma once

#include "../Utilities/DspCommon.h"
#include "../Utilities/Filters.h"

#include <atomic>

namespace af::dsp
{
class Smooth
{
public:
    static constexpr float kRatio = 3.0f;
    static constexpr float kMaxCutDb = 9.0f;
    static constexpr double kCentreHz = 2000.0;

    void prepare (double fs)
    {
        sampleRate = fs;
        const auto c = BiquadCoeffs::bandPass (fs, kCentreHz, 1.0);
        for (auto& b : band)
            b.setCoeffs (c);
        attack = onePoleCoeff (0.003, fs);
        release = onePoleCoeff (0.120, fs);
        gainSmooth = onePoleCoeff (0.002, fs);
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : band)
            b.reset();
        env = 0.0f;
        gain = 1.0f;
        reductionDb.store (0.0f, std::memory_order_relaxed);
    }

    /** Threshold in dBFS of the 2 kHz band for knob 0..1: -12 dB (gentle) .. -48 dB (heavy). */
    static float thresholdDb (float amount) noexcept { return -12.0f - 36.0f * clampv (amount, 0.0f, 1.0f); }

    /** In-place on both channels. `enable` ramps 0..1 across the block (0 = transparent). */
    void process (float* l, float* r, int n, float amount, float enableFrom, float enableTo) noexcept
    {
        const float thr = thresholdDb (amount);
        float maxCut = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float bl = band[0].process (l[i]);
            const float br = band[1].process (r[i]);
            const float level = std::max (std::abs (bl), std::abs (br));
            env = level + (level > env ? attack : release) * (env - level);
            const float levelDb = gainToDb (env + 1e-9f);
            float cutDb = std::max (0.0f, (levelDb - thr) * (1.0f - 1.0f / kRatio));
            const float enable = enableFrom + (enableTo - enableFrom) * (float) (i + 1) / (float) n;
            cutDb = std::min (cutDb, kMaxCutDb) * enable;
            const float target = dbToGain (-cutDb);
            gain = target + gainSmooth * (gain - target);
            l[i] -= (1.0f - gain) * bl;
            r[i] -= (1.0f - gain) * br;
            maxCut = std::max (maxCut, -gainToDb (gain));
        }
        reductionDb.store (maxCut, std::memory_order_relaxed);
    }

    /** Current gain reduction in dB (for the "process" light; read from any thread). */
    float getReductionDb() const noexcept { return reductionDb.load (std::memory_order_relaxed); }

private:
    double sampleRate = 48000.0;
    Biquad band[2];
    float env = 0.0f, gain = 1.0f, attack = 0.0f, release = 0.0f, gainSmooth = 0.0f;
    std::atomic<float> reductionDb { 0.0f };
};
} // namespace af::dsp

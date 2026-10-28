// Z.AMP — compressor, two types:
//
// Studio — clean feed-forward compressor: soft knee (6 dB), 4:1, attack 1–30 ms, 150 ms release,
//          automatic make-up gain, gentle tilt tone. Runs after the cabinet (stereo-linked, like
//          a mix-bus compressor). Amount 0 is identical to off: the threshold rises to 0 dBFS and
//          the whole effect (tone and level included) fades out over the bottom 8% of the knob.
// CS-3   — pedal-style OTA compressor/sustainer: near-limiting ratio, the Sustain knob lowers the
//          threshold and raises the make-up together, Attack lets more or less of the pick through,
//          a slower release that "breathes", and a Tone shelf to restore the top it squashes.
//
// The detector runs on the signal in dBFS (`detectScale` converts the engine's internal units).
#pragma once

#include "../Utilities/DspCommon.h"
#include "../Utilities/Filters.h"

#include <atomic>

namespace af::dsp
{
class Compressor
{
public:
    enum Type { studio = 0, cs3 = 1 };

    void prepare (double fs)
    {
        sampleRate = fs;
        detAttack = onePoleCoeff (0.0003, fs);
        lastTone = -1.0f;
        wetFrom = wetTo = 1.0f;
        setControls (studio, 0.5f, 0.5f, 0.5f, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        env = 0.0f;
        grDb = 0.0f;
        for (auto& t : tone)
            t.reset();
    }

    /** sustain, attack, toneKnob 0..1; level in dB. Call at control rate. */
    void setControls (int newType, float sustain, float attack, float toneKnob, float levelDb)
    {
        type = newType;
        sustain = clampv (sustain, 0.0f, 1.0f);
        attack = clampv (attack, 0.0f, 1.0f);
        if (type == cs3)
        {
            thresholdDb = -14.0f - 34.0f * sustain;
            ratio = 12.0f;
            kneeDb = 3.0f;
            attackCoeff = onePoleCoeff (0.001 * std::pow (40.0, (double) attack), sampleRate); // 1..40 ms
            releaseCoeff = onePoleCoeff (0.35, sampleRate);
            detRelease = onePoleCoeff (0.06, sampleRate);
            makeupDb = -thresholdDb * (1.0f - 1.0f / ratio) * 0.55f;
        }
        else
        {
            thresholdDb = -40.0f * sustain;
            ratio = 4.0f;
            kneeDb = 6.0f;
            attackCoeff = onePoleCoeff (0.001 * std::pow (30.0, (double) attack), sampleRate); // 1..30 ms
            releaseCoeff = onePoleCoeff (0.15, sampleRate);
            detRelease = onePoleCoeff (0.03, sampleRate);
            makeupDb = -thresholdDb * (1.0f - 1.0f / ratio) * 0.5f;
        }
        outGain = dbToGain (levelDb + makeupDb);
        wetFrom = wetTo;
        wetTo = type == cs3 ? 1.0f : clampv (sustain / 0.08f, 0.0f, 1.0f);
        if (std::abs (toneKnob - lastTone) > 1e-4f || type != lastType)
        {
            lastTone = toneKnob;
            lastType = type;
            const double db = (type == cs3 ? 16.0 : 8.0) * ((double) toneKnob - 0.5); // +/-8 dB or +/-4 dB
            const double hz = type == cs3 ? 2500.0 : 3000.0;
            for (auto& t : tone)
                t.setCoeffs (BiquadCoeffs::highShelf (sampleRate, hz, 0.6, db));
        }
    }

    /** Mono, in place; `mix` ramps 0..1 across the block (0 = bypassed). */
    void process (float* x, int n, float detectScale, float mixFrom, float mixTo) noexcept
    {
        float maxGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float g = gainFor (std::abs (x[i]) * detectScale);
            const float t = (float) (i + 1) / (float) n;
            const float m = (mixFrom + (mixTo - mixFrom) * t) * (wetFrom + (wetTo - wetFrom) * t);
            const float y = tone[0].process (x[i] * g);
            x[i] += m * (y - x[i]);
            maxGr = std::max (maxGr, grDb * m);
        }
        meterDb.store (maxGr, std::memory_order_relaxed);
        wetFrom = wetTo;
    }

    /** Stereo-linked, in place (post-cabinet position). */
    void process (float* l, float* r, int n, float detectScale, float mixFrom, float mixTo) noexcept
    {
        float maxGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float g = gainFor (std::max (std::abs (l[i]), std::abs (r[i])) * detectScale);
            const float t = (float) (i + 1) / (float) n;
            const float m = (mixFrom + (mixTo - mixFrom) * t) * (wetFrom + (wetTo - wetFrom) * t);
            const float yl = tone[0].process (l[i] * g), yr = tone[1].process (r[i] * g);
            l[i] += m * (yl - l[i]);
            r[i] += m * (yr - r[i]);
            maxGr = std::max (maxGr, grDb * m);
        }
        meterDb.store (maxGr, std::memory_order_relaxed);
        wetFrom = wetTo;
    }

    /** Largest gain reduction (dB) in the last processed block — for meters, any thread. */
    float getMeterDb() const noexcept { return meterDb.load (std::memory_order_relaxed); }
    void clearMeter() noexcept { meterDb.store (0.0f, std::memory_order_relaxed); }

private:
    float gainFor (float level) noexcept
    {
        env = level > env ? level + detAttack * (env - level) : level + detRelease * (env - level);
        const float over = gainToDb (env + 1e-9f) - thresholdDb;
        // Soft knee: quadratic blend across kneeDb around the threshold.
        float target = 0.0f;
        const float slope = 1.0f - 1.0f / ratio;
        if (over >= kneeDb * 0.5f)
            target = over * slope;
        else if (over > -kneeDb * 0.5f)
        {
            const float k = over + kneeDb * 0.5f;
            target = slope * k * k / (2.0f * kneeDb);
        }
        grDb = target > grDb ? target + attackCoeff * (grDb - target) : target + releaseCoeff * (grDb - target);
        return dbToGain (-grDb) * outGain;
    }

    double sampleRate = 48000.0;
    int type = studio, lastType = -1;
    float thresholdDb = -20.0f, ratio = 4.0f, kneeDb = 6.0f, makeupDb = 0.0f, outGain = 1.0f;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f, detAttack = 0.0f, detRelease = 0.0f;
    float env = 0.0f, grDb = 0.0f, lastTone = -1.0f;
    float wetFrom = 1.0f, wetTo = 1.0f; // amount fade (studio), ramped across each block
    Biquad tone[2];
    std::atomic<float> meterDb { 0.0f };
};

/** Envelope-controlled resonant band-pass ("auto-wah"): picking harder sweeps it higher. */
class AutoWah
{
public:
    void prepare (double fs)
    {
        sampleRate = fs;
        envAttack = onePoleCoeff (0.003, fs);
        envRelease = onePoleCoeff (0.09, fs);
        freqSmooth = onePoleCoeff (0.002, fs);
        reset();
    }

    void reset() noexcept
    {
        ic1 = ic2 = env = 0.0f;
        freq = 400.0f;
    }

    /** sens, range, peak, mix 0..1. In place; `enable` ramps 0..1 across the block. */
    void process (float* x, int n, float detectScale, float sens, float range, float peak, float mix,
                  float enableFrom, float enableTo) noexcept
    {
        const float fMin = 350.0f - 150.0f * range;
        const float fMax = std::min (1200.0f + 2000.0f * range, (float) (0.4 * sampleRate));
        const float q = 1.5f + 8.0f * clampv (peak, 0.0f, 1.0f);
        const float k = 1.0f / q;
        const float depth = 0.4f + 1.6f * clampv (sens, 0.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            const float level = std::abs (x[i]) * detectScale;
            env = level > env ? level + envAttack * (env - level) : level + envRelease * (env - level);
            // -48 dBFS (closed) .. about -12 dBFS (fully open), scaled by sensitivity.
            const float t = clampv ((gainToDb (env + 1e-9f) + 48.0f) / 36.0f * depth, 0.0f, 1.0f);
            const float target = fMin * std::pow (fMax / fMin, t);
            freq = target + freqSmooth * (freq - target);

            // Topology-preserving state-variable filter (Zavalishin).
            const float g = (float) std::tan (kPi * (double) freq / sampleRate);
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float v3 = x[i] - ic2;
            const float v1 = a1 * ic1 + g * a1 * v3;
            const float v2 = ic2 + g * v1;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            const float band = k * v1 * 2.0f; // unity-peak band-pass, +6 dB for presence
            const float wet = x[i] + mix * (band - x[i]);
            const float e = enableFrom + (enableTo - enableFrom) * (float) (i + 1) / (float) n;
            x[i] += e * (wet - x[i]);
        }
    }

private:
    double sampleRate = 48000.0;
    float ic1 = 0.0f, ic2 = 0.0f, env = 0.0f, freq = 400.0f;
    float envAttack = 0.0f, envRelease = 0.0f, freqSmooth = 0.0f;
};
} // namespace af::dsp

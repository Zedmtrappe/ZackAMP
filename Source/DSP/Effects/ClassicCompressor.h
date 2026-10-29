// Z.AMP — the Studio Comp after the cabinet, with six models of classic compressor behaviour.
//
// Every model shares one stereo-linked gain computer; they differ where the real units differ:
//
//   Pristine      clean feed-forward VCA, soft knee, ratio 2-20:1, attack 1-30 ms, release 30-750 ms.
//   LA-2A         optical: about 10 ms attack and the two-stage opto release (fast for the first
//                 half, then slow, and slower the longer it has been working); very soft knee,
//                 Compress about 3:1, Limit about 12:1; HF emphasis in the side chain; valve make-up.
//   1176          feedback FET: Input drives the signal into a fixed threshold, attack 20-800 us,
//                 release 50 ms-1.1 s, ratios 4/8/12/20 and "all buttons" (hard, pumping, extra
//                 distortion); FET distortion grows with gain reduction.
//   SSL Bus       feed-forward VCA, firm knee, ratios 2/4/10, stepped attack (0.1-30 ms) and release
//                 (0.1-1.2 s) or Auto (program-dependent), very clean.
//   Fairchild 670 variable-mu valve: very soft knee where the ratio rises with level, time constants
//                 TC1-TC6 (TC5/6 program-dependent), valve and transformer colour.
//   dbx 160       true-RMS detector, OverEasy or hard knee, fast attack and a constant 120 dB/s
//                 release.
//
// The knobs keep one meaning across models: Amount (clockwise = more compression), Attack
// (clockwise = slower), Tone (treble tilt after the compressor), Level (output). Amount 0 is
// identical to off on every model. The detector works on dBFS (`detectScale`).
#pragma once

#include "../Utilities/DspCommon.h"
#include "../Utilities/Filters.h"

#include <array>
#include <atomic>

namespace af::dsp
{
class ClassicCompressor
{
public:
    enum Model { studio = 0, la2a, fet1176, sslBus, fairchild, dbx160, numModels };

    struct Controls
    {
        int model = studio;
        float amount = 0.4f, attack = 0.5f, release = 0.5f, tone = 0.5f, levelDb = 0.0f;
        float mix = 1.0f, scHpfHz = 20.0f, emphasis = 0.0f;
        int ratio = 2;          // Studio / dbx: 2, 3, 4, 6, 10, 20 : 1
        int la2aMode = 0;       // 0 compress, 1 limit
        int ratio1176 = 0;      // 4, 8, 12, 20, all buttons
        int sslRatio = 1;       // 2, 4, 10
        bool sslAuto = false;
        int fairchildTc = 1;    // TC1..TC6
        bool overEasy = true;
    };

    void prepare (double fs)
    {
        sampleRate = fs;
        rmsCoeff = onePoleCoeff (0.008, fs);
        peakSmooth = onePoleCoeff (0.0002, fs);
        chargeCoeff = onePoleCoeff (0.4, fs);
        memRise = (float) (1.0 / (fs * 3.0));
        memFall = (float) (1.0 / (fs * 8.0));
        lastTone = lastHpf = lastEmph = -1.0f;
        lastModel = -1;
        wetFrom = wetTo = 1.0f;
        setControls (Controls {});
        wetFrom = wetTo;
        reset();
    }

    void reset() noexcept
    {
        env = ms = 0.0f;
        gr = grFast = grSlow = memory = 0.0f;
        fbL = fbR = 0.0f;
        for (auto* f : { &tone[0], &tone[1], &hpf[0], &hpf[1], &emph[0], &emph[1] })
            f->reset();
        dcL = dcR = 0.0f;
    }

    /** Call once per control block. */
    void setControls (const Controls& c)
    {
        model = c.model < 0 || c.model >= numModels ? studio : c.model;
        const float amount = clampv (c.amount, 0.0f, 1.0f);
        const float att = clampv (c.attack, 0.0f, 1.0f), rel = clampv (c.release, 0.0f, 1.0f);
        constexpr float kRatios[] = { 2.0f, 3.0f, 4.0f, 6.0f, 10.0f, 20.0f };
        feedback = false;
        allButtons = false;
        rmsDetector = false;
        programRelease = 0;
        linearReleaseDbPerSample = 0.0f;
        inputGainDb = 0.0f;
        satKind = 0;
        satAmount = 0.0f;
        switch (model)
        {
            case la2a:
                thresholdDb = -48.0f * amount;
                ratio = c.la2aMode == 1 ? 20.0f : 5.0f; // feedback: about 3:1 / 10:1 in effect
                kneeDb = c.la2aMode == 1 ? 8.0f : 14.0f;
                attackCoeff = onePoleCoeff (0.010, sampleRate);
                releaseCoeff = onePoleCoeff (0.060, sampleRate); // fast stage; slow stage below
                programRelease = 1;
                feedback = true;
                makeupDb = -thresholdDb * (1.0f - 1.0f / ratio) * 0.23f;
                satKind = 1;
                satAmount = 0.08f;
                break;
            case fet1176:
            {
                constexpr float k1176[] = { 4.0f, 8.0f, 12.0f, 20.0f, 20.0f };
                const int r = std::clamp (c.ratio1176, 0, 4);
                allButtons = r == 4;
                inputGainDb = 36.0f * amount;
                thresholdDb = allButtons ? -16.0f : -12.0f;
                ratio = k1176[r];
                kneeDb = allButtons ? 1.0f : (r == 0 ? 6.0f : 3.0f);
                attackCoeff = onePoleCoeff (0.00002 * std::pow (40.0, (double) att), sampleRate);
                releaseCoeff = onePoleCoeff (0.050 * std::pow (22.0, (double) rel), sampleRate);
                feedback = true;
                makeupDb = -0.52f * inputGainDb;
                satKind = 2;
                satAmount = allButtons ? 0.06f : 0.02f;
                break;
            }
            case sslBus:
            {
                constexpr float kSsl[] = { 2.0f, 4.0f, 10.0f };
                constexpr double kAtt[] = { 0.0001, 0.0003, 0.001, 0.003, 0.010, 0.030 };
                constexpr double kRel[] = { 0.1, 0.3, 0.6, 1.2 };
                thresholdDb = -36.0f * amount;
                ratio = kSsl[std::clamp (c.sslRatio, 0, 2)];
                kneeDb = 3.0f;
                attackCoeff = onePoleCoeff (kAtt[(size_t) std::lround (att * 5.0f)], sampleRate);
                releaseCoeff = onePoleCoeff (kRel[(size_t) std::lround (rel * 3.0f)], sampleRate);
                if (c.sslAuto)
                {
                    programRelease = 2; // fast 0.1 s + slow 1.2 s
                    releaseCoeff = onePoleCoeff (0.1, sampleRate);
                    slowCoeff = onePoleCoeff (1.2, sampleRate);
                }
                makeupDb = -thresholdDb * (1.0f - 1.0f / ratio) * 0.5f;
                break;
            }
            case fairchild:
            {
                // Attack / release per time constant; TC5 and TC6 are program-dependent.
                constexpr double kAtt[] = { 0.0002, 0.0002, 0.0004, 0.0004, 0.0004, 0.0002 };
                constexpr double kRel[] = { 0.3, 0.8, 2.0, 5.0, 2.0, 0.3 };
                constexpr double kSlow[] = { 0, 0, 0, 0, 10.0, 25.0 };
                const int tc = std::clamp (c.fairchildTc, 0, 5);
                thresholdDb = -40.0f * amount;
                ratio = 14.0f; // reached only far above the threshold: the knee is very wide
                kneeDb = 22.0f;
                attackCoeff = onePoleCoeff (kAtt[tc], sampleRate);
                releaseCoeff = onePoleCoeff (kRel[tc], sampleRate);
                if (kSlow[tc] > 0)
                {
                    programRelease = 2;
                    slowCoeff = onePoleCoeff (kSlow[tc], sampleRate);
                }
                makeupDb = -thresholdDb * 0.45f;
                satKind = 1;
                satAmount = 0.30f; // the valve gain stages are part of the sound
                break;
            }
            case dbx160:
                thresholdDb = -40.0f * amount;
                ratio = kRatios[std::clamp (c.ratio, 0, 5)];
                kneeDb = c.overEasy ? 12.0f : 0.5f;
                rmsDetector = true;
                attackCoeff = onePoleCoeff (0.004, sampleRate);
                linearReleaseDbPerSample = (float) (120.0 / sampleRate);
                makeupDb = -thresholdDb * (1.0f - 1.0f / ratio) * 0.17f;
                break;
            default: // studio
                thresholdDb = -40.0f * amount;
                ratio = kRatios[std::clamp (c.ratio, 0, 5)];
                kneeDb = 6.0f;
                attackCoeff = onePoleCoeff (0.001 * std::pow (30.0, (double) att), sampleRate);
                releaseCoeff = onePoleCoeff (0.030 * std::pow (25.0, (double) rel), sampleRate);
                makeupDb = -thresholdDb * (1.0f - 1.0f / ratio) * 0.5f;
                break;
        }
        // Peak detector: near-instant attack; its release is short so the gain computer's own
        // ballistics (above) shape the sound.
        detRelease = onePoleCoeff (model == fet1176 ? 0.004 : 0.03, sampleRate);
        outGain = dbToGain (c.levelDb + makeupDb);
        inGain = dbToGain (inputGainDb);
        mixKnob = clampv (c.mix, 0.0f, 1.0f);
        wetFrom = wetTo;
        wetTo = clampv (amount / 0.08f, 0.0f, 1.0f); // Amount 0 = identical to off

        if (std::abs (c.tone - lastTone) > 1e-4f || model != lastModel)
        {
            lastTone = c.tone;
            for (auto& t : tone)
                t.setCoeffs (BiquadCoeffs::highShelf (sampleRate, 3000.0, 0.6, 8.0 * ((double) c.tone - 0.5)));
        }
        if (std::abs (c.scHpfHz - lastHpf) > 0.5f)
        {
            lastHpf = c.scHpfHz;
            for (auto& h : hpf)
                h.setCoeffs (BiquadCoeffs::highPass (sampleRate, clampv ((double) c.scHpfHz, 20.0, 400.0), 0.7));
        }
        const float e = model == la2a ? c.emphasis : 0.0f;
        if (std::abs (e - lastEmph) > 1e-4f)
        {
            lastEmph = e;
            for (auto& h : emph)
                h.setCoeffs (BiquadCoeffs::highShelf (sampleRate, 2500.0, 0.7, 12.0 * (double) e));
        }
        useHpf = c.scHpfHz > 21.0f;
        useEmph = e > 0.001f;
        lastModel = model;
    }

    /** Stereo-linked, in place. `mix` ramps the whole effect in/out across the block (0 = bypassed). */
    void process (float* l, float* r, int n, float detectScale, float mixFrom, float mixTo) noexcept
    {
        float maxGr = 0.0f;
        if (programRelease == 1)
            la2aSlowCoeff = onePoleCoeffF (0.5f + 4.5f * memory);
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) (i + 1) / (float) n;
            const float m = (mixFrom + (mixTo - mixFrom) * t) * (wetFrom + (wetTo - wetFrom) * t);
            const float xl = l[i], xr = r[i];

            // Side chain: feed-forward models listen to the input, feedback models to their output.
            float sl = (feedback ? fbL : xl * inGain), sr = (feedback ? fbR : xr * inGain);
            if (useHpf) { sl = hpf[0].process (sl); sr = hpf[1].process (sr); }
            if (useEmph) { sl = emph[0].process (sl); sr = emph[1].process (sr); }
            const float level = std::max (std::abs (sl), std::abs (sr)) * detectScale;
            float det;
            if (rmsDetector)
            {
                ms = level * level + rmsCoeff * (ms - level * level);
                det = std::sqrt (ms);
            }
            else
            {
                env = level > env ? level + peakSmooth * (env - level) : level + detRelease * (env - level);
                det = env;
            }

            const float target = staticCurve (gainToDb (det + 1e-9f) - thresholdDb);
            updateGainReduction (target);

            const float g = dbToGain (-gr) * inGain;
            float yl = xl * g, yr = xr * g;
            saturate (yl, yr);
            fbL = yl; // feedback side chain: what comes out of the gain cell
            fbR = yr;
            yl = tone[0].process (yl * outGain);
            yr = tone[1].process (yr * outGain);
            // Parallel blend, then the bypass/amount fade.
            const float wl = xl + mixKnob * (yl - xl), wr = xr + mixKnob * (yr - xr);
            l[i] = xl + m * (wl - xl);
            r[i] = xr + m * (wr - xr);
            maxGr = std::max (maxGr, gr * m);
        }
        meterDb.store (maxGr, std::memory_order_relaxed);
        wetFrom = wetTo;
    }

    /** Largest gain reduction (dB) in the last block — for meters, any thread. */
    float getMeterDb() const noexcept { return meterDb.load (std::memory_order_relaxed); }
    void clearMeter() noexcept { meterDb.store (0.0f, std::memory_order_relaxed); }
    int getModel() const noexcept { return model; }

private:
    /** Gain reduction (dB) for `over` dB above the threshold: soft knee of kneeDb, then the ratio. */
    float staticCurve (float over) const noexcept
    {
        const float slope = 1.0f - 1.0f / ratio;
        if (over >= kneeDb * 0.5f)
            return over * slope;
        if (over <= -kneeDb * 0.5f)
            return 0.0f;
        const float k = over + kneeDb * 0.5f;
        return slope * k * k / (2.0f * kneeDb);
    }

    void updateGainReduction (float target) noexcept
    {
        if (programRelease == 1) // LA-2A: two-stage opto release with memory
        {
            if (target > gr)
            {
                gr = target + attackCoeff * (gr - target);
                grFast = grSlow = 0.5f * gr;
            }
            else
            {
                // Half of the reduction lets go in ~60 ms, the other half over 0.5-5 s depending on
                // how long the cell has been lit.
                grFast *= releaseCoeff;
                grSlow *= la2aSlowCoeff;
                const float sum = grFast + grSlow;
                if (sum < target)
                    grSlow += target - sum;
                gr = std::max (sum, target);
            }
            const float lit = gr > 2.0f ? 1.0f : 0.0f;
            memory += (lit - memory) * (lit > memory ? memRise : memFall);
        }
        else if (programRelease == 2) // program-dependent: fast after transients, slow after sustained compression
        {
            grFast = target > grFast ? target + attackCoeff * (grFast - target) : target + releaseCoeff * (grFast - target);
            grSlow = grFast > grSlow ? grFast + chargeCoeff * (grSlow - grFast) : grFast + slowCoeff * (grSlow - grFast);
            gr = std::max (grFast, grSlow);
        }
        else if (linearReleaseDbPerSample > 0.0f) // dbx: constant-rate release
        {
            gr = target > gr ? target + attackCoeff * (gr - target) : std::max (target, gr - linearReleaseDbPerSample);
        }
        else
        {
            gr = target > gr ? target + attackCoeff * (gr - target) : target + releaseCoeff * (gr - target);
            if (allButtons && target > gr + 6.0f)
                gr += 0.02f * (target - gr); // all buttons: grabs harder on big transients
        }
        gr = std::max (0.0f, gr);
    }

    void saturate (float& yl, float& yr) noexcept
    {
        if (satKind == 1) // valve / transformer: mostly even harmonics, DC removed
        {
            auto even = [this] (float y) { return y + satAmount * y * std::abs (y) * 0.5f; };
            yl = even (yl);
            yr = even (yr);
            dcL += 0.0005f * (yl - dcL);
            dcR += 0.0005f * (yr - dcR);
            yl -= dcL;
            yr -= dcR;
        }
        else if (satKind == 2) // FET: odd harmonics growing with gain reduction
        {
            const float k = satAmount * std::min (gr, 20.0f) / 10.0f;
            yl = yl / (1.0f + k * yl * yl);
            yr = yr / (1.0f + k * yr * yr);
        }
    }

    float onePoleCoeffF (float seconds) const noexcept { return (float) std::exp (-1.0 / ((double) seconds * sampleRate)); }

    double sampleRate = 48000.0;
    int model = studio, lastModel = -1;
    float thresholdDb = -20.0f, ratio = 4.0f, kneeDb = 6.0f, makeupDb = 0.0f, inputGainDb = 0.0f;
    float outGain = 1.0f, inGain = 1.0f, mixKnob = 1.0f;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f, slowCoeff = 0.0f, detRelease = 0.0f, peakSmooth = 0.0f, rmsCoeff = 0.0f;
    float linearReleaseDbPerSample = 0.0f;
    int programRelease = 0, satKind = 0;
    float satAmount = 0.0f;
    bool feedback = false, rmsDetector = false, allButtons = false, useHpf = false, useEmph = false;
    float env = 0.0f, ms = 0.0f, gr = 0.0f, grFast = 0.0f, grSlow = 0.0f, memory = 0.0f;
    float memRise = 0.0f, memFall = 0.0f, chargeCoeff = 0.0f, la2aSlowCoeff = 0.0f;
    float fbL = 0.0f, fbR = 0.0f, dcL = 0.0f, dcR = 0.0f;
    float lastTone = -1.0f, lastHpf = -1.0f, lastEmph = -1.0f;
    float wetFrom = 1.0f, wetTo = 1.0f;
    Biquad tone[2], hpf[2], emph[2];
    std::atomic<float> meterDb { 0.0f };
};
} // namespace af::dsp

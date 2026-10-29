// Objective audio-quality measurements: aliasing, DC, zipper noise, mono output.
#include <doctest.h>
#include "../DSP/TestSignals.h"
#include "../../Source/Audio/AmpForgeEngine.h"
#include "../../Source/DSP/Utilities/FFT.h"

using namespace af;

namespace
{
/** Ratio (dB) of energy that is not at a harmonic of f0 (i.e. aliasing + noise) to total. */
double inharmonicRatioDb (const std::vector<float>& x, double f0, double fs)
{
    const int n = 32768;
    dsp::RealFFT fft (n);
    std::vector<float> w ((size_t) n);
    const size_t start = x.size() - (size_t) n;
    for (int i = 0; i < n; ++i)
        w[(size_t) i] = x[start + (size_t) i] * (float) (0.5 - 0.5 * std::cos (dsp::kTwoPi * i / (n - 1)));
    std::vector<dsp::RealFFT::Complex> spec ((size_t) fft.getNumBins());
    fft.forward (w.data(), spec.data());
    double total = 0, inharmonic = 0;
    for (int k = 4; k < fft.getNumBins(); ++k)
    {
        const double f = k * fs / n;
        const double p = std::norm (spec[(size_t) k]);
        total += p;
        const double h = f / f0;
        if (std::abs (h - std::round (h)) * f0 > 12.0 * fs / n)
            inharmonic += p;
    }
    return 10.0 * std::log10 (inharmonic / total);
}

std::vector<float> renderHeavy (OversamplingMode mode, double f0, double fs)
{
    AmpForgeEngine e;
    e.setOversamplingMode (mode);
    e.params().set (pAmpModel, 9); // Triple: the highest-gain model
    e.params().set (pAmpGain, 0.8f);
    e.params().set (pCabOn, 0); // no cabinet: worst case, nothing hides aliasing
    e.prepare (fs, 256);
    auto x = test::sine (f0, fs, 65536, 0.25f);
    std::vector<float> l (x.size()), r (x.size());
    for (size_t p = 0; p < x.size(); p += 256)
        e.process (x.data() + p, l.data() + p, r.data() + p, 256);
    return l;
}
} // namespace

TEST_CASE ("Aliasing under heavy distortion decreases with oversampling")
{
    const double fs = 48000, f0 = 3101.0; // non-integer relation to fs so aliases land between harmonics
    const double a2 = inharmonicRatioDb (renderHeavy (OversamplingMode::x2, f0, fs), f0, fs);
    const double a4 = inharmonicRatioDb (renderHeavy (OversamplingMode::x4, f0, fs), f0, fs);
    const double a8 = inharmonicRatioDb (renderHeavy (OversamplingMode::x8, f0, fs), f0, fs);
    MESSAGE ("Inharmonic (alias) energy, 3.1 kHz into high gain, no cab: 2x " << a2 << " dB, 4x " << a4 << " dB, 8x " << a8 << " dB");
    CHECK (a4 < a2);
    CHECK (a8 < a4);
    CHECK (a8 < -60.0);

    // The automatic default at 48 kHz (8x) keeps lead-range aliasing below -60 dB.
    const double autoLead = inharmonicRatioDb (renderHeavy (OversamplingMode::automatic, 1001.0, fs), 1001.0, fs);
    MESSAGE ("Automatic mode, 1 kHz lead note: " << autoLead << " dB");
    CHECK (autoLead < -60.0);
}

TEST_CASE ("Output has no significant DC offset")
{
    for (int model = 0; model < dsp::kNumAmpModels; ++model)
    {
        AmpForgeEngine e;
        e.params().set (pAmpModel, (float) model);
        e.params().set (pAmpGain, 1.0f);
        e.params().set (pCabOn, 0);
        e.prepare (48000, 128);
        // Strongly asymmetric input (half-wave-ish) to provoke DC from bias shift.
        std::vector<float> x (96000);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = 0.4f * std::max (0.0f, (float) std::sin (dsp::kTwoPi * 150.0 * (double) i / 48000.0));
        std::vector<float> l (x.size()), r (x.size());
        for (size_t p = 0; p < x.size(); p += 128)
            e.process (x.data() + p, l.data() + p, r.data() + p, 128);
        double mean = 0;
        for (size_t i = 48000; i < l.size(); ++i) mean += l[i];
        mean /= 48000.0;
        INFO ("model " << model << " DC " << mean);
        CHECK (std::abs (mean) < 2e-3);
    }
}

TEST_CASE ("Fast knob changes are smoothed (no zipper steps)")
{
    for (int model : { 2, 4 }) // AC30, JCM800
    for (int param : { (int) pAmpGain, (int) pAmpMaster, (int) pAmpBass, (int) pOutLevel, (int) pEq750 })
    {
        AmpForgeEngine e;
        e.params().set (pAmpModel, (float) model);
        e.params().set (pCabOn, 0);
        e.params().set (pEqOn, 1);
        e.params().set (param, normalisedToPlain (param, 0.1f));
        e.prepare (48000, 64);
        auto x = test::sine (80.0, 48000, 48000, 0.1f);
        std::vector<float> l (x.size()), r (x.size());
        for (size_t p = 0; p < x.size(); p += 64)
        {
            if (p == 24000) // jump the control from 10% to 90% in one go
                e.params().set (param, normalisedToPlain (param, 0.9f));
            e.process (x.data() + p, l.data() + p, r.data() + p, 64);
        }
        // Largest second difference ("click energy") around the jump vs steady state after it.
        auto maxD2 = [&] (size_t a, size_t b)
        {
            float m = 0;
            for (size_t i = a + 2; i < b; ++i)
                m = std::max (m, std::abs (l[i] - 2 * l[i - 1] + l[i - 2]));
            return m;
        };
        const float around = maxD2 (23990, 26400);
        const float steady = maxD2 (40000, 48000);
        INFO ("model " << model << " " << std::string (paramDef (param).id) << " around " << around << " steady " << steady);
        // A smoothed ramp has small "corners" proportional to the signal level; an unsmoothed
        // step would produce a jump of the order of the level change itself (~100x larger).
        float level = 0;
        for (size_t i = 40000; i < l.size(); ++i)
            level = std::max (level, std::abs (l[i]));
        CHECK (around <= steady * 1.5f + 1e-4f + 2e-3f * level);
    }
}

TEST_CASE ("Mono output path (no right channel buffer)")
{
    AmpForgeEngine e;
    e.prepare (48000, 128);
    auto x = test::sine (220.0, 48000, 4800, 0.2f);
    std::vector<float> l (x.size());
    for (size_t p = 0; p < x.size(); p += 128)
        e.process (x.data() + p, l.data() + p, nullptr, (int) std::min<size_t> (128, x.size() - p));
    CHECK (test::allFinite (l));
    CHECK (test::peak (l, 2400) > 1e-3f);
}

TEST_CASE ("In-place processing: input buffer may alias the left output")
{
    AmpForgeEngine a, b;
    a.prepare (48000, 128);
    b.prepare (48000, 128);
    auto x = test::sine (196.0, 48000, 9600, 0.3f);
    std::vector<float> ref (x.size()), r1 (x.size()), buf = x, r2 (x.size());
    for (size_t p = 0; p < x.size(); p += 128)
    {
        a.process (x.data() + p, ref.data() + p, r1.data() + p, 128);
        b.process (buf.data() + p, buf.data() + p, r2.data() + p, 128);
    }
    for (size_t i = 0; i < x.size(); ++i)
        REQUIRE (buf[i] == doctest::Approx (ref[i]).epsilon (1e-6));
}

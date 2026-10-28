#include <doctest.h>
#include "TestSignals.h"
#include "../../Source/DSP/Effects/MicroPitch.h"

using namespace af;

/** Average frequency from zero crossings (what the ear tracks as pitch). */
static double meanFrequency (const std::vector<float>& x, double fs, size_t start)
{
    size_t first = 0, last = 0;
    int crossings = 0;
    for (size_t i = start + 1; i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            if (crossings == 0) first = i;
            last = i;
            ++crossings;
        }
    return (crossings - 1) / ((double) (last - first) / fs);
}

TEST_CASE ("Micro-pitch shifts by +13 cents left and -13 cents right")
{
    // Splice-based shifters slip a fraction of a cycle at each tap hand-over, so the pitch of a
    // single note can be off by up to ~0.25 Hz depending on its frequency; across notes it
    // averages out. Check every note within ~1.3 cents and the average within 0.6 cents.
    const double fs = 48000;
    double sumUp = 0, sumDown = 0;
    const double notes[] = { 196.0, 247.0, 330.0, 440.0, 587.0, 784.0, 1046.0 };
    for (double f0 : notes)
    {
        dsp::MicroPitch mp;
        mp.prepare (fs);
        auto x = test::sine (f0, fs, 48000 * 9, 0.3f);
        std::vector<float> l (x.size()), r (x.size()), wetL (x.size()), wetR (x.size());
        for (size_t p = 0; p < x.size(); p += 64)
        {
            for (size_t i = p; i < p + 64; ++i) { l[i] = x[i]; r[i] = x[i]; }
            mp.process (l.data() + p, r.data() + p, 64, 1.0f, 1.0f);
            for (size_t i = p; i < p + 64; ++i) { wetL[i] = l[i] - x[i]; wetR[i] = r[i] - x[i]; }
        }
        const double up = 1200.0 * std::log2 (meanFrequency (wetL, fs, 48000) / f0);
        const double down = 1200.0 * std::log2 (meanFrequency (wetR, fs, 48000) / f0);
        INFO (f0 << " Hz: " << up << " / " << down << " cents");
        CHECK (std::abs (up - 13.0) < 1.3);
        CHECK (std::abs (down + 13.0) < 1.3);
        CHECK (test::allFinite (l));
        sumUp += up;
        sumDown += down;
    }
    const double n = (double) std::size (notes);
    MESSAGE ("average shift over " << n << " notes: " << sumUp / n << " / " << sumDown / n << " cents");
    CHECK (std::abs (sumUp / n - 13.0) < 0.6); // measured ~ +0.4 cents: inaudible
    CHECK (std::abs (sumDown / n + 13.0) < 0.6);
}

TEST_CASE ("Micro-pitch at zero mix leaves the signal untouched")
{
    dsp::MicroPitch mp;
    mp.prepare (48000);
    auto x = test::sine (220.0, 48000, 4800, 0.3f);
    auto l = x, r = x;
    mp.process (l.data(), r.data(), (int) x.size(), 0.0f, 0.0f);
    for (size_t i = 0; i < x.size(); ++i) CHECK (l[i] == x[i]);
}

TEST_CASE ("Flanger mode is stable, differs from chorus, and switches without clicks")
{
    const double fs = 48000;
    auto x = test::sine (330.0, fs, 96000, 0.4f);
    auto run = [&] (bool flangerFirst, int switchAt)
    {
        dsp::Chorus c;
        c.prepare (fs);
        auto l = x, r = x;
        for (int p = 0; p < (int) x.size(); p += 64)
            c.process (l.data() + p, r.data() + p, 64, 0.5f, 1.0f, 1.0f, p >= switchAt ? ! flangerFirst : flangerFirst);
        return l;
    };
    const auto chorus = run (false, 1 << 30), flanger = run (true, 1 << 30);
    CHECK (test::allFinite (flanger));
    CHECK (test::peak (flanger) < 2.0f);
    double diff = 0;
    for (size_t i = 48000; i < x.size(); ++i) diff += std::pow (chorus[i] - flanger[i], 2);
    CHECK (diff / 48000.0 > 1e-3);

    // Mode switch mid-stream: no sample-to-sample jump bigger than the signal's own.
    const auto switched = run (false, 48000);
    float maxStep = 0, normalStep = 0;
    for (size_t i = 1; i < x.size(); ++i)
    {
        const float st = std::abs (switched[i] - switched[i - 1]);
        if (i > 47000 && i < 49200) maxStep = std::max (maxStep, st); // the 10 ms switch fade
        else if ((i > 20000 && i < 47000) || i > 60000) normalStep = std::max (normalStep, st); // steady chorus / flanger
    }
    CHECK (maxStep < normalStep * 1.5f);
}

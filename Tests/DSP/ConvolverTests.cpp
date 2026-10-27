#include <doctest.h>
#include "TestSignals.h"
#include "../../Source/DSP/Cabinet/CabinetSection.h"

#include <random>

using namespace af;

static std::vector<float> randomSignal (int n, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-1.f, 1.f);
    std::vector<float> v ((size_t) n);
    for (auto& x : v) x = d (rng);
    return v;
}

static std::vector<float> directConvolve (const std::vector<float>& x, const std::vector<float>& h)
{
    std::vector<float> y (x.size(), 0.0f);
    for (size_t n = 0; n < x.size(); ++n)
    {
        double acc = 0;
        for (size_t k = 0; k < h.size() && k <= n; ++k) acc += (double) h[k] * x[n - k];
        y[n] = (float) acc;
    }
    return y;
}

TEST_CASE ("Partitioned convolver matches direct convolution with zero latency")
{
    for (int irLen : { 1, 17, 64, 65, 200, 1000, 4801 })
    {
        auto h = randomSignal (irLen, 7u + (unsigned) irLen);
        for (auto& v : h) v *= 0.1f;
        auto x = randomSignal (6000, 99);
        auto ref = directConvolve (x, h);

        dsp::Convolver conv ({ h }, 48000.0);
        std::vector<float> y (x.size());
        // Irregular host block sizes, including 1-sample and non-multiples of the partition.
        int pos = 0, b = 0;
        const int sizes[] = { 1, 7, 64, 128, 33, 500, 2 };
        while (pos < (int) x.size())
        {
            const int n = std::min (sizes[b++ % 7], (int) x.size() - pos);
            conv.process (x.data() + pos, y.data() + pos, nullptr, n);
            pos += n;
        }
        double maxErr = 0;
        for (size_t i = 0; i < x.size(); ++i) maxErr = std::max (maxErr, (double) std::abs (y[i] - ref[i]));
        INFO ("IR length " << irLen);
        CHECK (maxErr < 2e-5);
    }
}

TEST_CASE ("Stereo IR produces independent channels")
{
    std::vector<float> hl (300, 0.0f), hr (300, 0.0f);
    hl[0] = 1.0f;
    hr[150] = 0.5f;
    dsp::Convolver conv ({ hl, hr }, 48000.0);
    CHECK (conv.getNumOutputs() == 2);
    std::vector<float> x (400, 0.0f), l (400), r (400);
    x[0] = 1.0f;
    conv.process (x.data(), l.data(), r.data(), 400);
    CHECK (l[0] == doctest::Approx (1.0f));
    CHECK (std::abs (r[0]) < 1e-6f);
    CHECK (r[150] == doctest::Approx (0.5f).epsilon (1e-4));
}

TEST_CASE ("Offline resampler preserves in-band tones")
{
    for (auto rates : { std::pair<double,double>{ 44100, 48000 }, { 96000, 48000 }, { 48000, 96000 }, { 192000, 44100 } })
    {
        auto x = test::sine (1000.0, rates.first, (int) rates.first / 2, 0.5f);
        auto y = dsp::resampleOffline (x, rates.first, rates.second);
        CHECK (std::abs ((double) y.size() - rates.second / 2) <= 1.0);
        const double a = test::toneAmplitude (y, 1000.0, rates.second, (int) y.size() / 4);
        CHECK (std::abs (test::db (a / 0.5)) < 0.1);
    }
}

TEST_CASE ("IR preparation truncates, normalises and stays finite")
{
    dsp::IRData ir;
    ir.sampleRate = 44100;
    ir.channels = { randomSignal (44100 * 3, 3) };
    for (size_t i = 0; i < ir.channels[0].size(); ++i) ir.channels[0][i] *= 5.0f * std::exp (-(float) i / 2000.0f);
    bool truncated = false;
    dsp::IRPrepareOptions opts;
    auto out = dsp::prepareImpulse (ir, 48000, opts, &truncated);
    CHECK (truncated);
    REQUIRE (out.size() == 1);
    CHECK (out[0].size() == 48000);
    CHECK (test::allFinite (out[0]));
    CHECK (std::abs (out[0].back()) < 1e-6f);
}

TEST_CASE ("Cabinet IR switching is crossfaded without discontinuities")
{
    dsp::GarbageCollector gc;
    dsp::CabinetSection cab (gc);
    const double fs = 48000;
    cab.prepare (fs, 256);

    auto makeIR = [&] (float delaySamples, float gain)
    {
        std::vector<float> h (512, 0.0f);
        h[(size_t) delaySamples] = gain;
        return std::make_unique<dsp::Convolver> (std::vector<std::vector<float>> { h }, fs);
    };
    cab.installImmediately (0, makeIR (0, 1.0f));

    dsp::CabinetSection::Settings s;
    auto x = test::sine (220.0, fs, 48000, 0.5f);
    std::vector<float> l (x.size()), r (x.size());
    bool published = false;
    for (int pos = 0; pos < (int) x.size(); pos += 128)
    {
        if (! published && pos >= 12000)
        {
            cab.publish (0, makeIR (100, -1.0f)); // phase-inverted, delayed: worst case for clicks
            published = true;
        }
        cab.process (x.data() + pos, l.data() + pos, r.data() + pos, 128, s);
    }
    // Largest sample-to-sample jump must stay near that of the sine itself.
    float maxStep = 0;
    for (size_t i = 1; i < l.size(); ++i) maxStep = std::max (maxStep, std::abs (l[i] - l[i - 1]));
    const float sineStep = 0.5f * (float) (dsp::kTwoPi * 220.0 / fs);
    CHECK (maxStep < sineStep * 1.6f);
    CHECK (test::allFinite (l));
    gc.collect();
}

TEST_CASE ("Cabinet bypass passes the dry signal")
{
    dsp::GarbageCollector gc;
    dsp::CabinetSection cab (gc);
    cab.prepare (48000, 64);
    std::vector<float> h (100, 0.0f); h[50] = 1.0f;
    cab.installImmediately (0, std::make_unique<dsp::Convolver> (std::vector<std::vector<float>> { h }, 48000.0));
    dsp::CabinetSection::Settings s;
    s.enabled = false;
    auto x = test::sine (440, 48000, 4800, 0.3f);
    std::vector<float> l (x.size()), r (x.size());
    for (int p = 0; p < 4800; p += 64) cab.process (x.data() + p, l.data() + p, r.data() + p, 64, s);
    for (int i = 2000; i < 4800; ++i) CHECK (l[(size_t) i] == doctest::Approx (x[(size_t) i]).epsilon (1e-5));
}

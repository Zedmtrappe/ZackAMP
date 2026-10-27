#include <doctest.h>
#include "TestSignals.h"
#include "../../Source/DSP/Utilities/Oversampler.h"

using namespace af;

static double measureImageRejection (int factorLog2, double toneHz, double fs)
{
    dsp::Oversampler os;
    const int n = 8192;
    os.prepare (factorLog2, n);
    auto x = test::sine (toneHz, fs, n, 0.5f);
    float* up = os.upsample (x.data(), n);
    const int factor = 1 << factorLog2;
    std::vector<float> u (up, up + n * factor);
    const double fsUp = fs * factor;
    const double signal = test::toneAmplitude (u, toneHz, fsUp, 2048);
    const double image = test::toneAmplitude (u, fs - toneHz, fsUp, 2048);
    return test::db (image / signal);
}

TEST_CASE ("Oversampler passband is flat and images are rejected")
{
    const double fs = 48000;
    for (int f : { 1, 2, 3 })
    {
        dsp::Oversampler os;
        const int n = 8192;
        os.prepare (f, n);
        auto x = test::sine (1000.0, fs, n, 0.5f);
        std::vector<float> out ((size_t) n);
        float* up = os.upsample (x.data(), n);
        os.downsample (up, out.data(), n);
        const double a = test::toneAmplitude (out, 1000.0, fs, 2048);
        CHECK (std::abs (test::db (a / 0.5)) < 0.05);
        CHECK (test::allFinite (out));

        auto x2 = test::sine (15000.0, fs, n, 0.5f);
        os.reset();
        up = os.upsample (x2.data(), n);
        os.downsample (up, out.data(), n);
        CHECK (std::abs (test::db (test::toneAmplitude (out, 15000.0, fs, 2048) / 0.5)) < 0.1);
    }

    const double rej1k = measureImageRejection (1, 1000.0, fs);
    const double rej10k = measureImageRejection (1, 10000.0, fs);
    MESSAGE ("2x image rejection @1k: " << rej1k << " dB, @10k: " << rej10k << " dB");
    CHECK (rej1k < -90.0);
    CHECK (rej10k < -90.0);
    CHECK (measureImageRejection (2, 5000.0, fs) < -85.0);
}

TEST_CASE ("Oversampler downsampling rejects content above base Nyquist")
{
    // A tone generated at 4x rate above the base Nyquist must not fold back.
    const double fs = 48000, fsUp = fs * 4;
    dsp::Oversampler os;
    const int n = 4096;
    os.prepare (2, n);
    auto hi = test::sine (30000.0, fsUp, n * 4, 0.5f); // would alias to 18 kHz
    std::vector<float> out ((size_t) n);
    os.downsample (hi.data(), out.data(), n);
    const double alias = test::toneAmplitude (out, 18000.0, fs, 1024);
    MESSAGE ("alias at 18k: " << test::db (alias / 0.5) << " dB");
    CHECK (test::db (alias / 0.5) < -80.0);
}

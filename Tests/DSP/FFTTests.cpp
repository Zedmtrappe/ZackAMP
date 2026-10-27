#include <doctest.h>
#include "TestSignals.h"
#include "../../Source/DSP/Utilities/FFT.h"

#include <random>

using af::dsp::RealFFT;

TEST_CASE ("RealFFT round trip and agreement with naive DFT")
{
    for (int n : { 4, 8, 64, 256, 2048 })
    {
        RealFFT fft (n);
        std::mt19937 rng (1234);
        std::uniform_real_distribution<float> d (-1.f, 1.f);
        std::vector<float> x ((size_t) n), y ((size_t) n);
        for (auto& v : x) v = d (rng);
        std::vector<RealFFT::Complex> X ((size_t) fft.getNumBins());
        fft.forward (x.data(), X.data());

        for (int k = 0; k <= n / 2; k += std::max (1, n / 16))
        {
            std::complex<double> ref = 0;
            for (int i = 0; i < n; ++i)
                ref += (double) x[(size_t) i] * std::polar (1.0, -af::dsp::kTwoPi * k * i / n);
            CHECK (std::abs (std::complex<double> (X[(size_t) k]) - ref) < 1e-3 * n);
        }

        fft.inverse (X.data(), y.data());
        double maxErr = 0;
        for (int i = 0; i < n; ++i) maxErr = std::max (maxErr, (double) std::abs (x[(size_t) i] - y[(size_t) i]));
        CHECK (maxErr < 1e-5);
    }
}

TEST_CASE ("RealFFT rejects non power of two")
{
    CHECK_THROWS (RealFFT (100));
}

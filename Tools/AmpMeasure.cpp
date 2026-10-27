// AMP FORGE — amp-model measurement tool: THD, harmonic balance and output level versus
// gain for each model at typical pickup levels. Used to calibrate gain staging.
#include "Audio/AmpForgeEngine.h"

#include <complex>
#include <cstdio>
#include <vector>

using namespace af;

static double harmonic (const std::vector<float>& x, double f, double fs)
{
    std::complex<double> acc = 0.0;
    double ws = 0;
    const int n = (int) x.size();
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (dsp::kTwoPi * i / (n - 1));
        acc += x[(size_t) i] * w * std::polar (1.0, -dsp::kTwoPi * f * i / fs);
        ws += w;
    }
    return 2.0 * std::abs (acc) / ws;
}

int main (int argc, char** argv)
{
    const double fs = argc > 1 ? std::atof (argv[1]) : 48000.0;
    AmpForgeEngine e;
    e.params().set (pCabOn, 0);
    e.params().set (pOutLevel, 0);
    e.prepare (fs, 256);
    const double f0 = 220.0;
    std::printf ("fs=%.0f oversampling=%dx\n", fs, e.getOversamplingFactor());
    for (int m = 0; m < dsp::kNumAmpModels; ++m)
    {
        std::printf ("\n== %s ==\n  in(dBFS)  gain |  THD%%   H2/H3(dB)  out RMS dBFS\n", dsp::getAmpVoicing (m).name);
        for (float inDb : { -24.0f, -12.0f })
            for (float g : { 0.1f, 0.3f, 0.5f, 0.7f, 1.0f })
            {
                e.params().set (pAmpModel, (float) m);
                e.params().set (pAmpGain, g);
                e.prepare (fs, 256);
                const int n = (int) fs; // 1 s
                std::vector<float> in ((size_t) n), l ((size_t) n), r ((size_t) n);
                const float a = std::pow (10.0f, inDb / 20.0f);
                for (int i = 0; i < n; ++i) in[(size_t) i] = a * (float) std::sin (dsp::kTwoPi * f0 * i / fs);
                e.process (in.data(), l.data(), r.data(), n);
                std::vector<float> tail (l.begin() + n / 2, l.end());
                const double h1 = harmonic (tail, f0, fs);
                double hs = 0;
                for (int k = 2; k <= 15; ++k) { const double h = harmonic (tail, f0 * k, fs); hs += h * h; }
                const double h2 = harmonic (tail, 2 * f0, fs), h3 = harmonic (tail, 3 * f0, fs);
                double rms = 0; for (float v : tail) rms += v * v; rms = std::sqrt (rms / tail.size());
                std::printf ("  %6.0f   %4.1f | %6.1f   %6.1f      %6.1f\n", inDb, g * 10, 100.0 * std::sqrt (hs) / std::max (h1, 1e-12),
                             20 * std::log10 (std::max (h2, 1e-12) / std::max (h3, 1e-12)), 20 * std::log10 (std::max (rms, 1e-12)));
            }
    }
}

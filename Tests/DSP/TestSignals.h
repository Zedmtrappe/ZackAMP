// Deterministic synthetic test signals and simple analysis helpers for DSP tests.
#pragma once

#include "../../Source/DSP/Utilities/DspCommon.h"
#include <cmath>
#include <complex>
#include <vector>

namespace af::test
{
inline std::vector<float> sine (double freq, double fs, int n, float amp = 1.0f)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = amp * (float) std::sin (af::dsp::kTwoPi * freq * i / fs);
    return v;
}

/** Single-bin DFT magnitude (Goertzel-like direct correlation) with a Hann window. */
inline double toneAmplitude (const std::vector<float>& x, double freq, double fs, int start = 0)
{
    const int n = (int) x.size() - start;
    std::complex<double> acc = 0.0;
    double wsum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (af::dsp::kTwoPi * i / (n - 1));
        acc += (double) x[(size_t) (start + i)] * w * std::polar (1.0, -af::dsp::kTwoPi * freq * i / fs);
        wsum += w;
    }
    return 2.0 * std::abs (acc) / wsum;
}

inline double rms (const std::vector<float>& x, int start = 0, int end = -1)
{
    if (end < 0) end = (int) x.size();
    double s = 0;
    for (int i = start; i < end; ++i) s += (double) x[(size_t) i] * x[(size_t) i];
    return std::sqrt (s / std::max (1, end - start));
}

inline bool allFinite (const std::vector<float>& x)
{
    for (float v : x) if (! std::isfinite (v)) return false;
    return true;
}

inline float peak (const std::vector<float>& x, int start = 0)
{
    float p = 0;
    for (size_t i = (size_t) start; i < x.size(); ++i) p = std::max (p, std::abs (x[i]));
    return p;
}

inline double db (double g) { return 20.0 * std::log10 (std::max (g, 1e-20)); }
} // namespace af::test

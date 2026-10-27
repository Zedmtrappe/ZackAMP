// ZackAMP — biquad and first-order filters (double precision state for low-frequency
// stability at oversampled rates). Coefficient design follows the RBJ Audio EQ Cookbook.
#pragma once

#include "DspCommon.h"
#include <complex>

namespace af::dsp
{
struct BiquadCoeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static BiquadCoeffs identity() { return {}; }

    static BiquadCoeffs lowPass (double fs, double f, double q)
    {
        const auto [cw, alpha] = prewarp (fs, f, q);
        const double a0 = 1 + alpha;
        return norm ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, a0, -2 * cw, 1 - alpha);
    }

    static BiquadCoeffs highPass (double fs, double f, double q)
    {
        const auto [cw, alpha] = prewarp (fs, f, q);
        const double a0 = 1 + alpha;
        return norm ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, a0, -2 * cw, 1 - alpha);
    }

    static BiquadCoeffs bandPass (double fs, double f, double q) // 0 dB peak gain
    {
        const auto [cw, alpha] = prewarp (fs, f, q);
        return norm (alpha, 0, -alpha, 1 + alpha, -2 * cw, 1 - alpha);
    }

    static BiquadCoeffs peaking (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const auto [cw, alpha] = prewarp (fs, f, q);
        return norm (1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
    }

    static BiquadCoeffs lowShelf (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const auto [cw, alpha] = prewarp (fs, f, q);
        const double sA = 2 * std::sqrt (A) * alpha;
        return norm (A * ((A + 1) - (A - 1) * cw + sA),
                     2 * A * ((A - 1) - (A + 1) * cw),
                     A * ((A + 1) - (A - 1) * cw - sA),
                     (A + 1) + (A - 1) * cw + sA,
                     -2 * ((A - 1) + (A + 1) * cw),
                     (A + 1) + (A - 1) * cw - sA);
    }

    static BiquadCoeffs highShelf (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const auto [cw, alpha] = prewarp (fs, f, q);
        const double sA = 2 * std::sqrt (A) * alpha;
        return norm (A * ((A + 1) + (A - 1) * cw + sA),
                     -2 * A * ((A - 1) + (A + 1) * cw),
                     A * ((A + 1) + (A - 1) * cw - sA),
                     (A + 1) - (A - 1) * cw + sA,
                     2 * ((A - 1) - (A + 1) * cw),
                     (A + 1) - (A - 1) * cw - sA);
    }

    std::complex<double> response (double freqHz, double fs) const
    {
        const double w = kTwoPi * freqHz / fs;
        const std::complex<double> z1 = std::polar (1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    }

    double magnitudeDb (double freqHz, double fs) const
    {
        return 20.0 * std::log10 (std::max (1e-12, std::abs (response (freqHz, fs))));
    }

    /** Poles strictly inside the unit circle (Jury criterion for 2nd order). */
    bool isStable() const { return std::abs (a2) < 1.0 && std::abs (a1) < 1.0 + a2; }

private:
    struct PW { double cw, alpha; };
    static PW prewarp (double fs, double f, double q)
    {
        f = clampv (f, 1.0, fs * 0.49);
        const double w = kTwoPi * f / fs;
        return { std::cos (w), std::sin (w) / (2.0 * std::max (q, 1e-3)) };
    }
    static BiquadCoeffs norm (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }
};

/** Transposed direct form II biquad. */
class Biquad
{
public:
    void setCoeffs (const BiquadCoeffs& c) noexcept { k = c; }
    const BiquadCoeffs& getCoeffs() const noexcept { return k; }
    void reset() noexcept { z1 = z2 = 0.0; }

    float process (float xf) noexcept
    {
        const double x = xf;
        const double y = k.b0 * x + z1;
        z1 = k.b1 * x - k.a1 * y + z2;
        z2 = k.b2 * x - k.a2 * y;
        return (float) y;
    }

    void processBlock (float* data, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            data[i] = process (data[i]);
    }

private:
    BiquadCoeffs k;
    double z1 = 0.0, z2 = 0.0;
};

/** First-order low/high-pass using the bilinear transform with prewarping (TPT form). */
class OnePole
{
public:
    enum class Type { lowPass, highPass };

    void prepare (double sampleRate, Type t, double cutoffHz) noexcept
    {
        fs = sampleRate;
        type = t;
        setCutoff (cutoffHz);
        reset();
    }

    void setCutoff (double hz) noexcept
    {
        hz = clampv (hz, 1.0, fs * 0.45);
        const double g = std::tan (kPi * hz / fs);
        G = g / (1.0 + g);
    }

    void reset() noexcept { s = 0.0; }

    float process (float xf) noexcept
    {
        const double v = (xf - s) * G;
        const double lp = v + s;
        s = lp + v;
        return (float) (type == Type::lowPass ? lp : xf - lp);
    }

private:
    double fs = 48000.0, G = 0.5, s = 0.0;
    Type type = Type::lowPass;
};

} // namespace af::dsp

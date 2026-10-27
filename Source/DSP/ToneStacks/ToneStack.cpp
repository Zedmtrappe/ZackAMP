#include "ToneStack.h"
#include "../Utilities/DspCommon.h"

namespace af::dsp
{
void ToneStack::prepare (double sampleRate)
{
    fs = sampleRate;
    setControls (0.5, 0.5, 0.5);
    reset();
}

void ToneStack::reset() noexcept { z = {}; }

void ToneStack::setComponents (const ToneStackComponents& c) { comp = c; }

double ToneStack::bassTaper (double knob)
{
    knob = clampv (knob, 0.0, 1.0);
    // Audio (log) taper: ~10% resistance at mid rotation.
    return (std::pow (10.0, 2.0 * knob) - 1.0) / 99.0;
}

void ToneStack::analogCoefficients (const ToneStackComponents& c, double l, double m, double t,
                                    std::array<double, 4>& num, std::array<double, 4>& den)
{
    const double R1 = c.R1, R2 = c.R2, R3 = c.R3, R4 = c.R4;
    const double C1 = c.C1, C2 = c.C2, C3 = c.C3;
    const double m2 = m * m;

    const double b1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);

    const double b2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4)
                      - m2 * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                      + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                      + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
                      + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                      + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);

    const double C123 = C1 * C2 * C3;
    const double b3 = l * m * (C123 * R1 * R2 * R3 + C123 * R2 * R3 * R4)
                      - m2 * (C123 * R1 * R3 * R3 + C123 * R3 * R3 * R4)
                      + m * (C123 * R1 * R3 * R3 + C123 * R3 * R3 * R4)
                      + t * C123 * R1 * R3 * R4 - t * m * C123 * R1 * R3 * R4
                      + t * l * C123 * R1 * R2 * R4;

    const double a0 = 1.0;
    const double a1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);

    const double a2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                      + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                      - m2 * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                      + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
                      + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3
                         + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);

    const double a3 = l * m * (C123 * R1 * R2 * R3 + C123 * R2 * R3 * R4)
                      - m2 * (C123 * R1 * R3 * R3 + C123 * R3 * R3 * R4)
                      + m * (C123 * R3 * R3 * R4 + C123 * R1 * R3 * R3 - C123 * R1 * R3 * R4)
                      + l * C123 * R1 * R2 * R4 + C123 * R1 * R3 * R4;

    num = { 0.0, b1, b2, b3 };
    den = { a0, a1, a2, a3 };
}

void ToneStack::setControls (double bass, double mid, double treble)
{
    const double l = clampv (bassTaper (bass), 1e-3, 1.0);
    const double m = clampv (mid, 1e-3, 1.0);
    const double t = clampv (treble, 1e-3, 1.0 - 1e-3);
    analogCoefficients (comp, l, m, t, an, ad);

    // Bilinear transform: s = c (1 - z^-1) / (1 + z^-1). Multiply through by (1 + z^-1)^3,
    // so s^k -> c^k (1 - z^-1)^k (1 + z^-1)^(3-k).
    const double c = 2.0 * fs;
    std::array<double, 4> B {}, A {};
    for (int k = 0; k <= 3; ++k)
    {
        // Expand (1 - x)^k (1 + x)^(3 - k).
        std::array<double, 4> poly { 1, 0, 0, 0 };
        int deg = 0;
        auto mulBy = [&] (double sign)
        {
            for (int i = deg + 1; i >= 1; --i)
                poly[(size_t) i] += sign * poly[(size_t) i - 1];
            ++deg;
        };
        for (int i = 0; i < k; ++i)
            mulBy (-1.0);
        for (int i = 0; i < 3 - k; ++i)
            mulBy (1.0);
        const double ck = std::pow (c, k);
        for (int i = 0; i < 4; ++i)
        {
            B[(size_t) i] += an[(size_t) k] * ck * poly[(size_t) i];
            A[(size_t) i] += ad[(size_t) k] * ck * poly[(size_t) i];
        }
    }
    for (int i = 0; i < 4; ++i)
    {
        b[(size_t) i] = B[(size_t) i] / A[0];
        a[(size_t) i] = A[(size_t) i] / A[0];
    }
}

std::complex<double> ToneStack::analogResponse (double f) const
{
    const std::complex<double> s (0.0, kTwoPi * f);
    const auto num = an[0] + s * (an[1] + s * (an[2] + s * an[3]));
    const auto den = ad[0] + s * (ad[1] + s * (ad[2] + s * ad[3]));
    return num / den;
}

std::complex<double> ToneStack::digitalResponse (double f) const
{
    const std::complex<double> zi = std::polar (1.0, -kTwoPi * f / fs);
    const auto num = b[0] + zi * (b[1] + zi * (b[2] + zi * b[3]));
    const auto den = a[0] + zi * (a[1] + zi * (a[2] + zi * a[3]));
    return num / den;
}

} // namespace af::dsp

#include "Oversampler.h"
#include "DspCommon.h"

#include <cmath>

namespace af::dsp
{
namespace
{
    double ipow (double x, int p)
    {
        double r = 1.0;
        for (int i = 0; i < p; ++i)
            r *= x;
        return r;
    }

    void transitionParams (double& k, double& q, double tbw)
    {
        k = std::tan ((1.0 - tbw * 2.0) * kPi / 4.0);
        k *= k;
        const double kksqrt = std::pow (1.0 - k * k, 0.25);
        const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
        const double e2 = e * e;
        const double e4 = e2 * e2;
        q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    }

    double accNum (double q, int order, int c)
    {
        double acc = 0.0, term = 0.0;
        int i = 0, j = 1;
        do
        {
            term = ipow (q, i * (i + 1)) * std::sin ((i * 2 + 1) * c * kPi / order) * j;
            acc += term;
            j = -j;
            ++i;
        } while (std::abs (term) > 1e-100 && i < 100);
        return acc;
    }

    double accDen (double q, int order, int c)
    {
        double acc = 0.0, term = 0.0;
        int i = 1, j = -1;
        do
        {
            term = ipow (q, i * i) * std::cos (i * 2 * c * kPi / order) * j;
            acc += term;
            j = -j;
            ++i;
        } while (std::abs (term) > 1e-100 && i < 100);
        return acc;
    }
} // namespace

std::vector<double> designHalfbandCoefficients (int numCoefs, double tbw)
{
    double k = 0, q = 0;
    transitionParams (k, q, tbw);
    const int order = numCoefs * 2 + 1;
    std::vector<double> c ((size_t) numCoefs);
    for (int idx = 0; idx < numCoefs; ++idx)
    {
        const int cc = idx + 1;
        const double num = accNum (q, order, cc) * std::pow (q, 0.25);
        const double den = accDen (q, order, cc) + 0.5;
        const double ww = num / den;
        const double wwsq = ww * ww;
        const double x = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        c[(size_t) idx] = (1.0 - x) / (1.0 + x);
    }
    return c;
}

void HalfbandStage::setCoefficients (const std::vector<double>& c)
{
    numCoefs = std::min ((int) c.size(), kMaxCoefs);
    for (int i = 0; i < numCoefs; ++i)
        up[(size_t) i].c = down[(size_t) i].c = c[(size_t) i];
    reset();
}

void HalfbandStage::reset() noexcept
{
    for (auto& a : up)
        a.x1 = a.y1 = 0;
    for (auto& a : down)
        a.x1 = a.y1 = 0;
}

void HalfbandStage::upsample (float in, float& out0, float& out1) noexcept
{
    double even = in, odd = in;
    for (int i = 0; i + 1 < numCoefs; i += 2)
    {
        even = up[(size_t) i].process (even);
        odd = up[(size_t) i + 1].process (odd);
    }
    if (numCoefs & 1)
        even = up[(size_t) numCoefs - 1].process (even);
    out0 = (float) even;
    out1 = (float) odd;
}

float HalfbandStage::downsample (float in0, float in1) noexcept
{
    double a = in1, b = in0;
    for (int i = 0; i + 1 < numCoefs; i += 2)
    {
        a = down[(size_t) i].process (a);
        b = down[(size_t) i + 1].process (b);
    }
    if (numCoefs & 1)
        a = down[(size_t) numCoefs - 1].process (a);
    return (float) (0.5 * (a + b));
}

void Oversampler::prepare (int factorLog2, int maxBlockSize)
{
    stagesUsed = clampv (factorLog2, 0, 3);
    maxBlock = std::max (1, maxBlockSize);
    bufA.assign ((size_t) maxBlock << stagesUsed, 0.0f);
    bufB.assign ((size_t) maxBlock << stagesUsed, 0.0f);

    // Stage 0 (base -> 2x) needs the steepest transition: passband to ~0.42 * fs_base.
    // Later stages only need to reject images of the already band-limited signal.
    // Attenuation figures are verified in Tests/DSP/OversamplerTests.cpp.
    stages[0].setCoefficients (designHalfbandCoefficients (8, 0.04));
    stages[1].setCoefficients (designHalfbandCoefficients (6, 0.12));
    stages[2].setCoefficients (designHalfbandCoefficients (4, 0.18));

    // Low-frequency group delay estimate (each stage ~ a few samples at its rate).
    latencyEstimate = 0.0;
    reset();
}

void Oversampler::reset() noexcept
{
    for (auto& s : stages)
        s.reset();
}

float* Oversampler::upsample (const float* in, int n) noexcept
{
    if (stagesUsed == 0)
    {
        std::copy (in, in + n, bufA.data());
        return bufA.data();
    }

    const float* src = in;
    float* dst = bufA.data();
    int len = n;
    for (int s = 0; s < stagesUsed; ++s)
    {
        dst = (s & 1) ? bufB.data() : bufA.data();
        for (int i = 0; i < len; ++i)
            stages[(size_t) s].upsample (src[i], dst[2 * i], dst[2 * i + 1]);
        src = dst;
        len *= 2;
    }
    return dst;
}

void Oversampler::downsample (const float* src, float* out, int n) noexcept
{
    if (stagesUsed == 0)
    {
        std::copy (src, src + n, out);
        return;
    }

    int len = n << stagesUsed;
    const float* s = src;
    for (int st = stagesUsed - 1; st >= 0; --st)
    {
        len /= 2;
        float* dst = (st == 0) ? out : ((s == bufA.data()) ? bufB.data() : bufA.data());
        for (int i = 0; i < len; ++i)
            dst[i] = stages[(size_t) st].downsample (s[2 * i], s[2 * i + 1]);
        s = dst;
    }
}

} // namespace af::dsp

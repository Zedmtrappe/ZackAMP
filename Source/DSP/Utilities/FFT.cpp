#include "FFT.h"
#include "DspCommon.h"

#include <cassert>
#include <stdexcept>

namespace af::dsp
{
RealFFT::RealFFT (int size) : n (size), half (size / 2)
{
    if (! isPowerOfTwo (size) || size < 4)
        throw std::invalid_argument ("RealFFT size must be a power of two >= 4");

    bitrev.resize ((size_t) half);
    int bits = 0;
    while ((1 << bits) < half)
        ++bits;
    for (int i = 0; i < half; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        bitrev[(size_t) i] = r;
    }

    twiddle.resize ((size_t) std::max (1, half / 2));
    for (int k = 0; k < (int) twiddle.size(); ++k)
        twiddle[(size_t) k] = std::polar (1.0f, (float) (-kTwoPi * k / half));

    realTwiddle.resize ((size_t) half + 1);
    for (int k = 0; k <= half; ++k)
        realTwiddle[(size_t) k] = std::polar (1.0f, (float) (-kTwoPi * k / n));

    work.resize ((size_t) half);
}

static inline std::complex<float> cmul (std::complex<float> a, std::complex<float> b) noexcept
{
    return { a.real() * b.real() - a.imag() * b.imag(), a.real() * b.imag() + a.imag() * b.real() };
}

void RealFFT::complexFFT (Complex* a, bool inv) noexcept
{
    const int m = half;
    for (int i = 0; i < m; ++i)
    {
        const int j = bitrev[(size_t) i];
        if (j > i)
            std::swap (a[i], a[j]);
    }

    for (int len = 2; len <= m; len <<= 1)
    {
        const int h = len >> 1;
        const int step = m / len;
        for (int i = 0; i < m; i += len)
        {
            for (int j = 0; j < h; ++j)
            {
                Complex w = twiddle[(size_t) (j * step)];
                if (inv)
                    w = { w.real(), -w.imag() };
                const Complex u = a[i + j];
                const Complex v = cmul (a[i + j + h], w);
                a[i + j] = u + v;
                a[i + j + h] = u - v;
            }
        }
    }
}

void RealFFT::forward (const float* in, Complex* out) noexcept
{
    Complex* z = work.data();
    for (int i = 0; i < half; ++i)
        z[i] = { in[2 * i], in[2 * i + 1] };

    complexFFT (z, false);

    for (int k = 0; k <= half; ++k)
    {
        const Complex zk = z[k % half];
        const Complex zc = std::conj (z[(half - k) % half]);
        const Complex even = 0.5f * (zk + zc);
        const Complex oddTimesI = 0.5f * (zk - zc); // = i * O[k]
        const Complex odd = { oddTimesI.imag(), -oddTimesI.real() };
        out[k] = even + cmul (realTwiddle[(size_t) k], odd);
    }
}

void RealFFT::inverse (const Complex* in, float* out) noexcept
{
    Complex* z = work.data();
    for (int k = 0; k < half; ++k)
    {
        const Complex xk = in[k];
        const Complex xc = std::conj (in[half - k]);
        const Complex even = 0.5f * (xk + xc);
        const Complex odd = cmul (0.5f * (xk - xc), std::conj (realTwiddle[(size_t) k]));
        z[k] = even + Complex (-odd.imag(), odd.real()); // even + i*odd
    }

    complexFFT (z, true);

    const float scale = 1.0f / (float) half;
    for (int i = 0; i < half; ++i)
    {
        out[2 * i] = z[i].real() * scale;
        out[2 * i + 1] = z[i].imag() * scale;
    }
}

} // namespace af::dsp

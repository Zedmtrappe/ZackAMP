// AMP FORGE — compact real FFT (radix-2, precomputed tables, allocation-free after construction).
#pragma once

#include <complex>
#include <vector>

namespace af::dsp
{
class RealFFT
{
public:
    using Complex = std::complex<float>;

    /** size must be a power of two >= 4. Allocates; construct off the audio thread. */
    explicit RealFFT (int size);

    int getSize() const noexcept { return n; }
    int getNumBins() const noexcept { return n / 2 + 1; }

    /** Unnormalised forward transform: n real samples -> n/2+1 bins. */
    void forward (const float* in, Complex* out) noexcept;

    /** Inverse transform including 1/n scaling, so inverse(forward(x)) == x. */
    void inverse (const Complex* in, float* out) noexcept;

    static bool isPowerOfTwo (int v) noexcept { return v > 0 && (v & (v - 1)) == 0; }
    static int nextPowerOfTwo (int v) noexcept
    {
        int p = 1;
        while (p < v)
            p <<= 1;
        return p;
    }

private:
    void complexFFT (Complex* data, bool inverse) noexcept;

    int n = 0, half = 0;
    std::vector<int> bitrev;
    std::vector<Complex> twiddle;     // exp(-2*pi*i*k/half), k < half/2
    std::vector<Complex> realTwiddle; // exp(-2*pi*i*k/n),    k <= half
    std::vector<Complex> work;
};

} // namespace af::dsp

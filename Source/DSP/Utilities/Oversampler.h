// ZackAMP — low-latency polyphase IIR half-band oversampling.
//
// Each 2x stage is a pair of all-pass chains (elliptic half-band design, after the
// classic polyphase IIR method used e.g. in Laurent de Soras' HIIR). Compared with
// linear-phase FIR half-bands this gives only a few samples of group delay, which
// matters more for live guitar monitoring than perfectly linear phase near Nyquist.
#pragma once

#include <array>
#include <vector>

namespace af::dsp
{
/** Designs half-band all-pass coefficients for a given number of coefficients and
    transition bandwidth (fraction of the oversampled rate, 0 < tbw < 0.5). */
std::vector<double> designHalfbandCoefficients (int numCoefficients, double transitionBandwidth);

class HalfbandStage
{
public:
    static constexpr int kMaxCoefs = 16;

    void setCoefficients (const std::vector<double>& c);
    void reset() noexcept;

    /** One input sample -> two output samples. */
    void upsample (float in, float& out0, float& out1) noexcept;
    /** Two input samples -> one output sample. */
    float downsample (float in0, float in1) noexcept;

private:
    struct AP
    {
        double c = 0, x1 = 0, y1 = 0;
        double process (double x) noexcept
        {
            const double y = c * (x - y1) + x1;
            x1 = x;
            y1 = y;
            return y;
        }
    };

    int numCoefs = 0;
    std::array<AP, kMaxCoefs> up {}, down {};
};

class Oversampler
{
public:
    /** factorLog2: 0 = 1x, 1 = 2x, 2 = 4x, 3 = 8x. Allocates; call off the audio thread. */
    void prepare (int factorLog2, int maxBlockSize);
    void reset() noexcept;

    int getFactor() const noexcept { return 1 << stagesUsed; }

    /** Upsamples n base-rate samples. Returns pointer to n * factor samples (internal buffer). */
    float* upsample (const float* in, int n) noexcept;

    /** Downsamples n * factor samples from the internal buffer (or `src`) into out[n]. */
    void downsample (const float* src, float* out, int n) noexcept;

    /** Approximate group delay at low frequencies, in base-rate samples. */
    double getLatencyEstimate() const noexcept { return latencyEstimate; }

private:
    int stagesUsed = 0;
    int maxBlock = 0;
    std::array<HalfbandStage, 3> stages;
    std::vector<float> bufA, bufB;
    double latencyEstimate = 0.0;
};

} // namespace af::dsp

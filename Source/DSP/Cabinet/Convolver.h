// AMP FORGE — zero-latency partitioned convolution for cabinet impulse responses.
//
// Scheme: the first P taps of the IR ("head") are convolved directly in the time domain,
// sample by sample, so there is no added latency. The remainder ("tail") is split into
// uniform partitions of P samples and convolved with uniformly-partitioned overlap-save
// FFT convolution (FFT size 2P, frequency-domain delay line). Because tail partition j>=1
// only needs input that is at least one full block old, its contribution to the next
// block can be computed as soon as the current block of P inputs completes.
//
// A Convolver instance owns all of its state and kernel data, is fully built on a
// non-real-time thread, and is then handed to the audio thread (see CabinetSection).
#pragma once

#include "../Utilities/FFT.h"
#include "../Utilities/RealtimeExchange.h"

#include <memory>
#include <string>
#include <vector>

namespace af::dsp
{
/** Impulse response audio at its original sample rate (source data, immutable once shared). */
struct IRData
{
    std::string id;          // stable identifier, e.g. "factory:us-open-1x12-c" or "user:<uuid>"
    std::string displayName;
    double sampleRate = 48000.0;
    std::vector<std::vector<float>> channels; // 1 or 2 channels

    int getNumChannels() const noexcept { return (int) channels.size(); }
    int getLength() const noexcept { return channels.empty() ? 0 : (int) channels[0].size(); }
};

struct IRPrepareOptions
{
    double maxSeconds = 1.0;     // longer IRs are truncated with a short fade (documented)
    bool normaliseLevel = true;  // equalise perceived level between IRs
    double fadeOutMs = 5.0;
};

/** Resamples, truncates and level-normalises an IR for a target sample rate (non-RT). */
std::vector<std::vector<float>> prepareImpulse (const IRData& ir, double targetRate,
                                                const IRPrepareOptions& opts, bool* wasTruncated = nullptr);

/** High-quality offline windowed-sinc resampler (Kaiser window). */
std::vector<float> resampleOffline (const std::vector<float>& in, double fromRate, double toRate);

class Convolver final : public Retirable
{
public:
    static constexpr int kPartition = 64;

    /** Builds a convolver for 1 or 2 IR channels (already at the processing sample rate). */
    Convolver (const std::vector<std::vector<float>>& impulse, double sampleRate);

    int getNumOutputs() const noexcept { return numOut; }
    int getLength() const noexcept { return irLength; }
    double getSampleRate() const noexcept { return rate; }

    void reset() noexcept;

    /** Mono input -> getNumOutputs() outputs. outR may be null for mono IRs. Adds nothing; writes. */
    void process (const float* in, float* outL, float* outR, int n) noexcept;

private:
    void computeTail() noexcept;

    using Complex = RealFFT::Complex;
    static constexpr int P = kPartition;
    static constexpr int kBins = P + 1;

    int numOut = 1, irLength = 0, numTail = 0;
    double rate = 48000.0;
    RealFFT fft { 2 * P };

    std::vector<float> headRev[2];          // reversed head taps
    std::vector<Complex> tailSpectra[2];    // numTail * kBins

    std::vector<float> history;             // 2P mirrored ring of inputs
    int histPos = 0;
    std::vector<float> frame;               // 2P: previous block + current block
    int blockPos = 0;
    std::vector<Complex> fdl;               // numTail * kBins, ring of input spectra
    int fdlPos = 0;
    std::vector<Complex> accum;
    std::vector<float> timeBuf;
    std::vector<float> tailOut[2];          // P samples of precomputed tail output
};

} // namespace af::dsp

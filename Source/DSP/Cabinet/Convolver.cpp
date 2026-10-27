#include "Convolver.h"
#include "../Utilities/DspCommon.h"

#include <cstring>
#include <stdexcept>

namespace af::dsp
{
//==============================================================================
namespace
{
    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1e-12 * sum)
                break;
        }
        return sum;
    }
} // namespace

std::vector<float> resampleOffline (const std::vector<float>& in, double fromRate, double toRate)
{
    if (in.empty() || fromRate <= 0 || toRate <= 0)
        return {};
    if (std::abs (fromRate - toRate) < 1e-6)
        return in;

    const double ratio = toRate / fromRate;
    const double cutoff = std::min (1.0, ratio) * 0.97; // relative to the source Nyquist
    const int halfWidth = (int) std::ceil (32.0 / std::min (1.0, ratio));
    const double beta = 9.0;
    const double i0beta = besselI0 (beta);
    const int outLen = std::max (1, (int) std::floor ((double) in.size() * ratio));

    std::vector<float> out ((size_t) outLen);
    const int inLen = (int) in.size();
    for (int i = 0; i < outLen; ++i)
    {
        const double pos = i / ratio;
        const int centre = (int) std::floor (pos);
        double acc = 0.0;
        for (int k = centre - halfWidth + 1; k <= centre + halfWidth; ++k)
        {
            if (k < 0 || k >= inLen)
                continue;
            const double d = pos - k;
            const double r = d / halfWidth;
            if (std::abs (r) >= 1.0)
                continue;
            const double w = besselI0 (beta * std::sqrt (1.0 - r * r)) / i0beta;
            const double xx = kPi * d * cutoff;
            const double sinc = std::abs (xx) < 1e-9 ? 1.0 : std::sin (xx) / xx;
            acc += in[(size_t) k] * cutoff * sinc * w;
        }
        out[(size_t) i] = (float) acc;
    }
    return out;
}

std::vector<std::vector<float>> prepareImpulse (const IRData& ir, double targetRate,
                                                const IRPrepareOptions& opts, bool* wasTruncated)
{
    std::vector<std::vector<float>> chans;
    if (wasTruncated != nullptr)
        *wasTruncated = false;
    if (ir.channels.empty() || ir.getLength() == 0)
        return chans;

    for (size_t c = 0; c < std::min<size_t> (2, ir.channels.size()); ++c)
        chans.push_back (resampleOffline (ir.channels[c], ir.sampleRate, targetRate));

    // Truncate.
    const size_t maxLen = (size_t) std::max (1.0, opts.maxSeconds * targetRate);
    if (chans[0].size() > maxLen)
    {
        if (wasTruncated != nullptr)
            *wasTruncated = true;
        const size_t fade = std::min (maxLen / 2, (size_t) (opts.fadeOutMs * 0.001 * targetRate));
        for (auto& ch : chans)
        {
            ch.resize (maxLen);
            for (size_t i = 0; i < fade; ++i)
                ch[maxLen - 1 - i] *= (float) (0.5 - 0.5 * std::cos (kPi * (double) i / (double) fade));
        }
    }

    // Level normalisation: average power gain over 100 Hz – 5 kHz (log-weighted) -> 0 dB,
    // so switching between IRs changes tone rather than loudness. One gain for all
    // channels keeps the stereo balance of stereo IRs.
    if (opts.normaliseLevel)
    {
        const int n = RealFFT::nextPowerOfTwo ((int) chans[0].size() * 2);
        RealFFT fft (std::max (n, 1024));
        std::vector<float> buf ((size_t) fft.getSize());
        std::vector<RealFFT::Complex> spec ((size_t) fft.getNumBins());
        double sumDb = 0.0;
        int count = 0;
        for (auto& ch : chans)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            std::copy (ch.begin(), ch.end(), buf.begin());
            fft.forward (buf.data(), spec.data());
            for (double f = 100.0; f <= 5000.0; f *= 1.05)
            {
                const int bin = (int) std::lround (f / targetRate * fft.getSize());
                const double p = std::norm (spec[(size_t) bin]);
                sumDb += 10.0 * std::log10 (std::max (p, 1e-20));
                ++count;
            }
        }
        const double avgDb = count > 0 ? sumDb / count : 0.0;
        const float g = (float) std::pow (10.0, -avgDb / 20.0);
        if (std::isfinite (g) && g > 0.0f)
            for (auto& ch : chans)
                for (auto& v : ch)
                    v *= g;
    }
    return chans;
}

//==============================================================================
Convolver::Convolver (const std::vector<std::vector<float>>& impulse, double sampleRate)
    : rate (sampleRate)
{
    if (impulse.empty() || impulse[0].empty())
        throw std::invalid_argument ("Convolver: empty impulse");

    numOut = std::min (2, (int) impulse.size());
    irLength = (int) impulse[0].size();
    numTail = std::max (0, (irLength - 1) / P); // partitions beyond the head

    std::vector<float> seg ((size_t) 2 * P);
    for (int c = 0; c < numOut; ++c)
    {
        const auto& h = impulse[(size_t) c];
        headRev[c].assign ((size_t) P, 0.0f);
        for (int t = 0; t < P && t < (int) h.size(); ++t)
            headRev[c][(size_t) (P - 1 - t)] = h[(size_t) t];

        tailSpectra[c].assign ((size_t) std::max (1, numTail) * kBins, Complex {});
        for (int part = 0; part < numTail; ++part)
        {
            std::fill (seg.begin(), seg.end(), 0.0f);
            const int start = (part + 1) * P;
            for (int t = 0; t < P && start + t < (int) h.size(); ++t)
                seg[(size_t) t] = h[(size_t) (start + t)];
            fft.forward (seg.data(), &tailSpectra[c][(size_t) part * kBins]);
        }
        tailOut[c].assign ((size_t) P, 0.0f);
    }

    history.assign ((size_t) 2 * P, 0.0f);
    frame.assign ((size_t) 2 * P, 0.0f);
    fdl.assign ((size_t) std::max (1, numTail) * kBins, Complex {});
    accum.assign ((size_t) kBins, Complex {});
    timeBuf.assign ((size_t) 2 * P, 0.0f);
    reset();
}

void Convolver::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (frame.begin(), frame.end(), 0.0f);
    std::fill (fdl.begin(), fdl.end(), Complex {});
    for (auto& t : tailOut)
        std::fill (t.begin(), t.end(), 0.0f);
    histPos = blockPos = fdlPos = 0;
}

void Convolver::computeTail() noexcept
{
    // frame = [previous block | current block]; transform into the FDL slot.
    fft.forward (frame.data(), &fdl[(size_t) fdlPos * kBins]);

    for (int c = 0; c < numOut; ++c)
    {
        std::fill (accum.begin(), accum.end(), Complex {});
        const Complex* H = tailSpectra[c].data();
        int slot = fdlPos;
        for (int part = 0; part < numTail; ++part)
        {
            const Complex* X = &fdl[(size_t) slot * kBins];
            const Complex* Hp = H + (size_t) part * kBins;
            for (int k = 0; k < kBins; ++k)
            {
                const float xr = X[k].real(), xi = X[k].imag();
                const float hr = Hp[k].real(), hi = Hp[k].imag();
                accum[(size_t) k] += Complex (xr * hr - xi * hi, xr * hi + xi * hr);
            }
            slot = (slot == 0) ? numTail - 1 : slot - 1;
        }
        fft.inverse (accum.data(), timeBuf.data());
        std::memcpy (tailOut[c].data(), timeBuf.data() + P, sizeof (float) * P);
    }

    fdlPos = (fdlPos + 1) % numTail;
    std::memcpy (frame.data(), frame.data() + P, sizeof (float) * P);
}

void Convolver::process (const float* in, float* outL, float* outR, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const float x = in[i];
        history[(size_t) histPos] = x;
        history[(size_t) (histPos + P)] = x;
        const float* win = &history[(size_t) histPos + 1]; // last P inputs, oldest first
        histPos = (histPos + 1) % P;

        for (int c = 0; c < numOut; ++c)
        {
            const float* h = headRev[c].data();
            float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
            for (int t = 0; t < P; t += 4)
            {
                a0 += h[t] * win[t];
                a1 += h[t + 1] * win[t + 1];
                a2 += h[t + 2] * win[t + 2];
                a3 += h[t + 3] * win[t + 3];
            }
            const float y = (a0 + a1) + (a2 + a3) + tailOut[c][(size_t) blockPos];
            if (c == 0)
                outL[i] = y;
            else if (outR != nullptr)
                outR[i] = y;
        }

        frame[(size_t) (P + blockPos)] = x;
        if (++blockPos == P)
        {
            blockPos = 0;
            if (numTail > 0)
                computeTail();
        }
    }
}

} // namespace af::dsp

#include "GraphicEQ.h"

namespace af::dsp
{
void GraphicEQDesigner::setSampleRate (double sampleRate)
{
    fs = sampleRate;
    constexpr int N = kNumBands;
    constexpr double probeDb = 12.0;

    // Interaction matrix M[j][i]: dB at centre j produced by band i at +probeDb, per dB.
    std::array<std::array<double, N>, N> M {};
    for (int i = 0; i < N; ++i)
    {
        const auto c = BiquadCoeffs::peaking (fs, kCentres[(size_t) i], kQ, probeDb);
        for (int j = 0; j < N; ++j)
            M[(size_t) j][(size_t) i] = c.magnitudeDb (kCentres[(size_t) j], fs) / probeDb;
    }

    // Invert with Gauss-Jordan elimination (matrix is diagonally dominant).
    std::array<std::array<double, 2 * N>, N> aug {};
    for (int r = 0; r < N; ++r)
    {
        for (int c = 0; c < N; ++c)
            aug[(size_t) r][(size_t) c] = M[(size_t) r][(size_t) c];
        aug[(size_t) r][(size_t) (N + r)] = 1.0;
    }
    for (int col = 0; col < N; ++col)
    {
        int pivot = col;
        for (int r = col + 1; r < N; ++r)
            if (std::abs (aug[(size_t) r][(size_t) col]) > std::abs (aug[(size_t) pivot][(size_t) col]))
                pivot = r;
        std::swap (aug[(size_t) col], aug[(size_t) pivot]);
        const double p = aug[(size_t) col][(size_t) col];
        for (auto& v : aug[(size_t) col])
            v /= p;
        for (int r = 0; r < N; ++r)
        {
            if (r == col)
                continue;
            const double f = aug[(size_t) r][(size_t) col];
            for (int c = 0; c < 2 * N; ++c)
                aug[(size_t) r][(size_t) c] -= f * aug[(size_t) col][(size_t) c];
        }
    }
    for (int r = 0; r < N; ++r)
        for (int c = 0; c < N; ++c)
            inverse[(size_t) r][(size_t) c] = aug[(size_t) r][(size_t) (N + c)];
}

void GraphicEQDesigner::design (const std::array<float, kNumBands>& sliderDb,
                                std::array<BiquadCoeffs, kNumBands>& out) const
{
    constexpr int N = kNumBands;
    std::array<double, N> g {};
    for (int i = 0; i < N; ++i)
    {
        double s = 0.0;
        for (int j = 0; j < N; ++j)
            s += inverse[(size_t) i][(size_t) j] * sliderDb[(size_t) j];
        g[(size_t) i] = clampv (s, -kMaxSectionDb, kMaxSectionDb);
    }

    // One refinement pass: peaking-filter dB responses are not perfectly linear in gain,
    // so correct the residual error at the centres using the same inverse matrix.
    for (int i = 0; i < N; ++i)
        out[(size_t) i] = BiquadCoeffs::peaking (fs, kCentres[(size_t) i], kQ, g[(size_t) i]);
    std::array<double, N> err {};
    for (int j = 0; j < N; ++j)
        err[(size_t) j] = sliderDb[(size_t) j] - responseDb (out, kCentres[(size_t) j], fs);
    for (int i = 0; i < N; ++i)
    {
        double s = 0.0;
        for (int j = 0; j < N; ++j)
            s += inverse[(size_t) i][(size_t) j] * err[(size_t) j];
        g[(size_t) i] = clampv (g[(size_t) i] + s, -kMaxSectionDb, kMaxSectionDb);
        out[(size_t) i] = BiquadCoeffs::peaking (fs, kCentres[(size_t) i], kQ, g[(size_t) i]);
    }
}

double GraphicEQDesigner::responseDb (const std::array<BiquadCoeffs, kNumBands>& c, double freq, double fs)
{
    double db = 0.0;
    for (const auto& s : c)
        db += s.magnitudeDb (freq, fs);
    return db;
}

void GraphicEQ::prepare (double sampleRate)
{
    designer.setSampleRate (sampleRate);
    last.fill (1e9f);
    setBands ({ 0, 0, 0, 0, 0 });
    reset();
}

void GraphicEQ::reset() noexcept
{
    for (auto& s : sections)
        s.reset();
}

void GraphicEQ::setBands (const std::array<float, GraphicEQDesigner::kNumBands>& sliderDb)
{
    if (sliderDb == last)
        return;
    last = sliderDb;
    std::array<BiquadCoeffs, GraphicEQDesigner::kNumBands> c;
    designer.design (sliderDb, c);
    for (size_t i = 0; i < sections.size(); ++i)
        sections[i].setCoeffs (c[i]);
}

} // namespace af::dsp

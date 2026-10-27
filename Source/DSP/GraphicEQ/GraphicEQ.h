// AMP FORGE — five-band guitar graphic EQ (80 / 240 / 750 / 2200 / 6600 Hz).
//
// Five broad peaking sections in series. Because neighbouring bands overlap, simply
// setting each section's gain to the slider value would over- or under-shoot at the
// centre frequencies when several sliders move together. We pre-compute the band
// interaction matrix (dB response of each band at every centre frequency) and solve
// for the section gains so the combined response hits the slider values at the five
// centres (cf. the "interaction matrix" method for graphic EQ design, Välimäki & Liski).
// The response curve shown in the UI is computed from the very same coefficients.
#pragma once

#include "../Utilities/Filters.h"
#include <array>

namespace af::dsp
{
class GraphicEQDesigner
{
public:
    static constexpr int kNumBands = 5;
    static constexpr std::array<double, kNumBands> kCentres { 80.0, 240.0, 750.0, 2200.0, 6600.0 };
    static constexpr double kQ = 0.9;           // ~1.5 octave bandwidth: broad, musical bands
    static constexpr double kMaxSectionDb = 20; // safety clamp on solved section gains

    explicit GraphicEQDesigner (double sampleRate = 48000.0) { setSampleRate (sampleRate); }

    void setSampleRate (double sampleRate);

    /** Slider dB values -> section coefficients (corrected for band interaction). */
    void design (const std::array<float, kNumBands>& sliderDb,
                 std::array<BiquadCoeffs, kNumBands>& out) const;

    /** Combined magnitude (dB) of a designed set at a frequency. */
    static double responseDb (const std::array<BiquadCoeffs, kNumBands>& c, double freq, double fs);

    double getSampleRate() const noexcept { return fs; }

private:
    double fs = 48000.0;
    std::array<std::array<double, kNumBands>, kNumBands> inverse {}; // section dB per target dB
};

class GraphicEQ
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    /** Call at control rate with smoothed slider values. Cheap (5x5 solve + 5 designs). */
    void setBands (const std::array<float, GraphicEQDesigner::kNumBands>& sliderDb);

    float process (float x) noexcept
    {
        for (auto& s : sections)
            x = s.process (x);
        return x;
    }

private:
    GraphicEQDesigner designer;
    std::array<Biquad, GraphicEQDesigner::kNumBands> sections;
    std::array<float, GraphicEQDesigner::kNumBands> last { 1e9f, 1e9f, 1e9f, 1e9f, 1e9f };
};

} // namespace af::dsp

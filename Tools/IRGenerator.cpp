// ZackAMP — procedural cabinet impulse response generator.
//
// Builds the factory IR set from scratch (no recorded or third-party material), so it can
// be redistributed freely. Each IR is designed as a magnitude response from physically
// motivated ingredients — speaker resonance and suspension roll-off, open-back dipole
// cancellation or closed-back box resonance, cone break-up peaks, voice-coil inductance
// roll-off, microphone position (cap / edge / off-axis) and room/distance reflections —
// then converted to minimum phase via the real cepstrum. These are honest *approximations*
// of cabinet character, not captures of real cabinets. Output: 48 kHz mono 24-bit WAV.
//
// Usage: ampforge_irgen <output-dir>
#include "DSP/Utilities/DspCommon.h"
#include "DSP/Utilities/FFT.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace af::dsp;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kFFT = 16384;
constexpr int kLength = 4096; // 85 ms

struct Peak { double hz, db, q; };

struct CabDesign
{
    std::string id, file, name, description;
    double resonanceHz;   // speaker-in-box resonance
    double resonanceQ;
    double resonanceDb;   // extra bump at resonance (closed-back boxes)
    bool openBack;
    double dipoleHz;      // open back: cancellation corner
    std::vector<Peak> peaks;
    double lowpassHz;     // break-up roll-off corner
    int lowpassOrder;     // 2nd-order sections
    double rippleDb;      // irregular cone response
    unsigned seed;
    double offAxisDb;     // high shelf cut above ~2 kHz for off-axis / edge placement
    double roomAmount;    // 0 = close mic, 1 = distant mic with reflections
};

double peakingDb (double f, const Peak& p)
{
    // Magnitude (dB) of an analogue peaking filter, evaluated in log frequency.
    const double A = std::pow (10.0, p.db / 40.0);
    const double w = f / p.hz;
    const std::complex<double> s (0.0, w);
    const auto num = s * s + s * (A / p.q) + 1.0;
    const auto den = s * s + s / (A * p.q) + 1.0;
    return 20.0 * std::log10 (std::abs (num / den));
}

double designMagnitudeDb (const CabDesign& d, double f, const std::vector<double>& ripple)
{
    f = std::max (f, 1.0);
    double db = 0.0;

    // Speaker + box high-pass (2nd order around resonance).
    {
        const double w = f / d.resonanceHz;
        const std::complex<double> s (0.0, w);
        const auto h = (s * s) / (s * s + s / d.resonanceQ + 1.0);
        db += 20.0 * std::log10 (std::abs (h));
    }
    db += peakingDb (f, { d.resonanceHz * 1.15, d.resonanceDb, 1.6 });
    if (d.openBack)
    {
        // Rear wave cancellation: an extra first-order roll-off below the dipole corner.
        const double w = f / d.dipoleHz;
        db += 20.0 * std::log10 (w / std::sqrt (1.0 + w * w));
    }
    for (const auto& p : d.peaks)
        db += peakingDb (f, p);

    // Cone break-up: steep roll-off.
    for (int i = 0; i < d.lowpassOrder; ++i)
    {
        const double w = f / d.lowpassHz;
        const std::complex<double> s (0.0, w);
        db += 20.0 * std::log10 (std::abs (1.0 / (s * s + s / 0.75 + 1.0)));
    }
    // Off-axis / edge: high shelf cut.
    if (d.offAxisDb != 0.0)
        db += d.offAxisDb / (1.0 + std::pow (2000.0 / f, 2.0));

    // Irregular response above ~800 Hz (smooth noise in log-frequency).
    const double lf = std::log2 (f / 20.0) * 12.0; // semitone index
    const int i0 = (int) lf;
    if (i0 >= 0 && i0 + 1 < (int) ripple.size())
    {
        const double fr = lf - i0;
        const double r = ripple[(size_t) i0] * (1.0 - fr) + ripple[(size_t) i0 + 1] * fr;
        db += r * d.rippleDb * std::min (1.0, std::max (0.0, (f - 800.0) / 1200.0));
    }
    return std::max (db, -120.0);
}

std::vector<float> buildIR (const CabDesign& d)
{
    std::mt19937 rng (d.seed);
    std::normal_distribution<double> nd (0.0, 1.0);
    std::vector<double> ripple (200);
    double prev = 0.0;
    for (auto& r : ripple)
    {
        prev = 0.6 * prev + 0.4 * nd (rng);
        r = prev;
    }

    RealFFT fft (kFFT);
    const int bins = kFFT / 2 + 1;
    std::vector<RealFFT::Complex> spec ((size_t) bins);
    std::vector<float> cep ((size_t) kFFT);

    // Real cepstrum of the log-magnitude.
    for (int k = 0; k < bins; ++k)
    {
        const double f = (double) k * kRate / kFFT;
        const double lnMag = designMagnitudeDb (d, std::max (f, 5.0), ripple) / 20.0 * std::log (10.0);
        spec[(size_t) k] = { (float) lnMag, 0.0f };
    }
    fft.inverse (spec.data(), cep.data());

    // Fold to minimum phase.
    std::vector<float> folded ((size_t) kFFT, 0.0f);
    folded[0] = cep[0];
    for (int n = 1; n < kFFT / 2; ++n)
        folded[(size_t) n] = 2.0f * cep[(size_t) n];
    folded[(size_t) kFFT / 2] = cep[(size_t) kFFT / 2];
    fft.forward (folded.data(), spec.data());
    for (auto& c : spec)
        c = std::exp (std::complex<float> (c.real(), c.imag()));
    std::vector<float> ir ((size_t) kFFT);
    fft.inverse (spec.data(), ir.data());

    // Room / distance: sparse early reflections (floor, back wall) with high-frequency loss.
    if (d.roomAmount > 0.0)
    {
        std::vector<float> wet ((size_t) kFFT, 0.0f);
        const double taps[][2] = { { 1.9, 0.45 }, { 3.4, 0.3 }, { 5.8, 0.25 }, { 9.7, 0.18 }, { 14.2, 0.12 }, { 21.0, 0.08 } };
        for (const auto& t : taps)
        {
            const int delay = (int) (t[0] * 0.001 * kRate);
            const float g = (float) (t[1] * d.roomAmount);
            float lp = 0.0f;
            for (int i = 0; i + delay < kFFT; ++i)
            {
                lp += 0.35f * (ir[(size_t) i] - lp); // reflections lose top end
                wet[(size_t) (i + delay)] += g * lp;
            }
        }
        for (int i = 0; i < kFFT; ++i)
            ir[(size_t) i] += wet[(size_t) i];
    }

    std::vector<float> out (ir.begin(), ir.begin() + kLength);
    const int fade = 512;
    for (int i = 0; i < fade; ++i)
        out[(size_t) (kLength - 1 - i)] *= (float) (0.5 - 0.5 * std::cos (kPi * i / fade));

    float pk = 0.0f;
    for (float v : out)
        pk = std::max (pk, std::abs (v));
    for (auto& v : out)
        v *= 0.7f / pk; // peak-normalised file; the engine normalises perceived level on load
    return out;
}

bool writeWav24 (const std::string& path, const std::vector<float>& data, int rate)
{
    std::ofstream f (path, std::ios::binary);
    if (! f)
        return false;
    auto u32 = [&] (uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); };
    auto u16 = [&] (uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); };
    const uint32_t dataBytes = (uint32_t) data.size() * 3;
    f.write ("RIFF", 4); u32 (36 + dataBytes); f.write ("WAVE", 4);
    f.write ("fmt ", 4); u32 (16); u16 (1); u16 (1); u32 ((uint32_t) rate); u32 ((uint32_t) rate * 3); u16 (3); u16 (24);
    f.write ("data", 4); u32 (dataBytes);
    for (float v : data)
    {
        const int32_t s = (int32_t) std::lround (clampv (v, -1.0f, 1.0f) * 8388607.0f);
        const char b[3] = { (char) (s & 0xff), (char) ((s >> 8) & 0xff), (char) ((s >> 16) & 0xff) };
        f.write (b, 3);
    }
    return (bool) f;
}

std::vector<CabDesign> designs()
{
    return {
        { "factory:am-1x12-open-cap", "am-1x12-open-cap.wav", "US 1x12 Open — Cap",
          "Open-back 1x12 combo, mic at the dust cap: bright, airy, light low end.",
          95, 0.76, 0.5, true, 140, { { 1200, -2.5, 1.0 }, { 2600, 4.0, 1.6 }, { 4300, 3.0, 2.2 } }, 5600, 2, 1.2, 11, 0.0, 0.0 },
        { "factory:am-1x12-open-edge", "am-1x12-open-edge.wav", "US 1x12 Open — Edge",
          "Same combo with the mic toward the cone edge: rounder and warmer.",
          95, 0.76, 0.5, true, 140, { { 1200, -2.0, 1.0 }, { 2400, 2.5, 1.6 } }, 4300, 2, 1.0, 12, -5.0, 0.0 },
        { "factory:am-2x12-open-cap", "am-2x12-open-cap.wav", "US 2x12 Open — Cap",
          "Open-back 2x12: glassy clean voice, scooped low mids, extended top.",
          85, 0.72, 0.7, true, 115, { { 450, -2.0, 0.9 }, { 2200, 3.5, 1.4 }, { 3800, 3.5, 2.0 }, { 5200, 1.5, 3.0 } }, 6200, 2, 1.0, 13, 0.0, 0.0 },
        { "factory:am-2x12-open-room", "am-2x12-open-room.wav", "US 2x12 Open — Room",
          "Open-back 2x12 with a distant mic and early reflections: spacious cleans.",
          85, 0.72, 0.5, true, 115, { { 450, -1.5, 0.9 }, { 2200, 2.5, 1.4 }, { 3800, 2.0, 2.0 } }, 5200, 2, 1.0, 14, -2.0, 0.8 },
        { "factory:br-4x12-v25-cap", "br-4x12-v25-cap.wav", "UK 4x12 Vintage 25W — Cap",
          "Closed-back 4x12 with vintage low-power speakers: warm lows, vocal 2 kHz mids.",
          100, 0.86, 1.6, false, 0, { { 700, 1.0, 1.0 }, { 1900, 5.0, 1.5 }, { 3200, 2.0, 2.5 }, { 4100, -3.0, 3.0 } }, 5000, 3, 1.3, 21, 0.0, 0.0 },
        { "factory:br-4x12-v25-edge", "br-4x12-v25-edge.wav", "UK 4x12 Vintage 25W — Edge",
          "Vintage 4x12, mic toward the edge: thicker and smoother for crunch.",
          100, 0.86, 1.6, false, 0, { { 700, 1.5, 1.0 }, { 1800, 3.5, 1.5 } }, 4200, 3, 1.1, 22, -4.0, 0.0 },
        { "factory:br-4x12-h75-cap", "br-4x12-h75-cap.wav", "UK 4x12 Heritage 75W — Cap",
          "Closed-back 4x12 with higher-power speakers: tight, forward upper mids for rock.",
          110, 0.9, 1.4, false, 0, { { 400, -1.0, 1.0 }, { 2500, 5.5, 1.7 }, { 4300, 3.0, 2.4 } }, 5400, 3, 1.4, 31, 0.0, 0.0 },
        { "factory:br-4x12-h75-offaxis", "br-4x12-h75-offaxis.wav", "UK 4x12 Heritage 75W — Off-axis",
          "75 W 4x12 with an angled mic: tames fizz on high-gain tones.",
          110, 0.9, 1.4, false, 0, { { 400, -1.0, 1.0 }, { 2400, 4.0, 1.7 } }, 4600, 3, 1.2, 32, -6.0, 0.0 },
        { "factory:md-2x12-closed-cap", "md-2x12-closed-cap.wav", "Modern 2x12 Closed — Cap",
          "Closed-back 2x12: focused mids, firm low end, smooth top for leads.",
          95, 0.82, 1.1, false, 0, { { 300, 1.0, 1.0 }, { 900, 1.5, 1.2 }, { 2600, 3.5, 1.5 }, { 3900, 1.5, 2.5 } }, 5000, 3, 1.1, 41, 0.0, 0.0 },
        { "factory:md-4x12-heavy-cap", "md-4x12-heavy-cap.wav", "Modern 4x12 Heavy — Cap",
          "Large closed 4x12 tuned for high gain: deep resonance, tight lows, scooped 500 Hz.",
          85, 0.93, 2.0, false, 0, { { 500, -2.5, 1.0 }, { 2800, 4.5, 1.6 }, { 4600, 2.0, 2.4 } }, 5200, 3, 1.3, 51, 0.0, 0.0 },
        { "factory:md-4x12-heavy-edge", "md-4x12-heavy-edge.wav", "Modern 4x12 Heavy — Edge",
          "Heavy 4x12, edge position: darker and thicker for rhythm.",
          85, 0.93, 2.0, false, 0, { { 500, -2.0, 1.0 }, { 2600, 3.0, 1.6 } }, 4300, 3, 1.1, 52, -4.0, 0.0 },
        { "factory:bt-1x12-closed-cap", "bt-1x12-closed-cap.wav", "Boutique 1x12 Closed — Cap",
          "Compact closed-back 1x12: punchy mids and controlled lows, great for lead.",
          105, 0.82, 0.9, false, 0, { { 800, 2.0, 1.0 }, { 2300, 3.0, 1.5 }, { 3600, 2.0, 2.4 } }, 5000, 3, 1.0, 61, -1.0, 0.0 },
    };
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: %s <output-dir>\n", argv[0]);
        return 1;
    }
    const std::string dir = argv[1];
    std::string manifest = "[\n";
    bool first = true;
    for (const auto& d : designs())
    {
        auto ir = buildIR (d);
        if (! writeWav24 (dir + "/" + d.file, ir, (int) kRate))
        {
            std::fprintf (stderr, "failed to write %s\n", d.file.c_str());
            return 1;
        }
        std::printf ("wrote %s (%s)\n", d.file.c_str(), d.name.c_str());
        manifest += std::string (first ? "" : ",\n") + "  { \"id\": \"" + d.id + "\", \"file\": \"" + d.file
                    + "\", \"name\": \"" + d.name + "\", \"description\": \"" + d.description + "\" }";
        first = false;
    }
    // Utility IR: unit impulse (no cabinet colouration). Honestly labelled as a test/utility IR.
    {
        std::vector<float> dirac ((size_t) 64, 0.0f);
        dirac[0] = 0.7f;
        writeWav24 (dir + "/util-flat.wav", dirac, (int) kRate);
        manifest += ",\n  { \"id\": \"factory:util-flat\", \"file\": \"util-flat.wav\", \"name\": \"Utility — Flat (test IR)\", "
                    "\"description\": \"Unit impulse: no cabinet colouration. For testing and A/B comparison.\" }";
    }
    manifest += "\n]\n";
    std::ofstream (dir + "/manifest.json") << manifest;
    std::printf ("wrote manifest.json\n");
    return 0;
}

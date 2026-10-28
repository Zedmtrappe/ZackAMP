// ZackAMP — power amplifier / output stage (run at the oversampled rate).
//
// Phase inverter saturation -> push-pull output stage with supply sag -> output
// transformer band limits -> speaker-load interaction. Presence and depth/resonance
// are modelled the way they work in real amps: they reduce negative feedback at high
// or low frequencies, which we approximate with shelving boosts ahead of the output
// stage whose maximum depends on how much feedback the voicing has.
#pragma once

#include "../Utilities/DspCommon.h"
#include "../Utilities/Filters.h"
#include "../Amplifiers/Valves.h"

#include <array>

#include <vector>

namespace af::dsp
{
struct PowerAmpSpec
{
    float piDrive = 0.25f;        // phase inverter saturation amount
    float hardness = 2.5f;        // knee exponent of output stage (higher NFB -> harder knee)
    float asymmetry = 0.04f;      // push-pull mismatch -> small even-order content
    float sag = 0.25f;            // supply sag depth (tube rectifier > silicon)
    float sagReleaseMs = 120.0f;
    float presenceHz = 3500.0f, presenceMaxDb = 8.0f;
    float depthHz = 90.0f, depthMaxDb = 0.0f; // 0 = no depth/resonance control
    float fixedPresence = 0.5f;   // knob value used when the voicing has no presence control
    float otHpHz = 45.0f, otLpHz = 9000.0f;
    float speakerLoad = 0.5f;     // 0 = high NFB (flat), 1 = low NFB (follows speaker impedance)
};

/** Precomputed soft-knee transfer y = v / (1 + |v|^h)^(1/h) for fast evaluation. */
class KneeTable
{
public:
    void build (float hardness)
    {
        table.resize (kSize + 2);
        for (int i = 0; i < kSize + 2; ++i)
        {
            const double v = (double) i / kSize * kRange;
            table[(size_t) i] = (float) (v / std::pow (1.0 + std::pow (v, (double) hardness), 1.0 / hardness));
        }
    }

    float operator() (float v) const noexcept
    {
        const float a = std::min (std::abs (v), kRange - 1e-3f);
        const float pos = a * (kSize / kRange);
        const int i = (int) pos;
        const float f = pos - (float) i;
        const float y = table[(size_t) i] + f * (table[(size_t) i + 1] - table[(size_t) i]);
        return v < 0.0f ? -y : y;
    }

private:
    static constexpr int kSize = 4096;
    static constexpr float kRange = 16.0f;
    std::vector<float> table;
};

class PowerAmp
{
public:
    /** Allocates (one knee table per power valve): call off the audio thread. */
    void prepare (double sampleRate, const PowerAmpSpec& s, int defaultValve);
    void reset() noexcept;

    /** Knobs 0..1, call at control rate. */
    void setControls (float presence, float depth);

    /** Selects the power valve relative to the voicing's default. Real-time safe (no allocation);
        changes the transfer curve, so call it while the output is faded out. */
    void setValve (int valveIndex) noexcept;

    /** sagKnob 0..1 (0.5 = the voicing's own sag), variac = supply voltage ratio (1 = nominal).
        Call at control rate with smoothed values. */
    void setSupply (float sagKnob, float variac) noexcept;

    float process (float x) noexcept
    {
        x = depthShelf.process (x);
        x = presenceShelf.process (x);

        // Long-tailed-pair phase inverter: gentle symmetric saturation.
        x = tanhRational (x * spec.piDrive) / spec.piDrive;

        // Supply sag lowers headroom while the stage works hard; a lower variac voltage
        // lowers headroom all the time.
        const float g = inputDrive / (1.0f + sag * env);
        const float v = x * g;
        const auto& k = knees[(size_t) valve];
        float y = k (v + asymmetry) - kneeRest;

        const float rect = std::abs (y);
        env = rect > env ? rect + sagAttack * (env - rect) : rect + sagRelease * (env - rect);

        y = tiltLow.process (y);
        y = tiltMid.process (y);
        y = tiltHigh.process (y);
        y = otHigh.process (y);
        y = otLow.process (y);
        y = speakerLow.process (y);
        y = speakerHigh.process (y);
        return y * outputLevel;
    }

private:
    void updateSupply() noexcept;

    PowerAmpSpec spec;
    double fs = 48000.0;
    int defaultValve = 0, valve = 0;
    std::array<KneeTable, kNumPowerValves> knees;
    float kneeRest = 0.0f, asymmetry = 0.0f;
    float valveDrive = 1.0f, valveSag = 1.0f, valveLevel = 1.0f;
    float sagKnob = 0.5f, variac = 1.0f;
    float sag = 0.0f, inputDrive = 1.0f, outputLevel = 1.0f;
    Biquad presenceShelf, depthShelf, otLow, speakerLow, speakerHigh, tiltLow, tiltMid, tiltHigh;
    OnePole otHigh;
    float env = 0.0f, sagAttack = 0.0f, sagRelease = 0.0f;
    float lastPresence = -1.0f, lastDepth = -1.0f;
};

} // namespace af::dsp

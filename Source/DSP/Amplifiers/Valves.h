// ZackAMP — selectable preamp (V1) and power valves.
//
// Each amp voicing is calibrated with its own "usual" valves. A different selection is applied
// *relative* to those: the ratio (or difference) between the chosen valve's characteristics and
// the voicing's default valve modifies the stage. So the default selection reproduces the
// calibrated sound exactly, and a swap changes gain, headroom, breakup character and tonal
// tilt the way that swap would in that amp. Characteristics are musically chosen, not
// datasheet simulations.
#pragma once

#include <array>

namespace af::dsp
{
/** Preamp valve, applied to the input stage (V1) — the valve players most often swap. */
struct PreampValve
{
    const char* name;
    float gain;     // relative amplification factor (12AX7 = 1)
    float headroom; // relative grid swing before clipping
    float softness; // > 1: softer, later cutoff-side limiting
};

constexpr std::array<PreampValve, 4> kPreampValves { {
    { "12AX7", 1.00f, 1.00f, 1.00f },
    { "5751", 0.72f, 1.10f, 1.00f },
    { "12AY7", 0.48f, 1.25f, 1.10f },
    { "12AT7", 0.60f, 1.45f, 0.95f },
} };

/** Power valve pair (push-pull output stage). */
struct PowerValve
{
    const char* name;
    float hardness;   // relative knee hardness (higher = cleaner, then more abrupt)
    float drive;      // relative drive into the output stage (higher = earlier breakup)
    float asymmetry;  // push-pull mismatch (even-order content)
    float sag;        // relative supply-sag sensitivity
    float lowDb, midDb, highDb; // tonal tilt: 120 Hz shelf, 800 Hz peak, 3 kHz shelf
};

constexpr std::array<PowerValve, 5> kPowerValves { {
    { "6L6", 1.00f, 1.00f, 0.03f, 1.00f, 0.5f, -0.5f, 0.0f },
    { "EL34", 0.85f, 1.25f, 0.05f, 1.10f, -0.5f, 1.5f, 0.5f },
    { "6V6", 0.80f, 1.60f, 0.05f, 1.25f, -1.0f, 0.5f, 0.5f },
    { "EL84", 0.75f, 1.50f, 0.06f, 1.30f, -1.5f, 1.0f, 1.5f },
    { "KT88", 1.20f, 0.75f, 0.02f, 0.80f, 1.0f, -1.0f, -0.5f },
} };

constexpr int kNumPreampValves = (int) kPreampValves.size();
constexpr int kNumPowerValves = (int) kPowerValves.size();
} // namespace af::dsp

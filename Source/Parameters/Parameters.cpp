#include "Parameters.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace af
{
namespace
{
    const char* const kAmpModels[] = { "Lacquer Clean", "Lacquer Tweed", "Albion Plexi",
                                       "Albion Lead", "Cascade Lead", "Cascade Heavy" };
    const char* const kEqPositions[] = { "Pre Power Amp", "Post Amp" };
    const char* const kDivisions[] = { "1/2", "1/4", "1/4 dotted", "1/4 triplet", "1/8", "1/8 dotted", "1/8 triplet", "1/16" };
    constexpr float kDivisionBeats[] = { 2.0f, 1.0f, 1.5f, 2.0f / 3.0f, 0.5f, 0.75f, 1.0f / 3.0f, 0.25f };

    constexpr auto LIN = ParamScale::linear;
    constexpr auto LOG = ParamScale::logarithmic;
    constexpr auto CHO = ParamScale::choice;
    constexpr auto TOG = ParamScale::toggle;

    const ParamDef kDefs[kNumParams] = {
        { "in.trim", "Input Trim", "dB", -24.f, 24.f, 0.f, LIN, 1.f, "Input", "Aligns your interface level with the amp input. Aim for strong picking to peak around -12 dBFS on the input meter." },
        { "engine.bypass", "Bypass", "", 0.f, 1.f, 0.f, TOG, 1.f, "Global", "Bypasses all processing (dry input to output). For checking your signal and latency." },

        { "gate.enabled", "Gate", "", 0.f, 1.f, 0.f, TOG, 1.f, "Gate", "Noise gate: listens to the clean guitar input and mutes amp hiss between phrases." },
        { "gate.threshold", "Threshold", "dB", -96.f, -20.f, -62.f, LIN, 1.f, "Gate", "Input level below which the gate closes (with 6 dB hysteresis)." },
        { "gate.release", "Release", "ms", 10.f, 600.f, 120.f, LOG, 1.f, "Gate", "How gently the gate closes. Longer keeps more sustain." },

        { "drive.enabled", "Drive", "", 0.f, 1.f, 0.f, TOG, 1.f, "Drive", "Overdrive / boost pedal in front of the amp." },
        { "drive.drive", "Drive", "", 0.f, 1.f, 0.25f, LIN, 10.f, "Drive", "Pedal clipping amount. Near zero it works as a tightening mid boost." },
        { "drive.tone", "Tone", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Drive", "Pedal treble." },
        { "drive.level", "Level", "dB", -12.f, 12.f, 3.f, LIN, 1.f, "Drive", "Pedal output level: how hard it pushes the amp." },

        { "amp.model", "Amp Model", "", 0.f, 5.f, 0.f, CHO, 1.f, "Amp", "Amplifier model.", kAmpModels, 6 },
        { "amp.gain", "Gain", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Amp", "Preamp volume / gain pot." },
        { "amp.bass", "Bass", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Amp", "Tone stack bass. Passive: interacts with mid and treble." },
        { "amp.mid", "Middle", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Amp", "Tone stack middle." },
        { "amp.treble", "Treble", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Amp", "Tone stack treble." },
        { "amp.presence", "Presence", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Amp", "Power-amp high-frequency feedback shaping." },
        { "amp.depth", "Depth", "", 0.f, 1.f, 0.4f, LIN, 10.f, "Amp", "Power-amp low-frequency resonance." },
        { "amp.master", "Master", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Amp", "Master volume: how hard the power amp is driven." },
        { "amp.bright", "Bright", "", 0.f, 1.f, 0.f, TOG, 1.f, "Amp", "Bright cap across the volume pot: more sparkle at low volume settings." },

        { "eq.enabled", "Graphic EQ", "", 0.f, 1.f, 0.f, TOG, 1.f, "EQ", "Five-band graphic EQ." },
        { "eq.position", "EQ Position", "", 0.f, 1.f, 0.f, CHO, 1.f, "EQ", "Pre Power Amp (classic position between preamp and power amp) or Post Amp (before the cabinet).", kEqPositions, 2 },
        { "eq.b80", "80", "dB", -12.f, 12.f, 0.f, LIN, 1.f, "EQ", "80 Hz band." },
        { "eq.b240", "240", "dB", -12.f, 12.f, 0.f, LIN, 1.f, "EQ", "240 Hz band." },
        { "eq.b750", "750", "dB", -12.f, 12.f, 0.f, LIN, 1.f, "EQ", "750 Hz band." },
        { "eq.b2200", "2.2k", "dB", -12.f, 12.f, 0.f, LIN, 1.f, "EQ", "2.2 kHz band." },
        { "eq.b6600", "6.6k", "dB", -12.f, 12.f, 0.f, LIN, 1.f, "EQ", "6.6 kHz band." },

        { "cab.enabled", "Cabinet", "", 0.f, 1.f, 1.f, TOG, 1.f, "Cabinet", "Speaker cabinet simulation. Turn off when using a real cabinet or an external cab simulator." },
        { "cab.dual", "Dual Cab", "", 0.f, 1.f, 0.f, TOG, 1.f, "Cabinet", "Blend two cabinet IRs with independent level and pan." },
        { "cab.a.level", "Cab A Level", "dB", -30.f, 6.f, 0.f, LIN, 1.f, "Cabinet", "Cabinet A level." },
        { "cab.a.pan", "Cab A Pan", "", -1.f, 1.f, 0.f, LIN, 1.f, "Cabinet", "Cabinet A pan (dual mode)." },
        { "cab.b.level", "Cab B Level", "dB", -30.f, 6.f, -3.f, LIN, 1.f, "Cabinet", "Cabinet B level (dual mode)." },
        { "cab.b.pan", "Cab B Pan", "", -1.f, 1.f, 0.f, LIN, 1.f, "Cabinet", "Cabinet B pan (dual mode)." },

        { "chorus.enabled", "Chorus", "", 0.f, 1.f, 0.f, TOG, 1.f, "Chorus", "Stereo chorus." },
        { "chorus.rate", "Rate", "Hz", 0.05f, 5.f, 0.6f, LOG, 1.f, "Chorus", "Modulation speed." },
        { "chorus.depth", "Depth", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Chorus", "Modulation depth." },
        { "chorus.mix", "Mix", "%", 0.f, 1.f, 0.5f, LIN, 100.f, "Chorus", "Chorus blend." },

        { "delay.enabled", "Delay", "", 0.f, 1.f, 0.f, TOG, 1.f, "Delay", "Stereo delay. Repeats ring out naturally when switched off." },
        { "delay.time", "Time", "ms", 20.f, 2000.f, 380.f, LOG, 1.f, "Delay", "Delay time (when not synced)." },
        { "delay.sync", "Sync", "", 0.f, 1.f, 0.f, TOG, 1.f, "Delay", "Sync delay time to the tempo." },
        { "delay.division", "Division", "", 0.f, 7.f, 5.f, CHO, 1.f, "Delay", "Note value when synced.", kDivisions, 8 },
        { "delay.feedback", "Feedback", "%", 0.f, 0.95f, 0.3f, LIN, 100.f, "Delay", "Number of repeats." },
        { "delay.mix", "Mix", "%", 0.f, 1.f, 0.25f, LIN, 100.f, "Delay", "Level of the repeats." },
        { "delay.tone", "Tone", "Hz", 800.f, 12000.f, 4500.f, LOG, 1.f, "Delay", "Darkens each repeat." },

        { "reverb.enabled", "Reverb", "", 0.f, 1.f, 0.f, TOG, 1.f, "Reverb", "Stereo reverb. Tails ring out naturally when switched off." },
        { "reverb.mix", "Mix", "%", 0.f, 1.f, 0.25f, LIN, 100.f, "Reverb", "Reverb level." },
        { "reverb.decay", "Decay", "s", 0.3f, 10.f, 1.8f, LOG, 1.f, "Reverb", "Reverb time (RT60)." },
        { "reverb.tone", "Tone", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Reverb", "Dark to bright." },

        { "out.level", "Output", "dB", -60.f, 12.f, 0.f, LIN, 1.f, "Output", "Final output level. A safety limiter at -1 dBFS protects your monitors." },
        { "global.tempo", "Tempo", "bpm", 40.f, 240.f, 120.f, LIN, 1.f, "Global", "Tempo for synced delay." },
    };
} // namespace

const ParamDef& paramDef (int index) noexcept { return kDefs[std::clamp (index, 0, kNumParams - 1)]; }

int findParamIndex (const std::string& id) noexcept
{
    for (int i = 0; i < kNumParams; ++i)
        if (id == kDefs[i].id)
            return i;
    return -1;
}

float delayDivisionBeats (int idx) noexcept
{
    return kDivisionBeats[std::clamp (idx, 0, (int) (sizeof (kDivisionBeats) / sizeof (float)) - 1)];
}

float sanitiseParamValue (int index, float v) noexcept
{
    const auto& d = paramDef (index);
    if (! std::isfinite (v))
        v = d.def;
    v = std::clamp (v, d.min, d.max);
    if (d.scale == ParamScale::choice)
        v = std::round (v);
    else if (d.scale == ParamScale::toggle)
        v = v >= 0.5f ? 1.0f : 0.0f;
    return v;
}

float plainToNormalised (int index, float v) noexcept
{
    const auto& d = paramDef (index);
    v = sanitiseParamValue (index, v);
    if (d.max <= d.min)
        return 0.0f;
    if (d.scale == ParamScale::logarithmic && d.min > 0.0f)
        return std::log (v / d.min) / std::log (d.max / d.min);
    return (v - d.min) / (d.max - d.min);
}

float normalisedToPlain (int index, float n) noexcept
{
    const auto& d = paramDef (index);
    n = std::clamp (std::isfinite (n) ? n : 0.0f, 0.0f, 1.0f);
    float v;
    if (d.scale == ParamScale::logarithmic && d.min > 0.0f)
        v = d.min * std::pow (d.max / d.min, n);
    else
        v = d.min + n * (d.max - d.min);
    return sanitiseParamValue (index, v);
}

std::string formatParamValue (int index, float v)
{
    const auto& d = paramDef (index);
    char buf[64];
    switch (d.scale)
    {
        case ParamScale::toggle:
            return v >= 0.5f ? "On" : "Off";
        case ParamScale::choice:
        {
            const int i = (int) std::lround (v);
            if (d.choices != nullptr && i >= 0 && i < d.numChoices)
                return d.choices[i];
            std::snprintf (buf, sizeof (buf), "%d", i);
            return buf;
        }
        default:
            break;
    }
    const float shown = v * d.displayScale;
    if (std::strcmp (d.unit, "Hz") == 0 && v >= 1000.0f)
        std::snprintf (buf, sizeof (buf), "%.1f kHz", v / 1000.0f);
    else if (std::strcmp (d.unit, "ms") == 0 && v >= 1000.0f)
        std::snprintf (buf, sizeof (buf), "%.2f s", v / 1000.0f);
    else if (d.unit[0] == 0)
    {
        if (d.min < 0.0f && d.max <= 1.0f) // pan
        {
            if (std::abs (v) < 0.01f)
                return "C";
            std::snprintf (buf, sizeof (buf), "%s%d", v < 0 ? "L" : "R", (int) std::lround (std::abs (v) * 100));
        }
        else
            std::snprintf (buf, sizeof (buf), "%.1f", shown);
    }
    else if (std::strcmp (d.unit, "%") == 0)
        std::snprintf (buf, sizeof (buf), "%d%%", (int) std::lround (shown));
    else if (std::strcmp (d.unit, "dB") == 0)
        std::snprintf (buf, sizeof (buf), "%+.1f dB", shown);
    else if (std::strcmp (d.unit, "Hz") == 0 && v < 10.0f)
        std::snprintf (buf, sizeof (buf), "%.2f Hz", shown);
    else
        std::snprintf (buf, sizeof (buf), "%.0f %s", shown, d.unit);
    return buf;
}

ParameterStore::ParameterStore() { resetToDefaults(); }

bool ParameterStore::set (int index, float plain) noexcept
{
    if (index < 0 || index >= kNumParams)
        return false;
    const float v = sanitiseParamValue (index, plain);
    const float old = values[(size_t) index].exchange (v, std::memory_order_relaxed);
    if (old != v)
    {
        changes.fetch_add (1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

void ParameterStore::resetToDefaults() noexcept
{
    for (int i = 0; i < kNumParams; ++i)
        values[(size_t) i].store (kDefs[i].def, std::memory_order_relaxed);
    changes.fetch_add (1, std::memory_order_relaxed);
}

std::array<float, kNumParams> ParameterStore::snapshot() const noexcept
{
    std::array<float, kNumParams> s {};
    for (int i = 0; i < kNumParams; ++i)
        s[(size_t) i] = get (i);
    return s;
}

} // namespace af

#include "Parameters.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace af
{
namespace
{
    const char* const kAmpModels[] = { "Tweed", "JC120", "AC30", "Plexi", "JCM800", "Coliseum", "MP1", "5150", "Dual", "Triple" };
    const char* const kPreampValveNames[] = { "12AX7", "5751", "12AY7", "12AT7" };
    const char* const kPowerValveNames[] = { "6L6", "EL34", "6V6", "EL84", "KT88" };
    const char* const kModModes[] = { "Chorus", "Flanger", "Script Phaser", "Phase 90" };
    const char* const kDriveTypes[] = { "Screamer", "RAT", "SD-1", "DS-1", "Guv'nor" };
    const char* const kDelayTypes[] = { "Studio", "Tape" };
    const char* const kCompTypes[] = { "Pristine", "LA-2A", "1176", "SSL Bus", "Fairchild 670", "dbx 160" };
    const char* const kCompRatios[] = { "2:1", "3:1", "4:1", "6:1", "10:1", "20:1" };
    const char* const kLa2aModes[] = { "Compress", "Limit" };
    const char* const k1176Ratios[] = { "4:1", "8:1", "12:1", "20:1", "All" };
    const char* const kSslRatios[] = { "2:1", "4:1", "10:1" };
    const char* const kFairchildTcs[] = { "TC 1", "TC 2", "TC 3", "TC 4", "TC 5", "TC 6" };
    const char* const kReverbTypes[] = { "Room", "Hall", "Spring", "Chamber", "Plate", "Lexicon", "Bricasti" };
    const char* const kLexPrograms[] = { "Concert Hall", "Random Hall", "Rich Plate", "Chamber" };
    const char* const kM7Programs[] = { "Large Hall", "Small Room", "Plate", "Chamber" };
    const char* const kEqPositions[] = { "Pre Power Amp", "Post Amp" };
    const char* const kDivisions[] = { "1/2", "1/4", "1/4 D", "1/4 T", "1/8", "1/8 D", "1/8 T", "1/16" }; // D = dotted, T = triplet
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

        { "comp.enabled", "CS-3", "", 0.f, 1.f, 0.f, TOG, 1.f, "Comp", "CS-3 style compressor pedal before the amp: squash and sustain." },
        { "comp.sustain", "Sustain", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Comp", "Amount of compression: lowers the threshold (make-up gain follows)." },
        { "comp.attack", "Attack", "", 0.f, 1.f, 0.4f, LIN, 10.f, "Comp", "Low: squashes the pick attack. High: lets the pick through before compressing." },
        { "comp.tone", "Tone", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Comp", "Treble shelf after the compressor (centre = flat)." },
        { "comp.level", "Level", "dB", -12.f, 12.f, 0.f, LIN, 1.f, "Comp", "Output level." },

        { "wah.enabled", "Auto-Wah", "", 0.f, 1.f, 0.f, TOG, 1.f, "Wah", "Envelope filter: the harder you pick, the higher it sweeps. Before the amp." },
        { "wah.sens", "Sens", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Wah", "How far your picking dynamics sweep the filter." },
        { "wah.range", "Range", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Wah", "Sweep range: low and narrow to wide and bright." },
        { "wah.peak", "Peak", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Wah", "Resonance of the filter: subtle to quacky." },
        { "wah.mix", "Mix", "%", 0.f, 1.f, 0.85f, LIN, 100.f, "Wah", "Filtered signal blended with the dry guitar." },

        { "drive.enabled", "Drive", "", 0.f, 1.f, 0.f, TOG, 1.f, "Drive", "Overdrive / boost pedal in front of the amp." },
        { "drive.drive", "Drive", "", 0.f, 1.f, 0.25f, LIN, 10.f, "Drive", "Pedal clipping amount. Near zero it works as a tightening mid boost." },
        { "drive.tone", "Tone", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Drive", "Pedal treble." },
        { "drive.level", "Level", "dB", -12.f, 12.f, 3.f, LIN, 1.f, "Drive", "Pedal output level: how hard it pushes the amp." },
        { "drive.type", "Type", "", 0.f, 4.f, 0.f, CHO, 1.f, "Drive", "Screamer: mid-hump soft clipping, tight lows. RAT: op-amp distortion into hard diodes; Tone is its Filter. SD-1: asymmetric soft clipping, smoother and more amp-like. DS-1: hard-clipping distortion with the scooped blend tone. Guv'nor: Marshall-in-a-box, hard clipping with full lows; Tone is its Treble.", kDriveTypes, 5 },

        { "amp.model", "Amp Model", "", 0.f, 9.f, 0.f, CHO, 1.f, "Amp", "Amplifier model.", kAmpModels, 10 },
        { "amp.gain", "Gain", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Amp", "Preamp volume / gain pot." },
        { "amp.bass", "Bass", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Amp", "Tone stack bass. Passive: interacts with mid and treble." },
        { "amp.mid", "Middle", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Amp", "Tone stack middle." },
        { "amp.treble", "Treble", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Amp", "Tone stack treble." },
        { "amp.presence", "Presence", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Amp", "Power-amp high-frequency feedback shaping." },
        { "amp.depth", "Depth", "", 0.f, 1.f, 0.4f, LIN, 10.f, "Amp", "Power-amp low-frequency resonance." },
        { "amp.master", "Master", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Amp", "Master volume: how hard the power amp is driven." },
        { "amp.bright", "Bright", "", 0.f, 1.f, 0.f, TOG, 1.f, "Amp", "Bright cap across the volume pot: more sparkle at low volume settings." },
        { "amp.pull", "Pull", "", 0.f, 1.f, 0.f, TOG, 1.f, "Amp", "Pull for saturation: about +10 dB more preamp gain for a thicker, more saturated sound." },
        { "amp.preampValve", "Preamp Valve", "", 0.f, 3.f, 0.f, CHO, 1.f, "Amp", "Input (V1) preamp valve. 12AX7 most gain; 5751 a little less; 12AY7 vintage, softer; 12AT7 most headroom. Resets to the model's usual valve when the model changes.", kPreampValveNames, 4 },
        { "amp.powerValve", "Power Valves", "", 0.f, 4.f, 0.f, CHO, 1.f, "Amp", "Power valves. 6L6 clean and tight; EL34 mid-forward, earlier breakup; 6V6 sweet, early breakup; EL84 chimey, compressed; KT88 huge headroom. Resets to the model's usual valves when the model changes.", kPowerValveNames, 5 },
        { "power.sag", "Sag", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Power", "Power-supply sag: how much the amp compresses and 'breathes' when played hard. 5 = the model's own amount." },
        { "power.variac", "Variac", "%", 60.f, 110.f, 100.f, LIN, 1.f, "Power", "Mains voltage, like a variac: lower voltage gives less headroom, earlier and softer breakup and a browner tone." },

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

        { "studiocomp.enabled", "Studio Comp", "", 0.f, 1.f, 0.f, TOG, 1.f, "StudioComp", "Compressor after the cabinet (stereo-linked), like a mix compressor: choose Pristine, LA-2A, 1176, SSL Bus, Fairchild 670 or dbx 160." },
        { "studiocomp.amount", "Amount", "", 0.f, 1.f, 0.4f, LIN, 10.f, "StudioComp", "Amount of compression: lowers the threshold (make-up gain follows)." },
        { "studiocomp.attack", "Attack", "", 0.f, 1.f, 0.5f, LIN, 10.f, "StudioComp", "1 ms (smooth, catches every transient) to 30 ms (punchy)." },
        { "studiocomp.tone", "Tone", "", 0.f, 1.f, 0.5f, LIN, 10.f, "StudioComp", "Gentle treble tilt after the compressor (centre = flat)." },
        { "studiocomp.level", "Level", "dB", -12.f, 12.f, 0.f, LIN, 1.f, "StudioComp", "Output level." },
        { "studiocomp.type", "Model", "", 0.f, 5.f, 0.f, CHO, 1.f, "StudioComp", "Compressor model: Pristine (clean, transparent VCA), LA-2A (opto), 1176 (FET), SSL Bus (VCA bus), Fairchild 670 (vari-mu valve), dbx 160 (RMS VCA).", kCompTypes, 6 },
        { "studiocomp.release", "Release", "", 0.f, 1.f, 0.5f, LIN, 10.f, "StudioComp", "Release time (Pristine 30 ms-0.75 s, 1176 50 ms-1.1 s, SSL 0.1/0.3/0.6/1.2 s). LA-2A, Fairchild and dbx set their own." },
        { "studiocomp.ratio", "Ratio", "", 0.f, 5.f, 2.f, CHO, 1.f, "StudioComp", "Compression ratio (Pristine and dbx 160).", kCompRatios, 6 },
        { "studiocomp.la2a", "Mode", "", 0.f, 1.f, 0.f, CHO, 1.f, "StudioComp", "LA-2A: Compress (gentle, about 3:1) or Limit (firm).", kLa2aModes, 2 },
        { "studiocomp.1176ratio", "Ratio", "", 0.f, 4.f, 0.f, CHO, 1.f, "StudioComp", "1176 ratio buttons; All = all buttons in (aggressive, distorted, pumping).", k1176Ratios, 5 },
        { "studiocomp.sslratio", "Ratio", "", 0.f, 2.f, 1.f, CHO, 1.f, "StudioComp", "SSL bus compressor ratio.", kSslRatios, 3 },
        { "studiocomp.sslauto", "Auto Release", "", 0.f, 1.f, 0.f, TOG, 1.f, "StudioComp", "SSL: program-dependent release (fast on transients, slow on sustained material)." },
        { "studiocomp.tc", "Time Constant", "", 0.f, 5.f, 1.f, CHO, 1.f, "StudioComp", "Fairchild time constant: 1-4 fixed attack/release (0.3 s to 5 s release), 5 and 6 program-dependent.", kFairchildTcs, 6 },
        { "studiocomp.overeasy", "OverEasy", "", 0.f, 1.f, 1.f, TOG, 1.f, "StudioComp", "dbx 160: OverEasy soft knee (off = hard knee)." },
        { "studiocomp.emphasis", "HF Emphasis", "", 0.f, 1.f, 0.f, LIN, 10.f, "StudioComp", "LA-2A: makes the gain cell react more to treble (the R37 trimmer), taming bright peaks." },
        { "studiocomp.mix", "Mix", "%", 0.f, 1.f, 1.f, LIN, 100.f, "StudioComp", "Parallel compression: blend of the dry and compressed signal." },
        { "studiocomp.schpf", "SC HPF", "Hz", 20.f, 300.f, 20.f, LOG, 1.f, "StudioComp", "Side-chain high-pass: stops low end from driving the compressor (20 Hz = off)." },

        { "smooth.enabled", "Smooth", "", 0.f, 1.f, 0.f, TOG, 1.f, "Smooth", "Dynamic 2 kHz tamer: compresses only the harsh upper-mid band (3:1) when it gets loud, after the cabinet." },
        { "smooth.amount", "Smooth", "", 0.f, 1.f, 0.4f, LIN, 10.f, "Smooth", "Threshold: turn clockwise to start smoothing at lower levels (more smoothing). Up to 9 dB of cut." },

        { "pitch.enabled", "Pitch", "", 0.f, 1.f, 0.f, TOG, 1.f, "Pitch", "Micro-pitch doubler: adds copies of the amp+cab sound shifted 13 cents up (left) and 13 cents down (right)." },
        { "pitch.mix", "Mix", "%", 0.f, 1.f, 0.5f, LIN, 100.f, "Pitch", "Level of the pitched copies added to the dry sound." },

        { "chorus.enabled", "Mod", "", 0.f, 1.f, 0.f, TOG, 1.f, "Chorus", "Modulation: chorus, flanger or phaser (choose with the Mode selector)." },
        { "chorus.mode", "Mode", "", 0.f, 3.f, 0.f, CHO, 1.f, "Chorus", "Chorus (lush doubling), Flanger (jet-like sweep), Script Phaser (smooth, subtle, no feedback) or Phase 90 (deeper, with feedback).", kModModes, 4 },
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
        { "delay.type", "Type", "", 0.f, 1.f, 0.f, CHO, 1.f, "Delay", "Studio: clean digital repeats. Tape: wow and flutter, saturation and repeats that darken as they fade.", kDelayTypes, 2 },

        { "reverb.enabled", "Reverb", "", 0.f, 1.f, 0.f, TOG, 1.f, "Reverb", "Stereo reverb. Tails ring out naturally when switched off." },
        { "reverb.mix", "Mix", "%", 0.f, 1.f, 0.25f, LIN, 100.f, "Reverb", "Reverb level." },
        { "reverb.decay", "Decay", "s", 0.3f, 10.f, 1.8f, LOG, 1.f, "Reverb", "Reverb time (RT60)." },
        { "reverb.tone", "Tone", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Reverb", "Dark to bright." },
        { "reverb.type", "Type", "", 0.f, 6.f, 1.f, CHO, 1.f, "Reverb", "Room: small and close. Hall: big and smooth. Spring: the classic amp tank, with its drip. Chamber: dense, bright and even. Plate: instant, dense and bright, the classic studio plate. Lexicon: lush, modulated studio reverb (ADV for its programs and controls). Bricasti: dense, natural, true-stereo studio reverb (ADV).", kReverbTypes, 7 },
        { "reverb.predelay", "Pre-Delay", "ms", 0.f, 250.f, 20.f, LIN, 1.f, "Reverb", "Lexicon / Bricasti: gap before the reverb starts." },
        { "reverb.size", "Size", "", 0.f, 1.f, 0.7f, LIN, 10.f, "Reverb", "Lexicon / Bricasti: size of the space (also spaces out the early reflections)." },
        { "reverb.diffusion", "Diffusion", "", 0.f, 1.f, 0.75f, LIN, 10.f, "Reverb", "Lexicon / Bricasti: low = grainy, distinct echoes; high = smooth, dense wash." },
        { "reverb.bassmult", "Bass Mult", "x", 0.5f, 2.5f, 1.2f, LIN, 1.f, "Reverb", "Lexicon / Bricasti: decay time below the crossover relative to Decay (1.0 = same)." },
        { "reverb.crossover", "Crossover", "Hz", 100.f, 2000.f, 500.f, LOG, 1.f, "Reverb", "Lexicon / Bricasti: frequency below which Bass Mult applies." },
        { "reverb.spin", "Spin", "Hz", 0.1f, 3.f, 0.8f, LOG, 1.f, "Reverb", "Lexicon / Bricasti: modulation rate of the tail." },
        { "reverb.wander", "Wander", "", 0.f, 1.f, 0.4f, LIN, 10.f, "Reverb", "Lexicon: depth of the tail's chorus-like modulation. Bricasti: very subtle movement." },
        { "reverb.early", "Early", "", 0.f, 1.f, 0.5f, LIN, 10.f, "Reverb", "Bricasti: level of the early reflections against the late reverb." },
        { "reverb.width", "Width", "%", 0.f, 1.5f, 1.f, LIN, 100.f, "Reverb", "Stereo width of the reverb (all types)." },
        { "reverb.lowcut", "Low Cut", "Hz", 20.f, 600.f, 20.f, LOG, 1.f, "Reverb", "High-pass on the reverb (all types): keeps the low end of the guitar clear (20 Hz = off)." },
        { "reverb.lexprog", "Program", "", 0.f, 3.f, 0.f, CHO, 1.f, "Reverb", "Lexicon program: Concert Hall, Random Hall (randomised modulation), Rich Plate, Chamber.", kLexPrograms, 4 },
        { "reverb.m7prog", "Program", "", 0.f, 3.f, 0.f, CHO, 1.f, "Reverb", "Bricasti program: Large Hall, Small Room, Plate, Chamber.", kM7Programs, 4 },

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
    float shown = v * d.displayScale;
    if (std::abs (shown) < 0.05f)
        shown = 0.0f; // never display "-0.0"
    if (std::strcmp (d.unit, "Hz") == 0 && v >= 1000.0f)
        std::snprintf (buf, sizeof (buf), "%.1f kHz", v / 1000.0f);
    else if (std::strcmp (d.unit, "ms") == 0 && v >= 1000.0f)
        std::snprintf (buf, sizeof (buf), "%.2f s", v / 1000.0f);
    else if (std::strcmp (d.unit, "x") == 0)
        std::snprintf (buf, sizeof (buf), "%.2fx", v);
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

// ZackAMP — central parameter model (framework independent).
//
// Every user-facing control maps to exactly one parameter here. IDs are stable strings
// that appear in presets, MIDI mappings and (later) plugin automation; never rename
// or reuse an ID. Values are stored in plain (real-world) units in lock-free atomics:
// any thread may write (UI, MIDI, preset loader); the audio thread reads once per
// control block and applies its own smoothing. Nothing here allocates after construction.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace af
{
enum class ParamScale : uint8_t
{
    linear,
    logarithmic, // frequency / time style parameters
    choice,      // integer index
    toggle       // 0 / 1
};

struct ParamDef
{
    const char* id;
    const char* name;
    const char* unit;       // "dB", "Hz", "ms", "s", "%", "bpm", "" ...
    float min, max, def;
    ParamScale scale;
    float displayScale;     // e.g. 10 for 0..1 knobs shown as 0..10
    const char* group;      // "Input", "Amp", "EQ", "Cabinet", "Gate", "Drive", ...
    const char* help;       // tooltip text
    const char* const* choices = nullptr;
    int numChoices = 0;
};

enum ParamIndex : int
{
    pInTrim,
    pBypass,
    pGateOn, pGateThreshold, pGateRelease,
    pDriveOn, pDriveDrive, pDriveTone, pDriveLevel,
    pAmpModel, pAmpGain, pAmpBass, pAmpMid, pAmpTreble, pAmpPresence, pAmpDepth, pAmpMaster, pAmpBright,
    pEqOn, pEqPosition, pEq80, pEq240, pEq750, pEq2200, pEq6600,
    pCabOn, pCabDual, pCabALevel, pCabAPan, pCabBLevel, pCabBPan,
    pPitchOn, pPitchMix,
    pChorusOn, pChorusMode, pChorusRate, pChorusDepth, pChorusMix,
    pDelayOn, pDelayTime, pDelaySync, pDelayDivision, pDelayFeedback, pDelayMix, pDelayTone,
    pReverbOn, pReverbMix, pReverbDecay, pReverbTone,
    pOutLevel,
    pTempo,
    kNumParams
};

const ParamDef& paramDef (int index) noexcept;
int findParamIndex (const std::string& id) noexcept; // -1 if unknown

/** Delay note divisions (in quarter notes) for tempo sync. */
float delayDivisionBeats (int choiceIndex) noexcept;

float plainToNormalised (int index, float plain) noexcept;
float normalisedToPlain (int index, float normalised) noexcept;
float sanitiseParamValue (int index, float plain) noexcept; // clamp, quantise choices/toggles
std::string formatParamValue (int index, float plain);

class ParameterStore
{
public:
    ParameterStore();

    float get (int index) const noexcept { return values[(size_t) index].load (std::memory_order_relaxed); }
    bool getBool (int index) const noexcept { return get (index) >= 0.5f; }
    int getInt (int index) const noexcept { return (int) (get (index) + 0.5f); }

    /** Any thread. Clamps/quantises. Returns true if the value changed. */
    bool set (int index, float plain) noexcept;
    bool setNormalised (int index, float norm) noexcept { return set (index, normalisedToPlain (index, norm)); }
    float getNormalised (int index) const noexcept { return plainToNormalised (index, get (index)); }

    void resetToDefaults() noexcept;

    /** Incremented on every change; lets UI / preset "modified" logic poll cheaply. */
    uint32_t getChangeCounter() const noexcept { return changes.load (std::memory_order_relaxed); }

    std::array<float, kNumParams> snapshot() const noexcept;

private:
    std::array<std::atomic<float>, kNumParams> values;
    std::atomic<uint32_t> changes { 0 };
};

} // namespace af

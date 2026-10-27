// ZackAMP — MIDI live control: CC -> parameter mapping, Learn mode, Program Change.
//
// Thread model: handleMessage() is called on the MIDI input thread. It only touches
// atomics: mapping slots, the ParameterStore (atomic values, smoothed later by the audio
// thread) and a pending-program slot that the message thread polls to perform the actual
// preset load (which needs file access). Mapping edits from the UI also go through atomics,
// so no locks are shared with the MIDI thread. Works with the UI closed.
#pragma once

#include "../Parameters/Parameters.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace af
{
struct CCMapping
{
    int cc = -1;
    int paramIndex = -1;
    float minNorm = 0.0f, maxNorm = 1.0f; // range limits (e.g. keep output level safe)
    bool toggle = false;                   // footswitch mode: each press (value >= 64) flips
};

class MidiController
{
public:
    explicit MidiController (ParameterStore& store);

    /** MIDI thread. Raw bytes of one complete message. Malformed / unsupported data is ignored. */
    void handleMessage (const uint8_t* data, int size) noexcept;

    // ---- configuration (message thread) ----
    void setChannel (int ch) noexcept { channel.store (std::clamp (ch, 0, 16)); } // 0 = omni
    int getChannel() const noexcept { return channel.load(); }
    void setProgramChangeEnabled (bool e) noexcept { pcEnabled.store (e); }
    bool isProgramChangeEnabled() const noexcept { return pcEnabled.load(); }

    void setMapping (const CCMapping& m) noexcept;
    void clearMapping (int cc) noexcept;
    void clearAll() noexcept;
    std::vector<CCMapping> getMappings() const;
    int findCCForParam (int paramIndex) const noexcept; // -1 if unmapped
    void loadDefaultMappings() noexcept;

    /** Learn: the next CC received is assigned to paramIndex. */
    void startLearn (int paramIndex) noexcept { learnParam.store (paramIndex); }
    void cancelLearn() noexcept { learnParam.store (-1); }
    int getLearnParam() const noexcept { return learnParam.load(); }

    /** Message thread: returns the most recent Program Change (0..127) once, or -1. */
    int takePendingProgram() noexcept { return pendingProgram.exchange (-1); }

    /** Increments on every accepted message (UI activity LED). */
    uint32_t getActivityCounter() const noexcept { return activity.load (std::memory_order_relaxed); }
    int getLastCC() const noexcept { return lastCC.load(); }

    /** Simple text serialisation: "cc,param-id,min,max,toggle;..." (stable param IDs). */
    std::string serialise() const;
    void deserialise (const std::string& s);

private:
    struct Slot
    {
        std::atomic<int> param { -1 };
        std::atomic<float> minNorm { 0.0f }, maxNorm { 1.0f };
        std::atomic<bool> toggle { false };
    };

    ParameterStore& params;
    std::array<Slot, 128> slots;
    std::atomic<int> channel { 0 }, learnParam { -1 }, pendingProgram { -1 }, lastCC { -1 };
    std::atomic<bool> pcEnabled { true };
    std::atomic<uint32_t> activity { 0 };
};

} // namespace af

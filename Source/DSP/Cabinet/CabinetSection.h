// AMP FORGE — cabinet section: one or two IR slots, crossfaded IR switching,
// level / pan per slot and a deliberate cabinet bypass.
#pragma once

#include "Convolver.h"
#include "../Utilities/DspCommon.h"

#include <array>
#include <atomic>
#include <vector>

namespace af::dsp
{
class CabinetSection
{
public:
    static constexpr int kNumSlots = 2;

    struct Settings
    {
        bool enabled = true;
        bool dual = false;
        std::array<float, kNumSlots> levelDb { 0.0f, 0.0f };
        std::array<float, kNumSlots> pan { 0.0f, 0.0f }; // -1..1
    };

    explicit CabinetSection (GarbageCollector& gc) : garbage (gc) {}
    ~CabinetSection();

    /** Non-RT, audio stopped. */
    void prepare (double sampleRate, int maxBlockSize);
    /** Non-RT, audio stopped: installs a convolver immediately (used when re-preparing). */
    void installImmediately (int slot, std::unique_ptr<Convolver> conv);
    /** Non-RT, any time: queues a convolver; the audio thread crossfades to it. nullptr = clear slot. */
    void publish (int slot, std::unique_ptr<Convolver> conv);
    void reset() noexcept;

    /** Audio thread. Mono in -> stereo out. */
    void process (const float* in, float* outL, float* outR, int n, const Settings& s) noexcept;

    /** Thread-safe status for the UI. */
    bool slotHasIR (int slot) const noexcept { return slotLoaded[(size_t) slot].load(); }

private:
    struct Slot
    {
        Convolver* active = nullptr;
        Convolver* fading = nullptr;
        int fadePos = 0;           // samples into the crossfade
        bool clearPending = false; // a "clear slot" request is being faded
        PendingSlot<Convolver> pending;
        std::atomic<bool> clearRequest { false };
    };

    void processSlot (Slot& s, const float* in, float* l, float* r, int n) noexcept;
    void runConvolver (Convolver* c, const float* in, float* l, float* r, int n) noexcept;

    GarbageCollector& garbage;
    std::array<Slot, kNumSlots> slots;
    std::array<std::atomic<bool>, kNumSlots> slotLoaded {};
    double fs = 48000.0;
    int fadeLength = 1440;

    LinearSmoother enableMix, dualMix;
    std::array<LinearSmoother, kNumSlots> gainL, gainR;
    std::vector<float> tmpL, tmpR, tmpL2, tmpR2, slotL, slotR;
};

} // namespace af::dsp

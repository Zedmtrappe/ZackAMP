#include "CabinetSection.h"

#include <cstring>

namespace af::dsp
{
CabinetSection::~CabinetSection()
{
    for (auto& s : slots)
    {
        delete s.active;
        delete s.fading;
    }
}

void CabinetSection::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    fadeLength = std::max (1, (int) (0.03 * fs)); // 30 ms raised-cosine crossfade
    for (auto* v : { &tmpL, &tmpR, &tmpL2, &tmpR2, &slotL, &slotR })
        v->assign ((size_t) std::max (1, maxBlockSize), 0.0f);

    enableMix.prepare (fs, 0.02);
    dualMix.prepare (fs, 0.02);
    for (int i = 0; i < kNumSlots; ++i)
    {
        gainL[(size_t) i].prepare (fs, 0.03);
        gainR[(size_t) i].prepare (fs, 0.03);
    }
    enableMix.snap (1.0f);
    dualMix.snap (0.0f);

    // Convolvers built for another sample rate are useless now; the engine re-installs.
    for (auto& s : slots)
    {
        delete s.fading;
        s.fading = nullptr;
        s.fadePos = 0;
        s.clearPending = false;
        if (s.active != nullptr && std::abs (s.active->getSampleRate() - fs) > 1e-6)
        {
            delete s.active;
            s.active = nullptr;
        }
    }
    for (int i = 0; i < kNumSlots; ++i)
        slotLoaded[(size_t) i] = slots[(size_t) i].active != nullptr;
    reset();
}

void CabinetSection::installImmediately (int slot, std::unique_ptr<Convolver> conv)
{
    auto& s = slots[(size_t) slot];
    delete s.active;
    s.active = conv.release();
    if (s.active != nullptr)
        s.active->reset();
    slotLoaded[(size_t) slot] = s.active != nullptr;
}

void CabinetSection::publish (int slot, std::unique_ptr<Convolver> conv)
{
    auto& s = slots[(size_t) slot];
    if (conv == nullptr)
    {
        s.pending.publish (nullptr);
        s.clearRequest = true;
        return;
    }
    s.clearRequest = false;
    s.pending.publish (std::move (conv));
}

void CabinetSection::reset() noexcept
{
    for (auto& s : slots)
    {
        if (s.active != nullptr)
            s.active->reset();
        if (s.fading != nullptr)
            s.fading->reset();
    }
}

void CabinetSection::runConvolver (Convolver* c, const float* in, float* l, float* r, int n) noexcept
{
    if (c == nullptr)
    {
        std::memset (l, 0, sizeof (float) * (size_t) n);
        std::memset (r, 0, sizeof (float) * (size_t) n);
        return;
    }
    c->process (in, l, r, n);
    if (c->getNumOutputs() == 1)
        std::memcpy (r, l, sizeof (float) * (size_t) n);
}

void CabinetSection::processSlot (Slot& s, const float* in, float* l, float* r, int n) noexcept
{
    // Accept a new convolver only when no crossfade is running; a newer publish simply
    // replaces the waiting one, so rapid browsing never piles up work on the audio thread.
    if (s.fading == nullptr)
    {
        if (Convolver* next = s.pending.take())
        {
            if (std::abs (next->getSampleRate() - fs) > 1e-6)
            {
                garbage.retire (next); // built for a stale sample rate
            }
            else
            {
                next->reset();
                s.fading = s.active;
                s.active = next;
                s.fadePos = 0;
            }
        }
        else if (s.clearRequest.exchange (false) && s.active != nullptr)
        {
            s.fading = s.active;
            s.active = nullptr;
            s.fadePos = 0;
        }
    }

    runConvolver (s.active, in, l, r, n);

    if (s.fading != nullptr)
    {
        runConvolver (s.fading, in, tmpL2.data(), tmpR2.data(), n);
        for (int i = 0; i < n; ++i)
        {
            const float t = std::min (1.0f, (float) (s.fadePos + i) / (float) fadeLength);
            // Equal-gain raised cosine: outputs of two cabinets fed by the same guitar are
            // strongly correlated, and the flat start hides the new convolver's cold history.
            const float gIn = 0.5f - 0.5f * std::cos ((float) kPi * t);
            const float gOut = 1.0f - gIn;
            l[i] = l[i] * gIn + tmpL2[(size_t) i] * gOut;
            r[i] = r[i] * gIn + tmpR2[(size_t) i] * gOut;
        }
        s.fadePos += n;
        if (s.fadePos >= fadeLength)
        {
            garbage.retire (s.fading);
            s.fading = nullptr;
        }
    }
}

void CabinetSection::process (const float* in, float* outL, float* outR, int n, const Settings& set) noexcept
{
    enableMix.setTarget (set.enabled ? 1.0f : 0.0f);
    dualMix.setTarget (set.dual ? 1.0f : 0.0f);
    for (int i = 0; i < kNumSlots; ++i)
    {
        const float g = dbToGain (set.levelDb[(size_t) i]);
        const float p = clampv (set.pan[(size_t) i], -1.0f, 1.0f);
        const float ang = (p + 1.0f) * 0.25f * (float) kPi; // equal-power pan
        gainL[(size_t) i].setTarget (g * std::cos (ang) * (float) std::sqrt (2.0));
        gainR[(size_t) i].setTarget (g * std::sin (ang) * (float) std::sqrt (2.0));
    }

    // Slot A (with no IR loaded at all the signal passes through un-cabbed)
    processSlot (slots[0], in, tmpL.data(), tmpR.data(), n);
    if (slots[0].active == nullptr && slots[0].fading == nullptr)
    {
        std::memcpy (tmpL.data(), in, sizeof (float) * (size_t) n);
        std::memcpy (tmpR.data(), in, sizeof (float) * (size_t) n);
    }

    // Slot B only runs while dual mode is (or is fading) active.
    const bool runB = set.dual || dualMix.isSmoothing() || dualMix.getCurrent() > 0.0f;
    if (runB)
        processSlot (slots[1], in, slotL.data(), slotR.data(), n);

    const float levelA = dbToGain (set.levelDb[0]);
    for (int i = 0; i < n; ++i)
    {
        const float d = dualMix.next();
        const float aL = gainL[0].next(), aR = gainR[0].next();
        const float bL = gainL[1].next(), bR = gainR[1].next();
        // Single mode: slot A centred at its level. Dual mode: level and pan for both slots.
        float l = tmpL[(size_t) i] * ((1.0f - d) * levelA + d * aL);
        float r = tmpR[(size_t) i] * ((1.0f - d) * levelA + d * aR);
        if (runB)
        {
            l += slotL[(size_t) i] * d * bL;
            r += slotR[(size_t) i] * d * bR;
        }
        const float e = enableMix.next();
        outL[i] = e * l + (1.0f - e) * in[i];
        outR[i] = e * r + (1.0f - e) * in[i];
    }

    for (int i = 0; i < kNumSlots; ++i)
        slotLoaded[(size_t) i].store (slots[(size_t) i].active != nullptr, std::memory_order_relaxed);
}

} // namespace af::dsp

#include "PresetApplier.h"

namespace af
{
juce::String defaultIrForModel (int ampModel) { return dsp::getAmpVoicing (ampModel).defaultIrId; }

PresetState captureState (const AmpForgeEngine& engine)
{
    PresetState s;
    s.values = engine.params().snapshot();
    for (int slot = 0; slot < 2; ++slot)
    {
        const auto info = engine.getCabinetInfo (slot);
        (slot == 0 ? s.irA : s.irB) = juce::String (info.irId);
        (slot == 0 ? s.irAName : s.irBName) = juce::String (info.displayName);
    }
    return s;
}

void applyState (AmpForgeEngine& engine, const IRLibrary& library, const PresetState& state, juce::StringArray& warnings)
{
    // 1. Resolve and decode IRs before touching the audio.
    std::shared_ptr<const dsp::IRData> irs[2];
    const juce::String ids[2] = { state.irA, state.irB };
    const juce::String names[2] = { state.irAName, state.irBName };
    const int model = (int) state.values[(size_t) pAmpModel];
    for (int slot = 0; slot < 2; ++slot)
    {
        if (ids[slot].isEmpty())
        {
            if (slot == 0)
            {
                juce::String err;
                irs[0] = library.load (defaultIrForModel (model), err);
            }
            continue;
        }
        juce::String err;
        irs[slot] = library.load (ids[slot], err);
        if (irs[slot] == nullptr)
        {
            const auto shown = names[slot].isNotEmpty() ? names[slot] : ids[slot];
            juce::String fallbackErr;
            irs[slot] = library.load (defaultIrForModel (model), fallbackErr);
            warnings.add ("Cabinet " + juce::String (slot == 0 ? "A" : "B") + ": IR \"" + shown + "\" is unavailable ("
                          + err + "). Using the amp's default cabinet instead.");
        }
    }

    // 2. Click-free swap.
    engine.beginTransition();
    auto& p = engine.params();
    for (int i = 0; i < kNumParams; ++i)
        p.set (i, state.values[(size_t) i]);
    for (int slot = 0; slot < 2; ++slot)
    {
        if (irs[slot] != nullptr)
        {
            std::string err;
            if (! engine.setCabinetIR (slot, irs[slot], &err))
                warnings.add ("Cabinet " + juce::String (slot == 0 ? "A" : "B") + ": " + juce::String (err));
        }
        else if (slot == 1)
        {
            engine.clearCabinetIR (1);
        }
    }
    engine.endTransition();
}
} // namespace af

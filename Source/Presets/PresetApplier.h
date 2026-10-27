// ZackAMP — moving complete sound states between presets and the engine (non-RT only).
#pragma once

#include "PresetManager.h"
#include "../Audio/AmpForgeEngine.h"
#include "../IR/IRLibrary.h"

namespace af
{
/** Captures the engine's current parameters and cabinet IR references. */
PresetState captureState (const AmpForgeEngine& engine);

/** Applies a preset: decodes IRs first (file I/O, off the audio thread), then soft-mutes the
    engine, swaps all parameters and IRs, and fades back in. Missing IRs fall back to the amp
    model's default cabinet with a warning. Call from a background/worker thread or the
    message thread (it blocks for at most ~60 ms while the audio fades out). */
void applyState (AmpForgeEngine& engine, const IRLibrary& library, const PresetState& state, juce::StringArray& warnings);

/** The default cabinet IR id for an amp model. */
juce::String defaultIrForModel (int ampModel);
} // namespace af

// ZackAMP — file-system helpers: standard data locations and atomic writes.
#pragma once

#include <juce_core/juce_core.h>

namespace af
{
/** ~/Library/Application Support/ZackAMP on macOS (~/.config/ZackAMP on Linux).
    Mutable user content never goes into the application bundle. */
juce::File getAppDataDirectory();
juce::File getUserPresetDirectory();
juce::File getUserIRDirectory();

/** Writes via a temporary sibling file and an atomic rename, so a crash or full disk can
    never leave a half-written preset / index behind. */
bool writeFileAtomically (const juce::File& target, const juce::String& text, juce::String* error = nullptr);

/** Allows test code to redirect all user data to a scratch folder. */
void setAppDataDirectoryOverride (const juce::File& dir);
} // namespace af

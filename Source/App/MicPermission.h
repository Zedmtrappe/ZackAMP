// ZackAMP — macOS audio-input ("Microphone") permission.
//
// macOS calls every audio input — including an audio interface — "Microphone". If the user
// declines that prompt, Core Audio silently delivers zeros, which looks like a broken input.
// JUCE does not report this on macOS, so we ask the system directly.
#pragma once

#include <functional>

namespace af
{
enum class MicPermission { granted, denied, notDetermined };

/** Current status (always `granted` on platforms without such a permission). */
MicPermission getMicPermission();

/** Opens System Settings at Privacy & Security → Microphone (macOS only). */
void openMicPrivacySettings();
} // namespace af

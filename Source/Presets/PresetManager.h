// ZackAMP — presets: versioned JSON schema, factory + user presets, safe file handling.
//
// A preset stores every parameter by its stable ID plus references (IDs, not audio) to the
// cabinet IRs. Unknown parameters are ignored, missing ones take their defaults, and the
// schema version drives migrations (see docs/PRESET_FORMAT.md). User presets are written
// atomically and are never silently overwritten.
#pragma once

#include "../Parameters/Parameters.h"

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace af
{
struct PresetState
{
    juce::String name { "Untitled" };
    juce::String category { "User" };
    juce::String description;
    juce::String author;
    std::array<float, kNumParams> values {};
    juce::String irA, irB;          // IR library IDs
    juce::String irAName, irBName;  // display names, used in "missing IR" messages

    PresetState();
    bool sameSoundAs (const PresetState& other) const; // ignores name/category/description
};

class PresetCodec
{
public:
    static constexpr int kSchemaVersion = 4;
    static constexpr const char* kFormatTag = "ampforge.preset";

    static juce::String toJson (const PresetState& s);

    /** Returns false (with a user-readable error) for unreadable presets; warnings report
        recoverable problems such as unknown parameters or a newer schema. */
    static bool fromJson (const juce::String& json, PresetState& out, juce::String& error, juce::StringArray& warnings);
};

struct PresetInfo
{
    PresetState state;
    bool isFactory = false;
    juce::File file; // user presets only
};

class PresetManager
{
public:
    explicit PresetManager (juce::File userPresetDirectory);

    void loadFactory();
    /** Re-reads the user folder. Corrupt files are skipped and reported. */
    juce::StringArray rescanUser();

    const std::vector<PresetInfo>& getPresets() const noexcept { return presets; }
    int findIndex (const juce::String& name, bool factory) const;

    /** Saves a user preset. If a user preset of that name exists and allowOverwrite is false,
        fails with an explanatory error. Factory names are reserved. */
    bool saveUser (const PresetState& state, bool allowOverwrite, juce::String& error, int* newIndex = nullptr);
    bool renameUser (int index, const juce::String& newName, juce::String& error);
    bool deleteUser (int index, juce::String& error);
    bool duplicate (int index, juce::String& error, int* newIndex = nullptr);
    juce::String makeUniqueName (const juce::String& base) const;

    /** Indices matching text (name/description) and category ("" = all), family (-1 = all). */
    std::vector<int> filter (const juce::String& text, const juce::String& category, int family) const;

    static juce::StringArray getCategories();
    static int familyOfModel (int ampModel); // 0 American, 1 British, 2 Modern

    juce::File getUserDirectory() const { return userDir; }

private:
    juce::File fileForName (const juce::String& name) const;
    void sortPresets();

    juce::File userDir;
    std::vector<PresetInfo> presets; // factory first (in curated order), then user (alphabetical)
};

} // namespace af

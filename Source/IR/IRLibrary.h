// AMP FORGE — impulse-response library: factory IRs (embedded in the app binary) and a
// user library of imported WAV/AIFF files.
//
// Import either *references* the original file (it must stay where it is) or *copies* it
// into the application-managed library folder (recommended: presets keep working even if
// the original is moved). Removing a library entry never deletes the user's original file;
// only a copy that AMP FORGE itself made is removed. All file access happens on non-real-time
// threads; decoded IRs are handed to the engine, which prepares them off the audio thread.
#pragma once

#include "../DSP/Cabinet/Convolver.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace af
{
struct IREntry
{
    juce::String id;               // "factory:..." or "user:<uuid>"
    juce::String name;
    juce::String description;
    juce::String originalFilename;
    juce::String filePath;         // absolute path for user entries
    bool isFactory = false;
    bool isCopy = false;           // file lives in the managed library folder
    bool missing = false;          // referenced file no longer exists
    int numChannels = 0;
    double sampleRate = 0.0;
    double durationSeconds = 0.0;
};

struct IRImportLimits
{
    juce::int64 maxFileBytes = 64 * 1024 * 1024; // 64 MB
    double maxSeconds = 10.0;                    // longer files are rejected outright
    int maxChannels = 2;
    double minRate = 8000.0, maxRate = 384000.0;
};

class IRLibrary
{
public:
    explicit IRLibrary (juce::File userLibraryDirectory);

    /** Registers the IRs embedded in the binary (manifest.json + WAVs). */
    void loadFactory();

    /** Reads the user index (library.json). Missing index = empty library. */
    bool loadUserIndex (juce::String* error = nullptr);
    bool saveUserIndex (juce::String* error = nullptr) const;

    std::vector<IREntry> getEntries() const;
    std::optional<IREntry> findEntry (const juce::String& id) const;

    struct ImportResult
    {
        bool ok = false;
        juce::String error;
        IREntry entry;
        juce::StringArray warnings;
    };

    /** Validates and adds a file. copyIntoLibrary = copy to the managed folder. */
    ImportResult importFile (const juce::File& file, bool copyIntoLibrary);

    /** Removes a user entry. Deletes the file only if it is AMP FORGE's own managed copy. */
    bool removeEntry (const juce::String& id, juce::String* error = nullptr);
    bool renameEntry (const juce::String& id, const juce::String& newName);

    /** Loads (decodes) an IR. Any non-real-time thread. Factory IRs are cached. */
    std::shared_ptr<const dsp::IRData> load (const juce::String& id, juce::String& error) const;

    /** Re-checks whether referenced user files still exist. */
    void refreshMissingFlags();

    juce::File getUserDirectory() const { return userDir; }
    juce::File getManagedFilesDirectory() const { return userDir.getChildFile ("Files"); }

    /** Decodes and validates audio data as an IR. */
    static std::shared_ptr<dsp::IRData> decode (std::unique_ptr<juce::InputStream> stream,
                                                const IRImportLimits& limits, juce::String& error);

    IRImportLimits limits;

private:
    juce::File userDir;
    mutable std::mutex lock;
    std::vector<IREntry> entries;
    mutable std::map<juce::String, std::shared_ptr<const dsp::IRData>> factoryCache;
    std::map<juce::String, juce::String> factoryResource; // id -> resource file name
};

} // namespace af

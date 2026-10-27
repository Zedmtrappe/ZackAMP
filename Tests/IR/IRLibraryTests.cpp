#include <doctest.h>
#include "../../Source/IR/IRLibrary.h"
#include "../../Source/Audio/AmpForgeEngine.h"

using namespace af;

namespace
{
struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("ampforge-irtest-" + juce::Uuid().toString());
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};

juce::File writeWav (const juce::File& f, int channels, double rate, int length, int bits, bool silent = false)
{
    juce::WavAudioFormat wav;
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = f.createOutputStream();
    REQUIRE (os != nullptr);
    juce::AudioBuffer<float> b (channels, length);
    b.clear();
    if (! silent)
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < length; ++i)
                b.setSample (c, i, 0.5f * std::exp (-i / 200.0f) * (i % 2 ? 1.f : -1.f));
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions {}.withSampleRate (rate).withNumChannels (channels).withBitsPerSample (bits));
    REQUIRE (w != nullptr);
    w->writeFromAudioSampleBuffer (b, 0, length);
    return f;
}
} // namespace

TEST_CASE ("Factory IRs load and convolve")
{
    TempDir t;
    IRLibrary lib (t.dir);
    lib.loadFactory();
    const auto entries = lib.getEntries();
    CHECK (entries.size() >= 12);
    AmpForgeEngine e;
    e.prepare (44100, 128); // forces resampling from 48 kHz
    for (const auto& en : entries)
    {
        juce::String err;
        auto ir = lib.load (en.id, err);
        INFO (en.id.toStdString());
        REQUIRE (ir != nullptr);
        CHECK (ir->sampleRate == 48000.0);
        CHECK (e.setCabinetIR (0, ir));
    }
}

TEST_CASE ("Import supports common WAV formats and copies into the managed library")
{
    TempDir t;
    IRLibrary lib (t.dir.getChildFile ("lib"));
    for (int bits : { 16, 24, 32 })
        for (int ch : { 1, 2 })
            for (double rate : { 44100.0, 48000.0, 96000.0 })
            {
                const auto f = writeWav (t.dir.getChildFile ("ir_" + juce::String (bits) + "_" + juce::String (ch) + "_" + juce::String ((int) rate) + ".wav"), ch, rate, 2000, bits);
                auto r = lib.importFile (f, true);
                INFO (f.getFileName().toStdString() << " " << r.error.toStdString());
                REQUIRE (r.ok);
                CHECK (r.entry.isCopy);
                CHECK (r.entry.numChannels == ch);
                CHECK (r.entry.sampleRate == rate);
                CHECK (juce::File (r.entry.filePath).isAChildOf (lib.getManagedFilesDirectory()));
                juce::String err;
                CHECK (lib.load (r.entry.id, err) != nullptr);
            }
    // Copy never overwrites: importing the same name twice creates a second file.
    const auto f = writeWav (t.dir.getChildFile ("same.wav"), 1, 48000, 500, 24);
    auto a = lib.importFile (f, true);
    auto b = lib.importFile (f, true);
    CHECK (a.entry.filePath != b.entry.filePath);
}

TEST_CASE ("Invalid IR files are rejected with useful messages")
{
    TempDir t;
    IRLibrary lib (t.dir.getChildFile ("lib"));
    auto expectError = [&] (const juce::File& f, const char* fragment)
    {
        auto r = lib.importFile (f, false);
        INFO (f.getFileName().toStdString() << " -> " << r.error.toStdString());
        CHECK_FALSE (r.ok);
        CHECK (r.error.containsIgnoreCase (fragment));
    };
    auto garbage = t.dir.getChildFile ("garbage.wav");
    garbage.replaceWithText ("RIFF this is not really a wav file at all");
    expectError (garbage, "unsupported");
    auto empty = t.dir.getChildFile ("empty.wav");
    empty.create();
    expectError (empty, "unsupported");
    expectError (writeWav (t.dir.getChildFile ("silent.wav"), 1, 48000, 1000, 24, true), "silent");
    expectError (writeWav (t.dir.getChildFile ("quad.wav"), 4, 48000, 1000, 24), "channels");
    lib.limits.maxSeconds = 1.0;
    expectError (writeWav (t.dir.getChildFile ("long.wav"), 1, 48000, 96000, 16), "long");
    lib.limits.maxFileBytes = 100;
    expectError (writeWav (t.dir.getChildFile ("big.wav"), 1, 48000, 1000, 24), "too large");
    expectError (t.dir.getChildFile ("nonexistent.wav"), "not found");
}

TEST_CASE ("Referenced IRs: index persistence, missing files, removal keeps originals")
{
    TempDir t;
    const auto libDir = t.dir.getChildFile ("lib");
    const auto original = writeWav (t.dir.getChildFile ("ref.wav"), 1, 48000, 1000, 24);
    juce::String id, copyPath;
    {
        IRLibrary lib (libDir);
        auto r = lib.importFile (original, false);
        REQUIRE (r.ok);
        CHECK_FALSE (r.entry.isCopy);
        id = r.entry.id;
        auto c = lib.importFile (original, true);
        copyPath = c.entry.filePath;
        // Second reference import of the same file is recognised.
        auto again = lib.importFile (original, false);
        CHECK (again.warnings.size() == 1);
    }
    {
        IRLibrary lib (libDir);
        REQUIRE (lib.loadUserIndex());
        REQUIRE (lib.findEntry (id).has_value());
        CHECK_FALSE (lib.findEntry (id)->missing);

        // Removing a referenced entry never deletes the user's file.
        juce::String err;
        REQUIRE (lib.removeEntry (id, &err));
        CHECK (original.existsAsFile());
    }
    {
        IRLibrary lib (libDir);
        lib.loadUserIndex();
        // Delete the managed copy behind the library's back: entry is flagged missing, load fails gracefully.
        juce::File (copyPath).deleteFile();
        lib.refreshMissingFlags();
        const auto entries = lib.getEntries();
        REQUIRE (entries.size() == 1);
        CHECK (entries[0].missing);
        juce::String err;
        CHECK (lib.load (entries[0].id, err) == nullptr);
        CHECK (err.contains ("moved or deleted"));
    }
}

TEST_CASE ("Damaged library index is preserved and reported")
{
    TempDir t;
    t.dir.getChildFile ("library.json").replaceWithText ("{ broken");
    IRLibrary lib (t.dir);
    juce::String err;
    CHECK_FALSE (lib.loadUserIndex (&err));
    CHECK (err.isNotEmpty());
    CHECK (t.dir.getChildFile ("library.damaged.json").existsAsFile());
}

#include "IRLibrary.h"
#include "../Platform/FileUtils.h"

#include <AmpForgeData.h>

namespace af
{
namespace
{
    const void* findResource (const juce::String& originalName, int& size)
    {
        for (int i = 0; i < AmpForgeData::namedResourceListSize; ++i)
            if (originalName == AmpForgeData::originalFilenames[i])
                return AmpForgeData::getNamedResource (AmpForgeData::namedResourceList[i], size);
        size = 0;
        return nullptr;
    }

    juce::var entryToVar (const IREntry& e)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", e.id);
        o->setProperty ("name", e.name);
        o->setProperty ("originalFilename", e.originalFilename);
        o->setProperty ("path", e.filePath);
        o->setProperty ("managedCopy", e.isCopy);
        o->setProperty ("channels", e.numChannels);
        o->setProperty ("sampleRate", e.sampleRate);
        o->setProperty ("duration", e.durationSeconds);
        return juce::var (o);
    }
} // namespace

IRLibrary::IRLibrary (juce::File userLibraryDirectory) : userDir (std::move (userLibraryDirectory)) {}

void IRLibrary::loadFactory()
{
    int size = 0;
    const void* data = findResource ("manifest.json", size);
    if (data == nullptr)
        return;
    const auto manifest = juce::JSON::parse (juce::String::fromUTF8 (static_cast<const char*> (data), size));
    if (! manifest.isArray())
        return;

    std::lock_guard<std::mutex> l (lock);
    for (const auto& item : *manifest.getArray())
    {
        IREntry e;
        e.id = item["id"].toString();
        e.name = item["name"].toString();
        e.description = item["description"].toString();
        e.originalFilename = item["file"].toString();
        e.isFactory = true;
        if (e.id.isEmpty() || findResource (e.originalFilename, size) == nullptr)
            continue;
        // Factory IRs are 48 kHz mono, written by Tools/IRGenerator.cpp.
        e.numChannels = 1;
        e.sampleRate = 48000.0;
        e.durationSeconds = 0.0;
        factoryResource[e.id] = e.originalFilename;
        entries.push_back (e);
    }
}

bool IRLibrary::loadUserIndex (juce::String* error)
{
    const auto file = userDir.getChildFile ("library.json");
    if (! file.existsAsFile())
        return true;
    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (! parsed.isObject() || ! parsed["entries"].isArray())
    {
        if (error != nullptr)
            *error = "The IR library index is damaged and was ignored. Your IR files are untouched; re-import them to rebuild the library.";
        // Keep the damaged file for inspection rather than overwriting it silently.
        file.copyFileTo (file.getSiblingFile ("library.damaged.json"));
        return false;
    }

    std::lock_guard<std::mutex> l (lock);
    entries.erase (std::remove_if (entries.begin(), entries.end(), [] (const IREntry& e) { return ! e.isFactory; }), entries.end());
    for (const auto& v : *parsed["entries"].getArray())
    {
        IREntry e;
        e.id = v["id"].toString();
        e.name = v["name"].toString();
        e.originalFilename = v["originalFilename"].toString();
        e.filePath = v["path"].toString();
        e.isCopy = (bool) v["managedCopy"];
        e.numChannels = (int) v["channels"];
        e.sampleRate = (double) v["sampleRate"];
        e.durationSeconds = (double) v["duration"];
        if (! e.id.startsWith ("user:") || e.filePath.isEmpty())
            continue;
        e.missing = ! juce::File (e.filePath).existsAsFile();
        entries.push_back (e);
    }
    return true;
}

bool IRLibrary::saveUserIndex (juce::String* error) const
{
    juce::Array<juce::var> list;
    {
        std::lock_guard<std::mutex> l (lock);
        for (const auto& e : entries)
            if (! e.isFactory)
                list.add (entryToVar (e));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "ampforge.irlibrary");
    root->setProperty ("version", 1);
    root->setProperty ("entries", list);
    return writeFileAtomically (userDir.getChildFile ("library.json"), juce::JSON::toString (juce::var (root)), error);
}

std::vector<IREntry> IRLibrary::getEntries() const
{
    std::lock_guard<std::mutex> l (lock);
    return entries;
}

std::optional<IREntry> IRLibrary::findEntry (const juce::String& id) const
{
    std::lock_guard<std::mutex> l (lock);
    for (const auto& e : entries)
        if (e.id == id)
            return e;
    return std::nullopt;
}

std::shared_ptr<dsp::IRData> IRLibrary::decode (std::unique_ptr<juce::InputStream> stream,
                                                const IRImportLimits& lim, juce::String& error)
{
    if (stream == nullptr)
    {
        error = "The file could not be opened.";
        return nullptr;
    }
    const auto total = stream->getTotalLength();
    if (total > lim.maxFileBytes)
    {
        error = "The file is too large (" + juce::File::descriptionOfSizeInBytes (total) + "). Cabinet IRs are normally well under 1 MB.";
        return nullptr;
    }

    juce::AudioFormatManager fm;
    fm.registerFormat (new juce::WavAudioFormat(), true);
    fm.registerFormat (new juce::AiffAudioFormat(), false);
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (std::move (stream)));
    if (reader == nullptr)
    {
        error = "Unsupported or damaged audio file. ZackAMP reads WAV (PCM 16/24/32-bit or 32-bit float) and AIFF.";
        return nullptr;
    }
    if (reader->numChannels < 1 || (int) reader->numChannels > lim.maxChannels)
    {
        error = "This file has " + juce::String ((int) reader->numChannels) + " channels. Only mono and stereo IRs are supported.";
        return nullptr;
    }
    if (reader->sampleRate < lim.minRate || reader->sampleRate > lim.maxRate)
    {
        error = "Unsupported sample rate (" + juce::String (reader->sampleRate, 0) + " Hz).";
        return nullptr;
    }
    if (reader->lengthInSamples <= 0)
    {
        error = "The file contains no audio.";
        return nullptr;
    }
    const double seconds = (double) reader->lengthInSamples / reader->sampleRate;
    if (seconds > lim.maxSeconds)
    {
        error = "The file is " + juce::String (seconds, 1) + " s long; the limit is " + juce::String (lim.maxSeconds, 0)
                + " s. This looks like a recording or reverb, not a cabinet IR.";
        return nullptr;
    }

    const int len = (int) reader->lengthInSamples;
    const int chans = (int) reader->numChannels;
    juce::AudioBuffer<float> buf;
    try
    {
        buf.setSize (chans, len);
    }
    catch (const std::bad_alloc&)
    {
        error = "Not enough memory to load this file.";
        return nullptr;
    }
    if (! reader->read (&buf, 0, len, 0, true, chans > 1))
    {
        error = "The audio data could not be read (file truncated or damaged).";
        return nullptr;
    }

    auto ir = std::make_shared<dsp::IRData>();
    ir->sampleRate = reader->sampleRate;
    float peak = 0.0f;
    for (int c = 0; c < chans; ++c)
    {
        const float* p = buf.getReadPointer (c);
        for (int i = 0; i < len; ++i)
        {
            if (! std::isfinite (p[i]))
            {
                error = "The file contains invalid sample values (NaN or infinity).";
                return nullptr;
            }
            peak = std::max (peak, std::abs (p[i]));
        }
        ir->channels.emplace_back (p, p + len);
    }
    if (peak < 1.0e-6f)
    {
        error = "The file is silent.";
        return nullptr;
    }
    return ir;
}

IRLibrary::ImportResult IRLibrary::importFile (const juce::File& file, bool copyIntoLibrary)
{
    ImportResult r;
    if (! file.existsAsFile())
    {
        r.error = "File not found.";
        return r;
    }
    juce::String err;
    auto ir = decode (file.createInputStream(), limits, err);
    if (ir == nullptr)
    {
        r.error = err;
        return r;
    }

    if (! copyIntoLibrary)
    {
        // Referencing the same file twice would just duplicate the entry.
        std::lock_guard<std::mutex> l (lock);
        for (const auto& e : entries)
            if (! e.isFactory && ! e.isCopy && e.filePath == file.getFullPathName())
            {
                r.ok = true;
                r.entry = e;
                r.warnings.add ("This file is already in your library.");
                return r;
            }
    }

    IREntry e;
    e.id = "user:" + juce::Uuid().toDashedString();
    e.name = file.getFileNameWithoutExtension();
    e.originalFilename = file.getFileName();
    e.numChannels = ir->getNumChannels();
    e.sampleRate = ir->sampleRate;
    e.durationSeconds = ir->getLength() / ir->sampleRate;
    e.filePath = file.getFullPathName();

    if (copyIntoLibrary)
    {
        const auto dir = getManagedFilesDirectory();
        if (! dir.createDirectory())
        {
            r.error = "Could not create the IR library folder.";
            return r;
        }
        // Never overwrite: pick a free name.
        const auto dest = dir.getChildFile (file.getFileName()).getNonexistentSibling (true);
        if (! file.copyFileTo (dest))
        {
            r.error = "Could not copy the file into the IR library (disk full or no permission?).";
            return r;
        }
        e.filePath = dest.getFullPathName();
        e.isCopy = true;
    }

    if (e.durationSeconds > 1.0)
        r.warnings.add ("This IR is " + juce::String (e.durationSeconds, 2) + " s long; only the first 1 s is used for convolution.");

    {
        std::lock_guard<std::mutex> l (lock);
        entries.push_back (e);
    }
    juce::String saveErr;
    if (! saveUserIndex (&saveErr))
        r.warnings.add ("The IR was added but the library index could not be saved: " + saveErr);
    r.ok = true;
    r.entry = e;
    return r;
}

bool IRLibrary::removeEntry (const juce::String& id, juce::String* error)
{
    IREntry removed;
    {
        std::lock_guard<std::mutex> l (lock);
        auto it = std::find_if (entries.begin(), entries.end(), [&] (const IREntry& e) { return e.id == id; });
        if (it == entries.end() || it->isFactory)
        {
            if (error != nullptr)
                *error = "Factory IRs cannot be removed.";
            return false;
        }
        removed = *it;
        entries.erase (it);
    }
    // Only delete our own managed copy, and only if it really is inside the managed folder.
    if (removed.isCopy)
    {
        const juce::File f (removed.filePath);
        if (f.isAChildOf (getManagedFilesDirectory()))
            f.deleteFile();
    }
    return saveUserIndex (error);
}

bool IRLibrary::renameEntry (const juce::String& id, const juce::String& newName)
{
    {
        std::lock_guard<std::mutex> l (lock);
        auto it = std::find_if (entries.begin(), entries.end(), [&] (const IREntry& e) { return e.id == id; });
        if (it == entries.end() || it->isFactory || newName.trim().isEmpty())
            return false;
        it->name = newName.trim();
    }
    return saveUserIndex();
}

void IRLibrary::refreshMissingFlags()
{
    std::lock_guard<std::mutex> l (lock);
    for (auto& e : entries)
        if (! e.isFactory)
            e.missing = ! juce::File (e.filePath).existsAsFile();
}

std::shared_ptr<const dsp::IRData> IRLibrary::load (const juce::String& id, juce::String& error) const
{
    auto entry = findEntry (id);
    if (! entry)
    {
        error = "IR not found in the library.";
        return nullptr;
    }

    std::shared_ptr<dsp::IRData> ir;
    if (entry->isFactory)
    {
        {
            std::lock_guard<std::mutex> l (lock);
            if (auto it = factoryCache.find (id); it != factoryCache.end())
                return it->second;
        }
        juce::String resourceName;
        {
            std::lock_guard<std::mutex> l (lock);
            resourceName = factoryResource.at (id);
        }
        int size = 0;
        const void* data = findResource (resourceName, size);
        if (data == nullptr)
        {
            error = "Factory IR resource missing.";
            return nullptr;
        }
        ir = decode (std::make_unique<juce::MemoryInputStream> (data, (size_t) size, false), limits, error);
    }
    else
    {
        const juce::File f (entry->filePath);
        if (! f.existsAsFile())
        {
            error = "The IR file \"" + entry->originalFilename + "\" has been moved or deleted.";
            return nullptr;
        }
        ir = decode (f.createInputStream(), limits, error);
    }

    if (ir == nullptr)
        return nullptr;
    ir->id = id.toStdString();
    ir->displayName = entry->name.toStdString();
    if (entry->isFactory)
    {
        std::lock_guard<std::mutex> l (lock);
        factoryCache[id] = ir;
    }
    return ir;
}

} // namespace af

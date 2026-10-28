#include "PresetManager.h"
#include "../Platform/FileUtils.h"
#include "../DSP/Amplifiers/AmpModel.h"

#include <AmpForgeData.h>

namespace af
{
PresetState::PresetState()
{
    for (int i = 0; i < kNumParams; ++i)
        values[(size_t) i] = paramDef (i).def;
}

bool PresetState::sameSoundAs (const PresetState& o) const
{
    for (int i = 0; i < kNumParams; ++i)
        if (std::abs (values[(size_t) i] - o.values[(size_t) i]) > 1.0e-5f)
            return false;
    return irA == o.irA && irB == o.irB;
}

//==============================================================================
juce::String PresetCodec::toJson (const PresetState& s)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", kFormatTag);
    root->setProperty ("schemaVersion", kSchemaVersion);
    root->setProperty ("name", s.name);
    root->setProperty ("category", s.category);
    root->setProperty ("description", s.description);
    root->setProperty ("author", s.author);

    auto* params = new juce::DynamicObject();
    for (int i = 0; i < kNumParams; ++i)
        params->setProperty (paramDef (i).id, (double) s.values[(size_t) i]);
    root->setProperty ("parameters", juce::var (params));

    auto* cab = new juce::DynamicObject();
    auto slot = [] (const juce::String& id, const juce::String& name)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("irId", id);
        o->setProperty ("irName", name);
        return juce::var (o);
    };
    cab->setProperty ("a", slot (s.irA, s.irAName));
    cab->setProperty ("b", slot (s.irB, s.irBName));
    root->setProperty ("cabinet", juce::var (cab));
    return juce::JSON::toString (juce::var (root));
}

bool PresetCodec::fromJson (const juce::String& json, PresetState& out, juce::String& error, juce::StringArray& warnings)
{
    juce::var root;
    const auto result = juce::JSON::parse (json, root);
    if (result.failed() || ! root.isObject())
    {
        error = "The preset file is not valid JSON.";
        return false;
    }
    if (root["format"].toString() != kFormatTag)
    {
        error = "This file is not an ZackAMP preset.";
        return false;
    }
    const int version = (int) root["schemaVersion"];
    if (version < 1)
    {
        error = "The preset has an invalid schema version.";
        return false;
    }
    if (version > kSchemaVersion)
        warnings.add ("This preset was made by a newer version of ZackAMP; some settings may not load.");

    PresetState s; // defaults for anything missing
    s.name = root["name"].toString().trim();
    if (s.name.isEmpty())
        s.name = "Untitled";
    s.category = root.hasProperty ("category") ? root["category"].toString() : juce::String ("User");
    s.description = root["description"].toString();
    s.author = root["author"].toString();

    // ---- migrations: version N -> N+1 would go here, keyed on `version` ----

    if (const auto* params = root["parameters"].getDynamicObject())
    {
        for (const auto& prop : params->getProperties())
        {
            const int idx = findParamIndex (prop.name.toString().toStdString());
            if (idx < 0)
            {
                warnings.add ("Unknown parameter ignored: " + prop.name.toString());
                continue;
            }
            if (! (prop.value.isDouble() || prop.value.isInt() || prop.value.isInt64() || prop.value.isBool()))
            {
                warnings.add ("Invalid value ignored for " + prop.name.toString());
                continue;
            }
            s.values[(size_t) idx] = sanitiseParamValue (idx, (float) (double) prop.value);
        }
    }
    else
    {
        warnings.add ("The preset contains no parameters; defaults were used.");
    }

    // Presets made before the valve selectors existed: use the model's usual valves, which is
    // exactly how they sounded.
    if (const auto* params2 = root["parameters"].getDynamicObject())
    {
        const auto& voicing = dsp::getAmpVoicing ((int) s.values[(size_t) pAmpModel]);
        if (! params2->hasProperty (paramDef (pAmpPreampValve).id))
            s.values[(size_t) pAmpPreampValve] = (float) voicing.defaultPreampValve;
        if (! params2->hasProperty (paramDef (pAmpPowerValve).id))
            s.values[(size_t) pAmpPowerValve] = (float) voicing.defaultPowerValve;
    }

    const auto cab = root["cabinet"];
    s.irA = cab["a"]["irId"].toString();
    s.irAName = cab["a"]["irName"].toString();
    s.irB = cab["b"]["irId"].toString();
    s.irBName = cab["b"]["irName"].toString();
    out = s;
    return true;
}

//==============================================================================
PresetManager::PresetManager (juce::File userPresetDirectory) : userDir (std::move (userPresetDirectory)) {}

juce::StringArray PresetManager::getCategories()
{
    return { "Clean", "Edge of Breakup", "Blues", "Classic Rock", "British Crunch", "Lead", "High Gain", "Modern Metal", "Ambient", "User" };
}

int PresetManager::familyOfModel (int ampModel) { return ampModel <= 1 ? 0 : (ampModel <= 3 ? 1 : 2); }

void PresetManager::loadFactory()
{
    std::vector<std::pair<juce::String, PresetInfo>> found;
    for (int i = 0; i < AmpForgeData::namedResourceListSize; ++i)
    {
        const juce::String file = AmpForgeData::originalFilenames[i];
        if (! file.endsWith (".json") || file == "manifest.json")
            continue;
        int size = 0;
        const char* data = AmpForgeData::getNamedResource (AmpForgeData::namedResourceList[i], size);
        juce::String err;
        juce::StringArray warnings;
        PresetInfo info;
        info.isFactory = true;
        if (PresetCodec::fromJson (juce::String::fromUTF8 (data, size), info.state, err, warnings))
            found.emplace_back (file, info);
        else
            jassertfalse; // factory content must always parse (covered by tests)
    }
    // Factory files are named "NN-name.json": the numeric prefix gives the curated order.
    std::sort (found.begin(), found.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    presets.erase (std::remove_if (presets.begin(), presets.end(), [] (const PresetInfo& p) { return p.isFactory; }), presets.end());
    std::vector<PresetInfo> factory;
    for (auto& f : found)
        factory.push_back (f.second);
    presets.insert (presets.begin(), factory.begin(), factory.end());
}

juce::StringArray PresetManager::rescanUser()
{
    juce::StringArray problems;
    presets.erase (std::remove_if (presets.begin(), presets.end(), [] (const PresetInfo& p) { return ! p.isFactory; }), presets.end());
    if (! userDir.isDirectory())
        return problems;
    for (const auto& f : userDir.findChildFiles (juce::File::findFiles, false, "*.afpreset"))
    {
        if (f.getSize() > 1024 * 1024)
        {
            problems.add (f.getFileName() + ": file too large to be a preset");
            continue;
        }
        PresetInfo info;
        juce::String err;
        juce::StringArray warnings;
        if (! PresetCodec::fromJson (f.loadFileAsString(), info.state, err, warnings))
        {
            problems.add (f.getFileName() + ": " + err);
            continue;
        }
        info.file = f;
        presets.push_back (info);
    }
    sortPresets();
    return problems;
}

void PresetManager::sortPresets()
{
    std::stable_sort (presets.begin(), presets.end(), [] (const PresetInfo& a, const PresetInfo& b)
    {
        if (a.isFactory != b.isFactory)
            return a.isFactory;
        if (a.isFactory)
            return false; // keep curated order
        return a.state.name.compareNatural (b.state.name) < 0;
    });
}

int PresetManager::findIndex (const juce::String& name, bool factory) const
{
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].isFactory == factory && presets[i].state.name.equalsIgnoreCase (name))
            return (int) i;
    return -1;
}

juce::File PresetManager::fileForName (const juce::String& name) const
{
    auto safe = juce::File::createLegalFileName (name.trim()).trim();
    if (safe.isEmpty())
        safe = "Preset";
    return userDir.getChildFile (safe + ".afpreset");
}

juce::String PresetManager::makeUniqueName (const juce::String& base) const
{
    if (findIndex (base, false) < 0 && findIndex (base, true) < 0)
        return base;
    for (int n = 2; n < 1000; ++n)
    {
        const auto candidate = base + " (" + juce::String (n) + ")";
        if (findIndex (candidate, false) < 0 && findIndex (candidate, true) < 0)
            return candidate;
    }
    return base + " " + juce::Uuid().toString().substring (0, 6);
}

bool PresetManager::saveUser (const PresetState& stateIn, bool allowOverwrite, juce::String& error, int* newIndex)
{
    auto state = stateIn;
    state.name = state.name.trim();
    if (state.name.isEmpty())
    {
        error = "Please enter a preset name.";
        return false;
    }
    if (findIndex (state.name, true) >= 0)
    {
        error = "\"" + state.name + "\" is a factory preset name. Please choose another name.";
        return false;
    }
    const int existing = findIndex (state.name, false);
    if (existing >= 0 && ! allowOverwrite)
    {
        error = "A preset called \"" + state.name + "\" already exists.";
        return false;
    }

    // Two different names can map to the same legal file name: never clobber another preset.
    juce::File target = existing >= 0 ? presets[(size_t) existing].file : fileForName (state.name);
    if (existing < 0 && target.existsAsFile())
        target = target.getNonexistentSibling (true);

    if (! writeFileAtomically (target, PresetCodec::toJson (state), &error))
        return false;

    if (existing >= 0)
    {
        presets[(size_t) existing].state = state;
        presets[(size_t) existing].file = target;
    }
    else
    {
        PresetInfo info;
        info.state = state;
        info.file = target;
        presets.push_back (info);
        sortPresets();
    }
    if (newIndex != nullptr)
        *newIndex = findIndex (state.name, false);
    return true;
}

bool PresetManager::renameUser (int index, const juce::String& newNameIn, juce::String& error)
{
    if (index < 0 || index >= (int) presets.size() || presets[(size_t) index].isFactory)
    {
        error = "Factory presets cannot be renamed. Use Save As to make your own copy.";
        return false;
    }
    const auto newName = newNameIn.trim();
    if (newName.isEmpty())
    {
        error = "Please enter a preset name.";
        return false;
    }
    if (! newName.equalsIgnoreCase (presets[(size_t) index].state.name)
        && (findIndex (newName, false) >= 0 || findIndex (newName, true) >= 0))
    {
        error = "A preset called \"" + newName + "\" already exists.";
        return false;
    }
    auto info = presets[(size_t) index];
    info.state.name = newName;
    auto target = fileForName (newName);
    if (target != info.file && target.existsAsFile())
        target = target.getNonexistentSibling (true);
    if (! writeFileAtomically (target, PresetCodec::toJson (info.state), &error))
        return false;
    if (target != info.file)
        info.file.deleteFile();
    info.file = target;
    presets[(size_t) index] = info;
    sortPresets();
    return true;
}

bool PresetManager::deleteUser (int index, juce::String& error)
{
    if (index < 0 || index >= (int) presets.size() || presets[(size_t) index].isFactory)
    {
        error = "Factory presets cannot be deleted.";
        return false;
    }
    const auto f = presets[(size_t) index].file;
    if (f.existsAsFile() && ! f.moveToTrash() && ! f.deleteFile())
    {
        error = "Could not delete the preset file.";
        return false;
    }
    presets.erase (presets.begin() + index);
    return true;
}

bool PresetManager::duplicate (int index, juce::String& error, int* newIndex)
{
    if (index < 0 || index >= (int) presets.size())
    {
        error = "No preset selected.";
        return false;
    }
    auto s = presets[(size_t) index].state;
    s.name = makeUniqueName (s.name + " Copy");
    if (presets[(size_t) index].isFactory)
        s.category = presets[(size_t) index].state.category;
    return saveUser (s, false, error, newIndex);
}

std::vector<int> PresetManager::filter (const juce::String& text, const juce::String& category, int family) const
{
    std::vector<int> out;
    for (size_t i = 0; i < presets.size(); ++i)
    {
        const auto& s = presets[i].state;
        if (category.isNotEmpty() && ! s.category.equalsIgnoreCase (category))
            continue;
        if (family >= 0 && familyOfModel ((int) s.values[(size_t) pAmpModel]) != family)
            continue;
        if (text.isNotEmpty() && ! s.name.containsIgnoreCase (text) && ! s.description.containsIgnoreCase (text)
            && ! s.category.containsIgnoreCase (text))
            continue;
        out.push_back ((int) i);
    }
    return out;
}

} // namespace af

#include <doctest.h>
#include "../../Source/Presets/PresetApplier.h"
#include "../../Source/Platform/FileUtils.h"

using namespace af;

namespace
{
struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("ampforge-test-" + juce::Uuid().toString());
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};
} // namespace

TEST_CASE ("Preset JSON round trip preserves every parameter and IR reference")
{
    PresetState s;
    s.name = "Round Trip";
    s.category = "Lead";
    s.description = "desc";
    for (int i = 0; i < kNumParams; ++i)
        s.values[(size_t) i] = normalisedToPlain (i, 0.37f);
    s.irA = "factory:br-4x12-v25-cap";
    s.irB = "user:1234";
    s.irBName = "My Cab";
    const auto json = PresetCodec::toJson (s);
    PresetState back;
    juce::String err;
    juce::StringArray warnings;
    REQUIRE (PresetCodec::fromJson (json, back, err, warnings));
    CHECK (warnings.isEmpty());
    CHECK (back.name == "Round Trip");
    CHECK (back.category == "Lead");
    CHECK (back.irA == s.irA);
    CHECK (back.irBName == "My Cab");
    CHECK (back.sameSoundAs (s));
}

TEST_CASE ("Invalid presets fail gracefully; partial presets use defaults")
{
    PresetState out;
    juce::String err;
    juce::StringArray w;
    CHECK_FALSE (PresetCodec::fromJson ("not json", out, err, w));
    CHECK (err.isNotEmpty());
    CHECK_FALSE (PresetCodec::fromJson ("{\"format\":\"something-else\"}", out, err, w));
    CHECK_FALSE (PresetCodec::fromJson ("[1,2,3]", out, err, w));
    CHECK_FALSE (PresetCodec::fromJson ("", out, err, w));

    w.clear();
    REQUIRE (PresetCodec::fromJson (R"({"format":"ampforge.preset","schemaVersion":1,"name":"P",
        "parameters":{"amp.gain":0.9,"bogus.param":3,"amp.bass":"oops","amp.mid":99}})", out, err, w));
    CHECK (out.values[(size_t) pAmpGain] == doctest::Approx (0.9f));
    CHECK (out.values[(size_t) pAmpMid] == doctest::Approx (1.0f)); // clamped
    CHECK (out.values[(size_t) pAmpTreble] == doctest::Approx (paramDef (pAmpTreble).def));
    CHECK (w.size() == 2); // unknown param + invalid value

    w.clear();
    REQUIRE (PresetCodec::fromJson (R"({"format":"ampforge.preset","schemaVersion":99,"name":"Future"})", out, err, w));
    CHECK (w.joinIntoString (" ").contains ("newer"));
}

TEST_CASE ("All factory presets parse and reference existing factory IRs")
{
    TempDir t;
    PresetManager pm (t.dir);
    pm.loadFactory();
    IRLibrary lib (t.dir.getChildFile ("ir"));
    lib.loadFactory();
    CHECK (pm.getPresets().size() >= 16);
    juce::StringArray cats;
    for (const auto& p : pm.getPresets())
    {
        INFO (p.state.name.toStdString());
        CHECK (p.isFactory);
        CHECK (p.state.description.isNotEmpty());
        CHECK (lib.findEntry (p.state.irA).has_value());
        if (p.state.irB.isNotEmpty())
            CHECK (lib.findEntry (p.state.irB).has_value());
        CHECK (PresetManager::getCategories().contains (p.state.category));
        cats.addIfNotAlreadyThere (p.state.category);
    }
    CHECK (cats.size() >= 8);
}

TEST_CASE ("User preset save, collision, rename, duplicate, delete")
{
    TempDir t;
    PresetManager pm (t.dir);
    pm.loadFactory();
    const auto factoryCount = pm.getPresets().size();
    PresetState s;
    s.name = "My Tone";
    s.values[(size_t) pAmpGain] = 0.81f;
    juce::String err;
    int idx = -1;
    REQUIRE (pm.saveUser (s, false, err, &idx));
    CHECK (pm.getPresets().size() == factoryCount + 1);

    // Collision without overwrite is refused; with overwrite it replaces.
    s.values[(size_t) pAmpGain] = 0.2f;
    CHECK_FALSE (pm.saveUser (s, false, err));
    CHECK (err.contains ("already exists"));
    REQUIRE (pm.saveUser (s, true, err));
    CHECK (pm.getPresets()[(size_t) pm.findIndex ("My Tone", false)].state.values[(size_t) pAmpGain] == doctest::Approx (0.2f));

    // Factory names are reserved.
    PresetState f;
    f.name = pm.getPresets()[0].state.name;
    CHECK_FALSE (pm.saveUser (f, true, err));

    // Rescan from disk finds the same data.
    PresetManager pm2 (t.dir);
    pm2.loadFactory();
    CHECK (pm2.rescanUser().isEmpty());
    REQUIRE (pm2.findIndex ("My Tone", false) >= 0);

    REQUIRE (pm.renameUser (pm.findIndex ("My Tone", false), "Renamed", err));
    CHECK (pm.findIndex ("My Tone", false) < 0);
    int dup = -1;
    REQUIRE (pm.duplicate (pm.findIndex ("Renamed", false), err, &dup));
    CHECK (pm.getPresets()[(size_t) dup].state.name == "Renamed Copy");
    REQUIRE (pm.deleteUser (pm.findIndex ("Renamed", false), err));
    CHECK (pm.findIndex ("Renamed", false) < 0);
    CHECK_FALSE (pm.deleteUser (0, err)); // factory
}

TEST_CASE ("Corrupt user preset files are skipped and reported")
{
    TempDir t;
    t.dir.getChildFile ("Broken.afpreset").replaceWithText ("{ this is not json");
    t.dir.getChildFile ("Empty.afpreset").replaceWithText ("");
    PresetManager pm (t.dir);
    const auto problems = pm.rescanUser();
    CHECK (problems.size() == 2);
    CHECK (pm.getPresets().empty());
}

TEST_CASE ("Filtering by text, category and family")
{
    TempDir t;
    PresetManager pm (t.dir);
    pm.loadFactory();
    CHECK_FALSE (pm.filter ("clean", "", -1).empty());
    for (int i : pm.filter ("", "High Gain", -1))
        CHECK (pm.getPresets()[(size_t) i].state.category == "High Gain");
    for (int i : pm.filter ("", "", 2))
        CHECK (PresetManager::familyOfModel ((int) pm.getPresets()[(size_t) i].state.values[(size_t) pAmpModel]) == 2);
    CHECK (pm.filter ("zzzz-no-match", "", -1).empty());
}

TEST_CASE ("Applying a preset restores the full engine state; missing IR falls back")
{
    TempDir t;
    IRLibrary lib (t.dir);
    lib.loadFactory();
    AmpForgeEngine e;
    e.prepare (48000, 128);

    PresetState s;
    s.values[(size_t) pAmpModel] = 3;
    s.values[(size_t) pAmpGain] = 0.77f;
    s.values[(size_t) pEqOn] = 1;
    s.values[(size_t) pEq750] = -4;
    s.values[(size_t) pDelayOn] = 1;
    s.irA = "factory:br-4x12-h75-cap";
    juce::StringArray warnings;
    applyState (e, lib, s, warnings);
    CHECK (warnings.isEmpty());
    const auto captured = captureState (e);
    CHECK (captured.sameSoundAs (s));

    s.irA = "user:does-not-exist";
    s.irAName = "Lost Cab";
    warnings.clear();
    applyState (e, lib, s, warnings);
    REQUIRE (warnings.size() == 1);
    CHECK (warnings[0].contains ("Lost Cab"));
    CHECK (e.getCabinetInfo (0).irId == dsp::getAmpVoicing (3).defaultIrId);
}

TEST_CASE ("Atomic writes never leave partial files")
{
    TempDir t;
    const auto f = t.dir.getChildFile ("a/b/c.txt");
    juce::String err;
    REQUIRE (writeFileAtomically (f, "hello", &err));
    REQUIRE (writeFileAtomically (f, "world", &err));
    CHECK (f.loadFileAsString() == "world");
    CHECK (t.dir.getChildFile ("a/b").findChildFiles (juce::File::findFiles, false).size() == 1);
}

TEST_CASE ("Presets without valve settings get the model's usual valves")
{
    PresetState out;
    juce::String err;
    juce::StringArray w;
    REQUIRE (PresetCodec::fromJson (R"({"format":"ampforge.preset","schemaVersion":2,"name":"No Valves",
        "parameters":{"amp.model":2,"amp.gain":0.7}})", out, err, w));
    CHECK ((int) out.values[(size_t) pAmpPreampValve] == dsp::getAmpVoicing (2).defaultPreampValve);
    CHECK ((int) out.values[(size_t) pAmpPowerValve] == dsp::getAmpVoicing (2).defaultPowerValve);
    REQUIRE (PresetCodec::fromJson (R"({"format":"ampforge.preset","schemaVersion":2,"name":"Swapped",
        "parameters":{"amp.model":2,"amp.preampValve":0,"amp.powerValve":4}})", out, err, w));
    CHECK ((int) out.values[(size_t) pAmpPowerValve] == 4);
}

TEST_CASE ("Version 1 presets are moved to the closest model of the new line-up")
{
    PresetState out;
    juce::String err;
    juce::StringArray w;
    const int expected[] = { 1, 0, 3, 4, 5, 9 }; // old clean, tweed, plexi, lead, cascade lead/heavy
    for (int old = 0; old < 6; ++old)
    {
        const auto json = juce::String (R"({"format":"ampforge.preset","schemaVersion":1,"name":"Old","parameters":{"amp.model":)")
                          + juce::String (old) + R"(,"amp.preampValve":1,"amp.powerValve":4}})";
        REQUIRE (PresetCodec::fromJson (json, out, err, w));
        const int m = (int) out.values[(size_t) pAmpModel];
        CHECK (m == expected[old]);
        CHECK ((int) out.values[(size_t) pAmpPreampValve] == dsp::getAmpVoicing (m).defaultPreampValve);
        CHECK ((int) out.values[(size_t) pAmpPowerValve] == dsp::getAmpVoicing (m).defaultPowerValve);
    }
}

TEST_CASE ("Single-compressor presets move to the CS-3 pedal or the Studio Comp")
{
    PresetState out;
    juce::String err;
    juce::StringArray w;
    REQUIRE (PresetCodec::fromJson (R"({"format":"ampforge.preset","schemaVersion":2,"name":"Studio",
        "parameters":{"comp.enabled":1,"comp.type":0,"comp.position":1,"comp.sustain":0.8,"comp.level":3}})", out, err, w));
    CHECK (w.isEmpty());
    CHECK (out.values[(size_t) pStudioCompOn] == 1.0f);
    CHECK (out.values[(size_t) pStudioCompAmount] == doctest::Approx (0.8f));
    CHECK (out.values[(size_t) pStudioCompLevel] == doctest::Approx (3.0f));
    CHECK (out.values[(size_t) pCompOn] == 0.0f);
    REQUIRE (PresetCodec::fromJson (R"({"format":"ampforge.preset","schemaVersion":2,"name":"Pedal",
        "parameters":{"comp.enabled":1,"comp.type":1,"comp.sustain":0.7}})", out, err, w));
    CHECK (out.values[(size_t) pCompOn] == 1.0f);
    CHECK (out.values[(size_t) pCompSustain] == doctest::Approx (0.7f));
    CHECK (out.values[(size_t) pStudioCompOn] == 0.0f);
}

TEST_CASE ("Studio reverb factory presets load their reverb settings into the engine")
{
    TempDir t;
    PresetManager pm (t.dir);
    pm.loadFactory();
    IRLibrary lib (t.dir.getChildFile ("ir"));
    lib.loadFactory();
    int found = 0;
    for (const auto& p : pm.getPresets())
    {
        if (p.state.category != "Studio Reverb")
            continue;
        ++found;
        INFO (p.state.name.toStdString());
        AmpForgeEngine e;
        e.prepare (48000, 256);
        juce::StringArray warnings;
        applyState (e, lib, p.state, warnings);
        MESSAGE (p.state.name << ": type " << e.params().getInt (pReverbType) << " on " << e.params().getBool (pReverbOn)
                              << " mix " << e.params().get (pReverbMix) << " predelay " << e.params().get (pReverbPreDelay));
        CHECK (e.params().getBool (pReverbOn));
        CHECK (e.params().getInt (pReverbType) >= dsp::Reverb::lexicon);
    }
    CHECK (found == 5);
}

TEST_CASE ("Version 2 presets keep their amp after the selector was reordered (schema 3)")
{
    // v2 numbering: Tweed, JC120, AC30, JCM800, Coliseum, MP1, Triple, Dual, Plexi, 5150.
    const char* names[] = { "Tweed", "JC120", "AC30", "JCM800", "Coliseum", "MP1", "Triple", "Dual", "Plexi", "5150" };
    for (int old = 0; old < 10; ++old)
    {
        PresetState out;
        juce::String err;
        juce::StringArray w;
        const auto json = juce::String (R"({"format":"ampforge.preset","schemaVersion":2,"name":"Old","parameters":{"amp.model":)")
                          + juce::String (old) + R"(,"amp.preampValve":0,"amp.powerValve":0}})";
        REQUIRE (PresetCodec::fromJson (json, out, err, w));
        INFO (names[old]);
        CHECK (juce::String (dsp::getAmpVoicing ((int) out.values[(size_t) pAmpModel]).name) == names[old]);
        CHECK ((int) out.values[(size_t) pAmpPowerValve] == 0); // explicit valves are kept
    }
    // Saved presets are written with the new schema and read back unchanged.
    PresetState s;
    s.values[(size_t) pAmpModel] = 3; // Plexi
    PresetState back;
    juce::String err;
    juce::StringArray w;
    REQUIRE (PresetCodec::fromJson (PresetCodec::toJson (s), back, err, w));
    CHECK ((int) back.values[(size_t) pAmpModel] == 3);
}

#include "MainComponent.h"
#include "../Platform/FileUtils.h"

namespace af::ui
{
namespace
{
    enum MenuIds
    {
        idSave = 1, idSaveAs, idImportIR, idRevealData, idPresetBrowser,
        idUndo = 20, idRedo,
        idAudioSettings = 40, idBypass, idSafe,
        idMidiSettings = 60,
        idSetupGuide = 80, idAbout,
    };
    constexpr int kMinContentHeight = 700;
} // namespace

MainComponent::MainComponent (AppController& a) : app (a)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    content.addAndMakeVisible (ampPanel);
    content.addAndMakeVisible (eqPanel);
    content.addAndMakeVisible (cabPanel);
    content.addAndMakeVisible (fxPanel);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (topBar);
    addAndMakeVisible (viewport);
    addAndMakeVisible (statusBar);
    refreshables = { &topBar, &ampPanel, &eqPanel, &cabPanel, &fxPanel, &statusBar };

    topBar.onPresetBrowser = [this] { openPresetBrowser(); };
    topBar.onSave = [this] { saveCurrent(); };
    topBar.onSaveAs = [this] { openSaveAs(); };
    topBar.onAudioSettings = [this] { openAudioSettings(); };
    topBar.onMidiSettings = [this] { openMidiSettings(); };
    topBar.onMenu = [this]
    {
        juce::PopupMenu m;
        const auto names = getMenuBarNames();
        for (int i = 0; i < names.size(); ++i)
            m.addSubMenu (names[i], buildMenu (i));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&topBar.menu),
                         [this] (int id) { if (id > 0) menuItemSelected (id, 0); });
    };
    cabPanel.onBrowse = [this] (int slot) { openIRBrowser (slot); };
    cabPanel.onImport = [this] (int slot) { importIRs (slot); };

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
#endif
    app.addChangeListener (this);
    setWantsKeyboardFocus (true);
    setSize (1280, 840);
    if (app.needsOnboarding())
        showOnboarding (true);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
#endif
    app.removeChangeListener (this);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MainComponent::paint (juce::Graphics& g) { g.fillAll (Colours::background); }

void MainComponent::resized()
{
    auto r = getLocalBounds();
    topBar.setBounds (r.removeFromTop (56));
    statusBar.setBounds (r.removeFromBottom (30));
    viewport.setBounds (r);

    // Content scrolls vertically only when the window is shorter than the minimum layout.
    const int w = r.getWidth() - (r.getHeight() < kMinContentHeight ? viewport.getScrollBarThickness() : 0);
    const int h = std::max (r.getHeight(), kMinContentHeight);
    content.setSize (w, h);
    auto c = content.getLocalBounds().reduced (10);
    ampPanel.setBounds (c.removeFromTop (juce::roundToInt (h * 0.34)));
    c.removeFromTop (10);
    auto middle = c.removeFromTop (juce::roundToInt (h * 0.30));
    eqPanel.setBounds (middle.removeFromLeft (middle.getWidth() * 55 / 100));
    middle.removeFromLeft (10);
    cabPanel.setBounds (middle);
    c.removeFromTop (10);
    fxPanel.setBounds (c);
    if (onboarding != nullptr)
        onboarding->setBounds (getLocalBounds());
}

void MainComponent::timerCallback()
{
    for (auto* r : refreshables)
        r->refresh();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto* r : refreshables)
        r->refresh();
    menuItemsChanged();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const bool cmd = mods.isCommandDown();
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    if (cmd && c == 's' && mods.isShiftDown()) { openSaveAs(); return true; }
    if (cmd && c == 's') { saveCurrent(); return true; }
    if (cmd && c == 'z' && mods.isShiftDown()) { app.undo.redo(); return true; }
    if (cmd && c == 'z') { app.undo.undo(); return true; }
    if (cmd && c == 'p') { openPresetBrowser(); return true; }
    if (cmd && key.getKeyCode() == juce::KeyPress::rightKey) { app.stepPreset (1); return true; }
    if (cmd && key.getKeyCode() == juce::KeyPress::leftKey) { app.stepPreset (-1); return true; }
    if (cmd && c == ',') { openAudioSettings(); return true; }
    return false;
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames() { return { "File", "Edit", "Audio", "MIDI", "Help" }; }

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&) { return buildMenu (index); }

juce::PopupMenu MainComponent::buildMenu (int index)
{
    juce::PopupMenu m;
    const int cur = app.getCurrentPresetIndex();
    const bool canSave = cur >= 0 && ! app.presets.getPresets()[(size_t) cur].isFactory;
    switch (index)
    {
        case 0:
            m.addItem (idPresetBrowser, "Browse Presets...");
            m.addItem (idSave, "Save Preset", canSave);
            m.addItem (idSaveAs, "Save Preset As...");
            m.addSeparator();
            m.addItem (idImportIR, "Import Impulse Responses...");
            m.addItem (idRevealData, "Show User Data Folder");
            break;
        case 1:
            m.addItem (idUndo, "Undo " + app.undo.getUndoDescription(), app.undo.canUndo());
            m.addItem (idRedo, "Redo " + app.undo.getRedoDescription(), app.undo.canRedo());
            break;
        case 2:
            m.addItem (idAudioSettings, "Audio Settings...");
            m.addItem (idBypass, "Bypass Processing", true, app.params().getBool (pBypass));
            m.addItem (idSafe, "Reset to Safe Settings");
            break;
        case 3:
            m.addItem (idMidiSettings, "MIDI Settings...");
            break;
        default:
            m.addItem (idSetupGuide, "Setup Guide");
            m.addItem (idAbout, "About " AMPFORGE_PRODUCT_NAME);
            break;
    }
    return m;
}

void MainComponent::menuItemSelected (int id, int)
{
    switch (id)
    {
        case idPresetBrowser: openPresetBrowser(); break;
        case idSave: saveCurrent(); break;
        case idSaveAs: openSaveAs(); break;
        case idImportIR: importIRs (0); break;
        case idRevealData: getAppDataDirectory().createDirectory(); getAppDataDirectory().revealToUser(); break;
        case idUndo: app.undo.undo(); break;
        case idRedo: app.undo.redo(); break;
        case idAudioSettings: openAudioSettings(); break;
        case idBypass: app.setParamWithUndo (pBypass, app.params().getBool (pBypass) ? 0.0f : 1.0f); break;
        case idSafe: app.resetToSafeSettings(); break;
        case idMidiSettings: openMidiSettings(); break;
        case idSetupGuide: showOnboarding (true); break;
        case idAbout:
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "About " AMPFORGE_PRODUCT_NAME,
                AMPFORGE_PRODUCT_NAME " " AMPFORGE_VERSION_STRING "\n\nOriginal amplifier models inspired by classic American, British and modern "
                "high-gain amplifier design principles. Not affiliated with or endorsed by any amplifier manufacturer.\n\n"
                "Built with JUCE. See LICENSES.md and docs/THIRD_PARTY_NOTICES.md.");
            break;
        default: break;
    }
}

//==============================================================================
void MainComponent::openAudioSettings() { showDialog ("Audio Settings", std::make_unique<AudioSettings> (app), this); }
void MainComponent::openMidiSettings() { showDialog ("MIDI Settings", std::make_unique<MidiSettings> (app), this); }
void MainComponent::openPresetBrowser() { showDialog ("Presets", std::make_unique<PresetBrowser> (app), this); }
void MainComponent::openSaveAs() { showDialog ("Save Preset As", std::make_unique<SaveAsForm> (app, nullptr), this, false); }

void MainComponent::saveCurrent()
{
    juce::String err;
    const int cur = app.getCurrentPresetIndex();
    if (cur < 0 || app.presets.getPresets()[(size_t) cur].isFactory)
    {
        openSaveAs(); // factory presets are read-only: Save becomes Save As
        return;
    }
    if (! app.saveCurrent (err))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save", err);
}

void MainComponent::openIRBrowser (int slot)
{
    if (slot == 1 && ! app.params().getBool (pCabDual))
        app.setParamWithUndo (pCabDual, 1.0f);
    showDialog (slot == 0 ? "Cabinet A - Impulse Responses" : "Cabinet B - Impulse Responses",
                std::make_unique<IRBrowser> (app, slot, [this, slot] { importIRs (slot); }), this);
}

void MainComponent::importIRs (int slot)
{
    chooser = std::make_unique<juce::FileChooser> ("Import impulse responses", juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.wav;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, slot] (const juce::FileChooser& fc)
    {
        const auto files = fc.getResults();
        if (files.isEmpty())
            return;
        juce::Component::SafePointer<MainComponent> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsYesNoCancel (
                                          juce::MessageBoxIconType::QuestionIcon, "Import impulse responses",
                                          "Copy the files into the AMP FORGE IR library (recommended: presets keep working if you move or delete the originals), "
                                          "or reference them where they are?", "Copy", "Reference", "Cancel", this),
                                      [safe, files, slot] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            juce::String lastId;
            juce::StringArray failures;
            for (const auto& f : files)
            {
                auto r = safe->app.importIR (f, result == 1);
                if (r.ok)
                    lastId = r.entry.id;
                else
                    failures.add (f.getFileName() + ": " + r.error);
            }
            if (lastId.isNotEmpty())
                safe->app.setCabinetIR (slot, lastId);
            if (! failures.isEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Some files could not be imported", failures.joinIntoString ("\n"));
        });
    });
}

void MainComponent::showOnboarding (bool show)
{
    if (! show)
    {
        onboarding.reset();
        return;
    }
    onboarding = std::make_unique<Onboarding> (app);
    onboarding->onOpenAudio = [this] { openAudioSettings(); };
    onboarding->onDismiss = [this]
    {
        app.setOnboardingDone();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe) safe->showOnboarding (false); });
    };
    addAndMakeVisible (*onboarding);
    onboarding->setBounds (getLocalBounds());
}
} // namespace af::ui

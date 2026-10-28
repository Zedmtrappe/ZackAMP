#include "MainComponent.h"
#include "../App/MicPermission.h"
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
    constexpr int kTopBarHeight = 56, kStatusHeight = 30, kBannerHeight = 46;
    constexpr int kHeaderHeight = 34, kGap = 8, kMargin = 10;
    constexpr int kEqCabHeight = 250, kFxHeight = 240;
} // namespace

PermissionBanner::PermissionBanner()
{
    openSettings.onClick = [] { openMicPrivacySettings(); };
    addAndMakeVisible (openSettings);
}

void PermissionBanner::resized()
{
    openSettings.setBounds (getLocalBounds().reduced (10, 7).removeFromRight (190));
}

void PermissionBanner::paint (juce::Graphics& g)
{
    g.fillAll (Colours::warn.withAlpha (0.18f));
    g.setColour (Colours::warn);
    g.fillRect (getLocalBounds().removeFromLeft (4));
    g.setFont (ForgeLookAndFeel::labelFont (13.0f));
    auto r = getLocalBounds().reduced (14, 0).withTrimmedRight (210);
    g.drawFittedText ("Z.AMP can't hear your guitar: macOS is blocking audio input. macOS calls every audio input "
                      "\"Microphone\", including your interface. Allow Z.AMP under Privacy & Security > Microphone.",
                      r, juce::Justification::centredLeft, 2);
}

MainComponent::MainComponent (AppController& a) : app (a)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    content.addAndMakeVisible (ampHead);
    content.addAndMakeVisible (eqCabHeader);
    content.addAndMakeVisible (fxHeader);
    content.addAndMakeVisible (eqPanel);
    content.addAndMakeVisible (cabPanel);
    content.addAndMakeVisible (fxPanel);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (topBar);
    addAndMakeVisible (viewport);
    addAndMakeVisible (statusBar);
    addChildComponent (permissionBanner);
    eqCabHeader.setOpen (app.getSettings().getBoolValue ("ui.showEqCab", false));
    fxHeader.setOpen (app.getSettings().getBoolValue ("ui.showEffects", false));
    eqCabHeader.onToggle = [this] { toggleSection (eqCabHeader, "ui.showEqCab"); };
    fxHeader.onToggle = [this] { toggleSection (fxHeader, "ui.showEffects"); };
    applySectionVisibility();
    refreshables = { &topBar, &ampHead, &eqPanel, &cabPanel, &fxPanel, &statusBar };

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

void MainComponent::applySectionVisibility()
{
    eqPanel.setVisible (eqCabHeader.isOpen());
    cabPanel.setVisible (eqCabHeader.isOpen());
    fxPanel.setVisible (fxHeader.isOpen());
}

int MainComponent::idealHeight() const
{
    return kTopBarHeight + kStatusHeight + (permissionBanner.isVisible() ? kBannerHeight : 0) + 2 * kMargin
           + AmpHead::kPreferredHeight + 2 * (kGap + kHeaderHeight) + (eqCabHeader.isOpen() ? kGap + kEqCabHeight : 0)
           + (fxHeader.isOpen() ? kGap + kFxHeight : 0);
}

void MainComponent::toggleSection (SectionHeader& header, const char* settingsKey)
{
    header.setOpen (! header.isOpen());
    app.getSettings().setValue (settingsKey, header.isOpen());
    applySectionVisibility();

    // Grow or shrink the window to fit, within the screen; scroll if the screen is too small.
    if (auto* window = findParentComponentOfClass<juce::ResizableWindow>())
    {
        if (! window->isFullScreen() && ! window->isMinimised())
        {
            auto bounds = window->getBounds();
            bounds.setHeight (bounds.getHeight() + idealHeight() - getHeight());
            if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (window->getScreenBounds()))
            {
                const auto area = display->userArea;
                bounds.setHeight (std::min (bounds.getHeight(), area.getHeight() - (window->getScreenBounds().getY() - bounds.getY()) - 4));
                if (bounds.getBottom() > area.getBottom())
                    bounds.setY (std::max (area.getY(), area.getBottom() - bounds.getHeight()));
            }
            window->setBounds (bounds);
        }
    }
    resized();
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    topBar.setBounds (r.removeFromTop (kTopBarHeight));
    statusBar.setBounds (r.removeFromBottom (kStatusHeight));
    if (permissionBanner.isVisible())
        permissionBanner.setBounds (r.removeFromTop (kBannerHeight));
    viewport.setBounds (r);

    // The amp head takes the space left by the open sections, within its limits; content scrolls
    // vertically when the window is too short even for the smallest head.
    const int sections = 2 * kMargin + 2 * (kGap + kHeaderHeight) + (eqCabHeader.isOpen() ? kGap + kEqCabHeight : 0)
                         + (fxHeader.isOpen() ? kGap + kFxHeight : 0);
    const int headH = juce::jlimit (AmpHead::kMinHeight, AmpHead::kMaxHeight, r.getHeight() - sections);
    const int h = std::max (r.getHeight(), headH + sections);
    const int w = r.getWidth() - (h > r.getHeight() ? viewport.getScrollBarThickness() : 0);
    content.setSize (w, h);
    auto c = content.getLocalBounds().reduced (kMargin);
    ampHead.setBounds (c.removeFromTop (headH));
    c.removeFromTop (kGap);
    eqCabHeader.setBounds (c.removeFromTop (kHeaderHeight));
    if (eqCabHeader.isOpen())
    {
        c.removeFromTop (kGap);
        auto middle = c.removeFromTop (kEqCabHeight);
        eqPanel.setBounds (middle.removeFromLeft (middle.getWidth() * 50 / 100));
        middle.removeFromLeft (10);
        cabPanel.setBounds (middle);
    }
    c.removeFromTop (kGap);
    fxHeader.setBounds (c.removeFromTop (kHeaderHeight));
    if (fxHeader.isOpen())
    {
        c.removeFromTop (kGap);
        fxPanel.setBounds (c.removeFromTop (kFxHeight));
    }
    if (onboarding != nullptr)
        onboarding->setBounds (getLocalBounds());
}

void MainComponent::timerCallback()
{
    for (auto* r : refreshables)
        r->refresh();

    // Poll the macOS audio-input permission about twice a second (cheap system call).
    if (--permissionPollCountdown <= 0)
    {
        permissionPollCountdown = 15;
        const bool denied = getMicPermission() == MicPermission::denied;
        if (denied != micDenied)
        {
            const bool justGranted = micDenied && ! denied;
            micDenied = denied;
            permissionBanner.setVisible (denied);
            if (onboarding != nullptr)
                onboarding->toFront (false); // the setup guide stays on top of the banner
            resized();
            if (justGranted)
                app.restartAudio(); // reopen the input now that macOS delivers real samples
        }
    }
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
                                          "Copy the files into the Z.AMP IR library (recommended: presets keep working if you move or delete the originals), "
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

// ZackAMP — main window content.
#pragma once

#include "Dialogs.h"
#include "AmpHead.h"

namespace af::ui
{
/** Shown while macOS denies audio-input ("Microphone") access: the guitar input is silent. */
class PermissionBanner final : public juce::Component
{
public:
    PermissionBanner();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::TextButton openSettings { "Open System Settings" };
};

class MainComponent final : public juce::Component, public juce::MenuBarModel, private juce::Timer, private juce::ChangeListener
{
public:
    explicit MainComponent (AppController& app);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int itemId, int topLevelIndex) override;

    void openAudioSettings();
    void openMidiSettings();
    void openPresetBrowser();
    void openSaveAs();
    void saveCurrent();
    void openIRBrowser (int slot);
    void importIRs (int slot);
    void showOnboarding (bool show);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    juce::PopupMenu buildMenu (int index);
    void toggleSection (SectionHeader& header, const char* settingsKey);
    void applySectionVisibility();
    int idealHeight() const;

    AppController& app;
    ForgeLookAndFeel lnf;
    TopBar topBar { app };
    AmpHead ampHead { app };
    SectionHeader eqCabHeader { "EQ & Cabinet", "Graphic EQ, speaker cabinet / IRs" };
    SectionHeader fxHeader { "Effects", "Gate, comp, wah, drive, smooth, pitch, chorus, delay, reverb" };
    EQPanel eqPanel { app };
    CabinetPanel cabPanel { app };
    EffectsPanel fxPanel { app };
    StatusBar statusBar { app };
    juce::Component content;
    juce::Viewport viewport;
    std::unique_ptr<Onboarding> onboarding;
    PermissionBanner permissionBanner;
    bool micDenied = false;
    int permissionPollCountdown = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };
    std::vector<Refreshable*> refreshables;
};
} // namespace af::ui

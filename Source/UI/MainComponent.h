// AMP FORGE — main window content.
#pragma once

#include "Dialogs.h"
#include "Panels.h"

namespace af::ui
{
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

    AppController& app;
    ForgeLookAndFeel lnf;
    TopBar topBar { app };
    AmpPanel ampPanel { app };
    EQPanel eqPanel { app };
    CabinetPanel cabPanel { app };
    EffectsPanel fxPanel { app };
    StatusBar statusBar { app };
    juce::Component content;
    juce::Viewport viewport;
    std::unique_ptr<Onboarding> onboarding;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };
    std::vector<Refreshable*> refreshables;
};
} // namespace af::ui

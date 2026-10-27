// ZackAMP — dialogs: preset browser, save-as, IR browser, MIDI settings, audio settings,
// first-launch setup guide.
#pragma once

#include "Controls.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace af::ui
{
/** Opens `content` in a modal-less dialog window owned by JUCE. */
void showDialog (const juce::String& title, std::unique_ptr<juce::Component> content, juce::Component* parent, bool resizable = true);

class PresetBrowser final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit PresetBrowser (AppController& app);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    void timerCallback() override;
    void refilter();
    int selectedPresetIndex() const;

    AppController& app;
    juce::TextEditor search;
    juce::ComboBox category, family;
    juce::ListBox list { "Presets", this };
    juce::TextButton rename { "Rename" }, duplicate { "Duplicate" }, remove { "Delete" }, reveal { "Show Folder" };
    juce::Label info;
    std::vector<int> rows;
    size_t lastCount = 0;
    int lastCurrent = -2;
};

/** Name / category / description form for Save As. */
class SaveAsForm final : public juce::Component
{
public:
    SaveAsForm (AppController& app, std::function<void()> onDone);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void attemptSave();
    AppController& app;
    std::function<void()> done;
    juce::TextEditor name, description;
    juce::ComboBox category;
    juce::TextButton save { "Save" }, cancel { "Cancel" };
    juce::Label error;
};

class IRBrowser final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    IRBrowser (AppController& app, int slot, std::function<void()> onImportRequest);
    ~IRBrowser() override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void timerCallback() override;
    void reload();
    void updateInfo();

    AppController& app;
    const int slot;
    juce::String originalId;
    bool committed = false;
    std::vector<IREntry> entries, shown;
    juce::TextEditor search;
    juce::ListBox list { "Impulse responses", this };
    juce::TextButton importButton { "Import..." }, removeButton { "Remove from Library" }, keep { "Keep" }, revert { "Revert" };
    juce::Label info;
    std::function<void()> requestImport;
    size_t lastEntryCount = 0;
};

class MidiSettings final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit MidiSettings (AppController& app);
    ~MidiSettings() override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    int getNumRows() override { return (int) mappings.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void timerCallback() override;

    AppController& app;
    juce::ComboBox channel, learnParam, pcOffset;
    juce::ToggleButton pcEnabled { "Program Change selects presets" }, toggleMode { "Footswitch toggle mode for learned switches" };
    juce::TextButton learn { "Learn" }, removeMapping { "Remove Selected" }, defaults { "Reset to Defaults" };
    juce::ListBox list { "Mappings", this };
    juce::Label status;
    std::vector<CCMapping> mappings;
    uint32_t lastActivity = 0;
    std::vector<int> learnableParams;
};

class AudioSettings final : public juce::Component, private juce::ChangeListener
{
public:
    explicit AudioSettings (AppController& app);
    ~AudioSettings() override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshInputs();
    AppController& app;
    juce::AudioDeviceSelectorComponent selector;
    juce::ComboBox guitarInput, oversampling;
    juce::Label guidance;
};

/** First-launch setup guide drawn over the main window. */
class Onboarding final : public juce::Component
{
public:
    explicit Onboarding (AppController& app);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onOpenAudio, onDismiss;

private:
    AppController& app;
    juce::TextButton openAudio { "Open Audio Settings" }, done { "Start Playing" };
    juce::Rectangle<int> card;
};
} // namespace af::ui

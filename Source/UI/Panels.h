// ZackAMP — main window sections: amp, graphic EQ, cabinet, effects, top bar, status bar.
#pragma once

#include "Controls.h"

namespace af::ui
{
class Refreshable
{
public:
    virtual ~Refreshable() = default;
    virtual void refresh() = 0;
};

//==============================================================================
class AmpPanel final : public juce::Component, public Refreshable
{
public:
    explicit AmpPanel (AppController& app);
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void applyStyle (int model);
    AppController& app;
    // One fixed row: every control is always visible and works on every model.
    ParamKnob model, gain, bass, mid, treble, presence, depth, master, sag, variac;
    ParamToggle pull, bright;
    ParamChoice preampValve, powerValve;
    int shownModel = -1;
    juce::Rectangle<int> plateArea, titleArea, powerArea, preampLabel, powerLabel;
};

//==============================================================================
class EQPanel final : public juce::Component, public Refreshable
{
public:
    explicit EQPanel (AppController& app);
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AppController& app;
    ParamToggle enable;
    ParamChoice position;
    juce::TextButton flat { "Flat" }, curves { "Curves" };
    juce::OwnedArray<ParamFader> faders;
};

//==============================================================================
/** Clickable bar with an arrow that shows/hides a section of the window. */
class SectionHeader final : public juce::Component
{
public:
    SectionHeader (const juce::String& title, const juce::String& hint);
    void setOpen (bool shouldBeOpen);
    bool isOpen() const noexcept { return open; }
    std::function<void()> onToggle;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::String title, hint;
    bool open = false;
};

//==============================================================================
class CabinetPanel final : public juce::Component, public Refreshable, public juce::FileDragAndDropTarget
{
public:
    explicit CabinetPanel (AppController& app);
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

    std::function<void (int slot)> onBrowse;
    std::function<void (int slot)> onImport;

private:
    AppController& app;
    ParamToggle enable, dual;
    juce::TextButton irA, irB, importButton { "Import IR..." }, resetButton { "Reset" };
    ParamKnob levelA, panA, levelB, panB;
    juce::Rectangle<int> rowA, rowB;
    bool dragOver = false, shownDual = false, shownOn = true;
};

//==============================================================================
class EffectModule final : public juce::Component
{
public:
    EffectModule (AppController& app, const juce::String& title, int enableParam, std::vector<int> knobParams);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void addExtra (juce::Component* c) { extras.push_back (c); addAndMakeVisible (c); }
    /** A control shown at the right of the module's header (e.g. a type selector or a light). */
    void setHeaderExtra (juce::Component* c, int width) { headerExtra = c; headerExtraWidth = width; addAndMakeVisible (c); }
    ParamKnob* knob (int i) { return knobs[(size_t) i]; }

private:
    AppController& app;
    juce::String title;
    int enableParam;
    std::unique_ptr<ParamToggle> enable;
    juce::OwnedArray<ParamKnob> knobs;
    std::vector<juce::Component*> extras;
    juce::Component* headerExtra = nullptr;
    int headerExtraWidth = 0;
    bool shownOn = false;
};

/** "Process" light: glows brighter the more gain reduction the Smooth compressor applies. */
class ProcessLight final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ProcessLight (AppController& a) : app (a) { setTooltip ("Lights up while Smooth is cutting: brighter = more smoothing."); }
    void refresh();
    void paint (juce::Graphics&) override;

private:
    AppController& app;
    float shown = 0.0f;
};

class EffectsPanel final : public juce::Component, public Refreshable
{
public:
    explicit EffectsPanel (AppController& app);
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AppController& app;
    EffectModule gate, comp, wah, drive, studioComp, smooth, pitch, chorus, delay, reverb, output;
    ParamToggle delaySync;
    ParamChoice delayDivision, chorusMode, driveType, delayType, reverbType;
    ProcessLight smoothLight;
    juce::TextButton tapTempo { "TAP" };
    std::vector<double> taps;
    juce::Rectangle<int> flowArea;
};

//==============================================================================
class TopBar final : public juce::Component, public Refreshable
{
public:
    explicit TopBar (AppController& app);
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onPresetBrowser, onSaveAs, onSave, onAudioSettings, onMidiSettings, onMenu;
    juce::TextButton presetButton, prev { "<" }, next { ">" }, save { "Save" }, saveAs { "Save As" },
        undoButton { "Undo" }, redoButton { "Redo" }, audio { "Audio" }, midiButton { "MIDI" }, menu { "..." };

private:
    AppController& app;
    ParamToggle bypass;
    uint32_t lastMidiActivity = 0;
    int midiLedCountdown = 0;
};

class StatusBar final : public juce::Component, public Refreshable
{
public:
    explicit StatusBar (AppController& app);
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AppController& app;
    LevelMeter inMeter { "IN" }, outMeter { "OUT" };
    juce::String message;
    bool messageIsWarning = false;
    int messageAge = 0;
    float cpu = 0.0f;
    bool limiting = false, gateOpen = true;
    uint32_t lastMidi = 0;
    int midiLed = 0;
    juce::Rectangle<int> textArea;
};
} // namespace af::ui

// ZackAMP — UI controls bound to engine parameters.
//
// Each control is bound to exactly one parameter index. User edits go through the
// AppController (with undo); refresh() pulls the current value from the ParameterStore,
// so MIDI, presets and undo are reflected without any callbacks from the audio thread.
#pragma once

#include "ForgeLookAndFeel.h"
#include "../App/AppController.h"

namespace af::ui
{
/** Shared right-click menu: reset, MIDI learn / clear. */
void showParamContextMenu (AppController& app, int paramIndex, juce::Component* target);
/** A panel or effect block whose controls can all be reset at once (right-click on the block, or
    "Reset all ... controls" in any of its controls' right-click menus). */
class ResettableBlock
{
public:
    virtual ~ResettableBlock() = default;
    virtual juce::String getBlockName() const = 0;
    virtual void resetBlockControls() = 0;
};

/** Right-click on a block: puts the given controls back to their defaults (one undo step). */
void resetBlock (AppController& app, const std::vector<std::pair<int, float>>& defaults, const juce::String& blockName);
std::vector<std::pair<int, float>> defaultsFor (std::initializer_list<int> params);

class KnobSlider final : public juce::Slider
{
public:
    std::function<void()> onKeyboardGestureStart, onKeyboardGestureEnd, onContextMenu;
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool keyGesture = false, contextPress = false;
};

class ParamKnob final : public juce::Component
{
public:
    ParamKnob (AppController& app, int paramIndex, const juce::String& label = {});

    void refresh();
    void setLabel (const juce::String& l) { label = l; repaint(); }
    void setFaceplateColours (juce::Colour cap, juce::Colour pointer, juce::Colour arc, juce::Colour text,
                              juce::Colour ring = juce::Colours::transparentBlack);
    int getParamIndex() const noexcept { return index; }
    /** Hide the name and value text (the knob fills the component; the panel engraves its own). */
    void setTextVisible (bool shouldShow) { showText = shouldShow; resized(); repaint(); }
    juce::Rectangle<int> getKnobBounds() const { return slider.getBounds(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    AppController& app;
    const int index;
    juce::String label;
    juce::Colour textColour { Colours::text };
    KnobSlider slider;
    bool updating = false, showText = true;
};

class ParamToggle final : public juce::ToggleButton
{
public:
    ParamToggle (AppController& app, int paramIndex, const juce::String& text = {}, juce::Colour led = Colours::accent);
    void refresh();
    void mouseDown (const juce::MouseEvent&) override;

private:
    AppController& app;
    const int index;
};

class ParamChoice final : public juce::ComboBox
{
public:
    ParamChoice (AppController& app, int paramIndex);
    void refresh();

private:
    AppController& app;
    const int index;
    bool updating = false;
};

class ParamFader final : public juce::Component
{
public:
    ParamFader (AppController& app, int paramIndex, const juce::String& label);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    int getParamIndex() const noexcept { return index; }

private:
    AppController& app;
    const int index;
    juce::String label;
    KnobSlider slider;
    bool updating = false;
};

/** Peak meter with dB scale, peak hold and clip lamp. */
class LevelMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LevelMeter (juce::String name) : title (std::move (name)) {}
    void setLevel (float linearPeak, bool clipped);
    void paint (juce::Graphics&) override;

private:
    juce::String title;
    float displayDb = -90.0f, holdDb = -90.0f;
    int holdCounter = 0, clipCounter = 0;
};
} // namespace af::ui

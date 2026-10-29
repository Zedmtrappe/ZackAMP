// Z.AMP — "Advanced" pop-up windows: the full control set of an effect whose front-panel module
// only shows the main knobs (Studio Comp models, reverb models).
#pragma once

#include "Panels.h"
#include "VuMeter.h"

namespace af::ui
{
/** Per-model front-panel naming of the Studio Comp knobs and which ones the model uses. */
struct CompModelUi
{
    const char* amount;
    const char* level;
    bool attack, release;
    const char* description;
};
const CompModelUi& compModelUi (int model);

/** A floating window that hosts an advanced panel; closing it just hides it. */
class AdvancedWindow final : public juce::DocumentWindow
{
public:
    AdvancedWindow (const juce::String& title, juce::Component* content);
    void closeButtonPressed() override { setVisible (false); }
};

/** Header control for the Studio Comp module: model menu and the ADV button. */
class CompHeader final : public juce::Component
{
public:
    explicit CompHeader (AppController& app);
    void resized() override;
    void refresh() { type.refresh(); }
    std::function<void()> onAdvanced;

private:
    ParamChoice type;
    juce::TextButton advanced { "ADV" };
};

class StudioCompAdvanced final : public juce::Component, public ResettableBlock
{
public:
    explicit StudioCompAdvanced (AppController& app);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getBlockName() const override { return "Studio Comp"; }
    void resetBlockControls() override;
    static std::vector<int> resetParams();

private:
    void applyModel (int model);
    AppController& app;
    ParamToggle enable;
    ParamChoice type;
    VuMeter vu;
    ParamKnob amount, attack, release, tone, level, mix, schpf, emphasis;
    ParamChoice ratio, la2aMode, ratio1176, sslRatio, tc;
    ParamToggle sslAuto, overEasy;
    juce::Label description;
    int shownModel = -1;
    std::vector<juce::Component*> visibleKnobs, visibleSwitches;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> switchLabels;
};
} // namespace af::ui

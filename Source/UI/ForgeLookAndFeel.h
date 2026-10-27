// AMP FORGE — visual identity: a dark, machined-hardware look with per-family faceplates.
// All artwork is drawn in code (no third-party or manufacturer assets).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace af::ui
{
namespace Colours
{
    const juce::Colour background { 0xff0f1012 };
    const juce::Colour panel { 0xff1a1b1f };
    const juce::Colour panelLight { 0xff24262b };
    const juce::Colour panelEdge { 0xff34363d };
    const juce::Colour text { 0xffe8e2d6 };
    const juce::Colour textDim { 0xff8f8c86 };
    const juce::Colour accent { 0xffff7a1a };   // forge orange
    const juce::Colour accentDim { 0xff7a3d12 };
    const juce::Colour good { 0xff53d27a };
    const juce::Colour warn { 0xffffc23a };
    const juce::Colour bad { 0xffff4a3d };
    const juce::Colour ledOff { 0xff3a2a20 };
} // namespace Colours

/** Faceplate styling per amp family. */
struct FamilyStyle
{
    juce::Colour plateTop, plateBottom, plateText, knobCap, knobPointer, accent;
    juce::String familyName;
};
FamilyStyle getFamilyStyle (int family);

class ForgeLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ForgeLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;

    static juce::Font labelFont (float height, bool bold = true);

    /** Knob cap colours are taken from these slider colour IDs (set per faceplate). */
    enum ColourIds
    {
        knobCapColourId = 0x2201000,
        knobPointerColourId = 0x2201001,
        knobArcColourId = 0x2201002,
    };
};

/** Draws a brushed / bevelled panel background with an optional title. */
void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title = {}, bool active = true);
void drawLed (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, bool on);
} // namespace af::ui

#include "ForgeLookAndFeel.h"

namespace af::ui
{
FamilyStyle getFamilyStyle (int family)
{
    switch (family)
    {
        case 0: // American: warm ivory faceplate, black knobs, chrome-ish pointers
            return { juce::Colour (0xffe3d7bd), juce::Colour (0xffc9b994), juce::Colour (0xff2a2116),
                     juce::Colour (0xff1c1c1e), juce::Colour (0xfff2efe8), juce::Colour (0xffb5482b), "AMERICAN" };
        case 1: // British: brushed copper faceplate, dark chocolate text, cream pointer knobs
            return { juce::Colour (0xffc08a54), juce::Colour (0xff8e5d33), juce::Colour (0xff22140a),
                     juce::Colour (0xff221a14), juce::Colour (0xfff0e2c4), juce::Colour (0xff3b1d0c), "BRITISH" };
        default: // Modern: graphite with steel-blue accents
            return { juce::Colour (0xff3a3d44), juce::Colour (0xff212328), juce::Colour (0xffd9dde4),
                     juce::Colour (0xff121315), juce::Colour (0xff8fd0ff), juce::Colour (0xff5aa0d0), "MODERN" };
    }
}

ForgeLookAndFeel::ForgeLookAndFeel()
{
    using namespace Colours;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::buttonColourId, panelLight);
    setColour (juce::TextButton::buttonOnColourId, accentDim);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, text);
    setColour (juce::ComboBox::backgroundColourId, panelLight);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, panelEdge);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1d1e22));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff141518));
    setColour (juce::ListBox::outlineColourId, panelEdge);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff141518));
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::highlightColourId, accentDim);
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::ScrollBar::thumbColourId, panelEdge);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, panelEdge);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf0202125));
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::DocumentWindow::textColourId, text);
    setColour (knobCapColourId, juce::Colour (0xff1c1c1e));
    setColour (knobPointerColourId, text);
    setColour (knobArcColourId, accent);
    setColour (juce::HyperlinkButton::textColourId, accent);
    setColour (juce::GroupComponent::outlineColourId, panelEdge);
    setColour (juce::GroupComponent::textColourId, textDim);

    auto scheme = getDarkColourScheme();
    scheme.setUIColour (ColourScheme::windowBackground, background);
    scheme.setUIColour (ColourScheme::widgetBackground, panelLight);
    scheme.setUIColour (ColourScheme::menuBackground, juce::Colour (0xff1d1e22));
    scheme.setUIColour (ColourScheme::outline, panelEdge);
    scheme.setUIColour (ColourScheme::defaultText, text);
    scheme.setUIColour (ColourScheme::highlightedFill, accentDim);
    scheme.setUIColour (ColourScheme::highlightedText, text);
    setColourScheme (scheme);
}

juce::Font ForgeLookAndFeel::labelFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

void ForgeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float size = std::min (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float radius = size * 0.5f;
    const float angle = start + pos * (end - start);
    const bool enabled = s.isEnabled();

    // Scale ticks
    const auto tickCol = s.findColour (juce::Slider::textBoxTextColourId).withAlpha (enabled ? 0.55f : 0.2f);
    g.setColour (tickCol);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (float) i / 10.0f * (end - start);
        const float r0 = radius * 0.92f, r1 = radius * (i % 5 == 0 ? 1.0f : 0.97f);
        g.drawLine (centre.x + r0 * std::sin (a), centre.y - r0 * std::cos (a), centre.x + r1 * std::sin (a),
                    centre.y - r1 * std::cos (a), i % 5 == 0 ? 1.6f : 1.0f);
    }

    // Value arc
    const float arcR = radius * 0.84f;
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (enabled)
    {
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float from = bipolar ? (start + end) * 0.5f : start;
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, std::min (from, angle), std::max (from, angle), true);
        g.setColour (s.findColour (knobArcColourId));
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Knob body: knurled skirt + domed cap
    const float bodyR = radius * 0.72f;
    const auto cap = s.findColour (knobCapColourId);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (centre.x - bodyR + 1.5f, centre.y - bodyR + 3.0f, bodyR * 2.0f, bodyR * 2.0f);
    juce::ColourGradient skirt (cap.brighter (0.35f), centre.x, centre.y - bodyR, cap.darker (0.6f), centre.x, centre.y + bodyR, false);
    g.setGradientFill (skirt);
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (cap.darker (0.8f).withAlpha (0.7f));
    const int ridges = 28;
    for (int i = 0; i < ridges; ++i)
    {
        const float a = angle + juce::MathConstants<float>::twoPi * (float) i / (float) ridges;
        g.drawLine (centre.x + bodyR * 0.86f * std::sin (a), centre.y - bodyR * 0.86f * std::cos (a),
                    centre.x + bodyR * std::sin (a), centre.y - bodyR * std::cos (a), 1.0f);
    }
    const float capR = bodyR * 0.78f;
    juce::ColourGradient dome (cap.brighter (0.55f), centre.x - capR * 0.4f, centre.y - capR * 0.6f, cap.darker (0.3f),
                               centre.x + capR * 0.5f, centre.y + capR * 0.7f, true);
    g.setGradientFill (dome);
    g.fillEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f, 1.0f);

    // Pointer
    const auto pointer = s.findColour (knobPointerColourId).withAlpha (enabled ? 1.0f : 0.35f);
    g.setColour (pointer);
    juce::Path p;
    p.addRoundedRectangle (-1.6f, -capR * 0.98f, 3.2f, capR * 0.62f, 1.5f);
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (centre));

    if (s.hasKeyboardFocus (true))
    {
        g.setColour (Colours::accent.withAlpha (0.7f));
        g.drawEllipse (centre.x - radius + 1.0f, centre.y - radius + 1.0f, radius * 2.0f - 2.0f, radius * 2.0f - 2.0f, 1.2f);
    }
}

void ForgeLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                         juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float cx = r.getCentreX();
    // Slot
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (cx - 3.0f, r.getY(), 6.0f, r.getHeight(), 3.0f);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (cx - 3.0f, r.getY(), 6.0f, r.getHeight(), 3.0f, 1.0f);
    // Centre (0 dB) line and scale
    const float mid = r.getY() + r.getHeight() * 0.5f;
    g.setColour (Colours::textDim.withAlpha (0.5f));
    for (int i = 0; i <= 8; ++i)
    {
        const float ty = r.getY() + r.getHeight() * (float) i / 8.0f;
        const float len = i == 4 ? 12.0f : 6.0f;
        g.drawLine (cx - 10.0f - len, ty, cx - 10.0f, ty, i == 4 ? 1.4f : 0.8f);
    }
    // Fill from centre
    if (s.isEnabled())
    {
        g.setColour (s.findColour (knobArcColourId).withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (cx - 2.0f, std::min (mid, pos), 4.0f, std::abs (pos - mid)));
    }
    // Fader cap
    const auto cap = juce::Rectangle<float> (cx - 13.0f, pos - 7.0f, 26.0f, 14.0f);
    juce::ColourGradient grad (juce::Colour (0xff5a5d66), cap.getX(), cap.getY(), juce::Colour (0xff2a2c31), cap.getX(), cap.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (cap, 3.0f);
    g.setColour (s.isEnabled() ? Colours::text : Colours::textDim);
    g.drawLine (cap.getX() + 4.0f, pos, cap.getRight() - 4.0f, pos, 1.6f);
    if (s.hasKeyboardFocus (true))
    {
        g.setColour (Colours::accent);
        g.drawRoundedRectangle (cap.expanded (1.5f), 3.0f, 1.2f);
    }
}

void ForgeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto base = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bg;
    if (! b.isEnabled())
        base = base.withMultipliedAlpha (0.5f);
    juce::ColourGradient grad (base.brighter (hi ? 0.25f : 0.12f), r.getX(), r.getY(), base.darker (down ? 0.4f : 0.2f), r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (b.hasKeyboardFocus (false) ? Colours::accent : Colours::panelEdge.brighter (hi ? 0.3f : 0.0f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void ForgeLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hi, bool)
{
    // LED-style toggle: lamp + label.
    auto r = b.getLocalBounds().toFloat();
    const float led = std::min (14.0f, r.getHeight() - 4.0f);
    auto ledR = juce::Rectangle<float> (r.getX() + 2.0f, r.getCentreY() - led * 0.5f, led, led);
    drawLed (g, ledR, b.findColour (juce::ToggleButton::tickColourId), b.getToggleState());
    g.setColour (b.findColour (juce::ToggleButton::textColourId).withAlpha (b.isEnabled() ? (hi ? 1.0f : 0.9f) : 0.4f));
    g.setFont (labelFont (std::min (13.0f, r.getHeight() * 0.7f)));
    g.drawFittedText (b.getButtonText(), juce::Rectangle<int> ((int) (ledR.getRight() + 6.0f), 0, b.getWidth() - (int) ledR.getRight() - 6, b.getHeight()),
                      juce::Justification::centredLeft, 1);
    if (b.hasKeyboardFocus (false))
    {
        g.setColour (Colours::accent.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }
}

void ForgeLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (true) ? Colours::accent : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font ForgeLookAndFeel::getComboBoxFont (juce::ComboBox& b) { return labelFont (std::min (14.0f, (float) b.getHeight() * 0.55f), false); }

juce::Font ForgeLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return labelFont (std::min (14.0f, (float) h * 0.5f)); }

void ForgeLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    g.fillAll (findColour (juce::TooltipWindow::backgroundColourId));
    g.setColour (Colours::accent.withAlpha (0.6f));
    g.drawRect (0, 0, width, height, 1);
    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (text, labelFont (13.0f, false), Colours::text);
    juce::TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, (float) width - 16.0f);
    tl.draw (g, { 8.0f, 0.0f, (float) width - 16.0f, (float) height });
}

juce::Rectangle<int> ForgeLookAndFeel::getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> area)
{
    juce::AttributedString s;
    s.append (tip, labelFont (13.0f, false));
    juce::TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, 320.0f);
    const int w = (int) tl.getWidth() + 18, h = (int) tl.getHeight() + 12;
    return juce::Rectangle<int> (pos.x > area.getCentreX() ? pos.x - (w + 12) : pos.x + 12,
                                 pos.y > area.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h)
        .constrainedWithin (area);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, bool active)
{
    juce::ColourGradient grad (Colours::panelLight, r.getX(), r.getY(), Colours::panel, r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 8.0f, r.getRight() - 8.0f);
    if (title.isNotEmpty())
    {
        g.setColour (active ? Colours::text.withAlpha (0.85f) : Colours::textDim);
        g.setFont (ForgeLookAndFeel::labelFont (12.0f));
        g.drawText (title.toUpperCase(), r.reduced (12.0f, 6.0f).removeFromTop (16.0f), juce::Justification::centredLeft);
    }
}

void drawLed (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, bool on)
{
    if (on)
    {
        g.setColour (colour.withAlpha (0.25f));
        g.fillEllipse (r.expanded (3.0f));
    }
    juce::ColourGradient grad (on ? colour.brighter (0.6f) : Colours::ledOff.brighter (0.2f), r.getCentreX() - r.getWidth() * 0.2f,
                               r.getCentreY() - r.getHeight() * 0.25f, on ? colour.darker (0.2f) : Colours::ledOff.darker (0.5f),
                               r.getCentreX(), r.getBottom(), true);
    g.setGradientFill (grad);
    g.fillEllipse (r);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (r, 1.0f);
}
} // namespace af::ui

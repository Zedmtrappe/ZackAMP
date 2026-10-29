#include "Controls.h"

namespace af::ui
{
std::vector<std::pair<int, float>> defaultsFor (std::initializer_list<int> params)
{
    std::vector<std::pair<int, float>> d;
    for (int p : params)
        d.emplace_back (p, paramDef (p).def);
    return d;
}

void resetBlock (AppController& app, const std::vector<std::pair<int, float>>& defaults, const juce::String& blockName)
{
    app.setParamsWithUndo (defaults, "Reset " + blockName);
    app.postMessage (blockName + ": controls reset to defaults (Undo to get them back).");
}

void showParamContextMenu (AppController& app, int paramIndex, juce::Component* target)
{
    const auto& def = paramDef (paramIndex);
    juce::PopupMenu m;
    m.addSectionHeader (def.name);
    m.addItem (1, "Reset to default (" + juce::String (formatParamValue (paramIndex, def.def)) + ")");
    juce::Component::SafePointer<juce::Component> blockComp;
    if (auto* block = target->findParentComponentOfClass<ResettableBlock>())
    {
        blockComp = dynamic_cast<juce::Component*> (block);
        m.addItem (5, "Reset all " + block->getBlockName() + " controls");
    }
    m.addSeparator();
    const int cc = app.midi.findCCForParam (paramIndex);
    if (app.midi.getLearnParam() == paramIndex)
        m.addItem (4, "Cancel MIDI Learn");
    else
        m.addItem (2, "MIDI Learn (move a controller now)");
    m.addItem (3, cc >= 0 ? "Clear MIDI mapping (CC " + juce::String (cc) + ")" : juce::String ("No MIDI mapping"), cc >= 0);
    juce::Component::SafePointer<juce::Component> safe (target);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [&app, paramIndex, cc, safe, blockComp] (int r) {
        if (r == 5)
        {
            if (auto* block = dynamic_cast<ResettableBlock*> (blockComp.getComponent()))
                block->resetBlockControls();
        }
        else if (r == 1)
            app.setParamWithUndo (paramIndex, paramDef (paramIndex).def);
        else if (r == 2)
        {
            app.midi.startLearn (paramIndex);
            app.postMessage (juce::String ("MIDI Learn: move a knob, fader or footswitch to control ") + paramDef (paramIndex).name);
        }
        else if (r == 3)
        {
            app.midi.clearMapping (cc);
            app.saveMidiSettings();
        }
        else if (r == 4)
            app.midi.cancelLearn();
        if (safe != nullptr)
            safe->repaint();
    });
}

//==============================================================================
bool KnobSlider::keyPressed (const juce::KeyPress& key)
{
    const bool fine = key.getModifiers().isShiftDown();
    const double step = (getMaximum() - getMinimum()) * (fine ? 0.002 : 0.02);
    double v = getValue();
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)
        v += step;
    else if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)
        v -= step;
    else if (code == juce::KeyPress::pageUpKey)
        v += step * 5.0;
    else if (code == juce::KeyPress::pageDownKey)
        v -= step * 5.0;
    else if (code == juce::KeyPress::homeKey)
        v = getMinimum();
    else if (code == juce::KeyPress::endKey)
        v = getMaximum();
    else
        return false;
    if (! keyGesture)
    {
        keyGesture = true;
        if (onKeyboardGestureStart)
            onKeyboardGestureStart();
    }
    setValue (juce::jlimit (getMinimum(), getMaximum(), v), juce::sendNotificationSync);
    return true;
}

void KnobSlider::mouseDown (const juce::MouseEvent& e)
{
    contextPress = e.mods.isPopupMenu();
    if (contextPress)
    {
        if (onContextMenu)
            onContextMenu();
        return;
    }
    juce::Slider::mouseDown (e);
}

void KnobSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (! contextPress)
        juce::Slider::mouseDrag (e);
}

void KnobSlider::mouseUp (const juce::MouseEvent& e)
{
    if (! contextPress)
        juce::Slider::mouseUp (e);
    contextPress = false;
}

bool KnobSlider::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown && keyGesture)
    {
        keyGesture = false;
        if (onKeyboardGestureEnd)
            onKeyboardGestureEnd();
    }
    return false;
}

//==============================================================================
ParamKnob::ParamKnob (AppController& a, int paramIndex, const juce::String& l)
    : app (a), index (paramIndex), label (l.isNotEmpty() ? l : juce::String (paramDef (paramIndex).name))
{
    const auto& def = paramDef (index);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    // Choice parameters (e.g. the amp model) click into detented positions.
    slider.setRange (0.0, 1.0, def.scale == ParamScale::choice && def.numChoices > 1 ? 1.0 / (def.numChoices - 1) : 0.0);
    slider.setMouseDragSensitivity (220);
    slider.setDoubleClickReturnValue (true, plainToNormalised (index, def.def));
    slider.setWantsKeyboardFocus (true);
    slider.setTitle (def.name);
    slider.setHelpText (def.help);
    slider.setTooltip (juce::String (def.help) + "\nDouble-click: reset.  Right-click: MIDI Learn.");
    slider.textFromValueFunction = [this] (double v) { return juce::String (formatParamValue (index, normalisedToPlain (index, (float) v))); };
    slider.onDragStart = [this] { app.beginGesture (index); };
    slider.onDragEnd = [this] { app.endGesture (index); };
    slider.onKeyboardGestureStart = [this] { app.beginGesture (index); };
    slider.onKeyboardGestureEnd = [this] { app.endGesture (index); };
    slider.onContextMenu = [this] { showParamContextMenu (app, index, this); };
    slider.onValueChange = [this]
    {
        if (updating)
            return;
        // Double-click reset arrives without a drag: record it as its own undo step.
        if (! slider.isMouseButtonDown())
            app.beginGesture (index);
        app.setParam (index, normalisedToPlain (index, (float) slider.getValue()));
        if (! slider.isMouseButtonDown())
            app.endGesture (index);
        repaint();
    };
    addAndMakeVisible (slider);
    refresh();
}

void ParamKnob::setFaceplateColours (juce::Colour cap, juce::Colour pointer, juce::Colour arc, juce::Colour text, juce::Colour ring)
{
    slider.setColour (ForgeLookAndFeel::knobCapColourId, cap);
    slider.setColour (ForgeLookAndFeel::knobPointerColourId, pointer);
    slider.setColour (ForgeLookAndFeel::knobArcColourId, arc);
    slider.setColour (ForgeLookAndFeel::knobRingColourId, ring);
    slider.setColour (juce::Slider::textBoxTextColourId, text);
    textColour = text;
    repaint();
}

void ParamKnob::refresh()
{
    const float n = app.params().getNormalised (index);
    if (std::abs ((float) slider.getValue() - n) > 1.0e-5f)
    {
        const juce::ScopedValueSetter<bool> s (updating, true);
        slider.setValue (n, juce::dontSendNotification);
        repaint();
    }
}

void ParamKnob::resized()
{
    auto r = getLocalBounds();
    if (showText)
    {
        r.removeFromTop (16);
        r.removeFromBottom (16);
    }
    const int side = std::min (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (side, side));
}

void ParamKnob::paint (juce::Graphics& g)
{
    if (! showText)
        return;
    auto r = getLocalBounds();
    g.setColour (textColour.withAlpha (isEnabled() ? 0.95f : 0.4f));
    g.setFont (ForgeLookAndFeel::labelFont (12.0f));
    g.drawFittedText (label.toUpperCase(), r.removeFromTop (16), juce::Justification::centred, 1);
    const bool learning = app.midi.getLearnParam() == index;
    g.setColour (learning ? Colours::accent : textColour.withAlpha (0.7f));
    g.setFont (ForgeLookAndFeel::labelFont (11.5f, false));
    const auto value = learning ? juce::String ("MIDI LEARN...") : slider.getTextFromValue (slider.getValue());
    g.drawFittedText (value, r.removeFromBottom (16), juce::Justification::centred, 1);
}

void ParamKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showParamContextMenu (app, index, this);
}

//==============================================================================
ParamToggle::ParamToggle (AppController& a, int paramIndex, const juce::String& buttonText, juce::Colour led)
    : juce::ToggleButton (buttonText.isNotEmpty() ? buttonText : juce::String (paramDef (paramIndex).name)), app (a), index (paramIndex)
{
    setColour (juce::ToggleButton::tickColourId, led);
    setTooltip (juce::String (paramDef (index).help) + "\nRight-click: MIDI Learn.");
    setTitle (paramDef (index).name);
    setClickingTogglesState (false);
    onClick = [this] { app.setParamWithUndo (index, app.params().getBool (index) ? 0.0f : 1.0f); refresh(); };
    refresh();
}

void ParamToggle::refresh()
{
    const bool v = app.params().getBool (index);
    if (getToggleState() != v)
        setToggleState (v, juce::dontSendNotification);
}

void ParamToggle::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showParamContextMenu (app, index, this);
        return;
    }
    juce::ToggleButton::mouseDown (e);
}

//==============================================================================
ParamChoice::ParamChoice (AppController& a, int paramIndex) : app (a), index (paramIndex)
{
    const auto& def = paramDef (index);
    for (int i = 0; i < def.numChoices; ++i)
        addItem (def.choices[i], i + 1);
    setTooltip (def.help);
    setTitle (def.name);
    onChange = [this]
    {
        if (! updating && getSelectedId() > 0)
            app.setChoiceFromUser (index, getSelectedId() - 1);
    };
    refresh();
}

void ParamChoice::refresh()
{
    const int id = app.params().getInt (index) + 1;
    if (getSelectedId() != id)
    {
        const juce::ScopedValueSetter<bool> s (updating, true);
        setSelectedId (id, juce::dontSendNotification);
    }
}

//==============================================================================
ParamFader::ParamFader (AppController& a, int paramIndex, const juce::String& l) : app (a), index (paramIndex), label (l)
{
    const auto& def = paramDef (index);
    slider.setSliderStyle (juce::Slider::LinearVertical);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (def.min, def.max, 0.0);
    slider.setDoubleClickReturnValue (true, def.def);
    slider.setMouseDragSensitivity (300);
    slider.setWantsKeyboardFocus (true);
    slider.setTitle (juce::String (def.name) + " Hz band");
    slider.setTooltip (juce::String (def.help) + "\nDouble-click: 0 dB.  Right-click: MIDI Learn.");
    slider.onDragStart = [this] { app.beginGesture (index); };
    slider.onDragEnd = [this] { app.endGesture (index); };
    slider.onKeyboardGestureStart = [this] { app.beginGesture (index); };
    slider.onKeyboardGestureEnd = [this] { app.endGesture (index); };
    slider.onContextMenu = [this] { showParamContextMenu (app, index, this); };
    slider.onValueChange = [this]
    {
        if (updating)
            return;
        if (! slider.isMouseButtonDown())
            app.beginGesture (index);
        app.setParam (index, (float) slider.getValue());
        if (! slider.isMouseButtonDown())
            app.endGesture (index);
        repaint();
    };
    addAndMakeVisible (slider);
    refresh();
}

void ParamFader::refresh()
{
    const float v = app.params().get (index);
    if (std::abs ((float) slider.getValue() - v) > 1.0e-4f)
    {
        const juce::ScopedValueSetter<bool> s (updating, true);
        slider.setValue (v, juce::dontSendNotification);
        repaint();
    }
}

void ParamFader::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (34);
    r.removeFromTop (4);
    slider.setBounds (r.withSizeKeepingCentre (std::min (r.getWidth(), 44), r.getHeight()));
}

void ParamFader::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().removeFromBottom (34);
    g.setFont (ForgeLookAndFeel::labelFont (12.5f));
    g.setColour (isEnabled() ? Colours::text : Colours::textDim);
    g.drawFittedText (label, r.removeFromTop (16), juce::Justification::centred, 1);
    g.setFont (ForgeLookAndFeel::labelFont (11.0f, false));
    g.setColour (app.midi.getLearnParam() == index ? Colours::accent : Colours::textDim);
    const float v = app.params().get (index);
    g.drawFittedText (app.midi.getLearnParam() == index ? juce::String ("LEARN") : juce::String (v, 1) + " dB", r, juce::Justification::centred, 1);
}

void ParamFader::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showParamContextMenu (app, index, this);
}

//==============================================================================
void LevelMeter::setLevel (float peak, bool clipped)
{
    const float db = juce::Decibels::gainToDecibels (peak, -90.0f);
    displayDb = db > displayDb ? db : std::max (db, displayDb - 1.5f); // ~30 dB/s fall at 20 Hz refresh
    if (db >= holdDb)
    {
        holdDb = db;
        holdCounter = 30;
    }
    else if (--holdCounter <= 0)
        holdDb = std::max (-90.0f, holdDb - 2.0f);
    if (clipped || peak >= 0.999f)
        clipCounter = 40;
    else if (clipCounter > 0)
        --clipCounter;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::textDim);
    g.setFont (ForgeLookAndFeel::labelFont (11.0f));
    g.drawText (title, r.removeFromLeft (28.0f), juce::Justification::centredLeft);
    auto clip = r.removeFromRight (10.0f).withSizeKeepingCentre (8.0f, 8.0f);
    drawLed (g, clip, Colours::bad, clipCounter > 0);
    r.removeFromRight (4.0f);
    auto bar = r.withSizeKeepingCentre (r.getWidth(), std::min (10.0f, r.getHeight()));
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (bar, 2.0f);
    auto toX = [&] (float db) { return bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    const float x = toX (displayDb);
    juce::ColourGradient grad (Colours::good, bar.getX(), 0, Colours::bad, bar.getRight(), 0, false);
    grad.addColour (0.8, Colours::warn); // -12 dB
    g.setGradientFill (grad);
    g.fillRect (bar.withRight (x));
    g.setColour (Colours::text);
    g.fillRect (juce::Rectangle<float> (toX (holdDb) - 1.0f, bar.getY(), 2.0f, bar.getHeight()));
    g.setColour (Colours::textDim.withAlpha (0.5f));
    for (float db : { -48.0f, -36.0f, -24.0f, -12.0f, -6.0f })
        g.drawVerticalLine ((int) toX (db), bar.getY(), bar.getBottom());
}
} // namespace af::ui

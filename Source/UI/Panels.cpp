#include "Panels.h"
#include "../DSP/GraphicEQ/GraphicEQ.h"

namespace af::ui
{
using dsp::getAmpVoicing;

//==============================================================================
AmpPanel::AmpPanel (AppController& a) : app (a)
{
    for (int i = 0; i < dsp::kNumAmpModels; ++i)
    {
        auto* b = modelButtons.add (new juce::TextButton (getAmpVoicing (i).name));
        b->setClickingTogglesState (false);
        b->setTooltip (getAmpVoicing (i).shortDescription);
        b->onClick = [this, i] { app.selectAmpModel (i); };
        addAndMakeVisible (b);
    }
    gain = std::make_unique<ParamKnob> (app, pAmpGain);
    bass = std::make_unique<ParamKnob> (app, pAmpBass);
    mid = std::make_unique<ParamKnob> (app, pAmpMid);
    treble = std::make_unique<ParamKnob> (app, pAmpTreble);
    presence = std::make_unique<ParamKnob> (app, pAmpPresence);
    depth = std::make_unique<ParamKnob> (app, pAmpDepth);
    master = std::make_unique<ParamKnob> (app, pAmpMaster);
    bright = std::make_unique<ParamToggle> (app, pAmpBright, "BRIGHT");
    for (auto* c : std::initializer_list<juce::Component*> { gain.get(), bass.get(), mid.get(), treble.get(), presence.get(),
                                                             depth.get(), master.get(), bright.get() })
        addChildComponent (c);
    refresh();
}

void AmpPanel::rebuildForModel (int model)
{
    shownModel = model;
    const auto& v = getAmpVoicing (model);
    const int family = (int) v.family;
    const auto style = getFamilyStyle (family);

    std::vector<ParamKnob*> order;
    using namespace dsp;
    auto has = [&] (uint32_t bit) { return (v.controls & bit) != 0; };
    if (family == 0)
        order = { gain.get(), treble.get(), mid.get(), bass.get(), presence.get(), master.get() };
    else if (family == 1)
        order = { gain.get(), bass.get(), mid.get(), treble.get(), presence.get(), master.get() };
    else
        order = { gain.get(), treble.get(), bass.get(), mid.get(), presence.get(), depth.get(), master.get() };

    visibleKnobs.clear();
    for (auto* k : { gain.get(), bass.get(), mid.get(), treble.get(), presence.get(), depth.get(), master.get() })
        k->setVisible (false);
    for (auto* k : order)
    {
        const int p = k->getParamIndex();
        const bool show = (p == pAmpGain && has (ctlGain)) || (p == pAmpBass && has (ctlBass)) || (p == pAmpMid && has (ctlMid))
                          || (p == pAmpTreble && has (ctlTreble)) || (p == pAmpPresence && has (ctlPresence))
                          || (p == pAmpDepth && has (ctlDepth)) || (p == pAmpMaster && has (ctlMaster));
        if (! show)
            continue;
        k->setFaceplateColours (style.knobCap, style.knobPointer, style.accent.brighter (0.4f), style.plateText);
        k->setVisible (true);
        visibleKnobs.push_back (k);
    }
    gain->setLabel (v.gainLabel);
    depth->setLabel (family == 2 && model == 4 ? "Resonance" : "Depth");
    bright->setVisible (v.bright.available);
    bright->setColour (juce::ToggleButton::textColourId, style.plateText);
    bright->setColour (juce::ToggleButton::tickColourId, Colours::accent);
    resized();
    repaint();
}

void AmpPanel::refresh()
{
    const int model = app.params().getInt (pAmpModel);
    if (model != shownModel)
        rebuildForModel (model);
    for (int i = 0; i < modelButtons.size(); ++i)
        modelButtons[i]->setToggleState (i == model, juce::dontSendNotification);
    for (auto* k : { gain.get(), bass.get(), mid.get(), treble.get(), presence.get(), depth.get(), master.get() })
        k->refresh();
    bright->refresh();
}

void AmpPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    selectorArea = r.removeFromTop (54);
    // Model selector: three family groups of two buttons.
    auto sel = selectorArea.withTrimmedTop (18);
    const int groupW = sel.getWidth() / 3;
    for (int f = 0; f < 3; ++f)
    {
        auto grp = sel.removeFromLeft (groupW).reduced (4, 0);
        const int bw = grp.getWidth() / 2;
        modelButtons[f * 2]->setBounds (grp.removeFromLeft (bw).reduced (2, 0));
        modelButtons[f * 2 + 1]->setBounds (grp.reduced (2, 0));
    }
    r.removeFromTop (8);
    plateArea = r;

    auto knobs = plateArea.reduced (24, 14);
    knobs.removeFromTop (30); // engraved title
    const bool hasBright = bright->isVisible();
    auto brightArea = hasBright ? knobs.removeFromRight (92) : juce::Rectangle<int>();
    const int n = (int) visibleKnobs.size();
    if (n > 0)
    {
        const int w = std::min (knobs.getWidth() / n, 130);
        auto row = knobs.withSizeKeepingCentre (w * n, std::min (knobs.getHeight(), 150));
        for (auto* k : visibleKnobs)
            k->setBounds (row.removeFromLeft (w).reduced (4, 0));
    }
    if (hasBright)
        bright->setBounds (brightArea.withSizeKeepingCentre (88, 28));
}

void AmpPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    const int model = shownModel < 0 ? 0 : shownModel;
    const auto& v = getAmpVoicing (model);
    const auto style = getFamilyStyle ((int) v.family);

    // Family labels over the selector
    auto sel = selectorArea;
    const int groupW = sel.getWidth() / 3;
    g.setFont (ForgeLookAndFeel::labelFont (11.0f));
    for (int f = 0; f < 3; ++f)
    {
        auto grp = sel.removeFromLeft (groupW).reduced (6, 0).removeFromTop (16);
        g.setColour ((int) v.family == f ? Colours::accent : Colours::textDim);
        g.drawText (getFamilyStyle (f).familyName, grp, juce::Justification::centredLeft);
    }

    // Faceplate
    auto plate = plateArea.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (plate.translated (0, 3), 10.0f);
    juce::ColourGradient grad (style.plateTop, plate.getX(), plate.getY(), style.plateBottom, plate.getX(), plate.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (plate, 10.0f);
    // Brushed texture
    juce::Random rng (model + 7);
    for (int y = (int) plate.getY() + 2; y < (int) plate.getBottom() - 2; y += 2)
    {
        g.setColour (juce::Colours::white.withAlpha (0.015f + 0.02f * rng.nextFloat()));
        g.drawHorizontalLine (y, plate.getX() + 6.0f, plate.getRight() - 6.0f);
    }
    g.setColour (style.plateText.withAlpha (0.35f));
    g.drawRoundedRectangle (plate.reduced (5.0f), 7.0f, 1.0f);
    // Corner screws
    for (auto pt : { plate.getTopLeft() + juce::Point<float> (14, 14), plate.getTopRight() + juce::Point<float> (-14, 14),
                     plate.getBottomLeft() + juce::Point<float> (14, -14), plate.getBottomRight() + juce::Point<float> (-14, -14) })
    {
        g.setColour (style.plateBottom.darker (0.5f));
        g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre (pt));
        g.setColour (style.plateTop.brighter (0.3f));
        g.drawLine (pt.x - 2.5f, pt.y - 2.5f, pt.x + 2.5f, pt.y + 2.5f, 1.0f);
    }
    // Engraved model name + pilot lamp
    auto title = plate.reduced (26.0f, 12.0f).removeFromTop (28.0f);
    g.setColour (style.plateText);
    g.setFont (ForgeLookAndFeel::labelFont (20.0f));
    g.drawText (juce::String (v.name).toUpperCase(), title, juce::Justification::centredLeft);
    g.setFont (ForgeLookAndFeel::labelFont (12.0f, false));
    g.setColour (style.plateText.withAlpha (0.7f));
    g.drawText (v.shortDescription, title.withTrimmedLeft (260.0f).withTrimmedRight (40.0f), juce::Justification::centredRight);
    const bool live = ! app.params().getBool (pBypass);
    drawLed (g, juce::Rectangle<float> (14, 14).withCentre ({ title.getRight() - 8.0f, title.getCentreY() }), juce::Colour (0xffff3b2f), live);
}

//==============================================================================
void EQResponse::update()
{
    std::array<float, 5> v {};
    for (int i = 0; i < 5; ++i)
        v[(size_t) i] = app.params().get (pEq80 + i);
    const bool on = app.params().getBool (pEqOn);
    if (v != last || on != lastOn)
    {
        last = v;
        lastOn = on;
        repaint();
    }
}

void EQResponse::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff111215));
    g.fillRoundedRectangle (r, 6.0f);
    const auto plot = r.reduced (8.0f, 6.0f);
    auto xOf = [&] (double f) { return plot.getX() + plot.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); };
    auto yOf = [&] (double db) { return plot.getCentreY() - plot.getHeight() * 0.5f * (float) (db / 15.0); };

    g.setFont (ForgeLookAndFeel::labelFont (10.0f, false));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (Colours::panelEdge.withAlpha (0.6f));
        g.drawVerticalLine ((int) xOf (f), plot.getY(), plot.getBottom());
        g.setColour (Colours::textDim.withAlpha (0.7f));
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) xOf (f) + 2, (int) plot.getBottom() - 12, 30, 12, juce::Justification::left);
    }
    for (double db : { -12.0, -6.0, 0.0, 6.0, 12.0 })
    {
        g.setColour (Colours::panelEdge.withAlpha (db == 0.0 ? 1.0f : 0.5f));
        g.drawHorizontalLine ((int) yOf (db), plot.getX(), plot.getRight());
    }

    // The curve is computed from the same designer the audio engine uses.
    dsp::GraphicEQDesigner designer (48000.0);
    std::array<dsp::BiquadCoeffs, 5> c;
    designer.design (last, c);
    juce::Path p;
    for (int i = 0; i <= 200; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, i / 200.0);
        const float y = yOf (juce::jlimit (-15.0, 15.0, dsp::GraphicEQDesigner::responseDb (c, f, 48000.0)));
        if (i == 0)
            p.startNewSubPath (xOf (f), y);
        else
            p.lineTo (xOf (f), y);
    }
    const auto col = lastOn ? Colours::accent : Colours::textDim.withAlpha (0.5f);
    juce::Path fill (p);
    fill.lineTo (plot.getRight(), yOf (0));
    fill.lineTo (plot.getX(), yOf (0));
    fill.closeSubPath();
    g.setColour (col.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (col);
    if (lastOn)
        g.strokePath (p, juce::PathStrokeType (2.0f));
    else
    {
        juce::Path dashed;
        const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dashes, 2);
        g.fillPath (dashed);
        g.setColour (Colours::textDim);
        g.setFont (ForgeLookAndFeel::labelFont (11.0f));
        g.drawText ("EQ BYPASSED", plot.reduced (6.0f), juce::Justification::topRight);
    }
    for (size_t i = 0; i < 5; ++i)
    {
        const double f = dsp::GraphicEQDesigner::kCentres[i];
        g.setColour (col);
        g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ xOf (f), yOf (last[i]) }));
    }
}

EQPanel::EQPanel (AppController& a)
    : app (a), enable (a, pEqOn, "GRAPHIC EQ"), position (a, pEqPosition), response (a)
{
    const char* labels[] = { "80", "240", "750", "2.2k", "6.6k" };
    for (int i = 0; i < 5; ++i)
        addAndMakeVisible (faders.add (new ParamFader (app, pEq80 + i, labels[i])));
    addAndMakeVisible (enable);
    addAndMakeVisible (position);
    addAndMakeVisible (flat);
    addAndMakeVisible (curves);
    addAndMakeVisible (response);
    flat.setTooltip ("Reset all five bands to 0 dB.");
    curves.setTooltip ("Factory EQ curves.");
    flat.onClick = [this]
    {
        std::vector<std::pair<int, float>> changes;
        for (int i = 0; i < 5; ++i)
            changes.push_back ({ pEq80 + i, 0.0f });
        app.setParamsWithUndo (changes, "EQ flat");
    };
    curves.onClick = [this]
    {
        struct Curve { const char* name; std::array<float, 5> db; };
        static const Curve list[] = {
            { "Flat", { 0, 0, 0, 0, 0 } },
            { "Classic V (scooped rhythm)", { 3, 0, -4, 1, 3 } },
            { "Deep V (heavy)", { 4, 1, -6, 1, 4 } },
            { "Mid Push (lead)", { 0, -1, 3, 2, -2 } },
            { "Tight Low End", { -3, -1, 1, 1, 0 } },
            { "Bright Clean", { -1, -2, 0, 2, 4 } },
            { "Smooth Top", { 0, 0, 1, -1, -4 } },
        };
        juce::PopupMenu m;
        for (int i = 0; i < (int) std::size (list); ++i)
            m.addItem (i + 1, list[i].name);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&curves), [this] (int r)
        {
            if (r <= 0)
                return;
            std::vector<std::pair<int, float>> changes { { pEqOn, 1.0f } };
            for (int i = 0; i < 5; ++i)
                changes.push_back ({ pEq80 + i, list[r - 1].db[(size_t) i] });
            app.setParamsWithUndo (changes, "EQ curve");
        });
    };
}

void EQPanel::refresh()
{
    enable.refresh();
    position.refresh();
    const bool on = app.params().getBool (pEqOn);
    for (auto* f : faders)
    {
        f->refresh();
        f->setAlpha (on ? 1.0f : 0.55f);
    }
    response.update();
}

void EQPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto header = r.removeFromTop (26);
    enable.setBounds (header.removeFromLeft (140));
    curves.setBounds (header.removeFromRight (70).reduced (0, 1));
    header.removeFromRight (6);
    flat.setBounds (header.removeFromRight (52).reduced (0, 1));
    header.removeFromRight (6);
    position.setBounds (header.removeFromRight (140).reduced (0, 1));
    r.removeFromTop (8);
    response.setBounds (r.removeFromLeft (r.getWidth() * 45 / 100));
    r.removeFromLeft (10);
    const int w = r.getWidth() / 5;
    for (auto* f : faders)
        f->setBounds (r.removeFromLeft (w));
}

void EQPanel::paint (juce::Graphics& g) { drawPanel (g, getLocalBounds().toFloat()); }

//==============================================================================
CabinetPanel::CabinetPanel (AppController& a)
    : app (a), enable (a, pCabOn, "CABINET"), dual (a, pCabDual, "DUAL CAB"),
      levelA (a, pCabALevel, "Level A"), panA (a, pCabAPan, "Pan A"), levelB (a, pCabBLevel, "Level B"), panB (a, pCabBPan, "Pan B")
{
    for (auto* c : std::initializer_list<juce::Component*> { &enable, &dual, &irA, &irB, &importButton, &resetButton, &levelA, &panA, &levelB, &panB })
        addAndMakeVisible (c);
    irA.setTooltip ("Cabinet A impulse response. Click to browse and audition IRs.");
    irB.setTooltip ("Cabinet B impulse response (dual mode). Click to browse.");
    importButton.setTooltip ("Import WAV/AIFF impulse responses. You can also drag files onto this panel.");
    resetButton.setTooltip ("Reset the cabinet to the amp model's default IR and levels.");
    irA.onClick = [this] { if (onBrowse) onBrowse (0); };
    irB.onClick = [this] { if (onBrowse) onBrowse (1); };
    importButton.onClick = [this] { if (onImport) onImport (0); };
    resetButton.onClick = [this] { app.resetCabinet(); };
    refresh();
}

void CabinetPanel::refresh()
{
    enable.refresh();
    dual.refresh();
    for (auto* k : { &levelA, &panA, &levelB, &panB })
        k->refresh();
    const bool isDual = app.params().getBool (pCabDual);
    const bool on = app.params().getBool (pCabOn);
    irB.setEnabled (isDual && on);
    levelB.setEnabled (isDual && on);
    panB.setEnabled (isDual && on);
    panA.setEnabled (isDual && on);
    irA.setEnabled (on);
    levelA.setEnabled (on);
    auto nameFor = [this] (int slot)
    {
        const auto info = app.engine.getCabinetInfo (slot);
        if (info.irId.empty())
            return juce::String (slot == 0 ? "No IR loaded" : "Choose IR for B...");
        return juce::String (info.displayName) + (info.truncated ? "  (truncated)" : "");
    };
    const auto a = nameFor (0), b = nameFor (1);
    if (irA.getButtonText() != a) irA.setButtonText (a);
    if (irB.getButtonText() != b) irB.setButtonText (b);
    if (isDual != shownDual || on != shownOn)
    {
        shownDual = isDual;
        shownOn = on;
        repaint();
    }
}

void CabinetPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto header = r.removeFromTop (26);
    enable.setBounds (header.removeFromLeft (110));
    dual.setBounds (header.removeFromLeft (110));
    resetButton.setBounds (header.removeFromRight (60).reduced (0, 1));
    header.removeFromRight (6);
    importButton.setBounds (header.removeFromRight (104).reduced (0, 1));
    r.removeFromTop (8);
    const int rowH = r.getHeight() / 2;
    rowA = r.removeFromTop (rowH);
    rowB = r;
    auto layoutRow = [] (juce::Rectangle<int> row, juce::TextButton& ir, ParamKnob& level, ParamKnob& pan)
    {
        row.removeFromLeft (22);
        const int knobW = std::min (78, row.getHeight() + 6);
        pan.setBounds (row.removeFromRight (knobW));
        level.setBounds (row.removeFromRight (knobW));
        ir.setBounds (row.reduced (4, 0).withSizeKeepingCentre (row.getWidth() - 8, std::min (30, row.getHeight())));
    };
    layoutRow (rowA, irA, levelA, panA);
    layoutRow (rowB, irB, levelB, panB);
}

void CabinetPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    g.setFont (ForgeLookAndFeel::labelFont (16.0f));
    g.setColour (Colours::accent);
    g.drawText ("A", rowA.withWidth (20), juce::Justification::centred);
    g.setColour (app.params().getBool (pCabDual) ? Colours::accent : Colours::textDim);
    g.drawText ("B", rowB.withWidth (20), juce::Justification::centred);
    if (! app.params().getBool (pCabOn))
    {
        auto banner = getLocalBounds().reduced (40, 0).withSizeKeepingCentre (getWidth() - 80, 30).toFloat();
        g.setColour (Colours::warn.withAlpha (0.15f));
        g.fillRoundedRectangle (banner, 5.0f);
        g.setColour (Colours::warn);
        g.setFont (ForgeLookAndFeel::labelFont (12.5f));
        g.drawText ("CAB BYPASSED - for a real cabinet or an external cab simulator", banner, juce::Justification::centred);
    }
    if (dragOver)
    {
        g.setColour (Colours::accent.withAlpha (0.2f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 8.0f);
        g.setColour (Colours::accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 8.0f, 2.0f);
    }
}

bool CabinetPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff"))
            return true;
    return false;
}

void CabinetPanel::filesDropped (const juce::StringArray& files, int, int y)
{
    dragOver = false;
    repaint();
    const int slot = (y > rowB.getY() && app.params().getBool (pCabDual)) ? 1 : 0;
    juce::Array<juce::File> list;
    for (const auto& f : files)
        if (isInterestedInFileDrag ({ f }))
            list.add (juce::File (f));
    auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel (
        juce::MessageBoxIconType::QuestionIcon, "Import impulse response",
        "Copy into the AMP FORGE IR library (recommended: presets keep working if you move the original), "
        "or reference the file where it is?",
        "Copy", "Reference", "Cancel", this);
    juce::Component::SafePointer<CabinetPanel> safe (this);
    juce::AlertWindow::showAsync (options, [safe, list, slot] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        juce::String lastId;
        for (const auto& f : list)
        {
            auto r = safe->app.importIR (f, result == 1);
            if (r.ok)
                lastId = r.entry.id;
            else
                safe->app.postMessage (f.getFileName() + ": " + r.error, true);
        }
        if (lastId.isNotEmpty())
            safe->app.setCabinetIR (slot, lastId);
    });
}

//==============================================================================
EffectModule::EffectModule (AppController& a, const juce::String& t, int enableP, std::vector<int> knobParams)
    : app (a), title (t), enableParam (enableP)
{
    if (enableParam >= 0)
    {
        enable = std::make_unique<ParamToggle> (app, enableParam, t.toUpperCase());
        addAndMakeVisible (*enable);
    }
    for (int p : knobParams)
        addAndMakeVisible (knobs.add (new ParamKnob (app, p)));
}

void EffectModule::refresh()
{
    if (enable != nullptr)
        enable->refresh();
    for (auto* k : knobs)
        k->refresh();
    const bool on = enableParam < 0 || app.params().getBool (enableParam);
    for (auto* k : knobs)
        k->setAlpha (on ? 1.0f : 0.5f);
    if (on != shownOn)
    {
        shownOn = on;
        repaint();
    }
}

void EffectModule::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto header = r.removeFromTop (24);
    if (enable != nullptr)
        enable->setBounds (header);
    if (! extras.empty())
    {
        auto ex = r.removeFromBottom (26);
        const int w = ex.getWidth() / (int) extras.size();
        for (auto* c : extras)
            c->setBounds (ex.removeFromLeft (w).reduced (2, 1));
    }
    const int n = knobs.size();
    if (n == 0)
        return;
    const int cols = n <= 3 ? n : 2;
    const int rows = (n + cols - 1) / cols;
    const int w = r.getWidth() / cols, h = r.getHeight() / rows;
    for (int i = 0; i < n; ++i)
        knobs[i]->setBounds (r.getX() + (i % cols) * w, r.getY() + (i / cols) * h, w, h);
}

void EffectModule::paint (juce::Graphics& g)
{
    const bool on = enableParam < 0 || app.params().getBool (enableParam);
    drawPanel (g, getLocalBounds().toFloat(), enableParam < 0 ? title : juce::String(), on);
    if (on && enableParam >= 0)
    {
        g.setColour (Colours::accent.withAlpha (0.5f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.2f);
    }
}

EffectsPanel::EffectsPanel (AppController& a)
    : app (a),
      gate (a, "Gate", pGateOn, { pGateThreshold, pGateRelease }),
      drive (a, "Drive", pDriveOn, { pDriveDrive, pDriveTone, pDriveLevel }),
      chorus (a, "Chorus", pChorusOn, { pChorusRate, pChorusDepth, pChorusMix }),
      delay (a, "Delay", pDelayOn, { pDelayTime, pDelayFeedback, pDelayMix, pDelayTone }),
      reverb (a, "Reverb", pReverbOn, { pReverbMix, pReverbDecay, pReverbTone }),
      output (a, "Global", -1, { pOutLevel, pInTrim, pTempo }),
      delaySync (a, pDelaySync, "SYNC"), delayDivision (a, pDelayDivision)
{
    for (auto* m : { &gate, &drive, &chorus, &delay, &reverb, &output })
        addAndMakeVisible (m);
    delay.addExtra (&delaySync);
    delay.addExtra (&delayDivision);
    delay.addExtra (&tapTempo);
    tapTempo.setTooltip ("Tap tempo: tap at least twice in time. Sets the tempo used by synced delay.");
    tapTempo.onClick = [this]
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (! taps.empty() && now - taps.back() > 2000.0)
            taps.clear(); // a long pause starts a new measurement
        taps.push_back (now);
        if (taps.size() > 5)
            taps.erase (taps.begin());
        if (taps.size() >= 2)
        {
            const double avgMs = (taps.back() - taps.front()) / (double) (taps.size() - 1);
            app.setParamWithUndo (pTempo, (float) (60000.0 / avgMs));
            if (! app.params().getBool (pDelaySync))
                app.setParamWithUndo (pDelaySync, 1.0f);
        }
    };
}

void EffectsPanel::refresh()
{
    for (auto* m : { &gate, &drive, &chorus, &delay, &reverb, &output })
        m->refresh();
    delaySync.refresh();
    delayDivision.refresh();
    const bool sync = app.params().getBool (pDelaySync);
    delay.knob (0)->setEnabled (! sync);
    delayDivision.setEnabled (sync);
    const auto tapText = juce::String (juce::roundToInt (app.params().get (pTempo))) + " BPM";
    if (tapTempo.getButtonText() != tapText)
        tapTempo.setButtonText (tapText);
    repaint (flowArea);
}

void EffectsPanel::resized()
{
    auto r = getLocalBounds();
    flowArea = r.removeFromTop (22);
    r.removeFromTop (4);
    // Width weights by module complexity.
    const float weights[] = { 1.0f, 1.3f, 1.3f, 1.55f, 1.3f, 1.3f };
    float total = 0;
    for (float w : weights) total += w;
    const int gap = 8;
    const float unit = (float) (r.getWidth() - gap * 5) / total;
    int i = 0;
    for (auto* m : { &gate, &drive, &chorus, &delay, &reverb, &output })
    {
        m->setBounds (r.removeFromLeft ((int) (unit * weights[i++])));
        r.removeFromLeft (gap);
    }
}

void EffectsPanel::paint (juce::Graphics& g)
{
    // Signal-flow strip: the fixed processing order, lit where a stage is active.
    struct Stage { const char* name; int param; };
    const Stage stages[] = { { "IN", -1 }, { "GATE", pGateOn }, { "DRIVE", pDriveOn }, { "AMP", -1 },
                             { "EQ", pEqOn }, { "CAB", pCabOn }, { "CHORUS", pChorusOn }, { "DELAY", pDelayOn },
                             { "REVERB", pReverbOn }, { "OUT", -1 } };
    auto r = flowArea.toFloat().reduced (2.0f, 2.0f);
    const int n = (int) std::size (stages);
    const float w = r.getWidth() / (float) n;
    g.setFont (ForgeLookAndFeel::labelFont (10.5f));
    const bool bypassed = app.params().getBool (pBypass);
    for (int i = 0; i < n; ++i)
    {
        auto cell = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (10.0f, 0.0f);
        const bool active = ! bypassed && (stages[i].param < 0 || app.params().getBool (stages[i].param));
        g.setColour (active ? Colours::accent.withAlpha (0.22f) : Colours::panel);
        g.fillRoundedRectangle (cell, 4.0f);
        g.setColour (active ? Colours::text : Colours::textDim);
        juce::String label = stages[i].name;
        if (i == 4 && app.params().getBool (pEqOn))
            label << (app.params().getInt (pEqPosition) == 0 ? " (pre-PA)" : " (post)");
        g.drawFittedText (label, cell.toNearestInt(), juce::Justification::centred, 1);
        if (i < n - 1)
        {
            g.setColour (Colours::textDim);
            const float x = cell.getRight() + 3.0f, y = cell.getCentreY();
            g.drawArrow (juce::Line<float> (x, y, x + 12.0f, y), 1.0f, 5.0f, 5.0f);
        }
    }
}

//==============================================================================
TopBar::TopBar (AppController& a) : app (a), bypass (a, pBypass, "BYPASS", Colours::warn)
{
    for (auto* b : { &presetButton, &prev, &next, &save, &saveAs, &undoButton, &redoButton, &audio, &midiButton, &menu })
        addAndMakeVisible (b);
    addAndMakeVisible (bypass);
    presetButton.setTooltip ("Browse presets (search, filter, rename, delete).");
    prev.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    save.setTooltip ("Save changes to the current user preset");
    saveAs.setTooltip ("Save as a new preset");
    audio.setTooltip ("Audio interface, input channel, sample rate and buffer size");
    midiButton.setTooltip ("MIDI controllers, Program Change and CC mappings");
    menu.setTooltip ("More options");
    prev.onClick = [this] { app.stepPreset (-1); };
    next.onClick = [this] { app.stepPreset (1); };
    undoButton.onClick = [this] { app.undo.undo(); };
    redoButton.onClick = [this] { app.undo.redo(); };
    presetButton.onClick = [this] { if (onPresetBrowser) onPresetBrowser(); };
    save.onClick = [this] { if (onSave) onSave(); };
    saveAs.onClick = [this] { if (onSaveAs) onSaveAs(); };
    audio.onClick = [this] { if (onAudioSettings) onAudioSettings(); };
    midiButton.onClick = [this] { if (onMidiSettings) onMidiSettings(); };
    menu.onClick = [this] { if (onMenu) onMenu(); };
}

void TopBar::refresh()
{
    auto name = app.getCurrentPresetName();
    if (app.isModified())
        name << "  *";
    if (presetButton.getButtonText() != name)
        presetButton.setButtonText (name);
    undoButton.setEnabled (app.undo.canUndo());
    redoButton.setEnabled (app.undo.canRedo());
    const int cur = app.getCurrentPresetIndex();
    save.setEnabled (cur >= 0 && ! app.presets.getPresets()[(size_t) cur].isFactory && app.isModified());
    bypass.refresh();
    const auto act = app.midi.getActivityCounter();
    if (act != lastMidiActivity)
    {
        lastMidiActivity = act;
        midiLedCountdown = 4;
    }
    else if (midiLedCountdown > 0)
        --midiLedCountdown;
    repaint (midiButton.getBounds().expanded (16, 0));
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    r.removeFromLeft (190); // wordmark
    menu.setBounds (r.removeFromRight (40));
    r.removeFromRight (6);
    audio.setBounds (r.removeFromRight (70));
    r.removeFromRight (6);
    midiButton.setBounds (r.removeFromRight (70).withTrimmedLeft (14));
    r.removeFromRight (10);
    bypass.setBounds (r.removeFromRight (96));
    r.removeFromRight (10);
    redoButton.setBounds (r.removeFromRight (56));
    r.removeFromRight (4);
    undoButton.setBounds (r.removeFromRight (56));
    r.removeFromRight (14);
    saveAs.setBounds (r.removeFromRight (74));
    r.removeFromRight (4);
    save.setBounds (r.removeFromRight (56));
    r.removeFromRight (14);
    prev.setBounds (r.removeFromLeft (32));
    r.removeFromLeft (4);
    next.setBounds (r.removeFromRight (32));
    r.removeFromRight (4);
    presetButton.setBounds (r);
}

void TopBar::paint (juce::Graphics& g)
{
    juce::ColourGradient grad (juce::Colour (0xff1d1e22), 0, 0, juce::Colour (0xff131417), 0, (float) getHeight(), false);
    g.setGradientFill (grad);
    g.fillRect (getLocalBounds());
    g.setColour (Colours::panelEdge);
    g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());
    // Wordmark
    auto r = getLocalBounds().reduced (14, 0).removeFromLeft (190).toFloat();
    g.setColour (Colours::accent);
    g.fillEllipse (juce::Rectangle<float> (16, 16).withCentre ({ r.getX() + 8.0f, r.getCentreY() }));
    g.setColour (juce::Colour (0xff131417));
    g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre ({ r.getX() + 8.0f, r.getCentreY() }));
    g.setColour (Colours::text);
    g.setFont (ForgeLookAndFeel::labelFont (21.0f));
    g.drawText (AMPFORGE_PRODUCT_NAME, r.withTrimmedLeft (24.0f), juce::Justification::centredLeft);
    // MIDI activity lamp next to the MIDI button
    auto led = juce::Rectangle<float> (8, 8).withCentre ({ (float) midiButton.getX() - 2.0f, (float) midiButton.getBounds().getCentreY() });
    drawLed (g, led, Colours::good, midiLedCountdown > 0);
}

//==============================================================================
StatusBar::StatusBar (AppController& a) : app (a)
{
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    inMeter.setTooltip ("Interface input level (before Input Trim). Strong picking should peak around -12 dBFS; the red lamp means the converter clipped - lower the interface gain.");
    outMeter.setTooltip ("Output level after the safety limiter.");
}

void StatusBar::refresh()
{
    const auto m = app.engine.readMeters();
    inMeter.setLevel (m.inputPeak, m.inputClipped);
    outMeter.setLevel (std::max (m.outputPeakL, m.outputPeakR), false);
    limiting = m.limiterReduction > 0.06f; // ~0.5 dB
    gateOpen = m.gateOpen;
    cpu = (float) app.deviceManager.getCpuUsage() * 100.0f;
    for (const auto& msg : app.takeMessages())
    {
        message = msg.text;
        messageIsWarning = msg.isWarning;
        messageAge = 0;
    }
    if (++messageAge > 20 * 12)
        message.clear();
    const auto act = app.midi.getActivityCounter();
    midiLed = act != lastMidi ? 4 : std::max (0, midiLed - 1);
    lastMidi = act;
    repaint (textArea);
}

void StatusBar::resized()
{
    auto r = getLocalBounds().reduced (10, 4);
    inMeter.setBounds (r.removeFromLeft (170));
    r.removeFromLeft (10);
    outMeter.setBounds (r.removeFromLeft (170));
    r.removeFromLeft (10);
    textArea = r;
}

void StatusBar::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff111214));
    g.setColour (Colours::panelEdge);
    g.drawHorizontalLine (0, 0, (float) getWidth());
    auto r = textArea;
    g.setFont (ForgeLookAndFeel::labelFont (11.5f, false));

    auto lamp = [&] (const juce::String& text, juce::Colour c, bool on, int width)
    {
        auto cell = r.removeFromRight (width);
        drawLed (g, juce::Rectangle<float> (8, 8).withCentre ({ (float) cell.getX() + 6.0f, (float) cell.getCentreY() }), c, on);
        g.setColour (on ? Colours::text : Colours::textDim);
        g.drawText (text, cell.withTrimmedLeft (14), juce::Justification::centredLeft);
    };
    lamp ("MIDI", Colours::good, midiLed > 0, 56);
    lamp ("LIMIT", Colours::bad, limiting, 60);
    lamp ("GATE", Colours::warn, app.params().getBool (pGateOn) && ! gateOpen, 58);
    g.setColour (cpu > 70.0f ? Colours::bad : Colours::textDim);
    g.drawText ("CPU " + juce::String (cpu, 0) + "%", r.removeFromRight (70), juce::Justification::centredLeft);

    g.setColour (Colours::textDim);
    const auto dev = app.getDeviceSummary();
    const int devW = std::min (r.getWidth() / 2, juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), dev)) + 20);
    g.drawText (dev, r.removeFromLeft (devW), juce::Justification::centredLeft, true);
    if (message.isNotEmpty())
    {
        g.setColour (messageIsWarning ? Colours::warn : Colours::text);
        g.drawText (message, r.reduced (6, 0), juce::Justification::centredLeft, true);
    }
}
} // namespace af::ui

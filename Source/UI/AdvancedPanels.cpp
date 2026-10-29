#include "AdvancedPanels.h"

namespace af::ui
{
const CompModelUi& compModelUi (int model)
{
    static const CompModelUi kUi[] = {
        { "Amount", "Level", true, true, "Clean, transparent VCA compressor with a soft knee." },
        { "Peak Red.", "Gain", false, false,
          "Optical leveller: smooth fixed attack and a two-stage release that slows the longer it works. Valve make-up stage." },
        { "Input", "Output", true, true,
          "FET limiting amplifier: ultra-fast attack, drive it with Input. All buttons in = aggressive, pumping and dirty." },
        { "Threshold", "Make-Up", true, true,
          "Console bus compressor: punchy, clean glue with stepped attack and release; Auto release follows the music." },
        { "Threshold", "Output", false, false,
          "Variable-mu valve limiter: very soft knee that firms up as you push it, six time constants, warm valve colour." },
        { "Threshold", "Output", false, false, "True-RMS VCA compressor: hard and punchy (OverEasy softens the knee), constant-rate release." },
    };
    return kUi[(size_t) juce::jlimit (0, 5, model)];
}

//==============================================================================
AdvancedWindow::AdvancedWindow (const juce::String& title, juce::Component* content)
    : juce::DocumentWindow (title, juce::Colour (0xff17181c), juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (content, true);
    setResizable (false, false);
    setAlwaysOnTop (true);
    centreWithSize (getWidth(), getHeight());
}

//==============================================================================
TypeHeader::TypeHeader (AppController& app, int typeParam) : type (app, typeParam)
{
    addAndMakeVisible (type);
    addAndMakeVisible (advanced);
    advanced.setTooltip ("Open the full set of controls for this effect.");
    advanced.onClick = [this] { if (onAdvanced) onAdvanced(); };
}

void TypeHeader::resized()
{
    auto r = getLocalBounds();
    advanced.setBounds (r.removeFromRight (40));
    r.removeFromRight (4);
    type.setBounds (r);
}

//==============================================================================
StudioCompAdvanced::StudioCompAdvanced (AppController& a)
    : app (a), enable (a, pStudioCompOn, "STUDIO COMP"), type (a, pStudioCompType),
      amount (a, pStudioCompAmount), attack (a, pStudioCompAttack), release (a, pStudioCompRelease), tone (a, pStudioCompTone),
      level (a, pStudioCompLevel), mix (a, pStudioCompMix), schpf (a, pStudioCompScHpf), emphasis (a, pStudioCompEmphasis),
      ratio (a, pStudioCompRatio), la2aMode (a, pStudioCompLa2aMode), ratio1176 (a, pStudioComp1176Ratio), sslRatio (a, pStudioCompSslRatio),
      tc (a, pStudioCompFairchildTc), sslAuto (a, pStudioCompSslAuto, "AUTO RELEASE"), overEasy (a, pStudioCompOverEasy, "OVEREASY")
{
    for (auto* c : std::initializer_list<juce::Component*> { &enable, &type, &vu, &amount, &attack, &release, &tone, &level, &mix, &schpf,
                                                             &emphasis, &ratio, &la2aMode, &ratio1176, &sslRatio, &tc, &sslAuto, &overEasy,
                                                             &description })
        addChildComponent (c);
    for (auto* c : std::initializer_list<juce::Component*> { &enable, &type, &vu, &description })
        c->setVisible (true);
    vu.readGainReduction = [this] { return app.engine.getStudioCompReductionDb(); };
    vu.isActive = [this] { return app.params().getBool (pStudioCompOn); };
    vu.setShowReadout (true);
    description.setFont (ForgeLookAndFeel::labelFont (12.0f, false));
    description.setColour (juce::Label::textColourId, Colours::textDim);
    description.setJustificationType (juce::Justification::topLeft);
    description.setMinimumHorizontalScale (1.0f);
    setSize (760, 360);
    refresh();
}

std::vector<int> StudioCompAdvanced::resetParams()
{
    return { pStudioCompAmount, pStudioCompAttack, pStudioCompTone, pStudioCompLevel, pStudioCompRelease, pStudioCompRatio,
             pStudioCompLa2aMode, pStudioComp1176Ratio, pStudioCompSslRatio, pStudioCompSslAuto, pStudioCompFairchildTc,
             pStudioCompOverEasy, pStudioCompEmphasis, pStudioCompMix, pStudioCompScHpf };
}

void StudioCompAdvanced::resetBlockControls()
{
    std::vector<std::pair<int, float>> d;
    for (int p : resetParams())
        d.emplace_back (p, paramDef (p).def);
    resetBlock (app, d, getBlockName());
}

void StudioCompAdvanced::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        resetBlockControls();
}

void StudioCompAdvanced::applyModel (int m)
{
    shownModel = m;
    const auto& ui = compModelUi (m);
    amount.setLabel (ui.amount);
    level.setLabel (ui.level);
    description.setText (ui.description, juce::dontSendNotification);

    visibleKnobs = { &amount };
    if (ui.attack) visibleKnobs.push_back (&attack);
    if (ui.release) visibleKnobs.push_back (&release);
    if (m == dsp::ClassicCompressor::la2a) visibleKnobs.push_back (&emphasis);
    for (auto* k : { &tone, &level, &mix, &schpf })
        visibleKnobs.push_back (k);

    visibleSwitches.clear();
    switch (m)
    {
        case dsp::ClassicCompressor::la2a: visibleSwitches = { &la2aMode }; break;
        case dsp::ClassicCompressor::fet1176: visibleSwitches = { &ratio1176 }; break;
        case dsp::ClassicCompressor::sslBus: visibleSwitches = { &sslRatio, &sslAuto }; break;
        case dsp::ClassicCompressor::fairchild: visibleSwitches = { &tc }; break;
        case dsp::ClassicCompressor::dbx160: visibleSwitches = { &ratio, &overEasy }; break;
        default: visibleSwitches = { &ratio }; break;
    }
    for (auto* c : std::initializer_list<juce::Component*> { &amount, &attack, &release, &tone, &level, &mix, &schpf, &emphasis, &ratio,
                                                             &la2aMode, &ratio1176, &sslRatio, &tc, &sslAuto, &overEasy })
        c->setVisible (std::find (visibleKnobs.begin(), visibleKnobs.end(), c) != visibleKnobs.end()
                       || std::find (visibleSwitches.begin(), visibleSwitches.end(), c) != visibleSwitches.end());
    resized();
    repaint();
}

void StudioCompAdvanced::refresh()
{
    const int m = app.params().getInt (pStudioCompType);
    if (m != shownModel)
        applyModel (m);
    enable.refresh();
    type.refresh();
    for (auto* k : { &amount, &attack, &release, &tone, &level, &mix, &schpf, &emphasis })
        k->refresh();
    for (auto* c : { &ratio, &la2aMode, &ratio1176, &sslRatio, &tc })
        c->refresh();
    sslAuto.refresh();
    overEasy.refresh();
    // The SSL's stepped release is replaced by Auto when that is on.
    release.setAlpha (m == dsp::ClassicCompressor::sslBus && app.params().getBool (pStudioCompSslAuto) ? 0.35f : 1.0f);
    vu.refresh();
}

void StudioCompAdvanced::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff17181c));
    drawPanel (g, getLocalBounds().toFloat().reduced (8.0f), {}, app.params().getBool (pStudioCompOn));
    g.setColour (Colours::textDim);
    g.setFont (ForgeLookAndFeel::labelFont (11.0f));
    g.setColour (Colours::text);
    for (const auto& [r, text] : switchLabels)
        g.drawText (text, r, juce::Justification::centredRight);
    g.setColour (Colours::textDim);
    g.drawText ("RIGHT-CLICK THE PANEL TO RESET", getLocalBounds().reduced (20, 16).removeFromBottom (14), juce::Justification::bottomLeft);
}

void StudioCompAdvanced::resized()
{
    auto r = getLocalBounds().reduced (20);
    auto header = r.removeFromTop (26);
    type.setBounds (header.removeFromRight (180));
    enable.setBounds (header);
    r.removeFromTop (10);
    auto left = r.removeFromLeft (290);
    vu.setBounds (left.removeFromTop (172).reduced (4, 0));
    left.removeFromTop (10);
    description.setBounds (left.removeFromTop (70));
    r.removeFromLeft (16);

    auto switchRow = r.removeFromBottom (30);
    r.removeFromBottom (8);
    const int perRow = 4;
    const int rows = ((int) visibleKnobs.size() + perRow - 1) / perRow;
    const int w = r.getWidth() / perRow, h = std::min (120, r.getHeight() / std::max (1, rows));
    for (size_t i = 0; i < visibleKnobs.size(); ++i)
        visibleKnobs[i]->setBounds (r.getX() + (int) (i % perRow) * w, r.getY() + (int) (i / perRow) * h, w, h);
    switchLabels.clear();
    for (auto* c : visibleSwitches)
    {
        if (auto* choice = dynamic_cast<ParamChoice*> (c))
        {
            switchLabels.push_back ({ switchRow.removeFromLeft (100), juce::String (paramDef (choice->getParamIndex()).name).toUpperCase() });
            switchRow.removeFromLeft (8);
        }
        c->setBounds (switchRow.removeFromLeft (140).reduced (0, 2));
        switchRow.removeFromLeft (16);
    }
}

//==============================================================================
namespace
{
    const char* reverbDescription (int type, int program)
    {
        static const char* lex[] = {
            "Lexicon Concert Hall: big, smooth and gently chorused - the classic lush studio hall.",
            "Lexicon Random Hall: randomised modulation keeps the tail wide and shimmering, never metallic - the classic lead and vocal hall.",
            "Lexicon Rich Plate: bright, dense and instant, with a rich chorused tail.",
            "Lexicon Chamber: darker, mid-sized and dense, like a studio echo chamber.",
        };
        static const char* m7[] = {
            "Bricasti Large Hall: natural, uncoloured concert hall with distinct early reflections and a smooth, dense tail.",
            "Bricasti Small Room: tight and realistic - mostly early reflections, short tail.",
            "Bricasti Plate: dense, smooth plate with no early reflections.",
            "Bricasti Chamber: even, natural chamber with a warm decay.",
        };
        static const char* basic[] = { "Room: small and close.", "Hall: big and smooth.", "Spring: the classic amp tank, with its drip.",
                                       "Chamber: dense, bright and even.", "Plate: instant, dense and bright." };
        if (type == dsp::Reverb::lexicon) return lex[juce::jlimit (0, 3, program)];
        if (type == dsp::Reverb::bricasti) return m7[juce::jlimit (0, 3, program)];
        return basic[juce::jlimit (0, 4, type)];
    }
} // namespace

ReverbAdvanced::ReverbAdvanced (AppController& a)
    : app (a), enable (a, pReverbOn, "REVERB"), type (a, pReverbType), lexProgram (a, pReverbLexProgram), m7Program (a, pReverbM7Program),
      mix (a, pReverbMix), decay (a, pReverbDecay), tone (a, pReverbTone), preDelay (a, pReverbPreDelay), size (a, pReverbSize),
      diffusion (a, pReverbDiffusion), bassMult (a, pReverbBassMult), crossover (a, pReverbCrossover), spin (a, pReverbSpin),
      wander (a, pReverbWander), early (a, pReverbEarly), width (a, pReverbWidth), lowCut (a, pReverbLowCut)
{
    for (auto* c : std::initializer_list<juce::Component*> { &enable, &type, &lexProgram, &m7Program, &mix, &decay, &tone, &preDelay, &size,
                                                             &diffusion, &bassMult, &crossover, &spin, &wander, &early, &width, &lowCut,
                                                             &description })
        addChildComponent (c);
    for (auto* c : std::initializer_list<juce::Component*> { &enable, &type, &description })
        c->setVisible (true);
    description.setFont (ForgeLookAndFeel::labelFont (12.0f, false));
    description.setColour (juce::Label::textColourId, Colours::textDim);
    description.setJustificationType (juce::Justification::topLeft);
    setSize (860, 380);
    refresh();
}

std::vector<int> ReverbAdvanced::resetParams()
{
    return { pReverbMix, pReverbDecay, pReverbTone, pReverbPreDelay, pReverbSize, pReverbDiffusion, pReverbBassMult, pReverbCrossover,
             pReverbSpin, pReverbWander, pReverbEarly, pReverbWidth, pReverbLowCut };
}

void ReverbAdvanced::resetBlockControls()
{
    std::vector<std::pair<int, float>> d;
    for (int p : resetParams())
        d.emplace_back (p, paramDef (p).def);
    resetBlock (app, d, getBlockName());
}

void ReverbAdvanced::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        resetBlockControls();
}

void ReverbAdvanced::applyType (int t, int program)
{
    shownType = t;
    shownProgram = program;
    const bool studio = t == dsp::Reverb::lexicon || t == dsp::Reverb::bricasti;
    description.setText (reverbDescription (t, program), juce::dontSendNotification);
    tone.setLabel (studio ? "Treble Decay" : "Tone");
    visibleKnobs = { &mix, &decay, &tone };
    if (studio)
        for (auto* k : { &preDelay, &size, &diffusion, &bassMult, &crossover, &spin, &wander })
            visibleKnobs.push_back (k);
    if (t == dsp::Reverb::bricasti)
        visibleKnobs.push_back (&early);
    visibleKnobs.push_back (&width);
    visibleKnobs.push_back (&lowCut);
    for (auto* k : std::initializer_list<juce::Component*> { &mix, &decay, &tone, &preDelay, &size, &diffusion, &bassMult, &crossover, &spin,
                                                             &wander, &early, &width, &lowCut })
        k->setVisible (std::find (visibleKnobs.begin(), visibleKnobs.end(), k) != visibleKnobs.end());
    lexProgram.setVisible (t == dsp::Reverb::lexicon);
    m7Program.setVisible (t == dsp::Reverb::bricasti);
    resized();
    repaint();
}

void ReverbAdvanced::refresh()
{
    const int t = app.params().getInt (pReverbType);
    const int prog = t == dsp::Reverb::bricasti ? app.params().getInt (pReverbM7Program) : app.params().getInt (pReverbLexProgram);
    if (t != shownType || prog != shownProgram)
        applyType (t, prog);
    enable.refresh();
    for (auto* c : { &type, &lexProgram, &m7Program })
        c->refresh();
    for (auto* k : { &mix, &decay, &tone, &preDelay, &size, &diffusion, &bassMult, &crossover, &spin, &wander, &early, &width, &lowCut })
        k->refresh();
}

void ReverbAdvanced::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff17181c));
    drawPanel (g, getLocalBounds().toFloat().reduced (8.0f), {}, app.params().getBool (pReverbOn));
    g.setColour (Colours::text);
    g.setFont (ForgeLookAndFeel::labelFont (11.0f));
    if (lexProgram.isVisible() || m7Program.isVisible())
        g.drawText ("PROGRAM", lexProgram.getBounds().translated (0, -18).withHeight (16), juce::Justification::centredLeft);
    g.setColour (Colours::textDim);
    g.drawText ("RIGHT-CLICK THE PANEL TO RESET", getLocalBounds().reduced (20, 16).removeFromBottom (14), juce::Justification::bottomLeft);
}

void ReverbAdvanced::resized()
{
    auto r = getLocalBounds().reduced (20);
    auto header = r.removeFromTop (26);
    type.setBounds (header.removeFromRight (180));
    enable.setBounds (header);
    r.removeFromTop (12);
    auto left = r.removeFromLeft (230);
    left.removeFromTop (18);
    const auto prog = left.removeFromTop (28);
    lexProgram.setBounds (prog);
    m7Program.setBounds (prog);
    left.removeFromTop (14);
    description.setBounds (left.removeFromTop (110));
    r.removeFromLeft (16);
    r.removeFromBottom (10);
    const int perRow = 5;
    const int rows = ((int) visibleKnobs.size() + perRow - 1) / perRow;
    const int w = r.getWidth() / perRow, h = std::min (110, r.getHeight() / std::max (1, rows));
    for (size_t i = 0; i < visibleKnobs.size(); ++i)
        visibleKnobs[i]->setBounds (r.getX() + (int) (i % perRow) * w, r.getY() + (int) (i / perRow) * h, w, h);
}
} // namespace af::ui

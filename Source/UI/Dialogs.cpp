#include "Dialogs.h"

namespace af::ui
{
namespace
{
    class ForgeDialogWindow final : public juce::DialogWindow
    {
    public:
        ForgeDialogWindow (const juce::String& title, bool canResize)
            : juce::DialogWindow (title, Colours::background, true, true)
        {
            setUsingNativeTitleBar (true);
            setResizable (canResize, false);
        }
        void closeButtonPressed() override { delete this; }
    };

    void styleEditor (juce::TextEditor& e, const juce::String& placeholder)
    {
        e.setTextToShowWhenEmpty (placeholder, Colours::textDim);
        e.setFont (ForgeLookAndFeel::labelFont (14.0f, false));
        e.setIndents (8, 6);
    }

    void closeParentDialog (juce::Component* c)
    {
        if (auto* dw = c->findParentComponentOfClass<juce::DialogWindow>())
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::DialogWindow> (dw)] { delete safe.getComponent(); });
    }
} // namespace

void showDialog (const juce::String& title, std::unique_ptr<juce::Component> content, juce::Component* parent, bool resizable)
{
    auto* w = new ForgeDialogWindow (title, resizable);
    const auto size = content->getBounds();
    w->setContentOwned (content.release(), true);
    w->centreAroundComponent (parent, size.getWidth(), size.getHeight());
    if (resizable)
        w->setResizeLimits (std::min (480, size.getWidth()), std::min (360, size.getHeight()), 2000, 1600);
    w->setVisible (true);
    w->toFront (true);
}

//==============================================================================
PresetBrowser::PresetBrowser (AppController& a) : app (a)
{
    styleEditor (search, "Search presets...");
    search.onTextChange = [this] { refilter(); };
    category.addItem ("All categories", 1);
    int id = 2;
    for (const auto& c : PresetManager::getCategories())
        category.addItem (c, id++);
    category.setSelectedId (1, juce::dontSendNotification);
    category.onChange = [this] { refilter(); };
    family.addItemList ({ "All amps", "American", "British", "Modern" }, 1);
    family.setSelectedId (1, juce::dontSendNotification);
    family.onChange = [this] { refilter(); };
    list.setRowHeight (34);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff141518));
    info.setColour (juce::Label::textColourId, Colours::textDim);
    info.setFont (ForgeLookAndFeel::labelFont (12.5f, false));
    info.setJustificationType (juce::Justification::topLeft);

    rename.onClick = [this]
    {
        const int idx = selectedPresetIndex();
        if (idx < 0)
            return;
        auto* w = new juce::AlertWindow ("Rename preset", "New name:", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", app.presets.getPresets()[(size_t) idx].state.name);
        w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<PresetBrowser> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, idx] (int r) {
            if (safe == nullptr || r != 1)
                return;
            juce::String err;
            if (! safe->app.renamePreset (idx, w->getTextEditorContents ("name"), err))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Rename", err);
            safe->refilter();
        }), true);
    };
    duplicate.onClick = [this]
    {
        juce::String err;
        if (selectedPresetIndex() >= 0 && ! app.duplicatePreset (selectedPresetIndex(), err))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Duplicate", err);
        refilter();
    };
    remove.onClick = [this]
    {
        const int idx = selectedPresetIndex();
        if (idx < 0)
            return;
        const auto& p = app.presets.getPresets()[(size_t) idx];
        if (p.isFactory)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Delete", "Factory presets cannot be deleted.");
            return;
        }
        juce::Component::SafePointer<PresetBrowser> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon,
                                          "Delete preset", "Delete \"" + p.state.name + "\"? It will be moved to the Trash where available.",
                                          "Delete", "Cancel", this),
                                      [safe, idx] (int r) {
                                          if (safe == nullptr || r != 1)
                                              return;
                                          juce::String err;
                                          if (! safe->app.deletePreset (idx, err))
                                              juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Delete", err);
                                          safe->refilter();
                                      });
    };
    reveal.onClick = [this] { app.presets.getUserDirectory().createDirectory(); app.presets.getUserDirectory().revealToUser(); };

    for (auto* c : std::initializer_list<juce::Component*> { &search, &category, &family, &list, &rename, &duplicate, &remove, &reveal, &info })
        addAndMakeVisible (c);
    setSize (720, 540);
    refilter();
    startTimerHz (5);
}

int PresetBrowser::selectedPresetIndex() const
{
    const int row = list.getSelectedRow();
    return row >= 0 && row < (int) rows.size() ? rows[(size_t) row] : -1;
}

void PresetBrowser::refilter()
{
    const auto cat = category.getSelectedId() > 1 ? category.getText() : juce::String();
    rows = app.presets.filter (search.getText().trim(), cat, family.getSelectedId() - 2);
    lastCount = app.presets.getPresets().size();
    list.updateContent();
    const auto it = std::find (rows.begin(), rows.end(), app.getCurrentPresetIndex());
    if (it != rows.end())
        list.selectRow ((int) (it - rows.begin()), false, true);
    else
        list.deselectAllRows();
    lastCurrent = app.getCurrentPresetIndex();
    list.repaint();
}

void PresetBrowser::timerCallback()
{
    if (app.presets.getPresets().size() != lastCount || app.getCurrentPresetIndex() != lastCurrent)
        refilter();
    const int idx = selectedPresetIndex();
    const bool user = idx >= 0 && ! app.presets.getPresets()[(size_t) idx].isFactory;
    rename.setEnabled (user);
    remove.setEnabled (user);
    duplicate.setEnabled (idx >= 0);
    if (idx >= 0)
    {
        const auto& s = app.presets.getPresets()[(size_t) idx].state;
        info.setText (s.description.isNotEmpty() ? s.description : juce::String ("No description."), juce::dontSendNotification);
    }
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) rows.size())
        return;
    const auto& p = app.presets.getPresets()[(size_t) rows[(size_t) row]];
    const bool current = rows[(size_t) row] == app.getCurrentPresetIndex();
    if (selected)
        g.fillAll (Colours::accentDim.withAlpha (0.6f));
    else if (row % 2)
        g.fillAll (juce::Colours::white.withAlpha (0.02f));
    g.setColour (current ? Colours::accent : Colours::text);
    g.setFont (ForgeLookAndFeel::labelFont (14.0f));
    g.drawText (p.state.name, 12, 0, w / 2, h, juce::Justification::centredLeft, true);
    g.setColour (Colours::textDim);
    g.setFont (ForgeLookAndFeel::labelFont (12.0f, false));
    const int model = (int) p.state.values[(size_t) pAmpModel];
    g.drawText (p.state.category, w / 2, 0, w / 5, h, juce::Justification::centredLeft, true);
    g.drawText (dsp::getAmpVoicing (model).name, w / 2 + w / 5, 0, w / 5, h, juce::Justification::centredLeft, true);
    g.drawText (p.isFactory ? "FACTORY" : "USER", w - 80, 0, 70, h, juce::Justification::centredRight, true);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) rows.size())
        app.loadPreset (rows[(size_t) row]); // click to load: quick auditioning while playing
}

void PresetBrowser::returnKeyPressed (int row)
{
    if (row >= 0 && row < (int) rows.size())
        app.loadPreset (rows[(size_t) row]);
}

void PresetBrowser::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (32);
    family.setBounds (top.removeFromRight (130));
    top.removeFromRight (8);
    category.setBounds (top.removeFromRight (160));
    top.removeFromRight (8);
    search.setBounds (top);
    r.removeFromTop (10);
    auto bottom = r.removeFromBottom (32);
    for (auto* b : { &reveal, &remove, &duplicate, &rename })
    {
        b->setBounds (bottom.removeFromRight (110));
        bottom.removeFromRight (8);
    }
    r.removeFromBottom (8);
    info.setBounds (r.removeFromBottom (44));
    list.setBounds (r);
}

void PresetBrowser::paint (juce::Graphics& g) { g.fillAll (Colours::background); }

//==============================================================================
SaveAsForm::SaveAsForm (AppController& a, std::function<void()> onDone) : app (a), done (std::move (onDone))
{
    styleEditor (name, "Preset name");
    styleEditor (description, "Description (optional)");
    description.setMultiLine (true);
    description.setReturnKeyStartsNewLine (true);
    name.setText (app.presets.makeUniqueName (app.getCurrentPresetName()));
    name.onReturnKey = [this] { attemptSave(); };
    category.addItemList (PresetManager::getCategories(), 1);
    const int cur = app.getCurrentPresetIndex();
    const auto curCat = cur >= 0 ? app.presets.getPresets()[(size_t) cur].state.category : juce::String ("User");
    category.setText (curCat, juce::dontSendNotification);
    error.setColour (juce::Label::textColourId, Colours::bad);
    save.onClick = [this] { attemptSave(); };
    cancel.onClick = [this] { if (done) done(); closeParentDialog (this); };
    for (auto* c : std::initializer_list<juce::Component*> { &name, &description, &category, &save, &cancel, &error })
        addAndMakeVisible (c);
    setSize (440, 280);
}

void SaveAsForm::attemptSave()
{
    juce::String err;
    if (! app.saveAs (name.getText(), category.getText(), description.getText(), err))
    {
        error.setText (err, juce::dontSendNotification);
        return;
    }
    if (done)
        done();
    closeParentDialog (this);
}

void SaveAsForm::resized()
{
    auto r = getLocalBounds().reduced (16);
    name.setBounds (r.removeFromTop (32));
    r.removeFromTop (8);
    category.setBounds (r.removeFromTop (30));
    r.removeFromTop (8);
    auto buttons = r.removeFromBottom (32);
    cancel.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    save.setBounds (buttons.removeFromRight (90));
    r.removeFromBottom (6);
    error.setBounds (r.removeFromBottom (22));
    description.setBounds (r);
}

void SaveAsForm::paint (juce::Graphics& g) { g.fillAll (Colours::background); }

//==============================================================================
IRBrowser::IRBrowser (AppController& a, int s, std::function<void()> onImportRequest)
    : app (a), slot (s), requestImport (std::move (onImportRequest))
{
    originalId = app.getCabinetIrId (slot);
    styleEditor (search, "Search IRs...");
    search.onTextChange = [this] { reload(); };
    list.setRowHeight (30);
    info.setFont (ForgeLookAndFeel::labelFont (12.5f, false));
    info.setColour (juce::Label::textColourId, Colours::textDim);
    info.setJustificationType (juce::Justification::topLeft);
    importButton.onClick = [this] { if (requestImport) requestImport(); };
    removeButton.onClick = [this]
    {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= (int) shown.size() || shown[(size_t) row].isFactory)
            return;
        const auto e = shown[(size_t) row];
        const auto msg = e.isCopy ? "Remove \"" + e.name + "\" and delete AMP FORGE's own copy of the file? Your original file is not affected."
                                  : "Remove \"" + e.name + "\" from the library? The file itself stays where it is.";
        juce::Component::SafePointer<IRBrowser> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "Remove IR", msg, "Remove", "Cancel", this),
                                      [safe, e] (int r) {
                                          if (safe == nullptr || r != 1)
                                              return;
                                          juce::String err;
                                          if (! safe->app.removeIR (e.id, err))
                                              juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Remove IR", err);
                                          safe->reload();
                                      });
    };
    keep.onClick = [this] { committed = true; closeParentDialog (this); };
    revert.onClick = [this]
    {
        if (originalId.isNotEmpty())
            app.setCabinetIR (slot, originalId);
        committed = true;
        closeParentDialog (this);
    };
    keep.setTooltip ("Keep the IR you are hearing now.");
    revert.setTooltip ("Go back to the IR that was loaded when you opened this browser.");
    for (auto* c : std::initializer_list<juce::Component*> { &search, &list, &importButton, &removeButton, &keep, &revert, &info })
        addAndMakeVisible (c);
    setSize (640, 520);
    reload();
    startTimerHz (4);
}

IRBrowser::~IRBrowser() = default; // closing the window keeps the auditioned IR (like "Keep")

void IRBrowser::reload()
{
    app.irLibrary.refreshMissingFlags();
    entries = app.irLibrary.getEntries();
    lastEntryCount = entries.size();
    shown.clear();
    const auto q = search.getText().trim();
    for (const auto& e : entries)
        if (q.isEmpty() || e.name.containsIgnoreCase (q) || e.description.containsIgnoreCase (q))
            shown.push_back (e);
    list.updateContent();
    const auto cur = app.getCabinetIrId (slot);
    for (size_t i = 0; i < shown.size(); ++i)
        if (shown[i].id == cur)
            list.selectRow ((int) i, false, true);
    list.repaint();
    updateInfo();
}

void IRBrowser::timerCallback()
{
    if (app.irLibrary.getEntries().size() != lastEntryCount)
        reload();
    updateInfo();
}

void IRBrowser::updateInfo()
{
    const int row = list.getSelectedRow();
    removeButton.setEnabled (row >= 0 && row < (int) shown.size() && ! shown[(size_t) row].isFactory);
    if (row < 0 || row >= (int) shown.size())
    {
        info.setText ("Click an IR to hear it immediately while you play. Drag WAV files onto the cabinet panel to import.", juce::dontSendNotification);
        return;
    }
    const auto& e = shown[(size_t) row];
    juce::String t;
    if (e.isFactory)
        t << e.description << "\nFactory IR (original, procedurally generated by AMP FORGE; see docs/IR_FORMATS.md).";
    else
    {
        t << e.originalFilename << "  -  " << e.numChannels << (e.numChannels == 1 ? " channel" : " channels") << ", "
          << juce::String (e.sampleRate / 1000.0, 1) << " kHz, " << juce::String (e.durationSeconds * 1000.0, 0) << " ms\n"
          << (e.isCopy ? "Copied into the AMP FORGE library." : "Referenced in place: keep the original file where it is.");
        if (e.missing)
            t << "\nMISSING: the file has been moved or deleted.";
    }
    info.setText (t, juce::dontSendNotification);
}

void IRBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) shown.size())
        return;
    const auto& e = shown[(size_t) row];
    if (selected)
        g.fillAll (Colours::accentDim.withAlpha (0.6f));
    else if (row % 2)
        g.fillAll (juce::Colours::white.withAlpha (0.02f));
    g.setColour (e.missing ? Colours::bad : Colours::text);
    g.setFont (ForgeLookAndFeel::labelFont (13.5f));
    g.drawText (e.name, 12, 0, w - 120, h, juce::Justification::centredLeft, true);
    g.setColour (Colours::textDim);
    g.setFont (ForgeLookAndFeel::labelFont (11.5f, false));
    g.drawText (e.isFactory ? "FACTORY" : (e.missing ? "MISSING" : (e.isCopy ? "USER" : "USER (ref)")), w - 110, 0, 100, h, juce::Justification::centredRight);
}

void IRBrowser::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) shown.size() && ! shown[(size_t) row].missing)
        app.setCabinetIR (slot, shown[(size_t) row].id); // audition: loads while you play
    updateInfo();
}

void IRBrowser::resized()
{
    auto r = getLocalBounds().reduced (12);
    search.setBounds (r.removeFromTop (32));
    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (32);
    revert.setBounds (bottom.removeFromRight (90));
    bottom.removeFromRight (8);
    keep.setBounds (bottom.removeFromRight (90));
    importButton.setBounds (bottom.removeFromLeft (100));
    bottom.removeFromLeft (8);
    removeButton.setBounds (bottom.removeFromLeft (160));
    r.removeFromBottom (8);
    info.setBounds (r.removeFromBottom (54));
    list.setBounds (r);
}

void IRBrowser::paint (juce::Graphics& g) { g.fillAll (Colours::background); }

//==============================================================================
MidiSettings::MidiSettings (AppController& a) : app (a)
{
    channel.addItem ("Omni (all channels)", 1);
    for (int i = 1; i <= 16; ++i)
        channel.addItem ("Channel " + juce::String (i), i + 1);
    channel.setSelectedId (app.midi.getChannel() + 1, juce::dontSendNotification);
    channel.onChange = [this] { app.midi.setChannel (channel.getSelectedId() - 1); app.saveMidiSettings(); };

    pcEnabled.setToggleState (app.midi.isProgramChangeEnabled(), juce::dontSendNotification);
    pcEnabled.onClick = [this] { app.midi.setProgramChangeEnabled (pcEnabled.getToggleState()); app.saveMidiSettings(); };
    for (int off = 0; off <= 20; ++off)
        pcOffset.addItem ("PC 0 (shown as 1 on most controllers) loads preset #" + juce::String (off + 1), off + 1);
    pcOffset.setSelectedId (app.getProgramChangeOffset() + 1, juce::dontSendNotification);
    pcOffset.onChange = [this] { app.setProgramChangeOffset (pcOffset.getSelectedId() - 1); };
    pcOffset.setTooltip ("Program Change numbers are 0-127 on the wire; many controllers display them as 1-128. "
                         "Program n loads preset (n + 1 + offset) in the preset list order (factory first, then yours alphabetically).");

    // Parameters that make sense to control live.
    for (int p : { (int) pAmpGain, (int) pAmpMaster, (int) pAmpModel, (int) pAmpBass, (int) pAmpMid, (int) pAmpTreble, (int) pAmpPresence,
                   (int) pDriveOn, (int) pDriveDrive, (int) pGateOn, (int) pEqOn, (int) pEq80, (int) pEq240, (int) pEq750, (int) pEq2200,
                   (int) pEq6600, (int) pCabOn, (int) pChorusOn, (int) pChorusMix, (int) pDelayOn, (int) pDelayMix, (int) pDelayFeedback,
                   (int) pDelayTime, (int) pReverbOn, (int) pReverbMix, (int) pReverbDecay, (int) pOutLevel, (int) pBypass, (int) pTempo })
        learnableParams.push_back (p);
    int id = 1;
    for (int p : learnableParams)
        learnParam.addItem (juce::String (paramDef (p).group) + ": " + paramDef (p).name, id++);
    learnParam.setSelectedId (1, juce::dontSendNotification);
    toggleMode.setTooltip ("For momentary footswitches that send 127 on every press: each press flips the effect on/off.");

    learn.onClick = [this]
    {
        const int p = learnableParams[(size_t) learnParam.getSelectedId() - 1];
        if (app.midi.getLearnParam() == p)
            app.midi.cancelLearn();
        else
            app.midi.startLearn (p);
    };
    removeMapping.onClick = [this]
    {
        const int row = list.getSelectedRow();
        if (row >= 0 && row < (int) mappings.size())
        {
            app.midi.clearMapping (mappings[(size_t) row].cc);
            app.saveMidiSettings();
        }
    };
    defaults.onClick = [this] { app.midi.loadDefaultMappings(); app.saveMidiSettings(); };
    list.setRowHeight (26);
    status.setFont (ForgeLookAndFeel::labelFont (12.5f, false));
    status.setColour (juce::Label::textColourId, Colours::textDim);
    for (auto* c : std::initializer_list<juce::Component*> { &channel, &pcEnabled, &pcOffset, &learnParam, &toggleMode, &learn, &removeMapping, &defaults, &list, &status })
        addAndMakeVisible (c);
    setSize (640, 560);
    startTimerHz (10);
}

MidiSettings::~MidiSettings()
{
    app.midi.cancelLearn();
    app.saveMidiSettings();
}

void MidiSettings::timerCallback()
{
    // A completed learn: apply the chosen footswitch mode to the new mapping.
    auto now = app.midi.getMappings();
    if (toggleMode.getToggleState())
        for (auto& m : now)
            if (paramDef (m.paramIndex).scale == ParamScale::toggle && ! m.toggle
                && std::none_of (mappings.begin(), mappings.end(), [&] (const CCMapping& o) { return o.cc == m.cc && o.paramIndex == m.paramIndex; }))
            {
                m.toggle = true;
                app.midi.setMapping (m);
            }
    if (now.size() != mappings.size() || ! std::equal (now.begin(), now.end(), mappings.begin(), [] (const CCMapping& x, const CCMapping& y) {
            return x.cc == y.cc && x.paramIndex == y.paramIndex && x.toggle == y.toggle; }))
    {
        mappings = app.midi.getMappings();
        list.updateContent();
        list.repaint();
        app.saveMidiSettings();
    }
    const int learning = app.midi.getLearnParam();
    learn.setButtonText (learning >= 0 ? "Cancel Learn" : "Learn");
    juce::String s;
    if (learning >= 0)
        s << "Learning " << paramDef (learning).name << ": move a knob or press a footswitch on your controller...";
    else if (app.midi.getActivityCounter() != lastActivity)
        s << "MIDI received" << (app.midi.getLastCC() >= 0 ? " (last CC " + juce::String (app.midi.getLastCC()) + ")" : juce::String());
    else
        s << "MIDI inputs are enabled in Audio Settings. Default mappings: CC80-85 effect switches, CC11 gain, CC7 output (-40..0 dB).";
    lastActivity = app.midi.getActivityCounter();
    status.setText (s, juce::dontSendNotification);
}

void MidiSettings::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) mappings.size())
        return;
    const auto& m = mappings[(size_t) row];
    if (selected)
        g.fillAll (Colours::accentDim.withAlpha (0.6f));
    g.setColour (Colours::text);
    g.setFont (ForgeLookAndFeel::labelFont (13.0f));
    g.drawText ("CC " + juce::String (m.cc), 10, 0, 70, h, juce::Justification::centredLeft);
    g.drawText (juce::String (paramDef (m.paramIndex).group) + ": " + paramDef (m.paramIndex).name, 80, 0, w / 2, h, juce::Justification::centredLeft, true);
    g.setColour (Colours::textDim);
    g.setFont (ForgeLookAndFeel::labelFont (12.0f, false));
    juce::String detail;
    if (paramDef (m.paramIndex).scale == ParamScale::toggle)
        detail = m.toggle ? "toggle on press" : "on >= 64, off < 64";
    else
        detail = juce::String (formatParamValue (m.paramIndex, normalisedToPlain (m.paramIndex, m.minNorm))) + " .. "
                 + juce::String (formatParamValue (m.paramIndex, normalisedToPlain (m.paramIndex, m.maxNorm)));
    g.drawText (detail, w / 2 + 80, 0, w / 2 - 90, h, juce::Justification::centredRight, true);
}

void MidiSettings::resized()
{
    auto r = getLocalBounds().reduced (14);
    auto row = r.removeFromTop (30);
    channel.setBounds (row.removeFromLeft (200));
    row.removeFromLeft (10);
    pcEnabled.setBounds (row);
    r.removeFromTop (8);
    pcOffset.setBounds (r.removeFromTop (30));
    r.removeFromTop (14);
    row = r.removeFromTop (30);
    learn.setBounds (row.removeFromRight (110));
    row.removeFromRight (8);
    learnParam.setBounds (row);
    r.removeFromTop (6);
    toggleMode.setBounds (r.removeFromTop (24));
    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (32);
    defaults.setBounds (bottom.removeFromRight (140));
    bottom.removeFromRight (8);
    removeMapping.setBounds (bottom.removeFromRight (140));
    r.removeFromBottom (8);
    status.setBounds (r.removeFromBottom (40));
    list.setBounds (r);
}

void MidiSettings::paint (juce::Graphics& g) { g.fillAll (Colours::background); }

//==============================================================================
AudioSettings::AudioSettings (AppController& a)
    : app (a), selector (a.deviceManager, 0, 2, 1, 2, true, false, false, false)
{
    guitarInput.onChange = [this] { app.setGuitarInputChannel (guitarInput.getSelectedId() - 1); };
    guitarInput.setTooltip ("Which active input channel carries your guitar.");
    oversampling.addItem ("Oversampling: Automatic (recommended)", 1);
    oversampling.addItem ("Oversampling: 2x (lowest CPU)", 2);
    oversampling.addItem ("Oversampling: 4x", 3);
    oversampling.addItem ("Oversampling: 8x (highest quality, most CPU)", 4);
    oversampling.setSelectedId ((int) app.getOversampling() + 1, juce::dontSendNotification);
    oversampling.onChange = [this] { app.setOversampling ((OversamplingMode) (oversampling.getSelectedId() - 1)); };
    oversampling.setTooltip ("Automatic uses 4x at 44.1/48 kHz, 2x at 88.2/96 kHz and 1x above. Restarts audio briefly.");
    guidance.setFont (ForgeLookAndFeel::labelFont (12.5f, false));
    guidance.setColour (juce::Label::textColourId, Colours::textDim);
    guidance.setJustificationType (juce::Justification::topLeft);
    guidance.setText ("Tips: choose your audio interface, enable the input your guitar is plugged into (an instrument / Hi-Z input), "
                      "and use 64 or 128 samples for responsive playing. Total latency = converter + driver + buffers; "
                      "AMP FORGE itself adds no buffering. Set the interface gain so hard picking peaks around -12 dBFS on the IN meter. "
                      "Avoid built-in microphone + speakers: loud feedback is likely.",
                      juce::dontSendNotification);
    addAndMakeVisible (selector);
    addAndMakeVisible (guitarInput);
    addAndMakeVisible (oversampling);
    addAndMakeVisible (guidance);
    app.deviceManager.addChangeListener (this);
    refreshInputs();
    setSize (620, 640);
}

AudioSettings::~AudioSettings() { app.deviceManager.removeChangeListener (this); }

void AudioSettings::changeListenerCallback (juce::ChangeBroadcaster*) { refreshInputs(); }

void AudioSettings::refreshInputs()
{
    guitarInput.clear (juce::dontSendNotification);
    const auto names = app.getActiveInputChannelNames();
    if (names.isEmpty())
        guitarInput.addItem ("Guitar input: no input channel enabled", 1000);
    for (int i = 0; i < names.size(); ++i)
        guitarInput.addItem ("Guitar input: " + names[i], i + 1);
    guitarInput.setSelectedId (names.isEmpty() ? 1000 : juce::jlimit (1, names.size(), app.getGuitarInputChannel() + 1), juce::dontSendNotification);
    guitarInput.setEnabled (! names.isEmpty());
}

void AudioSettings::resized()
{
    auto r = getLocalBounds().reduced (12);
    guidance.setBounds (r.removeFromBottom (86));
    r.removeFromBottom (6);
    oversampling.setBounds (r.removeFromBottom (30));
    r.removeFromBottom (6);
    guitarInput.setBounds (r.removeFromBottom (30));
    r.removeFromBottom (8);
    selector.setBounds (r);
}

void AudioSettings::paint (juce::Graphics& g) { g.fillAll (Colours::background); }

//==============================================================================
Onboarding::Onboarding (AppController& a) : app (a)
{
    openAudio.onClick = [this] { if (onOpenAudio) onOpenAudio(); };
    done.onClick = [this] { if (onDismiss) onDismiss(); };
    addAndMakeVisible (openAudio);
    addAndMakeVisible (done);
}

void Onboarding::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (std::min (620, getWidth() - 40), std::min (440, getHeight() - 40));
    auto buttons = card.reduced (24).removeFromBottom (36);
    done.setBounds (buttons.removeFromRight (150));
    buttons.removeFromRight (10);
    openAudio.setBounds (buttons.removeFromRight (190));
}

void Onboarding::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.72f));
    drawPanel (g, card.toFloat());
    auto r = card.reduced (28);
    g.setColour (Colours::accent);
    g.setFont (ForgeLookAndFeel::labelFont (22.0f));
    g.drawText ("Welcome to " AMPFORGE_PRODUCT_NAME, r.removeFromTop (34), juce::Justification::centredLeft);
    r.removeFromTop (8);
    const char* steps[] = {
        "1.  Plug your guitar into an instrument (Hi-Z) input on your audio interface.",
        "2.  Open Audio Settings: choose the interface, tick the guitar input, and pick a 64 or 128 sample buffer.",
        "3.  Set the interface gain so strong picking peaks around -12 dBFS on the IN meter (bottom left).",
        "4.  Start quietly: lower your monitors, then raise them while playing. Use headphones or the interface outputs.",
        "5.  Choose a preset at the top, or pick an amp, cabinet and effects. Right-click any knob for MIDI Learn.",
    };
    g.setFont (ForgeLookAndFeel::labelFont (14.0f, false));
    for (auto* s : steps)
    {
        g.setColour (Colours::text);
        g.drawFittedText (s, r.removeFromTop (44), juce::Justification::centredLeft, 2);
    }
    if (! app.hasInputDevice())
    {
        g.setColour (Colours::warn);
        g.drawFittedText ("No guitar input is active yet - open Audio Settings to choose one.", r.removeFromTop (30), juce::Justification::centredLeft, 2);
    }
}
} // namespace af::ui

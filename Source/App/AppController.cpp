#include "AppController.h"
#include "../Platform/FileUtils.h"

namespace af
{
namespace
{
    class ParamAction final : public juce::UndoableAction
    {
    public:
        ParamAction (ParameterStore& s, int i, float from, float to) : store (s), index (i), oldValue (from), newValue (to) {}
        bool perform() override { store.set (index, newValue); return true; }
        bool undo() override { store.set (index, oldValue); return true; }
        int getSizeInUnits() override { return 1; }

    private:
        ParameterStore& store;
        int index;
        float oldValue, newValue;
    };

    juce::String irSignature (const AmpForgeEngine& e)
    {
        return juce::String (e.getCabinetInfo (0).irId) + "|" + juce::String (e.getCabinetInfo (1).irId);
    }
} // namespace

AppController::AppController()
    : irLibrary (getUserIRDirectory()), presets (getUserPresetDirectory())
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = AMPFORGE_PRODUCT_NAME;
    opts.filenameSuffix = ".settings";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    opts.millisecondsBeforeSaving = 1000;
    settings = std::make_unique<juce::PropertiesFile> (getAppDataDirectory().getChildFile ("ZackAMP.settings"), opts);
}

AppController::~AppController() { shutdown(); }

void AppController::initialise()
{
    getAppDataDirectory().createDirectory();
    irLibrary.loadFactory();
    juce::String err;
    if (! irLibrary.loadUserIndex (&err))
        postMessage (err, true);
    presets.loadFactory();
    for (const auto& problem : presets.rescanUser())
        postMessage ("Skipped damaged preset " + problem, true);

    engine.setOversamplingMode ((OversamplingMode) settings->getIntValue ("oversampling", 0));
    audioHost.setInputChannel (settings->getIntValue ("guitarInputChannel", 0));
    pcOffset = settings->getIntValue ("programChangeOffset", 0);
    midi.setChannel (settings->getIntValue ("midiChannel", 0));
    midi.setProgramChangeEnabled (settings->getBoolValue ("programChangeEnabled", true));
    if (settings->containsKey ("midiMappings"))
        midi.deserialise (settings->getValue ("midiMappings").toStdString());

    restoreSession();
    openAudioDevice();
    // Route the device's audio through ZackAMP's engine (AudioHost -> AmpForgeEngine).
    deviceManager.addAudioCallback (&audioHost);
    enableMidiInputs();
    midiListConnection = std::make_unique<juce::MidiDeviceListConnection> (
        juce::MidiDeviceListConnection::make ([this] { enableMidiInputs(); }));
    deviceManager.addChangeListener (this);
    deviceManager.addMidiInputDeviceCallback ({}, this);
    startTimerHz (20);
}

void AppController::shutdown()
{
    if (settings == nullptr)
        return;
    stopTimer();
    worker.removeAllJobs (true, 4000);
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeChangeListener (this);
    saveSession();
    if (auto xml = deviceManager.createStateXml())
        settings->setValue ("audioDeviceState", xml.get());
    saveMidiSettings();
    settings->saveIfNeeded();
    deviceManager.removeAudioCallback (&audioHost);
    deviceManager.closeAudioDevice();
    engine.collectGarbage();
    settings.reset();
}

//==============================================================================
void AppController::openAudioDevice()
{
    auto saved = settings->getXmlValue ("audioDeviceState");
    juce::String error;
    if (saved != nullptr)
    {
        // Do NOT fall back to the system default device if the saved interface is missing:
        // the default is often a built-in microphone + speakers (feedback risk).
        error = deviceManager.initialise (2, 2, saved.get(), false);
        if (error.isNotEmpty() || deviceManager.getCurrentAudioDevice() == nullptr)
            postMessage ("Your audio interface is not available (" + (error.isNotEmpty() ? error : juce::String ("not connected"))
                             + "). Connect it, or choose a device in Audio Settings.", true);
    }
    else
    {
        // First launch: outputs only until the user picks the guitar input (see onboarding).
        error = deviceManager.initialise (0, 2, nullptr, true);
        if (error.isNotEmpty())
            postMessage ("Could not open an audio device: " + error, true);
    }
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        hadDevice = true;
        lastDeviceName = dev->getName();
    }
    ensureInputChannelsActive();
    if (saved != nullptr && ! hasInputDevice())
        postMessage ("No guitar input is active. Open Audio Settings to choose your interface input.", true);
}

bool AppController::ensureInputChannelsActive()
{
    // Choosing an input device must actually switch its inputs on. The first launch opens
    // outputs only (so a built-in microphone is never opened by surprise), which means a
    // device chosen afterwards would otherwise start with zero active input channels.
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup (setup);
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr || setup.inputDeviceName.isEmpty() || ! setup.inputChannels.isZero())
        return false;
    const int available = dev->getInputChannelNames().size();
    if (available <= 0 || autoEnabledInputDevice == setup.inputDeviceName)
        return false; // nothing to enable, or already tried for this device (no loops)
    autoEnabledInputDevice = setup.inputDeviceName;
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, std::min (2, available), true);
    const auto error = deviceManager.setAudioDeviceSetup (setup, true);
    if (error.isNotEmpty())
    {
        postMessage ("Could not enable the inputs of " + setup.inputDeviceName + ": " + error, true);
        return false;
    }
    postMessage ("Enabled the inputs of " + setup.inputDeviceName + ". If your guitar is on another input, choose it in Audio Settings.");
    return true;
}

void AppController::enableMidiInputs()
{
    // Enable every MIDI input by default so footswitches work without configuration;
    // devices the user explicitly disabled stay disabled.
    const auto disabled = juce::StringArray::fromTokens (settings->getValue ("midiDisabledInputs"), "\n", "");
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        if (! disabled.contains (d.identifier) && ! deviceManager.isMidiInputDeviceEnabled (d.identifier))
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
}

void AppController::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    midi.handleMessage (m.getRawData(), m.getRawDataSize());
}

void AppController::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (ensureInputChannelsActive())
        return; // the device restarts; another change message follows
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        if (! hadDevice || dev->getName() != lastDeviceName)
            postMessage ("Audio device: " + dev->getName());
        hadDevice = true;
        lastDeviceName = dev->getName();
        if (auto xml = deviceManager.createStateXml())
            settings->setValue ("audioDeviceState", xml.get());
        // Remember which MIDI inputs the user switched off.
        juce::StringArray disabled;
        for (const auto& d : juce::MidiInput::getAvailableDevices())
            if (! deviceManager.isMidiInputDeviceEnabled (d.identifier))
                disabled.add (d.identifier);
        settings->setValue ("midiDisabledInputs", disabled.joinIntoString ("\n"));
    }
    else if (hadDevice)
    {
        hadDevice = false;
        postMessage ("Audio device disconnected. Z.AMP will reconnect when it returns, or choose another device in Audio Settings.", true);
        reconnectCountdown = 40;
    }
    sendChangeMessage();
}

void AppController::timerCallback()
{
    followModelChange();

    const int program = midi.takePendingProgram();
    if (program >= 0)
    {
        const int index = program + pcOffset;
        if (index >= 0 && index < (int) presets.getPresets().size())
            loadPreset (index);
        else
            postMessage ("Program Change " + juce::String (program) + " has no preset assigned.", true);
    }

    const auto err = audioHost.takeLastError();
    if (err.isNotEmpty())
        postMessage ("Audio error: " + err, true);

    // Reconnect a vanished interface without ever substituting the default device.
    if (! hadDevice && deviceManager.getCurrentAudioDevice() == nullptr && settings->containsKey ("audioDeviceState"))
    {
        if (--reconnectCountdown <= 0)
        {
            reconnectCountdown = 40; // every ~2 s
            if (auto* type = deviceManager.getCurrentDeviceTypeObject())
                type->scanForDevices();
            if (auto saved = settings->getXmlValue ("audioDeviceState"))
            {
                const auto name = saved->getStringAttribute ("audioOutputDeviceName");
                bool present = false;
                if (auto* type = deviceManager.getCurrentDeviceTypeObject())
                    present = type->getDeviceNames (false).contains (name);
                if (present && deviceManager.initialise (2, 2, saved.get(), false).isEmpty()
                    && deviceManager.getCurrentAudioDevice() != nullptr)
                    postMessage ("Audio device reconnected.");
            }
        }
    }

    engine.collectGarbage();
}

//==============================================================================
void AppController::setParam (int index, float value) { engine.params().set (index, value); }

void AppController::setParamWithUndo (int index, float value)
{
    const float old = engine.params().get (index);
    if (sanitiseParamValue (index, value) == old)
        return;
    undo.beginNewTransaction (juce::String (paramDef (index).name));
    undo.perform (new ParamAction (engine.params(), index, old, value));
}

void AppController::setParamsWithUndo (const std::vector<std::pair<int, float>>& changes, const juce::String& name)
{
    undo.beginNewTransaction (name);
    for (const auto& [index, value] : changes)
    {
        const float old = engine.params().get (index);
        if (sanitiseParamValue (index, value) != old)
            undo.perform (new ParamAction (engine.params(), index, old, value));
    }
}

void AppController::selectAmpModel (int model)
{
    if (engine.params().getInt (pAmpModel) != model)
        setParamWithUndo (pAmpModel, (float) model); // follow-ups happen in followModelChange()
}

void AppController::followModelChange()
{
    // A model change made by the user (knob, MIDI, undo...) resets the valves to the new
    // model's usual pair; the cabinet stays as it is. Preset loads set their own valves,
    // so they are excluded via expectedModel / busy jobs.
    const int model = engine.params().getInt (pAmpModel);
    if (model == lastSeenModel)
        return;
    const bool first = lastSeenModel < 0;
    lastSeenModel = model;
    if (first || busyJobs.load() > 0 || expectedModel.exchange (-1) == model)
        return;
    const auto& v = dsp::getAmpVoicing (model);
    engine.params().set (pAmpPreampValve, (float) v.defaultPreampValve);
    engine.params().set (pAmpPowerValve, (float) v.defaultPowerValve);
}

void AppController::beginGesture (int index) { gestureStart[index] = engine.params().get (index); }

void AppController::endGesture (int index)
{
    auto it = gestureStart.find (index);
    if (it == gestureStart.end())
        return;
    const float from = it->second;
    const float to = engine.params().get (index);
    gestureStart.erase (it);
    if (from != to)
    {
        undo.beginNewTransaction (juce::String (paramDef (index).name));
        undo.perform (new ParamAction (engine.params(), index, from, to));
    }
}

//==============================================================================
juce::String AppController::getCurrentPresetName() const
{
    if (currentPreset >= 0 && currentPreset < (int) presets.getPresets().size())
        return presets.getPresets()[(size_t) currentPreset].state.name;
    return loadedState.name;
}

bool AppController::isModified()
{
    const auto counter = engine.params().getChangeCounter();
    const auto sig = irSignature (engine);
    if (counter != lastChangeCounter || sig != lastIrSignature)
    {
        lastChangeCounter = counter;
        lastIrSignature = sig;
        modifiedCache = ! captureState (engine).sameSoundAs (loadedState);
    }
    return modifiedCache;
}

void AppController::markLoaded (int index, const PresetState& state)
{
    lastSeenModel = (int) state.values[(size_t) pAmpModel];
    expectedModel = -1;
    currentPreset = index;
    loadedState = state;
    lastChangeCounter = 0;
    lastIrSignature.clear();
    undo.clearUndoHistory();
    settings->setValue ("lastPresetName", state.name);
    settings->setValue ("lastPresetFactory", index >= 0 && presets.getPresets()[(size_t) index].isFactory);
    sendChangeMessage();
}

void AppController::runOnWorker (std::function<void()> job)
{
    ++busyJobs;
    worker.addJob ([this, job = std::move (job)] {
        job();
        --busyJobs;
    });
}

void AppController::loadPreset (int index)
{
    if (index < 0 || index >= (int) presets.getPresets().size())
        return;
    const auto state = presets.getPresets()[(size_t) index].state;
    juce::WeakReference<AppController> weak (this);
    runOnWorker ([this, weak, state, index] {
        juce::StringArray warnings;
        expectedModel = (int) state.values[(size_t) pAmpModel];
        applyState (engine, irLibrary, state, warnings);
        juce::MessageManager::callAsync ([weak, state, index, warnings] {
            if (auto* self = weak.get())
            {
                // Remember what actually got loaded (fallback IRs differ from the file).
                auto actual = state;
                const auto cap = captureState (self->engine);
                actual.irA = cap.irA;
                actual.irB = cap.irB;
                self->markLoaded (index, actual);
                for (const auto& w : warnings)
                    self->postMessage (w, true);
            }
        });
    });
}

void AppController::stepPreset (int delta)
{
    const int n = (int) presets.getPresets().size();
    if (n == 0)
        return;
    const int base = currentPreset < 0 ? (delta > 0 ? -1 : 0) : currentPreset;
    loadPreset (((base + delta) % n + n) % n);
}

bool AppController::saveCurrent (juce::String& error)
{
    if (currentPreset < 0 || presets.getPresets()[(size_t) currentPreset].isFactory)
    {
        error = "Factory presets are read-only. Use Save As to store your version.";
        return false;
    }
    auto s = captureState (engine);
    const auto& cur = presets.getPresets()[(size_t) currentPreset].state;
    s.name = cur.name;
    s.category = cur.category;
    s.description = cur.description;
    s.author = cur.author;
    int idx = -1;
    if (! presets.saveUser (s, true, error, &idx))
        return false;
    markLoaded (idx, s);
    postMessage ("Saved \"" + s.name + "\"");
    return true;
}

bool AppController::saveAs (const juce::String& name, const juce::String& category, const juce::String& description, juce::String& error)
{
    auto s = captureState (engine);
    s.name = name.trim();
    s.category = category.isNotEmpty() ? category : juce::String ("User");
    s.description = description;
    s.author = "User";
    int idx = -1;
    if (! presets.saveUser (s, false, error, &idx))
        return false;
    markLoaded (idx, s);
    postMessage ("Saved \"" + s.name + "\"");
    return true;
}

bool AppController::renamePreset (int index, const juce::String& name, juce::String& error)
{
    const bool wasCurrent = index == currentPreset;
    if (! presets.renameUser (index, name, error))
        return false;
    if (wasCurrent)
    {
        currentPreset = presets.findIndex (name, false);
        loadedState.name = name;
    }
    sendChangeMessage();
    return true;
}

bool AppController::deletePreset (int index, juce::String& error)
{
    const auto name = presets.getPresets()[(size_t) juce::jlimit (0, (int) presets.getPresets().size() - 1, index)].state.name;
    const auto curName = getCurrentPresetName();
    const bool curFactory = currentPreset >= 0 && presets.getPresets()[(size_t) currentPreset].isFactory;
    if (! presets.deleteUser (index, error))
        return false;
    // The sound keeps playing; it just no longer has a saved preset behind it.
    currentPreset = index == currentPreset ? -1 : presets.findIndex (curName, curFactory);
    postMessage ("Deleted \"" + name + "\" (moved to the Trash where available)");
    sendChangeMessage();
    return true;
}

bool AppController::duplicatePreset (int index, juce::String& error)
{
    const auto curName = getCurrentPresetName();
    const bool curFactory = currentPreset >= 0 && presets.getPresets()[(size_t) currentPreset].isFactory;
    int newIndex = -1;
    if (! presets.duplicate (index, error, &newIndex))
        return false;
    currentPreset = presets.findIndex (curName, curFactory);
    sendChangeMessage();
    return true;
}

void AppController::resetToSafeSettings()
{
    PresetState s; // all defaults: clean amp, no effects, cab on
    s.name = "Safe Default";
    s.values[(size_t) pOutLevel] = -12.0f;
    s.values[(size_t) pAmpGain] = 0.3f;
    s.values[(size_t) pAmpMaster] = 0.4f;
    s.irA = defaultIrForModel (0);
    juce::WeakReference<AppController> weak (this);
    runOnWorker ([this, weak, s] {
        juce::StringArray warnings;
        expectedModel = (int) s.values[(size_t) pAmpModel];
        applyState (engine, irLibrary, s, warnings);
        juce::MessageManager::callAsync ([weak, s] {
            if (auto* self = weak.get())
            {
                self->markLoaded (-1, s);
                self->postMessage ("Reset to safe settings (clean amp, output -12 dB, effects off).");
            }
        });
    });
}

//==============================================================================
void AppController::setCabinetIR (int slot, const juce::String& irId)
{
    juce::WeakReference<AppController> weak (this);
    runOnWorker ([this, weak, slot, irId] {
        juce::String err;
        auto ir = irLibrary.load (irId, err);
        std::string engineErr;
        const bool ok = ir != nullptr && engine.setCabinetIR (slot, ir, &engineErr);
        if (! ok && err.isEmpty())
            err = engineErr;
        juce::MessageManager::callAsync ([weak, ok, err] {
            if (auto* self = weak.get())
            {
                if (! ok)
                    self->postMessage ("Could not load IR: " + err, true);
                else if (self->engine.getCabinetInfo (0).truncated || self->engine.getCabinetInfo (1).truncated)
                    self->postMessage ("Long IR truncated to 1 s for real-time use.", true);
                self->sendChangeMessage();
            }
        });
    });
}

juce::String AppController::getCabinetIrId (int slot) const { return engine.getCabinetInfo (slot).irId; }

void AppController::resetCabinet()
{
    setCabinetIR (0, defaultIrForModel (engine.params().getInt (pAmpModel)));
    engine.clearCabinetIR (1);
    for (int p : { (int) pCabOn, (int) pCabDual, (int) pCabALevel, (int) pCabAPan, (int) pCabBLevel, (int) pCabBPan })
        engine.params().set (p, paramDef (p).def);
}

IRLibrary::ImportResult AppController::importIR (const juce::File& file, bool copyIntoLibrary)
{
    auto r = irLibrary.importFile (file, copyIntoLibrary);
    if (r.ok)
        postMessage ("Imported IR \"" + r.entry.name + "\"" + (r.entry.isCopy ? " (copied into your library)" : " (referenced in place)"));
    for (const auto& w : r.warnings)
        postMessage (w, true);
    sendChangeMessage();
    return r;
}

bool AppController::removeIR (const juce::String& id, juce::String& error)
{
    const bool ok = irLibrary.removeEntry (id, &error);
    sendChangeMessage();
    return ok;
}

//==============================================================================
void AppController::setOversampling (OversamplingMode m)
{
    engine.setOversamplingMode (m);
    settings->setValue ("oversampling", (int) m);
    // Re-prepare through the device so the change happens with audio safely stopped.
    if (deviceManager.getCurrentAudioDevice() != nullptr)
    {
        deviceManager.closeAudioDevice();
        deviceManager.restartLastAudioDevice();
    }
}

void AppController::restartAudio()
{
    if (deviceManager.getCurrentAudioDevice() != nullptr)
    {
        deviceManager.closeAudioDevice();
        deviceManager.restartLastAudioDevice();
        postMessage ("Audio input access granted - audio restarted.");
    }
}

void AppController::setGuitarInputChannel (int index)
{
    audioHost.setInputChannel (index);
    settings->setValue ("guitarInputChannel", index);
}

juce::StringArray AppController::getActiveInputChannelNames() const
{
    juce::StringArray names;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        const auto all = dev->getInputChannelNames();
        const auto active = dev->getActiveInputChannels();
        for (int i = 0; i < all.size(); ++i)
            if (active[i])
                names.add (all[i]);
    }
    return names;
}

void AppController::setProgramChangeOffset (int offset)
{
    pcOffset = offset;
    settings->setValue ("programChangeOffset", offset);
}

void AppController::saveMidiSettings()
{
    settings->setValue ("midiMappings", juce::String (midi.serialise()));
    settings->setValue ("midiChannel", midi.getChannel());
    settings->setValue ("programChangeEnabled", midi.isProgramChangeEnabled());
}

bool AppController::needsOnboarding() const
{
    return ! settings->getBoolValue ("onboardingDone", false);
}

void AppController::setOnboardingDone() { settings->setValue ("onboardingDone", true); }

bool AppController::hasInputDevice() const
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    return dev != nullptr && dev->getActiveInputChannels().countNumberOfSetBits() > 0;
}

juce::String AppController::getDeviceSummary() const
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr)
        return "No audio device";
    const double sr = dev->getCurrentSampleRate();
    const int bs = dev->getCurrentBufferSizeSamples();
    const int inLat = dev->getInputLatencyInSamples(), outLat = dev->getOutputLatencyInSamples();
    juce::String s;
    s << dev->getName() << "  |  " << juce::String (sr / 1000.0, 1) << " kHz  |  " << bs << " smp ("
      << juce::String (1000.0 * bs / sr, 1) << " ms)";
    if (inLat + outLat > 0)
        s << "  |  driver-reported I/O latency " << juce::String (1000.0 * (inLat + outLat + 2 * bs) / sr, 1) << " ms";
    return s;
}

//==============================================================================
void AppController::postMessage (const juce::String& text, bool warning)
{
    std::lock_guard<std::mutex> l (messageLock);
    messages.push_back ({ text, warning });
    if (messages.size() > 50)
        messages.erase (messages.begin());
}

std::vector<StatusMessage> AppController::takeMessages()
{
    std::lock_guard<std::mutex> l (messageLock);
    auto m = std::move (messages);
    messages.clear();
    return m;
}

void AppController::saveSession()
{
    // The exact current sound (even unsaved edits) is restored on next launch.
    auto s = captureState (engine);
    s.name = getCurrentPresetName();
    settings->setValue ("sessionState", PresetCodec::toJson (s));
    settings->setValue ("sessionModified", isModified());
}

void AppController::restoreSession()
{
    PresetState s;
    juce::String err;
    juce::StringArray warnings;
    const bool haveSession = settings->containsKey ("sessionState")
                             && PresetCodec::fromJson (settings->getValue ("sessionState"), s, err, warnings);
    if (! haveSession)
    {
        // First launch: start on the first factory preset (a clean, moderate-level sound).
        if (! presets.getPresets().empty())
        {
            juce::StringArray w;
            const auto& p = presets.getPresets()[0];
            applyState (engine, irLibrary, p.state, w);
            markLoaded (0, p.state);
        }
        return;
    }
    juce::StringArray applyWarnings;
    applyState (engine, irLibrary, s, applyWarnings);
    for (const auto& w : applyWarnings)
        postMessage (w, true);
    const int idx = presets.findIndex (settings->getValue ("lastPresetName"), settings->getBoolValue ("lastPresetFactory"));
    markLoaded (idx, idx >= 0 ? presets.getPresets()[(size_t) idx].state : s);
}
} // namespace af

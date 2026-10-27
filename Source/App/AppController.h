// AMP FORGE — application controller: owns the engine and every non-UI service, and is the
// single place where UI actions become engine/preset/IR/MIDI operations.
//
// Threads:
//   message thread : UI, settings, preset list edits, IR import, MIDI program-change handling
//   worker thread  : preset application and IR decoding/preparation (file I/O, resampling)
//   device thread  : AudioHost callbacks -> engine.process()
//   MIDI thread    : MidiController::handleMessage() (atomics only)
#pragma once

#include "AudioHost.h"
#include "../IR/IRLibrary.h"
#include "../MIDI/MidiController.h"
#include "../Presets/PresetApplier.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

namespace af
{
struct StatusMessage
{
    juce::String text;
    bool isWarning = false;
};

class AppController final : public juce::ChangeBroadcaster,
                            private juce::Timer,
                            private juce::ChangeListener,
                            private juce::MidiInputCallback
{
public:
    AppController();
    ~AppController() override;

    void initialise();
    void shutdown();

    // ---- services ----
    AmpForgeEngine engine;
    juce::AudioDeviceManager deviceManager;
    MidiController midi { engine.params() };
    IRLibrary irLibrary;
    PresetManager presets;
    juce::UndoManager undo;

    ParameterStore& params() noexcept { return engine.params(); }

    // ---- parameter editing with undo ----
    void setParam (int index, float value);              // immediate, no undo record
    void setParamWithUndo (int index, float value);      // discrete change (toggle, combo)
    void setParamsWithUndo (const std::vector<std::pair<int, float>>& changes, const juce::String& name); // one undo step
    void beginGesture (int index);
    void endGesture (int index);

    /** Switches amp model. If cabinet A still holds the previous model's default IR, the
        cabinet follows to the new model's default (a customised cabinet is left alone). */
    void selectAmpModel (int model);

    // ---- presets ----
    int getCurrentPresetIndex() const noexcept { return currentPreset; }
    juce::String getCurrentPresetName() const;
    bool isModified();
    void loadPreset (int index);
    void stepPreset (int delta);
    bool saveCurrent (juce::String& error);  // overwrites the current *user* preset
    bool saveAs (const juce::String& name, const juce::String& category, const juce::String& description, juce::String& error);
    bool renamePreset (int index, const juce::String& name, juce::String& error);
    bool deletePreset (int index, juce::String& error);
    bool duplicatePreset (int index, juce::String& error);
    void resetToSafeSettings();

    // ---- cabinet IRs ----
    void setCabinetIR (int slot, const juce::String& irId);
    juce::String getCabinetIrId (int slot) const;
    void resetCabinet();
    IRLibrary::ImportResult importIR (const juce::File& file, bool copyIntoLibrary);
    bool removeIR (const juce::String& id, juce::String& error);

    // ---- audio / MIDI settings ----
    void setOversampling (OversamplingMode m);
    OversamplingMode getOversampling() const noexcept { return engine.getOversamplingMode(); }
    void setGuitarInputChannel (int indexAmongActive);
    int getGuitarInputChannel() const noexcept { return audioHost.getInputChannel(); }
    juce::StringArray getActiveInputChannelNames() const;
    void setProgramChangeOffset (int offset);
    int getProgramChangeOffset() const noexcept { return pcOffset; }
    void saveMidiSettings();

    bool needsOnboarding() const;
    void setOnboardingDone();

    // ---- status ----
    std::vector<StatusMessage> takeMessages();
    void postMessage (const juce::String& text, bool warning = false);
    juce::String getDeviceSummary() const;
    bool hasInputDevice() const;
    bool isBusy() const noexcept { return busyJobs.load() > 0; }

    juce::PropertiesFile& getSettings() { return *settings; }

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    void openAudioDevice();
    void enableMidiInputs();
    void saveSession();
    void restoreSession();
    void runOnWorker (std::function<void()> job);
    void markLoaded (int index, const PresetState& state);

    AudioHost audioHost { engine };
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::ThreadPool worker { 1 };
    std::atomic<int> busyJobs { 0 };

    int currentPreset = -1;
    PresetState loadedState;
    uint32_t lastChangeCounter = 0;
    bool modifiedCache = false;
    juce::String lastIrSignature;

    std::map<int, float> gestureStart;
    int pcOffset = 0;
    bool hadDevice = false;
    juce::String lastDeviceName;
    int reconnectCountdown = 0;
    std::unique_ptr<juce::MidiDeviceListConnection> midiListConnection;

    std::mutex messageLock;
    std::vector<StatusMessage> messages;

    JUCE_DECLARE_WEAK_REFERENCEABLE (AppController)
};
} // namespace af

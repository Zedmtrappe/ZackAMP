// ZackAMP — standalone audio-device integration. Standalone-only: plugin wrappers will
// feed the engine from the host's process callback instead.
#pragma once

#include "../Audio/AmpForgeEngine.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <mutex>

namespace af
{
class AudioHost final : public juce::AudioIODeviceCallback
{
public:
    explicit AudioHost (AmpForgeEngine& e) : engine (e) {}

    /** Index of the guitar input among the device's *active* input channels. */
    void setInputChannel (int index) noexcept { inputChannel.store (std::max (0, index)); }
    int getInputChannel() const noexcept { return inputChannel.load(); }

    bool isRunning() const noexcept { return running.load(); }
    juce::String takeLastError();

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numIn, float* const* outputs, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& message) override;

private:
    AmpForgeEngine& engine;
    std::atomic<int> inputChannel { 0 };
    std::atomic<bool> running { false };
    std::vector<float> silence, scratchL, scratchR; // sized in audioDeviceAboutToStart
    std::mutex errorLock;
    juce::String lastError;
};
} // namespace af

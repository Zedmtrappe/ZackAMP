#include "AudioHost.h"

namespace af
{
void AudioHost::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // Device thread, before callbacks start: safe to allocate and prepare.
    const int bufferSize = std::max (device->getCurrentBufferSizeSamples(), 16);
    const size_t capacity = (size_t) std::max (bufferSize * 2, 8192);
    silence.assign (capacity, 0.0f);
    scratchL.assign (capacity, 0.0f);
    scratchR.assign (capacity, 0.0f);
    engine.prepare (device->getCurrentSampleRate(), bufferSize);
    running = true;
}

void AudioHost::audioDeviceStopped()
{
    running = false;
    engine.releaseResources();
}

void AudioHost::audioDeviceError (const juce::String& message)
{
    std::lock_guard<std::mutex> l (errorLock);
    lastError = message;
}

juce::String AudioHost::takeLastError()
{
    std::lock_guard<std::mutex> l (errorLock);
    auto e = lastError;
    lastError.clear();
    return e;
}

void AudioHost::audioDeviceIOCallbackWithContext (const float* const* inputs, int numIn, float* const* outputs,
                                                  int numOut, int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    // Real-time thread: no allocation, no locks, no logging.
    if (numSamples <= 0)
        return;
    callbacks.fetch_add (1, std::memory_order_relaxed);
    if ((size_t) numSamples > scratchL.size())
    {
        // Never expected (the device promised a smaller buffer), but never overrun either.
        for (int c = 0; c < numOut; ++c)
            if (outputs[c] != nullptr)
                juce::FloatVectorOperations::clear (outputs[c], numSamples);
        return;
    }

    const float* in = silence.data();
    if (numIn > 0)
    {
        const int ch = std::min (inputChannel.load (std::memory_order_relaxed), numIn - 1);
        if (inputs[ch] != nullptr)
            in = inputs[ch];
    }

    float* l = (numOut > 0 && outputs[0] != nullptr) ? outputs[0] : scratchL.data();
    float* r = (numOut > 1 && outputs[1] != nullptr) ? outputs[1] : scratchR.data();
    engine.process (in, l, r, numSamples);

    if (numOut == 1 && outputs[0] != nullptr)
    {
        // Single output: mono sum of the stereo effects.
        for (int i = 0; i < numSamples; ++i)
            outputs[0][i] = 0.5f * (outputs[0][i] + scratchR[(size_t) i]);
    }
    for (int c = 2; c < numOut; ++c)
        if (outputs[c] != nullptr)
            juce::FloatVectorOperations::clear (outputs[c], numSamples);
}
} // namespace af

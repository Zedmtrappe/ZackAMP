// ZackAMP — the shared, framework-independent audio engine.
//
// This is the only object a host integration (standalone app today, AU/VST3 later) needs:
//
//   prepare(sampleRate, maxBlock)      non-RT, audio stopped
//   process(in, outL, outR, n)         audio thread; any n >= 0, any call pattern
//   params()                           lock-free parameter store (any thread)
//   setCabinetIR(slot, ir)             non-RT, any time (builds off the audio thread)
//   collectGarbage()                   non-RT, periodically (frees retired objects)
//   beginTransition()/endTransition()  non-RT, click-free preset changes
//
// Signal flow (fixed order, see docs/DSP_DESIGN.md):
//   input trim -> [gate detector] -> upsample -> [drive] -> preamp -> [EQ pre-power]
//   -> power amp -> downsample -> [EQ post-amp] -> gate gain -> cabinet (mono->stereo)
//   -> micro-pitch -> chorus -> delay -> reverb -> output level -> DC block -> safety limiter
//
// The audio thread never allocates, locks, logs or touches files. Internally it works in
// control blocks of at most kControlBlock samples, so host block size is irrelevant.
#pragma once

#include "../DSP/Amplifiers/AmpModel.h"
#include "../DSP/Cabinet/CabinetSection.h"
#include "../DSP/Effects/MicroPitch.h"
#include "../DSP/Effects/ModulationEffects.h"
#include "../DSP/Effects/NoiseGate.h"
#include "../DSP/Effects/OutputLimiter.h"
#include "../DSP/Effects/Overdrive.h"
#include "../DSP/Effects/Reverb.h"
#include "../DSP/Effects/Smooth.h"
#include "../DSP/GraphicEQ/GraphicEQ.h"
#include "../DSP/Utilities/Oversampler.h"
#include "../Parameters/Parameters.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace af
{
enum class OversamplingMode : int { automatic = 0, x2 = 1, x4 = 2, x8 = 3 };

struct EngineMeters
{
    float inputPeak = 0.0f;   // raw interface input (before trim), linear
    float outputPeakL = 0.0f, outputPeakR = 0.0f;
    float limiterReduction = 0.0f;
    bool gateOpen = true;
    bool inputClipped = false;
};

struct CabinetSlotInfo
{
    std::string irId, displayName;
    bool loaded = false;
    bool truncated = false;
    std::string error;
};

class AmpForgeEngine
{
public:
    static constexpr int kControlBlock = 32;
    /** Amp-input units per digital full scale (0 dBFS ~= 2 V into a 1.5 V-headroom triode). */
    static constexpr float kInputScale = 2.0f / 1.5f;

    AmpForgeEngine();
    ~AmpForgeEngine();

    // ---- lifecycle (non-RT) ----
    void prepare (double sampleRate, int maxBlockSize);
    void releaseResources();
    bool isPrepared() const noexcept { return prepared.load(); }
    double getSampleRate() const noexcept { return sampleRate; }
    int getOversamplingFactor() const noexcept { return oversampler.getFactor(); }
    void setOversamplingMode (OversamplingMode m); // takes effect at the next prepare()
    OversamplingMode getOversamplingMode() const noexcept { return osMode; }

    // ---- audio thread ----
    /** Mono guitar input -> stereo output. `in` may alias outL (in-place processing is
        supported and tested); outR may be null for mono output. */
    void process (const float* in, float* outL, float* outR, int numSamples) noexcept;

    // ---- parameters (any thread) ----
    ParameterStore& params() noexcept { return store; }
    const ParameterStore& params() const noexcept { return store; }

    // ---- cabinet IRs (non-RT) ----
    /** Prepares the IR for the current sample rate and hands it to the audio thread.
        Thread-safe w.r.t. other non-RT calls. Returns false (and keeps the old IR) on failure. */
    bool setCabinetIR (int slot, std::shared_ptr<const dsp::IRData> ir, std::string* error = nullptr);
    void clearCabinetIR (int slot);
    CabinetSlotInfo getCabinetInfo (int slot) const;
    std::shared_ptr<const dsp::IRData> getCabinetIR (int slot) const;

    /** Non-RT: frees objects the audio thread has finished with. Call every ~100–500 ms. */
    void collectGarbage() { garbage.collect(); }

    // ---- click-free state changes (non-RT) ----
    /** Asks the audio thread to fade out; waits up to timeoutMs for it. Returns true if muted
        (false if audio is not running — changes can then be applied directly anyway). */
    bool beginTransition (int timeoutMs = 60);
    /** Releases the mute; the audio thread snaps smoothers to the new values and fades in. */
    void endTransition();

    // ---- metering (any thread; read-and-reset peak hold) ----
    EngineMeters readMeters() noexcept;

    /** Group delay of the oversampling filters at low frequencies (samples, informational). */
    double getProcessingLatencySamples() const noexcept;

    /** Current gain reduction of the Smooth (2 kHz) compressor in dB, for its "process" light. */
    float getSmoothReductionDb() const noexcept { return smooth.getReductionDb(); }

private:
    struct Smoothed
    {
        dsp::LinearSmoother s;
        float from = 0.0f, to = 0.0f;
        void prepare (double rate, double secs) { s.prepare (rate, secs); }
        void target (float v) noexcept { s.setTarget (v); }
        void advance (int n) noexcept { from = to; to = s.skip (n); }
        void snap (float v) noexcept { s.snap (v); from = to = v; }
        float at (int i, int n) const noexcept { return from + (to - from) * (float) (i + 1) / (float) n; }
    };

    void processChunk (const float* in, float* outL, float* outR, int n) noexcept;
    void updateControls (int n) noexcept;
    void snapAll() noexcept;
    void resetDsp() noexcept;
    void prepareAmp (int model);
    std::unique_ptr<dsp::Convolver> buildConvolver (const dsp::IRData& ir, double rate, bool& truncated, std::string& err) const;

    ParameterStore store;
    dsp::GarbageCollector garbage;

    double sampleRate = 48000.0, osRate = 192000.0;
    OversamplingMode osMode = OversamplingMode::automatic;
    std::atomic<bool> prepared { false };

    dsp::Oversampler oversampler;
    std::array<dsp::AmpModel, dsp::kNumAmpModels> amps;
    int activeModel = 0, activePreValve = 0, activePowValve = 0;
    enum class ModelFade { none, out, in } modelFade = ModelFade::none;
    float modelFadeGain = 1.0f, modelFadeStep = 0.0f;

    dsp::Overdrive drive;
    dsp::NoiseGate gate;
    dsp::GraphicEQ eqPre, eqPost;
    dsp::CabinetSection cabinet { garbage };
    dsp::MicroPitch pitch;
    dsp::Smooth smooth;
    int activeDriveType = 0;
    dsp::Chorus chorus;
    dsp::StereoDelay delay;
    dsp::Reverb reverb;
    dsp::OutputLimiter limiter;
    dsp::DCBlocker dcL, dcR;

    // smoothed controls
    Smoothed inTrim, outGain, bypassMix, driveMix, eqPreMix, eqPostMix, gateMix;
    Smoothed chorusMix, pitchMix, delayIn, reverbIn, smoothMix;
    dsp::LinearSmoother ampGain, ampBass, ampMid, ampTreble, ampPresence, ampDepth, ampMaster;
    dsp::LinearSmoother ampPull, powerSag, powerVariac;
    dsp::LinearSmoother driveDrive, driveTone, driveLevel, chorusRate, chorusDepth, delayFb, delayMix, reverbMix;
    std::array<dsp::LinearSmoother, 5> eqBands;

    // transition (soft mute) state
    std::atomic<bool> muteRequest { false }, muted { false };
    float muteGain = 1.0f, muteStep = 0.0f;
    bool wasMuted = false;

    // scratch (allocated in prepare)
    std::vector<float> chunkIn, base, dryBuf, gateGain, cabL, cabR, discardR;

    // meters
    std::atomic<float> mInPeak { 0 }, mOutL { 0 }, mOutR { 0 }, mLimiter { 0 };
    std::atomic<bool> mGateOpen { true };

    // non-RT state
    mutable std::mutex nonRtMutex;
    std::array<std::shared_ptr<const dsp::IRData>, dsp::CabinetSection::kNumSlots> irSources;
    std::array<CabinetSlotInfo, dsp::CabinetSection::kNumSlots> irInfo;
};

} // namespace af

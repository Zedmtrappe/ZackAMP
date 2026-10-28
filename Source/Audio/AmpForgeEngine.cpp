#include "AmpForgeEngine.h"

#include <chrono>
#include <complex>
#include <cstring>
#include <thread>

namespace af
{
using namespace dsp;

namespace
{
    void atomicMax (std::atomic<float>& a, float v) noexcept
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }
} // namespace

AmpForgeEngine::AmpForgeEngine() = default;

AmpForgeEngine::~AmpForgeEngine() { garbage.collect(); }

void AmpForgeEngine::setOversamplingMode (OversamplingMode m)
{
    std::lock_guard<std::mutex> lock (nonRtMutex);
    osMode = m;
}

void AmpForgeEngine::prepare (double newSampleRate, int /*maxBlockSize: irrelevant, we work in control blocks*/)
{
    std::lock_guard<std::mutex> lock (nonRtMutex);
    prepared = false;
    sampleRate = newSampleRate > 0 ? newSampleRate : 48000.0;

    int factorLog2 = 0;
    if (osMode == OversamplingMode::automatic)
        // Chosen from measurements (Tests/Audio/AudioQualityTests.cpp, docs/DSP_DESIGN.md):
        // the processing rate is kept at >= ~350 kHz so high-gain alias products stay below
        // roughly -60 dB for lead-range fundamentals.
        factorLog2 = sampleRate <= 50000.0 ? 3 : (sampleRate <= 100000.0 ? 2 : 1);
    else
        factorLog2 = (int) osMode;
    oversampler.prepare (factorLog2, kControlBlock);
    osRate = sampleRate * oversampler.getFactor();

    for (int i = 0; i < kNumAmpModels; ++i)
        amps[(size_t) i].prepare (osRate, getAmpVoicing (i));
    activeModel = store.getInt (pAmpModel);
    activePreValve = store.getInt (pAmpPreampValve);
    activePowValve = store.getInt (pAmpPowerValve);
    amps[(size_t) activeModel].setValves (activePreValve, activePowValve);
    modelFade = ModelFade::none;
    modelFadeGain = 1.0f;
    modelFadeStep = (float) (1.0 / (0.012 * sampleRate));

    drive.prepare (osRate);
    gate.prepare (sampleRate);
    eqPre.prepare (osRate);
    eqPost.prepare (sampleRate);
    pitch.prepare (sampleRate);
    smooth.prepare (sampleRate);
    compPre.prepare (sampleRate);
    compPost.prepare (sampleRate);
    wah.prepare (sampleRate);
    chorus.prepare (sampleRate);
    delay.prepare (sampleRate);
    reverb.prepare (sampleRate);
    limiter.prepare (sampleRate);
    dcL.prepare (sampleRate, 6.0);
    dcR.prepare (sampleRate, 6.0);

    cabinet.prepare (sampleRate, kControlBlock);
    for (int s = 0; s < CabinetSection::kNumSlots; ++s)
    {
        if (irSources[(size_t) s] == nullptr)
            continue;
        bool truncated = false;
        std::string err;
        auto conv = buildConvolver (*irSources[(size_t) s], sampleRate, truncated, err);
        irInfo[(size_t) s].loaded = conv != nullptr;
        irInfo[(size_t) s].truncated = truncated;
        irInfo[(size_t) s].error = err;
        cabinet.installImmediately (s, std::move (conv));
    }

    const size_t osLen = (size_t) kControlBlock * (size_t) oversampler.getFactor();
    for (auto* v : { &chunkIn, &base, &dryBuf, &gateGain, &cabL, &cabR, &discardR })
        v->assign (std::max<size_t> (osLen, kControlBlock), 0.0f);

    for (auto* s : { &inTrim, &outGain })
        s->prepare (sampleRate, 0.03);
    for (auto* s : { &bypassMix, &driveMix, &eqPreMix, &eqPostMix, &gateMix, &chorusMix, &pitchMix, &delayIn, &reverbIn, &smoothMix, &compPreMix, &compPostMix, &wahMix })
        s->prepare (sampleRate, 0.02);
    for (auto* s : { &ampGain, &ampBass, &ampMid, &ampTreble, &ampPresence, &ampDepth, &ampMaster,
                     &driveDrive, &driveTone, &driveLevel, &chorusRate, &chorusDepth, &delayFb, &delayMix, &reverbMix })
        s->prepare (sampleRate, 0.04);
    ampPull.prepare (sampleRate, 0.03);
    powerSag.prepare (sampleRate, 0.1);
    powerVariac.prepare (sampleRate, 0.15);
    for (auto& s : eqBands)
        s.prepare (sampleRate, 0.05);

    muteStep = (float) (1.0 / (0.010 * sampleRate));
    muteGain = muteRequest.load() ? 0.0f : 1.0f;
    muted = muteRequest.load();

    snapAll();
    resetDsp();
    prepared = true;
}

void AmpForgeEngine::releaseResources()
{
    prepared = false;
    garbage.collect();
}

double AmpForgeEngine::getProcessingLatencySamples() const noexcept
{
    // Phase delay of the IIR half-band chain at 1 kHz, measured on a scratch copy.
    Oversampler os;
    const int factorLog2 = oversampler.getFactor() == 8 ? 3 : oversampler.getFactor() == 4 ? 2 : oversampler.getFactor() == 2 ? 1 : 0;
    if (factorLog2 == 0)
        return 0.0;
    const int n = 2048;
    os.prepare (factorLog2, n);
    std::vector<float> imp ((size_t) n, 0.0f), out ((size_t) n);
    imp[0] = 1.0f;
    float* up = os.upsample (imp.data(), n);
    os.downsample (up, out.data(), n);
    std::complex<double> acc = 0.0;
    const double w = kTwoPi * 1000.0 / sampleRate;
    for (int i = 0; i < n; ++i)
        acc += (double) out[(size_t) i] * std::polar (1.0, -w * i);
    double phase = -std::arg (acc);
    if (phase < 0)
        phase += kTwoPi;
    return phase / w;
}

std::unique_ptr<Convolver> AmpForgeEngine::buildConvolver (const IRData& ir, double rate, bool& truncated,
                                                           std::string& err) const
{
    try
    {
        IRPrepareOptions opts;
        auto chans = prepareImpulse (ir, rate, opts, &truncated);
        if (chans.empty() || chans[0].empty())
        {
            err = "Impulse response is empty.";
            return nullptr;
        }
        for (auto& ch : chans)
            for (float v : ch)
                if (! std::isfinite (v))
                {
                    err = "Impulse response contains invalid (non-finite) samples.";
                    return nullptr;
                }
        return std::make_unique<Convolver> (chans, rate);
    }
    catch (const std::bad_alloc&)
    {
        err = "Not enough memory to load this impulse response.";
    }
    catch (const std::exception& e)
    {
        err = e.what();
    }
    return nullptr;
}

bool AmpForgeEngine::setCabinetIR (int slot, std::shared_ptr<const IRData> ir, std::string* error)
{
    if (slot < 0 || slot >= CabinetSection::kNumSlots)
        return false;
    std::lock_guard<std::mutex> lock (nonRtMutex);
    std::string err;
    if (ir == nullptr || ir->getLength() == 0 || ir->getNumChannels() == 0)
        err = "Impulse response is empty.";

    std::unique_ptr<Convolver> conv;
    bool truncated = false;
    if (err.empty() && prepared)
        conv = buildConvolver (*ir, sampleRate, truncated, err);

    if (! err.empty())
    {
        if (error != nullptr)
            *error = err;
        return false;
    }

    irSources[(size_t) slot] = ir;
    auto& info = irInfo[(size_t) slot];
    info.irId = ir->id;
    info.displayName = ir->displayName;
    info.truncated = truncated;
    info.error.clear();
    info.loaded = true;
    if (conv != nullptr)
        cabinet.publish (slot, std::move (conv));
    garbage.collect();
    return true;
}

void AmpForgeEngine::clearCabinetIR (int slot)
{
    if (slot < 0 || slot >= CabinetSection::kNumSlots)
        return;
    std::lock_guard<std::mutex> lock (nonRtMutex);
    irSources[(size_t) slot] = nullptr;
    irInfo[(size_t) slot] = {};
    cabinet.publish (slot, nullptr);
}

CabinetSlotInfo AmpForgeEngine::getCabinetInfo (int slot) const
{
    std::lock_guard<std::mutex> lock (nonRtMutex);
    return irInfo[(size_t) std::clamp (slot, 0, CabinetSection::kNumSlots - 1)];
}

std::shared_ptr<const IRData> AmpForgeEngine::getCabinetIR (int slot) const
{
    std::lock_guard<std::mutex> lock (nonRtMutex);
    return irSources[(size_t) std::clamp (slot, 0, CabinetSection::kNumSlots - 1)];
}

bool AmpForgeEngine::beginTransition (int timeoutMs)
{
    muteRequest.store (true, std::memory_order_release);
    if (! prepared)
        return false;
    const auto start = std::chrono::steady_clock::now();
    while (! muted.load (std::memory_order_acquire))
    {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds (timeoutMs))
            return false; // audio not running (or stalled): caller applies changes anyway
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    return true;
}

void AmpForgeEngine::endTransition() { muteRequest.store (false, std::memory_order_release); }

EngineMeters AmpForgeEngine::readMeters() noexcept
{
    EngineMeters m;
    m.inputPeak = mInPeak.exchange (0.0f);
    m.outputPeakL = mOutL.exchange (0.0f);
    m.outputPeakR = mOutR.exchange (0.0f);
    m.limiterReduction = mLimiter.exchange (0.0f);
    m.gateOpen = mGateOpen.load();
    m.inputClipped = m.inputPeak >= 0.999f;
    return m;
}

//==============================================================================
void AmpForgeEngine::snapAll() noexcept
{
    updateControls (kControlBlock);
    const auto& p = store;
    inTrim.snap (dbToGain (p.get (pInTrim)));
    outGain.snap (dbToGain (p.get (pOutLevel)));
    bypassMix.snap (p.getBool (pBypass) ? 1.0f : 0.0f);
    driveMix.snap (p.getBool (pDriveOn) ? 1.0f : 0.0f);
    activeDriveType = p.getInt (pDriveType);
    drive.setType (activeDriveType);
    smoothMix.snap (p.getBool (pSmoothOn) ? 1.0f : 0.0f);
    compPreMix.snap (p.getBool (pCompOn) ? 1.0f : 0.0f);
    compPostMix.snap (p.getBool (pStudioCompOn) ? 1.0f : 0.0f);
    wahMix.snap (p.getBool (pWahOn) ? 1.0f : 0.0f);
    const bool eqOn = p.getBool (pEqOn);
    eqPreMix.snap (eqOn && p.getInt (pEqPosition) == 0 ? 1.0f : 0.0f);
    eqPostMix.snap (eqOn && p.getInt (pEqPosition) == 1 ? 1.0f : 0.0f);
    gateMix.snap (p.getBool (pGateOn) ? 1.0f : 0.0f);
    chorusMix.snap (p.getBool (pChorusOn) ? p.get (pChorusMix) : 0.0f);
    pitchMix.snap (p.getBool (pPitchOn) ? p.get (pPitchMix) : 0.0f);
    delayIn.snap (p.getBool (pDelayOn) ? 1.0f : 0.0f);
    reverbIn.snap (p.getBool (pReverbOn) ? 1.0f : 0.0f);
    for (auto* s : { &ampGain, &ampBass, &ampMid, &ampTreble, &ampPresence, &ampDepth, &ampMaster,
                     &driveDrive, &driveTone, &driveLevel, &chorusRate, &chorusDepth, &delayFb, &delayMix, &reverbMix,
                     &ampPull, &powerSag, &powerVariac })
        s->snapToTarget();
    for (auto& s : eqBands)
        s.snapToTarget();
    activeModel = p.getInt (pAmpModel);
    activePreValve = p.getInt (pAmpPreampValve);
    activePowValve = p.getInt (pAmpPowerValve);
    amps[(size_t) activeModel].setValves (activePreValve, activePowValve);
    modelFade = ModelFade::none;
    modelFadeGain = 1.0f;
    updateControls (kControlBlock); // push snapped values into the DSP objects
    delay.snapTime (p.getBool (pDelaySync) ? 60000.0f / p.get (pTempo) * delayDivisionBeats (p.getInt (pDelayDivision))
                                           : p.get (pDelayTime));
}

void AmpForgeEngine::resetDsp() noexcept
{
    oversampler.reset();
    for (auto& a : amps)
        a.reset();
    drive.reset();
    gate.reset();
    eqPre.reset();
    eqPost.reset();
    cabinet.reset();
    pitch.reset();
    smooth.reset();
    compPre.reset();
    compPost.reset();
    wah.reset();
    chorus.reset();
    delay.reset();
    reverb.reset();
    limiter.reset();
    dcL.reset();
    dcR.reset();
}

void AmpForgeEngine::updateControls (int n) noexcept
{
    const auto& p = store;
    inTrim.target (dbToGain (p.get (pInTrim)));
    outGain.target (dbToGain (p.get (pOutLevel)));
    bypassMix.target (p.getBool (pBypass) ? 1.0f : 0.0f);
    // A drive-type change fades the pedal out, switches while silent, then fades back in.
    const int driveType = p.getInt (pDriveType);
    if (driveType != activeDriveType && driveMix.from == 0.0f && driveMix.to == 0.0f)
    {
        activeDriveType = driveType;
        drive.setType (driveType);
        drive.reset();
    }
    driveMix.target (p.getBool (pDriveOn) && driveType == activeDriveType ? 1.0f : 0.0f);
    smoothMix.target (p.getBool (pSmoothOn) ? 1.0f : 0.0f);
    // Two compressors: a CS-3 style pedal before the amp, a studio compressor after the cabinet.
    compPreMix.target (p.getBool (pCompOn) ? 1.0f : 0.0f);
    compPostMix.target (p.getBool (pStudioCompOn) ? 1.0f : 0.0f);
    compPre.setControls (dsp::Compressor::cs3, p.get (pCompSustain), p.get (pCompAttack), p.get (pCompTone), p.get (pCompLevel));
    compPost.setControls (dsp::Compressor::studio, p.get (pStudioCompAmount), p.get (pStudioCompAttack), p.get (pStudioCompTone),
                          p.get (pStudioCompLevel));
    wahMix.target (p.getBool (pWahOn) ? 1.0f : 0.0f);
    const bool eqOn = p.getBool (pEqOn);
    const int eqPos = p.getInt (pEqPosition);
    eqPreMix.target (eqOn && eqPos == 0 ? 1.0f : 0.0f);
    eqPostMix.target (eqOn && eqPos == 1 ? 1.0f : 0.0f);
    gateMix.target (p.getBool (pGateOn) ? 1.0f : 0.0f);
    chorusMix.target (p.getBool (pChorusOn) ? p.get (pChorusMix) : 0.0f);
    pitchMix.target (p.getBool (pPitchOn) ? p.get (pPitchMix) : 0.0f);
    delayIn.target (p.getBool (pDelayOn) ? 1.0f : 0.0f);
    reverbIn.target (p.getBool (pReverbOn) ? 1.0f : 0.0f);

    for (auto* s : { &inTrim, &outGain, &bypassMix, &driveMix, &eqPreMix, &eqPostMix, &gateMix, &chorusMix, &pitchMix, &delayIn, &reverbIn, &smoothMix, &compPreMix, &compPostMix, &wahMix })
        s->advance (n);

    ampGain.setTarget (p.get (pAmpGain));
    ampBass.setTarget (p.get (pAmpBass));
    ampMid.setTarget (p.get (pAmpMid));
    ampTreble.setTarget (p.get (pAmpTreble));
    ampPresence.setTarget (p.get (pAmpPresence));
    ampDepth.setTarget (p.get (pAmpDepth));
    ampMaster.setTarget (p.get (pAmpMaster));
    ampPull.setTarget (p.getBool (pAmpPull) ? 1.0f : 0.0f);
    powerSag.setTarget (p.get (pPowerSag));
    powerVariac.setTarget (p.get (pPowerVariac) * 0.01f);
    AmpControls ac;
    ac.gain = ampGain.skip (n);
    ac.bass = ampBass.skip (n);
    ac.mid = ampMid.skip (n);
    ac.treble = ampTreble.skip (n);
    ac.presence = ampPresence.skip (n);
    ac.depth = ampDepth.skip (n);
    ac.master = ampMaster.skip (n);
    ac.bright = p.getBool (pAmpBright);
    ac.pull = ampPull.skip (n);
    ac.sag = powerSag.skip (n);
    ac.variac = powerVariac.skip (n);
    amps[(size_t) activeModel].setControls (ac);

    gate.setControls (p.get (pGateThreshold), p.get (pGateRelease));

    driveDrive.setTarget (p.get (pDriveDrive));
    driveTone.setTarget (p.get (pDriveTone));
    driveLevel.setTarget (p.get (pDriveLevel));
    drive.setControls (driveDrive.skip (n), driveTone.skip (n), driveLevel.skip (n));

    std::array<float, 5> bands {};
    for (int i = 0; i < 5; ++i)
    {
        eqBands[(size_t) i].setTarget (p.get (pEq80 + i));
        bands[(size_t) i] = eqBands[(size_t) i].skip (n);
    }
    if (eqPreMix.to > 0.0f || eqPreMix.from > 0.0f)
        eqPre.setBands (bands);
    if (eqPostMix.to > 0.0f || eqPostMix.from > 0.0f)
        eqPost.setBands (bands);

    chorusRate.setTarget (p.get (pChorusRate));
    chorusDepth.setTarget (p.get (pChorusDepth));
    chorusRate.skip (n);
    chorusDepth.skip (n);

    delayFb.setTarget (p.get (pDelayFeedback));
    delayMix.setTarget (p.get (pDelayMix));
    delayFb.skip (n);
    delayMix.skip (n);
    delay.setTone (p.get (pDelayTone));

    reverbMix.setTarget (p.get (pReverbMix));
    reverbMix.skip (n);
    reverb.setControls (p.get (pReverbDecay), p.get (pReverbTone), p.getInt (pReverbType));
}

void AmpForgeEngine::process (const float* in, float* outL, float* outR, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;
    if (! prepared.load (std::memory_order_acquire) || in == nullptr)
    {
        std::memset (outL, 0, sizeof (float) * (size_t) numSamples);
        if (outR != nullptr)
            std::memset (outR, 0, sizeof (float) * (size_t) numSamples);
        return;
    }

    ScopedNoDenormals noDenormals;
    for (int pos = 0; pos < numSamples; pos += kControlBlock)
    {
        const int n = std::min (kControlBlock, numSamples - pos);
        processChunk (in + pos, outL + pos, outR != nullptr ? outR + pos : discardR.data(), n);
    }
}

void AmpForgeEngine::processChunk (const float* in, float* outL, float* outR, int n) noexcept
{
    // ---- transitions (preset changes) ----
    const bool muteReq = muteRequest.load (std::memory_order_acquire);
    if (! muteReq && muted.load (std::memory_order_relaxed))
    {
        snapAll();
        resetDsp();
        muted.store (false, std::memory_order_relaxed);
    }

    updateControls (n);

    // ---- input ----
    float inPk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float x = sanitise (in[i]);
        inPk = std::max (inPk, std::abs (x));
        const float trimmed = x * inTrim.at (i, n);
        dryBuf[(size_t) i] = trimmed;
        chunkIn[(size_t) i] = trimmed * kInputScale;
    }
    atomicMax (mInPeak, inPk);

    // ---- gate detector (on the clean input) ----
    for (int i = 0; i < n; ++i)
    {
        const float g = gate.next (chunkIn[(size_t) i]);
        gateGain[(size_t) i] = 1.0f + gateMix.at (i, n) * (g - 1.0f);
    }
    mGateOpen.store (gate.isOpen(), std::memory_order_relaxed);

    // ---- pedals before the amp: compressor, auto-wah (base rate, clean signal) ----
    constexpr float detectScale = 1.0f / kInputScale; // internal units -> dBFS for the detectors
    if (compPreMix.from > 0.0f || compPreMix.to > 0.0f)
    {
        if (compPreMix.from == 0.0f)
            compPre.reset();
        compPre.process (chunkIn.data(), n, detectScale, compPreMix.from, compPreMix.to);
    }
    if (wahMix.from > 0.0f || wahMix.to > 0.0f)
    {
        if (wahMix.from == 0.0f)
            wah.reset();
        wah.process (chunkIn.data(), n, detectScale, store.get (pWahSens), store.get (pWahRange), store.get (pWahPeak),
                     store.get (pWahMix), wahMix.from, wahMix.to);
    }

    // ---- amp model change: fade out, switch, fade in ----
    // Model or valve changes alter transfer curves: fade out, switch while silent, fade in.
    const int requested = store.getInt (pAmpModel);
    const int requestedPre = store.getInt (pAmpPreampValve);
    const int requestedPow = store.getInt (pAmpPowerValve);
    if (modelFade == ModelFade::none
        && (requested != activeModel || requestedPre != activePreValve || requestedPow != activePowValve))
        modelFade = ModelFade::out;

    // ---- oversampled section ----
    const int factor = oversampler.getFactor();
    const int on = n * factor;
    float* os = oversampler.upsample (chunkIn.data(), n);

    if (driveMix.from > 0.0f || driveMix.to > 0.0f)
    {
        if (driveMix.from == 0.0f)
            drive.reset();
        for (int i = 0; i < on; ++i)
        {
            const float m = driveMix.at (i / factor, n);
            os[i] += m * (drive.process (os[i]) - os[i]);
        }
    }

    auto& amp = amps[(size_t) activeModel];
    amp.processPreamp (os, on);

    if (eqPreMix.from > 0.0f || eqPreMix.to > 0.0f)
    {
        if (eqPreMix.from == 0.0f)
            eqPre.reset();
        for (int i = 0; i < on; ++i)
        {
            const float m = eqPreMix.at (i / factor, n);
            os[i] += m * (eqPre.process (os[i]) - os[i]);
        }
    }

    amp.processPower (os, on);
    oversampler.downsample (os, base.data(), n);

    // ---- base-rate section ----
    if (eqPostMix.from > 0.0f || eqPostMix.to > 0.0f)
    {
        if (eqPostMix.from == 0.0f)
            eqPost.reset();
        for (int i = 0; i < n; ++i)
        {
            const float m = eqPostMix.at (i, n);
            base[(size_t) i] += m * (eqPost.process (base[(size_t) i]) - base[(size_t) i]);
        }
    }

    bool switchModel = false;
    for (int i = 0; i < n; ++i)
    {
        if (modelFade == ModelFade::out)
        {
            modelFadeGain = std::max (0.0f, modelFadeGain - modelFadeStep);
            switchModel = modelFadeGain <= 0.0f;
        }
        else if (modelFade == ModelFade::in)
        {
            modelFadeGain = std::min (1.0f, modelFadeGain + modelFadeStep);
            if (modelFadeGain >= 1.0f)
                modelFade = ModelFade::none;
        }
        base[(size_t) i] = sanitise (base[(size_t) i] * gateGain[(size_t) i] * modelFadeGain);
    }
    if (switchModel)
    {
        // Silent now: swap to the requested model with clean state; it fades in next block.
        activeModel = std::clamp (requested, 0, kNumAmpModels - 1);
        activePreValve = requestedPre;
        activePowValve = requestedPow;
        amps[(size_t) activeModel].setValves (activePreValve, activePowValve);
        amps[(size_t) activeModel].reset();
        eqPre.reset();
        modelFade = ModelFade::in;
    }

    // ---- cabinet ----
    CabinetSection::Settings cs;
    cs.enabled = store.getBool (pCabOn);
    cs.dual = store.getBool (pCabDual);
    cs.levelDb = { store.get (pCabALevel), store.get (pCabBLevel) };
    cs.pan = { store.get (pCabAPan), store.get (pCabBPan) };
    float* L = cabL.data();
    float* R = cabR.data();
    cabinet.process (base.data(), L, R, n, cs);

    // ---- post effects ----
    if (compPostMix.from > 0.0f || compPostMix.to > 0.0f)
    {
        if (compPostMix.from == 0.0f)
            compPost.reset();
        compPost.process (L, R, n, 1.0f, compPostMix.from, compPostMix.to);
    }
    else
        compPost.clearMeter();
    if (smoothMix.from > 0.0f || smoothMix.to > 0.0f)
    {
        if (smoothMix.from == 0.0f)
            smooth.reset();
        smooth.process (L, R, n, store.get (pSmoothAmount), smoothMix.from, smoothMix.to);
    }
    else
        smooth.reset(); // idle: nothing to show on the process light
    if (pitchMix.from > 0.0f || pitchMix.to > 0.0f)
    {
        if (pitchMix.from == 0.0f)
            pitch.reset(); // start from silence, never from stale audio
        pitch.process (L, R, n, pitchMix.from, pitchMix.to);
    }
    if (chorusMix.from > 0.0f || chorusMix.to > 0.0f)
        chorus.process (L, R, n, chorusRate.getCurrent(), chorusDepth.getCurrent(), chorusMix.to, store.getInt (pChorusMode));

    const float delayMs = store.getBool (pDelaySync)
                              ? 60000.0f / store.get (pTempo) * delayDivisionBeats (store.getInt (pDelayDivision))
                              : store.get (pDelayTime);
    delay.process (L, R, n, std::min (delayMs, (float) (StereoDelay::kMaxSeconds * 1000.0 - 10.0)),
                   delayFb.getCurrent(), delayMix.getCurrent(), delayIn.to, store.getInt (pDelayType) == 1);
    reverb.process (L, R, n, reverbIn.to, reverbMix.getCurrent());

    // ---- output ----
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.at (i, n);
        const float b = bypassMix.at (i, n);
        const float l = dcL.process (L[i]) * g;
        const float r = dcR.process (R[i]) * g;
        const float dry = dryBuf[(size_t) i];
        muteGain = muteReq ? std::max (0.0f, muteGain - muteStep) : std::min (1.0f, muteGain + muteStep);
        outL[i] = ((1.0f - b) * l + b * dry) * muteGain;
        outR[i] = ((1.0f - b) * r + b * dry) * muteGain;
    }
    if (muteReq && muteGain <= 0.0f)
        muted.store (true, std::memory_order_release);

    limiter.process (outL, outR, n);

    float pl = 0.0f, pr = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        pl = std::max (pl, std::abs (outL[i]));
        pr = std::max (pr, std::abs (outR[i]));
    }
    atomicMax (mOutL, pl);
    atomicMax (mOutR, pr);
    atomicMax (mLimiter, limiter.getReduction());
}

} // namespace af

#include "PowerAmp.h"

namespace af::dsp
{
void PowerAmp::prepare (double sampleRate, const PowerAmpSpec& s, int defaultValveIndex)
{
    fs = sampleRate;
    spec = s;
    defaultValve = clampv (defaultValveIndex, 0, kNumPowerValves - 1);
    const auto& def = kPowerValves[(size_t) defaultValve];
    for (int i = 0; i < kNumPowerValves; ++i)
        knees[(size_t) i].build (spec.hardness * kPowerValves[(size_t) i].hardness / def.hardness);
    otHigh.prepare (fs, OnePole::Type::highPass, spec.otHpHz);
    otLow.setCoeffs (BiquadCoeffs::lowPass (fs, spec.otLpHz, 0.6));
    // Speaker impedance: resonance bump (~100 Hz) and rising voice-coil inductance.
    speakerLow.setCoeffs (BiquadCoeffs::peaking (fs, 105.0, 1.4, 4.0 * spec.speakerLoad));
    speakerHigh.setCoeffs (BiquadCoeffs::highShelf (fs, 2500.0, 0.6, 3.5 * spec.speakerLoad));
    sagAttack = onePoleCoeff (0.004, fs);
    sagRelease = onePoleCoeff (spec.sagReleaseMs * 0.001, fs);
    lastPresence = lastDepth = -1.0f;
    setControls (spec.fixedPresence, 0.5f);
    setValve (defaultValve);
    setSupply (0.5f, 1.0f);
    reset();
}

void PowerAmp::setValve (int index) noexcept
{
    valve = clampv (index, 0, kNumPowerValves - 1);
    const auto& def = kPowerValves[(size_t) defaultValve];
    const auto& sel = kPowerValves[(size_t) valve];
    asymmetry = spec.asymmetry + (sel.asymmetry - def.asymmetry);
    kneeRest = knees[(size_t) valve] (asymmetry);
    valveDrive = sel.drive / def.drive;
    valveSag = sel.sag / def.sag;
    // Partial level compensation: a valve that breaks up earlier is not simply louder.
    valveLevel = 1.0f / std::sqrt (valveDrive);
    tiltLow.setCoeffs (BiquadCoeffs::lowShelf (fs, 120.0, 0.7, sel.lowDb - def.lowDb));
    tiltMid.setCoeffs (BiquadCoeffs::peaking (fs, 800.0, 0.8, sel.midDb - def.midDb));
    tiltHigh.setCoeffs (BiquadCoeffs::highShelf (fs, 3000.0, 0.7, sel.highDb - def.highDb));
    updateSupply();
}

void PowerAmp::setSupply (float newSagKnob, float newVariac) noexcept
{
    if (newSagKnob == sagKnob && newVariac == variac)
        return;
    sagKnob = clampv (newSagKnob, 0.0f, 1.0f);
    variac = clampv (newVariac, 0.5f, 1.2f);
    updateSupply();
}

void PowerAmp::updateSupply() noexcept
{
    // Variac: the output stage's clipping level follows the supply voltage, so the same drive
    // clips earlier (input scaled up) while the maximum output swing drops; a starved supply
    // also sags more. Output is partly compensated so it is a character control, not a volume knob.
    const float headroom = std::pow (variac, 1.3f);
    inputDrive = valveDrive / headroom;
    outputLevel = valveLevel * headroom * std::pow (variac, -0.7f);
    sag = spec.sag * valveSag * (2.0f * sagKnob) / std::sqrt (variac);
}

void PowerAmp::reset() noexcept
{
    presenceShelf.reset();
    depthShelf.reset();
    otLow.reset();
    otHigh.reset();
    speakerLow.reset();
    speakerHigh.reset();
    tiltLow.reset();
    tiltMid.reset();
    tiltHigh.reset();
    env = 0.0f;
}

void PowerAmp::setControls (float presence, float depth)
{
    if (std::abs (presence - lastPresence) > 1e-4f)
    {
        lastPresence = presence;
        // Centre position is neutral-ish; fully down removes some top end, as reducing
        // feedback shaping does on a real presence control.
        const float db = spec.presenceMaxDb * (presence - 0.3f) / 0.7f;
        presenceShelf.setCoeffs (BiquadCoeffs::highShelf (fs, spec.presenceHz, 0.7, db));
    }
    if (std::abs (depth - lastDepth) > 1e-4f)
    {
        lastDepth = depth;
        const float db = spec.depthMaxDb * depth;
        depthShelf.setCoeffs (spec.depthMaxDb > 0.0f ? BiquadCoeffs::peaking (fs, spec.depthHz, 0.9, db)
                                                     : BiquadCoeffs::identity());
    }
}

} // namespace af::dsp

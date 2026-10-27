#include "PowerAmp.h"

namespace af::dsp
{
void PowerAmp::prepare (double sampleRate, const PowerAmpSpec& s)
{
    fs = sampleRate;
    spec = s;
    knee.build (spec.hardness);
    kneeRest = knee (spec.asymmetry);
    otHigh.prepare (fs, OnePole::Type::highPass, spec.otHpHz);
    otLow.setCoeffs (BiquadCoeffs::lowPass (fs, spec.otLpHz, 0.6));
    // Speaker impedance: resonance bump (~100 Hz) and rising voice-coil inductance.
    speakerLow.setCoeffs (BiquadCoeffs::peaking (fs, 105.0, 1.4, 4.0 * spec.speakerLoad));
    speakerHigh.setCoeffs (BiquadCoeffs::highShelf (fs, 2500.0, 0.6, 3.5 * spec.speakerLoad));
    sagAttack = onePoleCoeff (0.004, fs);
    sagRelease = onePoleCoeff (spec.sagReleaseMs * 0.001, fs);
    lastPresence = lastDepth = -1.0f;
    setControls (spec.fixedPresence, 0.5f);
    reset();
}

void PowerAmp::reset() noexcept
{
    presenceShelf.reset();
    depthShelf.reset();
    otLow.reset();
    otHigh.reset();
    speakerLow.reset();
    speakerHigh.reset();
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

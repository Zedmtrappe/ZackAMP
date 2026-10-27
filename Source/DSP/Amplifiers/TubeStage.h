// ZackAMP — circuit-inspired preamp building blocks (run at the oversampled rate).
//
// Units: signals inside the amp are "grid-normalised": 1.0 is roughly the input swing at
// which a small-signal triode stage starts to conduct grid current (~1.5 V in a real amp).
// A triode stage's small-signal gain is its `swing` (plate swing relative to the next
// grid's headroom), so cascading stages and the pots/tone stack between them reproduce
// the gain structure of a real preamp rather than an arbitrary "drive" number.
#pragma once

#include "../Utilities/DspCommon.h"
#include "../Utilities/Filters.h"

namespace af::dsp
{
struct TriodeSpec
{
    float couplingHpHz = 20.0f; // input coupling capacitor / grid-leak corner
    float cathodeHz = 0.0f;     // > 0: partially bypassed cathode, shelf corner frequency
    float cathodeCutDb = 0.0f;  // gain reduction below cathodeHz (negative dB)
    float plateLpHz = 14000.0f; // Miller capacitance / plate load roll-off
    float swing = 40.0f;        // small-signal gain to the next stage
    float bias = 0.0f;          // operating point: negative = colder (more asymmetric)
    float negLimit = 1.6f;      // cutoff-side headroom relative to grid-conduction side
    float blocking = 0.25f;     // grid-conduction bias shift amount (dynamics, "bloom")
};

/** Static triode transfer: firm limit on the grid-conduction side, softer and later
    limiting on the cutoff side. The asymmetry produces even harmonics that grow with level. */
inline float triodeShape (float v, float negLimit) noexcept
{
    return v >= 0.0f ? tanhRational (v) : -negLimit * tanhRational (-v / negLimit);
}

class TriodeStage
{
public:
    void prepare (double fs, const TriodeSpec& s)
    {
        spec = s;
        coupling.prepare (fs, OnePole::Type::highPass, s.couplingHpHz);
        plate.prepare (fs, OnePole::Type::lowPass, s.plateLpHz);
        hasCathodeShelf = s.cathodeHz > 0.0f && s.cathodeCutDb < 0.0f;
        if (hasCathodeShelf)
            cathode.setCoeffs (BiquadCoeffs::lowShelf (fs, s.cathodeHz, 0.6, s.cathodeCutDb));
        attack = onePoleCoeff (0.002, fs);
        release = onePoleCoeff (0.080, fs);
        restOut = triodeShape (spec.bias, spec.negLimit);
        reset();
    }

    void reset() noexcept
    {
        coupling.reset();
        plate.reset();
        cathode.reset();
        shift = 0.0f;
    }

    float process (float x) noexcept
    {
        x = coupling.process (x);
        if (hasCathodeShelf)
            x = cathode.process (x);

        const float v = x + spec.bias - shift;

        // Grid conduction charges the coupling capacitor and pushes the bias colder;
        // it recovers with the RC time constant. Bounded so it can never run away.
        const float conduction = std::max (0.0f, v - 0.6f) * spec.blocking;
        const float c = conduction > shift ? attack : release;
        shift = clampv (conduction + c * (shift - conduction), 0.0f, 2.0f);

        const float y = -(triodeShape (v, spec.negLimit) - restOut) * spec.swing; // inverting stage
        return plate.process (y);
    }

private:
    TriodeSpec spec;
    OnePole coupling, plate;
    Biquad cathode;
    bool hasCathodeShelf = false;
    float shift = 0.0f, attack = 0.0f, release = 0.0f, restOut = 0.0f;
};

/** Cathode follower driving a tone stack: near-unity gain, but it can only source limited
    current on positive swings — the classic source of "British" upper-mid compression. */
struct CathodeFollowerSpec
{
    float inputScale = 0.05f; // previous plate swing -> follower's normalised range
    float posLimit = 0.7f;    // grid-current limited side
    float negLimit = 2.5f;
    float outputScale = 20.0f;
};

class CathodeFollower
{
public:
    void prepare (const CathodeFollowerSpec& s) noexcept { spec = s; }
    float process (float x) const noexcept
    {
        const float v = x * spec.inputScale;
        const float y = v >= 0.0f ? spec.posLimit * tanhRational (v / spec.posLimit)
                                  : -spec.negLimit * tanhRational (-v / spec.negLimit);
        return y * spec.outputScale;
    }

private:
    CathodeFollowerSpec spec;
};

} // namespace af::dsp

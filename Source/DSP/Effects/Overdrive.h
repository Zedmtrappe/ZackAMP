// AMP FORGE — "Forge Drive" overdrive / boost pedal (run at the oversampled rate, before the amp).
//
// Classic mid-hump overdrive topology: the clipping path only sees frequencies above a
// few hundred Hz (so the low end stays clean and tight in front of a gainy amp), the
// clipped signal is added to the dry signal, then a tone low-pass and output level.
// With drive at zero it acts as a transparent-ish mid boost — the usual way players
// tighten a high-gain amp.
#pragma once

#include "../Utilities/DspCommon.h"
#include "../Utilities/Filters.h"

namespace af::dsp
{
class Overdrive
{
public:
    void prepare (double fs)
    {
        sampleRate = fs;
        clipHp.prepare (fs, OnePole::Type::highPass, 720.0);
        preLp.prepare (fs, OnePole::Type::lowPass, 7000.0);
        setControls (0.3f, 0.5f, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        clipHp.reset();
        preLp.reset();
        tone.reset();
    }

    /** drive/tone 0..1, level in dB. Call at control rate with smoothed values. */
    void setControls (float drive, float toneKnob, float levelDb)
    {
        gain = 1.0f + 180.0f * drive * drive;   // up to ~+45 dB on the clipping path
        if (std::abs (toneKnob - lastTone) > 1e-4f)
        {
            lastTone = toneKnob;
            const double hz = 600.0 * std::pow (10.0, (double) toneKnob); // 600 Hz .. 6 kHz
            tone.setCoeffs (BiquadCoeffs::lowPass (sampleRate, hz, 0.707));
        }
        level = dbToGain (levelDb);
    }

    float process (float x) noexcept
    {
        // Diode clipping at ~0.6 V, expressed in the amp's grid-normalised units.
        constexpr float clipLevel = 0.4f;
        const float hp = preLp.process (clipHp.process (x)) * gain;
        const float clipped = clipLevel * tanhRational (hp / clipLevel);
        return tone.process (x + clipped) * level;
    }

private:
    double sampleRate = 48000.0;
    OnePole clipHp, preLp;
    Biquad tone;
    float gain = 1.0f, level = 1.0f, lastTone = -1.0f;
};

} // namespace af::dsp

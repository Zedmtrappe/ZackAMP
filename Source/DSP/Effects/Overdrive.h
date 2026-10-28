// Z.AMP — drive pedal in front of the amp (run at the oversampled rate). Two types:
//
// Screamer — classic mid-hump overdrive topology: the clipping path only sees frequencies above a
// few hundred Hz (so the low end stays clean and tight in front of a gainy amp), the
// clipped signal is added to the dry signal, then a tone low-pass and output level.
// With drive at zero it acts as a transparent-ish mid boost — the usual way players
// tighten a high-gain amp.
//
// RAT — op-amp distortion: full-range gain of up to ~+60 dB whose bandwidth shrinks as the gain
// rises (op-amp gain-bandwidth limit, the source of its rasp), hard symmetrical diode clipping
// to ground, then the passive "Filter" low-pass (here the Tone knob: clockwise = brighter).
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
        ratHp.prepare (fs, OnePole::Type::highPass, 60.0);
        ratCoupling.prepare (fs, OnePole::Type::highPass, 20.0);
        setControls (0.3f, 0.5f, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        clipHp.reset();
        preLp.reset();
        tone.reset();
        ratHp.reset();
        ratCoupling.reset();
        ratGbw.reset();
    }

    enum Type { screamer = 0, rat = 1 };
    void setType (int t) noexcept
    {
        if (t != type)
        {
            type = t;
            lastTone = lastGain = -1.0f; // recompute the filters for this type
        }
    }

    /** drive/tone 0..1, level in dB. Call at control rate with smoothed values. */
    void setControls (float drive, float toneKnob, float levelDb)
    {
        if (type == rat)
        {
            gain = 1.0f + 1000.0f * drive * drive; // up to ~+60 dB
            if (std::abs (gain - lastGain) > 0.5f)
            {
                lastGain = gain;
                // Gain-bandwidth limit (~1 MHz op-amp): the closed-loop bandwidth falls with gain.
                const double bw = clampv (1.0e6 / (double) gain, 3000.0, 0.45 * sampleRate);
                ratGbw.setCoeffs (BiquadCoeffs::lowPass (sampleRate, std::min (bw, 0.45 * sampleRate), 0.707));
            }
            if (std::abs (toneKnob - lastTone) > 1e-4f)
            {
                lastTone = toneKnob;
                const double hz = 475.0 * std::pow (30.0, (double) toneKnob); // Filter: 475 Hz .. 14 kHz
                tone.setCoeffs (BiquadCoeffs::lowPass (sampleRate, std::min (hz, 0.45 * sampleRate), 0.6));
            }
            level = dbToGain (levelDb - 6.0f); // hard clipping is loud: keep Level comparable
            return;
        }
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
        if (type == rat)
        {
            // Op-amp gain (soft rail limit), then silicon diodes to ground: hard knee at ~0.6 V.
            const float v = ratGbw.process (ratHp.process (x) * gain);
            const float railed = 3.0f * tanhRational (v / 3.0f);
            constexpr float diode = 0.32f;
            const float t = railed / diode;
            const float t2 = t * t;
            const float clipped = diode * t / std::sqrt (std::sqrt (1.0f + t2 * t2)); // knee x/(1+x^4)^(1/4)
            return tone.process (ratCoupling.process (clipped)) * level;
        }
        // Diode clipping at ~0.6 V, expressed in the amp's grid-normalised units.
        constexpr float clipLevel = 0.4f;
        const float hp = preLp.process (clipHp.process (x)) * gain;
        const float clipped = clipLevel * tanhRational (hp / clipLevel);
        return tone.process (x + clipped) * level;
    }

private:
    double sampleRate = 48000.0;
    OnePole clipHp, preLp, ratHp, ratCoupling;
    Biquad tone, ratGbw;
    float gain = 1.0f, level = 1.0f, lastTone = -1.0f, lastGain = -1.0f;
    int type = screamer;
};

} // namespace af::dsp

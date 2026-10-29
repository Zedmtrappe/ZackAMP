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
//
// SD-1 — the Screamer topology with asymmetric clipping (one diode one way, two the other): more
// even harmonics, a smoother, more amp-like push; active treble tone.
//
// DS-1 — transistor booster, op-amp gain, hard silicon diodes to ground, then the blend tone
// control (low-pass and high-pass mixed: a mid scoop around noon, bass-heavy or bright at the ends).
//
// Guv'nor — Marshall's "amp in a box": high op-amp gain into hard diodes with fuller lows than the
// DS-1, then a Marshall-style EQ (Bass and Contour fixed at noon; Tone works as its Treble).
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
        dsBoostHp.prepare (fs, OnePole::Type::highPass, 100.0);
        dsLp.setCoeffs (BiquadCoeffs::lowPass (fs, 560.0, 0.6));
        dsHp.setCoeffs (BiquadCoeffs::highPass (fs, 1300.0, 0.6));
        guvBass.setCoeffs (BiquadCoeffs::lowShelf (fs, 120.0, 0.7, 3.0));
        guvMid.setCoeffs (BiquadCoeffs::peaking (fs, 650.0, 0.8, -3.5));
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
        dsBoostHp.reset();
        dsLp.reset();
        dsHp.reset();
        guvBass.reset();
        guvMid.reset();
    }

    enum Type { screamer = 0, rat = 1, sd1 = 2, ds1 = 3, guvnor = 4, numTypes = 5 };
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
        if (type == ds1 || type == guvnor)
        {
            gain = type == ds1 ? 1.0f + 400.0f * drive * drive : 1.0f + 700.0f * drive * drive;
            if (std::abs (gain - lastGain) > 0.5f)
            {
                lastGain = gain;
                const double bw = clampv ((type == ds1 ? 1.6e6 : 1.0e6) / (double) gain, 3500.0, 0.45 * sampleRate);
                ratGbw.setCoeffs (BiquadCoeffs::lowPass (sampleRate, std::min (bw, 0.45 * sampleRate), 0.707));
            }
            if (std::abs (toneKnob - lastTone) > 1e-4f)
            {
                lastTone = toneKnob;
                if (type == guvnor) // Treble: +/-8 dB shelf
                    tone.setCoeffs (BiquadCoeffs::highShelf (sampleRate, 2200.0, 0.7, 16.0 * ((double) toneKnob - 0.5)));
            }
            dsBlend = clampv (toneKnob, 0.0f, 1.0f);
            level = dbToGain (levelDb + (type == ds1 ? 9.5f : 1.3f));
            return;
        }
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
            level = dbToGain (levelDb + 2.0f); // matched to the Screamer at equal knob settings
            return;
        }
        gain = 1.0f + (type == sd1 ? 140.0f : 180.0f) * drive * drive; // up to ~+43/+45 dB on the clipping path
        if (std::abs (toneKnob - lastTone) > 1e-4f)
        {
            lastTone = toneKnob;
            const double hz = (type == sd1 ? 750.0 : 600.0) * std::pow (10.0, (double) toneKnob); // 600 Hz .. 6 kHz (SD-1 brighter)
            tone.setCoeffs (BiquadCoeffs::lowPass (sampleRate, std::min (hz, 0.45 * sampleRate), 0.707));
        }
        level = dbToGain (levelDb + (type == sd1 ? -1.5f : 0.0f));
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
        if (type == ds1 || type == guvnor)
        {
            // DS-1: transistor booster (cuts the deep lows) first; Guv'nor keeps them.
            const float in = type == ds1 ? dsBoostHp.process (x) * 1.8f : ratHp.process (x);
            const float v = ratGbw.process (in * gain);
            const float railed = 3.5f * tanhRational (v / 3.5f);
            constexpr float diode = 0.3f;
            const float t = railed / diode;
            const float t2 = t * t;
            const float clipped = ratCoupling.process (diode * t / std::sqrt (std::sqrt (1.0f + t2 * t2)));
            if (type == ds1) // blend tone: low-pass (counter-clockwise) .. high-pass (clockwise)
                return (dsLp.process (clipped) * (1.0f - dsBlend) + dsHp.process (clipped) * dsBlend * 1.3f) * level;
            return tone.process (guvMid.process (guvBass.process (clipped))) * level;
        }
        // Diode clipping at ~0.6 V, expressed in the amp's grid-normalised units. The SD-1 clips
        // asymmetrically (two diodes in series on one side).
        constexpr float clipLevel = 0.4f;
        const float hp = preLp.process (clipHp.process (x)) * gain;
        const float clipped = type == sd1 ? (hp >= 0.0f ? clipLevel * tanhRational (hp / clipLevel)
                                                        : 1.7f * clipLevel * tanhRational (hp / (1.7f * clipLevel)))
                                          : clipLevel * tanhRational (hp / clipLevel);
        return tone.process (x + clipped) * level;
    }

private:
    double sampleRate = 48000.0;
    OnePole clipHp, preLp, ratHp, ratCoupling, dsBoostHp;
    Biquad tone, ratGbw, dsLp, dsHp, guvBass, guvMid;
    float dsBlend = 0.5f;
    float gain = 1.0f, level = 1.0f, lastTone = -1.0f, lastGain = -1.0f;
    int type = screamer;
};

} // namespace af::dsp

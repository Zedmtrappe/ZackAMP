#include "AmpModel.h"

namespace af::dsp
{
namespace
{
    // ---- Tone stack component sets (original values chosen per voicing) -----------------
    // R1 treble pot, R2 bass pot, R3 mid pot, R4 slope resistor, C1 treble cap, C2/C3 bass/mid caps.
    constexpr ToneStackComponents tsAmericanClean { 250e3, 250e3, 10e3, 100e3, 250e-12, 100e-9, 47e-9 };
    constexpr ToneStackComponents tsAmericanTweed { 250e3, 1e6, 25e3, 56e3, 250e-12, 20e-9, 20e-9 };
    constexpr ToneStackComponents tsBritish { 220e3, 1e6, 25e3, 33e3, 470e-12, 22e-9, 22e-9 };
    constexpr ToneStackComponents tsModernLead { 250e3, 250e3, 10e3, 100e3, 470e-12, 100e-9, 47e-9 };
    constexpr ToneStackComponents tsModernHeavy { 250e3, 1e6, 25e3, 47e3, 500e-12, 22e-9, 22e-9 };

    constexpr BlockSpec triode (int i) { return { BlockKind::triode, i, 0.0f }; }
    constexpr BlockSpec gainPot() { return { BlockKind::gainPot, 0, 0.0f }; }
    constexpr BlockSpec toneStack() { return { BlockKind::toneStack, 0, 0.0f }; }
    constexpr BlockSpec follower() { return { BlockKind::cathodeFollower, 0, 0.0f }; }
    constexpr BlockSpec pad (float db) { return { BlockKind::pad, 0, db }; }

    TriodeSpec tri (float swing, float couplingHz, float cathodeHz, float cathodeDb, float plateLp,
                    float bias = 0.0f, float negLimit = 1.6f, float blocking = 0.25f)
    {
        TriodeSpec t;
        t.swing = swing;
        t.couplingHpHz = couplingHz;
        t.cathodeHz = cathodeHz;
        t.cathodeCutDb = cathodeDb;
        t.plateLpHz = plateLp;
        t.bias = bias;
        t.negLimit = negLimit;
        t.blocking = blocking;
        return t;
    }

    std::array<AmpVoicing, kNumAmpModels> buildVoicings()
    {
        std::array<AmpVoicing, kNumAmpModels> v {};

        // ---------------- American family ----------------
        {
            auto& a = v[0];
            a.id = "american.clean";
            a.name = "Lacquer Clean";
            a.shortDescription = "American clean: big headroom, glassy top, scooped and articulate.";
            a.family = AmpFamily::american;
            a.gainLabel = "Volume";
            a.controls = ctlGain | ctlBass | ctlMid | ctlTreble | ctlMaster | ctlBright;
            a.inputHpHz = 20.0f;
            a.triodes[0] = tri (40.0f, 20.0f, 0, 0, 16000.0f, 0.0f, 1.7f, 0.15f);
            a.triodes[1] = tri (40.0f, 30.0f, 0, 0, 14000.0f, 0.0f, 1.6f, 0.25f);
            a.chain = { triode (0), toneStack(), gainPot(), triode (1) };
            a.toneStack = tsAmericanClean;
            a.bright = { true, 1800.0f, 8.0f };
            a.power = { 0.22f, 3.0f, 0.03f, 0.30f, 150.0f, 4000.0f, 5.0f, 90.0f, 0.0f, 0.35f, 40.0f, 11000.0f, 0.30f };
            a.powerDriveDb = -2.0f;
            a.outputTrimDb = 3.0f;
            a.defaultIrId = "factory:am-2x12-open-cap";
        }
        {
            auto& a = v[1];
            a.id = "american.tweed";
            a.name = "Lacquer Tweed";
            a.shortDescription = "Vintage American drive: touch-sensitive breakup, loose and warm.";
            a.family = AmpFamily::american;
            a.gainLabel = "Volume";
            a.controls = ctlGain | ctlBass | ctlMid | ctlTreble | ctlPresence | ctlMaster;
            a.inputHpHz = 20.0f;
            a.triodes[0] = tri (40.0f, 20.0f, 0, 0, 14000.0f, 0.0f, 1.6f, 0.2f);
            a.triodes[1] = tri (40.0f, 25.0f, 0, 0, 10000.0f, -0.1f, 1.5f, 0.35f);
            a.chain = { triode (0), gainPot(), triode (1), follower(), toneStack() };
            a.toneStack = tsAmericanTweed;
            a.cathodeFollower = { 0.028f, 0.85f, 2.5f, 20.0f };
            a.power = { 0.30f, 2.0f, 0.05f, 0.50f, 180.0f, 4000.0f, 7.0f, 90.0f, 0.0f, 0.5f, 45.0f, 7500.0f, 0.70f };
            a.powerDriveDb = 3.0f;
            a.defaultIrId = "factory:am-1x12-open-cap";
        }

        // ---------------- British family ----------------
        {
            auto& a = v[2];
            a.id = "british.plexi";
            a.name = "Albion Plexi";
            a.shortDescription = "Vintage British: chewy upper mids, crunch that grows with the volume.";
            a.family = AmpFamily::british;
            a.gainLabel = "Volume";
            a.controls = ctlGain | ctlBass | ctlMid | ctlTreble | ctlPresence | ctlMaster | ctlBright;
            a.inputHpHz = 25.0f;
            a.triodes[0] = tri (40.0f, 25.0f, 400.0f, -6.0f, 14000.0f, 0.0f, 1.6f, 0.25f);
            a.triodes[1] = tri (40.0f, 30.0f, 150.0f, -4.0f, 11000.0f, -0.05f, 1.5f, 0.35f);
            a.chain = { triode (0), gainPot(), triode (1), follower(), toneStack() };
            a.toneStack = tsBritish;
            a.cathodeFollower = { 0.03f, 0.75f, 2.5f, 20.0f };
            a.bright = { true, 2200.0f, 6.0f };
            a.power = { 0.30f, 2.3f, 0.05f, 0.30f, 140.0f, 3500.0f, 8.0f, 90.0f, 0.0f, 0.5f, 50.0f, 8500.0f, 0.70f };
            a.powerDriveDb = 2.0f;
            a.defaultIrId = "factory:br-4x12-v25-cap";
        }
        {
            auto& a = v[3];
            a.id = "british.lead";
            a.name = "Albion Lead";
            a.shortDescription = "Hot-rodded British: cascaded gain, tight lows, aggressive classic rock.";
            a.family = AmpFamily::british;
            a.gainLabel = "Gain";
            a.controls = ctlGain | ctlBass | ctlMid | ctlTreble | ctlPresence | ctlMaster;
            a.inputHpHz = 30.0f;
            a.triodes[0] = tri (40.0f, 30.0f, 500.0f, -8.0f, 14000.0f, 0.0f, 1.6f, 0.2f);
            a.triodes[1] = tri (30.0f, 60.0f, 0, 0, 10000.0f, -0.35f, 1.4f, 0.4f);
            a.triodes[2] = tri (40.0f, 40.0f, 200.0f, -3.0f, 9000.0f, 0.0f, 1.6f, 0.3f);
            a.chain = { triode (0), gainPot(), triode (1), pad (-10.0f), triode (2), follower(), toneStack() };
            a.toneStack = tsBritish;
            a.cathodeFollower = { 0.025f, 0.8f, 2.5f, 20.0f };
            a.power = { 0.32f, 2.6f, 0.05f, 0.25f, 120.0f, 3500.0f, 8.0f, 90.0f, 0.0f, 0.5f, 55.0f, 8500.0f, 0.60f };
            a.powerDriveDb = 0.0f;
            a.defaultIrId = "factory:br-4x12-h75-cap";
        }

        // ---------------- Modern family ----------------
        {
            auto& a = v[4];
            a.id = "modern.lead";
            a.name = "Cascade Lead";
            a.shortDescription = "Focused singing lead: tone stack before the gain, vocal mids, graphic EQ.";
            a.family = AmpFamily::modern;
            a.gainLabel = "Drive";
            a.controls = ctlGain | ctlBass | ctlMid | ctlTreble | ctlPresence | ctlDepth | ctlMaster;
            a.inputHpHz = 35.0f;
            a.triodes[0] = tri (40.0f, 30.0f, 120.0f, -3.0f, 14000.0f, 0.0f, 1.6f, 0.2f);
            a.triodes[1] = tri (40.0f, 100.0f, 700.0f, -10.0f, 9000.0f, -0.3f, 1.4f, 0.35f);
            a.triodes[2] = tri (40.0f, 60.0f, 300.0f, -6.0f, 7000.0f, 0.0f, 1.6f, 0.3f);
            a.triodes[3] = tri (25.0f, 40.0f, 0, 0, 6500.0f, -0.2f, 1.5f, 0.3f);
            a.chain = { triode (0), toneStack(), gainPot(), triode (1), pad (-6.0f), triode (2), pad (-14.0f), triode (3) };
            a.gainFloorDb = -55.0f;
            a.toneStack = tsModernLead;
            a.power = { 0.25f, 3.2f, 0.03f, 0.12f, 90.0f, 4500.0f, 7.0f, 85.0f, 6.0f, 0.5f, 55.0f, 9500.0f, 0.35f };
            a.powerDriveDb = -12.0f;
            a.defaultIrId = "factory:bt-1x12-closed-cap";
        }
        {
            auto& a = v[5];
            a.id = "modern.heavy";
            a.name = "Cascade Heavy";
            a.shortDescription = "Tight modern high gain: firm low end, defined chords, deep resonance.";
            a.family = AmpFamily::modern;
            a.gainLabel = "Gain";
            a.controls = ctlGain | ctlBass | ctlMid | ctlTreble | ctlPresence | ctlDepth | ctlMaster;
            a.inputHpHz = 60.0f;
            a.triodes[0] = tri (40.0f, 40.0f, 300.0f, -6.0f, 14000.0f, 0.0f, 1.6f, 0.2f);
            a.triodes[1] = tri (40.0f, 120.0f, 800.0f, -10.0f, 9000.0f, -0.4f, 1.4f, 0.4f);
            a.triodes[2] = tri (40.0f, 60.0f, 250.0f, -6.0f, 8000.0f, -0.2f, 1.5f, 0.3f);
            a.triodes[3] = tri (30.0f, 40.0f, 0, 0, 7000.0f, 0.0f, 1.6f, 0.3f);
            a.chain = { triode (0), gainPot(), triode (1), pad (-12.0f), triode (2), pad (-14.0f), triode (3), follower(), toneStack() };
            a.gainFloorDb = -60.0f;
            a.toneStack = tsModernHeavy;
            a.cathodeFollower = { 0.025f, 0.9f, 2.5f, 20.0f };
            a.power = { 0.28f, 3.0f, 0.03f, 0.15f, 100.0f, 4000.0f, 8.0f, 80.0f, 9.0f, 0.5f, 50.0f, 9000.0f, 0.45f };
            a.powerDriveDb = -4.0f;
            a.outputTrimDb = 2.0f;
            a.prePowerEQDefault = true;
            a.defaultIrId = "factory:md-4x12-heavy-cap";
        }
        return v;
    }

    const std::array<AmpVoicing, kNumAmpModels>& voicings()
    {
        static const auto v = buildVoicings();
        return v;
    }
} // namespace

const AmpVoicing& getAmpVoicing (int index) noexcept
{
    return voicings()[(size_t) clampv (index, 0, kNumAmpModels - 1)];
}

int getNumAmpModels() noexcept { return kNumAmpModels; }

float AmpModel::potTaper (float knob, float floorDb) noexcept
{
    knob = clampv (knob, 0.0f, 1.0f);
    const float floorGain = dbToGain (floorDb);
    const float audio = (std::pow (10.0f, 2.0f * knob) - 1.0f) / 99.0f; // ~10% at noon
    return floorGain + (1.0f - floorGain) * audio;
}

void AmpModel::prepare (double oversampledRate, const AmpVoicing& v)
{
    voicing = &v;
    fs = oversampledRate;
    inputHp.prepare (fs, OnePole::Type::highPass, v.inputHpHz);
    for (size_t i = 0; i < triodes.size(); ++i)
        triodes[i].prepare (fs, v.triodes[i]);
    toneStack.setComponents (v.toneStack);
    toneStack.prepare (fs);
    follower.prepare (v.cathodeFollower);
    power.prepare (fs, v.power);
    powerDrive = dbToGain (v.powerDriveDb);
    // A saturated power stage peaks near +/-1; keep 12 dB of headroom for the cabinet
    // resonances and effects so the output limiter stays a safety device, not a tone stage.
    constexpr float kAmpOutputHeadroomDb = -12.0f;
    outputTrim = dbToGain (v.outputTrimDb + kAmpOutputHeadroomDb);
    lastToneB = lastToneM = lastToneT = lastBrightGain = -1.0f;
    current = AmpControls {};
    setControls (current);
    gainPotFrom = gainPotTo;
    masterFrom = masterTo;
    reset();
}

void AmpModel::reset() noexcept
{
    inputHp.reset();
    for (auto& t : triodes)
        t.reset();
    toneStack.reset();
    power.reset();
    brightShelf.reset();
}

void AmpModel::setControls (const AmpControls& c)
{
    current = c;
    const auto& v = *voicing;

    if (c.bass != lastToneB || c.mid != lastToneM || c.treble != lastToneT)
    {
        lastToneB = c.bass;
        lastToneM = c.mid;
        lastToneT = c.treble;
        // Models without a mid control use a fixed mid setting as the circuit would.
        toneStack.setControls (c.bass, (v.controls & ctlMid) ? c.mid : 0.5f, c.treble);
    }

    gainPotFrom = gainPotTo;
    gainPotTo = potTaper (c.gain, v.gainFloorDb);
    masterFrom = masterTo;
    masterTo = (v.controls & ctlMaster) ? potTaper (c.master, -60.0f) : 1.0f;

    // Bright cap across the volume pot: the treble bypass matters more the lower the pot is set.
    brightOn = v.bright.available && c.bright;
    if (brightOn)
    {
        const float potDb = gainToDb (gainPotTo);
        const float boost = std::min (v.bright.maxDb, std::max (0.0f, -potDb * 0.5f));
        if (std::abs (boost - lastBrightGain) > 0.05f || ! lastBright)
        {
            lastBrightGain = boost;
            brightShelf.setCoeffs (BiquadCoeffs::highShelf (fs, v.bright.hz, 0.6, boost));
        }
    }
    lastBright = brightOn;

    power.setControls ((v.controls & ctlPresence) ? c.presence : v.power.fixedPresence,
                       (v.controls & ctlDepth) ? c.depth : 0.0f);
}

void AmpModel::processPreamp (float* buf, int n) noexcept
{
    const auto& v = *voicing;
    for (int i = 0; i < n; ++i)
        buf[i] = inputHp.process (buf[i]);

    for (const auto& block : v.chain)
    {
        switch (block.kind)
        {
            case BlockKind::none:
                break;
            case BlockKind::triode:
            {
                auto& t = triodes[(size_t) block.index];
                for (int i = 0; i < n; ++i)
                    buf[i] = t.process (buf[i]);
                break;
            }
            case BlockKind::gainPot:
            {
                const float step = (gainPotTo - gainPotFrom) / (float) std::max (1, n);
                float g = gainPotFrom;
                for (int i = 0; i < n; ++i)
                {
                    g += step;
                    buf[i] *= g;
                }
                gainPotFrom = gainPotTo;
                if (brightOn)
                    for (int i = 0; i < n; ++i)
                        buf[i] = brightShelf.process (buf[i]);
                break;
            }
            case BlockKind::toneStack:
                for (int i = 0; i < n; ++i)
                    buf[i] = toneStack.process (buf[i]);
                break;
            case BlockKind::cathodeFollower:
                for (int i = 0; i < n; ++i)
                    buf[i] = follower.process (buf[i]);
                break;
            case BlockKind::pad:
            {
                const float g = dbToGain (block.value);
                for (int i = 0; i < n; ++i)
                    buf[i] *= g;
                break;
            }
        }
    }
}

void AmpModel::processPower (float* buf, int n) noexcept
{
    const float step = (masterTo - masterFrom) / (float) std::max (1, n);
    float m = masterFrom;
    for (int i = 0; i < n; ++i)
    {
        m += step;
        buf[i] = power.process (buf[i] * m * powerDrive) * outputTrim;
    }
    masterFrom = masterTo;
}

} // namespace af::dsp

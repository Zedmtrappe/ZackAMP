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
    constexpr ToneStackComponents tsSolidState { 250e3, 250e3, 10e3, 47e3, 470e-12, 47e-9, 47e-9 };
    constexpr ToneStackComponents tsTopBoost { 1e6, 1e6, 10e3, 100e3, 56e-12, 22e-9, 22e-9 };
    constexpr ToneStackComponents tsRecto { 250e3, 1e6, 10e3, 39e3, 500e-12, 22e-9, 22e-9 }; // deeper mid scoop at noon
    constexpr ToneStackComponents tsScooped { 250e3, 250e3, 6.8e3, 100e3, 500e-12, 100e-9, 47e-9 };

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

        // Owner's line-up (private build; names refer to the amps each voicing is modelled on).
        {
            auto& a = v[0];
            a.id = "tweed";
            a.defaultPreampValve = 2; // 12AY7
            a.defaultPowerValve = 2;  // 6V6
            a.name = "Tweed";
            a.shortDescription = "1950s American tweed: touch-sensitive breakup, loose, warm and hairy.";
            a.family = AmpFamily::american;
            a.gainLabel = "Volume";
            a.inputHpHz = 20.0f;
            a.triodes[0] = tri (40.0f, 20.0f, 0, 0, 14000.0f, 0.0f, 1.6f, 0.2f);
            a.triodes[1] = tri (40.0f, 25.0f, 0, 0, 10000.0f, -0.1f, 1.5f, 0.35f);
            a.chain = { triode (0), gainPot(), triode (1), follower(), toneStack() };
            a.toneStack = tsAmericanTweed;
            a.cathodeFollower = { 0.028f, 0.85f, 2.5f, 20.0f };
            a.power = { 0.30f, 2.0f, 0.05f, 0.50f, 180.0f, 4000.0f, 7.0f, 90.0f, 0.0f, 0.5f, 45.0f, 7500.0f, 0.70f };
            a.powerDriveDb = 3.0f;
            a.outputTrimDb = 1.0f;
            a.volumeMakeupDb = 12.0f;
            a.defaultIrId = "factory:am-1x12-open-cap";
        }
        {
            // Solid-state stereo clean: very linear preamp, hard and abrupt transistor power
            // clipping, no supply sag, wide and bright (its built-in chorus is the Chorus effect).
            auto& a = v[1];
            a.id = "jc120";
            a.defaultPreampValve = 3; // 12AT7 (most headroom)
            a.defaultPowerValve = 4;  // KT88 (stiffest)
            a.name = "JC120";
            a.shortDescription = "Solid-state stereo clean: huge headroom, crystal top, no valve sag.";
            a.family = AmpFamily::solidState;
            a.gainLabel = "Volume";
            a.inputHpHz = 25.0f;
            a.triodes[0] = tri (12.0f, 25.0f, 0, 0, 19000.0f, 0.0f, 1.0f, 0.0f);
            a.triodes[0].headroom = 5.0f;
            a.triodes[1] = tri (14.0f, 30.0f, 0, 0, 17000.0f, 0.0f, 1.0f, 0.0f);
            a.triodes[1].headroom = 6.0f;
            a.chain = { triode (0), toneStack(), gainPot(), triode (1) };
            a.toneStack = tsSolidState;
            a.bright = { true, 3000.0f, 6.0f };
            a.power = { 0.03f, 9.0f, 0.0f, 0.02f, 60.0f, 4500.0f, 6.0f, 90.0f, 0.0f, 0.5f, 30.0f, 15000.0f, 0.10f };
            a.powerDriveDb = 6.0f;
            a.outputTrimDb = -2.0f;
            a.volumeMakeupDb = 6.0f;
            a.gainRangeScale = 0.35f; // linear amp: the full pot range would be a 30 dB level jump
            a.defaultIrId = "factory:am-2x12-open-cap";
        }
        {
            // British class-A chime: Top Boost stage and cathode follower into a bright two-knob
            // stack, then the volume and an EL84 output stage without negative feedback (soft
            // knee, rectifier sag, follows the speaker): breaks up early and sweetly.
            auto& a = v[2];
            a.id = "ac30";
            a.defaultPreampValve = 0; // 12AX7
            a.defaultPowerValve = 3;  // EL84
            a.name = "AC30";
            a.shortDescription = "British class-A chime: jangly Top Boost highs, early, sweet EL84 breakup.";
            a.family = AmpFamily::british;
            a.gainLabel = "Volume";
            a.inputHpHz = 35.0f;
            a.triodes[0] = tri (40.0f, 30.0f, 0, 0, 15000.0f, 0.0f, 1.6f, 0.2f);
            a.triodes[1] = tri (30.0f, 40.0f, 0, 0, 12000.0f, -0.05f, 1.5f, 0.3f);
            a.chain = { triode (0), follower(), toneStack(), gainPot(), triode (1) };
            a.toneStack = tsTopBoost;
            a.cathodeFollower = { 0.03f, 0.8f, 2.5f, 20.0f };
            a.bright = { true, 2600.0f, 6.0f };
            a.power = { 0.40f, 1.8f, 0.07f, 0.45f, 160.0f, 3000.0f, 6.0f, 100.0f, 0.0f, 0.5f, 65.0f, 9000.0f, 0.80f };
            a.powerDriveDb = 9.0f;
            a.outputTrimDb = -1.0f;
            a.volumeMakeupDb = 14.0f;
            a.defaultIrId = "factory:am-2x12-open-room";
        }
        {
            // British hot-rodded lead: cascaded gain with a cold-clipping second stage.
            auto& a = v[3];
            a.id = "jcm800";
            a.defaultPreampValve = 0; // 12AX7
            a.defaultPowerValve = 1;  // EL34
            a.name = "JCM800";
            a.shortDescription = "1980s British lead: cold-clipped cascade, tight lows, aggressive bite.";
            a.family = AmpFamily::british;
            a.gainLabel = "Pre-amp";
            a.inputHpHz = 30.0f;
            a.triodes[0] = tri (40.0f, 30.0f, 500.0f, -8.0f, 14000.0f, 0.0f, 1.6f, 0.2f);
            a.triodes[1] = tri (32.0f, 60.0f, 0, 0, 10000.0f, -0.45f, 1.4f, 0.4f);
            a.triodes[2] = tri (40.0f, 40.0f, 200.0f, -3.0f, 9000.0f, 0.0f, 1.6f, 0.3f);
            a.chain = { triode (0), gainPot(), triode (1), pad (-9.0f), triode (2), follower(), toneStack() };
            a.toneStack = tsBritish;
            a.cathodeFollower = { 0.025f, 0.8f, 2.5f, 20.0f };
            a.power = { 0.34f, 2.5f, 0.05f, 0.25f, 120.0f, 3800.0f, 8.0f, 90.0f, 0.0f, 0.5f, 55.0f, 8500.0f, 0.60f };
            a.powerDriveDb = 0.0f;
            a.defaultIrId = "factory:br-4x12-v25-cap";
        }
        {
            // High-power American lead: tone stack before the gain, cascading stages, graphic EQ
            // territory, and a big, stiff 6L6 power section with plenty of headroom.
            auto& a = v[4];
            a.id = "coliseum";
            a.defaultPreampValve = 0; // 12AX7
            a.defaultPowerValve = 0;  // 6L6
            a.name = "Coliseum";
            a.shortDescription = "High-power American lead: tone stack before the gain, singing mids, huge clean power.";
            a.family = AmpFamily::modern;
            a.gainLabel = "Drive";
            a.inputHpHz = 35.0f;
            a.triodes[0] = tri (40.0f, 30.0f, 120.0f, -3.0f, 14000.0f, 0.0f, 1.6f, 0.2f);
            a.triodes[1] = tri (40.0f, 100.0f, 700.0f, -10.0f, 9000.0f, -0.3f, 1.4f, 0.35f);
            a.triodes[2] = tri (40.0f, 60.0f, 300.0f, -6.0f, 7000.0f, 0.0f, 1.6f, 0.3f);
            a.triodes[3] = tri (25.0f, 40.0f, 0, 0, 6500.0f, -0.2f, 1.5f, 0.3f);
            a.chain = { triode (0), toneStack(), gainPot(), triode (1), pad (-6.0f), triode (2), pad (-14.0f), triode (3) };
            a.gainFloorDb = -55.0f;
            a.toneStack = tsModernLead;
            a.power = { 0.22f, 3.6f, 0.03f, 0.10f, 90.0f, 4500.0f, 7.0f, 85.0f, 6.0f, 0.5f, 50.0f, 9500.0f, 0.30f };
            a.powerDriveDb = -14.0f;
            a.outputTrimDb = -0.5f;
            a.defaultIrId = "factory:bt-1x12-closed-cap";
        }
        {
            // 1980s rack valve preamp: very tight input, three cascaded high-gain stages, a
            // scooped stack after the distortion and a clean, stiff power amp: thrash rhythm.
            auto& a = v[5];
            a.id = "mp1";
            a.defaultPreampValve = 0; // 12AX7
            a.defaultPowerValve = 0;  // 6L6
            a.name = "MP1";
            a.shortDescription = "1980s rack valve preamp: tight, scooped and saturated, into a clean power amp.";
            a.family = AmpFamily::modern;
            a.gainLabel = "Drive";
            a.inputHpHz = 110.0f;
            a.triodes[0] = tri (40.0f, 60.0f, 600.0f, -9.0f, 13000.0f, 0.0f, 1.6f, 0.2f);
            a.triodes[1] = tri (40.0f, 150.0f, 900.0f, -10.0f, 8000.0f, -0.35f, 1.4f, 0.4f);
            a.triodes[2] = tri (40.0f, 90.0f, 400.0f, -6.0f, 6500.0f, -0.2f, 1.5f, 0.3f);
            a.triodes[3] = tri (12.0f, 40.0f, 0, 0, 9000.0f, 0.0f, 1.2f, 0.1f);
            a.triodes[3].headroom = 3.0f;
            a.chain = { triode (0), gainPot(), triode (1), pad (-4.0f), triode (2), toneStack(), triode (3) };
            a.gainFloorDb = -55.0f;
            a.toneStack = tsScooped;
            a.power = { 0.12f, 4.5f, 0.02f, 0.06f, 80.0f, 4500.0f, 7.0f, 85.0f, 6.0f, 0.5f, 50.0f, 10000.0f, 0.25f };
            a.powerDriveDb = -12.0f;
            a.outputTrimDb = -5.0f;
            a.defaultIrId = "factory:md-2x12-closed-cap";
        }
        {
            // Modern American high gain: cascaded cold stages, stack after the distortion, loose
            // and huge low end, with more supply sag (rectifier) for its bloom.
            auto& a = v[6];
            a.id = "triple";
            a.defaultPreampValve = 0; // 12AX7
            a.defaultPowerValve = 0;  // 6L6
            a.name = "Triple";
            a.shortDescription = "Modern American high gain: thick, saturated and heavy, huge low end.";
            a.family = AmpFamily::modern;
            a.gainLabel = "Gain";
            a.inputHpHz = 30.0f;
            a.triodes[0] = tri (40.0f, 25.0f, 150.0f, -4.0f, 14000.0f, 0.0f, 1.6f, 0.2f);
            a.triodes[1] = tri (40.0f, 50.0f, 320.0f, -6.0f, 8500.0f, -0.45f, 1.4f, 0.25f);
            a.triodes[2] = tri (40.0f, 35.0f, 150.0f, -4.0f, 6500.0f, -0.2f, 1.5f, 0.3f);
            a.triodes[3] = tri (30.0f, 30.0f, 0, 0, 6000.0f, 0.0f, 1.6f, 0.3f);
            a.chain = { triode (0), gainPot(), triode (1), pad (-12.0f), triode (2), pad (-14.0f), triode (3), follower(), toneStack() };
            a.gainFloorDb = -60.0f;
            a.toneStack = tsRecto;
            a.cathodeFollower = { 0.025f, 0.9f, 2.5f, 20.0f };
            a.power = { 0.30f, 2.6f, 0.03f, 0.35f, 160.0f, 4000.0f, 8.0f, 75.0f, 10.0f, 0.5f, 30.0f, 8000.0f, 0.90f };
            a.powerDriveDb = -4.0f;
            a.outputTrimDb = 3.0f;
            a.defaultIrId = "factory:md-4x12-heavy-cap";
        }
        // Every control works on every model (owner's request): give voicings that had no
        // bright cap or depth/resonance control sensible ones. Defaults (bright off) keep the
        // calibrated sound; depth now also adds low-end resonance on the vintage models.
        for (auto& a : v)
        {
            a.controls = ctlGain | ctlBass | ctlMid | ctlTreble | ctlPresence | ctlDepth | ctlMaster | ctlBright;
            if (! a.bright.available)
                a.bright = { true, 2000.0f, 6.0f };
            if (a.power.depthMaxDb <= 0.0f)
            {
                a.power.depthMaxDb = 6.0f;
                a.power.depthHz = 100.0f;
            }
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
    power.prepare (fs, v.power, v.defaultPowerValve);
    powerDrive = dbToGain (v.powerDriveDb);
    // A saturated power stage peaks near +/-1; keep 12 dB of headroom for the cabinet
    // resonances and effects so the output limiter stays a safety device, not a tone stage.
    constexpr float kAmpOutputHeadroomDb = -12.0f;
    outputTrim = dbToGain (v.outputTrimDb + kAmpOutputHeadroomDb);
    lastToneB = lastToneM = lastToneT = lastBrightGain = -1.0f;
    current = AmpControls {};
    setValves (v.defaultPreampValve, v.defaultPowerValve);
    setControls (current);
    gainPotFrom = gainPotTo;
    masterFrom = masterTo;
    makeupFrom = makeupTo;
    reset();
}

void AmpModel::setValves (int preampValve, int powerValve) noexcept
{
    const auto& v = *voicing;
    preValve = clampv (preampValve, 0, kNumPreampValves - 1);
    powValve = clampv (powerValve, 0, kNumPowerValves - 1);

    // Preamp valve: applied to V1 relative to the valve the voicing was calibrated with.
    const auto& def = kPreampValves[(size_t) v.defaultPreampValve];
    const auto& sel = kPreampValves[(size_t) preValve];
    TriodeSpec t = v.triodes[0];
    const float gainRatio = sel.gain / def.gain;
    t.swing *= gainRatio;
    t.headroom *= sel.headroom / def.headroom;
    t.negLimit *= sel.softness / def.softness;
    triodes[0].prepare (fs, t);
    // Partial level compensation: in clean settings V1 gain changes the level directly.
    valveLevel = 1.0f / std::sqrt (gainRatio);

    power.setValve (powValve);
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
    // "Pull for saturation": up to +10 dB more preamp gain, blended smoothly.
    constexpr float kPullBoost = 3.1623f; // +10 dB
    gainPotTo = std::pow (potTaper (c.gain, v.gainFloorDb), v.gainRangeScale) * (1.0f + c.pull * (kPullBoost - 1.0f));
    makeupFrom = makeupTo;
    makeupTo = dbToGain (v.volumeMakeupDb * (1.0f - clampv (c.gain, 0.0f, 1.0f)));
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
    power.setSupply (c.sag, c.variac);
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
    const float mkStep = (makeupTo - makeupFrom) / (float) std::max (1, n);
    float m = masterFrom, mk = makeupFrom;
    for (int i = 0; i < n; ++i)
    {
        m += step;
        mk += mkStep;
        buf[i] = power.process (buf[i] * m * powerDrive) * outputTrim * valveLevel * mk;
    }
    masterFrom = masterTo;
    makeupFrom = makeupTo;
}

} // namespace af::dsp

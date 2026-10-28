#include <doctest.h>
#include "../../Source/Parameters/Parameters.h"

using namespace af;

TEST_CASE ("Parameter table rows line up with the ParamIndex enum")
{
    // Every enum value must point at the definition with its own stable ID. A mismatch would
    // silently connect knobs, presets and MIDI to the wrong parameters.
    const std::pair<int, const char*> expected[] = {
        { pInTrim, "in.trim" }, { pBypass, "engine.bypass" },
        { pGateOn, "gate.enabled" }, { pGateThreshold, "gate.threshold" }, { pGateRelease, "gate.release" },
        { pDriveOn, "drive.enabled" }, { pDriveDrive, "drive.drive" }, { pDriveTone, "drive.tone" }, { pDriveLevel, "drive.level" }, { pDriveType, "drive.type" },
        { pAmpModel, "amp.model" }, { pAmpGain, "amp.gain" }, { pAmpBass, "amp.bass" }, { pAmpMid, "amp.mid" },
        { pAmpTreble, "amp.treble" }, { pAmpPresence, "amp.presence" }, { pAmpDepth, "amp.depth" }, { pAmpMaster, "amp.master" },
        { pAmpBright, "amp.bright" }, { pAmpPull, "amp.pull" }, { pAmpPreampValve, "amp.preampValve" },
        { pAmpPowerValve, "amp.powerValve" }, { pPowerSag, "power.sag" }, { pPowerVariac, "power.variac" },
        { pEqOn, "eq.enabled" }, { pEqPosition, "eq.position" }, { pEq80, "eq.b80" }, { pEq240, "eq.b240" },
        { pEq750, "eq.b750" }, { pEq2200, "eq.b2200" }, { pEq6600, "eq.b6600" },
        { pCabOn, "cab.enabled" }, { pCabDual, "cab.dual" }, { pCabALevel, "cab.a.level" }, { pCabAPan, "cab.a.pan" },
        { pCabBLevel, "cab.b.level" }, { pCabBPan, "cab.b.pan" },
        { pSmoothOn, "smooth.enabled" }, { pSmoothAmount, "smooth.amount" },
        { pPitchOn, "pitch.enabled" }, { pPitchMix, "pitch.mix" },
        { pChorusOn, "chorus.enabled" }, { pChorusMode, "chorus.mode" }, { pChorusRate, "chorus.rate" },
        { pChorusDepth, "chorus.depth" }, { pChorusMix, "chorus.mix" },
        { pDelayOn, "delay.enabled" }, { pDelayTime, "delay.time" }, { pDelaySync, "delay.sync" },
        { pDelayDivision, "delay.division" }, { pDelayFeedback, "delay.feedback" }, { pDelayMix, "delay.mix" },
        { pDelayTone, "delay.tone" }, { pDelayType, "delay.type" },
        { pReverbOn, "reverb.enabled" }, { pReverbMix, "reverb.mix" }, { pReverbDecay, "reverb.decay" }, { pReverbTone, "reverb.tone" }, { pReverbType, "reverb.type" },
        { pOutLevel, "out.level" }, { pTempo, "global.tempo" },
    };
    CHECK ((int) std::size (expected) == kNumParams);
    for (const auto& [index, id] : expected)
    {
        INFO (id);
        CHECK (std::string (paramDef (index).id) == id);
        CHECK (findParamIndex (id) == index);
    }
    for (int i = 0; i < kNumParams; ++i)
        CHECK (paramDef (i).min <= paramDef (i).def);
}

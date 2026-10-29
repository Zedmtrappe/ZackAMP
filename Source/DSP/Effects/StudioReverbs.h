// Z.AMP — the two studio reverb models of the Reverb block.
//
// LexiconReverb  The figure-eight tank published by Lexicon's designer (Griesinger's structure,
//                as documented by Dattorro): input diffusion, two cross-coupled branches of
//                modulated all-pass, delay, damping and decay. Its modulated, chorused "Spin and
//                Wander" tail and Bass Multiply/Crossover decay are what define the sound.
//                Programs: Concert Hall, Random Hall (randomised modulation), Rich Plate, Chamber.
// M7Reverb       Dense, natural and uncoloured, true stereo, in the manner of the Bricasti M7: a
//                distinct early-reflection pattern scaled by Size feeding a 16-line late network
//                (Householder feedback) with low/mid/high decay and only subtle modulation.
//                Programs: Large Hall, Small Room, Plate, Chamber.
//
// These are original implementations of well-documented reverb structures, voiced after the
// published character of those units — not copies of their (proprietary) algorithms.
#pragma once

#include "ModulationEffects.h"

#include <array>

namespace af::dsp
{
struct StudioReverbControls
{
    int program = 0;
    float decay = 2.0f;        // RT60 at mid frequencies, seconds
    float tone = 0.5f;         // high-frequency decay: dark .. bright
    float preDelayMs = 20.0f;
    float size = 0.7f;         // 0..1
    float diffusion = 0.75f;   // 0..1
    float bassMult = 1.2f;     // low-frequency decay multiplier
    float crossoverHz = 500.0f;
    float spinHz = 0.8f;       // modulation rate
    float wander = 0.4f;       // modulation depth 0..1
    float early = 0.5f;        // early reflections vs late (M7)
};

class LexiconReverb
{
public:
    void prepare (double fs);
    void reset() noexcept;
    void setControls (const StudioReverbControls& c);
    void tick (float inL, float inR, float& outL, float& outR) noexcept;

private:
    struct AllPass
    {
        DelayLine line;
        float process (float x, float delay, float g) noexcept
        {
            const float d = line.read (delay);
            const float v = x + g * d;
            line.push (v);
            return d - g * v;
        }
        float tap (float delay) const noexcept { return line.read (delay); }
    };
    double fs = 48000.0;
    float k = 1.0f;                 // sample-rate scale of the published 29761 Hz lengths
    float sizeNow = 1.0f, sizeTarget = 1.0f;
    float inGain1 = 0.75f, inGain2 = 0.625f, decayDiff1 = 0.7f, decayDiff2 = 0.5f;
    float bandwidth = 0.9995f, damping = 0.0005f, decayGain = 0.5f, lowFactor = 1.0f, lowCoeff = 0.95f;
    float excursion = 16.0f, spinInc = 0.0f;
    bool randomMod = false;
    std::array<AllPass, 4> inAp;
    AllPass apL1, apL2, apR1, apR2;
    DelayLine dL1, dL2, dR1, dR2;
    float bwState = 0.0f, dampL = 0.0f, dampR = 0.0f, lowL = 0.0f, lowR = 0.0f, fbL = 0.0f, fbR = 0.0f;
    double phase = 0.0;
    float rndA = 0.0f, rndB = 0.0f, rndTargetA = 0.0f, rndTargetB = 0.0f;
    uint32_t seed = 22222u;
    float programSize = 1.0f;
};

class M7Reverb
{
public:
    void prepare (double fs);
    void reset() noexcept;
    void setControls (const StudioReverbControls& c);
    void tick (float inL, float inR, float& outL, float& outR) noexcept;

private:
    static constexpr int N = 16;
    static constexpr int kTaps = 12;
    double fs = 48000.0;
    float sizeNow = 1.0f, sizeTarget = 1.0f, erScale = 1.0f, erScaleTarget = 1.0f;
    std::array<DelayLine, N> lines;
    std::array<float, N> baseLen {}, gain {}, lowFac {}, highFac {}, damp {}, low {};
    float dampCoeff = 0.3f, lowCoeff = 0.95f, erGain = 0.5f, lateGain = 1.0f, inDiffG = 0.6f;
    DelayLine erL, erR;
    float erLp[2] {};
    float erLpCoeff = 0.5f;
    std::array<DelayLine, 4> diff;
    std::array<float, 4> diffLen {};
    double phase = 0.0;
    float spinInc = 0.0f, modDepth = 0.0f;
    float programSize = 1.0f, programEr = 1.0f;
    bool plate = false;
    float lastDecay = -1.0f, lastBass = -1.0f, lastSize = -1.0f, lastCross = -1.0f, hfRatio = 0.5f, lastHf = -1.0f;
    void updateGains();
};
} // namespace af::dsp

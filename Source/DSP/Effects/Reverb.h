// AMP FORGE — stereo algorithmic reverb (8-line feedback delay network).
//
// Input diffusion (4 series all-passes) -> 8-line FDN with an orthogonal Hadamard
// feedback matrix (energy preserving, so stability depends only on the per-line decay
// gains), per-line damping low-pass for frequency-dependent decay ("tone"), and gentle
// delay modulation on two lines to avoid metallic ringing. Decay gains are derived from
// RT60 so "decay" is in seconds at mid frequencies.
#pragma once

#include "ModulationEffects.h"

#include <array>

namespace af::dsp
{
class Reverb
{
public:
    void prepare (double fs);
    void reset() noexcept;

    /** decay seconds (RT60), tone 0..1 (dark..bright). Call at control rate. */
    void setControls (float decaySeconds, float tone);

    /** Feeds l/r (scaled by inputGain) into the reverb and adds `mix` of the tail to l/r.
        Switching an effect off ramps inputGain to 0, so tails ring out naturally. */
    void process (float* l, float* r, int n, float inputGain, float mix) noexcept;

private:
    static constexpr int N = 8;
    struct AllPass
    {
        DelayLine line;
        int delay = 100;
        float g = 0.6f;
        float process (float x) noexcept
        {
            const float d = line.read ((float) delay);
            const float v = x + g * d;
            line.push (v);
            return d - g * v;
        }
    };

    double sampleRate = 48000.0;
    std::array<DelayLine, N> lines;
    std::array<float, N> baseDelay {}, decayGain {}, lpState {};
    std::array<AllPass, 4> diffusers;
    DelayLine preDelay;
    float damping = 0.3f, lastDecay = -1.0f, lastTone = -1.0f;
    double modPhase = 0.0;
};

} // namespace af::dsp

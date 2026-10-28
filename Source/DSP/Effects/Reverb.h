// ZackAMP — stereo algorithmic reverb (8-line feedback delay network).
//
// Input diffusion (4 series all-passes) -> 8-line FDN with an orthogonal Hadamard
// feedback matrix (energy preserving, so stability depends only on the per-line decay
// gains), per-line damping low-pass for frequency-dependent decay ("tone"), and gentle
// delay modulation on two lines to avoid metallic ringing. Decay gains are derived from
// RT60 so "decay" is in seconds at mid frequencies.
//
// Types scale the same network: Room (small, short pre-delay), Hall (the original large space),
// Chamber (mid-size, denser and brighter), Spring (a short network fed through a dispersive
// all-pass chain — the "drip" and chirp of a spring tank — with the tank's band-limited output).
// A type change fades the wet output out, clears the tank and fades back in (no clicks).
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

    enum Type { room = 0, hall = 1, spring = 2, chamber = 3 };

    /** decay seconds (RT60), tone 0..1 (dark..bright), type. Call at control rate. */
    void setControls (float decaySeconds, float tone, int type = hall);

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

    struct Dispersion // stretched first-order all-pass: (a + z^-k) / (1 + a z^-k)
    {
        std::array<float, 4> x {}, y {};
        float process (float in, float a, int k) noexcept
        {
            const float out = a * in + x[(size_t) (k - 1)] - a * y[(size_t) (k - 1)];
            for (int i = k - 1; i > 0; --i)
            {
                x[(size_t) i] = x[(size_t) (i - 1)];
                y[(size_t) i] = y[(size_t) (i - 1)];
            }
            x[0] = in;
            y[0] = out;
            return out;
        }
    };
    void applyType (int t);
    void updateDecay();

    double sampleRate = 48000.0;
    int type = hall, pendingType = -1;
    float scale = 1.0f, preDelayMs = 12.0f, modAmount = 1.0f, fade = 1.0f, fadeStep = 0.0f;
    std::array<Dispersion, 18> dispersion;
    Biquad springLp[2];
    OnePole springHp[2];
    std::array<DelayLine, N> lines;
    std::array<float, N> baseDelay {}, decayGain {}, lpState {};
    std::array<AllPass, 4> diffusers;
    DelayLine preDelay;
    float damping = 0.3f, lastDecay = -1.0f, lastTone = -1.0f;
    double modPhase = 0.0;
};

} // namespace af::dsp

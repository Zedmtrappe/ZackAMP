#include "Reverb.h"

namespace af::dsp
{
void Reverb::prepare (double fs)
{
    sampleRate = fs;
    // Mutually prime-ish delay lengths (ms) for a dense, room/plate-like tail.
    constexpr float delaysMs[N] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.3f, 73.1f };
    for (int i = 0; i < N; ++i)
    {
        baseDelay[(size_t) i] = delaysMs[i] * 0.001f * (float) fs;
        lines[(size_t) i].prepare ((int) (baseDelay[(size_t) i] + 0.002 * fs) + 8);
    }
    constexpr float apMs[4] = { 4.77f, 3.59f, 2.73f, 1.73f };
    for (int i = 0; i < 4; ++i)
    {
        diffusers[(size_t) i].delay = std::max (2, (int) (apMs[i] * 0.001 * fs));
        diffusers[(size_t) i].line.prepare (diffusers[(size_t) i].delay + 8);
        diffusers[(size_t) i].g = 0.62f;
    }
    preDelay.prepare ((int) (0.03 * fs));
    lastDecay = lastTone = -1.0f;
    setControls (1.8f, 0.5f);
    reset();
}

void Reverb::reset() noexcept
{
    for (auto& l : lines)
        l.reset();
    for (auto& d : diffusers)
        d.line.reset();
    preDelay.reset();
    lpState.fill (0.0f);
    modPhase = 0.0;
}

void Reverb::setControls (float decaySeconds, float tone)
{
    if (decaySeconds != lastDecay)
    {
        lastDecay = decaySeconds;
        const float rt60 = clampv (decaySeconds, 0.1f, 20.0f);
        for (int i = 0; i < N; ++i)
            decayGain[(size_t) i] = std::pow (10.0f, -3.0f * baseDelay[(size_t) i] / (rt60 * (float) sampleRate));
    }
    if (tone != lastTone)
    {
        lastTone = tone;
        // One-pole damping coefficient: 0 = no damping. Brighter tone -> less damping.
        const double cutoff = 1500.0 * std::pow (10.0, (double) clampv (tone, 0.0f, 1.0f)); // 1.5k..15k
        damping = (float) std::exp (-kTwoPi * cutoff / sampleRate);
    }
}

void Reverb::process (float* l, float* r, int n, float inputGain, float mix) noexcept
{
    const double modInc = 0.7 / sampleRate;
    const float modDepth = (float) (0.0008 * sampleRate);
    for (int i = 0; i < n; ++i)
    {
        float x = 0.5f * (l[i] + r[i]) * inputGain;
        preDelay.push (x);
        x = preDelay.read ((float) (0.012 * sampleRate));
        for (auto& d : diffusers)
            x = d.process (x);

        const float m = (float) std::sin (kTwoPi * modPhase);
        modPhase += modInc;
        if (modPhase >= 1.0)
            modPhase -= 1.0;

        std::array<float, N> s {};
        for (int k = 0; k < N; ++k)
        {
            float d = baseDelay[(size_t) k];
            if (k == 1)
                d += modDepth * m;
            else if (k == 5)
                d -= modDepth * m;
            float v = lines[(size_t) k].read (d);
            lpState[(size_t) k] = v + damping * (lpState[(size_t) k] - v);
            s[(size_t) k] = lpState[(size_t) k] * decayGain[(size_t) k];
        }

        // Fast Walsh–Hadamard transform (orthonormal with 1/sqrt(8)).
        for (int len = 1; len < N; len <<= 1)
            for (int a = 0; a < N; a += 2 * len)
                for (int b = a; b < a + len; ++b)
                {
                    const float u = s[(size_t) b], w = s[(size_t) (b + len)];
                    s[(size_t) b] = u + w;
                    s[(size_t) (b + len)] = u - w;
                }
        constexpr float norm = 0.35355339f;

        float outL = 0.0f, outR = 0.0f;
        for (int k = 0; k < N; ++k)
        {
            const float fb = s[(size_t) k] * norm;
            lines[(size_t) k].push (x * 0.5f + fb);
            const float tap = lpState[(size_t) k];
            if (k & 1)
                outR += tap;
            else
                outL += tap;
            if ((k & 3) == 0)
                outR -= tap * 0.3f;
        }
        l[i] += outL * 0.25f * mix;
        r[i] += outR * 0.25f * mix;
    }
}

} // namespace af::dsp

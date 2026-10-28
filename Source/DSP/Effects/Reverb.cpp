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
    for (auto& f : springLp)
        f.setCoeffs (BiquadCoeffs::lowPass (fs, 4300.0, 0.9));
    for (auto& f : springHp)
        f.prepare (fs, OnePole::Type::highPass, 170.0);
    fadeStep = (float) (1.0 / (0.012 * fs));
    lastDecay = lastTone = -1.0f;
    pendingType = -1;
    applyType (type);
    setControls (1.8f, 0.5f, type);
    reset();
}

void Reverb::applyType (int t)
{
    type = clampv (t, 0, 4);
    switch (type)
    {
        case room:    scale = 0.40f; preDelayMs = 3.0f;  modAmount = 0.4f; break;
        case chamber: scale = 0.66f; preDelayMs = 7.0f;  modAmount = 0.5f; break;
        case spring:  scale = 0.55f; preDelayMs = 2.0f;  modAmount = 0.0f; break;
        case plate:   scale = 0.50f; preDelayMs = 1.0f;  modAmount = 0.6f; break;
        default:      scale = 1.00f; preDelayMs = 12.0f; modAmount = 1.0f; break;
    }
    // Plate: the densest build-up (a plate has no distinct early reflections).
    const float g = type == plate ? 0.75f : type == chamber ? 0.70f : type == room ? 0.66f : 0.62f;
    for (auto& d : diffusers)
        d.g = g;
    updateDecay();
}

void Reverb::updateDecay()
{
    const float rt60 = clampv (lastDecay < 0.0f ? 1.8f : lastDecay, 0.1f, 20.0f);
    for (int i = 0; i < N; ++i)
        decayGain[(size_t) i] = std::pow (10.0f, -3.0f * baseDelay[(size_t) i] * scale / (rt60 * (float) sampleRate));
}

void Reverb::reset() noexcept
{
    for (auto& l : lines)
        l.reset();
    for (auto& d : diffusers)
        d.line.reset();
    preDelay.reset();
    lpState.fill (0.0f);
    for (auto& d : dispersion)
        d = {};
    for (auto& f : springLp)
        f.reset();
    for (auto& f : springHp)
        f.reset();
    modPhase = 0.0;
}

void Reverb::setControls (float decaySeconds, float tone, int newType)
{
    newType = clampv (newType, 0, 4);
    if (newType != type && newType != pendingType)
        pendingType = newType; // switched in process() once the wet output has faded out
    if (decaySeconds != lastDecay)
    {
        lastDecay = decaySeconds;
        updateDecay();
    }
    if (tone != lastTone)
    {
        lastTone = tone;
        // One-pole damping coefficient: 0 = no damping. Brighter tone -> less damping.
        double cutoff = 1500.0 * std::pow (10.0, (double) clampv (tone, 0.0f, 1.0f)); // 1.5k..15k
        if (type == chamber)
            cutoff *= 1.4;
        else if (type == plate)
            cutoff *= 1.8; // bright, shimmering top
        damping = (float) std::exp (-kTwoPi * std::min (cutoff, 0.45 * sampleRate) / sampleRate);
    }
}

void Reverb::process (float* l, float* r, int n, float inputGain, float mix) noexcept
{
    const double modInc = 0.7 / sampleRate;
    const float modDepth = (float) (0.0008 * sampleRate) * modAmount;
    const float pre = std::max (1.0f, preDelayMs * 0.001f * (float) sampleRate);
    const bool isSpring = type == spring;
    for (int i = 0; i < n; ++i)
    {
        // Type change: fade the wet output out, clear the tank, switch, fade back in.
        if (pendingType >= 0)
        {
            fade -= fadeStep;
            if (fade <= 0.0f)
            {
                fade = 0.0f;
                reset();
                const int t = pendingType;
                pendingType = -1;
                applyType (t);
                lastTone = -1.0f; // re-derive damping for the new type on the next control update
                return;           // rest of this block stays dry
            }
        }
        else if (fade < 1.0f)
            fade = std::min (1.0f, fade + fadeStep);

        float x = 0.5f * (l[i] + r[i]) * inputGain;
        preDelay.push (x);
        x = preDelay.read (pre);
        if (isSpring)
            for (auto& d : dispersion)
                x = d.process (x, 0.62f, 3);
        for (auto& d : diffusers)
            x = d.process (x);

        const float m = (float) std::sin (kTwoPi * modPhase);
        modPhase += modInc;
        if (modPhase >= 1.0)
            modPhase -= 1.0;

        std::array<float, N> s {};
        for (int k = 0; k < N; ++k)
        {
            float d = baseDelay[(size_t) k] * scale;
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
        if (isSpring)
        {
            outL = springLp[0].process (springHp[0].process (outL)) * 1.4f;
            outR = springLp[1].process (springHp[1].process (outR)) * 1.4f;
        }
        l[i] += outL * 0.25f * mix * fade;
        r[i] += outR * 0.25f * mix * fade;
    }
}

} // namespace af::dsp

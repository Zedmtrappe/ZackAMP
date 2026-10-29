#include "StudioReverbs.h"

namespace af::dsp
{
namespace
{
    float coeffFor (double hz, double fs) { return (float) std::exp (-kTwoPi * std::min (hz, 0.45 * fs) / fs); }
    constexpr float kMaxSize = 1.25f;
} // namespace

//==============================================================================
// Lexicon-style figure-eight tank. Lengths in samples at 29761 Hz (the published reference).
namespace lex
{
    constexpr float in1 = 142, in2 = 107, in3 = 379, in4 = 277;
    constexpr float apL1 = 672, dL1 = 4453, apL2 = 1800, dL2 = 3720;
    constexpr float apR1 = 908, dR1 = 4217, apR2 = 2656, dR2 = 3163;
    constexpr float loop = apL1 + dL1 + apL2 + dL2 + apR1 + dR1 + apR2 + dR2;
} // namespace lex

void LexiconReverb::prepare (double sampleRate)
{
    fs = sampleRate;
    k = (float) (fs / 29761.0);
    auto prep = [this] (DelayLine& d, float len) { d.prepare ((int) (len * k * kMaxSize + 64.0f * k) + 8); };
    for (auto [ap, len] : { std::pair { &inAp[0], lex::in1 }, { &inAp[1], lex::in2 }, { &inAp[2], lex::in3 }, { &inAp[3], lex::in4 } })
        prep (ap->line, len);
    prep (apL1.line, lex::apL1);
    prep (apL2.line, lex::apL2);
    prep (apR1.line, lex::apR1);
    prep (apR2.line, lex::apR2);
    prep (dL1, lex::dL1);
    prep (dL2, lex::dL2);
    prep (dR1, lex::dR1);
    prep (dR2, lex::dR2);
    setControls (StudioReverbControls {});
    sizeNow = sizeTarget;
    reset();
}

void LexiconReverb::reset() noexcept
{
    for (auto& a : inAp) a.line.reset();
    for (auto* a : { &apL1, &apL2, &apR1, &apR2 }) a->line.reset();
    for (auto* d : { &dL1, &dL2, &dR1, &dR2 }) d->reset();
    bwState = dampL = dampR = lowL = lowR = fbL = fbR = 0.0f;
    rndA = rndB = rndTargetA = rndTargetB = 0.0f;
}

void LexiconReverb::setControls (const StudioReverbControls& c)
{
    // Program character: Concert Hall, Random Hall, Rich Plate, Chamber.
    const int prog = std::clamp (c.program, 0, 3);
    programSize = prog == 2 ? 0.62f : prog == 3 ? 0.7f : 1.0f;
    randomMod = prog == 1;
    bandwidth = prog == 2 ? 0.9999f : prog == 3 ? 0.995f : 0.9995f;
    const float diffusion = clampv (c.diffusion * (prog == 2 ? 1.15f : 1.0f), 0.0f, 1.0f);
    inGain1 = 0.45f + 0.35f * diffusion;
    inGain2 = 0.35f + 0.3f * diffusion;
    decayDiff1 = 0.45f + 0.3f * diffusion;
    decayDiff2 = 0.35f + 0.2f * diffusion;

    sizeTarget = clampv ((0.45f + 0.75f * clampv (c.size, 0.0f, 1.0f)) * programSize, 0.25f, kMaxSize);
    const float rt = clampv (c.decay, 0.1f, 30.0f);
    const float loopSec = lex::loop * k * sizeTarget / (float) fs;
    decayGain = std::pow (10.0f, -3.0f * loopSec / (4.0f * rt));
    const float lowGain = std::pow (10.0f, -3.0f * loopSec / (4.0f * rt * clampv (c.bassMult, 0.3f, 4.0f)));
    lowFactor = lowGain / std::max (1e-6f, decayGain);
    lowCoeff = coeffFor (clampv (c.crossoverHz, 50.0f, 3000.0f), fs);
    // Tone = high-frequency decay: damping low-pass in the tank.
    const double cutoff = 1500.0 * std::pow (10.0, (double) clampv (c.tone, 0.0f, 1.0f)) * (prog == 2 ? 1.6 : 1.0);
    damping = coeffFor (cutoff, fs);
    excursion = 2.0f + 30.0f * k * clampv (c.wander, 0.0f, 1.0f);
    spinInc = (float) (clampv (c.spinHz, 0.05f, 5.0f) / fs);
}

void LexiconReverb::tick (float inL, float inR, float& outL, float& outR) noexcept
{
    sizeNow += (sizeTarget - sizeNow) * 0.0005f; // slow glide: no zipper on size changes
    const float s = sizeNow * k;
    // Modulation: sine (Spin) or smoothed random (Random Hall).
    phase += spinInc;
    if (phase >= 1.0)
        phase -= 1.0;
    float ma, mb;
    if (randomMod)
    {
        if (phase < spinInc) // new random targets once per cycle
        {
            seed = seed * 1664525u + 1013904223u;
            rndTargetA = (float) (seed >> 8) / 8388608.0f - 1.0f;
            seed = seed * 1664525u + 1013904223u;
            rndTargetB = (float) (seed >> 8) / 8388608.0f - 1.0f;
        }
        rndA += (rndTargetA - rndA) * spinInc * 4.0f;
        rndB += (rndTargetB - rndB) * spinInc * 4.0f;
        ma = rndA;
        mb = rndB;
    }
    else
    {
        ma = (float) std::sin (kTwoPi * phase);
        mb = (float) std::cos (kTwoPi * phase);
    }

    // Input: band-limit and diffuse.
    float x = 0.5f * (inL + inR);
    bwState = bandwidth * x + (1.0f - bandwidth) * bwState; // input bandwidth low-pass
    x = bwState;
    x = inAp[0].process (x, lex::in1 * k, inGain1);
    x = inAp[1].process (x, lex::in2 * k, inGain1);
    x = inAp[2].process (x, lex::in3 * k, inGain2);
    x = inAp[3].process (x, lex::in4 * k, inGain2);

    auto decaySeg = [this] (float v, float& dampState, float& lowState)
    {
        dampState = v + damping * (dampState - v); // high-frequency damping
        float y = dampState * decayGain;
        lowState = y + lowCoeff * (lowState - y);  // Bass Multiply below the crossover
        return y + lowState * (lowFactor - 1.0f);
    };

    // Left branch (fed by the right branch's output) and right branch (fed by the left's).
    float l = x + fbR;
    l = apL1.process (l, lex::apL1 * s + excursion * ma, -decayDiff1);
    dL1.push (l);
    l = decaySeg (dL1.read (lex::dL1 * s), dampL, lowL);
    l = apL2.process (l, lex::apL2 * s, decayDiff2);
    dL2.push (l);
    const float nextL = dL2.read (lex::dL2 * s) * decayGain;

    float r = x + fbL;
    r = apR1.process (r, lex::apR1 * s + excursion * mb, -decayDiff1);
    dR1.push (r);
    r = decaySeg (dR1.read (lex::dR1 * s), dampR, lowR);
    r = apR2.process (r, lex::apR2 * s, decayDiff2);
    dR2.push (r);
    const float nextR = dR2.read (lex::dR2 * s) * decayGain;
    fbL = nextL;
    fbR = nextR;

    // Output taps (published positions, scaled with Size).
    auto t = [s] (float n) { return std::max (1.0f, n * s); };
    outL = 0.15f * (dR1.read (t (266)) + dR1.read (t (2974)) - apR2.tap (t (1913)) + dR2.read (t (1996)) - dL1.read (t (1990))
                   - apL2.tap (t (187)) - dL2.read (t (1066)));
    outR = 0.15f * (dL1.read (t (353)) + dL1.read (t (3627)) - apL2.tap (t (1228)) + dL2.read (t (2673)) - dR1.read (t (2111))
                   - apR2.tap (t (335)) - dR2.read (t (121)));
}

//==============================================================================
namespace m7
{
    // Late network line lengths (samples at 48 kHz), mutually prime.
    constexpr float lens[16] = { 1031, 1171, 1291, 1427, 1549, 1667, 1789, 1907, 2027, 2153, 2281, 2399, 2521, 2647, 2777, 2903 };
    // Early reflections (ms), per channel, and their levels.
    constexpr float erL[12] = { 4.3f, 7.9f, 11.2f, 15.6f, 19.1f, 24.3f, 29.8f, 33.1f, 41.7f, 48.2f, 57.9f, 66.4f };
    constexpr float erR[12] = { 5.1f, 8.8f, 12.9f, 14.4f, 21.7f, 26.2f, 31.5f, 37.9f, 44.6f, 51.3f, 60.8f, 70.1f };
    constexpr float erG[12] = { 0.82f, 0.74f, 0.70f, 0.61f, 0.57f, 0.50f, 0.44f, 0.40f, 0.33f, 0.29f, 0.24f, 0.20f };
    constexpr float diffMs[4] = { 3.1f, 4.4f, 2.3f, 3.7f };
} // namespace m7

void M7Reverb::prepare (double sampleRate)
{
    fs = sampleRate;
    const float r = (float) (fs / 48000.0);
    for (int i = 0; i < N; ++i)
    {
        baseLen[(size_t) i] = m7::lens[i] * r;
        lines[(size_t) i].prepare ((int) (baseLen[(size_t) i] * 1.4f + 64.0f) + 8);
    }
    erL.prepare ((int) (0.075 * fs * 1.3) + 8);
    erR.prepare ((int) (0.075 * fs * 1.3) + 8);
    for (int i = 0; i < 4; ++i)
    {
        diffLen[(size_t) i] = (float) (m7::diffMs[i] * 0.001 * fs);
        diff[(size_t) i].prepare ((int) diffLen[(size_t) i] + 8);
    }
    lastDecay = lastBass = lastSize = lastCross = -1.0f;
    setControls (StudioReverbControls {});
    sizeNow = sizeTarget;
    erScale = erScaleTarget;
    reset();
}

void M7Reverb::reset() noexcept
{
    for (auto& l : lines) l.reset();
    for (auto& d : diff) d.reset();
    erL.reset();
    erR.reset();
    damp.fill (0.0f);
    low.fill (0.0f);
    erLp[0] = erLp[1] = 0.0f;
}

void M7Reverb::updateGains()
{
    for (int i = 0; i < N; ++i)
    {
        const float sec = baseLen[(size_t) i] * sizeTarget / (float) fs;
        gain[(size_t) i] = std::pow (10.0f, -3.0f * sec / lastDecay);
        const float lowGain = std::pow (10.0f, -3.0f * sec / (lastDecay * lastBass));
        lowFac[(size_t) i] = lowGain / std::max (1e-6f, gain[(size_t) i]);
        const float highGain = std::pow (10.0f, -3.0f * sec / (lastDecay * hfRatio));
        highFac[(size_t) i] = highGain / std::max (1e-6f, gain[(size_t) i]);
    }
}

void M7Reverb::setControls (const StudioReverbControls& c)
{
    // Program character: Large Hall, Small Room, Plate, Chamber.
    const int prog = std::clamp (c.program, 0, 3);
    programSize = prog == 1 ? 0.45f : prog == 2 ? 0.6f : prog == 3 ? 0.72f : 1.0f;
    programEr = prog == 1 ? 1.5f : prog == 2 ? 0.0f : prog == 3 ? 1.0f : 0.85f;
    plate = prog == 2;
    sizeTarget = clampv ((0.4f + 0.9f * clampv (c.size, 0.0f, 1.0f)) * programSize, 0.25f, 1.35f);
    erScaleTarget = clampv (0.35f + 0.85f * clampv (c.size, 0.0f, 1.0f), 0.3f, 1.25f) * (prog == 1 ? 0.55f : 1.0f);
    const float early = clampv (c.early, 0.0f, 1.0f);
    erGain = 1.3f * early * programEr;
    lateGain = 1.0f - 0.55f * early * std::min (1.0f, programEr);
    inDiffG = 0.35f + 0.4f * clampv (c.diffusion, 0.0f, 1.0f) + (plate ? 0.1f : 0.0f);

    // Tone = treble decay as a fraction of Decay (dark 0.2x .. bright 1.0x), like a high-frequency
    // decay control, so the tail length does not depend on the line lengths.
    hfRatio = clampv ((0.2f + 0.8f * clampv (c.tone, 0.0f, 1.0f)) * (plate ? 1.25f : 1.0f), 0.15f, 1.0f);
    const float rt = clampv (c.decay, 0.1f, 30.0f), bm = clampv (c.bassMult, 0.3f, 4.0f);
    if (rt != lastDecay || bm != lastBass || hfRatio != lastHf || std::abs (sizeTarget - lastSize) > 1e-4f)
    {
        lastDecay = rt;
        lastBass = bm;
        lastHf = hfRatio;
        lastSize = sizeTarget;
        updateGains();
    }
    lowCoeff = coeffFor (clampv (c.crossoverHz, 50.0f, 3000.0f), fs);
    const double cutoff = 1800.0 * std::pow (10.0, (double) clampv (c.tone, 0.0f, 1.0f)) * (plate ? 1.5 : 1.0);
    dampCoeff = coeffFor (4000.0, fs); // treble-decay crossover
    erLpCoeff = coeffFor (cutoff * 1.3, fs);
    // Subtle modulation only: the M7 is prized for not sounding chorused.
    modDepth = (float) (fs / 48000.0) * 6.0f * clampv (c.wander, 0.0f, 1.0f);
    spinInc = (float) (clampv (c.spinHz, 0.05f, 5.0f) * 0.5 / fs);
}

void M7Reverb::tick (float inL, float inR, float& outL, float& outR) noexcept
{
    sizeNow += (sizeTarget - sizeNow) * 0.0005f;
    erScale += (erScaleTarget - erScale) * 0.0005f;
    phase += spinInc;
    if (phase >= 1.0)
        phase -= 1.0;
    const float mA = (float) std::sin (kTwoPi * phase) * modDepth, mB = (float) std::cos (kTwoPi * phase * 1.37) * modDepth;

    // Early reflections: a sparse, size-scaled pattern per channel with some cross-feed.
    erL.push (inL);
    erR.push (inR);
    float el = 0.0f, er = 0.0f;
    const float msToS = (float) (0.001 * fs) * erScale;
    for (int i = 0; i < kTaps; ++i)
    {
        const float sgn = (i % 3 == 2) ? -1.0f : 1.0f;
        el += sgn * m7::erG[i] * (0.8f * erL.read (m7::erL[i] * msToS) + 0.2f * erR.read (m7::erR[i] * msToS));
        er += sgn * m7::erG[i] * (0.8f * erR.read (m7::erR[i] * msToS) + 0.2f * erL.read (m7::erL[i] * msToS));
    }
    el *= 0.32f;
    er *= 0.32f;
    erLp[0] = el + erLpCoeff * (erLp[0] - el);
    erLp[1] = er + erLpCoeff * (erLp[1] - er);

    // Late network input: the early field (smooth build-up) plus some direct sound, diffused.
    float xl = 0.6f * erLp[0] + 0.4f * inL, xr = 0.6f * erLp[1] + 0.4f * inR;
    auto ap = [this] (DelayLine& d, float len, float x)
    {
        const float del = d.read (len);
        const float v = x + inDiffG * del;
        d.push (v);
        return del - inDiffG * v;
    };
    xl = ap (diff[1], diffLen[1], ap (diff[0], diffLen[0], xl));
    xr = ap (diff[3], diffLen[3], ap (diff[2], diffLen[2], xr));

    std::array<float, N> s {};
    float sum = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        float len = baseLen[(size_t) i] * sizeNow;
        if (i == 3) len += mA;
        else if (i == 10) len += mB;
        const float v = lines[(size_t) i].read (len);
        damp[(size_t) i] = v + dampCoeff * (damp[(size_t) i] - v); // band below 4 kHz
        float y = (damp[(size_t) i] + (v - damp[(size_t) i]) * highFac[(size_t) i]) * gain[(size_t) i];
        low[(size_t) i] = y + lowCoeff * (low[(size_t) i] - y);
        y += low[(size_t) i] * (lowFac[(size_t) i] - 1.0f);
        s[(size_t) i] = y;
        sum += y;
    }
    // Householder feedback: x - (2/N) * sum, energy preserving.
    const float h = sum * (2.0f / (float) N);
    float lateL = 0.0f, lateR = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        const float fb = s[(size_t) i] - h;
        lines[(size_t) i].push (fb + ((i & 1) ? xr : xl) * 0.35f);
        if (i & 1)
            lateR += s[(size_t) i] * ((i & 2) ? 1.0f : -1.0f);
        else
            lateL += s[(size_t) i] * ((i & 2) ? 1.0f : -1.0f);
    }
    outL = 0.32f * lateL * lateGain + erLp[0] * erGain;
    outR = 0.32f * lateR * lateGain + erLp[1] * erGain;
}
} // namespace af::dsp

// ZackAMP — shared DSP utilities (framework independent, real-time safe).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(__SSE__) || defined(__x86_64__) || defined(_M_X64)
 #include <xmmintrin.h>
 #define AF_HAS_SSE 1
#endif

namespace af::dsp
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline double dbToGain (double db) noexcept { return std::pow (10.0, db * 0.05); }

inline float gainToDb (float g, float floorDb = -120.0f) noexcept
{
    return g > 0.0f ? std::max (floorDb, 20.0f * std::log10 (g)) : floorDb;
}

template <typename T>
inline T clampv (T v, T lo, T hi) noexcept { return std::min (hi, std::max (lo, v)); }

/** Replaces NaN / Inf with zero. Used at module boundaries as a last line of defence. */
inline float sanitise (float x) noexcept { return std::isfinite (x) ? x : 0.0f; }

/** Fast rational tanh approximation; exact limits of +/-1 at |x| >= 3, C1 continuous. */
inline float tanhRational (float x) noexcept
{
    x = clampv (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** Exponential smoothing coefficient for a time constant in seconds. */
inline float onePoleCoeff (double timeSeconds, double sampleRate) noexcept
{
    if (timeSeconds <= 0.0 || sampleRate <= 0.0)
        return 0.0f;
    return (float) std::exp (-1.0 / (timeSeconds * sampleRate));
}

/** Sets flush-to-zero / denormals-are-zero for the scope of a processing call. */
class ScopedNoDenormals
{
public:
    ScopedNoDenormals() noexcept
    {
#if AF_HAS_SSE
        saved = _mm_getcsr();
        _mm_setcsr (saved | 0x8040u);
#elif defined(__aarch64__)
        asm volatile ("mrs %0, fpcr" : "=r"(saved));
        uint64_t v = saved | (1ull << 24);
        asm volatile ("msr fpcr, %0" : : "r"(v));
#endif
    }

    ~ScopedNoDenormals() noexcept
    {
#if AF_HAS_SSE
        _mm_setcsr (saved);
#elif defined(__aarch64__)
        asm volatile ("msr fpcr, %0" : : "r"(saved));
#endif
    }

    ScopedNoDenormals (const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator= (const ScopedNoDenormals&) = delete;

private:
#if AF_HAS_SSE
    unsigned int saved = 0;
#else
    uint64_t saved = 0;
#endif
};

/** Linear ramp smoother. Values are advanced per sample or skipped per control block. */
class LinearSmoother
{
public:
    void prepare (double sampleRate, double rampSeconds) noexcept
    {
        rampLength = std::max (1, (int) std::lround (sampleRate * rampSeconds));
        countdown = 0;
        current = target;
    }

    void setTarget (float newTarget) noexcept
    {
        if (newTarget == target)
            return;
        target = newTarget;
        if (rampLength <= 1)
        {
            current = target;
            countdown = 0;
            return;
        }
        countdown = rampLength;
        step = (target - current) / (float) rampLength;
    }

    void snap (float value) noexcept
    {
        target = current = value;
        countdown = 0;
    }

    void snapToTarget() noexcept { snap (target); }

    float next() noexcept
    {
        if (countdown <= 0)
            return target;
        --countdown;
        current = (countdown == 0) ? target : current + step;
        return current;
    }

    /** Advances by n samples and returns the value at the end of that span. */
    float skip (int n) noexcept
    {
        if (countdown <= 0)
            return target;
        if (n >= countdown)
        {
            countdown = 0;
            current = target;
            return current;
        }
        countdown -= n;
        current += step * (float) n;
        return current;
    }

    float getCurrent() const noexcept { return current; }
    float getTarget() const noexcept { return target; }
    bool isSmoothing() const noexcept { return countdown > 0; }

private:
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int countdown = 0, rampLength = 1;
};

/** Simple DC blocker (one-pole high-pass around 5–10 Hz). */
class DCBlocker
{
public:
    void prepare (double sampleRate, double cutoffHz = 7.0) noexcept
    {
        r = (float) std::exp (-kTwoPi * cutoffHz / sampleRate);
        reset();
    }
    void reset() noexcept { x1 = y1 = 0.0f; }
    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float r = 0.999f, x1 = 0.0f, y1 = 0.0f;
};

} // namespace af::dsp

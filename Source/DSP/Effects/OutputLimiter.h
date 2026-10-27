// ZackAMP — output safety stage. This is a protection device for ears and speakers,
// not a tone tool: factory presets are levelled well below its ceiling so it should
// rarely act. Instant-attack peak gain computer (no look-ahead, to add no latency),
// smooth release, then a hard clamp as a last line of defence.
#pragma once

#include "../Utilities/DspCommon.h"

namespace af::dsp
{
class OutputLimiter
{
public:
    void prepare (double fs)
    {
        release = onePoleCoeff (0.15, fs);
        reset();
    }

    void reset() noexcept { gain = 1.0f; reduction = 0.0f; }

    void process (float* l, float* r, int n, float ceiling = 0.89f) noexcept // ~ -1 dBFS
    {
        float maxRed = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float pk = std::max (std::abs (l[i]), std::abs (r[i]));
            const float needed = pk > ceiling ? ceiling / pk : 1.0f;
            gain = needed < gain ? needed : needed + release * (gain - needed);
            l[i] = clampv (sanitise (l[i] * gain), -1.0f, 1.0f);
            r[i] = clampv (sanitise (r[i] * gain), -1.0f, 1.0f);
            maxRed = std::max (maxRed, 1.0f - gain);
        }
        reduction = maxRed;
    }

    /** Gain reduction in the last block (0 = none), for the UI. */
    float getReduction() const noexcept { return reduction; }

private:
    float gain = 1.0f, release = 0.999f, reduction = 0.0f;
};

} // namespace af::dsp

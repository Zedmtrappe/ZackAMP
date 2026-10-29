// Z.AMP — analogue VU meter used as a gain-reduction meter (like the LA-2A's in GR mode): the
// needle rests at 0 VU and swings left as the compressor works. Real VU scale (deflection follows
// voltage, -20 to +3) and real VU ballistics (about 300 ms to settle, slight overshoot).
#pragma once

#include "ForgeLookAndFeel.h"

#include <functional>

namespace af::ui
{
class VuMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    VuMeter();
    /** Where the gain reduction comes from (dB, >= 0) and whether the meter is lit. */
    std::function<float()> readGainReduction;
    std::function<bool()> isActive;

    /** Call from the UI timer (~30 Hz). */
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override { face = {}; }

    void setShowReadout (bool shouldShow) { showReadout = shouldShow; repaint(); }

private:
    static float positionFor (float vu) noexcept; // 0..1 across the arc
    void renderFace (float scale);

    float needle = 0.0f, velocity = 0.0f; // needle position 0..1 and its speed
    float shownGr = 0.0f;
    double lastTime = 0.0;
    bool lit = false, showReadout = false;
    juce::Image face;
    float faceScale = 0.0f;
};
} // namespace af::ui

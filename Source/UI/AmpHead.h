// ZackAMP — the main amp, drawn as an old-school amp head: tolex cabinet with handle and piping,
// a glass front (edge-lit purple, "ZackAMP" etched into it) showing the valves and transformers
// behind it, and the control faceplate below.
//
// Static artwork is rendered once into cached images (per size and display scale); each frame only
// draws those images plus a handful of glow gradients. Animation runs on the UI timer only.
#pragma once

#include "Panels.h"

namespace af::ui
{
class GlassWindow final : public juce::Component
{
public:
    explicit GlassWindow (AppController& app);
    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called from the UI timer (~30 Hz): eases glow levels towards their targets. */
    void tick();

private:
    struct Valve
    {
        juce::Rectangle<float> body; // whole valve incl. base, in glass-local coordinates
        juce::Rectangle<float> plate;
        juce::Path envelope;
        bool power = false;
        float flicker = 1.0f;
    };

    void layoutParts();
    void renderLayers (float scale);
    void drawBackWall (juce::Graphics&) const;
    void drawParts (juce::Graphics&) const;
    void drawFront (juce::Graphics&) const;
    void drawLit (juce::Graphics&) const;
    juce::Path makeLogoPath() const;

    // Photo-realistic interior: rendered light layers mixed in linear light (see Tools/Render3D).
    void prepareLayers (int pixelW, int pixelH);
    void compositeLayers();
    bool photo = false;
    enum Layer { baseLayer, powerGlow, preGlow, trafoLayer, ledLayer, numLayers };
    std::array<juce::Image, numLayers> layers;  // cropped and scaled to the glass, in pixels
    juce::Image composite;
    int layersValve = -1;
    bool compositeDirty = true;

    AppController& app;
    juce::Rectangle<float> glass, ptRect, otRect;
    std::vector<Valve> valves;
    int shownPreValve = -1, shownPowValve = -1;

    juce::Image backLayer, partsLayer, frontLayer, litLayer;
    float cacheScale = 0.0f;

    // Animated state
    float heat = 0.3f, heatTarget = 0.3f;     // 0 = faint orange … 1 = white hot
    float power = 0.0f;                       // heaters on (audio running)
    float led = 0.0f;                         // purple edge lighting
    float trafoGlow = 0.0f, trafoTarget = 0.0f;
    uint64_t lastCallbacks = 0;
    int idleTicks = 1000, tickCount = 0;
    juce::Random rng;
};

class AmpHead final : public juce::Component, public Refreshable
{
public:
    explicit AmpHead (AppController& app);
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kMinHeight = 520;
    static constexpr int kPreferredHeight = 640;
    static constexpr int kMaxHeight = 760;

private:
    GlassWindow glass;
    AmpPanel panel;
    juce::Image tolex;
    juce::Rectangle<float> cabinet;
};
} // namespace af::ui

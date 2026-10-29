#include "VuMeter.h"

namespace af::ui
{
namespace
{
    constexpr float kArcStart = juce::MathConstants<float>::pi * -0.27f; // needle angle at -20 VU
    constexpr float kArcEnd = juce::MathConstants<float>::pi * 0.27f;    // ... at +3 VU

    /** Pivot below the face and the scale radius, for a face rectangle. */
    struct Geometry
    {
        juce::Point<float> pivot;
        float radius;
    };
    Geometry geometryFor (juce::Rectangle<float> f)
    {
        const float radius = std::min (f.getWidth() * 0.56f, f.getHeight() * 1.0f);
        return { { f.getCentreX(), f.getY() + f.getHeight() * 0.18f + radius }, radius };
    }
    juce::Point<float> onArc (const Geometry& g, float angle, float radius)
    {
        return g.pivot + juce::Point<float> (std::sin (angle), -std::cos (angle)) * radius;
    }
} // namespace

VuMeter::VuMeter()
{
    setTooltip ("Gain reduction (VU): the needle rests at 0 and swings left as the compressor works.");
    setOpaque (false);
}

float VuMeter::positionFor (float vu) noexcept
{
    // VU deflection follows voltage: 0 VU sits at 1/1.413 of full scale (+3 VU).
    const float p = std::pow (10.0f, vu / 20.0f) / 1.4125f;
    return juce::jlimit (0.0f, 1.0f, (p - 0.0707f) / (1.0f - 0.0707f)); // -20 VU at the left end
}

void VuMeter::refresh()
{
    const bool active = isActive ? isActive() : true;
    const float gr = active && readGainReduction ? std::max (0.0f, readGainReduction()) : 0.0f;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = lastTime > 0.0 ? (float) juce::jlimit (0.0, 0.1, now - lastTime) : 0.033f;
    lastTime = now;

    // Second-order needle: critically-ish damped (zeta 0.8), about 300 ms to settle.
    const float target = positionFor (-gr);
    constexpr float omega = 13.0f, zeta = 0.8f;
    const int steps = 8;
    const float h = dt / (float) steps;
    for (int i = 0; i < steps; ++i)
    {
        const float accel = omega * omega * (target - needle) - 2.0f * zeta * omega * velocity;
        velocity += accel * h;
        needle += velocity * h;
    }
    shownGr += (gr - shownGr) * 0.25f;
    if (active != lit)
    {
        lit = active;
        face = {};
    }
    repaint();
}

void VuMeter::renderFace (float scale)
{
    const auto b = getLocalBounds().toFloat();
    face = juce::Image (juce::Image::ARGB, std::max (1, juce::roundToInt (b.getWidth() * scale)),
                        std::max (1, juce::roundToInt (b.getHeight() * scale)), true);
    faceScale = scale;
    juce::Graphics g (face);
    g.addTransform (juce::AffineTransform::scale (scale));

    // Bezel and the cream face, warmly back-lit when the compressor is on.
    g.setColour (juce::Colour (0xff0b0c0e));
    g.fillRoundedRectangle (b, 5.0f);
    const auto f = b.reduced (3.0f);
    const auto cream = lit ? juce::Colour (0xfff5e4b3) : juce::Colour (0xffcfc5a6);
    juce::ColourGradient bg (cream.brighter (0.06f), f.getCentreX(), f.getY() + f.getHeight() * 0.35f,
                             cream.darker (lit ? 0.18f : 0.32f), f.getX(), f.getBottom(), true);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (f, 3.5f);

    g.saveState();
    g.reduceClipRegion (f.toNearestInt());
    const auto geo = geometryFor (f);
    const float r = geo.radius * 0.76f;
    const auto ink = juce::Colour (0xff1c1a16);
    const auto red = juce::Colour (0xffb8261b);
    // Scale arc: black up to 0 VU, red above.
    auto arc = [&] (float from, float to, juce::Colour c, float thick)
    {
        juce::Path p;
        p.addCentredArc (geo.pivot.x, geo.pivot.y, r, r, 0.0f, kArcStart + (kArcEnd - kArcStart) * from,
                         kArcStart + (kArcEnd - kArcStart) * to, true);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thick));
    };
    const float zero = positionFor (0.0f);
    arc (0.0f, zero, ink, 1.2f);
    arc (zero, 1.0f, red, 3.0f);
    const float fontH = juce::jlimit (6.5f, 13.0f, f.getHeight() * 0.13f);
    g.setFont (ForgeLookAndFeel::labelFont (fontH, false));
    for (float vu : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f })
    {
        const float a = kArcStart + (kArcEnd - kArcStart) * positionFor (vu);
        const bool major = vu == -20.0f || vu == -10.0f || vu == -5.0f || vu == 0.0f || vu == 3.0f || vu == -3.0f || vu == -7.0f;
        g.setColour (vu > 0.0f ? red : ink);
        g.drawLine (juce::Line<float> (onArc (geo, a, r), onArc (geo, a, r + (major ? fontH * 0.75f : fontH * 0.45f))), major ? 1.3f : 1.0f);
        if (major || f.getWidth() > 180.0f)
        {
            const auto c = onArc (geo, a, r + fontH * 1.45f);
            const auto text = vu > 0.0f ? "+" + juce::String ((int) vu) : juce::String ((int) std::abs (vu));
            g.drawText (text, juce::Rectangle<float> (fontH * 2.4f, fontH * 1.2f).withCentre (c), juce::Justification::centred, false);
        }
    }
    // Legends.
    g.setColour (ink.withAlpha (0.85f));
    g.setFont (ForgeLookAndFeel::labelFont (fontH * 1.25f));
    g.drawText ("VU", juce::Rectangle<float> (f.getWidth(), fontH * 1.6f).withCentre ({ f.getCentreX(), f.getY() + f.getHeight() * 0.62f }),
                juce::Justification::centred, false);
    g.setFont (ForgeLookAndFeel::labelFont (fontH * 0.75f, false));
    g.drawText ("GAIN REDUCTION", juce::Rectangle<float> (f.getWidth(), fontH).withCentre ({ f.getCentreX(), f.getY() + f.getHeight() * 0.76f }),
                juce::Justification::centred, false);
    g.restoreState();
}

void VuMeter::paint (juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (face.isNull() || std::abs (scale - faceScale) > 0.01f)
        renderFace (scale);
    const auto b = getLocalBounds().toFloat();
    g.drawImage (face, b, juce::RectanglePlacement::stretchToFit);

    const auto f = b.reduced (3.0f);
    const auto geo = geometryFor (f);
    g.saveState();
    g.reduceClipRegion (f.toNearestInt());
    // Needle with a soft shadow, then the pivot housing.
    const float a = kArcStart + (kArcEnd - kArcStart) * juce::jlimit (-0.02f, 1.03f, needle);
    const auto tip = onArc (geo, a, geo.radius * 0.93f);
    const auto base = onArc (geo, a, geo.radius * 0.30f);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.drawLine (juce::Line<float> (base.translated (1.5f, 2.0f), tip.translated (1.5f, 2.0f)), 1.6f);
    g.setColour (juce::Colour (0xff121110));
    g.drawLine (juce::Line<float> (base, tip), 1.3f);
    const float hub = f.getHeight() * 0.16f;
    juce::Path housing;
    housing.addRoundedRectangle (f.getCentreX() - f.getWidth() * 0.2f, f.getBottom() - hub, f.getWidth() * 0.4f, hub * 2.0f, hub * 0.4f);
    g.setColour (juce::Colour (0xff17181b));
    g.fillPath (housing);
    g.restoreState();

    // Glass: a soft highlight across the top.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.20f), 0, f.getY(), juce::Colours::white.withAlpha (0.0f), 0,
                                             f.getY() + f.getHeight() * 0.45f, false));
    g.fillRoundedRectangle (f.withHeight (f.getHeight() * 0.45f), 3.5f);

    if (showReadout) // numeric readout on the pivot housing
    {
        g.setColour (juce::Colour (0xffe8d9b0));
        g.setFont (ForgeLookAndFeel::labelFont (juce::jlimit (9.0f, 13.0f, f.getHeight() * 0.09f)));
        g.drawText (juce::String (shownGr, 1) + " dB", juce::Rectangle<float> (f.getWidth() * 0.4f, hub).withCentre ({ f.getCentreX(), f.getBottom() - hub * 0.5f }),
                    juce::Justification::centred, false);
    }
}

} // namespace af::ui

#include "AmpHead.h"
#include "../DSP/Amplifiers/Valves.h"
#if ZAMP_HAS_INTERIOR
 #include "ZampInterior.h"
#endif

namespace af::ui
{
namespace
{
    const juce::Colour kPurple { 0xffa848ff };
    const juce::Colour kPurpleLight { 0xffe9ccff };

    // Valve heat per amp model: faint orange on the cleanest model … white hot on the heaviest.
    // Order: Tweed, JC120, AC30, JCM800, Coliseum, MP1, Triple.
    constexpr float kModelHeat[] = { 0.32f, 0.12f, 0.45f, 0.66f, 0.80f, 0.90f, 1.0f, 0.96f, 0.58f };
    static_assert (std::size (kModelHeat) == dsp::kNumAmpModels);

    juce::Colour heatColour (float h)
    {
        h = juce::jlimit (0.0f, 1.15f, h);
        struct Stop { float at; juce::uint32 c; };
        static const Stop stops[] = { { 0.0f, 0xffc8400a }, { 0.4f, 0xffff7d22 }, { 0.7f, 0xffffb850 },
                                      { 0.9f, 0xffffe2a8 }, { 1.0f, 0xfffff7ee }, { 1.15f, 0xffffffff } };
        for (size_t i = 1; i < std::size (stops); ++i)
            if (h <= stops[i].at)
            {
                const float t = (h - stops[i - 1].at) / (stops[i].at - stops[i - 1].at);
                return juce::Colour (stops[i - 1].c).interpolatedWith (juce::Colour (stops[i].c), t);
            }
        return juce::Colours::white;
    }

    void radialGlow (juce::Graphics& g, juce::Point<float> c, float rx, float ry, juce::Colour col, float alpha)
    {
        if (alpha <= 0.002f || rx < 1.0f)
            return;
        juce::ColourGradient grad (col.withMultipliedAlpha (alpha), c.x, c.y, col.withAlpha (0.0f), c.x + rx, c.y, true);
        grad.addColour (0.35, col.withMultipliedAlpha (alpha * 0.55f));
        g.setGradientFill (grad);
        g.saveState();
        g.addTransform (juce::AffineTransform::scale (1.0f, ry / rx, c.x, c.y));
        g.fillEllipse (c.x - rx, c.y - rx, rx * 2.0f, rx * 2.0f);
        g.restoreState();
    }

    juce::Font logoFont (float height)
    {
        static const juce::StringArray preferred { "Futura", "Avenir Next", "Gill Sans", "Inter" };
        static const juce::String family = []
        {
            const auto all = juce::Font::findAllTypefaceNames();
            for (const auto& f : preferred)
                if (all.contains (f))
                    return f;
            return juce::String();
        }();
        auto opts = juce::FontOptions (height, juce::Font::bold);
        if (family.isNotEmpty())
            opts = opts.withName (family);
        return juce::Font (opts).withExtraKerningFactor (0.04f);
    }

    /** Loads one rendered interior layer, e.g. "base_6L6.jpg" (null if not bundled). */
    juce::Image loadInterior (const juce::String& file)
    {
#if ZAMP_HAS_INTERIOR
        int size = 0;
        const auto resource = file.replaceCharacter ('.', '_');
        if (const auto* data = ZampInterior::getNamedResource (resource.toRawUTF8(), size))
            return juce::ImageCache::getFromMemory (data, size);
#else
        juce::ignoreUnused (file);
#endif
        return {};
    }

    struct LinearTables
    {
        std::array<float, 256> toLinear {};
        std::array<juce::uint8, 4097> toSrgb {};
        LinearTables()
        {
            for (int i = 0; i < 256; ++i)
            {
                const float c = (float) i / 255.0f;
                toLinear[(size_t) i] = c <= 0.04045f ? c / 12.92f : std::pow ((c + 0.055f) / 1.055f, 2.4f);
            }
            for (int i = 0; i <= 4096; ++i)
            {
                const float l = (float) i / 4096.0f;
                const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow (l, 1.0f / 2.4f) - 0.055f;
                toSrgb[(size_t) i] = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (c * 255.0f));
            }
        }
    };
    const LinearTables& tables()
    {
        static const LinearTables t;
        return t;
    }

    juce::Image makeTolex()
    {
        juce::Image img (juce::Image::RGB, 96, 96, false);
        juce::Random rng (1234);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < 96; ++y)
            for (int x = 0; x < 96; ++x)
            {
                // Pebbled vinyl: dark base, fine speckle, slight diagonal grain.
                const float n = rng.nextFloat();
                const float grain = 0.5f + 0.5f * std::sin ((float) (x + y) * 0.9f + rng.nextFloat() * 0.6f);
                const auto v = (juce::uint8) juce::jlimit (0, 255, (int) (17.0f + 9.0f * n * n + 3.0f * grain));
                bd.setPixelColour (x, y, juce::Colour (v, v, (juce::uint8) std::min (255, v + 1)));
            }
        return img;
    }
} // namespace

//==============================================================================
GlassWindow::GlassWindow (AppController& a) : app (a)
{
    photo = loadInterior ("base_6L6.jpg").isValid();
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
}

void GlassWindow::resized()
{
    glass = getLocalBounds().toFloat().reduced (9.0f);
    layoutParts();
    cacheScale = 0.0f;
}

void GlassWindow::layoutParts()
{
    const float W = glass.getWidth(), H = glass.getHeight();
    const float deck = H * 0.83f;
    shownPreValve = app.params().getInt (pAmpPreampValve);
    shownPowValve = app.params().getInt (pAmpPowerValve);

    // Transformers at both ends: power transformer left, output transformer right.
    ptRect = { W * 0.03f, deck - H * 0.50f, W * 0.135f, H * 0.50f };
    otRect = { W * 0.845f, deck - H * 0.43f, W * 0.12f, H * 0.43f };

    valves.clear();
    auto addValve = [&] (float cx, float w, float h, bool isPower)
    {
        Valve v;
        v.power = isPower;
        v.body = { cx - w * 0.5f, deck - h, w, h + H * 0.02f };
        const float baseH = isPower ? v.body.getHeight() * 0.15f : v.body.getHeight() * 0.04f;
        const auto env = v.body.withTrimmedBottom (baseH);
        v.envelope.addRoundedRectangle (env.getX(), env.getY(), env.getWidth(), env.getHeight(), env.getWidth() * 0.5f,
                                        env.getWidth() * 0.42f, true, true, false, false);
        if (! isPower) // exhaust tip
            v.envelope.addRoundedRectangle (env.getCentreX() - w * 0.09f, env.getY() - w * 0.16f, w * 0.18f, w * 0.24f, w * 0.08f);
        v.plate = env.withSizeKeepingCentre (env.getWidth() * (isPower ? 0.64f : 0.70f), env.getHeight() * (isPower ? 0.50f : 0.46f))
                      .withY (env.getY() + env.getHeight() * (isPower ? 0.28f : 0.30f));
        valves.push_back (std::move (v));
    };

    // Preamp: V1–V3 plus phase inverter (dual triodes).
    const float pw = juce::jlimit (16.0f, 44.0f, W * 0.032f);
    const float ph = H * 0.40f;
    for (int i = 0; i < 4; ++i)
        addValve (W * (0.215f + 0.062f * (float) i), pw, ph, false);

    // Power valves: size follows the selected type.
    static const float sizeOf[] = { 1.0f, 1.03f, 0.86f, 0.74f, 1.14f }; // 6L6 EL34 6V6 EL84 KT88
    const float s = sizeOf[(size_t) juce::jlimit (0, 4, shownPowValve)];
    const float bw = juce::jlimit (26.0f, 70.0f, W * 0.052f) * (shownPowValve == 3 ? 0.80f : 1.0f);
    for (int i = 0; i < 4; ++i)
        addValve (W * (0.505f + 0.088f * (float) i), bw * std::sqrt (s), std::min (H * 0.70f, H * 0.60f * s), true);
}

void GlassWindow::tick()
{
    const auto& p = app.params();
    const int model = juce::jlimit (0, dsp::kNumAmpModels - 1, p.getInt (pAmpModel));
    heatTarget = kModelHeat[model] + 0.06f * (p.get (pAmpGain) - 0.5f) + (p.getBool (pAmpPull) ? 0.05f : 0.0f);
    trafoTarget = model == 6 ? 1.0f : model == 7 ? 0.9f : model == 5 ? 0.75f : model == 4 ? 0.5f : 0.0f;

    const auto callbacks = app.getAudioCallbackCount();
    idleTicks = callbacks != lastCallbacks ? 0 : idleTicks + 1;
    lastCallbacks = callbacks;
    const bool running = idleTicks < 15;
    const float powerTarget = running ? 1.0f : 0.0f;
    const float ledTarget = running && ! p.getBool (pBypass) ? 1.0f : 0.0f;

    bool changed = false;
    auto ease = [&changed] (float& v, float target, float rate)
    {
        const float d = target - v;
        if (std::abs (d) > 0.001f)
        {
            v += d * rate;
            changed = true;
        }
        else if (v != target)
        {
            v = target;
            changed = true;
        }
    };
    ease (heat, heatTarget, 0.10f);
    ease (power, powerTarget, power < powerTarget ? 0.035f : 0.06f); // heaters warm up slowly
    ease (led, ledTarget, 0.18f);
    ease (trafoGlow, trafoTarget, 0.06f);

    // Gentle heater flicker, updated at 10 Hz (drawn interior only: the rendered one recomposites).
    if (! photo && ++tickCount % 3 == 0 && power > 0.05f)
    {
        for (auto& v : valves)
            v.flicker = 0.75f * v.flicker + 0.25f * (0.95f + 0.07f * rng.nextFloat());
        changed = true;
    }

    if (p.getInt (pAmpPreampValve) != shownPreValve || p.getInt (pAmpPowerValve) != shownPowValve)
    {
        layoutParts();
        cacheScale = 0.0f;
        changed = true;
    }
    if (changed)
    {
        compositeDirty = true;
        repaint();
    }
}

//==============================================================================
void GlassWindow::renderLayers (float scale)
{
    cacheScale = scale;
    const int w = std::max (1, juce::roundToInt ((float) getWidth() * scale));
    const int h = std::max (1, juce::roundToInt ((float) getHeight() * scale));
    auto render = [&] (juce::Image& img, bool opaque, auto&& fn)
    {
        img = juce::Image (opaque ? juce::Image::RGB : juce::Image::ARGB, w, h, true);
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));
        fn (g);
    };
    if (! photo)
    {
        render (backLayer, true, [this] (juce::Graphics& g) { drawBackWall (g); });
        render (partsLayer, false, [this] (juce::Graphics& g) { drawParts (g); });
    }
    render (frontLayer, false, [this] (juce::Graphics& g) { drawFront (g); });
    render (litLayer, false, [this] (juce::Graphics& g) { drawLit (g); });
}

void GlassWindow::drawBackWall (juce::Graphics& g) const
{
    g.fillAll (juce::Colour (0xff09090a));
    g.saveState();
    g.setOrigin (glass.getPosition().toInt());
    const float W = glass.getWidth(), H = glass.getHeight(), deck = H * 0.83f;

    // Back wall: dark perforated steel.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff17181b), 0, 0, juce::Colour (0xff0c0c0e), 0, deck, false));
    g.fillRect (0.0f, 0.0f, W, deck);
    for (float y = 7.0f; y < deck - 4.0f; y += 9.0f)
        for (float x = 6.0f + std::fmod (y, 18.0f) * 0.5f; x < W - 4.0f; x += 9.0f)
        {
            g.setColour (juce::Colour (0xff1f2024));
            g.fillEllipse (x - 2.0f, y - 1.2f, 4.0f, 4.0f);
            g.setColour (juce::Colour (0xff040405));
            g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
        }

    // Chassis deck: brushed aluminium seen from the front.
    juce::Rectangle<float> deckRect (0.0f, deck, W, H - deck);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a6d73), 0, deck, juce::Colour (0xff2b2c30), 0, H, false));
    g.fillRect (deckRect);
    juce::Random brush (77);
    for (float y = deck + 2.0f; y < H; y += 1.5f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.02f + 0.04f * brush.nextFloat()));
        g.drawHorizontalLine ((int) y, 0.0f, W);
    }
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawHorizontalLine ((int) deck, 0.0f, W);

    // Vignette
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), W * 0.5f, H * 0.45f,
                                             juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true));
    g.fillRect (0.0f, 0.0f, W, H);
    g.restoreState();
}

void GlassWindow::drawParts (juce::Graphics& g) const
{
    g.saveState();
    g.setOrigin (glass.getPosition().toInt());

    // Transformers: laminated core between two black end bells, bolted to the deck.
    auto drawTransformer = [&g] (juce::Rectangle<float> r)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillEllipse (r.withY (r.getBottom() - 8.0f).withHeight (14.0f).expanded (6.0f, 0.0f));
        auto core = r.reduced (r.getWidth() * 0.22f, r.getHeight() * 0.04f);
        g.setColour (juce::Colour (0xff26272a));
        g.fillRect (core);
        for (float y = core.getY(); y < core.getBottom(); y += 2.0f)
        {
            g.setColour (juce::Colour (0xff3d3f44).withAlpha (0.8f));
            g.drawHorizontalLine ((int) y, core.getX(), core.getRight());
        }
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), core.getX(), 0,
                                                 juce::Colours::black.withAlpha (0.3f), core.getRight(), 0, false));
        g.fillRect (core);
        for (auto bell : { r.withWidth (r.getWidth() * 0.26f), r.withTrimmedLeft (r.getWidth() * 0.74f) })
        {
            juce::ColourGradient grad (juce::Colour (0xff2c2d31), bell.getX(), bell.getY(), juce::Colour (0xff0e0e10), bell.getRight(), bell.getBottom(), false);
            grad.addColour (0.3, juce::Colour (0xff3a3c41));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bell, 6.0f);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawRoundedRectangle (bell.reduced (0.5f), 6.0f, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.fillRoundedRectangle (bell.withWidth (3.0f).translated (bell.getWidth() * 0.25f, 0).reduced (0.0f, 8.0f), 1.5f);
        }
        for (auto pt : { core.getTopLeft(), core.getTopRight(), core.getBottomLeft(), core.getBottomRight() })
        {
            const auto c = pt.translated (pt.x < core.getCentreX() ? 7.0f : -7.0f, pt.y < core.getCentreY() ? 7.0f : -7.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd5d8dd), c.x - 2, c.y - 2, juce::Colour (0xff55585e), c.x + 3, c.y + 3, false));
            g.fillEllipse (c.x - 3.5f, c.y - 3.5f, 7.0f, 7.0f);
        }
    };
    drawTransformer (ptRect);
    drawTransformer (otRect);

    // Valve internals, bases and sockets (the glass envelopes are in the front layer).
    for (const auto& v : valves)
    {
        const auto env = v.envelope.getBounds();
        const float w = v.body.getWidth();
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.fillEllipse (v.body.getX() - w * 0.12f, v.body.getBottom() - 6.0f, w * 1.24f, 10.0f);
        if (v.power)
        {
            auto base = v.body.withTop (env.getBottom() - 1.0f);
            juce::ColourGradient grad (juce::Colour (0xff141414), base.getX(), 0, juce::Colour (0xff101010), base.getRight(), 0, false);
            grad.addColour (0.35, juce::Colour (0xff3b3a38));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (base, 3.0f);
            g.setColour (juce::Colour (0xffd8cfa8).withAlpha (0.5f)); // printed base marking
            g.fillRect (base.withSizeKeepingCentre (w * 0.3f, 1.5f));
        }

        g.saveState();
        g.reduceClipRegion (v.envelope);
        // Support rods
        g.setColour (juce::Colour (0xff6d6f74));
        for (float fx : { 0.24f, 0.76f })
            g.drawVerticalLine ((int) (env.getX() + env.getWidth() * fx), env.getY() + env.getHeight() * 0.2f, env.getBottom());
        // Plates: one for a power valve, two triode sections for a preamp valve.
        auto drawPlate = [&g] (juce::Rectangle<float> pr)
        {
            juce::ColourGradient grad (juce::Colour (0xff1c1d20), pr.getX(), 0, juce::Colour (0xff1a1b1d), pr.getRight(), 0, false);
            grad.addColour (0.3, juce::Colour (0xff55585e));
            grad.addColour (0.55, juce::Colour (0xff2a2b2f));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (pr, 1.5f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawVerticalLine ((int) pr.getCentreX(), pr.getY() + 2.0f, pr.getBottom() - 2.0f);
        };
        if (v.power)
            drawPlate (v.plate);
        else
        {
            drawPlate (v.plate.withTrimmedBottom (v.plate.getHeight() * 0.53f));
            drawPlate (v.plate.withTrimmedTop (v.plate.getHeight() * 0.53f));
        }
        // Mica spacers
        g.setColour (juce::Colour (0xffcfc6b0).withAlpha (0.55f));
        for (float y : { v.plate.getY() - 3.0f, v.plate.getBottom() + 1.0f })
            g.fillRect (env.getX() + 2.0f, y, env.getWidth() - 4.0f, 2.0f);
        // Getter flash (mirrored top)
        const auto getter = env.withHeight (env.getHeight() * (v.power ? 0.16f : 0.14f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9aa0a8).withAlpha (0.85f), getter.getX(), getter.getY(),
                                                 juce::Colour (0xff1d1e21).withAlpha (0.2f), getter.getX(), getter.getBottom(), false));
        g.fillRect (getter);
        g.restoreState();
    }
    g.restoreState();
}

namespace
{
    /** One wall of the etched groove: the band inside `logo` that is uncovered when the logo is
        shifted by `offset` — i.e. the inner wall facing away from that direction. Built up in
        several steps so the wall shades softly from the lip down to the floor. */
    void drawGrooveWall (juce::Graphics& g, const juce::Path& logo, juce::Point<float> offset, juce::Colour colour, int steps)
    {
        for (int i = steps; i >= 1; --i)
        {
            const float t = (float) i / (float) steps;
            juce::Path wall (logo);
            wall.addPath (logo, juce::AffineTransform::translation (offset.x * t, offset.y * t));
            wall.setUsingNonZeroWinding (false); // logo minus its shifted copy
            g.saveState();
            g.reduceClipRegion (logo);
            g.setColour (colour.withMultipliedAlpha (1.0f / (float) steps * (1.6f - 0.6f * t)));
            g.fillPath (wall);
            g.restoreState();
        }
    }

    /** The logo etched into the glass as a V-groove lit from above: the upper walls in shadow, the
        lower walls catching the light, a bright lip along the top edge and a frosted floor. With
        `lit`, the LED light trapped in the glass makes the cut surfaces glow. */
    void drawEtchedLogo (juce::Graphics& g, const juce::Path& logo, bool lit)
    {
        const float d = juce::jlimit (1.5f, 5.0f, logo.getBounds().getHeight() * 0.045f); // groove depth as seen
        const auto floorColour = lit ? kPurpleLight : juce::Colours::white;
        // Floor: frosted (scatters light), seen slightly lower than the lip by refraction.
        g.setColour (floorColour.withAlpha (lit ? 0.55f : 0.10f));
        g.fillPath (logo, juce::AffineTransform::translation (0.0f, d * 0.35f));
        // Walls: the upper ones face down, away from the light (dark); the lower ones face up (bright).
        drawGrooveWall (g, logo, { d * 0.35f, d }, juce::Colours::black.withAlpha (lit ? 0.5f : 0.75f), 4);
        drawGrooveWall (g, logo, { -d * 0.2f, -d * 0.8f }, juce::Colours::white.withAlpha (lit ? 0.85f : 0.42f), 4);
        // Lips: a fine highlight where the top edges break the surface, a dark line along the bottom
        // edges where the cut catches the shadow of the glass.
        g.saveState();
        juce::Path outside;
        outside.addRectangle (logo.getBounds().expanded (d * 3.0f));
        outside.addPath (logo);
        outside.setUsingNonZeroWinding (false);
        g.reduceClipRegion (outside);
        g.setColour (juce::Colours::white.withAlpha (lit ? 0.7f : 0.30f));
        g.strokePath (logo, juce::PathStrokeType (0.9f), juce::AffineTransform::translation (0.0f, -0.4f));
        g.setColour (juce::Colours::black.withAlpha (lit ? 0.25f : 0.45f));
        g.strokePath (logo, juce::PathStrokeType (0.9f), juce::AffineTransform::translation (0.0f, 0.6f));
        g.restoreState();
    }
} // namespace

juce::Path GlassWindow::makeLogoPath() const
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (logoFont (100.0f), "Z.AMP", 0.0f, 0.0f);
    juce::Path p;
    ga.createPath (p);
    const auto target = juce::Rectangle<float> (glass.getWidth() * 0.50f, glass.getHeight() * 0.30f)
                            .withCentre ({ glass.getCentreX(), glass.getY() + glass.getHeight() * 0.40f });
    p.applyTransform (p.getTransformToScaleToFit (target, true));
    return p;
}

void GlassWindow::drawFront (juce::Graphics& g) const
{
    // Glass envelopes of the valves, with printed type names (drawn interior only).
    g.saveState();
    g.setOrigin (glass.getPosition().toInt());
    for (size_t i = 0; i < (photo ? 0 : valves.size()); ++i)
    {
        const auto& v = valves[i];
        const auto env = v.envelope.getBounds();
        juce::ColourGradient body (juce::Colours::white.withAlpha (0.16f), env.getX(), 0, juce::Colours::white.withAlpha (0.13f), env.getRight(), 0, false);
        body.addColour (0.25, juce::Colours::white.withAlpha (0.03f));
        body.addColour (0.7, juce::Colours::white.withAlpha (0.02f));
        g.setGradientFill (body);
        g.fillPath (v.envelope);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.strokePath (v.envelope, juce::PathStrokeType (1.0f));
        auto streak = juce::Rectangle<float> (env.getX() + env.getWidth() * 0.17f, env.getY() + env.getHeight() * 0.10f,
                                              std::max (2.0f, env.getWidth() * 0.09f), env.getHeight() * 0.75f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.45f), 0, streak.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), 0, streak.getBottom(), false));
        g.fillRoundedRectangle (streak, streak.getWidth() * 0.5f);

        const char* name = v.power ? dsp::kPowerValves[(size_t) juce::jlimit (0, dsp::kNumPowerValves - 1, shownPowValve)].name
                                   : (i == 0 ? dsp::kPreampValves[(size_t) juce::jlimit (0, dsp::kNumPreampValves - 1, shownPreValve)].name : "12AX7");
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.setFont (ForgeLookAndFeel::labelFont (juce::jlimit (7.0f, 12.0f, env.getWidth() * 0.26f)));
        g.drawText (name, env.withY (env.getY() + env.getHeight() * (v.power ? 0.83f : 0.84f)).withHeight (12.0f),
                    juce::Justification::centred, false);
    }
    g.restoreState();

    // The glass sheet: slight tint, top gloss and diagonal reflections.
    g.setColour (juce::Colour (0xff10121a).withAlpha (0.12f));
    g.fillRect (glass);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), 0, glass.getY(),
                                             juce::Colours::white.withAlpha (0.0f), 0, glass.getY() + glass.getHeight() * 0.3f, false));
    g.fillRect (glass);
    g.saveState();
    g.reduceClipRegion (glass.toNearestInt());
    for (auto [x0, wFrac, a] : { std::tuple { 0.10f, 0.10f, 0.05f }, std::tuple { 0.24f, 0.035f, 0.045f }, std::tuple { 0.70f, 0.06f, 0.03f } })
    {
        juce::Path band;
        const float x = glass.getX() + glass.getWidth() * x0, bw = glass.getWidth() * wFrac, sk = glass.getHeight() * 0.55f;
        band.startNewSubPath (x + sk, glass.getY());
        band.lineTo (x + sk + bw, glass.getY());
        band.lineTo (x + bw - sk * 0.2f, glass.getBottom());
        band.lineTo (x - sk * 0.2f, glass.getBottom());
        band.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (a));
        g.fillPath (band);
    }
    g.restoreState();

    // Etched (frosted) logo, as seen with the LEDs off.
    drawEtchedLogo (g, makeLogoPath(), false);

    // Bezel around the glass.
    const auto outer = getLocalBounds().toFloat();
    juce::Path frame;
    frame.addRoundedRectangle (outer, 8.0f);
    frame.addRectangle (glass);
    frame.setUsingNonZeroWinding (false);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3b40), 0, outer.getY(), juce::Colour (0xff111114), 0, outer.getBottom(), false));
    g.fillPath (frame);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (outer.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRect (glass.expanded (1.0f), 1.5f);
    // Inner shadow cast by the bezel onto the glass.
    for (int i = 0; i < 6; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.10f - 0.016f * (float) i));
        g.drawRect (glass.reduced ((float) i), 1.0f);
    }
}

void GlassWindow::drawLit (juce::Graphics& g) const
{
    // Light from the LED strips enters at the glass edges...
    const float depth = std::min (34.0f, glass.getHeight() * 0.12f);
    auto edge = [&g] (juce::Rectangle<float> r, juce::Point<float> from, juce::Point<float> to)
    {
        juce::ColourGradient grad (kPurple.withAlpha (0.55f), from.x, from.y, kPurple.withAlpha (0.0f), to.x, to.y, false);
        grad.addColour (0.15, kPurple.withAlpha (0.28f));
        g.setGradientFill (grad);
        g.fillRect (r);
    };
    const auto gl = glass;
    edge (gl.withHeight (depth), { 0, gl.getY() }, { 0, gl.getY() + depth });
    edge (gl.withTop (gl.getBottom() - depth), { 0, gl.getBottom() }, { 0, gl.getBottom() - depth });
    edge (gl.withWidth (depth), { gl.getX(), 0 }, { gl.getX() + depth, 0 });
    edge (gl.withLeft (gl.getRight() - depth), { gl.getRight(), 0 }, { gl.getRight() - depth, 0 });
    g.setColour (kPurpleLight.withAlpha (0.85f));
    g.drawRect (gl.reduced (0.5f), 1.5f);

    // ...and is scattered out by the etching, so the logo glows.
    const auto logo = makeLogoPath();
    juce::DropShadow (kPurple.withAlpha (0.75f), 38, {}).drawForPath (g, logo);
    juce::DropShadow (kPurple.withAlpha (0.9f), 14, {}).drawForPath (g, logo);
    const auto b = logo.getBounds();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc58bff).withAlpha (0.55f), 0, b.getY(), kPurple.withAlpha (0.5f), 0, b.getBottom(), false));
    g.fillPath (logo);
    drawEtchedLogo (g, logo, true);
}

namespace
{
    // Where the V1 patches (v1_<type>.jpg, v1g_<type>.jpg) sit in the full render, as fractions
    // of its size (left, top, right, bottom). Written by Tools/Render3D/amp_interior.py.
    constexpr float kV1Patch[4] = { 0.17f, 0.46f, 0.26f, 0.86f };

    /** Blends a patch into `full` at kV1Patch, feathering its edges so the seam never shows. */
    void pastePatch (juce::Image& full, const juce::Image& patch)
    {
        if (! patch.isValid())
            return;
        const int x0 = juce::roundToInt (kV1Patch[0] * (float) full.getWidth());
        const int y0 = juce::roundToInt (kV1Patch[1] * (float) full.getHeight());
        const int pw = std::min (patch.getWidth(), full.getWidth() - x0), ph = std::min (patch.getHeight(), full.getHeight() - y0);
        const float feather = (float) std::max (4, std::min (pw, ph) / 8);
        full = full.createCopy(); // the source may be shared with the image cache
        juce::Image::BitmapData out (full, juce::Image::BitmapData::readWrite);
        juce::Image::BitmapData in (patch, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < ph; ++y)
            for (int x = 0; x < pw; ++x)
            {
                const float edge = (float) std::min (std::min (x, pw - 1 - x), std::min (y, ph - 1 - y));
                const float a = juce::jlimit (0.0f, 1.0f, edge / feather);
                const auto s = in.getPixelColour (x, y), d = out.getPixelColour (x0 + x, y0 + y);
                out.setPixelColour (x0 + x, y0 + y, d.interpolatedWith (s, a));
            }
    }
} // namespace

void GlassWindow::prepareLayers (int pixelW, int pixelH)
{
    const auto* valveName = dsp::kPowerValves[(size_t) juce::jlimit (0, dsp::kNumPowerValves - 1, shownPowValve)].name;
    const juce::String preName = dsp::kPreampValves[(size_t) juce::jlimit (0, dsp::kNumPreampValves - 1, shownPreValve)].name;
    const juce::String files[numLayers] = { juce::String ("base_") + valveName + ".jpg", juce::String ("pow_") + valveName + ".jpg",
                                            "pre.jpg", "trafo.jpg", "led.jpg" };
    for (int i = 0; i < numLayers; ++i)
    {
        auto src = loadInterior (files[i]);
        if (! src.isValid() && (i == baseLayer || i == powerGlow)) // fall back to the 6L6 renders
            src = loadInterior (files[i].replace (valveName, "6L6"));
        if (! src.isValid())
        {
            layers[(size_t) i] = {};
            continue;
        }
        // V1 shows the selected preamp valve type.
        if (i == baseLayer)
            pastePatch (src, loadInterior ("v1_" + preName + ".jpg"));
        else if (i == preGlow)
            pastePatch (src, loadInterior ("v1g_" + preName + ".jpg"));

        // Cover the glass: crop the render to the glass aspect ratio, then scale to the glass size
        // in physical pixels. A wide glass keeps the chassis deck and valve bases (the bottom of
        // the crop stays near 90 % of the render); a tall glass zooms in around the centre.
        const float aspect = (float) pixelW / (float) pixelH;
        auto crop = src.getBounds();
        if ((float) crop.getWidth() / (float) crop.getHeight() > aspect)
            crop = crop.withSizeKeepingCentre (juce::roundToInt ((float) crop.getHeight() * aspect), crop.getHeight());
        else
        {
            const int h = juce::roundToInt ((float) crop.getWidth() / aspect);
            const int top = juce::jlimit (0, src.getHeight() - h, juce::roundToInt (0.90f * (float) src.getHeight()) - h);
            crop = { 0, top, crop.getWidth(), h };
        }
        layers[(size_t) i] = src.getClippedImage (crop).rescaled (pixelW, pixelH, juce::Graphics::highResamplingQuality)
                                 .convertedToFormat (juce::Image::RGB);
    }
    composite = juce::Image (juce::Image::RGB, pixelW, pixelH, false);
    layersValve = shownPowValve;
    layersPre = shownPreValve;
    compositeDirty = true;
}

void GlassWindow::compositeLayers()
{
    compositeDirty = false;
    if (! layers[baseLayer].isValid())
        return;
    const auto& t = tables();
    const auto lin = [] (juce::Colour c, float k)
    {
        const auto& tl = tables().toLinear;
        return std::array<float, 3> { tl[c.getRed()] * k, tl[c.getGreen()] * k, tl[c.getBlue()] * k };
    };
    // Heater light: the render is white light; tint it by the model's heat colour.
    const float heaterGain = (0.35f + 1.4f * heat) * power;
    const auto glow = lin (heatColour (heat), heaterGain);
    const auto trafo = lin (juce::Colours::white, trafoGlow * power * 1.6f);
    const auto purple = lin (juce::Colours::white, led);

    struct Src { const juce::Image* img; std::array<float, 3> k; };
    std::vector<Src> adds;
    auto addLayer = [&] (int index, std::array<float, 3> k)
    {
        if (layers[(size_t) index].isValid() && (k[0] > 0.0f || k[1] > 0.0f || k[2] > 0.0f))
            adds.push_back ({ &layers[(size_t) index], k });
    };
    addLayer (powerGlow, glow);
    addLayer (preGlow, glow);
    addLayer (trafoLayer, trafo);
    addLayer (ledLayer, purple);

    const int w = composite.getWidth(), h = composite.getHeight();
    juce::Image::BitmapData out (composite, juce::Image::BitmapData::writeOnly);
    juce::Image::BitmapData in (layers[baseLayer], juce::Image::BitmapData::readOnly);
    std::vector<std::unique_ptr<juce::Image::BitmapData>> addData;
    for (auto& a : adds)
        addData.push_back (std::make_unique<juce::Image::BitmapData> (*const_cast<juce::Image*> (a.img), juce::Image::BitmapData::readOnly));

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const auto* b = reinterpret_cast<const juce::PixelRGB*> (in.getPixelPointer (x, y));
            float r = t.toLinear[b->getRed()], gr = t.toLinear[b->getGreen()], bl = t.toLinear[b->getBlue()];
            for (size_t i = 0; i < adds.size(); ++i)
            {
                const auto* p = reinterpret_cast<const juce::PixelRGB*> (addData[i]->getPixelPointer (x, y));
                r += t.toLinear[p->getRed()] * adds[i].k[0];
                gr += t.toLinear[p->getGreen()] * adds[i].k[1];
                bl += t.toLinear[p->getBlue()] * adds[i].k[2];
            }
            auto enc = [&t] (float v) { return t.toSrgb[(size_t) juce::jlimit (0, 4096, (int) (v * 4096.0f))]; };
            reinterpret_cast<juce::PixelRGB*> (out.getPixelPointer (x, y))->setARGB (255, enc (r), enc (gr), enc (bl));
        }
}

void GlassWindow::paint (juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (std::abs (scale - cacheScale) > 0.01f || frontLayer.isNull())
        renderLayers (scale);
    const auto bounds = getLocalBounds().toFloat();
    auto drawLayer = [&] (const juce::Image& img, float opacity)
    {
        g.setOpacity (opacity);
        g.drawImage (img, bounds, juce::RectanglePlacement::stretchToFit);
    };

    if (photo)
    {
        const int pw = std::max (1, juce::roundToInt (glass.getWidth() * scale));
        const int ph = std::max (1, juce::roundToInt (glass.getHeight() * scale));
        if (composite.getWidth() != pw || composite.getHeight() != ph || layersValve != shownPowValve || layersPre != shownPreValve)
            prepareLayers (pw, ph);
        if (compositeDirty)
            compositeLayers();
        g.fillAll (juce::Colours::black);
        g.setOpacity (1.0f);
        g.drawImage (composite, glass, juce::RectanglePlacement::stretchToFit);
        drawLayer (frontLayer, 1.0f);
        if (led > 0.005f)
            drawLayer (litLayer, led);
        return;
    }

    drawLayer (backLayer, 1.0f);

    const float lvl = power;
    const auto col = heatColour (heat);
    const float intensity = (0.45f + 0.55f * heat) * lvl;
    g.saveState();
    g.setOrigin (glass.getPosition().toInt());
    // Warm halo on the back wall behind each valve and (modern high gain) the transformers.
    for (const auto& v : valves)
    {
        const auto c = v.plate.getCentre();
        const float r = v.body.getWidth() * (1.4f + 1.2f * heat) * (v.power ? 1.2f : 1.0f);
        radialGlow (g, c, r, r * 1.6f, col, 0.55f * intensity * v.flicker);
    }
    const auto trafoCol = juce::Colour (0xffff5a1e);
    for (auto r : { ptRect, otRect })
        radialGlow (g, r.getCentre(), r.getWidth() * 0.9f, r.getHeight() * 0.75f, trafoCol, 0.30f * trafoGlow * lvl);
    g.restoreState();

    drawLayer (partsLayer, 1.0f);

    g.saveState();
    g.setOrigin (glass.getPosition().toInt());
    // Transformer cores heat up (artistic licence on the modern models).
    if (trafoGlow * lvl > 0.01f)
        for (auto r : { ptRect, otRect })
        {
            const auto core = r.reduced (r.getWidth() * 0.22f, r.getHeight() * 0.04f);
            juce::ColourGradient grad (trafoCol.withAlpha (0.0f), 0, core.getY(), trafoCol.withAlpha (0.32f * trafoGlow * lvl), 0, core.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRect (core);
        }
    // Heater glow inside the valves: through the gaps and at the ends of the plate structure.
    for (const auto& v : valves)
    {
        g.saveState();
        g.reduceClipRegion (v.envelope);
        const float a = intensity * v.flicker;
        const float w = v.plate.getWidth();
        // Light leaking around the plates fills the envelope...
        radialGlow (g, v.plate.getCentre(), w * (0.9f + 0.6f * heat), v.plate.getHeight() * 0.85f, col, 0.75f * a);
        // ...the heater shows through the slot in the plate...
        const auto slot = v.plate.withSizeKeepingCentre (std::max (1.5f, w * 0.07f), v.plate.getHeight() * 0.9f);
        g.setColour (col.interpolatedWith (juce::Colours::white, 0.3f * heat).withMultipliedAlpha (juce::jlimit (0.0f, 1.0f, 1.2f * a)));
        g.fillRoundedRectangle (slot, slot.getWidth() * 0.5f);
        // ...and glows brightest where it leaves the plate structure, top and bottom.
        for (float y : { v.plate.getY() - 2.0f, v.plate.getBottom() + 2.0f })
        {
            radialGlow (g, { v.plate.getCentreX(), y }, w * (0.6f + 0.4f * heat), w * 0.32f, col, 1.4f * a);
            radialGlow (g, { v.plate.getCentreX(), y }, w * 0.3f, w * 0.10f, col.interpolatedWith (juce::Colours::white, 0.4f + 0.6f * heat), 1.2f * a);
        }
        g.restoreState();
    }
    g.restoreState();

    drawLayer (frontLayer, 1.0f);
    if (led > 0.005f)
        drawLayer (litLayer, led);
}

//==============================================================================
AmpHead::AmpHead (AppController& a) : glass (a), panel (a), tolex (makeTolex())
{
    addAndMakeVisible (glass);
    addAndMakeVisible (panel);
}

void AmpHead::refresh()
{
    panel.refresh();
    glass.tick();
}

void AmpHead::resized()
{
    const auto r = getLocalBounds().toFloat();
    cabinet = r.withTrimmedTop (14.0f).reduced (2.0f, 0.0f).withTrimmedBottom (4.0f);
    auto inner = cabinet.reduced (22.0f, 20.0f).toNearestInt();
    const int panelH = 236;
    panel.setBounds (inner.removeFromBottom (panelH));
    inner.removeFromBottom (12);
    glass.setBounds (inner);
}

void AmpHead::paint (juce::Graphics& g)
{
    // Shadow, then the tolex-covered cabinet.
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (cabinet.translated (0.0f, 4.0f), 12.0f);
    g.setTiledImageFill (tolex, 0, 0, 1.0f);
    g.fillRoundedRectangle (cabinet, 12.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), 0, cabinet.getY(),
                                             juce::Colours::black.withAlpha (0.25f), 0, cabinet.getBottom(), false));
    g.fillRoundedRectangle (cabinet, 12.0f);

    // Piping around the front baffle.
    const auto piping = cabinet.reduced (11.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (piping.translated (0.0f, 1.0f), 7.0f, 2.5f);
    g.setColour (juce::Colour (0xffcfc6b2));
    g.drawRoundedRectangle (piping, 7.0f, 2.0f);

    // Corner protectors.
    const float cs = 26.0f;
    for (int i = 0; i < 4; ++i)
    {
        const bool right = (i & 1) != 0, bottom = (i & 2) != 0;
        const auto corner = juce::Rectangle<float> (cs, cs).withPosition (right ? cabinet.getRight() - cs : cabinet.getX(),
                                                                          bottom ? cabinet.getBottom() - cs : cabinet.getY());
        juce::Path p;
        p.addRoundedRectangle (corner.getX(), corner.getY(), cs, cs, 10.0f, 10.0f, ! right && ! bottom, right && ! bottom, ! right && bottom, right && bottom);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe4e6ea), corner.getX(), corner.getY(), juce::Colour (0xff5a5d63), corner.getRight(), corner.getBottom(), false));
        g.fillPath (p);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
        const auto rivet = corner.getCentre().translated (right ? 3.0f : -3.0f, bottom ? 3.0f : -3.0f);
        g.setColour (juce::Colour (0xff2a2b2e));
        g.fillEllipse (rivet.x - 2.5f, rivet.y - 2.5f, 5.0f, 5.0f);
    }

    // Leather handle with chrome end caps.
    const auto handle = juce::Rectangle<float> (std::min (300.0f, getWidth() * 0.24f), 14.0f).withCentre ({ cabinet.getCentreX(), 8.0f });
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e2a27), 0, handle.getY(), juce::Colour (0xff0b0a09), 0, handle.getBottom(), false));
    g.fillRoundedRectangle (handle, 6.0f);
    juce::Path stitch;
    stitch.addRoundedRectangle (handle.reduced (5.0f, 3.5f), 3.0f);
    juce::Path dashed;
    const float dashes[] = { 3.0f, 3.0f };
    juce::PathStrokeType (0.8f).createDashedStroke (dashed, stitch, dashes, 2);
    g.setColour (juce::Colour (0xff8a7d6a).withAlpha (0.7f));
    g.fillPath (dashed);
    for (float x : { handle.getX() - 10.0f, handle.getRight() - 14.0f })
    {
        const juce::Rectangle<float> cap (x, 4.0f, 24.0f, 14.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe0e2e6), cap.getX(), cap.getY(), juce::Colour (0xff55585e), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 3.0f);
    }

    // Recess shadow behind the glass and faceplate.
    for (auto* c : std::initializer_list<juce::Component*> { &glass, &panel })
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (c->getBounds().toFloat().expanded (2.0f).translated (0.0f, 2.0f), 10.0f);
    }
}
} // namespace af::ui

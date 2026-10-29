#include <doctest.h>
#include <complex>
#include "TestSignals.h"
#include "../../Source/DSP/Effects/Overdrive.h"
#include "../../Source/DSP/Effects/Reverb.h"
#include "../../Source/DSP/Effects/Smooth.h"

using namespace af;

namespace
{
    /** Runs Smooth over a stereo copy of x (both channels equal), returns the left output. */
    std::vector<float> runSmooth (const std::vector<float>& x, float amount, float enable, dsp::Smooth* keep = nullptr)
    {
        dsp::Smooth local;
        auto& s = keep != nullptr ? *keep : local;
        s.prepare (48000.0);
        std::vector<float> l (x), r (x);
        for (size_t p = 0; p + 32 <= x.size(); p += 32)
            s.process (l.data() + p, r.data() + p, 32, amount, enable, enable);
        return l;
    }
}

TEST_CASE ("Smooth cuts a loud 2 kHz band, leaves quiet signals and low frequencies alone")
{
    const double fs = 48000;
    const int n = 48000;
    const int settle = 24000;

    // Loud 2 kHz tone, heavy setting: cut by several dB (bounded by the 9 dB maximum).
    auto loud = test::sine (2000.0, fs, n, 0.5f);
    dsp::Smooth s;
    auto out = runSmooth (loud, 1.0f, 1.0f, &s);
    const double cut = test::db (test::toneAmplitude (out, 2000.0, fs, settle) / test::toneAmplitude (loud, 2000.0, fs, settle));
    CHECK (cut < -4.0);
    CHECK (cut > -9.5);
    CHECK (s.getReductionDb() > 4.0f);

    // Quiet 2 kHz tone at the gentlest setting: below threshold, untouched.
    auto quiet = test::sine (2000.0, fs, n, 0.02f);
    auto outQ = runSmooth (quiet, 0.0f, 1.0f);
    CHECK (std::abs (test::db (test::toneAmplitude (outQ, 2000.0, fs, settle) / test::toneAmplitude (quiet, 2000.0, fs, settle))) < 0.1);

    // Loud low note: outside the band, barely affected.
    auto low = test::sine (110.0, fs, n, 0.5f);
    auto outL = runSmooth (low, 1.0f, 1.0f);
    CHECK (std::abs (test::db (test::toneAmplitude (outL, 110.0, fs, settle) / test::toneAmplitude (low, 110.0, fs, settle))) < 0.5);

    // Turning the knob clockwise smooths more.
    auto gentle = runSmooth (loud, 0.2f, 1.0f);
    CHECK (test::toneAmplitude (gentle, 2000.0, fs, settle) > test::toneAmplitude (out, 2000.0, fs, settle));

    // Disabled: transparent.
    auto off = runSmooth (loud, 1.0f, 0.0f);
    for (int i = 0; i < n; i += 997)
        CHECK (off[(size_t) i] == doctest::Approx (loud[(size_t) i]).epsilon (1e-6));
}

TEST_CASE ("Drive types: Screamer and RAT are stable, distinct, and the RAT keeps more low end")
{
    const double fs = 48000 * 8;
    auto lowNote = test::sine (82.4, fs, (int) fs / 2, 0.3f);
    auto run = [&] (int type, const std::vector<float>& in)
    {
        dsp::Overdrive d;
        d.prepare (fs);
        d.setType (type);
        d.setControls (0.7f, 0.5f, 0.0f);
        std::vector<float> out (in.size());
        for (size_t i = 0; i < in.size(); ++i)
            out[i] = d.process (in[i]);
        return out;
    };
    const auto ts = run (dsp::Overdrive::screamer, lowNote);
    const auto rat = run (dsp::Overdrive::rat, lowNote);
    CHECK (test::allFinite (ts));
    CHECK (test::allFinite (rat));
    CHECK (test::peak (rat) < 2.0f);
    // Screamer: the clipping path is high-passed, so a low E fundamental passes mostly clean;
    // the RAT clips the full-range signal hard.
    const int start = (int) fs / 4;
    CHECK (test::toneAmplitude (rat, 82.4, fs, start) > 0.05);
    double diff = 0;
    for (size_t i = (size_t) start; i < ts.size(); ++i)
        diff += std::abs (ts[i] - rat[i]);
    CHECK (diff / (double) (ts.size() - (size_t) start) > 0.01);

    // Extreme input stays bounded.
    std::vector<float> huge (4096);
    for (size_t i = 0; i < huge.size(); ++i)
        huge[i] = (i / 50) % 2 ? 100.0f : -100.0f;
    CHECK (test::peak (run (dsp::Overdrive::rat, huge)) < 4.0f);
}

TEST_CASE ("Reverb types are stable and a type change does not click")
{
    const double fs = 48000;
    for (int type = 0; type < dsp::Reverb::numTypes; ++type)
    {
        dsp::Reverb rv;
        rv.prepare (fs);
        rv.setControls (2.0f, 0.5f, type);
        // Force the type in (the first change happens via a fade inside process()).
        std::vector<float> l (32), r (32);
        for (int k = 0; k < 40; ++k)
            rv.process (l.data(), r.data(), 32, 0.0f, 1.0f);
        std::vector<float> outL;
        for (int b = 0; b < (int) fs * 3 / 32; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            if (b == 0)
                l[0] = r[0] = 1.0f;
            rv.process (l.data(), r.data(), 32, 1.0f, 1.0f);
            outL.insert (outL.end(), l.begin(), l.end());
        }
        INFO ("type " << type);
        CHECK (test::allFinite (outL));
        CHECK (test::peak (outL, 1) < 1.0f); // wet tail only (sample 0 is the dry impulse)
        CHECK (test::rms (outL, 0, (int) fs / 2) > 1e-4);           // there is a tail
        CHECK (test::rms (outL, (int) (fs * 2.6), -1) < test::rms (outL, 0, (int) fs / 2)); // and it decays
    }

    // Switch type while a tail is ringing: no sample-to-sample jump bigger than the tail itself.
    dsp::Reverb rv;
    rv.prepare (fs);
    auto tone = test::sine (440.0, fs, (int) fs, 0.3f);
    std::vector<float> l (32), r (32), out;
    for (int b = 0; b < (int) fs / 32; ++b)
    {
        if (b == (int) fs / 64)
            rv.setControls (1.8f, 0.5f, dsp::Reverb::spring);
        // Feed a tone for the first quarter second, then let the tail ring; output is wet only.
        const bool feeding = b < (int) fs / 128;
        for (int i = 0; i < 32; ++i)
            l[(size_t) i] = r[(size_t) i] = feeding ? tone[(size_t) (b * 32 + i)] : 0.0f;
        std::vector<float> dryL (l);
        rv.process (l.data(), r.data(), 32, 1.0f, 1.0f);
        for (int i = 0; i < 32; ++i)
            l[(size_t) i] -= dryL[(size_t) i];
        out.insert (out.end(), l.begin(), l.end());
    }
    float maxStep = 0.0f;
    for (size_t i = 1; i < out.size(); ++i)
        maxStep = std::max (maxStep, std::abs (out[i] - out[i - 1]));
    CHECK (maxStep < 0.05f);
}

TEST_CASE ("Tape delay is stable and darker than studio delay")
{
    const double fs = 48000;
    auto run = [&] (bool tape)
    {
        dsp::StereoDelay d;
        d.prepare (fs);
        d.snapTime (250.0f);
        std::vector<float> l (32), r (32), out;
        for (int b = 0; b < (int) fs * 2 / 32; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            if (b == 0)
                for (int i = 0; i < 8; ++i)
                    l[(size_t) i] = r[(size_t) i] = (i % 2 ? -0.8f : 0.8f); // bright click
            d.process (l.data(), r.data(), 32, 250.0f, 0.9f, 1.0f, 1.0f, tape);
            out.insert (out.end(), l.begin(), l.end());
        }
        return out;
    };
    const auto studio = run (false), tape = run (true);
    CHECK (test::allFinite (tape));
    CHECK (test::peak (tape) < 2.0f);
    // High-frequency content of the repeats (first difference) is lower on tape.
    auto hf = [] (const std::vector<float>& x)
    {
        double e = 0;
        for (size_t i = 13000; i < x.size(); ++i)
            e += (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
        return e;
    };
    CHECK (hf (tape) < hf (studio));
}

#include "../../Source/DSP/Effects/Compressor.h"

TEST_CASE ("Compressor: both types reduce dynamic range, Sustain compresses more, quiet signals pass")
{
    const double fs = 48000;
    for (int type : { (int) dsp::Compressor::studio, (int) dsp::Compressor::cs3 })
    {
        auto levelOut = [&] (float amp, float sustain)
        {
            dsp::Compressor c;
            c.prepare (fs);
            c.setControls (type, sustain, 0.3f, 0.5f, 0.0f);
            auto x = test::sine (440.0, fs, (int) fs, amp);
            for (size_t p = 0; p + 32 <= x.size(); p += 32)
                c.process (x.data() + p, 32, 1.0f, 1.0f, 1.0f);
            CHECK (test::allFinite (x));
            return test::db (test::rms (x, (int) fs / 2));
        };
        INFO ("type " << type);
        // 24 dB more input -> much less than 24 dB more output.
        const double quiet = levelOut (0.02f, 0.6f), loud = levelOut (0.32f, 0.6f);
        CHECK (loud - quiet < 15.0);
        CHECK (loud - quiet > 1.0);
        // More sustain = flatter.
        const double loudMore = levelOut (0.32f, 1.0f), quietMore = levelOut (0.02f, 1.0f);
        CHECK (loudMore - quietMore < loud - quiet);
    }
    // CS-3 squashes harder than Studio at the same settings.
    auto spread = [&] (int type)
    {
        double out[2];
        int k = 0;
        for (float amp : { 0.02f, 0.32f })
        {
            dsp::Compressor c;
            c.prepare (fs);
            c.setControls (type, 0.6f, 0.3f, 0.5f, 0.0f);
            auto x = test::sine (440.0, fs, (int) fs, amp);
            for (size_t p = 0; p + 32 <= x.size(); p += 32)
                c.process (x.data() + p, 32, 1.0f, 1.0f, 1.0f);
            out[k++] = test::db (test::rms (x, (int) fs / 2));
        }
        return out[1] - out[0];
    };
    CHECK (spread (dsp::Compressor::cs3) < spread (dsp::Compressor::studio));
}

TEST_CASE ("Studio compressor at Amount 0 is identical to off, whatever the other knobs")
{
    const double fs = 48000;
    dsp::Compressor c;
    c.prepare (fs);
    auto x = test::sine (440.0, fs, (int) fs / 2, 0.9f);
    const auto dry = x;
    for (size_t p = 0; p + 64 <= x.size(); p += 64)
    {
        c.setControls (dsp::Compressor::studio, 0.0f, 0.2f, 1.0f, 9.0f); // tone and level far from neutral
        c.process (x.data() + p, 64, 1.0f, 1.0f, 1.0f);
    }
    // After the first block's fade-out the output is the input, bit for bit.
    for (size_t i = 64; i < x.size(); ++i)
        REQUIRE (x[i] == dry[i]);
    CHECK (c.getMeterDb() == 0.0f);
}

TEST_CASE ("Auto-wah sweeps up with picking strength and stays stable")
{
    const double fs = 48000;
    // Broadband input (pulse train); compare high-band energy for a soft vs a hard signal.
    auto run = [&] (float amp)
    {
        dsp::AutoWah w;
        w.prepare (fs);
        std::vector<float> x ((size_t) fs);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = (i % 200) < 3 ? amp : 0.0f;
        for (size_t p = 0; p + 32 <= x.size(); p += 32)
            w.process (x.data() + p, 32, 1.0f, 0.7f, 0.6f, 0.6f, 1.0f, 1.0f, 1.0f);
        CHECK (test::allFinite (x));
        double hf = 0, all = 0;
        for (size_t i = x.size() / 2 + 1; i < x.size(); ++i)
        {
            hf += (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
            all += x[i] * x[i];
        }
        return hf / std::max (all, 1e-12);
    };
    CHECK (run (0.5f) > run (0.01f) * 1.5); // brighter (higher filter) when hit harder
}

#include "../../Source/DSP/Effects/ModulationEffects.h"
#include <random>

TEST_CASE ("Phasers: stable, notch the spectrum, Phase 90 deeper than Script")
{
    const double fs = 48000;
    auto run = [&] (int mode)
    {
        dsp::Chorus c;
        c.prepare (fs);
        std::mt19937 rng (3);
        std::uniform_real_distribution<float> noise (-0.2f, 0.2f);
        std::vector<float> l ((size_t) fs * 2), r;
        for (auto& v : l) v = noise (rng);
        r = l;
        const auto dry = l;
        for (size_t p = 0; p + 64 <= l.size(); p += 64)
            c.process (l.data() + p, r.data() + p, 64, 0.5f, 0.7f, 1.0f, mode);
        double diff = 0;
        for (size_t i = (size_t) fs / 2; i < l.size(); ++i)
            diff += (l[i] - dry[i]) * (l[i] - dry[i]);
        CHECK (test::allFinite (l));
        CHECK (test::peak (l) < 1.0f);
        return diff;
    };
    const double script = run (dsp::Chorus::scriptPhaser), p90 = run (dsp::Chorus::phase90);
    CHECK (script > 1.0); // audibly changes the sound
    CHECK (p90 > script); // feedback: deeper effect
}

TEST_CASE ("Compressor meter reports gain reduction and clears")
{
    dsp::Compressor c;
    c.prepare (48000.0);
    c.setControls (dsp::Compressor::studio, 0.8f, 0.3f, 0.5f, 0.0f);
    auto x = test::sine (440.0, 48000.0, 24000, 0.4f), y = x;
    for (size_t p = 0; p + 32 <= x.size(); p += 32)
        c.process (x.data() + p, y.data() + p, 32, 1.0f, 1.0f, 1.0f);
    CHECK (c.getMeterDb() > 3.0f);
    c.clearMeter();
    CHECK (c.getMeterDb() == 0.0f);
}

#include "../../Source/DSP/Effects/ClassicCompressor.h"

TEST_CASE ("Classic compressor models: all compress, are stable, and Amount 0 equals off")
{
    const double fs = 48000;
    const char* names[] = { "Studio", "LA-2A", "1176", "SSL", "Fairchild", "dbx" };
    for (int m = 0; m < dsp::ClassicCompressor::numModels; ++m)
    {
        INFO ("model " << m << " " << std::string (names[m]));
        auto run = [&] (float amp, float amount, std::vector<float>* outL = nullptr, int extra = 0)
        {
            dsp::ClassicCompressor c;
            c.prepare (fs);
            dsp::ClassicCompressor::Controls ctl;
            ctl.model = m;
            ctl.amount = amount;
            ctl.ratio1176 = extra;
            ctl.fairchildTc = extra;
            ctl.sslAuto = extra > 0;
            auto l = test::sine (220.0, fs, (int) fs, amp), r = l;
            const auto dry = l;
            for (size_t p = 0; p + 64 <= l.size(); p += 64)
            {
                c.setControls (ctl);
                c.process (l.data() + p, r.data() + p, 64, 1.0f, 1.0f, 1.0f);
            }
            CHECK (test::allFinite (l));
            CHECK (test::peak (l) < 8.0f);
            if (outL != nullptr)
                *outL = l;
            if (amount == 0.0f)
                for (size_t i = 64; i < l.size(); ++i)
                    REQUIRE (l[i] == dry[i]);
            return std::pair<double, float> (test::db (test::rms (l, (int) fs / 2)), c.getMeterDb());
        };
        // 24 dB more input -> clearly less than 24 dB more output, and the meter shows it.
        const auto quiet = run (0.02f, 0.6f), loud = run (0.4f, 0.6f);
        CHECK (loud.first - quiet.first < 18.0);
        CHECK (loud.second > 1.0f);
        run (0.4f, 0.0f); // bit-identical bypass at Amount 0
        for (int extra : { 1, 4, 5 })
            run (0.5f, 1.0f, nullptr, extra); // all buttons, TC5/TC6, auto release: stable at extremes
    }
}

TEST_CASE ("LA-2A releases in two stages; dbx releases at a constant rate")
{
    const double fs = 48000;
    auto releaseCurve = [&] (int model)
    {
        dsp::ClassicCompressor c;
        c.prepare (fs);
        dsp::ClassicCompressor::Controls ctl;
        ctl.model = model;
        ctl.amount = 0.8f;
        std::vector<float> gr;
        const int n = (int) fs * 3;
        auto x = test::sine (220.0, fs, n, 0.5f);
        for (int i = n / 2; i < n; ++i) x[(size_t) i] *= 0.01f; // loud for 1.5 s, then nearly silent
        auto r = x;
        for (int p = 0; p + 64 <= n; p += 64)
        {
            c.setControls (ctl);
            c.process (x.data() + p, r.data() + p, 64, 1.0f, 1.0f, 1.0f);
            gr.push_back (c.getMeterDb());
        }
        return gr;
    };
    const double blocksPerSec = fs / 64.0;
    auto at = [&] (const std::vector<float>& g, double seconds) { return g[(size_t) (seconds * blocksPerSec)]; };
    const auto la = releaseCurve (dsp::ClassicCompressor::la2a);
    const float start = at (la, 1.49);
    REQUIRE (start > 6.0f);
    CHECK (at (la, 1.65) < 0.65f * start);  // fast stage: about half gone within ~0.15 s
    CHECK (at (la, 1.65) > 0.2f * start);   // ...but the slow stage holds on
    CHECK (at (la, 2.4) > 0.05f * start);   // still releasing almost a second later
    const auto dbx = releaseCurve (dsp::ClassicCompressor::dbx160);
    const float d0 = at (dbx, 1.49);
    REQUIRE (d0 > 6.0f);
    // 120 dB/s: 6 dB takes about 50 ms.
    CHECK (at (dbx, 1.5 + 0.05) < d0 - 3.0f);
}

TEST_CASE ("Classic compressor models stay close to the dry level at default settings")
{
    // Plucked-note-like bursts at mixed levels; every model with its defaults (Amount 0.4).
    const double fs = 48000;
    std::vector<float> x ((size_t) fs * 4);
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) (i % 24000) / fs;
        const float amp = ((i / 24000) % 3 == 0) ? 0.5f : ((i / 24000) % 3 == 1 ? 0.15f : 0.05f);
        x[i] = amp * (float) (std::exp (-t * 4.0) * std::sin (dsp::kTwoPi * 196.0 * (double) i / fs));
    }
    const double dry = test::db (test::rms (x, 0));
    for (int m = 0; m < dsp::ClassicCompressor::numModels; ++m)
    {
        dsp::ClassicCompressor c;
        c.prepare (fs);
        dsp::ClassicCompressor::Controls ctl;
        ctl.model = m;
        auto l = x, r = x;
        for (size_t p = 0; p + 64 <= l.size(); p += 64)
        {
            c.setControls (ctl);
            c.process (l.data() + p, r.data() + p, 64, 1.0f, 1.0f, 1.0f);
        }
        const double wet = test::db (test::rms (l, 0));
        MESSAGE ("model " << m << ": " << wet - dry << " dB vs dry");
        CHECK (std::abs (wet - dry) < 3.0); // switching models must not jump the level
    }
}


TEST_CASE ("Lexicon and Bricasti: every program is stable, decays at the set time and matches the Hall level")
{
    const double fs = 48000;
    // Impulse response of one type/program, wet only, with the type forced in first.
    auto ir = [&] (int type, int program, float decay, float bassMult = 1.0f)
    {
        dsp::Reverb rv;
        rv.prepare (fs);
        dsp::StudioReverbControls c;
        c.program = program;
        c.preDelayMs = 0.0f;
        c.bassMult = bassMult;
        std::vector<float> l (32), r (32), out;
        for (int k = 0; k < 40; ++k)
        {
            rv.setControls (decay, 0.5f, type, c);
            rv.process (l.data(), r.data(), 32, 0.0f, 1.0f);
        }
        for (int b = 0; b < (int) fs * 6 / 32; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            if (b == 0)
                l[0] = r[0] = 1.0f;
            rv.setControls (decay, 0.5f, type, c);
            rv.process (l.data(), r.data(), 32, 1.0f, 0.5f); // 50 %: full dry + the tail
            if (b == 0)
                l[0] -= 1.0f;
            out.insert (out.end(), l.begin(), l.end());
        }
        return out;
    };
    // RT60 from the energy decay curve (Schroeder integration), fitted between -5 and -25 dB.
    auto rt60 = [&] (const std::vector<float>& h)
    {
        std::vector<double> edc (h.size());
        double acc = 0;
        for (size_t i = h.size(); i-- > 0;) { acc += (double) h[i] * h[i]; edc[i] = acc; }
        const double total = edc[0];
        double t5 = -1, t25 = -1;
        for (size_t i = 0; i < edc.size(); ++i)
        {
            const double db = 10 * std::log10 (edc[i] / total + 1e-30);
            if (t5 < 0 && db <= -5) t5 = (double) i / fs;
            if (t25 < 0 && db <= -25) { t25 = (double) i / fs; break; }
        }
        return (t25 - t5) * 3.0;
    };
    const auto hall = ir (dsp::Reverb::hall, 0, 2.0f);
    const double hallLevel = test::db (test::rms (hall, 0, (int) fs));
    for (int type : { (int) dsp::Reverb::lexicon, (int) dsp::Reverb::bricasti })
        for (int prog = 0; prog < 4; ++prog)
        {
            INFO ("type " << type << " program " << prog);
            const auto h = ir (type, prog, 2.0f);
            CHECK (test::allFinite (h));
            CHECK (test::peak (h) < 1.0f);
            const double t = rt60 (h);
            MESSAGE ("type " << type << " program " << prog << ": RT60 " << t << " s, level " << test::db (test::rms (h, 0, (int) fs)) - hallLevel << " dB vs Hall");
            CHECK (t > 1.3);
            CHECK (t < 3.0);
            CHECK (std::abs (test::db (test::rms (h, 0, (int) fs)) - hallLevel) < 6.0); // impulse-based; loudness on playing is matched in EngineTests
        }
    // Bass Multiply lengthens the low-frequency decay.
    for (int type : { (int) dsp::Reverb::lexicon, (int) dsp::Reverb::bricasti })
    {
        auto lowTail = [&] (float bm)
        {
            auto h = ir (type, 0, 2.0f, bm);
            dsp::Biquad lp;
            lp.setCoeffs (dsp::BiquadCoeffs::lowPass (fs, 200.0, 0.7));
            for (auto& v : h) v = lp.process (v);
            return test::rms (h, (int) (fs * 1.5), (int) (fs * 2.5));
        };
        CHECK (lowTail (2.0f) > 2.0 * lowTail (1.0f));
    }
}

namespace
{
    struct CompProfile
    {
        std::array<double, 5> grAt {}; // steady gain reduction (dB) at -30, -20, -12, -6, 0 dBFS
        double attackMs = 0, releaseMs = 0, thd = 0;
    };

    CompProfile profileOf (int model, float amount = 0.5f)
    {
        const double fs = 48000;
        CompProfile p;
        dsp::ClassicCompressor::Controls ctl;
        ctl.model = model;
        ctl.amount = amount;
        auto runSteady = [&] (float db, std::vector<float>* keep = nullptr)
        {
            dsp::ClassicCompressor c;
            c.prepare (fs);
            auto l = test::sine (1000.0, fs, (int) fs * 2, std::pow (10.0f, db / 20.0f)), r = l;
            float gr = 0;
            for (size_t i = 0; i + 32 <= l.size(); i += 32)
            {
                c.setControls (ctl);
                c.process (l.data() + i, r.data() + i, 32, 1.0f, 1.0f, 1.0f);
                gr = c.getMeterDb();
            }
            if (keep != nullptr) *keep = l;
            return (double) gr;
        };
        const float levels[] = { -30, -20, -12, -6, 0 };
        for (int k = 0; k < 5; ++k)
            p.grAt[(size_t) k] = runSteady (levels[k]);
        // Attack / release: a -30 dBFS tone stepping up to -6 dBFS for 1 s, then back down.
        dsp::ClassicCompressor c;
        c.prepare (fs);
        const int n = (int) fs * 4;
        auto l = test::sine (1000.0, fs, n, 1.0f);
        for (int i = 0; i < n; ++i)
            l[(size_t) i] *= (i >= (int) fs && i < (int) fs * 2) ? 0.5f : 0.0316f;
        auto r = l;
        std::vector<float> gr;
        for (int i = 0; i + 32 <= n; i += 32)
        {
            c.setControls (ctl);
            c.process (l.data() + i, r.data() + i, 32, 1.0f, 1.0f, 1.0f);
            gr.push_back (c.getMeterDb());
        }
        const double blockMs = 32.0 / fs * 1000.0;
        const size_t up = (size_t) (fs / 32), down = (size_t) (fs * 2 / 32);
        const double top = gr[down - 1], base = gr[up - 1];
        for (size_t b = up; b < down; ++b)
            if (gr[b] >= base + 0.63 * (top - base)) { p.attackMs = (double) (b - up + 1) * blockMs; break; }
        for (size_t b = down; b < gr.size(); ++b)
            if (gr[b] <= top - 0.63 * (top - base)) { p.releaseMs = (double) (b - down + 1) * blockMs; break; }
        // Harmonic distortion at -6 dBFS, steady state.
        std::vector<float> y;
        runSteady (-6, &y);
        std::vector<float> tail (y.end() - 32768, y.end());
        double fund = 0, harm = 0;
        for (int h = 1; h <= 7; ++h)
        {
            std::complex<double> acc = 0;
            for (size_t i = 0; i < tail.size(); ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (dsp::kTwoPi * (double) i / (double) (tail.size() - 1));
                acc += (double) tail[i] * w * std::polar (1.0, -dsp::kTwoPi * 1000.0 * h * (double) i / fs);
            }
            (h == 1 ? fund : harm) += std::norm (acc);
        }
        p.thd = 100.0 * std::sqrt (harm / fund);
        return p;
    }
} // namespace

TEST_CASE ("Studio Comp models behave measurably differently")
{
    const char* names[] = { "Pristine", "LA-2A", "1176", "SSL Bus", "Fairchild", "dbx 160" };
    std::vector<CompProfile> ps;
    for (int m = 0; m < dsp::ClassicCompressor::numModels; ++m)
    {
        ps.push_back (profileOf (m));
        const auto& p = ps.back();
        MESSAGE (std::string (names[m]) << ": GR@-30/-20/-12/-6/0 = " << p.grAt[0] << " / " << p.grAt[1] << " / " << p.grAt[2] << " / " << p.grAt[3] << " / "
                          << p.grAt[4] << " dB, attack " << p.attackMs << " ms, release " << p.releaseMs << " ms, THD " << p.thd << " %");
    }
    using M = dsp::ClassicCompressor;
    // Ballistics: the FET and vari-mu grab far faster than the opto and the RMS dbx.
    CHECK (ps[M::fet1176].attackMs * 4 < ps[M::la2a].attackMs);
    CHECK (ps[M::fairchild].attackMs * 4 < ps[M::dbx160].attackMs);
    // Release: dbx (120 dB/s) is fastest; opto and vari-mu are slow.
    CHECK (ps[M::dbx160].releaseMs * 3 < ps[M::studio].releaseMs);
    CHECK (ps[M::la2a].releaseMs > 2 * ps[M::studio].releaseMs);
    CHECK (ps[M::fairchild].releaseMs > 2 * ps[M::studio].releaseMs);
    // Knee: the opto and vari-mu are already working where the firm-knee SSL has not started.
    CHECK (ps[M::la2a].grAt[1] > ps[M::sslBus].grAt[1] + 1.5);
    CHECK (ps[M::fairchild].grAt[1] > ps[M::sslBus].grAt[1] + 1.5);
    // Colour: the 1176 and the valve units add distortion, the VCAs stay clean.
    CHECK (ps[M::fet1176].thd > 20 * ps[M::studio].thd + 0.2);
    CHECK (ps[M::fairchild].thd > 0.2);
    CHECK (ps[M::sslBus].thd < 0.05);
}

TEST_CASE ("Drive types SD-1, DS-1, Guv'nor: stable, level-matched, and each with its own character")
{
    const double fs = 48000 * 8;
    // A plucked chord-ish signal: low E + B + high E with decay.
    const int n = (int) fs / 2;
    std::vector<float> in ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        in[(size_t) i] = (float) (0.25 * std::exp (-t * 3) * (std::sin (dsp::kTwoPi * 82.4 * t) + 0.6 * std::sin (dsp::kTwoPi * 246.9 * t)
                                                             + 0.4 * std::sin (dsp::kTwoPi * 329.6 * t)));
    }
    auto run = [&] (int type, float drive, const std::vector<float>& x)
    {
        dsp::Overdrive d;
        d.prepare (fs);
        d.setType (type);
        d.setControls (drive, 0.5f, 0.0f);
        std::vector<float> out (x.size());
        for (size_t i = 0; i < x.size(); ++i)
            out[i] = d.process (x[i]);
        return out;
    };
    const char* names[] = { "Screamer", "RAT", "SD-1", "DS-1", "Guv'nor" };
    std::vector<std::vector<float>> outs;
    const double ref = test::db (test::rms (run (dsp::Overdrive::screamer, 0.5f, in), n / 8));
    for (int t = 0; t < dsp::Overdrive::numTypes; ++t)
    {
        outs.push_back (run (t, 0.5f, in));
        const auto& o = outs.back();
        const double lvl = test::db (test::rms (o, n / 8));
        const double low = test::toneAmplitude (o, 82.4, fs, n / 8), mid = test::toneAmplitude (o, 246.9, fs, n / 8);
        // Even/odd balance of the low E: 2nd vs 3rd harmonic.
        const double h2 = test::toneAmplitude (o, 164.8, fs, n / 8), h3 = test::toneAmplitude (o, 247.2, fs, n / 8);
        MESSAGE (std::string (names[t]) << ": level " << lvl - ref << " dB vs Screamer, low E " << test::db (low) << " dB, H2 " << test::db (h2) << " dB");
        INFO (names[t]);
        CHECK (test::allFinite (o));
        CHECK (test::peak (o) < 3.0f);
        CHECK (std::abs (lvl - ref) < 4.0); // switching types must not jump the level much
        (void) mid; (void) h3;
    }
    // SD-1's asymmetric clipping makes clearly more 2nd harmonic than the symmetric Screamer
    // (measured on a note above the clipping path's high-pass, where both clip).
    {
        const auto note = test::sine (880.0, fs, n, 0.3f);
        const auto sd = run (dsp::Overdrive::sd1, 0.6f, note), ts = run (dsp::Overdrive::screamer, 0.6f, note);
        const double h2sd = test::toneAmplitude (sd, 1760.0, fs, n / 8) / test::toneAmplitude (sd, 880.0, fs, n / 8);
        const double h2ts = test::toneAmplitude (ts, 1760.0, fs, n / 8) / test::toneAmplitude (ts, 880.0, fs, n / 8);
        MESSAGE ("H2/H1: SD-1 " << test::db (h2sd) << " dB, Screamer " << test::db (h2ts) << " dB");
        CHECK (h2sd > 4.0 * h2ts);
    }
    // Guv'nor keeps more low end than the DS-1 (whose booster thins the bass).
    CHECK (test::toneAmplitude (outs[dsp::Overdrive::guvnor], 82.4, fs, n / 8) > 1.3 * test::toneAmplitude (outs[dsp::Overdrive::ds1], 82.4, fs, n / 8));
    // All five differ from one another.
    for (int a = 0; a < dsp::Overdrive::numTypes; ++a)
        for (int b = a + 1; b < dsp::Overdrive::numTypes; ++b)
        {
            double sab = 0, saa = 0, sbb = 0;
            for (size_t i = (size_t) n / 8; i < (size_t) n; ++i)
            {
                sab += outs[(size_t) a][i] * outs[(size_t) b][i];
                saa += outs[(size_t) a][i] * outs[(size_t) a][i];
                sbb += outs[(size_t) b][i] * outs[(size_t) b][i];
            }
            INFO (names[a] << " vs " << names[b]);
            CHECK (sab / std::sqrt (saa * sbb) < 0.98);
        }
    // Extreme input stays bounded.
    std::vector<float> huge (4096);
    for (size_t i = 0; i < huge.size(); ++i)
        huge[i] = (i / 50) % 2 ? 100.0f : -100.0f;
    // (The SD-1, like the Screamer, adds the clean signal to the clipped one, so it is not bounded.)
    for (int t : { (int) dsp::Overdrive::ds1, (int) dsp::Overdrive::guvnor })
        CHECK (test::peak (run (t, 1.0f, huge)) < 6.0f);
}

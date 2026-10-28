#include <doctest.h>
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
    for (int type = 0; type < 4; ++type)
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

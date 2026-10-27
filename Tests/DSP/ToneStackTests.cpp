#include <doctest.h>
#include "TestSignals.h"
#include "../../Source/DSP/ToneStacks/ToneStack.h"

using namespace af;

TEST_CASE ("Tone stack digital response matches analogue model")
{
    for (double fs : { 44100.0, 96000.0, 192000.0 })
    {
        dsp::ToneStack ts;
        ts.prepare (fs);
        for (auto ctl : { std::array<double,3>{ 0.5, 0.5, 0.5 }, { 1, 0, 1 }, { 0, 1, 0 }, { 0.2, 0.8, 0.6 } })
        {
            ts.setControls (ctl[0], ctl[1], ctl[2]);
            for (double f : { 50.0, 200.0, 1000.0, 3000.0 })
            {
                const double ad = test::db (std::abs (ts.analogResponse (f)));
                const double dd = test::db (std::abs (ts.digitalResponse (f)));
                // Bilinear warping only matters close to Nyquist.
                CHECK (std::abs (ad - dd) < (fs > 50000 ? 0.05 : 0.5));
            }
        }
    }
}

TEST_CASE ("Tone stack controls behave like a passive TMB network")
{
    dsp::ToneStack ts;
    ts.prepare (96000);
    auto mag = [&] (double f) { return test::db (std::abs (ts.digitalResponse (f))); };

    ts.setControls (0.5, 0.5, 0.5);
    const double lo = mag (60), mid = mag (500), hi = mag (4000);
    MESSAGE ("Noon response: 60Hz " << lo << " 500Hz " << mid << " 4k " << hi);
    CHECK (mid < lo);          // classic mid scoop
    CHECK (mid < hi);
    CHECK (hi < 0.0);          // passive: insertion loss everywhere
    CHECK (lo < 0.0);

    ts.setControls (1.0, 0.5, 0.5);
    const double loBass = mag (60);
    ts.setControls (0.0, 0.5, 0.5);
    CHECK (loBass - mag (60) > 10.0); // bass knob has a large effect at 60 Hz

    ts.setControls (0.5, 0.5, 1.0);
    const double hiTreble = mag (4000);
    ts.setControls (0.5, 0.5, 0.0);
    CHECK (hiTreble - mag (4000) > 10.0);

    ts.setControls (0.5, 1.0, 0.5);
    const double midUp = mag (500);
    ts.setControls (0.5, 0.0, 0.5);
    CHECK (midUp - mag (500) > 6.0);
}

TEST_CASE ("Tone stack is stable and finite under extreme settings and input")
{
    dsp::ToneStack ts;
    ts.prepare (192000);
    for (auto ctl : { std::array<double,3>{ 0, 0, 0 }, { 1, 1, 1 }, { 0, 1, 1 }, { 1, 0, 0 } })
    {
        ts.setControls (ctl[0], ctl[1], ctl[2]);
        ts.reset();
        float maxAbs = 0;
        for (int i = 0; i < 192000; ++i)
        {
            const float x = (i < 100) ? 1000.0f : 0.0f;
            const float y = ts.process (x);
            REQUIRE (std::isfinite (y));
            if (i > 150000) maxAbs = std::max (maxAbs, std::abs (y));
        }
        CHECK (maxAbs < 1e-3f); // impulse response decays
    }
}

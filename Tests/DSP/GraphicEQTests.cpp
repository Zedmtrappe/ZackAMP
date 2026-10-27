#include <doctest.h>
#include "TestSignals.h"
#include "../../Source/DSP/GraphicEQ/GraphicEQ.h"

using namespace af;
using dsp::GraphicEQDesigner;

TEST_CASE ("Graphic EQ: single band hits its slider value at the centre")
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        GraphicEQDesigner d (fs);
        for (int band = 0; band < 5; ++band)
            for (float g : { -12.0f, -6.0f, 6.0f, 12.0f })
            {
                std::array<float, 5> s {};
                s[(size_t) band] = g;
                std::array<dsp::BiquadCoeffs, 5> c;
                d.design (s, c);
                for (int j = 0; j < 5; ++j)
                {
                    const double r = GraphicEQDesigner::responseDb (c, GraphicEQDesigner::kCentres[(size_t) j], fs);
                    CHECK (std::abs (r - s[(size_t) j]) < 0.35);
                }
                for (auto& k : c) CHECK (k.isStable());
            }
    }
}

TEST_CASE ("Graphic EQ: combined curves (V-shape, all up) are accurate")
{
    GraphicEQDesigner d (48000);
    for (auto s : { std::array<float,5>{ 6, -3, -8, 2, 7 }, { 12, 12, 12, 12, 12 }, { -12, 12, -12, 12, -12 }, { 0, 0, 0, 0, 0 } })
    {
        std::array<dsp::BiquadCoeffs, 5> c;
        d.design (s, c);
        for (int j = 0; j < 5; ++j)
        {
            const double r = GraphicEQDesigner::responseDb (c, GraphicEQDesigner::kCentres[(size_t) j], 48000);
            CHECK (std::abs (r - s[(size_t) j]) < 1.0);
        }
    }
    // Flat means flat everywhere.
    std::array<dsp::BiquadCoeffs, 5> c;
    d.design ({ 0, 0, 0, 0, 0 }, c);
    for (double f = 20; f < 20000; f *= 1.2)
        CHECK (std::abs (GraphicEQDesigner::responseDb (c, f, 48000)) < 1e-6);
}

TEST_CASE ("Graphic EQ processing matches the designed response")
{
    const double fs = 48000;
    dsp::GraphicEQ eq;
    eq.prepare (fs);
    std::array<float, 5> s { 6, -3, -8, 2, 7 };
    eq.setBands (s);
    GraphicEQDesigner d (fs);
    std::array<dsp::BiquadCoeffs, 5> c;
    d.design (s, c);
    for (double f : { 80.0, 750.0, 6600.0 })
    {
        eq.reset();
        auto x = test::sine (f, fs, 16384, 0.25f);
        for (auto& v : x) v = eq.process (v);
        const double measured = test::db (test::toneAmplitude (x, f, fs, 8192) / 0.25);
        CHECK (std::abs (measured - GraphicEQDesigner::responseDb (c, f, fs)) < 0.2);
    }
}

// AMP FORGE — real-time performance benchmark.
//
// Measures the engine's processing time per host block across sample rates, block sizes,
// amp models, IR lengths and effect loads. Reports mean, 99th percentile and worst case as
// a fraction of the block's real-time budget. Worst case matters more than the average for
// dropout-free live playing. Results depend on the machine — run it on the target Mac.
//
// Usage: ampforge_bench [seconds-per-case=2]
#include "Audio/AmpForgeEngine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace af;
using Clock = std::chrono::steady_clock;

namespace
{
std::shared_ptr<dsp::IRData> syntheticIR (double fs, double seconds, int channels, unsigned seed)
{
    auto ir = std::make_shared<dsp::IRData>();
    ir->id = "bench";
    ir->sampleRate = fs;
    std::mt19937 rng (seed);
    std::normal_distribution<float> d (0.0f, 1.0f);
    const int n = (int) (fs * seconds);
    for (int c = 0; c < channels; ++c)
    {
        std::vector<float> h ((size_t) n);
        for (int i = 0; i < n; ++i)
            h[(size_t) i] = d (rng) * std::exp (-(float) i / (0.01f * (float) fs));
        ir->channels.push_back (std::move (h));
    }
    return ir;
}

struct Result { double mean, p99, worst; };

Result run (AmpForgeEngine& e, double fs, int block, double seconds)
{
    const int blocks = (int) (seconds * fs / block);
    std::vector<float> in ((size_t) block), l ((size_t) block), r ((size_t) block);
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> u (-0.3f, 0.3f);
    std::vector<double> times;
    times.reserve ((size_t) blocks);
    double phase = 0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            phase += 110.0 / fs;
            in[(size_t) i] = 0.3f * (float) std::sin (dsp::kTwoPi * phase) + 0.02f * u (rng);
        }
        const auto t0 = Clock::now();
        e.process (in.data(), l.data(), r.data(), block);
        const auto t1 = Clock::now();
        times.push_back (std::chrono::duration<double> (t1 - t0).count());
    }
    std::sort (times.begin(), times.end());
    const double budget = block / fs;
    double sum = 0;
    for (double t : times) sum += t;
    return { sum / (double) times.size() / budget * 100.0,
             times[(size_t) ((double) times.size() * 0.99)] / budget * 100.0,
             times.back() / budget * 100.0 };
}
} // namespace

int main (int argc, char** argv)
{
    const double seconds = argc > 1 ? std::atof (argv[1]) : 2.0;
    std::printf ("AMP FORGE benchmark — %% of real-time block budget (lower is better)\n");
    std::printf ("%-48s %6s %6s | %7s %7s %7s\n", "case", "rate", "block", "mean%", "p99%", "worst%");

    struct Case { const char* name; int model; double irSeconds; int irChannels; bool dual; bool effects; int os; };
    const Case cases[] = {
        { "Clean, 85ms IR, no FX", 0, 0.085, 1, false, false, 0 },
        { "Plexi, 85ms IR, no FX", 2, 0.085, 1, false, false, 0 },
        { "Heavy, 85ms IR, no FX", 5, 0.085, 1, false, false, 0 },
        { "Heavy, 85ms IR, all FX + gate + drive + EQ", 5, 0.085, 1, false, true, 0 },
        { "Heavy, dual stereo 500ms IRs, all FX", 5, 0.5, 2, true, true, 0 },
        { "Heavy, 1s IR (max), all FX", 5, 1.0, 1, false, true, 0 },
        { "Heavy, 85ms IR, all FX, 8x oversampling", 5, 0.085, 1, false, true, 3 },
        { "Heavy, 85ms IR, all FX, 2x oversampling", 5, 0.085, 1, false, true, 1 },
    };

    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (const auto& c : cases)
            for (int block : { 32, 64, 128, 256 })
            {
                if (fs > 50000 && c.os != 0)
                    continue;
                AmpForgeEngine e;
                auto& p = e.params();
                p.set (pAmpModel, (float) c.model);
                p.set (pAmpGain, 0.7f);
                if (c.effects)
                {
                    for (int id : { (int) pGateOn, (int) pDriveOn, (int) pEqOn, (int) pChorusOn, (int) pDelayOn, (int) pReverbOn })
                        p.set (id, 1.0f);
                    p.set (pEq750, 3.0f);
                }
                p.set (pCabDual, c.dual ? 1.0f : 0.0f);
                e.setOversamplingMode ((OversamplingMode) c.os);
                e.prepare (fs, block);
                e.setCabinetIR (0, syntheticIR (fs, c.irSeconds, c.irChannels, 1));
                if (c.dual)
                    e.setCabinetIR (1, syntheticIR (fs, c.irSeconds, c.irChannels, 2));
                run (e, fs, block, 0.2); // warm-up (crossfades, caches)
                const auto res = run (e, fs, block, seconds);
                std::printf ("%-48s %6.0f %6d | %7.2f %7.2f %7.2f\n", c.name, fs, block, res.mean, res.p99, res.worst);
                e.collectGarbage();
            }
    return 0;
}

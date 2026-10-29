#include <doctest.h>
#include "../DSP/TestSignals.h"
#include "../../Source/Audio/AmpForgeEngine.h"

#include <atomic>
#include <random>
#include <thread>

using namespace af;

namespace
{
std::shared_ptr<dsp::IRData> testIR (double fs, int len, unsigned seed, int channels = 1)
{
    auto ir = std::make_shared<dsp::IRData>();
    ir->id = "test:" + std::to_string (seed);
    ir->displayName = "Test IR";
    ir->sampleRate = fs;
    std::mt19937 rng (seed);
    std::normal_distribution<float> d (0.f, 1.f);
    for (int c = 0; c < channels; ++c)
    {
        std::vector<float> h ((size_t) len);
        for (int i = 0; i < len; ++i) h[(size_t) i] = d (rng) * std::exp (-(float) i / (0.004f * (float) fs));
        ir->channels.push_back (h);
    }
    return ir;
}

std::vector<float> render (AmpForgeEngine& e, const std::vector<float>& in, int block, std::vector<float>* right = nullptr)
{
    std::vector<float> l (in.size()), r (in.size());
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - pos);
        e.process (in.data() + pos, l.data() + pos, r.data() + pos, n);
    }
    if (right) *right = r;
    return l;
}

std::vector<float> guitarish (double fs, int n, float amp)
{
    // Decaying harmonic-rich plucks: a deterministic stand-in for a guitar DI.
    std::vector<float> x ((size_t) n, 0.0f);
    const double notes[] = { 82.4, 110.0, 146.8, 196.0 };
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / fs;
            double v = 0;
            for (int h = 1; h <= 8; ++h) v += std::sin (dsp::kTwoPi * notes[k] * h * t) / h;
            x[(size_t) i] += (float) (amp * 0.3 * v * std::exp (-2.0 * t));
        }
    return x;
}
} // namespace

TEST_CASE ("Engine outputs zeros when unprepared and handles zero-length blocks")
{
    AmpForgeEngine e;
    std::vector<float> in (64, 0.5f), l (64, 1.f), r (64, 1.f);
    e.process (in.data(), l.data(), r.data(), 64);
    CHECK (test::peak (l) == 0.0f);
    e.prepare (48000, 64);
    e.process (in.data(), l.data(), r.data(), 0);
}

TEST_CASE ("Engine produces finite output for silence, guitar-like, extreme and invalid input at all rates")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (int model = 0; model < dsp::kNumAmpModels; ++model)
        {
            AmpForgeEngine e;
            auto& p = e.params();
            p.set (pAmpModel, (float) model);
            p.set (pAmpGain, 1.0f);
            p.set (pDriveOn, 1); p.set (pDriveDrive, 1.0f);
            p.set (pEqOn, 1); p.set (pEq80, 12); p.set (pEq6600, 12);
            p.set (pChorusOn, 1); p.set (pDelayOn, 1); p.set (pDelayFeedback, 0.95f); p.set (pReverbOn, 1);
            p.set (pReverbDecay, 10.0f);
            e.prepare (fs, 512);
            REQUIRE (e.setCabinetIR (0, testIR (48000, 4000, 1)));
            const int n = (int) (fs * 0.5);

            auto silence = render (e, std::vector<float> ((size_t) n, 0.0f), 128);
            CHECK (test::allFinite (silence));
            CHECK (test::peak (silence) < 1e-3f);

            auto g = render (e, guitarish (fs, n, 0.5f), 61);
            CHECK (test::allFinite (g));
            CHECK (test::peak (g) <= 1.0f);
            CHECK (test::peak (g) > 1e-3f);

            std::vector<float> extreme ((size_t) n);
            for (int i = 0; i < n; ++i) extreme[(size_t) i] = (i / 50) % 2 ? 1000.0f : -1000.0f;
            auto ex = render (e, extreme, 256);
            CHECK (test::allFinite (ex));
            CHECK (test::peak (ex) <= 1.0f);

            std::vector<float> bad ((size_t) 4096, std::numeric_limits<float>::quiet_NaN());
            bad[100] = std::numeric_limits<float>::infinity();
            auto b = render (e, bad, 64);
            CHECK (test::allFinite (b));
            auto after = render (e, guitarish (fs, 8192, 0.3f), 64);
            CHECK (test::allFinite (after));
        }
}

TEST_CASE ("Engine output does not depend on host block size")
{
    auto x = guitarish (48000, 24000, 0.4f);
    std::vector<float> ref;
    for (int block : { 512, 1, 7, 32, 33, 128, 4096 })
    {
        AmpForgeEngine e;
        e.params().set (pAmpModel, 2);
        e.params().set (pDelayOn, 1);
        e.params().set (pReverbOn, 1);
        e.prepare (48000, block);
        e.setCabinetIR (0, testIR (48000, 3000, 5));
        auto y = render (e, x, block);
        if (ref.empty()) { ref = y; continue; }
        double maxDiff = 0;
        for (size_t i = 0; i < y.size(); ++i) maxDiff = std::max (maxDiff, (double) std::abs (y[i] - ref[i]));
        INFO ("block " << block);
        CHECK (maxDiff < 1e-5);
    }
}

TEST_CASE ("Bypass passes the trimmed dry input to both channels")
{
    AmpForgeEngine e;
    e.params().set (pBypass, 1);
    e.params().set (pInTrim, -6.0f);
    e.prepare (48000, 64);
    auto x = test::sine (440, 48000, 9600, 0.5f);
    std::vector<float> r;
    auto l = render (e, x, 64, &r);
    for (int i = 4800; i < 9600; ++i)
    {
        CHECK (l[(size_t) i] == doctest::Approx (x[(size_t) i] * 0.501187f).epsilon (1e-3));
        CHECK (r[(size_t) i] == doctest::Approx (l[(size_t) i]));
    }
}

TEST_CASE ("Amp models are measurably distinct")
{
    auto x = guitarish (48000, 24000, 0.4f);
    std::vector<std::vector<float>> outs;
    for (int m = 0; m < dsp::kNumAmpModels; ++m)
    {
        AmpForgeEngine e;
        e.params().set (pAmpModel, (float) m);
        e.params().set (pCabOn, 0);
        e.prepare (48000, 128);
        outs.push_back (render (e, x, 128));
    }
    for (int a = 0; a < dsp::kNumAmpModels; ++a)
        for (int b = a + 1; b < dsp::kNumAmpModels; ++b)
        {
            // Compare spectral shape (level-independent): correlation of normalised outputs.
            double sab = 0, saa = 0, sbb = 0;
            for (size_t i = 4800; i < x.size(); ++i)
            {
                sab += outs[(size_t) a][i] * outs[(size_t) b][i];
                saa += outs[(size_t) a][i] * outs[(size_t) a][i];
                sbb += outs[(size_t) b][i] * outs[(size_t) b][i];
            }
            const double corr = sab / std::sqrt (saa * sbb + 1e-30);
            INFO ("models " << a << " vs " << b << " correlation " << corr);
            CHECK (corr < 0.97);
        }
}

TEST_CASE ("Gain, tone and master controls change the audio")
{
    const auto plucks = guitarish (48000, 24000, 0.3f);
    const auto hiTone = test::sine (4000.0, 48000, 24000, 0.05f); // presence acts above ~3.5 kHz
    std::vector<float> x;
    auto run = [&] (int param, float value)
    {
        AmpForgeEngine e;
        e.params().set (pAmpModel, 2);
        e.params().set (pCabOn, 0);
        e.params().set (param, value);
        e.prepare (48000, 128);
        return render (e, x, 128);
    };
    for (int param : { (int) pAmpGain, (int) pAmpBass, (int) pAmpMid, (int) pAmpTreble, (int) pAmpPresence, (int) pAmpMaster })
    {
        x = param == pAmpPresence ? hiTone : plucks;
        auto lo = run (param, 0.1f), hi = run (param, 0.9f);
        double diff = 0, ref = 0;
        for (size_t i = 4800; i < lo.size(); ++i) { diff += std::pow (lo[i] - hi[i], 2); ref += hi[i] * hi[i]; }
        INFO (std::string (paramDef (param).id));
        CHECK (diff / ref > 0.01);
    }
}

TEST_CASE ("Amp model switching and preset transitions are click-free")
{
    AmpForgeEngine e;
    e.params().set (pCabOn, 0);
    e.params().set (pAmpModel, 0);
    e.prepare (48000, 64);
    auto x = test::sine (110, 48000, 48000, 0.2f);
    std::vector<float> l (x.size()), r (x.size());
    float maxStep = 0, maxStepBefore = 0;
    for (size_t pos = 0; pos < x.size(); pos += 64)
    {
        if (pos == 16000) e.params().set (pAmpModel, 6);
        e.process (x.data() + pos, l.data() + pos, r.data() + pos, 64);
    }
    for (size_t i = 1; i < l.size(); ++i)
    {
        const float s = std::abs (l[i] - l[i - 1]);
        if (i < 16000) maxStepBefore = std::max (maxStepBefore, s);
        else if (i > 16000 + 2400) {} // new model's steady state is louder/brighter, ignore
        else maxStep = std::max (maxStep, s);
    }
    CHECK (maxStep < std::max (0.05f, maxStepBefore * 2.0f));
}

TEST_CASE ("beginTransition times out gracefully when audio is not running")
{
    AmpForgeEngine e;
    e.prepare (48000, 64);
    CHECK_FALSE (e.beginTransition (20));
    e.endTransition();
    // Audio resumes: muted state must clear and audio flow.
    auto x = test::sine (220, 48000, 4800, 0.3f);
    std::vector<float> l (x.size()), r (x.size());
    e.process (x.data(), l.data(), r.data(), (int) x.size());
    CHECK (test::peak (l, 2400) > 1e-3f);
}

TEST_CASE ("Concurrent IR publishing and parameter changes while audio runs")
{
    AmpForgeEngine e;
    e.prepare (48000, 128);
    e.setCabinetIR (0, testIR (44100, 2000, 1));
    std::atomic<bool> stop { false };
    // A second non-RT thread collecting garbage concurrently (as the UI timer does in the app).
    std::thread collector ([&] {
        while (! stop)
        {
            e.collectGarbage();
            std::this_thread::sleep_for (std::chrono::microseconds (300));
        }
    });
    std::thread loader ([&] {
        unsigned seed = 10;
        while (! stop)
        {
            e.setCabinetIR ((int) (seed & 1), testIR (seed % 2 ? 44100 : 96000, 1000 + (int) (seed % 7) * 500, seed, 1 + (int) (seed % 2)));
            e.params().set (pAmpModel, (float) (seed % 6));
            e.params().set (pCabDual, (float) (seed % 3 == 0));
            e.collectGarbage();
            ++seed;
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
        }
    });
    auto x = guitarish (48000, 128, 0.3f);
    std::vector<float> l (128), r (128);
    bool finite = true;
    for (int i = 0; i < 3000; ++i)
    {
        e.process (x.data(), l.data(), r.data(), 128);
        for (float v : l) finite = finite && std::isfinite (v) && std::abs (v) <= 1.0f;
    }
    stop = true;
    loader.join();
    collector.join();
    e.collectGarbage();
    CHECK (finite);
}

TEST_CASE ("Invalid IR is rejected and the previous IR is kept")
{
    AmpForgeEngine e;
    e.prepare (48000, 128);
    REQUIRE (e.setCabinetIR (0, testIR (48000, 1000, 3)));
    auto empty = std::make_shared<dsp::IRData>();
    std::string err;
    CHECK_FALSE (e.setCabinetIR (0, empty, &err));
    CHECK_FALSE (err.empty());
    auto nan = testIR (48000, 100, 4);
    nan->channels[0][10] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE (e.setCabinetIR (0, nan, &err));
    CHECK (e.getCabinetInfo (0).irId == "test:3");
}

TEST_CASE ("Noise gate attenuates hiss between notes but passes notes")
{
    AmpForgeEngine e;
    e.params().set (pAmpModel, 6);
    e.params().set (pAmpGain, 0.8f);
    e.params().set (pGateOn, 1);
    e.params().set (pGateThreshold, -50.0f);
    e.params().set (pCabOn, 0);
    e.prepare (48000, 128);
    std::mt19937 rng (1);
    std::normal_distribution<float> d (0, 0.0003f); // ~ -70 dBFS input hiss
    std::vector<float> x (96000);
    for (auto& v : x) v = d (rng);
    for (int i = 0; i < 24000; ++i) x[(size_t) i] += 0.2f * (float) std::sin (dsp::kTwoPi * 196.0 * i / 48000.0);
    auto y = render (e, x, 128);
    const double note = test::rms (y, 12000, 24000);
    const double hiss = test::rms (y, 72000, 96000);
    MESSAGE ("note " << test::db (note) << " dB, gated hiss " << test::db (hiss) << " dB");
    CHECK (test::db (note) - test::db (hiss) > 50.0);

    e.params().set (pGateOn, 0);
    auto y2 = render (e, x, 128);
    CHECK (test::db (test::rms (y2, 72000, 96000)) - test::db (hiss) > 30.0);
}

TEST_CASE ("Delay sync and output level")
{
    AmpForgeEngine e;
    e.params().set (pBypass, 0);
    e.params().set (pCabOn, 0);
    e.params().set (pAmpModel, 0);
    e.params().set (pAmpGain, 0.3f);
    e.params().set (pDelayOn, 1);
    e.params().set (pDelaySync, 1);
    e.params().set (pTempo, 120.0f);
    e.params().set (pDelayDivision, 1); // 1/4 at 120 = 500 ms
    e.params().set (pDelayMix, 1.0f);
    e.params().set (pDelayFeedback, 0.0f);
    e.prepare (48000, 64);
    std::vector<float> x (48000, 0.0f);
    for (int i = 100; i < 300; ++i) x[(size_t) i] = 0.3f * (float) std::sin (dsp::kTwoPi * 440.0 * i / 48000.0);
    auto y = render (e, x, 64);
    // Energy appears again ~500 ms later.
    CHECK (test::rms (y, 24100, 24400) > 0.2 * test::rms (y, 100, 400));
    CHECK (test::rms (y, 12000, 20000) < 1e-3);
}

TEST_CASE ("Valve swaps, pull, sag and variac change the sound; valve swaps are click-free")
{
    auto x = guitarish (48000, 48000, 0.3f);
    auto run = [&] (std::initializer_list<std::pair<int, float>> settings)
    {
        AmpForgeEngine e;
        e.params().set (pAmpModel, 2);
        e.params().set (pAmpPreampValve, 0);
        e.params().set (pAmpPowerValve, 1);
        e.params().set (pAmpGain, 0.6f);
        e.params().set (pAmpMaster, 0.8f);
        e.params().set (pCabOn, 0);
        for (auto [p, v] : settings) e.params().set (p, v);
        e.prepare (48000, 128);
        return render (e, x, 128);
    };
    const auto base = run ({});
    auto differs = [&] (const std::vector<float>& y)
    {
        double d = 0, r = 0;
        for (size_t i = 4800; i < y.size(); ++i) { d += std::pow (y[i] - base[i], 2); r += base[i] * base[i]; }
        return d / r;
    };
    CHECK (differs (run ({ { pAmpPreampValve, 2.0f } })) > 0.01);
    CHECK (differs (run ({ { pAmpPowerValve, 0.0f } })) > 0.01);
    CHECK (differs (run ({ { pAmpPull, 1.0f } })) > 0.01);
    CHECK (differs (run ({ { pPowerSag, 1.0f } })) > 1e-4);
    CHECK (differs (run ({ { pPowerVariac, 65.0f } })) > 0.01);
    for (int pv = 0; pv < dsp::kNumPowerValves; ++pv)
        for (int pre = 0; pre < dsp::kNumPreampValves; ++pre)
            CHECK (test::allFinite (run ({ { pAmpPreampValve, (float) pre }, { pAmpPowerValve, (float) pv }, { pPowerVariac, 60.0f }, { pPowerSag, 1.0f } })));

    // Swapping the power valve mid-stream goes through the fade: no click.
    AmpForgeEngine e;
    e.params().set (pAmpModel, 2);
    e.params().set (pCabOn, 0);
    e.prepare (48000, 64);
    auto sine = test::sine (110, 48000, 48000, 0.2f);
    std::vector<float> l (sine.size()), r (sine.size());
    for (size_t pos = 0; pos < sine.size(); pos += 64)
    {
        if (pos == 16000) e.params().set (pAmpPowerValve, 4);
        e.process (sine.data() + pos, l.data() + pos, r.data() + pos, 64);
    }
    float before = 0, around = 0;
    for (size_t i = 1; i < l.size(); ++i)
    {
        const float st = std::abs (l[i] - l[i - 1]);
        if (i < 16000) before = std::max (before, st);
        else if (i < 16000 + 2400) around = std::max (around, st);
    }
    CHECK (around < std::max (0.05f, before * 2.0f));
}

TEST_CASE ("Default valves and supply settings reproduce the calibrated amp")
{
    // Explicitly selecting each model's own valves with Sag 5 / Variac 100 % must be identical
    // to the voicing's calibrated configuration.
    auto x = guitarish (48000, 24000, 0.3f);
    for (int m = 0; m < dsp::kNumAmpModels; ++m)
    {
        const auto& v = dsp::getAmpVoicing (m);
        AmpForgeEngine a, b;
        for (auto* e : { &a, &b })
        {
            e->params().set (pAmpModel, (float) m);
            e->params().set (pCabOn, 0);
        }
        b.params().set (pAmpPreampValve, (float) v.defaultPreampValve);
        b.params().set (pAmpPowerValve, (float) v.defaultPowerValve);
        a.params().set (pAmpPreampValve, (float) v.defaultPreampValve);
        a.params().set (pAmpPowerValve, (float) v.defaultPowerValve);
        a.params().set (pPowerSag, 0.5f);
        a.params().set (pPowerVariac, 100.0f);
        a.prepare (48000, 128);
        b.prepare (48000, 128);
        const auto ya = render (a, x, 128), yb = render (b, x, 128);
        for (size_t i = 0; i < ya.size(); ++i)
            REQUIRE (ya[i] == yb[i]);
    }
}

TEST_CASE ("Changing the Studio Comp model while playing does not click")
{
    // A steady clean tone through the engine; step through every compressor model mid-stream.
    AmpForgeEngine e;
    e.params().set (pAmpModel, 1); // clean
    e.params().set (pCabOn, 0);
    e.params().set (pStudioCompOn, 1);
    e.params().set (pStudioCompAmount, 0.7f);
    e.prepare (48000, 128);
    const int n = 48000 * 3;
    auto x = test::sine (330.0, 48000, n, 0.3f);
    std::vector<float> l ((size_t) n), r ((size_t) n);
    for (int p = 0; p < n; p += 128)
    {
        if (p % 24000 == 0 && p > 0)
            e.params().set (pStudioCompType, (float) ((p / 24000) % 6));
        e.process (x.data() + p, l.data() + p, r.data() + p, std::min (128, n - p));
    }
    CHECK (test::allFinite (l));
    float normalStep = 0, maxStep = 0;
    for (int i = 16000; i < 24000; ++i) normalStep = std::max (normalStep, std::abs (l[(size_t) i] - l[(size_t) i - 1]));
    for (int i = 24000; i < n; ++i) maxStep = std::max (maxStep, std::abs (l[(size_t) i] - l[(size_t) i - 1]));
    MESSAGE ("max sample step " << maxStep << " (steady " << normalStep << ")");
    CHECK (maxStep < normalStep * 2.0f);
}

TEST_CASE ("Every reverb type produces a tail through the engine")
{
    for (int type = 0; type < dsp::Reverb::numTypes; ++type)
    {
        AmpForgeEngine e;
        e.params().set (pCabOn, 0);
        e.params().set (pAmpModel, 1);
        e.params().set (pReverbOn, 1);
        e.params().set (pReverbMix, 0.3f);
        e.params().set (pReverbType, (float) type);
        e.prepare (48000, 256);
        const int n = 48000 * 2;
        auto in = test::sine (220.0, 48000, n, 0.2f);
        std::fill (in.begin() + 24000, in.end(), 0.0f);
        std::vector<float> l ((size_t) n), r ((size_t) n);
        for (int p = 0; p < n; p += 256)
            e.process (in.data() + p, l.data() + p, r.data() + p, 256);
        const double tail = test::rms (l, 36000, 60000);
        INFO ("type " << type);
        MESSAGE ("reverb type " << type << " tail " << test::db (tail) << " dB");
        CHECK (tail > 1e-4);
    }
}

TEST_CASE ("Reverb at 100% mix is reverb only and as loud as the dry; CS-3 engages without a jump")
{
    // Sustained, guitar-like input.
    auto x = guitarish (48000, 48000 * 3, 0.3f);
    auto render = [&] (auto setup)
    {
        AmpForgeEngine e;
        e.params().set (pCabOn, 0);
        e.params().set (pAmpModel, 1);
        setup (e);
        e.prepare (48000, 256);
        std::vector<float> l (x.size()), r (x.size());
        for (size_t p = 0; p + 256 <= x.size(); p += 256)
            e.process (x.data() + p, l.data() + p, r.data() + p, 256);
        return l;
    };
    const auto dry = render ([] (AmpForgeEngine&) {});
    const double dryDb = test::db (test::rms (dry, 48000));
    for (int t = 0; t < dsp::Reverb::numTypes; ++t)
    {
        auto out = render ([t] (AmpForgeEngine& e) { e.params().set (pReverbOn, 1); e.params().set (pReverbType, (float) t); e.params().set (pReverbMix, 1.0f); });
        // At 100 % the output is the reverb alone.
        const double wetDb = test::db (test::rms (out, 48000));
        MESSAGE ("reverb type " << t << ": 100% mix (reverb only) is " << wetDb - dryDb << " dB relative to dry");
        CHECK (std::abs (wetDb - dryDb) < 2.0);
    }
    const auto cs3 = render ([] (AmpForgeEngine& e) { e.params().set (pCompOn, 1); });
    const double cs3Db = test::db (test::rms (cs3, 48000)) - dryDb;
    MESSAGE ("CS-3 default settings: " << cs3Db << " dB vs off");
    CHECK (cs3Db > -1.0);
    CHECK (cs3Db < 3.5);
}

TEST_CASE ("Switching the reverb on and off at 100% mix brings the dry back smoothly")
{
    AmpForgeEngine e;
    e.params().set (pCabOn, 0);
    e.params().set (pAmpModel, 1);
    e.params().set (pReverbOn, 1);
    e.params().set (pReverbMix, 1.0f);
    e.prepare (48000, 128);
    const int n = 48000 * 2;
    auto x = test::sine (330.0, 48000, n, 0.2f);
    std::vector<float> l ((size_t) n), r ((size_t) n);
    for (int p = 0; p < n; p += 128)
    {
        if (p == 48000) e.params().set (pReverbOn, 0);
        if (p == 72000) e.params().set (pReverbOn, 1);
        e.process (x.data() + p, l.data() + p, r.data() + p, 128);
    }
    float steady = 0, around = 0;
    for (int i = 40000; i < 47000; ++i) steady = std::max (steady, std::abs (l[(size_t) i] - l[(size_t) i - 1]));
    for (int i = 47000; i < n; ++i) around = std::max (around, std::abs (l[(size_t) i] - l[(size_t) i - 1]));
    MESSAGE ("max step " << around << " (steady " << steady << ")");
    CHECK (around < steady * 3.0f + 0.01f);
    // Off: the dry signal is back at full level.
    CHECK (test::rms (l, 60000, 71000) > 0.5 * test::rms (std::vector<float> (x.begin() + 60000, x.begin() + 71000)));
}

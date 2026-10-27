// AMP FORGE — offline renderer, test-signal generator and preset level calibration.
//
//   ampforge_render --generate <dir>              write the synthetic DI test set (WAV)
//   ampforge_render --preset "<name>" --in <di.wav> --out <out.wav> [--rate 48000]
//   ampforge_render --all-presets <dir> [--in <di.wav>]   render every factory preset
//   ampforge_render --calibrate                    measure preset loudness, suggest out.level
//
// The DI test set is synthesised (Karplus-Strong plucked strings with pick, pickup and
// palm-mute models), so it is original material that can ship with the project. It is a
// repeatable stand-in for real DI recordings — not a replacement for playing a guitar.
#include "Audio/AmpForgeEngine.h"
#include "IR/IRLibrary.h"
#include "Platform/FileUtils.h"
#include "Presets/PresetApplier.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>
#include <random>

using namespace af;

namespace
{
//==============================================================================
/** Plucked-string guitar DI synthesiser (deterministic). */
class GuitarSynth
{
public:
    explicit GuitarSynth (double sampleRate) : fs (sampleRate) {}

    struct Note
    {
        double start;        // seconds
        double freq;         // Hz
        double velocity;     // 0..1 (pick strength)
        double length = 2.0; // seconds before damping
        bool palmMute = false;
        double bendSemis = 0.0, bendTime = 0.0;
    };

    std::vector<float> render (const std::vector<Note>& notes, double seconds, float pickupGain, double brightness)
    {
        std::vector<float> out ((size_t) (seconds * fs), 0.0f);
        std::mt19937 rng (42);
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        for (const auto& n : notes)
        {
            const double baseLen = fs / n.freq;
            std::vector<float> line ((size_t) baseLen * 2 + 8, 0.0f);
            const int len0 = (int) baseLen;
            // Excitation: noise burst low-passed by pick hardness, with pick-position comb.
            float lp = 0.0f;
            const float pickLp = (float) (0.25 + 0.6 * n.velocity * brightness);
            for (int i = 0; i < len0; ++i)
            {
                lp += pickLp * (u (rng) - lp);
                line[(size_t) i] = lp * (float) n.velocity;
            }
            const int pickPos = std::max (1, len0 / 7);
            for (int i = len0 - 1; i >= pickPos; --i)
                line[(size_t) i] -= line[(size_t) (i - pickPos)];

            const int start = (int) (n.start * fs);
            const int stop = std::min ((int) out.size(), start + (int) ((n.length + 1.5) * fs));
            double readPos = 0.0;
            float prev = 0.0f;
            const double damp = n.palmMute ? 0.62 : 0.996;
            const double lpCoef = n.palmMute ? 0.35 : 0.5;
            const size_t L = line.size();
            size_t w = (size_t) len0;
            for (int i = start; i < stop; ++i)
            {
                const double t = (double) (i - start) / fs;
                double semis = 0.0;
                if (n.bendSemis != 0.0 && t > n.bendTime)
                    semis = n.bendSemis * std::min (1.0, (t - n.bendTime) / 0.25);
                const double period = baseLen / std::pow (2.0, semis / 12.0);
                // read `period` samples behind the write head (linear interpolation)
                readPos = (double) w - period;
                while (readPos < 0)
                    readPos += (double) L;
                const size_t r0 = (size_t) readPos % L;
                const size_t r1 = (r0 + 1) % L;
                const float frac = (float) (readPos - std::floor (readPos));
                const float y = line[r0] + frac * (line[r1] - line[r0]);
                const double release = (t > n.length) ? 0.9 : 1.0;
                const float filtered = (float) (damp * release * ((1.0 - lpCoef) * y + lpCoef * prev));
                prev = y;
                line[w % L] = filtered;
                w = (w + 1) % L;
                out[(size_t) i] += y * pickupGain;
            }
        }
        // Pickup: gentle resonant low-pass (single coil ~5 kHz, humbucker ~3 kHz, set by brightness).
        dsp::Biquad pu;
        pu.setCoeffs (dsp::BiquadCoeffs::lowPass (fs, 2500.0 + 3000.0 * brightness, 1.6));
        for (auto& v : out)
            v = pu.process (v);
        return out;
    }

private:
    double fs;
};

double noteHz (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); }

struct TestClip
{
    const char* name;
    std::vector<GuitarSynth::Note> notes;
    double seconds;
    float gain;        // pickup output level
    double brightness; // 1 = single coil, 0.3 = humbucker
};

std::vector<TestClip> testClips()
{
    using N = GuitarSynth::Note;
    std::vector<TestClip> clips;
    // E major open chord, strummed
    std::vector<N> chord;
    const int eMaj[] = { 40, 47, 52, 56, 59, 64 };
    for (int i = 0; i < 6; ++i) chord.push_back ({ 0.2 + i * 0.012, noteHz (eMaj[i]), 0.8, 3.0 });
    clips.push_back ({ "open-chord-singlecoil", chord, 4.0, 0.25f, 1.0 });
    clips.push_back ({ "open-chord-humbucker", chord, 4.0, 0.5f, 0.35 });

    std::vector<N> singles;
    const int line[] = { 52, 55, 57, 59, 62, 64, 62, 59 };
    for (int i = 0; i < 8; ++i) singles.push_back ({ 0.2 + i * 0.4, noteHz (line[i]), 0.7, 0.35 });
    clips.push_back ({ "single-notes-clean", singles, 4.0, 0.25f, 1.0 });

    std::vector<N> dyn;
    for (int i = 0; i < 8; ++i) dyn.push_back ({ 0.2 + i * 0.5, noteHz (45), 0.15 + 0.12 * i, 0.45 });
    clips.push_back ({ "soft-to-hard-picking", dyn, 4.5, 0.35f, 0.6 });

    std::vector<N> chug;
    for (int i = 0; i < 16; ++i) chug.push_back ({ 0.2 + i * 0.15, noteHz (40), 0.85, 0.12, true });
    for (int i = 0; i < 2; ++i) { chug.push_back ({ 2.6 + i * 0.8, noteHz (40), 0.9, 0.7 }); chug.push_back ({ 2.6 + i * 0.8, noteHz (47), 0.9, 0.7 }); chug.push_back ({ 2.6 + i * 0.8, noteHz (52), 0.9, 0.7 }); }
    clips.push_back ({ "palm-mutes-power-chords-humbucker", chug, 4.5, 0.55f, 0.35 });

    std::vector<N> lead { { 0.2, noteHz (69), 0.8, 2.5, false, 2.0, 0.6 }, { 3.0, noteHz (72), 0.8, 1.2 } };
    clips.push_back ({ "lead-bend-humbucker", lead, 4.5, 0.5f, 0.4 });

    clips.push_back ({ "volume-rolled-back", chord, 4.0, 0.08f, 0.8 });
    return clips;
}

bool writeWav (const juce::File& f, const std::vector<std::vector<float>>& chans, double rate)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = f.createOutputStream();
    if (os == nullptr)
        return false;
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions {}.withSampleRate (rate)
                                          .withNumChannels ((int) chans.size()).withBitsPerSample (24));
    if (w == nullptr)
        return false;
    std::vector<const float*> ptrs;
    for (auto& c : chans) ptrs.push_back (c.data());
    return w->writeFromFloatArrays (ptrs.data(), (int) ptrs.size(), (int) chans[0].size());
}

std::vector<float> readMono (const juce::File& f, double& rate)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr)
        return {};
    rate = r->sampleRate;
    juce::AudioBuffer<float> b (1, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, false);
    return { b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples() };
}

std::vector<std::vector<float>> renderThrough (AmpForgeEngine& e, const std::vector<float>& in)
{
    std::vector<float> l (in.size()), r (in.size());
    for (size_t pos = 0; pos < in.size(); pos += 128)
    {
        const int n = (int) std::min<size_t> (128, in.size() - pos);
        e.process (in.data() + pos, l.data() + pos, r.data() + pos, n);
        if ((pos / 128) % 64 == 0)
            e.collectGarbage();
    }
    return { l, r };
}

double loudnessDb (const std::vector<std::vector<float>>& st)
{
    // Simple loudness proxy: RMS after an 80 Hz high-pass and a gentle 4 kHz shelf cut,
    // averaged over both channels. Good enough to align presets; not a LUFS meter.
    double sum = 0.0;
    size_t count = 0;
    for (const auto& ch : st)
    {
        dsp::Biquad hp, sh;
        hp.setCoeffs (dsp::BiquadCoeffs::highPass (48000, 80, 0.7));
        sh.setCoeffs (dsp::BiquadCoeffs::highShelf (48000, 4000, 0.7, -4));
        for (float v : ch)
        {
            const float y = sh.process (hp.process (v));
            sum += (double) y * y;
            ++count;
        }
    }
    return 10.0 * std::log10 (std::max (1e-20, sum / (double) std::max<size_t> (1, count)));
}

std::vector<float> concatClips (double fs)
{
    std::vector<float> all;
    GuitarSynth gs (fs);
    for (const auto& c : testClips())
    {
        if (juce::String (c.name) == "volume-rolled-back")
            continue;
        auto v = gs.render (c.notes, c.seconds, c.gain, c.brightness);
        all.insert (all.end(), v.begin(), v.end());
    }
    return all;
}
} // namespace

int main (int argc, char** argv)
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    auto argAfter = [&] (const char* flag) { const int i = args.indexOf (flag); return i >= 0 && i + 1 < args.size() ? args[i + 1] : juce::String(); };
    const double fs = argAfter ("--rate").isNotEmpty() ? argAfter ("--rate").getDoubleValue() : 48000.0;

    if (args.contains ("--generate"))
    {
        const juce::File dir (juce::File::getCurrentWorkingDirectory().getChildFile (argAfter ("--generate")));
        dir.createDirectory();
        GuitarSynth gs (fs);
        for (const auto& c : testClips())
        {
            auto v = gs.render (c.notes, c.seconds, c.gain, c.brightness);
            float pk = 0; for (float x : v) pk = std::max (pk, std::abs (x));
            writeWav (dir.getChildFile (juce::String (c.name) + ".wav"), { v }, fs);
            std::printf ("%-36s peak %6.1f dBFS\n", c.name, 20 * std::log10 (std::max (pk, 1e-9f)));
        }
        return 0;
    }

    IRLibrary irLib (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ampforge-render-irs"));
    irLib.loadFactory();
    PresetManager presets (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ampforge-render-presets"));
    presets.loadFactory();

    auto renderPreset = [&] (const PresetState& st, const std::vector<float>& in)
    {
        AmpForgeEngine e;
        e.prepare (fs, 128);
        juce::StringArray warnings;
        applyState (e, irLib, st, warnings);
        for (auto& w : warnings) std::printf ("  warning: %s\n", w.toRawUTF8());
        return renderThrough (e, in);
    };

    if (args.contains ("--calibrate"))
    {
        const auto in = concatClips (fs);
        const double target = -20.0;
        std::printf ("%-26s %10s %10s %12s\n", "preset", "loudness", "out.level", "suggested");
        for (const auto& p : presets.getPresets())
        {
            const auto out = renderPreset (p.state, in);
            const double l = loudnessDb (out);
            float pk = 0; for (auto& ch : out) for (float v : ch) pk = std::max (pk, std::abs (v));
            const double cur = p.state.values[(size_t) pOutLevel];
            std::printf ("%-26s %8.1f dB %8.1f dB %10.1f dB   (peak %.1f dBFS)\n", p.state.name.toRawUTF8(), l, cur,
                         cur + (target - l), 20 * std::log10 (std::max (pk, 1e-9f)));
        }
        return 0;
    }

    std::vector<float> input;
    double inRate = fs;
    if (argAfter ("--in").isNotEmpty())
    {
        input = readMono (juce::File::getCurrentWorkingDirectory().getChildFile (argAfter ("--in")), inRate);
        if (input.empty()) { std::fprintf (stderr, "could not read input\n"); return 1; }
        if (std::abs (inRate - fs) > 1) input = dsp::resampleOffline (input, inRate, fs);
    }
    else
        input = concatClips (fs);

    if (args.contains ("--all-presets"))
    {
        const juce::File dir (juce::File::getCurrentWorkingDirectory().getChildFile (argAfter ("--all-presets")));
        dir.createDirectory();
        int i = 1;
        for (const auto& p : presets.getPresets())
        {
            const auto out = renderPreset (p.state, input);
            const auto name = juce::String (i++).paddedLeft ('0', 2) + "-" + juce::File::createLegalFileName (p.state.name) + ".wav";
            writeWav (dir.getChildFile (name), out, fs);
            std::printf ("rendered %s\n", name.toRawUTF8());
        }
        return 0;
    }

    const auto presetName = argAfter ("--preset");
    const auto outPath = argAfter ("--out");
    const int idx = presets.findIndex (presetName, true);
    if (idx < 0 || outPath.isEmpty())
    {
        std::fprintf (stderr, "usage: see header of Tools/Render.cpp\n");
        return 1;
    }
    const auto out = renderPreset (presets.getPresets()[(size_t) idx].state, input);
    return writeWav (juce::File::getCurrentWorkingDirectory().getChildFile (outPath), out, fs) ? 0 : 1;
}

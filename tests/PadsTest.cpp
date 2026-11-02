// Checks the button box (src/engine/Pads.h) without the app: a 1 kHz tone at -18 dBFS played from
// a pad comes out at the right level and length (also from a 44.1 kHz file into a 48 kHz engine),
// each play mode and choke group behaves, Stop all fades out, and 16 pads at once render cleanly
// and quickly. Prints PASS/FAIL per check; exits non-zero on any failure.
#include <juce_audio_formats/juce_audio_formats.h>
#include "../src/engine/Pads.h"

namespace
{
    constexpr double engineRate = 48000.0;
    constexpr int block = 480;
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    }

    juce::File writeTone (double rate, double seconds, const juce::String& name)
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
        file.deleteFile();
        juce::AudioBuffer<float> tone (1, (int) (rate * seconds));
        for (int i = 0; i < tone.getNumSamples(); ++i)
            tone.setSample (0, i, 0.125893f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / rate));   // -18 dBFS peak

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), rate, 1, 24, {}, 0));
        writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
        return file;
    }

    // Renders `seconds` of the pads; returns the left channel.
    std::vector<float> run (Pads& pads, double seconds)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, block);
        for (int done = 0; done < (int) (engineRate * seconds); done += block)
        {
            pads.render (buffer, block, engineRate);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
        }
        return out;
    }

    double rmsDb (const std::vector<float>& x, size_t from, size_t to)
    {
        double sum = 0.0;
        for (size_t i = from; i < to; ++i) sum += (double) x[i] * x[i];
        return 10.0 * std::log10 (sum / (double) (to - from) + 1.0e-20);
    }

    float peak (const std::vector<float>& x, size_t from, size_t to)
    {
        float p = 0.0f;
        for (size_t i = from; i < to; ++i) p = juce::jmax (p, std::abs (x[i]));
        return p;
    }

    size_t lastSound (const std::vector<float>& x)
    {
        for (size_t i = x.size(); i-- > 0;)
            if (std::abs (x[i]) > 1.0e-4f)
                return i;
        return 0;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::String error;
    const auto tone48 = writeTone (48000.0, 1.0, "pads-test-48k.wav");
    const auto tone44 = writeTone (44100.0, 1.0, "pads-test-44k.wav");

    {
        Pads pads;
        check (pads.load (0, tone48, engineRate, error), "loads a WAV");
        pads.press (0);
        const auto out = run (pads, 1.5);
        const double rms = rmsDb (out, 4800, 43200);
        check (std::abs (rms - -21.03) < 0.1, "one-shot level: RMS " + juce::String (rms, 2) + " dBFS (expect -21.03)");
        check (std::abs (juce::Decibels::gainToDecibels (peak (out, 4800, 43200)) - -18.0f) < 0.1f, "one-shot peak -18 dBFS");
        const auto end = lastSound (out);
        check (std::abs ((double) end - 48000.0) < 50.0, "plays once, to the end: stops at sample " + juce::String ((int) end) + " (expect ~48000)");
        check (! pads.isPlaying (0), "reports stopped afterwards");
    }

    {
        Pads pads;
        pads.load (0, tone44, engineRate, error);
        pads.press (0);
        const auto out = run (pads, 1.5);
        const auto end = lastSound (out);
        check (std::abs ((double) end - 48000.0) < 60.0, "44.1 kHz file resampled: lasts 1 s at 48 kHz (ends at " + juce::String ((int) end) + ")");
        check (std::abs (rmsDb (out, 4800, 43200) - -21.03) < 0.15, "44.1 kHz file level " + juce::String (rmsDb (out, 4800, 43200), 2) + " dBFS");
    }

    {
        Pads pads;
        pads.load (0, tone48, engineRate, error);
        pads.pad (0).mode = Pads::Mode::loop;
        pads.press (0);
        auto out = run (pads, 2.5);
        check (pads.isPlaying (0) && peak (out, (size_t) (2.2 * engineRate), out.size()) > 0.1f, "loop keeps playing past its end");
        pads.press (0);   // stop
        out = run (pads, 0.1);
        check (! pads.isPlaying (0) && peak (out, 2400, out.size()) < 1.0e-4f, "loop: second press stops it within 30 ms");
    }

    {
        Pads pads;
        pads.load (0, tone48, engineRate, error);
        pads.pad (0).mode = Pads::Mode::hold;
        pads.press (0);
        run (pads, 0.2);
        pads.release (0);
        const auto out = run (pads, 0.1);
        check (peak (out, 1920, out.size()) < 1.0e-4f, "hold: fades out within 40 ms of release");
    }

    {
        Pads pads;
        pads.load (0, tone48, engineRate, error);
        pads.load (1, tone48, engineRate, error);
        pads.pad (0).choke = pads.pad (1).choke = 1;
        pads.press (0);
        run (pads, 0.1);
        pads.press (1);
        run (pads, 0.05);
        check (! pads.isPlaying (0) && pads.isPlaying (1), "choke group: the second pad cuts the first off");
    }

    {
        Pads pads;
        for (int i = 0; i < 16; ++i)
        {
            pads.load (i, tone48, engineRate, error);
            pads.pad (i).gain = 1.0f / 16.0f;
        }
        for (int i = 0; i < 16; ++i)
            pads.press (i);

        juce::AudioBuffer<float> buffer (2, block);
        double worstMs = 0.0;
        bool finite = true;
        int playing = 0;
        for (int b = 0; b < 50; ++b)
        {
            const auto start = juce::Time::getHighResolutionTicks();
            pads.render (buffer, block, engineRate);
            worstMs = juce::jmax (worstMs, juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start) * 1000.0);
            for (int i = 0; i < block; ++i)
                finite = finite && std::isfinite (buffer.getSample (0, i));
            if (b == 10)
                for (int i = 0; i < 16; ++i)
                    playing += pads.isPlaying (i) ? 1 : 0;
        }
        check (playing == 16, "16 pads at once all play (" + juce::String (playing) + ")");
        check (finite, "16 pads: no NaN or infinity");
        check (worstMs < 1.0, "16 pads: slowest 10 ms block took " + juce::String (worstMs, 3) + " ms (budget 1 ms)");

        pads.stopAll();
        const auto out = run (pads, 0.1);
        check (peak (out, 1920, out.size()) < 1.0e-4f, "Stop all: silent within 40 ms");
    }

    // Trim and fades: a 2 s tone trimmed to 0.5 .. 1.5 s with 100 ms fades in and out plays for
    // exactly 1 s, silent at its very start and end, full level in the middle; back to the whole
    // file when the edits are undone.
    {
        const auto tone2s = writeTone (48000.0, 2.0, "pads-test-2s.wav");
        Pads pads;
        juce::String error;
        pads.load (2, tone2s, engineRate, error);
        auto& p = pads.pad (2);
        p.trimStart = 0.5;
        p.trimEnd = 1.5;
        p.fadeIn = p.fadeOut = 0.1;
        pads.applyEdits (2);
        check (std::abs (p.seconds() - 1.0) < 0.001 && std::abs (p.fullSeconds() - 2.0) < 0.001,
               "trim: plays " + juce::String (p.seconds(), 3) + " s of the " + juce::String (p.fullSeconds(), 3) + " s file");
        pads.press (2);
        const auto out = run (pads, 1.2);
        check (std::abs (out[0]) < 1.0e-4f && peak (out, 47900, 48000) < 0.005f, "fades: silent at the very start and end");
        check (std::abs (rmsDb (out, 12000, 36000) - -21.03) < 0.1, "fades: full level between them (" + juce::String (rmsDb (out, 12000, 36000), 2) + " dB)");
        const float halfWay = peak (out, 2400, 2400 + 480);
        check (halfWay > 0.05f && halfWay < 0.08f, "fades: about half way up 50 ms in (" + juce::String (halfWay, 3) + ")");
        check (peak (out, 49000, 57600) == 0.0f, "trim: nothing after its end");
        p.trimStart = p.trimEnd = p.fadeIn = p.fadeOut = 0.0;
        pads.applyEdits (2);
        check (p.sound == p.full, "edits undone: the whole file again, uncopied");
        tone2s.deleteFile();
    }

    tone48.deleteFile();
    tone44.deleteFile();
    std::cout << (failures == 0 ? "\nAll passed.\n" : "\nFailures: " + std::to_string (failures) + "\n");
    return failures == 0 ? 0 : 1;
}

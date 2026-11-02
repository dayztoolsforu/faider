// Checks the tape deck's player (src/engine/Tape.h) without the app: a -18 dBFS tone comes out
// at the right level (also from a 44.1 kHz file into a 48 kHz engine), one track runs straight
// into the next with no gap, seek, pause, resume and stop land where they should and fade (also
// at the very end of the playlist and inside another fade), a queued track is dropped when it no
// longer follows, and a block renders quickly. Prints PASS/FAIL per check; exits non-zero on any failure.
//
// Rendering is paced (a short sleep per block) so the background read-ahead keeps up, as it does
// in real time.
#include <juce_audio_formats/juce_audio_formats.h>
#include "../src/engine/Tape.h"

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

    juce::File temp (const juce::String& name) { return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tape-test").getChildFile (name); }

    // A stereo file: a 1 kHz tone at -18 dBFS peak, or (dc >= 0) a constant value.
    juce::File write (const juce::String& name, double rate, double seconds, float dc = -1.0f)
    {
        auto file = temp (name);
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        juce::AudioBuffer<float> audio (2, (int) (rate * seconds));
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            const float v = dc >= 0.0f ? dc : 0.125893f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / rate);
            audio.setSample (0, i, v);
            audio.setSample (1, i, v);
        }
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), rate, 2, 24, {}, 0));
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        return file;
    }

    // Renders `seconds` of the tape (calling update() as the panel's timer would); returns the left channel.
    std::vector<float> run (Tape& tape, double seconds)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, block);
        for (int done = 0, n = 0; done < (int) (engineRate * seconds); done += block, ++n)
        {
            tape.render (buffer, block, engineRate);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
            if (n % 3 == 0)
                tape.update (engineRate);
            juce::Thread::sleep (2);
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

    void settle (Tape& tape) { tape.update (engineRate); juce::Thread::sleep (100); }   // lets the read-ahead fill
}

int main()
{
    const auto tone48 = write ("tone-48k.wav", 48000.0, 4.0);
    const auto tone44 = write ("tone-44k.wav", 44100.0, 2.0);
    const auto quarter = write ("dc-quarter.wav", 48000.0, 1.0, 0.25f);
    const auto half = write ("dc-half.wav", 48000.0, 1.0, 0.5f);

    // Level, and the fade in at the start.
    {
        Tape tape;
        tape.update (engineRate);
        tape.add (tone48);
        tape.play (0);
        settle (tape);
        const auto out = run (tape, 1.0);
        const double level = rmsDb (out, 4800, 48000);
        check (std::abs (level - -21.03) < 0.1, "level: 1 kHz at -18 dBFS peak plays at " + juce::String (level, 2) + " dB RMS (expect -21.03)");
        check (std::abs (out[0]) < 0.001f && peak (out, 0, 240) < peak (out, 2400, 4800) * 0.5f, "starts with a fade in, not a click");
        check (std::abs (tape.length() - 4.0) < 0.001, "length " + juce::String (tape.length(), 3) + " s (expect 4.000)");
        check (std::abs (tape.position() - 1.0) < 0.011, "position " + juce::String (tape.position(), 3) + " s after 1 s");

        // Seek: fades out, jumps, fades back in.
        tape.seek (3.0);
        const auto after = run (tape, 0.2);
        check (std::abs (tape.position() - 3.17) < 0.02, "seek to 3.0 s: at " + juce::String (tape.position(), 3) + " s 0.2 s later (expect ~3.17, the 30 ms fade out first)");
        check (peak (after, 0, 1440) > 0.01f && peak (after, 1400, 1480) < 0.02f, "seek fades out before it jumps");

        // Pause and resume.
        tape.togglePlay();
        const auto paused = run (tape, 0.3);
        const double at = tape.position();
        check (! tape.isPlaying() && peak (paused, 2400, paused.size()) == 0.0f, "pause: silent after the fade, not playing");
        run (tape, 0.2);
        check (std::abs (tape.position() - at) < 0.0001, "paused position holds");
        tape.togglePlay();
        const auto resumed = run (tape, 0.3);
        check (tape.isPlaying() && rmsDb (resumed, 4800, resumed.size()) > -21.2, "resume: plays again");
        check (std::abs (tape.position() - (at + 0.3)) < 0.011, "resume carries on from " + juce::String (at, 3) + " s");

        // Stop: back to the start.
        tape.stop();
        run (tape, 0.2);
        check (! tape.isPlaying() && tape.position() == 0.0, "stop: not playing, back to the start");

        // Load: one block of a playing track.
        tape.togglePlay();
        settle (tape);
        juce::AudioBuffer<float> buffer (2, block);
        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < 100; ++i)
            tape.render (buffer, block, engineRate);
        const double ms = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start) * 1000.0 / 100.0;
        check (ms < 0.1, "render: " + juce::String (ms, 4) + " ms per 10 ms block");
    }

    // A 44.1 kHz file into a 48 kHz engine.
    {
        Tape tape;
        tape.update (engineRate);
        tape.add (tone44);
        tape.play (0);
        settle (tape);
        const auto out = run (tape, 1.0);
        check (std::abs (tape.length() - 2.0) < 0.001, "44.1 kHz file: length " + juce::String (tape.length(), 3) + " s (expect 2.000)");
        const double level = rmsDb (out, 4800, 48000);
        check (std::abs (level - -21.03) < 0.15, "44.1 kHz file: level " + juce::String (level, 2) + " dB RMS (expect -21.03)");
    }

    // Gapless: 1 s at 0.25 runs straight into 1 s at 0.5; with repeat off, then the end.
    {
        Tape tape;
        tape.update (engineRate);
        tape.repeat = Tape::Repeat::off;
        tape.add (quarter);
        tape.add (half);
        tape.play (0);
        settle (tape);
        const auto out = run (tape, 2.5);
        size_t gaps = 0, change = 0;
        for (size_t i = 1440; i < 96000 - 8; ++i)
        {
            if (out[i] < 0.2f) ++gaps;
            if (change == 0 && out[i] > 0.375f) change = i;
        }
        check (gaps == 0, "gapless: no dip between the tracks (" + juce::String ((int) gaps) + " samples)");
        check (std::abs ((int) change - 48000) <= 4, "the next track starts at sample " + juce::String ((int) change) + " (expect 48000)");
        check (std::abs (out[47000] - 0.25f) < 0.002f && std::abs (out[60000] - 0.5f) < 0.002f, "both tracks at their level");
        check (tape.current() == 1, "the deck shows the second track (" + juce::String (tape.current()) + ")");
        check (! tape.isPlaying() && peak (out, 96100, out.size()) == 0.0f, "repeat off: stops at the end of the playlist");

        // Next and previous.
        tape.repeat = Tape::Repeat::all;
        tape.play (0);
        run (tape, 0.2);
        tape.next();
        run (tape, 0.2);
        check (tape.current() == 1, "next: second track");
        tape.previous();
        run (tape, 0.2);
        check (tape.current() == 0, "previous: first track");
    }

    // No TAPE channel (the engine calls drain() instead of render()): presses are taken and
    // dropped, so nothing piles up to burst out when a TAPE channel arrives.
    {
        Tape tape;
        tape.update (engineRate);
        tape.add (tone48);
        for (int i = 0; i < 100; ++i)
        {
            tape.play (0);
            tape.drain();
        }
        tape.update (engineRate);
        check (! tape.isPlaying() && ! tape.isLoaded(), "no TAPE channel: 100 PLAYs taken and dropped, nothing loaded");
        tape.play (0);
        settle (tape);
        const auto out = run (tape, 0.3);
        check (tape.isPlaying() && peak (out, 4800, out.size()) > 0.1f, "then a TAPE channel: PLAY plays");
    }

    // The playlist edited while playing; a seek while paused.
    {
        Tape tape;
        tape.update (engineRate);
        tape.add (quarter);
        tape.add (half);
        tape.add (tone48);
        tape.play (1);
        settle (tape);
        run (tape, 0.2);
        tape.remove (0);
        run (tape, 0.2);
        check (tape.current() == 0 && tape.file (0) == half, "an earlier track removed: the deck stays on the one playing (" + juce::String (tape.current()) + ")");
        tape.togglePlay();
        run (tape, 0.1);
        tape.seek (0.5);
        run (tape, 0.1);
        check (! tape.isPlaying() && std::abs (tape.position() - 0.5) < 0.02, "seek while paused: waits at " + juce::String (tape.position(), 3) + " s (expect 0.5)");
        tape.togglePlay();
        run (tape, 0.1);
        check (tape.isPlaying() && tape.position() > 0.55, "and plays on from there");
    }

    // Stop pressed in the last 10 ms of the playlist: the playlist ends during the fade, and the
    // stop still lands (loaded at the start, with what it queued after it).
    {
        Tape tape;
        tape.update (engineRate);
        tape.repeat = Tape::Repeat::off;
        tape.add (quarter);
        tape.add (half);
        tape.play (1);
        settle (tape);
        run (tape, 0.99);
        tape.repeat = Tape::Repeat::all;   // so the stop queues the first track after it
        tape.stop();
        run (tape, 0.2);
        check (tape.isLoaded() && ! tape.isPlaying() && tape.position() == 0.0 && tape.current() == 1,
               "stop at the very end: loaded at " + juce::String (tape.position(), 3) + " s, not playing (expect 0)");
        tape.togglePlay();
        const auto out = run (tape, 1.5);
        check (tape.isPlaying() && tape.current() == 0 && std::abs (out[24000] - 0.5f) < 0.002f && std::abs (out[60000] - 0.25f) < 0.002f,
               "then PLAY plays it and carries on to the queued next (" + juce::String (tape.current()) + ")");
    }

    // Repeat one turned off on the last track: the copy of it queued for repeating is dropped.
    {
        Tape tape;
        tape.update (engineRate);
        tape.repeat = Tape::Repeat::one;
        tape.add (quarter);
        tape.add (half);
        tape.play (1);
        settle (tape);
        run (tape, 0.3);
        tape.repeat = Tape::Repeat::off;
        tape.setShuffle (tape.getShuffle());   // as the deck's REPEAT button does
        const auto out = run (tape, 1.0);
        check (! tape.isPlaying() && tape.current() == 1 && peak (out, 36000, out.size()) == 0.0f, "repeat one -> off on the last track: doesn't play it again");
    }

    // A seek within a pause's fade (the message thread still thought it was playing): stays paused.
    {
        Tape tape;
        tape.update (engineRate);
        tape.add (tone48);
        tape.play (0);
        settle (tape);
        run (tape, 0.5);
        tape.togglePlay();
        run (tape, 0.01);   // one block: still fading out
        tape.seek (2.0);
        run (tape, 0.2);
        check (! tape.isPlaying() && std::abs (tape.position() - 2.0) < 0.01, "pause then seek in the fade: paused at " + juce::String (tape.position(), 3) + " s (expect 2.0)");
    }

    temp ({}).deleteRecursively();
    std::cout << (failures == 0 ? "ALL PASS\n" : juce::String (failures) + " FAILED\n");
    return failures == 0 ? 0 : 1;
}

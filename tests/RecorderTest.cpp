// Checks the tape deck's recorder (src/engine/Recorder.h) without the app: 1 s of a -18 dBFS tone
// offered block by block, then 0.5 s where the source isn't processed, then 0.5 s of another
// source that mustn't be kept. The file must be a 24-bit stereo WAV at 48 kHz, exactly 2 s long,
// the tone at the right level, then silence. Prints PASS/FAIL per check; exits non-zero on failure.
#include <juce_audio_formats/juce_audio_formats.h>
#include "../src/engine/Recorder.h"

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 480;
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    }

    double rmsDb (const juce::AudioBuffer<float>& b, int ch, int from, int to)
    {
        double sum = 0.0;
        for (int i = from; i < to; ++i) sum += (double) b.getSample (ch, i) * b.getSample (ch, i);
        return 10.0 * std::log10 (sum / (double) (to - from) + 1.0e-20);
    }
}

int main()
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("recorder-test");
    folder.deleteRecursively();

    Recorder recorder;
    juce::SpinLock lock;
    juce::String error;
    const auto file = recorder.start (Recorder::Kind::output, 1, "OUT MUSIC", folder, rate, lock, error);
    check (file.existsAsFile(), "starts a file: " + (file == juce::File() ? error : file.getFileName()));
    check (file.getFileName().startsWith ("Faider OUT MUSIC 20"), "named by source and date");
    check (recorder.isRecording(), "recording");

    juce::AudioBuffer<float> tone (2, block), other (2, block);
    other.clear();
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < block; ++i) other.setSample (ch, i, 0.5f);
    double phase = 0.0;

    auto run = [&] (double seconds, bool offerTone)
    {
        for (int done = 0; done < (int) (rate * seconds); done += block)
        {
            for (int i = 0; i < block; ++i, phase += juce::MathConstants<double>::twoPi * 1000.0 / rate)
                for (int ch = 0; ch < 2; ++ch)
                    tone.setSample (ch, i, 0.125893f * (float) std::sin (phase));
            recorder.beginBlock();
            recorder.offer (Recorder::Kind::output, 0, other, block);    // another output: not this one
            recorder.offer (Recorder::Kind::input, 1, other, block);     // same index, other kind: not this one
            if (offerTone)
                recorder.offer (Recorder::Kind::output, 1, tone, block);
            recorder.endBlock (block);
            juce::Thread::sleep (1);
        }
    };
    run (1.5, true);    // 1.5 s of tone
    run (0.5, false);   // the source switched off: silence

    check (std::abs (recorder.seconds() - 2.0) < 0.001, "time " + juce::String (recorder.seconds(), 3) + " s (expect 2.000)");
    recorder.stop (lock);
    check (! recorder.isRecording(), "stopped");
    check (recorder.droppedBlocks() == 0, "no blocks dropped");

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (new juce::FileInputStream (file), true));
    check (reader != nullptr, "the file reads back as WAV");
    if (reader != nullptr)
    {
        check (reader->bitsPerSample == 24 && reader->numChannels == 2 && reader->sampleRate == rate,
               juce::String (reader->bitsPerSample) + "-bit, " + juce::String (reader->numChannels) + " channels, " + juce::String (reader->sampleRate, 0) + " Hz");
        check (reader->lengthInSamples == (juce::int64) (rate * 2.0), "length " + juce::String (reader->lengthInSamples) + " samples (expect 96000)");

        juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples);
        reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
        const double level = rmsDb (audio, 0, 4800, 67200);
        check (std::abs (level - -21.03) < 0.05, "tone level " + juce::String (level, 2) + " dB RMS (expect -21.03)");
        check (std::abs (rmsDb (audio, 1, 4800, 67200) - level) < 0.01, "both channels");
        check (audio.getMagnitude (0, 72000, 24000) == 0.0f, "silence while the source was off (not the other sources)");
    }

    // Every other format: 1 s of the tone, read back at the right length and level.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (auto& f : Recorder::formats())
    {
        if (f.id == "wav24" || f.codec.isNotEmpty())   // FFmpeg's convert in the background, after stop
            continue;
        recorder.format = f.id;
        const auto other = recorder.start (Recorder::Kind::output, 1, "FORMAT " + f.id, folder, rate, lock, error);
        run (1.0, true);
        recorder.stop (lock);
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (other));
        bool ok = r != nullptr && other.hasFileExtension (f.extension);
        double level = -200.0;
        if (r != nullptr)
        {
            juce::AudioBuffer<float> audio (2, (int) r->lengthInSamples);
            r->read (&audio, 0, audio.getNumSamples(), 0, true, true);
            level = rmsDb (audio, 0, 4800, juce::jmin (43200, audio.getNumSamples()));
            ok = ok && std::abs ((double) r->lengthInSamples - rate) < 2048.0 && std::abs (level - -21.03) < 0.3;
        }
        check (ok, f.name + ": " + other.getFileName().fromLastOccurrenceOf (" ", false, false) + ", " + juce::String (level, 2) + " dB RMS");
    }

    // Space estimates: the drive's free space less the reserve, at each format's rate; AIFF
    // capped at its 4 GB; two tracks get half the time of one.
    {
        const double room = (double) (recorder.freeSpace() - Recorder::diskReserve);
        const double wav = recorder.secondsThatFit ("wav24", rate, 1);
        check (room > 0.0 && std::abs (wav - room / (rate * 2 * 3)) < 1.0, "estimate: WAV 24-bit " + juce::String (wav / 3600.0, 1) + " h on this drive");
        check (std::abs (recorder.secondsThatFit ("wav24", rate, 2) - wav / 2) < 1.0, "estimate: two tracks, half the time");
        check (recorder.secondsThatFit ("flac24", rate, 1) > wav && recorder.secondsThatFit ("ogg128", rate, 1) > recorder.secondsThatFit ("ogg320", rate, 1),
               "estimate: FLAC beats WAV, OGG 128k beats 320k");
        const double aiff = recorder.secondsThatFit ("aiff24", rate, 1);
        check (aiff <= 4294967295.0 / (rate * 2 * 3) + 1.0, "estimate: AIFF at most its 4 GB (" + juce::String (aiff / 3600.0, 1) + " h)");
        check (recorder.stopIfFull (lock).isEmpty(), "not recording: nothing to stop");
    }

    // Multitrack: three sources, each its own file; one switched off half way. Every file must be
    // the same length, sample for sample, with what was offered to it (and silence after for the
    // one switched off).
    {
        recorder.format = "wav24";
        const std::vector<Recorder::TrackSource> sources { { Recorder::Kind::input, 0, "MIC" }, { Recorder::Kind::input, 1, "MUSIC" },
                                                           { Recorder::Kind::output, 0, "STREAM" } };
        const auto dir = recorder.startMultitrack (sources, folder, rate, lock, error);
        check (dir.isDirectory() && dir.getFileName().startsWith ("Faider MULTITRACK 20"), "multitrack: a folder " + (dir == juce::File() ? error : dir.getFileName()));
        check (recorder.isRecording() && recorder.isMultitracking() && recorder.trackCount() == 3, "multitrack: recording 3 tracks");
        for (int b = 0; b < (int) rate / block; ++b)   // 1 s; MUSIC only the first half
        {
            for (int i = 0; i < block; ++i, phase += juce::MathConstants<double>::twoPi * 1000.0 / rate)
                for (int ch = 0; ch < 2; ++ch)
                    tone.setSample (ch, i, 0.125893f * (float) std::sin (phase));
            recorder.beginBlock();
            recorder.offer (Recorder::Kind::input, 0, tone, block);
            if (b < (int) rate / block / 2)
                recorder.offer (Recorder::Kind::input, 1, other, block);
            recorder.offer (Recorder::Kind::output, 0, tone, block);
            recorder.endBlock (block);
            juce::Thread::sleep (1);
        }
        recorder.stop (lock);
        check (! recorder.isRecording(), "multitrack: stopped");

        juce::StringArray names;
        bool sameLength = true;
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.wav"))
        {
            names.add (f.getFileNameWithoutExtension());
            std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (new juce::FileInputStream (f), true));
            sameLength = sameLength && r != nullptr && r->lengthInSamples == (juce::int64) rate;
            if (r != nullptr && f.getFileName().startsWith ("IN MUSIC"))
            {
                juce::AudioBuffer<float> audio (2, (int) r->lengthInSamples);
                r->read (&audio, 0, audio.getNumSamples(), 0, true, true);
                check (std::abs (audio.getSample (0, 1000) - 0.5f) < 0.001f && audio.getMagnitude (0, 24000, 24000) == 0.0f,
                       "multitrack: the track switched off half way holds silence after");
            }
        }
        names.sort (false);
        check (names.joinIntoString (",") == "IN MIC,IN MUSIC,OUT STREAM", "multitrack: files " + names.joinIntoString (", "));
        check (sameLength, "multitrack: every file 48000 samples long");
    }

    // A recording follows its source when strips move (remapLocked), and says when it's gone.
    {
        Recorder follower;
        const auto made = follower.start (Recorder::Kind::input, 2, "IN FOLLOW", folder, rate, lock, error);
        juce::AudioBuffer<float> mine (2, block), theirs (2, block);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
            {
                mine.setSample (ch, i, 0.25f);
                theirs.setSample (ch, i, 0.75f);
            }
        auto blockOf = [&] (int mineAt, int theirsAt)
        {
            follower.beginBlock();
            follower.offer (Recorder::Kind::input, mineAt, mine, block);
            follower.offer (Recorder::Kind::input, theirsAt, theirs, block);
            follower.endBlock (block);
        };
        for (int i = 0; i < 10; ++i) blockOf (2, 1);   // recording input 2
        const bool lostOnMove = follower.remapLocked (Recorder::Kind::input, [] (int i) { return i == 0 ? -1 : i > 0 ? i - 1 : i; });
        for (int i = 0; i < 10; ++i) blockOf (1, 2);   // input 0 removed: it's input 1 now, and another strip is at 2
        const bool otherKindLeftAlone = ! follower.remapLocked (Recorder::Kind::output, [] (int) { return -1; });
        const bool lostOnRemove = follower.remapLocked (Recorder::Kind::input, [] (int i) { return i == 1 ? -1 : i; });
        follower.stop (lock);

        check (! lostOnMove && otherKindLeftAlone && lostOnRemove, "follows: kept through a move, told when its strip goes, other kinds left alone");
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (made));
        juce::AudioBuffer<float> audio (2, (int) (reader != nullptr ? reader->lengthInSamples : 0));
        if (reader != nullptr)
            reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
        check (audio.getNumSamples() == 20 * block && std::abs (audio.getMagnitude (0, 0, audio.getNumSamples()) - 0.25f) < 0.001f,
               "follows: all " + juce::String (audio.getNumSamples()) + " samples are its own strip's, none the strip that took its place");
    }

    folder.deleteRecursively();
    std::cout << (failures == 0 ? "ALL PASS\n" : juce::String (failures) + " FAILED\n");
    return failures == 0 ? 0 : 1;
}

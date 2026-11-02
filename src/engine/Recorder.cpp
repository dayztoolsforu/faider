#include "Recorder.h"

Recorder::Recorder()
{
    silence.setSize (2, 8192);
    silence.clear();
    thread.startThread (juce::Thread::Priority::normal);
}

Recorder::~Recorder()
{
    owned = nullptr;   // flushes and closes the file (nothing's recording by now: the engine stopped it)
    tracks.clear();
    thread.stopThread (2000);
}

juce::String Recorder::fileNameFor (const juce::String& sourceName)
{
    const auto cleaned = juce::File::createLegalFileName (sourceName).trim();
    return juce::String (Product::name) + " " + cleaned + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
}

std::vector<Recorder::Format> Recorder::formats()
{
    constexpr double aiffLimit = 4294967295.0;   // its chunk sizes are 32-bit
    std::vector<Format> list {
        { "wav24", "WAV 24-BIT", ".wav", {}, 24 },   { "wav16", "WAV 16-BIT", ".wav", {}, 16 },
        { "wav32", "WAV 32-BIT", ".wav", {}, 32 },   { "wav32f", "WAV 32-BIT FLOAT", ".wav", {}, 32 },
        { "flac24", "FLAC 24-BIT", ".flac", {}, 24, 0, 0.6f }, { "flac16", "FLAC 16-BIT", ".flac", {}, 16, 0, 0.6f },
        { "aiff24", "AIFF 24-BIT", ".aiff", {}, 24, 0, 1.0f, aiffLimit }, { "aiff16", "AIFF 16-BIT", ".aiff", {}, 16, 0, 1.0f, aiffLimit },
        { "ogg128", "OGG VORBIS 128K", ".ogg", {}, 0, 128 }, { "ogg", "OGG VORBIS 192K", ".ogg", {}, 0, 192 },
        { "ogg256", "OGG VORBIS 256K", ".ogg", {}, 0, 256 }, { "ogg320", "OGG VORBIS 320K", ".ogg", {}, 0, 320 },
    };
    if (ffmpeg().existsAsFile())
    {
        list.push_back ({ "mp3128", "MP3 128K (FFMPEG)", ".mp3", "-c:a libmp3lame -b:a 128k", 0, 128 });
        list.push_back ({ "mp3192", "MP3 192K (FFMPEG)", ".mp3", "-c:a libmp3lame -b:a 192k", 0, 192 });
        list.push_back ({ "mp3", "MP3 256K (FFMPEG)", ".mp3", "-c:a libmp3lame -b:a 256k", 0, 256 });
        list.push_back ({ "mp3320", "MP3 320K (FFMPEG)", ".mp3", "-c:a libmp3lame -b:a 320k", 0, 320 });
        list.push_back ({ "aac256", "AAC 256K M4A (FFMPEG)", ".m4a", "-c:a aac -b:a 256k", 0, 256 });
        list.push_back ({ "opus160", "OPUS 160K (FFMPEG)", ".opus", "-c:a libopus -b:a 160k", 0, 160 });
        list.push_back ({ "alac", "ALAC M4A (FFMPEG)", ".m4a", "-c:a alac", 24, 0, 0.6f });
    }
    return list;
}

juce::int64 Recorder::freeSpace() const
{
    auto dir = folder;   // may not be made yet: its nearest folder that is
    while (! dir.isDirectory() && dir != dir.getParentDirectory())
        dir = dir.getParentDirectory();
    return dir.getBytesFreeOnVolume();
}

double Recorder::diskBytesPerSecond (const Format& f, double sampleRate)
{
    if (f.codec.isNotEmpty())
        return sampleRate * 2 * 3;   // its 24-bit WAV
    if (f.kbps > 0)
        return f.kbps * 1000.0 / 8.0;
    return sampleRate * 2 * f.bits / 8.0 * f.packed;
}

juce::File Recorder::firstFile() const
{
    return multitracking.load() ? (tracks.empty() ? juce::File() : tracks.front()->file) : file;
}

double Recorder::secondsThatFit (const juce::String& formatId, double sampleRate, int tracks) const
{
    const auto f = formatFor (formatId);
    const double perSecond = diskBytesPerSecond (f, sampleRate);
    double seconds = (double) juce::jmax<juce::int64> (0, freeSpace() - diskReserve) / (perSecond * juce::jmax (1, tracks));
    if (f.maxBytes > 0.0)
    {
        const double used = isRecording() ? (double) firstFile().getSize() : 0.0;
        seconds = juce::jmin (seconds, juce::jmax (0.0, f.maxBytes - used) / perSecond);
    }
    return seconds;
}

juce::String Recorder::stopIfFull (juce::SpinLock& lock)
{
    if (! isRecording())
        return {};
    const auto f = formatFor (format);
    juce::String why;
    if (freeSpace() < diskReserve)
        why = "less than 1 GB left on the drive";
    else if (f.maxBytes > 0.0 && (double) firstFile().getSize() > f.maxBytes - 64.0 * 1024 * 1024)   // the writer's 2 s buffer and then some
        why = f.name + " files stop at 4 GB";
    if (why.isEmpty())
        return {};
    const auto name = currentFile().getFileName();
    stop (lock);
    return "recording stopped, " + why + ": " + name;
}

Recorder::Format Recorder::formatFor (const juce::String& id)
{
    for (auto& f : formats())
        if (f.id == id)
            return f;
    return formats().front();
}

juce::File Recorder::ffmpeg()
{
    static const juce::File found = []
    {
        juce::StringArray dirs;
        dirs.addTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), ";", "\"");
        for (auto& d : dirs)
            if (auto f = juce::File (d.trim()).getChildFile ("ffmpeg.exe"); d.isNotEmpty() && f.existsAsFile())
                return f;
        return juce::File();
    }();
    return found;
}

std::unique_ptr<juce::AudioFormatWriter> Recorder::createWriter (const juce::String& id, std::unique_ptr<juce::OutputStream>& stream,
                                                                 double sampleRate, juce::String& error)
{
    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (2);
    std::unique_ptr<juce::AudioFormatWriter> w;
    if (id == "wav16")       w = juce::WavAudioFormat().createWriterFor (stream, options.withBitsPerSample (16));
    else if (id == "wav32")  w = juce::WavAudioFormat().createWriterFor (stream, options.withBitsPerSample (32));
    else if (id == "wav32f") w = juce::WavAudioFormat().createWriterFor (stream, options.withBitsPerSample (32).withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    else if (id == "flac24") w = juce::FlacAudioFormat().createWriterFor (stream, options.withBitsPerSample (24));
    else if (id == "flac16") w = juce::FlacAudioFormat().createWriterFor (stream, options.withBitsPerSample (16));
    else if (id == "aiff24") w = juce::AiffAudioFormat().createWriterFor (stream, options.withBitsPerSample (24));
    else if (id == "aiff16") w = juce::AiffAudioFormat().createWriterFor (stream, options.withBitsPerSample (16));
    else if (id.startsWith ("ogg"))   // OggVorbisAudioFormat::getQualityOptions: 128k is 4, 192k 6, 256k 8, 320k 9
        w = juce::OggVorbisAudioFormat().createWriterFor (stream, options.withBitsPerSample (32)
                                                                     .withQualityOptionIndex (id == "ogg128" ? 4 : id == "ogg256" ? 8 : id == "ogg320" ? 9 : 6));
    else                     w = juce::WavAudioFormat().createWriterFor (stream, options.withBitsPerSample (24));   // wav24, and the FFmpeg formats' WAV
    if (w == nullptr)
        error = "Can't make a " + formatFor (id).name + " writer at " + juce::String (sampleRate, 0) + " Hz";
    return w;
}

void Recorder::convertWithFFmpeg (const juce::File& wav, const juce::File& target, const juce::String& codec)
{
    juce::Thread::launch ([wav, target, codec]
    {
        juce::ChildProcess ffmpegProcess;
        juce::StringArray args { ffmpeg().getFullPathName(), "-y", "-loglevel", "error", "-i", wav.getFullPathName() };
        args.addTokens (codec, " ", {});
        args.add (target.getFullPathName());
        // No time limit: a recording of many hours takes minutes to convert.
        if (ffmpegProcess.start (args, 0) && ffmpegProcess.waitForProcessToFinish (-1)
            && ffmpegProcess.getExitCode() == 0 && target.existsAsFile())
            wav.deleteFile();   // kept if the conversion failed
    });
}

juce::File Recorder::start (Kind k, int index, const juce::String& sourceName, const juce::File& into, double sampleRate,
                            juce::SpinLock& lock, juce::String& error)
{
    stop (lock);

    if (! into.createDirectory())
    {
        error = "Can't make the folder " + into.getFullPathName();
        return {};
    }
    if (freeSpace() < diskReserve)
    {
        error = "Less than 1 GB is free on the drive of " + into.getFullPathName();
        return {};
    }

    // FFmpeg formats: recorded as WAV, converted when it stops.
    const auto chosen = formatFor (format);
    const bool viaFFmpeg = chosen.codec.isNotEmpty();
    auto target = into.getNonexistentChildFile (fileNameFor (sourceName), viaFFmpeg ? ".wav" : chosen.extension, false);
    auto out = std::make_unique<juce::FileOutputStream> (target);
    if (! out->openedOk())
    {
        error = "Can't write " + target.getFullPathName();
        return {};
    }

    std::unique_ptr<juce::OutputStream> stream (std::move (out));   // the writer takes it
    auto w = createWriter (chosen.id, stream, sampleRate, error);
    if (w == nullptr)
    {
        stream = nullptr;
        target.deleteFile();
        return {};
    }
    convertTo = viaFFmpeg ? target.withFileExtension (chosen.extension) : juce::File();
    convertCodec = chosen.codec;

    // Two seconds of room between the audio thread and the disk.
    owned = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), thread, (int) (sampleRate * 2.0));
    rate = sampleRate;
    file = target;
    written = 0;
    dropped = 0;

    const juce::SpinLock::ScopedLockType sl (lock);
    kind = (int) k;
    source = index;
    writer = owned.get();
    return target;
}

juce::File Recorder::startMultitrack (const std::vector<TrackSource>& sources, const juce::File& into, double sampleRate,
                                      juce::SpinLock& lock, juce::String& error)
{
    stop (lock);

    const auto folder = into.getNonexistentChildFile (fileNameFor ("MULTITRACK"), {}, false);
    if (freeSpace() < diskReserve)
    {
        error = "Less than 1 GB is free on the drive of " + into.getFullPathName();
        return {};
    }
    if (sources.empty() || ! folder.createDirectory())
    {
        error = sources.empty() ? juce::String ("Nothing to record.") : "Can't make the folder " + folder.getFullPathName();
        return {};
    }

    const auto chosen = formatFor (format);
    const bool viaFFmpeg = chosen.codec.isNotEmpty();
    convertCodec = chosen.codec;
    std::vector<std::unique_ptr<Track>> fresh;
    for (auto& s : sources)
    {
        const auto prefix = s.kind == Kind::input ? "IN " : s.kind == Kind::bus ? "BUS " : "OUT ";
        auto target = folder.getNonexistentChildFile (juce::File::createLegalFileName (prefix + s.name).trim(), viaFFmpeg ? ".wav" : chosen.extension, false);
        std::unique_ptr<juce::OutputStream> stream (target.createOutputStream().release());
        auto w = stream != nullptr ? createWriter (chosen.id, stream, sampleRate, error) : nullptr;
        if (w == nullptr)
        {
            if (error.isEmpty())
                error = "Can't write " + target.getFullPathName();
            stream = nullptr;
            fresh.clear();   // closes the ones already made
            folder.deleteRecursively();
            return {};
        }
        auto t = std::make_unique<Track>();
        t->kind = s.kind;
        t->index = s.index;
        t->file = target;
        t->convertTo = viaFFmpeg ? target.withFileExtension (chosen.extension) : juce::File();
        t->writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), thread, (int) (sampleRate * 2.0));
        fresh.push_back (std::move (t));
    }

    rate = sampleRate;
    file = folder;
    written = 0;
    dropped = 0;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (tracks, fresh);
        multitracking = true;
    }
    return folder;
}

void Recorder::stopMultitrack (juce::SpinLock& lock)
{
    std::vector<std::unique_ptr<Track>> finished;
    {
        const juce::SpinLock::ScopedLockType sl (lock);   // the audio thread is between blocks
        std::swap (tracks, finished);
        multitracking = false;
    }
    for (auto& t : finished)
    {
        t->writer = nullptr;   // writes what's left and finishes the file
        if (t->convertTo != juce::File())
            convertWithFFmpeg (t->file, t->convertTo, convertCodec);
    }
}

void Recorder::stop (juce::SpinLock& lock)
{
    if (multitracking.load())
        stopMultitrack (lock);
    if (writer.load() == nullptr)
        return;
    {
        const juce::SpinLock::ScopedLockType sl (lock);   // the audio thread is between blocks
        writer = nullptr;
    }
    owned = nullptr;   // writes what's left and finishes the file
    if (convertTo != juce::File())
    {
        convertWithFFmpeg (file, convertTo, convertCodec);
        file = convertTo;
        convertTo = juce::File();
    }
}

void Recorder::armReplay (Kind k, int index, double seconds, double sampleRate, juce::SpinLock& lock)
{
    const int length = seconds > 0.0 ? (int) (seconds * sampleRate) : 0;
    if (length == ring.getNumSamples() && (length == 0 || (replaySource.load() == index && replayKind.load() == (int) k)))
        return;   // as it is: keep what's buffered

    juce::AudioBuffer<float> fresh;
    if (length > 0)
    {
        fresh.setSize (2, length);
        fresh.clear();
    }
    {
        const juce::SpinLock::ScopedLockType sl (lock);   // the audio thread is between blocks
        std::swap (ring, fresh);
        replayKind = (int) k;
        replaySource = length > 0 ? index : -1;
        ringWritten = 0;
        replayRate = sampleRate;
    }
}   // the old buffer goes here, outside the lock

void Recorder::writeRing (const float* const* data, int numSamples) noexcept
{
    const int length = ring.getNumSamples();
    int pos = (int) (ringWritten.load() % length);
    for (int done = 0; done < numSamples;)
    {
        const int chunk = juce::jmin (numSamples - done, length - pos);
        for (int ch = 0; ch < 2; ++ch)
            ring.copyFrom (ch, pos, data[ch] + done, chunk);
        done += chunk;
        pos = (pos + chunk) % length;
    }
    ringWritten += numSamples;
}

std::shared_ptr<juce::AudioBuffer<float>> Recorder::copyReplay (juce::String& error) const
{
    const int length = ring.getNumSamples();
    if (length == 0 || ringWritten.load() == 0)
    {
        error = "Nothing buffered yet.";
        return {};
    }

    // A copy of the newest audio, oldest first. The oldest quarter second is left out: the audio
    // thread may be writing over it while this copies. The memory is taken first (a long replay's
    // can take a while), and only then where the audio is, so the margin is for the copy alone.
    const int most = juce::jmax (1, length - (int) (replayRate * 0.25));
    auto audio = std::make_shared<juce::AudioBuffer<float>> (2, most);
    const auto total = ringWritten.load();
    const int count = (int) juce::jmin<juce::int64> (total, most);
    audio->setSize (2, count, true, false, true);
    const int start = (int) ((total - count) % length);
    const int first = juce::jmin (count, length - start);
    for (int ch = 0; ch < 2; ++ch)
    {
        audio->copyFrom (ch, 0, ring, ch, start, first);
        if (count > first)
            audio->copyFrom (ch, first, ring, ch, 0, count - first);
    }
    return audio;
}

juce::File Recorder::saveReplayNow (const juce::String& name, juce::String& error)
{
    const auto audio = copyReplay (error);
    if (audio == nullptr)
        return {};
    if (! folder.createDirectory())
    {
        error = "Can't make the folder " + folder.getFullPathName();
        return {};
    }
    const auto target = folder.getNonexistentChildFile (fileNameFor (name), ".wav", false);
    std::unique_ptr<juce::OutputStream> stream (target.createOutputStream().release());
    auto w = stream != nullptr ? createWriter ("wav24", stream, replayRate, error) : nullptr;
    if (w == nullptr || ! w->writeFromAudioSampleBuffer (*audio, 0, audio->getNumSamples()))
    {
        if (error.isEmpty())
            error = "Can't write " + target.getFullPathName();
        return {};
    }
    return target;   // finished when the writer goes, here
}

juce::File Recorder::saveReplay (const juce::String& sourceName, juce::String& error)
{
    const auto audio = copyReplay (error);
    if (audio == nullptr)
        return {};

    if (! folder.createDirectory())
    {
        error = "Can't make the folder " + folder.getFullPathName();
        return {};
    }
    const auto chosen = formatFor (format);
    const bool viaFFmpeg = chosen.codec.isNotEmpty();
    // A name free as both the WAV and what FFmpeg makes of it (FFmpeg overwrites), taken now, so
    // a second replay saved in the same second gets a name of its own.
    const auto base = fileNameFor ("REPLAY " + sourceName);
    const juce::String extension = viaFFmpeg ? ".wav" : chosen.extension;
    auto target = folder.getChildFile (base + extension);
    for (int n = 2; target.exists() || target.withFileExtension (chosen.extension).exists(); ++n)
        target = folder.getChildFile (base + " (" + juce::String (n) + ")" + extension);
    if (! target.create())
    {
        error = "Can't write " + target.getFullPathName();
        return {};
    }

    // Written in the background, so a failure there can't be reported back to the press; the
    // empty or partial file is at least not left behind looking like a replay.
    juce::Thread::launch ([audio, target, sampleRate = replayRate, chosen, viaFFmpeg]
    {
        bool wrote = false;
        {
            std::unique_ptr<juce::OutputStream> stream (target.createOutputStream().release());
            juce::String error;
            if (stream != nullptr)
                if (auto w = createWriter (chosen.id, stream, sampleRate, error))
                    wrote = w->writeFromAudioSampleBuffer (*audio, 0, audio->getNumSamples());
        }   // closed here
        if (! wrote)
        {
            target.deleteFile();
            return;
        }
        if (viaFFmpeg)
            convertWithFFmpeg (target, target.withFileExtension (chosen.extension), chosen.codec);
    });
    return viaFFmpeg ? target.withFileExtension (chosen.extension) : target;
}

void Recorder::beginBlock() noexcept
{
    offered = replayOffered = false;
    if (multitracking.load())
        for (auto& t : tracks)
            t->offered = false;
}

bool Recorder::remapLocked (Kind k, const std::function<int (int)>& newIndex) noexcept
{
    const auto follow = [&] (int index) { return index >= 0 ? newIndex (index) : index; };
    if (replayKind.load() == (int) k)
        replaySource = follow (replaySource.load());
    for (auto& t : tracks)
        if (t->kind == k)
            t->index = follow (t->index);
    if (writer.load() == nullptr || kind.load() != (int) k || source.load() < 0)
        return false;
    source = follow (source.load());
    return source.load() < 0;
}

void Recorder::offer (Kind k, int index, const juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    if (! replayOffered && ring.getNumSamples() > 0 && index == replaySource.load() && (int) k == replayKind.load())
    {
        replayOffered = true;
        writeRing (buffer.getArrayOfReadPointers(), numSamples);
    }

    if (multitracking.load())
    {
        for (auto& t : tracks)
            if (! t->offered && t->kind == k && t->index == index)
            {
                t->offered = true;
                if (! t->writer->write (buffer.getArrayOfReadPointers(), numSamples))
                    ++dropped;
            }
        return;
    }

    auto* w = writer.load();
    if (w == nullptr || offered || (int) k != kind.load() || index != source.load())
        return;
    offered = true;
    if (! w->write (buffer.getArrayOfReadPointers(), numSamples))
        ++dropped;   // the disk fell more than two seconds behind
    written += numSamples;
}

void Recorder::endBlock (int numSamples) noexcept
{
    if (! replayOffered && ring.getNumSamples() > 0)   // the replay's source is off: silence keeps its time
        for (int done = 0; done < numSamples;)
        {
            const int n = juce::jmin (numSamples - done, silence.getNumSamples());
            writeRing (silence.getArrayOfReadPointers(), n);
            done += n;
        }

    if (multitracking.load())   // each track not processed this block (switched off) keeps time with silence
    {
        for (auto& t : tracks)
            for (int done = 0; ! t->offered && done < numSamples;)
            {
                const int n = juce::jmin (numSamples - done, silence.getNumSamples());
                if (! t->writer->write (silence.getArrayOfReadPointers(), n))
                    ++dropped;
                done += n;
            }
        written += numSamples;
        return;
    }

    auto* w = writer.load();
    if (w == nullptr || offered)
        return;
    // The source wasn't processed this block (switched off): keep time with silence.
    for (int done = 0; done < numSamples;)
    {
        const int n = juce::jmin (numSamples - done, silence.getNumSamples());
        if (! w->write (silence.getArrayOfReadPointers(), n))
            ++dropped;
        done += n;
    }
    written += numSamples;
}

#include "Tape.h"
#include <algorithm>
#include <numeric>
#include <random>

namespace
{
    constexpr double fadeSeconds = 0.03;
    constexpr double ringSeconds = 2.0;      // read ahead of the audio thread
    constexpr double primeSeconds = 0.1;     // read when opened, so it starts at once
    constexpr int chunk = 4096;              // read and converted at a time
    constexpr const char* extensions = "wav;flac;aif;aiff;ogg;mp3";
    constexpr const char* wildcards = "*.wav;*.flac;*.aif;*.aiff;*.ogg;*.mp3";
}

// One opened file, from where it was opened: read, decoded and converted to the engine's rate by
// the read-ahead thread into `ring`, which the audio thread empties.
struct Tape::Track : public juce::TimeSliceClient
{
    explicit Track (juce::TimeSliceThread& t) : thread (t) {}

    ~Track() override
    {
        thread.removeTimeSliceClient (this);   // (waits for a read in progress)
        resampler = nullptr;                   // reads from the reader: freed first
        reader = nullptr;
    }

    // Read-ahead thread (and once on the message thread, before the thread is given it).
    int useTimeSlice() override
    {
        if (finished.load())
            return 200;
        const int n = (int) juce::jmin<juce::int64> (ring.getFreeSpace(), chunk, length - produced);
        if (n <= 0)
        {
            if (produced >= length)
                finished = true;
            return 20;
        }
        juce::AudioSourceChannelInfo info (&scratch, 0, n);
        resampler->getNextAudioBlock (info);

        int start1, size1, start2, size2;
        ring.prepareToWrite (n, start1, size1, start2, size2);
        for (int ch = 0; ch < 2; ++ch)
        {
            if (size1 > 0) ringData.copyFrom (ch, start1, scratch, ch, 0, size1);
            if (size2 > 0) ringData.copyFrom (ch, start2, scratch, ch, size1, size2);
        }
        ring.finishedWrite (size1 + size2);
        produced += n;
        return ring.getFreeSpace() > chunk ? 0 : 20;
    }

    // Audio thread: up to `n` samples into `out` at `at`; returns how many there were.
    int read (juce::AudioBuffer<float>& out, int at, int n)
    {
        int start1, size1, start2, size2;
        ring.prepareToRead (n, start1, size1, start2, size2);
        for (int ch = 0; ch < 2; ++ch)
        {
            if (size1 > 0) out.copyFrom (ch, at, ringData, ch, start1, size1);
            if (size2 > 0) out.copyFrom (ch, at + size1, ringData, ch, start2, size2);
        }
        ring.finishedRead (size1 + size2);
        played += size1 + size2;
        return size1 + size2;
    }

    bool ended() const { return played >= length || (finished.load() && ring.getNumReady() == 0); }

    juce::TimeSliceThread& thread;
    int index = -1;                                     // in the playlist when opened
    std::unique_ptr<juce::AudioFormatReaderSource> reader;
    std::unique_ptr<juce::ResamplingAudioSource> resampler;
    juce::int64 length = 0;                             // the whole file, at the engine's rate
    juce::int64 produced = 0;                           // read-ahead thread: read up to here
    juce::int64 played = 0;                             // audio thread: played up to here
    std::atomic<juce::int64> playedShown { 0 };
    std::atomic<bool> finished { false };               // read to the end
    juce::AbstractFifo ring { 1 };
    juce::AudioBuffer<float> ringData, scratch { 2, chunk };
};

Tape::Tape()
{
    formats.registerBasicFormats();
    readAhead.startThread (juce::Thread::Priority::normal);
}

Tape::~Tape()
{
    tracks.clear();
    readAhead.stopThread (2000);
}

bool Tape::isSoundFile (const juce::File& f) { return f.hasFileExtension (extensions); }

//==============================================================================
void Tape::add (const juce::File& f)
{
    if (f.isDirectory())
    {
        auto found = f.findChildFiles (juce::File::findFiles, true, wildcards);
        std::sort (found.begin(), found.end(), [] (const juce::File& a, const juce::File& b) { return a.getFullPathName().compareNatural (b.getFullPathName()) < 0; });
        for (auto& child : found)
            playlist.push_back (child);
    }
    else if (isSoundFile (f))
    {
        playlist.push_back (f);
    }
    reorder();
}

void Tape::remove (int index)
{
    if (! juce::isPositiveAndBelow (index, size()))
        return;
    playlist.erase (playlist.begin() + index);
    if (currentIndex == index)      currentIndex = -1;
    else if (currentIndex > index)  --currentIndex;
    renumber ([index] (int i) { return i == index ? -1 : i > index ? i - 1 : i; });
    reorder();
}

// The playlist changed under opened tracks (the one playing, the one queued): their places in it follow.
void Tape::renumber (const std::function<int (int)>& newIndex)
{
    for (auto& t : tracks)
        if (t->index >= 0)
            t->index = newIndex (t->index);
}

void Tape::clearPlaylist()
{
    send ({ Command::eject });
    playlist.clear();
    currentIndex = nextIndex = -1;
    reorder();
}

void Tape::move (int from, int to)
{
    if (! juce::isPositiveAndBelow (from, size()) || ! juce::isPositiveAndBelow (to, size()) || from == to)
        return;
    const auto f = playlist[(size_t) from];
    playlist.erase (playlist.begin() + from);
    playlist.insert (playlist.begin() + to, f);
    auto moved = [from, to] (int i)
    {
        if (i == from) return to;
        if (from < i && to >= i) return i - 1;
        if (from > i && to <= i) return i + 1;
        return i;
    };
    currentIndex = currentIndex >= 0 ? moved (currentIndex) : currentIndex;
    renumber (moved);
    reorder();
}

void Tape::setShuffle (bool on)
{
    shuffle = on;
    reorder();
}

void Tape::reorder()
{
    order.resize (playlist.size());
    std::iota (order.begin(), order.end(), 0);
    if (shuffle)
    {
        std::random_device seed;
        std::shuffle (order.begin(), order.end(), std::mt19937 (seed()));
        // The playing track first, so the shuffle carries on from it.
        if (auto it = std::find (order.begin(), order.end(), currentIndex); it != order.end())
            std::iter_swap (order.begin(), it);
    }
    if (currentIndex >= 0 && nowShown.load() != nullptr)
        queueFollowing();   // the next track may have changed
}

int Tape::following (int index, bool skipping) const
{
    if (playlist.empty() || index < 0)
        return -1;
    if (repeat == Repeat::one && ! skipping)
        return index;
    const auto it = std::find (order.begin(), order.end(), index);
    const auto pos = it == order.end() ? -1 : (int) (it - order.begin());
    if (pos + 1 < (int) order.size())
        return order[(size_t) pos + 1];
    return repeat == Repeat::all ? order.front() : -1;
}

int Tape::preceding (int index) const
{
    if (playlist.empty())
        return -1;
    const auto it = std::find (order.begin(), order.end(), index);
    const auto pos = it == order.end() ? 0 : (int) (it - order.begin());
    return pos > 0 ? order[(size_t) pos - 1] : (repeat == Repeat::off ? order.front() : order.back());
}

//==============================================================================
Tape::Track* Tape::open (int index, juce::int64 from)
{
    if (! juce::isPositiveAndBelow (index, size()))
        return nullptr;

    auto* reader = formats.createReaderFor (playlist[(size_t) index]);
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        delete reader;
        return nullptr;
    }

    auto t = std::make_unique<Track> (readAhead);
    const double ratio = reader->sampleRate / rate;
    t->index = index;
    t->length = (juce::int64) ((double) reader->lengthInSamples / ratio);
    t->produced = t->played = t->playedShown = juce::jlimit<juce::int64> (0, t->length, from);
    t->reader = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    t->reader->setNextReadPosition ((juce::int64) ((double) t->produced * ratio));
    t->resampler = std::make_unique<juce::ResamplingAudioSource> (t->reader.get(), false, 2);
    t->resampler->setResamplingRatio (ratio);
    t->resampler->prepareToPlay (chunk, rate);
    const int capacity = (int) (rate * ringSeconds);
    t->ring.setTotalSize (capacity);
    t->ringData.setSize (2, capacity);

    // A little read now, so it starts at once; the read-ahead thread does the rest.
    while (! t->finished.load() && t->produced - t->played < (juce::int64) (rate * primeSeconds))
        t->useTimeSlice();
    readAhead.addTimeSliceClient (t.get());

    tracks.push_back (std::move (t));
    return tracks.back().get();
}

void Tape::send (Message m)
{
    int start1, size1, start2, size2;
    fifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0)      queue[(size_t) start1] = m;
    else if (size2 > 0) queue[(size_t) start2] = m;
    fifo.finishedWrite (size1 + size2);

    if (size1 + size2 == 0 && m.track != nullptr)   // the queue's full: the audio thread never saw it
        tracks.erase (std::remove_if (tracks.begin(), tracks.end(), [&m] (auto& t) { return t.get() == m.track; }), tracks.end());
}

void Tape::queueFollowing()
{
    // Sent even when nothing follows (or it can't be read), so a track queued before (repeat
    // changed, the next one removed) is dropped rather than played.
    nextIndex = following (currentIndex);
    send ({ Command::queue, open (nextIndex) });
}

void Tape::play (int index)
{
    if (auto* t = open (index))
    {
        currentIndex = index;
        send ({ Command::start, t });
        queueFollowing();
    }
}

void Tape::togglePlay()
{
    if (playing.load())
        send ({ Command::pause });
    else if (nowShown.load() != nullptr && currentIndex >= 0 && position() >= length())
        play (currentIndex);   // at its end: from the start
    else if (nowShown.load() != nullptr)
        send ({ Command::resume });
    else if (! playlist.empty())
        play (currentIndex >= 0 ? currentIndex : order.front());
}

// Back to the start of the track: that's the track opened again, loaded without playing.
void Tape::stop()
{
    send ({ Command::stop });
    if (nowShown.load() != nullptr)
        if (auto* t = open (currentIndex))
        {
            send ({ Command::load, t });
            queueFollowing();
        }
}

void Tape::next()                    { if (const int n = following (currentIndex, true); n >= 0) play (n); }
void Tape::skip (double seconds)     { seek (position() + seconds); }

// The track opened again at that place: playing on from there, or waiting there if paused. The
// track after it hasn't changed, so the one already queued stays (no second open of it here).
void Tape::seek (double seconds)
{
    if (nowShown.load() == nullptr || currentIndex < 0)
        return;
    if (auto* t = open (currentIndex, (juce::int64) (juce::jlimit (0.0, length(), seconds) * rate)))
        send ({ playing.load() ? Command::start : Command::load, t, true });
}

void Tape::previous()
{
    if (position() > 3.0)
        seek (0.0);
    else if (const int p = preceding (currentIndex); p >= 0)
        play (p);
}

double Tape::position() const
{
    auto* t = nowShown.load();
    return t != nullptr ? (double) t->playedShown.load() / rate : 0.0;
}

double Tape::length() const
{
    auto* t = nowShown.load();
    return t != nullptr ? (double) t->length / rate : 0.0;
}

void Tape::update (double engineRate)
{
    rate = engineRate;

    // The audio thread took over the queued track: it's now current; queue the one after.
    if (const int c = changes.load(); c != changesSeen)
    {
        changesSeen = c;
        if (auto* t = nowShown.load())
            currentIndex = t->index;
        queueFollowing();
    }

    // Free the tracks the audio thread has handed back (it holds no pointer to them any more).
    int start1, size1, start2, size2;
    returns.prepareToRead (returns.getNumReady(), start1, size1, start2, size2);
    for (int i = 0; i < size1 + size2; ++i)
    {
        auto* gone = returned[(size_t) (i < size1 ? start1 + i : start2 + i - size1)];
        tracks.erase (std::remove_if (tracks.begin(), tracks.end(), [gone] (auto& t) { return t.get() == gone; }), tracks.end());
    }
    returns.finishedRead (size1 + size2);
}

//==============================================================================
void Tape::letGo (Track* t)
{
    if (t == nullptr || t == now || t == upNext || t == pendingStart || t == pendingNext)
        return;
    if (letGoCount < (int) letGoNow.size())   // (more than a block ever lets go: that one is kept
        letGoNow[(size_t) letGoCount++] = t;  //  until Patchbay closes rather than freed too early)
}

// After nowShown has moved on, so the message thread never frees what it's still showing.
void Tape::handBack()
{
    for (int i = 0; i < letGoCount; ++i)
    {
        int start1, size1, start2, size2;
        returns.prepareToWrite (1, start1, size1, start2, size2);
        if (size1 > 0)      returned[(size_t) start1] = letGoNow[(size_t) i];
        else if (size2 > 0) returned[(size_t) start2] = letGoNow[(size_t) i];
        returns.finishedWrite (size1 + size2);   // (full: kept until Patchbay closes, never freed early)
    }
    letGoCount = 0;
}

// Anything still waiting for a fade is let go too: this switch overrides it, and a stale
// pendingStart would otherwise take the next queued track or jump in at a later fade.
void Tape::switchTo (Track* t, bool play, bool keepNext)
{
    Track* held[] = { now, keepNext ? nullptr : upNext, pendingStart, pendingNext };
    now = t;
    if (! keepNext)
        upNext = nullptr;
    pendingStart = pendingNext = nullptr;
    pendingKeepsNext = false;
    for (auto* old : held)
        letGo (old);
    fade = 0.0f;
    fadeTarget = 1.0f;
    stopWhenSilent = pauseWhenSilent = ejectWhenSilent = false;
    playing = play;
}

void Tape::eject()
{
    Track* held[] = { now, upNext, pendingStart, pendingNext };
    now = upNext = pendingStart = pendingNext = nullptr;
    for (auto* t : held)
        letGo (t);
    stopWhenSilent = pauseWhenSilent = ejectWhenSilent = false;
    playing = false;
}

void Tape::takeCommands (bool rendering)
{
    int start1, size1, start2, size2;
    fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);
    for (int i = 0; i < size1 + size2; ++i)
    {
        const auto& m = queue[(size_t) (i < size1 ? start1 + i : start2 + i - size1)];
        const bool audible = rendering && playing.load() && now != nullptr && fade > 0.0f;

        if (! rendering && (m.command == Command::start || m.command == Command::load || m.command == Command::queue))
        {
            letGo (m.track);   // nowhere to play it: dropped
            continue;
        }

        switch (m.command)
        {
            case Command::start:
            case Command::load:
            {
                // A start arriving while a pause fades out (a seek sent before the message thread
                // saw the pause) only loads: the pause was pressed, so it stays paused.
                const bool plays = m.command == Command::start && ! pauseWhenSilent;
                if (audible)
                {
                    auto* old = pendingStart;
                    if (! m.keepNext)
                        pendingKeepsNext = false;
                    else if (old == nullptr)
                        pendingKeepsNext = true;   // (after another start, its pendingNext follows this too)
                    pendingStart = m.track;
                    pendingPlays = plays;
                    fadeTarget = 0.0f;
                    letGo (old);
                }
                else
                    switchTo (m.track, plays, m.keepNext);
                break;
            }
            case Command::queue:
            {
                auto& slot = pendingStart != nullptr ? pendingNext : upNext;
                auto* old = slot;
                slot = m.track;
                if (pendingStart != nullptr)
                    pendingKeepsNext = false;
                letGo (old);
                break;
            }
            case Command::pause:
                if (audible) { fadeTarget = 0.0f; pauseWhenSilent = true; pendingPlays = false; } else playing = false;
                break;
            case Command::resume:
                // While a start or eject waits for the fade, the fade carries on (else it would
                // never act); a waiting start then plays.
                if (rendering && now != nullptr)
                {
                    pauseWhenSilent = stopWhenSilent = false;
                    pendingPlays = true;
                    if (pendingStart == nullptr && ! ejectWhenSilent)
                        fadeTarget = 1.0f;
                    playing = true;
                }
                break;
            case Command::stop:   if (audible) { fadeTarget = 0.0f; stopWhenSilent = true; pauseWhenSilent = false; } else playing = false; break;
            case Command::eject:  if (audible) { fadeTarget = 0.0f; ejectWhenSilent = true; } else eject(); break;
        }
    }
    fifo.finishedRead (size1 + size2);
}

void Tape::drain()
{
    takeCommands (false);
    nowShown = now;
    handBack();
}

void Tape::render (juce::AudioBuffer<float>& out, int numSamples, double engineRate)
{
    takeCommands (true);
    out.clear (0, numSamples);

    bool movedOn = false;
    if (playing.load() && now != nullptr)
    {
        // Copy from the current track; at its end, carry straight on with the queued one. A track
        // the disk hasn't kept up with leaves the rest of the block silent (and plays on after).
        int done = 0;
        while (done < numSamples && now != nullptr)
        {
            const int want = (int) juce::jmin ((juce::int64) (numSamples - done), now->length - now->played);
            const int got = want > 0 ? now->read (out, done, want) : 0;
            done += got;

            if (now->ended())
            {
                if (upNext != nullptr)
                {
                    auto* old = now;
                    now = upNext;
                    upNext = nullptr;
                    letGo (old);
                    movedOn = true;
                }
                else
                {
                    // The end of the playlist. Something waiting for silence (a start, stop, seek
                    // or eject pressed in the last moments) still fades out and acts.
                    if (pendingStart == nullptr && ! stopWhenSilent && ! pauseWhenSilent && ! ejectWhenSilent)
                        playing = false;
                    break;
                }
            }
            else if (got < want)
                break;   // ran dry
        }

        // Level and fades.
        const float gain = level.load();
        const float step = (float) (1.0 / (fadeSeconds * engineRate));
        for (int i = 0; i < numSamples; ++i)
        {
            fade = fadeTarget > fade ? juce::jmin (fadeTarget, fade + step) : juce::jmax (fadeTarget, fade - step);
            const float g = gain * fade;
            out.getWritePointer (0)[i] *= g;
            out.getWritePointer (1)[i] *= g;
        }

        // Faded out: do what was waiting for silence.
        if (fade <= 0.0f)
        {
            if (ejectWhenSilent)
                eject();
            else if (pendingStart != nullptr)
            {
                auto* t = pendingStart;
                auto* following = pendingNext;
                const bool keepNext = pendingKeepsNext;
                pendingStart = pendingNext = nullptr;
                pendingKeepsNext = false;
                switchTo (t, pendingPlays, keepNext);
                if (! keepNext)
                    upNext = following;
            }
            else if (stopWhenSilent)  { playing = false; stopWhenSilent = false; }
            else if (pauseWhenSilent) { playing = false; pauseWhenSilent = false; }
        }
    }

    if (now != nullptr)
        now->playedShown = now->played;
    nowShown = now;
    if (movedOn)
        ++changes;   // after nowShown, so update() reads the new track's index
    handBack();
}

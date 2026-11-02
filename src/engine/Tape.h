#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// The tape deck's player: a playlist of music files (WAV, FLAC, AIFF, OGG, MP3) streamed from disk,
// into the TAPE input strip.
//
// Each track is read, decoded and converted to the engine's rate on a background thread into a
// ring of its own, so the audio thread only copies from memory and never waits on the disk (a
// slow drive makes the tape run dry, never the desk). A track always plays from where it was
// opened: a seek opens the file again at the new place. The next track is opened before the
// current one ends and taken over in the same block, so albums play without gaps. Pause, stop and
// skips fade (30 ms) rather than cut. Transport commands reach the audio thread through a lock-free
// queue, and tracks it has let go come back through another, to be freed on the message thread.
class Tape
{
public:
    enum class Repeat { off, one, all };

    Tape();
    ~Tape();

    // Message thread: the playlist.
    void add (const juce::File&);              // a sound file, or every sound file in a folder (sorted)
    void remove (int index);
    void clearPlaylist();
    void move (int from, int to);
    int size() const { return (int) playlist.size(); }
    const juce::File& file (int index) const { return playlist[(size_t) index]; }
    static bool isSoundFile (const juce::File&);

    // Message thread: the transport.
    void play (int index);                     // starts that track
    void cue (int index)        { currentIndex = juce::isPositiveAndBelow (index, size()) ? index : -1; }   // the track PLAY starts
    void togglePlay();                         // play / pause
    void stop();                               // fades out, back to the start of the track
    void next();
    void previous();                           // back to this track's start, or the one before if near it
    void seek (double seconds);
    void skip (double seconds);                // wind forward/back within the track
    void setLevel (float gain)  { level = gain; }
    float getLevel() const      { return level.load(); }
    void setShuffle (bool on);
    bool getShuffle() const     { return shuffle; }
    Repeat repeat = Repeat::all;

    // Message thread, a few times a second: notices the audio thread moving on to the next track
    // and opens the one after it; frees tracks the audio thread has handed back.
    void update (double engineRate);

    // What's playing (for the deck's display).
    int current() const        { return currentIndex; }
    bool isPlaying() const     { return playing.load(); }
    bool isLoaded() const      { return nowShown.load() != nullptr; }   // a track in the deck (playing or paused)
    double position() const;   // seconds
    double length() const;     // seconds

    // Audio thread: the deck's output, two channels, overwritten.
    void render (juce::AudioBuffer<float>& out, int numSamples, double engineRate);

    // Audio thread, a block with no TAPE channel to render into: the commands are taken anyway
    // (stop, pause and eject act; a start or queue is dropped), so nothing piles up or leaks.
    void drain();

private:
    struct Track;
    enum class Command { start, load, queue, pause, resume, stop, eject };
    // keepNext (a seek: the same track somewhere else): whatever is queued after it stays queued.
    struct Message { Command command; Track* track = nullptr; bool keepNext = false; };

    Track* open (int index, juce::int64 from = 0);   // kept in `tracks`; nullptr if it can't be read
    void send (Message);
    int following (int index, bool skipping = false) const;   // the track after `index` (shuffle, repeat), or -1
    int preceding (int index) const;
    void queueFollowing();                     // opens the next track and hands it to the audio thread
    void reorder();                            // the play order, after the playlist or shuffle changes
    void renumber (const std::function<int (int)>& newIndex);   // opened tracks' playlist places

    // Audio thread.
    void takeCommands (bool rendering);
    void switchTo (Track*, bool play, bool keepNext = false);
    void eject();                              // lets go of every track
    void letGo (Track*);                       // not used here any more: handed back (once nothing points at it)
    void handBack();                           // the let-go tracks, to the message thread

    std::vector<juce::File> playlist;
    std::vector<int> order;                    // play order (shuffled or 0..n-1)
    bool shuffle = false;
    juce::AudioFormatManager formats;
    juce::TimeSliceThread readAhead { "Tape read-ahead" };

    // Owned here, used by the audio thread; freed by update() once the audio thread hands them back.
    std::vector<std::unique_ptr<Track>> tracks;
    juce::AbstractFifo fifo { 64 };
    std::array<Message, 64> queue;
    // A block lets go of at most the four it holds plus one per command, so these never fill in
    // one block (and the message thread empties `returns` several times a second).
    juce::AbstractFifo returns { 4 * 64 };
    std::array<Track*, 4 * 64> returned {};
    std::array<Track*, 2 * 64 + 4> letGoNow {};   // audio thread: let go this block, handed back at its end
    int letGoCount = 0;

    // Audio-thread state. A start, stop, pause or load while playing first fades out, then acts.
    Track* now = nullptr;
    Track* upNext = nullptr;
    Track* pendingStart = nullptr;
    Track* pendingNext = nullptr;              // what follows pendingStart (queued while the old track fades out)
    bool pendingPlays = true;                  // pendingStart: started (true) or only loaded (a seek while paused)
    bool pendingKeepsNext = false;             // pendingStart is a seek: upNext stays (pendingNext isn't coming)
    float fade = 0.0f, fadeTarget = 0.0f;      // 0..1
    bool stopWhenSilent = false, pauseWhenSilent = false, ejectWhenSilent = false;

    // Shared.
    std::atomic<bool> playing { false };
    std::atomic<float> level { 1.0f };
    std::atomic<Track*> nowShown { nullptr };
    std::atomic<int> changes { 0 };            // bumped when the audio thread moves to the next track
    int changesSeen = 0;
    int currentIndex = -1, nextIndex = -1;
    double rate = 48000.0;
};

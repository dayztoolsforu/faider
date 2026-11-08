#pragma once
#include "../ProductName.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>

// The tape deck's recorder: one output, bus or input recorded to a stereo file (Format).
//
// The audio thread only hands each block over (AudioFormatWriter::ThreadedWriter's lock-free
// FIFO); a background thread writes the file, so recording never causes dropouts. A block where
// the source isn't processed (it's switched off) is recorded as silence, so time keeps running.
class Recorder
{
public:
    enum class Kind { output, bus, input };   // what's recorded: AudioEngine's masters, buses or strips

    Recorder();
    ~Recorder();

    // Message thread. start() makes the folder `into` if needed and returns the new file, or an empty File
    // (with `error` set) if it can't write there. `lock`: the engine's graph lock, held while the
    // audio thread's writer is swapped in or out.
    juce::File start (Kind, int index, const juce::String& sourceName, const juce::File& into, double sampleRate,
                      juce::SpinLock& lock, juce::String& error);
    void stop (juce::SpinLock& lock);   // either kind of recording
    bool isRecording() const       { return writer.load() != nullptr || multitracking.load(); }

    // Multitrack: several sources at once, each to its own file ("IN MIC FOCUS.wav") in a new
    // folder in `into` ("Faider MULTITRACK 2026-10-25 01-23-45"), all starting on the same
    // sample, so they line up in an editor. Message thread; returns the folder (empty, with
    // `error`, if it can't). currentFile() is the folder while it runs; stop() ends it.
    struct TrackSource { Kind kind; int index; juce::String name; };
    juce::File startMultitrack (const std::vector<TrackSource>&, const juce::File& into, double sampleRate,
                                juce::SpinLock& lock, juce::String& error);
    bool isMultitracking() const   { return multitracking.load(); }
    int trackCount() const         { return (int) tracks.size(); }
    double seconds() const         { return rate > 0.0 ? (double) written.load() / rate : 0.0; }
    int droppedBlocks() const      { return dropped.load(); }
    juce::File currentFile() const { return file; }

    // A file name for `sourceName` now, without its extension: "Faider OUT MUSIC 2026-10-24 01-23-45".
    static juce::String fileNameFor (const juce::String& sourceName);

    // File formats: WAV (16, 24, 32-bit, 32-bit float; past 4 GB it becomes RF64), FLAC (16, 24),
    // AIFF (16, 24; 4 GB at most), Ogg Vorbis 128-320 kbps (all written directly), and with FFmpeg
    // (picked in SETTINGS, or on the PATH) MP3 128-320 kbps, AAC, Opus and ALAC (recorded as a 24-bit
    // WAV, then converted by FFmpeg in the background). `format` is the TAPE tab's choice (saved with the session).
    struct Format
    {
        juce::String id, name, extension;
        juce::String codec;      // FFmpeg's encoder and settings ("-c:a libopus -b:a 160k"); empty: written directly
        int bits = 24;           // PCM and lossless: bits per sample
        int kbps = 0;            // lossy: the bitrate
        float packed = 1.0f;     // lossless: about what's left of the PCM size (estimates only)
        double maxBytes = 0.0;   // the format's own limit on a file's size; 0: none
    };
    static std::vector<Format> formats();
    static Format formatFor (const juce::String& id);
    static juce::File ffmpeg();   // where FFmpeg is (the one picked, else the PATH's), or a missing File
    static juce::File ffmpegOnPath();
    static void setFfmpeg (const juce::File& exe);   // the one picked in SETTINGS (none: the PATH's)
    juce::String format { "wav24" };

    // Disk space (message thread). A recording stops, its file finished, when less than
    // diskReserve is left on the drive or a file nears its format's limit.
    static constexpr juce::int64 diskReserve = 1024LL * 1024 * 1024;
    juce::int64 freeSpace() const;   // on the recordings folder's drive
    // Bytes a second on disk while recording one source (FFmpeg formats: their 24-bit WAV).
    static double diskBytesPerSecond (const Format&, double sampleRate);
    // How long `tracks` sources at once can record in `formatId` before the reserve (or the
    // format's limit) is reached; while recording, the time left.
    double secondsThatFit (const juce::String& formatId, double sampleRate, int tracks) const;
    // Once a second: stops a recording that has run out of room. Returns what happened (for
    // the event log), or nothing.
    juce::String stopIfFull (juce::SpinLock& lock);

    // Message thread: the TAPE tab's choices, saved with the session. `chosen` names the source
    // ("output:OUT MUSIC", "bus:SUMMING", "input:MIC FOCUS"); empty for the first output.
    juce::File folder { juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (juce::String (Product::name) + " Recordings") };
    juce::String chosen;

    // Instant replay: the last `seconds` of a source always held in memory; saveReplay() writes
    // them to a WAV in `folder` on a background thread. Message thread. seconds <= 0: off (the
    // memory is freed). Arming the same source and length again keeps what's buffered.
    void armReplay (Kind, int index, double seconds, double sampleRate, juce::SpinLock& lock);
    bool replayArmed() const      { return replaySource.load() >= 0; }
    double replayBuffered() const { return replayRate > 0.0 ? (double) juce::jmin<juce::int64> (ringWritten.load(), ring.getNumSamples()) / replayRate : 0.0; }
    juce::File saveReplay (const juce::String& sourceName, juce::String& error);
    juce::File saveReplayNow (const juce::String& name, juce::String& error);   // 24-bit WAV, written before it returns (a pad loads it)
    double replayLength = 0.0;    // the TAPE tab's choice (seconds, 0: off), saved with the session

    // Strips, buses or masters of `k` moved or removed: what's recorded (and the replay) follows its
    // source to `newIndex (old)`; -1 (removed) records silence from now on. Call with the engine's
    // graph lock held, in the same hold as the change. Returns true if the single recording's
    // source went: the engine stops it once the lock is released.
    bool remapLocked (Kind k, const std::function<int (int)>& newIndex) noexcept;

    // Audio thread, each block: beginBlock(), then offer() every processed buffer (only the one
    // being recorded is kept), then endBlock().
    void beginBlock() noexcept;
    void offer (Kind, int index, const juce::AudioBuffer<float>&, int numSamples) noexcept;
    void endBlock (int numSamples) noexcept;

private:
    // A writer for `format` on `stream` (null, with `error`, if it can't).
    static std::unique_ptr<juce::AudioFormatWriter> createWriter (const juce::String& format, std::unique_ptr<juce::OutputStream>& stream,
                                                                  double sampleRate, juce::String& error);
    // Background; deletes the WAV after (kept if the conversion fails).
    static void convertWithFFmpeg (const juce::File& wav, const juce::File& target, const juce::String& codec);
    juce::File firstFile() const;   // the file being written (a multitrack's first track)

    juce::File convertTo;   // FFmpeg formats: the file the WAV being recorded becomes when it stops
    juce::String convertCodec;

    juce::TimeSliceThread thread { "Recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> owned;   // message thread
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> writer { nullptr };
    std::atomic<int> kind { 0 }, source { -1 };
    std::atomic<juce::int64> written { 0 };
    std::atomic<int> dropped { 0 };
    juce::AudioBuffer<float> silence;
    bool offered = false;   // audio thread
    double rate = 0.0;
    juce::File file;

    // Multitrack: one writer per source; swapped in and out under the lock (the audio thread
    // reads it inside the engine's graph lock).
    struct Track
    {
        Kind kind;
        int index;
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
        juce::File file, convertTo;
        bool offered = false;   // audio thread
    };
    std::vector<std::unique_ptr<Track>> tracks;
    std::atomic<bool> multitracking { false };
    void stopMultitrack (juce::SpinLock& lock);

    void writeRing (const float* const* data, int numSamples) noexcept;
    std::shared_ptr<juce::AudioBuffer<float>> copyReplay (juce::String& error) const;   // what's buffered, oldest first
    juce::AudioBuffer<float> ring;                   // replay: audio thread writes (swapped in under the lock)
    std::atomic<int> replayKind { 0 }, replaySource { -1 };
    std::atomic<juce::int64> ringWritten { 0 };
    bool replayOffered = false;                      // audio thread
    double replayRate = 0.0;
};

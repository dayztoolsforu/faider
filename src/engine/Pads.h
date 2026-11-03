#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// The button box: banks of pads that each play a sound file (WAV, FLAC, AIFF, OGG, MP3) into the
// PADS input strip, which routes it like any other input.
//
//   One-shot   plays to the end; pressing again restarts it
//   Hold       plays while held, fades out on release
//   Loop       first press starts it looping, the next stops it
//
// Pads in the same choke group cut each other off; Stop all fades everything out. Sounds are
// decoded once into memory at the engine's rate. Presses reach the audio thread through a
// lock-free queue, so the callback never locks or allocates.
class Pads
{
public:
    static constexpr int banks = 4, perBank = 16, count = banks * perBank, voices = 32;
    enum class Mode { oneShot, hold, loop };   // saved as numbers: append only

    struct Sound
    {
        juce::AudioBuffer<float> audio;   // stereo
        double rate = 48000.0;
    };

    struct Pad
    {
        juce::String name, file;
        juce::Colour colour;              // transparent: the theme's
        float gain = 1.0f;
        Mode mode = Mode::oneShot;
        int choke = 0;                    // 0: none; pads in the same group cut each other off
        juce::String hotkey;              // a global hotkey (juce::KeyPress text), or empty
        juce::String macro;               // a macro the pad runs too (by name), or empty
        std::shared_ptr<const Sound> sound;   // what plays: `full` trimmed and faded (applyEdits)

        // The editor's trim and fades, in seconds of the file: the part that plays (end 0: to the
        // end), faded in at its start and out at its end. A new file starts them at 0.
        double trimStart = 0.0, trimEnd = 0.0, fadeIn = 0.0, fadeOut = 0.0;
        std::shared_ptr<const Sound> full;    // the whole file as decoded

        double seconds() const { return sound != nullptr ? sound->audio.getNumSamples() / sound->rate : 0.0; }
        double fullSeconds() const { return full != nullptr ? full->audio.getNumSamples() / full->rate : 0.0; }
    };

    Pads();

    // Message thread.
    Pad& pad (int index) { return pads[(size_t) index]; }
    const Pad& pad (int index) const { return pads[(size_t) index]; }
    bool load (int index, const juce::File&, double rate, juce::String& error);   // decodes it at `rate`
    void applyEdits (int index);   // the pad's trim and fades changed: what plays is made again (it stops if playing)
    void clear (int index);
    void swap (int a, int b);   // two pads trade places (sounds and settings)
    void press (int index);
    void release (int index);
    void stop (int index);   // fades out whatever that pad is playing, in any mode
    void stopAll();
    bool isPlaying (int index) const { return playing[(size_t) index].load() > 0; }
    float progress (int index) const { return position[(size_t) index].load(); }   // 0..1, its newest voice

    // Audio thread: the playing pads, mixed into `out` (two channels, overwritten).
    void render (juce::AudioBuffer<float>& out, int numSamples, double rate);

    // Audio thread, a block nothing rendered the pads (no PADS channel on): the commands waiting
    // are taken anyway, so the queue never fills; presses are dropped (nothing would hear them,
    // and they mustn't all fire later), stops and releases still apply.
    void drain (double rate);

private:
    enum class Command { press, release, stop, stopAll };
    struct Message
    {
        Command command = Command::press;
        int pad = -1;
        const Sound* sound = nullptr;
        float gain = 1.0f;
        Mode mode = Mode::oneShot;
        int choke = 0;
        std::uint64_t number = 0;   // in sending order (the audio thread says how far it has got)
    };

    struct Voice
    {
        const Sound* sound = nullptr;   // nullptr: free
        int pad = -1, choke = 0;
        double pos = 0.0;
        float gain = 1.0f;
        bool loop = false;
        int fade = -1, fadeLength = 1;  // fading out: samples left (-1: not fading)
        std::uint64_t started = 0;
    };

    std::uint64_t send (Message);   // its number, or 0 if the queue was full (dropped)
    void handle (const Message&, double rate);
    void take (double rate, bool presses);   // audio thread: the commands waiting
    static void fadeOut (Voice&, double rate, double seconds);
    void retire (int index);

    std::array<Pad, count> pads;
    juce::AbstractFifo fifo { 256 };
    std::array<Message, 256> queue;
    std::array<Voice, voices> voice;
    std::uint64_t starts = 0;
    std::array<std::atomic<int>, count> playing {};
    std::array<std::atomic<float>, count> position {};
    // Replaced sounds, each with the number of its pad's stop: freed once the audio thread has
    // taken that stop (no voice can be playing it then, and no press waiting names it). A stop
    // that couldn't be sent (the queue full) keeps its sound until Faider closes.
    std::vector<std::pair<std::uint64_t, std::shared_ptr<const Sound>>> retired;
    std::uint64_t sent = 0;                 // message thread: the last command's number
    std::atomic<std::uint64_t> taken { 0 };   // audio thread: the last command it has taken
    juce::AudioFormatManager formats;
};

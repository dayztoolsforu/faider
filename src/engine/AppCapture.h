#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cstdint>
#include <memory>
#include "DriftFifo.h"

// Captures one app's audio (and its child processes') using Windows process loopback
// (Windows 10 2004+). No driver needed: the app keeps playing to its own device and Patchbay
// receives a copy of exactly that app's sound. Runs its own capture thread feeding `fifo`.
class AppCapture
{
public:
    static std::unique_ptr<AppCapture> open (uint32_t processId, double interfaceRate, int interfaceBlock, juce::String& error);
    ~AppCapture();

    uint32_t getProcessId() const { return pid; }

    DriftFifo fifo;   // device (capture thread) pushes, master pulls

    // Audio thread: the block read from the fifo once (`pulledAt`: which block), so every strip
    // capturing this app gets the same audio rather than a share of it.
    juce::AudioBuffer<float> inBlock { 2, 8192 };   // (the engine's largest block)
    std::uint64_t pulledAt = 0;

private:
    AppCapture() = default;

    struct Impl;
    std::unique_ptr<Impl> impl;
    uint32_t pid = 0;
};

struct AudioApp
{
    uint32_t processId = 0;
    juce::String exe;   // e.g. "Spotify.exe"
};

// Apps that currently have an audio session on any active playback device.
juce::Array<AudioApp> listAudioApps();

// The process to capture for an exe name: one that's playing audio if any, else the top-level
// running copy (its children are captured with it), or 0 if it isn't running.
uint32_t findAppProcess (const juce::String& exe);

// Cheap (no process list): whether this process is still running.
bool processIsRunning (uint32_t processId);
uint32_t thisProcessId();   // Patchbay's own

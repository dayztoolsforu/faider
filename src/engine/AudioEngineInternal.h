#pragma once

// For AudioEngine's own .cpp files (AudioEngine.cpp, AudioEngineProcess.cpp, AudioEngineDevices.cpp,
// AudioEngineGraph.cpp, AudioEngineRack.cpp): the sizes they share and the two structs AudioEngine.h
// only names.

#include "AudioEngine.h"

namespace
{
    constexpr int pluginMaxChannels = 16;
    constexpr int maxCompensation   = 48000;   // 1 s at 48 kHz
    constexpr int maxBlock          = 8192;    // every per-block buffer is sized for this
}

//==============================================================================
// A rack's patch, compiled (compileRack) for the audio thread: its units in an order where each
// comes after everything feeding it, each feed delayed so parallel paths arrive together.
struct AudioEngine::RackPlan
{
    struct Feed
    {
        int from = -1;     // a node's index, or -1: the channel's input
        int delay = 0;     // samples, to line up with the slowest feed into the same place
        int line = -1;     // its delay line (in `lines`), or -1
    };
    struct Node
    {
        int builtin = -1;                              // a RackBuiltin, or
        juce::AudioPluginInstance* plugin = nullptr;   // a plugin
        std::vector<Feed> feeds;
    };
    struct Line { juce::AudioBuffer<float> ring; int pos = 0; };

    std::vector<Node> nodes;
    std::vector<Feed> out;                            // into the fader
    std::vector<juce::AudioBuffer<float>> buffers;    // one per node
    std::vector<Line> lines;
    juce::AudioBuffer<float> scratch, result;
    int latency = 0;                                  // the channel's input to the fader
    unsigned before = 0, after = 0;                   // the strip's own stages (not in the rack): before it, and after it
};

struct AudioEngine::Secondary : public juce::AudioIODeviceCallback
{
    juce::String name;
    bool isOutput = true;
    std::unique_ptr<juce::AudioIODevice> device;
    DriftFifo fifo;   // output: master pushes, device pulls. input: device pushes, master pulls.
    int channels = 2;  // all the device has (a 16-channel cable: 16), each output or input using a pair of them

    // Output, audio thread: every output bus playing here is summed into `mix` on its own
    // channels, which is pushed to the fifo once per block (several outputs can share one device).
    juce::AudioBuffer<float> mix { 2, maxBlock };
    bool mixPending = false;

    // Input, audio thread: the block read from the fifo once (`pulledAt`: which block), for
    // every strip taking its own channels from it.
    juce::AudioBuffer<float> inBlock { 2, maxBlock };
    std::uint64_t pulledAt = 0;

    ~Secondary() override
    {
        if (device != nullptr)
            device->close();
    }

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override
    {
        if (isOutput)
            fifo.pull (outputs, numOutputs, numSamples);
        else
            fifo.push (inputs, numInputs, numSamples);
    }

    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
};

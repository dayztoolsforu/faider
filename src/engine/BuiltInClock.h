#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

// The master when no device can be: a timed thread that asks for a block every
// blockSize / sampleRate seconds, with no channels of its own. Every device then follows it
// through its DriftFifo, as they follow an interface. The timing is absolute (from the
// high-resolution counter), so the rate holds on average; a block's wake-up jitter is the FIFOs'.
class BuiltInClock final : public juce::AudioIODevice,
                           private juce::Thread
{
public:
    static inline const juce::String deviceName { "Built-in clock" };

    BuiltInClock();
    ~BuiltInClock() override;

    juce::StringArray getOutputChannelNames() override { return {}; }
    juce::StringArray getInputChannelNames() override  { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { 44100.0, 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override    { return { 128, 256, 480, 512, 1024 }; }
    int getDefaultBufferSize() override                    { return 256; }

    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double sampleRate, int bufferSizeSamples) override;
    void close() override;
    bool isOpen() override { return opened; }

    void start (juce::AudioIODeviceCallback*) override;
    void stop() override;
    bool isPlaying() override { return callback != nullptr; }

    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return block; }
    double getCurrentSampleRate() override     { return rate; }
    int getCurrentBitDepth() override          { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return {}; }
    juce::BigInteger getActiveInputChannels() const override  { return {}; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override  { return 0; }
    int getXRunCount() const noexcept override { return lateBlocks.load(); }

private:
    void run() override;

    bool opened = false;
    double rate = 48000.0;
    int block = 256;
    juce::AudioIODeviceCallback* callback = nullptr;
    juce::CriticalSection callbackLock;
    std::atomic<int> lateBlocks { 0 };
};

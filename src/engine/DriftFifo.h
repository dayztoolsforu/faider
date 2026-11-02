#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

// Carries audio between two devices that run on different clocks.
//
// The producer pushes samples at its own rate. The consumer pulls blocks at its rate and
// resamples on the way out. The resampling ratio is nominal (producerRate / consumerRate)
// plus a small correction from a PI controller that holds the FIFO fill level at a target,
// which absorbs clock drift without clicks. One producer thread, one consumer thread.
class DriftFifo
{
public:
    void prepare (int numChannels, double producerRate, double consumerRate, int producerBlock, int consumerBlock);

    // Producer thread. Drops samples (counted as overruns) if the FIFO is full.
    void push (const float* const* data, int numChannels, int numSamples);

    // Consumer thread. Writes silence (counted as an underrun) until enough audio is buffered.
    void pull (float* const* data, int numChannels, int numSamples);

    int    getFill() const         { return fifo != nullptr ? fifo->getNumReady() : 0; }
    int    getTarget() const       { return target; }
    double getCorrectionPpm() const { return correctionPpm.load(); }
    int    getUnderruns() const    { return underruns.load(); }
    int    getOverruns() const     { return overruns.load(); }
    int    getResyncs() const      { return resyncs.load(); }
    void   resetCounters()         { underruns = 0; overruns = 0; resyncs = 0; }   // for diagnostics

private:
    std::unique_ptr<juce::AbstractFifo> fifo;
    juce::AudioBuffer<float> ring, scratch, discard;
    std::vector<juce::LagrangeInterpolator> interpolators;

    double nominalRatio = 1.0, consumerRate = 48000.0;
    std::atomic<int> target { 0 };   // set by the consumer (it grows for bursty devices, decays after a calm spell), read for diagnostics
    int capacity = 0, producerBlockSize = 0;
    int channels = 0;

    // Consumer-thread state
    bool primed = false;
    double smoothedFill = 0.0, integral = 0.0;
    int floorTarget = 0;                       // what the device's largest blocks need: the decay stops here
    double calmSeconds = 0.0, shrink = 0.0;    // time since the last underrun; the decay's fraction of a sample

    std::atomic<double> correctionPpm { 0.0 };
    std::atomic<int> underruns { 0 }, overruns { 0 }, resyncs { 0 };
};

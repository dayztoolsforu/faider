#include "DriftFifo.h"

namespace
{
    // PI controller on fill error (samples), tuned in seconds so it behaves the same for any
    // block size. Loop: d(error)/dt = -48000 * correction, critically damped at ~0.5 rad/s, so
    // a fill error settles in a few seconds and the integral ends up holding the real drift.
    // Real clock drift is ~10-200 ppm; the clamp is far above that but still inaudible.
    constexpr double proportionalGain = 2.0e-5;    // correction per sample of error
    constexpr double integralGain     = 5.0e-6;    // correction per sample-second of error
    constexpr double maxCorrection    = 0.005;     // 5000 ppm
    constexpr double fillSmoothingSec = 0.25;

    // A target grown by underruns comes back down after this long without one, at this share of
    // the consumed rate (500 ppm: the controller follows it with a small, inaudible correction).
    constexpr double decayAfterSec    = 30.0;
    constexpr double decayShare       = 0.0005;
}

void DriftFifo::prepare (int numChannels, double producerRate, double consumerRateHz, int producerBlock, int consumerBlock)
{
    channels = numChannels;
    consumerRate = consumerRateHz;
    nominalRatio = producerRate / consumerRate;

    // Enough to cover one block on each side plus jitter; the controller holds the fill here.
    // It grows (see pull) for devices that ask for bigger or burstier blocks than they announce,
    // and what hiccups added shrinks back after a calm spell.
    producerBlockSize = producerBlock;
    target = producerBlock + (int) std::ceil (consumerBlock * nominalRatio) + 64;

    // Devices may ask for blocks much larger than announced (WASAPI often does; VB-CABLE with
    // a large latency setting asks for up to 8192), so allow that. The target can grow to a
    // third of the ring, so the ring holds three of the largest such blocks' worth at least
    // (else at a high rate ratio the target could never cover one block).
    const int maxConsumerBlock = juce::jmax (consumerBlock * 4, 8192);
    capacity = std::max ({ 32768, target * 8, 3 * ((int) std::ceil (maxConsumerBlock * nominalRatio * (1.0 + maxCorrection)) + producerBlock + 64) });

    fifo = std::make_unique<juce::AbstractFifo> (capacity);
    ring.setSize (numChannels, capacity);
    ring.clear();
    scratch.setSize (numChannels, (int) std::ceil (maxConsumerBlock * nominalRatio * (1.0 + maxCorrection)) + 16);
    discard.setSize (1, maxConsumerBlock);

    interpolators = std::vector<juce::LagrangeInterpolator> ((size_t) numChannels);

    primed = false;
    smoothedFill = target;
    integral = 0.0;
    floorTarget = target;
    calmSeconds = shrink = 0.0;
    correctionPpm = 0.0;
    underruns = 0;
    overruns = 0;
    resyncs = 0;
}

void DriftFifo::push (const float* const* data, int numChannels, int numSamples)
{
    if (fifo == nullptr)
        return;

    if (fifo->getFreeSpace() < numSamples)
    {
        ++overruns;
        return;
    }

    int start1, size1, start2, size2;
    fifo->prepareToWrite (numSamples, start1, size1, start2, size2);

    for (int ch = 0; ch < channels; ++ch)
    {
        if (ch < numChannels && data[ch] != nullptr)
        {
            ring.copyFrom (ch, start1, data[ch], size1);
            if (size2 > 0)
                ring.copyFrom (ch, start2, data[ch] + size1, size2);
        }
        else
        {
            ring.clear (ch, start1, size1);
            if (size2 > 0)
                ring.clear (ch, start2, size2);
        }
    }

    fifo->finishedWrite (size1 + size2);
}

void DriftFifo::pull (float* const* data, int numChannels, int numSamples)
{
    auto silence = [&]
    {
        for (int ch = 0; ch < numChannels; ++ch)
            if (data[ch] != nullptr)
                juce::FloatVectorOperations::clear (data[ch], numSamples);
    };

    if (fifo == nullptr)
        return silence();

    int fill = fifo->getNumReady();

    // A bigger block than the target allows for: hold enough to cover blocks this size from now
    // on (for good: the decay below never goes under it).
    floorTarget = juce::jmax (floorTarget, juce::jmin (producerBlockSize + (int) std::ceil (numSamples * nominalRatio) + 64, capacity / 3));
    if (floorTarget > target)
        target = floorTarget;

    // Far too much buffered (e.g. a device that delivers a backlog when it opens): drop the
    // excess in one step instead of slowly speeding up, and restart the controller.
    if (fill > target * 3)
    {
        fifo->finishedRead (fill - target);
        fill = target;
        smoothedFill = target;
        integral = 0.0;
        ++resyncs;
    }

    if (! primed)
    {
        if (fill < target)
            return silence();

        primed = true;
        smoothedFill = fill;
    }

    const double dt = numSamples / consumerRate;
    smoothedFill += juce::jmin (1.0, dt / fillSmoothingSec) * (fill - smoothedFill);
    const double error = smoothedFill - target;
    integral = juce::jlimit (-maxCorrection, maxCorrection, integral + integralGain * error * dt);
    const double correction = juce::jlimit (-maxCorrection, maxCorrection, proportionalGain * error + integral);
    correctionPpm = correction * 1.0e6;

    // More buffered than the target -> consume slightly faster, and vice versa.
    const double ratio = nominalRatio * (1.0 + correction);
    const int needed = (int) std::ceil (numSamples * ratio) + 4;

    if (numSamples > discard.getNumSamples() || needed > scratch.getNumSamples())
    {
        ++underruns;   // block far larger than the device announced; shouldn't happen
        return silence();
    }

    if (fill < needed)
    {
        ++underruns;
        primed = false;   // rebuild the cushion rather than crackling on every block
        target = juce::jmin (target + numSamples, capacity / 3);   // and keep a bigger one: this device is burstier than allowed for
        if (calmSeconds > decayAfterSec)
            floorTarget = target;   // it ran dry while decaying: the decay went too far, so it stops here from now on
        calmSeconds = 0.0;
        return silence();
    }

    // Long enough without a hiccup (one burst of system load, not a bursty device): the cushion
    // grown for it shrinks back toward what the blocks need, so the latency doesn't stay up for good.
    calmSeconds += dt;
    if (calmSeconds > decayAfterSec && target > floorTarget)
    {
        shrink += numSamples * nominalRatio * decayShare;
        const int step = (int) shrink;
        shrink -= step;
        target = juce::jmax (floorTarget, target.load() - step);
    }

    int start1, size1, start2, size2;
    fifo->prepareToRead (needed, start1, size1, start2, size2);

    for (int ch = 0; ch < channels; ++ch)
    {
        scratch.copyFrom (ch, 0, ring, ch, start1, size1);
        if (size2 > 0)
            scratch.copyFrom (ch, size1, ring, ch, start2, size2);
    }

    // Every interpolator runs each block so all channels stay sample-aligned, even ones the
    // consumer isn't listening to right now.
    int consumed = 0;

    for (int ch = 0; ch < channels; ++ch)
    {
        auto* dest = (ch < numChannels && data[ch] != nullptr) ? data[ch] : discard.getWritePointer (0);
        consumed = interpolators[(size_t) ch].process (ratio, scratch.getReadPointer (ch), dest, numSamples);
    }

    for (int ch = channels; ch < numChannels; ++ch)
        if (data[ch] != nullptr)
            juce::FloatVectorOperations::clear (data[ch], numSamples);

    fifo->finishedRead (juce::jlimit (0, needed, consumed));
}

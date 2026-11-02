#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

// The last few seconds of a channel, always kept in memory: HEAR WHAT
// THEY HEAR plays it back to check it. Stereo, 16-bit (a minute at 48 kHz is about 11 MB). The audio thread
// writes; the message thread copies the newest part out. A copy racing the writer can only meet
// it at the oldest end, which is left alone (the newest is all that's ever wanted).
class RetroBuffer
{
public:
    // Message thread, before the channel plays (or with the audio stopped): `frames` long, or off (0).
    void prepare (int frames)
    {
        data.assign ((size_t) juce::jmax (0, frames) * 2, 0);
        size = juce::jmax (0, frames);
        write.store (0);
        filled.store (0);
        base = total.load();   // (frame `base` of the count is at index 0)
    }

    int capacity() const { return size; }

    // Every frame pushed since it was made: a reader that keeps its own count takes only what's new.
    juce::int64 written() const { return total.load (std::memory_order_acquire); }

    // Audio thread: a block as the channel sounds (two channels; a mono one is copied to both).
    void push (const juce::AudioBuffer<float>& block, int numSamples) noexcept
    {
        if (size == 0 || numSamples <= 0)
            return;
        const float* l = block.getReadPointer (0);
        const float* r = block.getReadPointer (block.getNumChannels() > 1 ? 1 : 0);
        int w = write.load (std::memory_order_relaxed);
        auto toShort = [] (float v) { return (std::int16_t) juce::roundToInt (juce::jlimit (-1.0f, 1.0f, v) * 32767.0f); };
        for (int i = 0; i < numSamples; ++i)
        {
            data[(size_t) w * 2] = toShort (l[i]);
            data[(size_t) w * 2 + 1] = toShort (r[i]);
            if (++w == size)
                w = 0;
        }
        write.store (w, std::memory_order_release);
        total.fetch_add (numSamples, std::memory_order_release);
        filled.store (juce::jmin (size, filled.load (std::memory_order_relaxed) + numSamples), std::memory_order_release);
    }

    // Message thread: the newest `frames` (fewer if it hasn't heard that much yet), oldest first.
    juce::AudioBuffer<float> newest (int frames) const { return copy (write.load (std::memory_order_acquire), frames); }

    // Message thread: the `frames` that end at frame `end` of the count (a value written() gave),
    // whatever has been pushed since: a reader taking what's new gets exactly that, never a block
    // twice or none.
    juce::AudioBuffer<float> ending (juce::int64 end, int frames) const
    {
        return copy (size > 0 ? (int) ((end - base) % size) : 0, frames);
    }

private:
    juce::AudioBuffer<float> copy (int end, int frames) const
    {
        const int have = juce::jmin (filled.load (std::memory_order_acquire), juce::jmax (0, size - 8192));   // (clear of the writer)
        const int n = juce::jlimit (0, have, frames);
        juce::AudioBuffer<float> out (2, juce::jmax (1, n));
        out.clear();
        for (int i = 0; i < n; ++i)
        {
            const int at = (end - n + i + size) % size;
            out.setSample (0, i, data[(size_t) at * 2] / 32767.0f);
            out.setSample (1, i, data[(size_t) at * 2 + 1] / 32767.0f);
        }
        if (n == 0)
            out.setSize (2, 0);
        return out;
    }

    std::vector<std::int16_t> data;   // L R buses
    int size = 0;
    juce::int64 base = 0;
    std::atomic<int> write { 0 }, filled { 0 };
    std::atomic<juce::int64> total { 0 };
};

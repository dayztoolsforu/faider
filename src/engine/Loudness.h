#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

// Short-term loudness (EBU R128 / ITU-R BS.1770): K-weighted mean square over the last 3 s, in
// LUFS. Audio thread: process() each block; lufs() any time after.
class Loudness
{
public:
    void prepare (double sampleRate)
    {
        rate = sampleRate;
        // K-weighting: a high shelf (+4 dB from about 1.7 kHz, the head's effect) then a gentle high-pass.
        const auto shelf = juce::IIRCoefficients::makeHighShelf (rate, 1681.97, 0.7072, juce::Decibels::decibelsToGain (4.0));
        const auto hp = juce::IIRCoefficients::makeHighPass (rate, 38.13, 0.5003);
        for (int c = 0; c < 2; ++c)
        {
            stage1[(size_t) c].setCoefficients (shelf);
            stage2[(size_t) c].setCoefficients (hp);
            stage1[(size_t) c].reset();
            stage2[(size_t) c].reset();
        }
        blockLength = (int) (rate * 0.1);
        blocks.fill (0.0);
        filled = 0;
        sum = 0.0;
        counted = 0;
    }

    void process (const juce::AudioBuffer<float>& buf, int numSamples, bool stereo) noexcept
    {
        if (blockLength <= 0)
            return;
        const int channels = stereo ? 2 : 1;
        for (int i = 0; i < numSamples; ++i)
        {
            double e = 0.0;
            for (int c = 0; c < channels; ++c)
            {
                const float k = stage2[(size_t) c].processSingleSampleRaw (stage1[(size_t) c].processSingleSampleRaw (buf.getSample (c, i)));
                e += (double) k * k;
            }
            sum += e;
            if (++counted >= blockLength)   // a 100 ms block done: into the 3 s window
            {
                blocks[(size_t) (filled++ % blocks.size())] = sum / counted;
                sum = 0.0;
                counted = 0;
                double total = 0.0;
                const int n = juce::jmin (filled, (int) blocks.size());
                for (int b = 0; b < n; ++b)
                    total += blocks[(size_t) b];
                const double meanSquare = total / juce::jmax (1, n);
                shortTerm = meanSquare > 1.0e-10 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare)) : -100.0f;
            }
        }
    }

    float lufs() const { return shortTerm.load(); }

private:
    double rate = 48000.0;
    std::array<juce::IIRFilter, 2> stage1, stage2;
    std::array<double, 30> blocks {};   // 30 x 100 ms = 3 s
    int blockLength = 0, filled = 0, counted = 0;
    double sum = 0.0;
    std::atomic<float> shortTerm { -100.0f };
};

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

// Noise suppression for a voice: steady background noise (fans, hum, hiss, air conditioning) is
// pulled down while speech passes. Short-time spectra (512-point frames, half overlapped): each
// frequency's noise floor is learnt by following its quietest level (it rises slowly, so speech
// doesn't count as noise), and each frequency is turned down by how far it sits above that floor,
// never further than `depth`. Latency: one frame (latencySamples()).
class NoiseReducer
{
public:
    static constexpr int order = 9, size = 1 << order, hop = size / 2;

    void prepare (double sampleRate)
    {
        rate = sampleRate;
        for (auto& c : channels)
        {
            c.input.fill (0.0f);
            c.output.fill (0.0f);
            c.inPos = 0;
        }
        noise.fill (0.0f);
        smoothed.fill (0.0f);
        gains.fill (1.0f);
        learnt = 0;
        // Sqrt-Hann on the way in and out: overlapping halves add back to exactly 1.
        for (int i = 0; i < size; ++i)
            window[(size_t) i] = (float) std::sqrt (0.5 * (1.0 - std::cos (juce::MathConstants<double>::twoPi * i / size)));
    }

    static constexpr int latencySamples() { return size; }

    // Processes stereo in place, one sample at a time. depthDb: the most any frequency is cut.
    void process (float& l, float& r, float depthDb) noexcept
    {
        float* io[2] = { &l, &r };
        for (int c = 0; c < 2; ++c)
        {
            auto& ch = channels[(size_t) c];
            ch.input[(size_t) ch.inPos] = *io[c];
            *io[c] = ch.output[(size_t) ch.inPos];
            ch.output[(size_t) ch.inPos] = 0.0f;
            ch.inPos = (ch.inPos + 1) % size;
        }
        if (++sinceFrame >= hop)
        {
            sinceFrame = 0;
            floorGain = juce::Decibels::decibelsToGain (depthDb);   // (only a frame reads it)
            frame();
        }
    }

private:
    struct Channel
    {
        std::array<float, size> input {}, output {};   // rings: the last `size` samples in; what's due out
        int inPos = 0;
    };

    void frame() noexcept
    {
        // Both channels' spectra; the noise floor and gains follow their sum (so the image holds).
        std::array<std::array<float, 2 * size>, 2> spectra;
        for (int c = 0; c < 2; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto& s = spectra[(size_t) c];
            for (int i = 0; i < size; ++i)
                s[(size_t) i] = ch.input[(size_t) ((ch.inPos + i) % size)] * window[(size_t) i];
            std::fill (s.begin() + size, s.end(), 0.0f);
            fft.performRealOnlyForwardTransform (s.data());
        }

        const float rise = 1.0f + 0.4f * (float) hop / (float) rate;   // a quietest-level tracker that creeps up about 1.7 dB a second
        for (int b = 0; b <= size / 2; ++b)
        {
            float power = 0.0f;
            for (auto& s : spectra)
                power += s[(size_t) (2 * b)] * s[(size_t) (2 * b)] + s[(size_t) (2 * b + 1)] * s[(size_t) (2 * b + 1)];

            // Each frequency's level, smoothed over a few frames; the noise floor follows its
            // quietest (creeping up slowly), times 3: the quietest moments of noise sit well
            // below its average.
            auto& level = smoothed[(size_t) b];
            level = learnt == 0 ? power : level + (power - level) * 0.35f;
            auto& n = noise[(size_t) b];
            n = learnt == 0 ? level : juce::jmin (level, n * rise);

            // Wiener-style: keep what stands above the noise; cut at most to the floor.
            const float wanted = level > 0.0f ? juce::jlimit (floorGain, 1.0f, 1.0f - 2.0f * 3.0f * n / level) : floorGain;
            auto& g = gains[(size_t) b];
            g += (wanted - g) * (wanted > g ? 0.7f : 0.25f);   // opens quickly, closes gently (less "musical noise")
            for (auto& s : spectra)
            {
                s[(size_t) (2 * b)] *= g;
                s[(size_t) (2 * b + 1)] *= g;
            }
        }
        ++learnt;

        for (int c = 0; c < 2; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto& s = spectra[(size_t) c];
            fft.performRealOnlyInverseTransform (s.data());
            for (int i = 0; i < size; ++i)   // overlap-add into what's due out over the next frame
                ch.output[(size_t) ((ch.inPos + i) % size)] += s[(size_t) i] * window[(size_t) i];
        }
    }

    juce::dsp::FFT fft { order };
    double rate = 48000.0;
    std::array<Channel, 2> channels;
    std::array<float, size> window {};
    std::array<float, size / 2 + 1> noise {}, gains {}, smoothed {};
    int sinceFrame = 0, learnt = 0;
    float floorGain = 0.1f;
};

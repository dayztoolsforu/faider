#pragma once

#include "Metering.h"
#include <array>
#include <cmath>

// Measures the selected meter standard (Metering.h) for one strip, pair or bus. Runs on the
// audio thread: no allocation, and it re-prepares itself when the standard or sample rate
// changes. Each block gives one value per channel, as a linear amplitude.
class MeterProcessor
{
public:
    void process (const float* left, const float* right, int n, bool stereo, double sampleRate, float& outL, float& outR)
    {
        const int index = Metering::currentStandard.load();
        if (index != preparedIndex || sampleRate != rate)
            prepare (index, sampleRate);

        const auto& s = Metering::standard();
        const float* ch[2] = { left, stereo ? right : left };

        switch (s.kind)
        {
            case Metering::Kind::samplePeak:
            case Metering::Kind::truePeak:
            {
                const float release = juce::Decibels::decibelsToGain (-s.releaseDbPerSecond * (float) (n / rate));
                for (int c = 0; c < 2; ++c)
                {
                    const float block = s.kind == Metering::Kind::truePeak ? truePeak (c, ch[c], n) : samplePeak (ch[c], n);
                    value[c] = juce::jmax (block, value[c] * release);
                }
                break;
            }

            case Metering::Kind::ppm:
                for (int c = 0; c < 2; ++c)
                {
                    float v = value[c];
                    for (int i = 0; i < n; ++i)
                    {
                        const float r = std::abs (ch[c][i]);
                        v = r > v ? v + ppmAttack * (r - v) : v * ppmRelease;
                    }
                    value[c] = v;
                }
                break;

            case Metering::Kind::vu:
                // Full-wave average through a critically damped 2nd-order response (two equal
                // poles reaching 99% in 300 ms), scaled so a sine reads its peak level.
                for (int c = 0; c < 2; ++c)
                {
                    double a = vuState[c][0], b = vuState[c][1];
                    for (int i = 0; i < n; ++i)
                    {
                        a += vuCoeff * (std::abs (ch[c][i]) - a);
                        b += vuCoeff * (a - b);
                    }
                    vuState[c][0] = a;
                    vuState[c][1] = b;
                    value[c] = (float) (b * juce::MathConstants<double>::halfPi);
                }
                break;

            case Metering::Kind::rms:
                // Rectangular window; x sqrt(2) so a sine reads its peak level (AES17).
                for (int c = 0; c < 2; ++c)
                {
                    double e = 0.0;
                    for (int i = 0; i < n; ++i)
                        e += (double) ch[c][i] * ch[c][i];
                    window[c].add (e, n, windowSamples);
                    value[c] = (float) std::sqrt (2.0 * window[c].mean());
                }
                break;

            case Metering::Kind::loudness:
            {
                // BS.1770 K-weighting per channel, energies summed (L + R), mean over 400 ms.
                double e = 0.0;
                for (int c = 0; c < (stereo ? 2 : 1); ++c)
                    for (int i = 0; i < n; ++i)
                    {
                        const double y = kWeight[c].process (ch[c][i]);
                        e += y * y;
                    }
                window[0].add (e, n, windowSamples);
                const double ms = window[0].mean();
                const double lufs = ms > 1.0e-20 ? -0.691 + 10.0 * std::log10 (ms) : -200.0;
                value[0] = value[1] = (float) std::pow (10.0, lufs / 20.0);
                break;
            }
        }

        // A NaN or infinity got in (a misbehaving plugin): start again from silence, or the
        // filters and windows would carry it for good (a window's sum never lets go of a NaN).
        if (! std::isfinite (value[0]) || ! std::isfinite (value[1]) || ! std::isfinite (window[0].sum) || ! std::isfinite (window[1].sum))
            prepare (index, rate);

        outL = value[0];
        outR = value[1];
    }

private:
    // Sums of per-block energies over a sliding window of whole blocks.
    struct Window
    {
        std::array<double, 4096> energy {};
        std::array<int, 4096> count {};
        int head = 0, used = 0, samples = 0;
        double sum = 0.0;

        void reset() { head = used = samples = 0; sum = 0.0; }

        void add (double e, int n, int maxSamples)
        {
            if (used == (int) energy.size())
                drop();
            energy[(size_t) ((head + used) % (int) energy.size())] = e;
            count[(size_t) ((head + used) % (int) energy.size())] = n;
            ++used;
            sum += e;
            samples += n;
            while (used > 1 && samples - count[(size_t) head] >= maxSamples)
                drop();
        }

        void drop()
        {
            sum -= energy[(size_t) head];
            samples -= count[(size_t) head];
            head = (head + 1) % (int) energy.size();
            --used;
        }

        double mean() const { return samples > 0 ? juce::jmax (0.0, sum) / samples : 0.0; }
    };

    // Direct form I biquad pair: the BS.1770 pre-filter (high shelf) and RLB high-pass.
    struct KWeighting
    {
        double b[2][3] {}, a[2][3] {}, x[2][2] {}, y[2][2] {};

        void prepare (double fs)
        {
            {
                const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
                const double k = std::tan (juce::MathConstants<double>::pi * f0 / fs);
                const double vh = std::pow (10.0, g / 20.0), vb = std::pow (vh, 0.4996667741545416);
                const double a0 = 1.0 + k / q + k * k;
                b[0][0] = (vh + vb * k / q + k * k) / a0;
                b[0][1] = 2.0 * (k * k - vh) / a0;
                b[0][2] = (vh - vb * k / q + k * k) / a0;
                a[0][1] = 2.0 * (k * k - 1.0) / a0;
                a[0][2] = (1.0 - k / q + k * k) / a0;
            }
            {
                const double f0 = 38.13547087602444, q = 0.5003270373238773;
                const double k = std::tan (juce::MathConstants<double>::pi * f0 / fs);
                const double a0 = 1.0 + k / q + k * k;
                b[1][0] = 1.0;  b[1][1] = -2.0;  b[1][2] = 1.0;
                a[1][1] = 2.0 * (k * k - 1.0) / a0;
                a[1][2] = (1.0 - k / q + k * k) / a0;
            }
            for (auto& s : x) s[0] = s[1] = 0.0;
            for (auto& s : y) s[0] = s[1] = 0.0;
        }

        double process (double in)
        {
            for (int st = 0; st < 2; ++st)
            {
                const double out = b[st][0] * in + b[st][1] * x[st][0] + b[st][2] * x[st][1] - a[st][1] * y[st][0] - a[st][2] * y[st][1];
                x[st][1] = x[st][0];  x[st][0] = in;
                y[st][1] = y[st][0];  y[st][0] = out;
                in = out;
            }
            return in;
        }
    };

    void prepare (int index, double sampleRate)
    {
        preparedIndex = index;
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const auto& s = Metering::standard();

        value[0] = value[1] = 0.0f;
        windowSamples = juce::jmax (1, (int) (s.windowSeconds * rate));
        for (auto& w : window) w.reset();
        for (auto& k : kWeight) k.prepare (rate);
        for (auto& st : vuState) st[0] = st[1] = 0.0;

        ppmAttack = s.attackMs > 0.0f ? (float) (1.0 - std::exp (-1000.0 / (s.attackMs * rate))) : 1.0f;
        ppmRelease = juce::Decibels::decibelsToGain (-s.releaseDbPerSecond / (float) rate);
        vuCoeff = 1.0 - std::exp (-1.0 / ((0.3 / 6.64) * rate));   // two such poles reach 99% in 300 ms

        // 4x oversampling for true peak: windowed-sinc (Lanczos) interpolation, 12 taps per phase.
        for (int p = 0; p < 4; ++p)
        {
            double sum = 0.0;
            for (int j = 0; j < taps; ++j)
            {
                const double u = (double) j - taps / 2 + p / 4.0;
                auto sinc = [] (double v) { return std::abs (v) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * v) / (juce::MathConstants<double>::pi * v); };
                firCoeff[p][j] = (float) (sinc (u) * sinc (u / (taps / 2)));
                sum += firCoeff[p][j];
            }
            for (int j = 0; j < taps; ++j)
                firCoeff[p][j] = (float) (firCoeff[p][j] / sum);
        }
        for (auto& h : history) h.fill (0.0f);
        historyPos[0] = historyPos[1] = 0;
    }

    // The highest sample of this block, including the three points between each pair of samples.
    float truePeak (int c, const float* x, int n)
    {
        auto& h = history[(size_t) c];
        int& pos = historyPos[c];
        float peak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            h[(size_t) pos] = h[(size_t) (pos + taps)] = x[i];   // doubled ring: always a contiguous window
            pos = (pos + 1) % taps;

            for (int p = 0; p < 4; ++p)
            {
                float y = 0.0f;
                for (int j = 0; j < taps; ++j)
                    y += firCoeff[p][j] * h[(size_t) (pos + taps - 1 - j)];
                peak = juce::jmax (peak, std::abs (y));
            }
        }
        return peak;
    }

    static float samplePeak (const float* x, int n)
    {
        const auto range = juce::FloatVectorOperations::findMinAndMax (x, n);
        return juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
    }

    static constexpr int taps = 12;

    int preparedIndex = -1;
    double rate = 0.0;
    float value[2] {};
    int windowSamples = 1;
    Window window[2];
    KWeighting kWeight[2];
    double vuState[2][2] {};
    double vuCoeff = 0.0;
    float ppmAttack = 1.0f, ppmRelease = 1.0f;
    float firCoeff[4][taps] {};
    std::array<std::array<float, 2 * taps>, 2> history {};
    int historyPos[2] {};
};

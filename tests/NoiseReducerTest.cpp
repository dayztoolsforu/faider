// Checks the noise reducer (src/engine/NoiseReducer.h) without the app:
//  1. at 0 dB depth it changes nothing: the output is the input, one frame late, sample for sample;
//  2. steady noise alone is turned down by close to the depth once the floor is learnt;
//  3. speech-like bursts of tone well above the noise come through at about their level.
// Prints PASS/FAIL per check; exits non-zero on failure.
#include <juce_dsp/juce_dsp.h>
#include "../src/engine/NoiseReducer.h"
#include "../src/engine/NeuralNoise.h"

namespace
{
    constexpr double rate = 48000.0;
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    }

    double rmsDb (const std::vector<float>& x, size_t from, size_t to)
    {
        double sum = 0.0;
        for (size_t i = from; i < to; ++i) sum += (double) x[i] * x[i];
        return 10.0 * std::log10 (sum / (double) (to - from) + 1.0e-20);
    }
}

int main()
{
    juce::Random random (1234);
    const size_t n = (size_t) (rate * 4.0);
    std::vector<float> noise (n), tone (n);
    for (size_t i = 0; i < n; ++i)
    {
        noise[i] = (random.nextFloat() * 2.0f - 1.0f) * 0.01f;   // about -45 dBFS RMS
        // Speech-like: a tone in syllables (3 a second, on half the time, soft edges).
        const double t = (double) i / rate;
        const double syllable = std::sin (juce::MathConstants<double>::twoPi * 3.0 * t);
        const double envelope = juce::jlimit (0.0, 1.0, syllable * 4.0);
        tone[i] = (float) (0.25 * envelope * std::sin (juce::MathConstants<double>::twoPi * 440.0 * t));
    }

    // 1. Transparent at 0 dB.
    {
        NoiseReducer nr;
        nr.prepare (rate);
        double worst = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            float l = noise[i] + tone[i], r = l;
            nr.process (l, r, 0.0f);
            if (i >= (size_t) NoiseReducer::latencySamples())
                worst = juce::jmax (worst, (double) std::abs (l - (noise[i - NoiseReducer::latencySamples()] + tone[i - NoiseReducer::latencySamples()])));
        }
        check (worst < 1.0e-4, "0 dB depth: output = input delayed " + juce::String (NoiseReducer::latencySamples()) + " samples (worst error " + juce::String (worst, 7) + ")");
    }

    // 2. Noise alone, -18 dB depth: the last second is well down.
    {
        NoiseReducer nr;
        nr.prepare (rate);
        std::vector<float> out (n);
        for (size_t i = 0; i < n; ++i)
        {
            float l = noise[i], r = l;
            nr.process (l, r, -18.0f);
            out[i] = l;
        }
        const double before = rmsDb (noise, n - (size_t) rate, n), after = rmsDb (out, n - (size_t) rate, n);
        check (after < before - 10.0, "noise alone: " + juce::String (before, 1) + " dB -> " + juce::String (after, 1) + " dB (at least 10 dB down)");
    }

    // 3. Tone over noise: the tone keeps its level (within 1.5 dB).
    {
        NoiseReducer nr;
        nr.prepare (rate);
        std::vector<float> out (n), in (n);
        for (size_t i = 0; i < n; ++i)
        {
            in[i] = noise[i] + tone[i];
            float l = in[i], r = l;
            nr.process (l, r, -18.0f);
            out[i] = l;
        }
        const double before = rmsDb (in, n - (size_t) rate, n), after = rmsDb (out, n - (size_t) rate, n);
        check (std::abs (after - before) < 1.5, "tone over noise: " + juce::String (before, 1) + " dB -> " + juce::String (after, 1) + " dB (within 1.5 dB)");
    }

    // RNNoise (src/engine/NeuralNoise.h), on a voice-like sound: a 140 Hz buzz with harmonics in
    // syllables, over the same noise.
    std::vector<float> voice (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / rate;
        const double envelope = juce::jlimit (0.0, 1.0, std::sin (juce::MathConstants<double>::twoPi * 3.0 * t) * 4.0);
        double v = 0.0;
        for (int h = 1; h <= 20; ++h)
            v += std::sin (juce::MathConstants<double>::twoPi * 140.0 * h * t) / h;
        voice[i] = (float) (0.12 * envelope * v);
    }
    auto runNeural = [&] (const std::vector<float>& in, float depth, std::vector<float>& out, double& seconds)
    {
        NeuralNoise nr;
        nr.prepare();
        out.resize (n);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (size_t i = 0; i < n; ++i)
        {
            float l = in[i], r = in[i] * 0.9f;   // the two sides differ: both states run
            nr.process (l, r, depth);
            out[i] = l;
        }
        seconds = (juce::Time::getMillisecondCounterHiRes() - start) * 0.001;
    };

    // 4. Noise alone, -40 dB depth: well down.
    {
        std::vector<float> out;
        double seconds = 0.0;
        runNeural (noise, -40.0f, out, seconds);
        const double before = rmsDb (noise, n - (size_t) rate, n), after = rmsDb (out, n - (size_t) rate, n);
        check (after < before - 10.0, "RNNoise, noise alone: " + juce::String (before, 1) + " dB -> " + juce::String (after, 1) + " dB (at least 10 dB down)");
        check (seconds < 4.0 * 0.1, "RNNoise, stereo: " + juce::String (100.0 * seconds / 4.0, 1) + " % of real time (under 10 %)");
    }

    // 5. Voice over noise: the voice comes through, lined up with latencySamples().
    {
        std::vector<float> in (n), out;
        for (size_t i = 0; i < n; ++i) in[i] = voice[i] + noise[i];
        double seconds = 0.0;
        runNeural (in, -40.0f, out, seconds);
        const double before = rmsDb (in, n - (size_t) rate, n), after = rmsDb (out, n - (size_t) rate, n);
        check (std::abs (after - before) < 3.0, "RNNoise, voice over noise: " + juce::String (before, 1) + " dB -> " + juce::String (after, 1) + " dB (within 3 dB)");

        int bestLag = 0;
        double best = -1.0;
        for (int lag = 0; lag < 3000; ++lag)
        {
            double sum = 0.0;
            for (size_t i = n - (size_t) rate; i < n; ++i) sum += (double) out[i] * voice[i - (size_t) lag];
            if (sum > best) { best = sum; bestLag = lag; }
        }
        // Within 2 samples: RNNoise's own low cut (about 15 Hz) leads the phase a little.
        check (std::abs (bestLag - NeuralNoise::latencySamples()) <= 2,
               "RNNoise latency: " + juce::String (bestLag) + " samples (expect " + juce::String (NeuralNoise::latencySamples()) + " +/- 2)");
    }

    // 6. Mono (both sides the same) runs once and gives both sides the same.
    {
        NeuralNoise nr;
        nr.prepare();
        double worst = 0.0;
        for (size_t i = 0; i < (size_t) rate; ++i)
        {
            float l = voice[i] + noise[i], r = l;
            nr.process (l, r, -20.0f);
            worst = juce::jmax (worst, (double) std::abs (l - r));
        }
        check (worst == 0.0, "RNNoise, mono: both sides the same");
    }

    std::cout << (failures == 0 ? "ALL PASSED\n" : "FAILURES: " + juce::String (failures).toStdString() + "\n");
    return failures == 0 ? 0 : 1;
}

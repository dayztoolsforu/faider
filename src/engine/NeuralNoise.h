#pragma once

#include <array>

struct DenoiseState;

// Noise suppression by RNNoise (Jean-Marc Valin, Xiph.Org; BSD licence; third_party/rnnoise): a
// small neural network trained on speech and noise sets each band's gain every 10 ms frame, so
// it takes out changing noise (keyboards, traffic, chatter) as well as steady noise. 48 kHz only.
// depth: what RNNoise took out is mixed back in at that level, so -40 dB is RNNoise in full and
// -6 dB is a light touch. Latency: one frame gathering the input, two inside RNNoise.
class NeuralNoise
{
public:
    static constexpr int frame = 480;
    static constexpr int latencySamples() { return 3 * frame; }

    NeuralNoise();
    ~NeuralNoise();
    NeuralNoise (const NeuralNoise&) = delete;
    NeuralNoise& operator= (const NeuralNoise&) = delete;

    void prepare();   // clears what's buffered (call before playing)

    // Processes stereo in place, one sample at a time. depthDb: how far down the noise goes.
    void process (float& l, float& r, float depthDb) noexcept;

    // RNNoise's estimate that the last frame held a voice, 0 to 1.
    float voice() const noexcept { return voiceProbability; }

private:
    void runFrame() noexcept;

    struct Channel
    {
        DenoiseState* state = nullptr;
        std::array<float, frame> in {}, out {};
        std::array<std::array<float, frame>, 2> dry {};   // the two frames before `in`: what `out` was made from
    };
    std::array<Channel, 2> channels;
    int pos = 0;
    float floorGain = 0.1f, voiceProbability = 0.0f;
    bool shared = false;   // the right channel's state is a copy of the left's (both channels the same)
};

#include "NeuralNoise.h"

#include <cmath>
#include <cstring>
#include <rnnoise.h>

NeuralNoise::NeuralNoise()
{
    for (auto& c : channels)
        c.state = rnnoise_create (nullptr);   // the model built into the library
}

NeuralNoise::~NeuralNoise()
{
    for (auto& c : channels)
        rnnoise_destroy (c.state);
}

void NeuralNoise::prepare()
{
    for (auto& c : channels)
    {
        rnnoise_init (c.state, nullptr);
        c.in.fill (0.0f);
        c.out.fill (0.0f);
        for (auto& d : c.dry)
            d.fill (0.0f);
    }
    pos = 0;
    shared = false;
    voiceProbability = 0.0f;
}

void NeuralNoise::process (float& l, float& r, float depthDb) noexcept
{
    float* io[2] = { &l, &r };
    for (int c = 0; c < 2; ++c)
    {
        auto& ch = channels[(size_t) c];
        ch.in[(size_t) pos] = *io[c];
        *io[c] = ch.out[(size_t) pos];
    }
    if (++pos == frame)
    {
        pos = 0;
        floorGain = std::pow (10.0f, depthDb / 20.0f);   // (only a frame reads it)
        runFrame();
    }
}

void NeuralNoise::runFrame() noexcept
{
    // A mono source on both sides: RNNoise runs once. The right side's state is caught up from
    // the left's when they part.
    const bool same = channels[0].in == channels[1].in;
    if (shared && ! same)
        std::memcpy (channels[1].state, channels[0].state, (size_t) rnnoise_get_size());

    for (int c = 0; c < 2; ++c)
    {
        auto& ch = channels[(size_t) c];
        if (c == 1 && same)
            ch.out = channels[0].out;
        else
        {
            // RNNoise works in 16-bit sample units; its output is the input of two frames ago.
            std::array<float, frame> scaled, denoised;
            for (int i = 0; i < frame; ++i)
                scaled[(size_t) i] = ch.in[(size_t) i] * 32768.0f;
            const float vad = rnnoise_process_frame (ch.state, denoised.data(), scaled.data());
            if (c == 0)
                voiceProbability = vad;
            for (int i = 0; i < frame; ++i)
            {
                const float d = denoised[(size_t) i] * (1.0f / 32768.0f);
                ch.out[(size_t) i] = d + floorGain * (ch.dry[0][(size_t) i] - d);
            }
        }
        ch.dry[0] = ch.dry[1];
        ch.dry[1] = ch.in;
    }
    shared = same;
}

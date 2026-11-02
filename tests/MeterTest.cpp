// Checks every meter standard (MeterProcessor.h) against known answers. A 1 kHz sine at -18 dBFS
// (peak), in both channels, at 48 kHz in 128-sample blocks, for 3 s; then silence, to time the fall.
//
// Expected while the tone plays: sine-referenced meters read the sine's peak level (-18 dBFS),
// so aligned scales read their alignment mark: 0 VU, TEST on EBU/Nordic, 4 on BBC (shown as
// 0 relative), -9 on DIN (0 DIN = -9 dBFS), +2 on K-20, and +5 LU (-18 LUFS vs -23).

#include "../src/engine/MeterProcessor.h"
#include <cstdio>

int main()
{
    constexpr double rate = 48000.0;
    constexpr int block = 128;
    const float amplitude = juce::Decibels::decibelsToGain (-18.0f);
    int failures = 0;

    auto& list = Metering::standards();
    for (int s = 0; s < (int) list.size(); ++s)
    {
        Metering::currentStandard = s;
        MeterProcessor meter;
        juce::AudioBuffer<float> buf (2, block);
        double phase = 0.0;
        float l = 0.0f, r = 0.0f;

        for (int b = 0; b < (int) (3.0 * rate / block); ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                const float x = amplitude * (float) std::sin (phase);
                phase += juce::MathConstants<double>::twoPi * 1000.0 / rate;
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
            }
            meter.process (buf.getReadPointer (0), buf.getReadPointer (1), block, true, rate, l, r);
        }
        const float onTone = Metering::readout (l);

        buf.clear();
        float afterOneSecond = 0.0f;
        for (int b = 0; b < (int) (1.0 * rate / block); ++b)
            meter.process (buf.getReadPointer (0), buf.getReadPointer (1), block, true, rate, l, r);
        afterOneSecond = Metering::readout (l);

        const auto& st = list[(size_t) s];
        const float expected = st.kind == Metering::Kind::loudness ? 5.0f : -18.0f - st.zeroDb;
        const bool ok = std::abs (onTone - expected) < 0.6f;
        failures += ok ? 0 : 1;

        std::printf ("%-12s tone %+7.2f (expect %+6.2f) %s   1 s after: %+8.2f\n",
                     st.shortName.toRawUTF8(), onTone, expected, ok ? "ok  " : "FAIL", afterOneSecond);
    }

    std::printf ("%d failure(s)\n", failures);
    return failures;
}

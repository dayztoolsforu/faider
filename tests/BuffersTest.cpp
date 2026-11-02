// Checks two of the engine's buffers without the app (src/engine/DriftFifo.h, src/engine/Retro.h):
// the drift fifo carries a device that asks for far bigger blocks than the engine makes, at a
// high rate ratio, without running dry over and over, and its cushion grown for hiccups comes back
// down once they stop; and the retro buffer hands a reader exactly
// the frames up to the count it took, however many came in since. Prints PASS/FAIL per check;
// exits non-zero on any failure.
#include <juce_audio_basics/juce_audio_basics.h>
#include "../src/engine/DriftFifo.h"
#include "../src/engine/Retro.h"
#include <iostream>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    }
}

int main()
{
    // A 96 kHz engine (480-sample blocks) playing into a 48 kHz device that announces 480 but
    // asks for 8192 at a time (VB-CABLE with a long latency setting): the fifo has to hold more
    // than a whole such block at a 2:1 ratio.
    {
        DriftFifo fifo;
        fifo.prepare (2, 96000.0, 48000.0, 480, 480);
        juce::AudioBuffer<float> in (2, 480), out (2, 8192);
        in.clear();
        double produced = 0.0, consumed = 0.0;
        int underrunsLate = 0;
        for (int step = 0; step < 4000; ++step)
        {
            // Real time: the engine makes 96000 a second, the device takes 48000 a second.
            fifo.push (in.getArrayOfReadPointers(), 2, 480);
            produced += 480.0 / 96000.0;
            if (produced - consumed >= 8192.0 / 48000.0)
            {
                const int before = fifo.getUnderruns();
                fifo.pull (out.getArrayOfWritePointers(), 2, 8192);
                consumed += 8192.0 / 48000.0;
                if (consumed > 5.0 && fifo.getUnderruns() > before)
                    ++underrunsLate;
            }
        }
        check (underrunsLate == 0, "drift fifo: 8192-sample pulls at 2:1, no underruns once settled (" + juce::String (underrunsLate) + ")");
        check (fifo.getTarget() > 8192 * 2, "drift fifo: its cushion grew past a whole block (" + juce::String (fifo.getTarget()) + " samples)");
    }

    // Hiccups, then calm: a 48 kHz device in step with the engine (480-sample blocks both sides)
    // whose producer stalls for 60 ms every 3 s and then catches up. The cushion grows for the
    // stalls; once they stop it shrinks back to where it started, without running dry on the way.
    {
        DriftFifo fifo;
        fifo.prepare (2, 48000.0, 48000.0, 480, 480);
        const int base = fifo.getTarget();
        juce::AudioBuffer<float> in (2, 480), out (2, 480);
        in.clear();

        for (int step = 0; step < 3000; ++step)   // 30 s of stalls
        {
            const int phase = step % 300;
            if (phase >= 6)
                for (int k = 0; k < (phase == 6 ? 7 : 1); ++k)   // (the stalled blocks arrive at once)
                    fifo.push (in.getArrayOfReadPointers(), 2, 480);
            fifo.pull (out.getArrayOfWritePointers(), 2, 480);
        }
        const int grown = fifo.getTarget();
        check (grown > base,"drift fifo: its cushion grew for the stalls (" + juce::String (base) + " -> " + juce::String (grown) + " samples)");

        const int underrunsBefore = fifo.getUnderruns();
        for (int step = 0; step < 60000; ++step)   // 10 minutes steady
        {
            fifo.push (in.getArrayOfReadPointers(), 2, 480);
            fifo.pull (out.getArrayOfWritePointers(), 2, 480);
        }
        check (fifo.getTarget() == base, "drift fifo: the cushion came back down once calm (" + juce::String (fifo.getTarget()) + " samples)");
        check (fifo.getUnderruns() == underrunsBefore, "drift fifo: no underruns while it came down");
    }

    // The retro buffer: frames numbered by their value, read back by the count.
    {
        RetroBuffer retro;
        retro.prepare (48000);
        juce::AudioBuffer<float> block (2, 480);
        int next = 0;
        auto push = [&]
        {
            for (int i = 0; i < 480; ++i, ++next)
                for (int ch = 0; ch < 2; ++ch)
                    block.setSample (ch, i, (float) (next % 1000) / 32767.0f);
            retro.push (block, 480);
        };
        for (int i = 0; i < 20; ++i)
            push();

        const auto seen = retro.written();
        for (int i = 0; i < 3; ++i)
            push();   // three blocks land after the count was taken
        const auto got = retro.ending (seen, 960);
        bool exact = got.getNumSamples() == 960;
        for (int i = 0; exact && i < 960; ++i)
            exact = juce::roundToInt (got.getSample (0, i) * 32767.0f) == (int) ((seen - 960 + i) % 1000);
        check (exact, "retro buffer: ending() gives the frames up to the count taken, not the ones pushed since");

        const auto newest = retro.newest (480);
        check (newest.getNumSamples() == 480 && juce::roundToInt (newest.getSample (0, 479) * 32767.0f) == (next - 1) % 1000,
               "retro buffer: newest() ends at the last frame pushed");

        retro.prepare (48000);   // made again (a new rate): the count carries on, reads still line up
        const auto again = retro.written();
        push();
        const auto after = retro.ending (again + 480, 480);
        check (juce::roundToInt (after.getSample (0, 0) * 32767.0f) == (int) ((next - 480) % 1000),
               "retro buffer: still exact after being made again");
    }

    std::cout << (failures == 0 ? "ALL PASS\n" : juce::String (failures) + " FAILED\n");
    return failures == 0 ? 0 : 1;
}

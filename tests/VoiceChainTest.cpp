// Checks the channel's processing (src/engine/VoiceChain.h) without the app:
//  1. auto-level brings a steady -28 dB voice to its -18 dB target;
//  2. it holds its gain through a pause (silence doesn't pump it up);
//  3. it never goes past its range (a -40 dB voice with 6 dB of range gets +6 dB);
//  4. the gate with RANGE at -INF closes to true silence;
//  5. the dynamics' settings: old defaults, compressor INF:1 and MIX, gate HYSTERESIS and HOLD.
// Prints PASS/FAIL per check; exits non-zero on failure.
#include "../src/engine/VoiceChain.h"

// The editor isn't part of this check.
juce::AudioProcessorEditor* VoiceChain::createEditor() { return nullptr; }

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 480;
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    }

    void set (VoiceChain& chain, const char* id, float value)
    {
        auto* p = chain.params.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    // Runs `seconds` of a 440 Hz sine `rmsDb` dB RMS (or silence if NaN) through the chain;
    // returns the output's RMS in dB over the last second.
    double run (VoiceChain& chain, double seconds, double rmsDb, double& phase, bool* allZero = nullptr, double hz = 440.0)
    {
        const float amplitude = std::isnan (rmsDb) ? 0.0f : (float) (juce::Decibels::decibelsToGain (rmsDb) * std::sqrt (2.0));
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        const int blocks = (int) (seconds * rate / block), lastSecond = (int) (rate / block);
        double sum = 0.0;
        if (allZero != nullptr)
            *allZero = true;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                const float v = amplitude * (float) std::sin (phase);
                phase += juce::MathConstants<double>::twoPi * hz / rate;
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            chain.processBlock (buf, midi);
            if (b >= blocks - lastSecond)
                for (int i = 0; i < block; ++i)
                {
                    const float out = buf.getSample (0, i);
                    sum += (double) out * out;
                    if (allZero != nullptr && out != 0.0f)
                        *allZero = false;
                }
        }
        return 10.0 * std::log10 (sum / (lastSecond * block) + 1.0e-30);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;   // parameters notify on the message thread's terms
    double phase = 0.0;

    {
        VoiceChain chain;
        chain.prepareToPlay (rate, block);
        chain.setAllStages (false);
        set (chain, "levelOn", 1.0f);
        set (chain, "levelTarget", -18.0f);
        set (chain, "levelRange", 12.0f);

        const double out = run (chain, 20.0, -28.0, phase);
        check (std::abs (out + 18.0) < 1.0, "auto-level: -28 dB in comes out at " + juce::String (out, 1) + " dB (target -18)");

        const float before = chain.levelGain.load();
        run (chain, 5.0, std::numeric_limits<double>::quiet_NaN(), phase);
        const float after = chain.levelGain.load();
        check (std::abs (after - before) < 0.5f, "auto-level: held through 5 s of silence (" + juce::String (before, 1) + " -> " + juce::String (after, 1) + " dB)");
    }

    {
        VoiceChain chain;
        chain.prepareToPlay (rate, block);
        chain.setAllStages (false);
        set (chain, "levelOn", 1.0f);
        set (chain, "levelTarget", -18.0f);
        set (chain, "levelRange", 6.0f);
        run (chain, 20.0, -40.0, phase);
        const float gain = chain.levelGain.load();
        check (std::abs (gain - 6.0f) < 0.3f, "auto-level: stays within its range (+" + juce::String (gain, 1) + " dB of 6)");
    }

    {
        VoiceChain chain;
        chain.prepareToPlay (rate, block);
        chain.setAllStages (false);
        set (chain, "gateOn", 1.0f);
        set (chain, "gateThreshold", -40.0f);
        set (chain, "gateRange", VoiceChain::gateSilent);
        bool silent = false;
        run (chain, 3.0, -60.0, phase, &silent);
        check (silent, "gate: RANGE -INF closes to true silence");
    }

    // The dynamics' times and knee start at what they were when fixed, so old sessions sound the same.
    {
        VoiceChain chain;
        const std::pair<const char*, float> was[] = { { "gateAttack", 1.0f }, { "gateHold", 0.0f }, { "gateRelease", 150.0f }, { "gateHysteresis", 0.0f },
                                                      { "compAttack", 5.0f }, { "compRelease", 120.0f }, { "compKnee", 6.0f }, { "compMix", 100.0f },
                                                      { "limitRelease", 80.0f } };
        bool same = true;
        for (auto& [id, value] : was)
        {
            auto* p = chain.params.getParameter (id);
            same = same && p != nullptr && std::abs (p->convertFrom0to1 (p->getDefaultValue()) - value) < 0.05f;
        }
        check (same, "dynamics: new settings default to the old fixed values");
    }

    // BELL: the low band +12 dB at 100 Hz; a 25 Hz tone two octaves under gets the shelf's full
    // boost, but only a bell's skirt.
    {
        auto low = [&phase] (bool bell)
        {
            VoiceChain chain;
            chain.prepareToPlay (rate, block);
            chain.setAllStages (false);
            set (chain, "eqOn", 1.0f);
            set (chain, "lowFreq", 100.0f);
            set (chain, "lowGain", 12.0f);
            set (chain, "lowBell", bell ? 1.0f : 0.0f);
            return run (chain, 2.0, -20.0, phase, nullptr, 25.0);
        };
        const double shelf = low (false), bell = low (true);
        check (shelf > -9.0 && bell < shelf - 6.0, "EQ: LF BELL a bell, not a shelf (25 Hz: shelf " + juce::String (shelf, 1) + " dB, bell " + juce::String (bell, 1) + " dB)");
    }

    // Compressor: INF:1 holds a -10 dB tone (peaks -7 dB) at the -20 dB threshold (RMS -23 dB);
    // MIX 0 % is the tone untouched, 50 % between.
    {
        auto compressed = [&phase] (float ratio, float mix)
        {
            VoiceChain chain;
            chain.prepareToPlay (rate, block);
            chain.setAllStages (false);
            set (chain, "compOn", 1.0f);
            set (chain, "compThreshold", -20.0f);
            set (chain, "compRatio", ratio);
            set (chain, "compKnee", 0.0f);
            set (chain, "makeup", 0.0f);
            set (chain, "compMix", mix);
            return run (chain, 2.0, -10.0, phase);
        };
        const double inf = compressed (VoiceChain::compRatioInf, 100.0f), dry = compressed (VoiceChain::compRatioInf, 0.0f),
                     half = compressed (VoiceChain::compRatioInf, 50.0f);
        check (std::abs (inf + 23.0) < 1.0, "compressor: INF:1 holds the threshold (" + juce::String (inf, 1) + " dB, expect -23)");
        check (std::abs (dry + 10.0) < 0.1, "compressor: MIX 0 % passes the tone untouched (" + juce::String (dry, 1) + " dB)");
        check (half > inf + 3.0 && half < dry - 1.0, "compressor: MIX 50 % between (" + juce::String (half, 1) + " dB)");

        // AUTO MAKE-UP: a tone peaking at the line-up level (-18 dBFS, RMS -21) comes out as it went
        // in, however hard the threshold and ratio squeeze it there.
        VoiceChain chain;
        chain.prepareToPlay (rate, block);
        chain.setAllStages (false);
        set (chain, "compOn", 1.0f);
        set (chain, "compThreshold", -38.0f);
        set (chain, "compRatio", 4.0f);
        set (chain, "compKnee", 0.0f);
        set (chain, "compAutoMakeup", 1.0f);
        const double autoLevel = run (chain, 2.0, -21.0, phase);
        check (std::abs (autoLevel + 21.0) < 1.0, "compressor: AUTO MAKE-UP keeps the line-up level (" + juce::String (autoLevel, 1) + " dB, expect -21)");
    }

    // LINK: a quiet channel (-40 dB, under the -20 dB threshold) linked to a loud one (about -1 dB)
    // is compressed by the louder: the pair moves together.
    {
        VoiceChain chain;
        chain.prepareToPlay (rate, block);
        chain.setAllStages (false);
        set (chain, "compOn", 1.0f);
        set (chain, "compThreshold", -20.0f);
        set (chain, "compRatio", VoiceChain::compRatioInf);
        set (chain, "compKnee", 0.0f);
        set (chain, "makeup", 0.0f);
        juce::AudioBuffer<float> buf (2, block), key (2, block);
        juce::MidiBuffer midi;
        double ph = 0.0, sum = 0.0;
        const int blocks = (int) rate / block;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                const float v = (float) std::sin (ph);
                ph += juce::MathConstants<double>::twoPi * 1000.0 / rate;
                for (int c = 0; c < 2; ++c)
                {
                    buf.setSample (c, i, 0.01f * v);
                    key.setSample (c, i, 0.9f * v);
                }
            }
            chain.setKey (key.getReadPointer (0), key.getReadPointer (1), block, true);
            chain.processBlock (buf, midi);
            if (b >= blocks / 2)
                for (int i = 0; i < block; ++i)
                    sum += (double) buf.getSample (0, i) * buf.getSample (0, i);
        }
        const double linked = 10.0 * std::log10 (sum / (blocks / 2 * block) + 1.0e-20);
        check (linked < -55.0, "LINK: the quiet channel is held down by its loud partner (" + juce::String (linked, 1) + " dB, alone -43)");
    }

    // Gate: opened by a -30 dB tone, then a -45 dB tone under its -40 dB threshold. With 10 dB of
    // hysteresis it stays open (closes only under -50 dB); without, it closes.
    {
        auto after = [&phase] (float hysteresis, float hold, double seconds)
        {
            VoiceChain chain;
            chain.prepareToPlay (rate, block);
            chain.setAllStages (false);
            set (chain, "gateOn", 1.0f);
            set (chain, "gateThreshold", -40.0f);
            set (chain, "gateRange", VoiceChain::gateSilent);
            set (chain, "gateHysteresis", hysteresis);
            set (chain, "gateHold", hold);
            run (chain, 1.0, -30.0, phase);
            if (hold > 0.0f)
                run (chain, 0.25, -60.0, phase);   // then measure 0.25-0.4 s after the voice dropped
            return run (chain, seconds, hold > 0.0f ? -60.0 : -45.0, phase);
        };
        const double held = after (10.0f, 0.0f, 2.0), closed = after (0.0f, 0.0f, 2.0);
        check (std::abs (held + 45.0) < 0.5, "gate: HYSTERESIS 10 dB keeps it open at -45 dB (" + juce::String (held, 1) + " dB)");
        check (closed < -100.0, "gate: no hysteresis, it closes at -45 dB (" + juce::String (closed, 1) + " dB)");

        // HOLD 500 ms: 0.25-0.4 s after the voice drops it still passes; without, it has mostly closed.
        const double withHold = after (0.0f, 500.0f, 0.15), withoutHold = after (0.0f, 0.001f, 0.15);
        check (withHold > withoutHold + 10.0, "gate: HOLD 500 ms keeps it open after the voice drops ("
                                              + juce::String (withHold - withoutHold, 1) + " dB more than no hold)");
    }

    // EQ: the four bands and filters where they're set. A tone at each point checked against the
    // expected boost or cut; the defaults leave the old three-band EQ as it was (MID 2 flat).
    {
        auto through = [&phase] (std::initializer_list<std::pair<const char*, float>> settings, double hz)
        {
            VoiceChain chain;
            chain.prepareToPlay (rate, block);
            chain.setAllStages (false);
            for (auto& [id, value] : settings)
                set (chain, id, value);
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            double sum = 0.0, local = 0.0;
            const int blocks = (int) rate / block;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < block; ++i)
                {
                    const float v = 0.1f * (float) std::sin (local);
                    local += juce::MathConstants<double>::twoPi * hz / rate;
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                chain.processBlock (buf, midi);
                if (b >= blocks / 2)
                    for (int i = 0; i < block; ++i)
                        sum += (double) buf.getSample (0, i) * buf.getSample (0, i);
            }
            juce::ignoreUnused (phase);
            return 10.0 * std::log10 (sum / (blocks / 2 * block)) - 10.0 * std::log10 (0.005);   // dB against the tone in
        };
        const double low = through ({ { "eqOn", 1.0f }, { "lowFreq", 300.0f }, { "lowGain", 9.0f } }, 60.0);
        const double mid2 = through ({ { "eqOn", 1.0f }, { "mid2Freq", 5000.0f }, { "mid2Gain", -10.0f }, { "mid2Q", 2.0f } }, 5000.0);
        const double high = through ({ { "eqOn", 1.0f }, { "highFreq", 3000.0f }, { "highGain", 8.0f } }, 15000.0);
        const double flat = through ({ { "eqOn", 1.0f } }, 1000.0);
        check (std::abs (low - 9.0) < 0.5, "EQ: LOW shelf at 300 Hz, +9 dB at 60 Hz (" + juce::String (low, 1) + ")");
        check (std::abs (mid2 + 10.0) < 0.5, "EQ: MID 2 bell -10 dB at its 5 kHz (" + juce::String (mid2, 1) + ")");
        check (std::abs (high - 8.0) < 0.5, "EQ: HIGH shelf at 3 kHz, +8 dB at 15 kHz (" + juce::String (high, 1) + ")");
        check (std::abs (flat) < 0.1, "EQ: in, all flat: untouched (" + juce::String (flat, 2) + ")");

        // HPF at 100 Hz: an octave below (50 Hz) about -12 dB at 12 dB/oct, -24 dB at 24 dB/oct.
        const double gentle = through ({ { "hpfOn", 1.0f }, { "hpf", 100.0f } }, 50.0);
        const double steep = through ({ { "hpfOn", 1.0f }, { "hpf", 100.0f }, { "hpfSteep", 1.0f } }, 50.0);
        check (gentle < -11.0 && gentle > -13.0 && steep < -23.0 && steep > -25.5,
               "HPF: 12 dB/oct " + juce::String (gentle, 1) + " dB, 24 dB/oct " + juce::String (steep, 1) + " dB an octave down");
        const double lpf = through ({ { "lpfOn", 1.0f }, { "lpf", 4000.0f } }, 8000.0);
        check (lpf < -11.0 && lpf > -14.5, "LPF at 4 kHz: " + juce::String (lpf, 1) + " dB an octave up (a little more than 12: nearer Nyquist)");

        // SPLIT off (the console's normal path): the filters belong to the EQ, out with it, in with it.
        const double unsplitOut = through ({ { "hpfOn", 1.0f }, { "hpf", 100.0f }, { "filterSplit", 0.0f } }, 50.0);
        const double unsplitIn = through ({ { "hpfOn", 1.0f }, { "hpf", 100.0f }, { "filterSplit", 0.0f }, { "eqOn", 1.0f } }, 50.0);
        check (std::abs (unsplitOut) < 0.1 && unsplitIn < -11.0, "SPLIT off: the HPF out with the EQ (" + juce::String (unsplitOut, 2) + " dB), in with it ("
                                                                 + juce::String (unsplitIn, 1) + " dB)");

        // CLASSIC: the high-pass at 18 dB/oct (-18 dB an octave down); the mid bell lands its
        // gain on its stepped frequency and narrows as it's boosted (an octave away keeps less of a
        // big boost than of a small one); the shelves overshoot a little past their corner.
        const double classicHpf = through ({ { "hpfOn", 1.0f }, { "hpf", 100.0f }, { "eqClassic", 1.0f } }, 50.0);
        check (classicHpf < -17.0 && classicHpf > -19.5, "CLASSIC HPF: 18 dB/oct, " + juce::String (classicHpf, 1) + " dB an octave down");
        auto bell = [&through] (float gain, double hz)
        {
            return through ({ { "eqOn", 1.0f }, { "eqClassic", 1.0f }, { "midFreq", 3200.0f }, { "midGain", gain } }, hz);
        };
        const double big = bell (12.0f, 3200.0), bigOctave = bell (12.0f, 1600.0), small = bell (3.0f, 3200.0), smallOctave = bell (3.0f, 1600.0);
        check (std::abs (big - 12.0) < 0.5 && std::abs (small - 3.0) < 0.3, "CLASSIC MID: +12 and +3 dB at its stepped 3.2 kHz ("
                   + juce::String (big, 1) + ", " + juce::String (small, 1) + ")");
        check (bigOctave / big < smallOctave / small - 0.1, "CLASSIC MID: narrows with boost (an octave away keeps "
                   + juce::String (juce::roundToInt (100.0 * bigOctave / big)) + "% of +12, " + juce::String (juce::roundToInt (100.0 * smallOctave / small)) + "% of +3)");
        auto shelfDip = [&through] (bool classic)
        {
            return through ({ { "eqOn", 1.0f }, { "eqClassic", classic ? 1.0f : 0.0f }, { "highFreq", 10000.0f }, { "highGain", 10.0f } }, 4000.0);
        };
        const double classicDip = shelfDip (true), plainDip = shelfDip (false);
        const double classicTop = through ({ { "eqOn", 1.0f }, { "eqClassic", 1.0f }, { "highFreq", 10000.0f }, { "highGain", 10.0f } }, 18000.0);
        check (classicDip < plainDip - 0.2 && classicTop > 9.0, "CLASSIC HF shelf: dips below its corner (" + juce::String (classicDip, 2) + " dB at 4 kHz, plain "
                   + juce::String (plainDip, 2) + "), " + juce::String (classicTop, 1) + " dB at 18 kHz");
    }

    // DRIVE: level-matched (a quiet tone comes out level), with distortion rising as it's pushed
    // (and none at 0). A 1 kHz tone; its harmonics measured exactly (whole cycles).
    {
        auto drive = [] (float driveDb, double peakDb, double& levelDb)
        {
            VoiceChain chain;
            chain.prepareToPlay (rate, block);
            chain.setAllStages (false);
            set (chain, "drive", driveDb);
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            const float amp = (float) juce::Decibels::decibelsToGain (peakDb);
            std::vector<float> out;
            double ph = 0.0;
            for (int b = 0; b < (int) rate / block; ++b)
            {
                for (int i = 0; i < block; ++i)
                {
                    const float v = amp * (float) std::sin (ph);
                    ph += juce::MathConstants<double>::twoPi * 1000.0 / rate;
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                chain.processBlock (buf, midi);
                if (b >= (int) rate / block / 2)
                    out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + block);
            }
            auto magnitude = [&out] (double hz)
            {
                double re = 0.0, im = 0.0;
                for (size_t i = 0; i < out.size(); ++i)
                {
                    const double a = juce::MathConstants<double>::twoPi * hz * (double) i / rate;
                    re += out[i] * std::cos (a);
                    im += out[i] * std::sin (a);
                }
                return 2.0 * std::sqrt (re * re + im * im) / (double) out.size();
            };
            const double fundamental = magnitude (1000.0);
            double harmonics = 0.0;
            for (int h = 2; h <= 5; ++h)
                harmonics += std::pow (magnitude (1000.0 * h), 2.0);
            levelDb = juce::Decibels::gainToDecibels (fundamental) - peakDb;
            return 100.0 * std::sqrt (harmonics) / fundamental;   // THD, %
        };
        double quietLevel = 0.0, l0 = 0.0, l6 = 0.0, l18 = 0.0;
        drive (18.0f, -40.0, quietLevel);
        const double thd0 = drive (0.0f, -6.0, l0), thd6 = drive (6.0f, -6.0, l6), thd18 = drive (18.0f, -6.0, l18);
        check (std::abs (quietLevel) < 0.5, "DRIVE: level-matched (a -40 dB tone through 18 dB of drive: " + juce::String (quietLevel, 2) + " dB)");
        check (thd0 < 0.01 && thd6 > thd0 + 0.1 && thd18 > thd6 * 2.0, "DRIVE: distortion rises with drive (THD " + juce::String (thd0, 3) + "%, "
                   + juce::String (thd6, 2) + "%, " + juce::String (thd18, 2) + "% at 0, 6, 18 dB)");
    }

    // Stage meters: a -10 dB RMS tone (peaks -7 dB) through a compressor at INF:1 from -20 dB:
    // the meter before it reads -7, after it -20, and the output too. With every stage out,
    // every point reads the same.
    {
        VoiceChain chain;
        chain.prepareToPlay (rate, block);
        chain.setAllStages (false);
        set (chain, "compOn", 1.0f);
        set (chain, "compThreshold", -20.0f);
        set (chain, "compRatio", VoiceChain::compRatioInf);
        set (chain, "compKnee", 0.0f);
        set (chain, "makeup", 0.0f);
        run (chain, 2.0, -10.0, phase);
        auto db = [&chain] (VoiceChain::Point p) { return juce::Decibels::gainToDecibels (chain.pointLevel[(size_t) p].load()); };
        check (std::abs (db (VoiceChain::afterGate) + 7.0f) < 0.5f && std::abs (db (VoiceChain::afterComp) + 20.0f) < 1.0f
                   && std::abs (db (VoiceChain::atOutput) - db (VoiceChain::afterComp)) < 0.01f,
               "meters: before the compressor " + juce::String (db (VoiceChain::afterGate), 1) + " dB, after "
                   + juce::String (db (VoiceChain::afterComp), 1) + " dB (expect -7, -20)");

        set (chain, "compOn", 0.0f);
        run (chain, 1.0, -10.0, phase);
        bool same = true;
        for (int p = 1; p < VoiceChain::pointCount; ++p)
            same = same && std::abs (db ((VoiceChain::Point) p) - db (VoiceChain::atInput)) < 0.01f;
        check (same, "meters: every stage out, every point the same (" + juce::String (db (VoiceChain::atInput), 1) + " dB)");
    }

    // Input stage: TRIM +6 dB, POLARITY flips, DELAY 10 ms moves an impulse 480 samples. All of
    // it works with every stage out.
    {
        VoiceChain chain;
        chain.prepareToPlay (rate, block);
        chain.setAllStages (false);
        set (chain, "trim", 6.0f);
        const double trimmed = run (chain, 1.0, -20.0, phase);
        check (std::abs (trimmed + 14.0) < 0.1, "input: TRIM +6 dB (" + juce::String (trimmed, 1) + " dB, expect -14)");
        {
            // THE STEREO MODULE'S INPUT: a signal on the left only, MONO L+R puts half on both;
            // ØL flips the left alone.
            VoiceChain st;
            st.prepareToPlay (rate, block);
            st.setAllStages (false);
            set (st, "stereoSource", 3.0f);
            juce::AudioBuffer<float> b (2, block);
            juce::MidiBuffer m;
            b.clear();
            for (int i = 0; i < block; ++i)
                b.setSample (0, i, 0.5f);
            st.processBlock (b, m);
            const bool summed = std::abs (b.getSample (0, block - 1) - 0.25f) < 0.001f && std::abs (b.getSample (1, block - 1) - 0.25f) < 0.001f;
            set (st, "stereoSource", 0.0f);
            set (st, "polarityL", 1.0f);
            b.clear();
            for (int i = 0; i < block; ++i)
                b.setSample (0, i, 0.5f), b.setSample (1, i, 0.5f);
            st.processBlock (b, m);
            const bool flipped = b.getSample (0, block - 1) < -0.49f && b.getSample (1, block - 1) > 0.49f;
            check (summed && flipped, "stereo input: MONO L+R halves a left-only signal onto both; ØL flips the left alone");
        }
        set (chain, "inputOn", 0.0f);   // INPUT out: the trim bypassed
        const double bypassed = run (chain, 1.0, -20.0, phase);
        check (std::abs (bypassed + 20.0) < 0.1, "input: IN out bypasses TRIM (" + juce::String (bypassed, 1) + " dB, expect -20)");
        set (chain, "inputOn", 1.0f);

        set (chain, "trim", 0.0f);
        set (chain, "polarity", 1.0f);
        set (chain, "inputDelay", 10.0f);
        run (chain, 0.1, std::numeric_limits<double>::quiet_NaN(), phase);   // the trim's ramp and the line settle
        juce::AudioBuffer<float> buf (2, block * 2);
        buf.clear();
        buf.setSample (0, 0, 0.5f);
        juce::MidiBuffer midi;
        for (int b = 0; b < 2; ++b)
        {
            juce::AudioBuffer<float> part (buf.getArrayOfWritePointers(), 2, b * block, block);
            chain.processBlock (part, midi);
        }
        check (buf.getSample (0, 480) == -0.5f && buf.getMagnitude (0, 0, 480) == 0.0f,
               "input: DELAY 10 ms moves it 480 samples, POLARITY flips it (" + juce::String (buf.getSample (0, 480)) + ")");
    }

    // EXPANDER (the classic console's, GATE off): a tone 7 dB under the threshold (its peaks) comes
    // out 7 dB further down (2:1), where the gate (RANGE -INF) shuts it to silence.
    {
        auto through = [&phase] (bool expand)
        {
            VoiceChain chain;
            chain.prepareToPlay (rate, block);
            chain.setAllStages (false);
            set (chain, "gateOn", 1.0f);
            set (chain, "gateThreshold", -30.0f);
            set (chain, "gateRange", VoiceChain::gateSilent);
            set (chain, "gateExpand", expand ? 1.0f : 0.0f);
            return run (chain, 2.0, -40.0, phase);
        };
        const double expanded = through (true), gated = through (false);
        check (std::abs (expanded + 47.0) < 1.5 && gated < -100.0, "EXPANDER: 2:1 under the threshold (-40 dB in, " + juce::String (expanded, 1)
                   + " dB out, expect -47); as a gate, shut (" + juce::String (gated, 1) + " dB)");
    }

    // SIDECHAIN: a 60 Hz tone 10 dB over the gate's threshold opens it, unless the key's high-pass
    // (500 Hz) keeps the hum out of the detector; a silent key from another channel holds it shut;
    // LISTEN plays the filtered key (the hum, far down).
    {
        auto through = [] (std::function<void (VoiceChain&)> setUp, const float* key)
        {
            VoiceChain chain;
            chain.prepareToPlay (rate, block);
            chain.setAllStages (false);
            set (chain, "gateOn", 1.0f);
            set (chain, "gateThreshold", -30.0f);
            set (chain, "gateRange", VoiceChain::gateSilent);
            set (chain, "gateRelease", 10.0f);   // (shut within the measured half)
            setUp (chain);
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            double sum = 0.0, ph = 0.0;
            const int blocks = (int) rate / block;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < block; ++i)
                {
                    const float v = 0.1f * (float) std::sin (ph);   // -20 dB peak
                    ph += juce::MathConstants<double>::twoPi * 60.0 / rate;
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                chain.setKey (key, key, key != nullptr ? block : 0);
                chain.processBlock (buf, midi);
                if (b >= blocks / 2)
                    for (int i = 0; i < block; ++i)
                        sum += (double) buf.getSample (0, i) * buf.getSample (0, i);
            }
            return 10.0 * std::log10 (sum / (blocks / 2 * block) + 1.0e-30);
        };
        std::vector<float> silence ((size_t) block, 0.0f);
        const double open = through ([] (VoiceChain&) {}, nullptr);
        const double filtered = through ([] (VoiceChain& c) { set (c, "keyHpf", 500.0f); }, nullptr);
        const double keyedShut = through ([] (VoiceChain& c) { set (c, "keyExternal", 1.0f); }, silence.data());
        const double listened = through ([] (VoiceChain& c) { set (c, "keyHpf", 500.0f); set (c, "keyListen", 1.0f); }, nullptr);
        check (open > -25.0 && filtered < -100.0, "SIDECHAIN: hum opens the gate (" + juce::String (open, 1) + " dB); with the key's 500 Hz high-pass it stays shut ("
                   + juce::String (filtered, 1) + " dB)");
        check (keyedShut < -100.0, "SIDECHAIN: keyed from a silent channel, the gate holds shut (" + juce::String (keyedShut, 1) + " dB)");
        check (listened < open - 25.0 && listened > -100.0, "SIDECHAIN: LISTEN plays the filtered key (" + juce::String (listened, 1) + " dB, the hum far down)");
    }

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (failures)) << "\n";
    return failures == 0 ? 0 : 1;
}

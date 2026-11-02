#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <atomic>
#include <vector>

// One definition of a level meter, shared by the engine (which measures) and every view (which
// only displays), so the Mixer and Routing always show the same level.
//
// The meter follows one of several industry standards, chosen app-wide. The engine computes the
// selected one per strip, pair and bus (MeterProcessor.h): each has its own measurement and
// ballistics. Views draw that value on the standard's scale, with no smoothing of their own.
//
// Values are linear amplitudes; 20*log10 gives "dB" on the standard's own axis: dBFS for the
// digital meters, sine-referenced dBFS for RMS/VU/PPM/K (a sine reads its peak level, so an
// alignment tone at -18 dBFS reads 0 VU / TEST / PPM 4), and LUFS for loudness.
namespace Metering
{
    enum class Kind { samplePeak, truePeak, rms, vu, ppm, loudness };

    struct Mark { float db; juce::String label; };
    struct Zone { float fromDb; juce::Colour colour; };

    struct Standard
    {
        juce::String id, shortName, menuName, unit;
        Kind kind;
        float attackMs = 0.0f;              // ppm: one-pole attack (from the standard's integration time)
        float releaseDbPerSecond = 0.0f;    // peak/ppm fall-off
        float windowSeconds = 0.0f;         // rms/loudness integration
        float floorDb, topDb, zeroDb;       // scale bottom and top, and the value that reads "0"
        std::vector<Mark> marks;            // printed scale, most important first
        std::vector<Zone> zones;            // colours, ascending
    };

    inline juce::Colour green()  { return juce::Colour (0xff3fcf4a); }
    inline juce::Colour yellow() { return juce::Colour (0xffeadb3a); }
    inline juce::Colour red()    { return juce::Colour (0xffff3b30); }
    inline juce::Colour amber()  { return juce::Colour (0xffffb340); }

    // Marks relative to a zero point ("+3", "0", "-6"), the usual way for aligned scales.
    inline std::vector<Mark> relativeMarks (float zeroDb, std::initializer_list<float> steps)
    {
        std::vector<Mark> m;
        for (float s : steps)
            m.push_back ({ zeroDb + s, (s > 0 ? "+" : "") + juce::String ((int) s) });
        return m;
    }

    inline std::vector<Mark> absoluteMarks (std::initializer_list<float> dbs)
    {
        std::vector<Mark> m;
        for (float db : dbs)
            m.push_back ({ db, juce::String (std::abs ((int) db)) });
        return m;
    }

    inline const std::vector<Standard>& standards()
    {
        static const std::vector<Standard> list = []
        {
            std::vector<Standard> s;

            // The default: sample peak with the fast fall most DAWs use, so meters drop promptly
            // when the sound stops. The IEC version falls at the standard's slower rate.
            s.push_back ({ "peak", "Peak", "Peak  (dBFS, fast fall 26 dB/s, as DAWs default)", "dBFS", Kind::samplePeak, 0, 26.0f, 0,
                           -60.0f, 0.0f, 0.0f, absoluteMarks ({ 0, -12, -60, -6, -20, -3, -9, -30, -40, -15 }),
                           { { -1000, green() }, { -12, yellow() }, { -3, red() } } });

            s.push_back ({ "peak-iec", "Peak IEC", "Digital Peak  (dBFS, IEC 60268-18, falls 20 dB in 1.7 s)", "dBFS", Kind::samplePeak, 0, 20.0f / 1.7f, 0,
                           -60.0f, 0.0f, 0.0f, absoluteMarks ({ 0, -12, -60, -6, -20, -3, -9, -30, -40, -15 }),
                           { { -1000, green() }, { -12, yellow() }, { -3, red() } } });

            s.push_back ({ "truepeak", "True Peak", "True Peak  (dBTP, ITU-R BS.1770, 4x oversampled)", "dBTP", Kind::truePeak, 0, 26.0f, 0,
                           -60.0f, 3.0f, 0.0f, { { 3, "+3" }, { 0, "0" }, { -1, "1" }, { -12, "12" }, { -60, "60" }, { -6, "6" }, { -20, "20" }, { -3, "3" }, { -30, "30" }, { -40, "40" } },
                           { { -1000, green() }, { -12, yellow() }, { -1, red() } } });

            s.push_back ({ "rms", "RMS", "RMS  (dBFS, AES17, 300 ms)", "dBFS", Kind::rms, 0, 0, 0.3f,
                           -60.0f, 0.0f, 0.0f, absoluteMarks ({ 0, -12, -60, -6, -20, -3, -9, -30, -40, -15 }),
                           { { -1000, green() }, { -18, yellow() }, { -6, red() } } });

            s.push_back ({ "vu", "VU", "VU  (IEC 60268-17, 300 ms, 0 VU = -18 dBFS)", "VU", Kind::vu, 0, 0, 0.3f,
                           -38.0f, -15.0f, -18.0f, relativeMarks (-18.0f, { 0, -20, 3, -10, -5, -3, 1, -7, 2, -1, -2 }),
                           { { -1000, green() }, { -18, red() } } });

            // PPMs: quasi-peak. Type II (EBU, BBC): 10 ms integration (a 10 ms burst reads 1 dB
            // low), falls 24 dB in 2.8 s. Type I (DIN, Nordic): 5 ms (reads 2 dB low), faster fall.
            s.push_back ({ "ppm-ebu", "PPM EBU", "PPM EBU  (IEC 60268-10 Type IIb, TEST = -18 dBFS)", "dB", Kind::ppm, 4.5f, 24.0f / 2.8f, 0,
                           -30.0f, -6.0f, -18.0f, { { -18, "TEST" }, { -6, "+12" }, { -30, "-12" }, { -9, "+9" }, { -26, "-8" }, { -14, "+4" }, { -22, "-4" }, { -10, "+8" } },
                           { { -1000, amber() }, { -9, red() } } });

            s.push_back ({ "ppm-bbc", "PPM BBC", "PPM BBC  (IEC 60268-10 Type IIa, PPM 4 = -18 dBFS)", "", Kind::ppm, 4.5f, 24.0f / 2.8f, 0,
                           -34.0f, -6.0f, -18.0f, { { -18, "4" }, { -6, "7" }, { -30, "1" }, { -10, "6" }, { -26, "2" }, { -14, "5" }, { -22, "3" } },
                           { { -1000, amber() }, { -10, red() } } });

            s.push_back ({ "ppm-din", "PPM DIN", "PPM DIN  (IEC 60268-10 Type I, 0 dB = -9 dBFS)", "dB", Kind::ppm, 3.2f, 20.0f / 1.5f, 0,
                           -59.0f, -4.0f, -9.0f, relativeMarks (-9.0f, { 0, -50, 5, -10, -20, -30, -5, -40, -3 }),
                           { { -1000, green() }, { -19, yellow() }, { -9, red() } } });

            s.push_back ({ "ppm-nordic", "PPM Nordic", "PPM Nordic  (IEC 60268-10 Type I, TEST = -18 dBFS)", "dB", Kind::ppm, 3.2f, 20.0f / 1.7f, 0,
                           -54.0f, -6.0f, -18.0f, { { -18, "TEST" }, { -6, "+12" }, { -54, "-36" }, { -9, "+9" }, { -30, "-12" }, { -42, "-24" }, { -24, "-6" }, { -12, "+6" } },
                           { { -1000, green() }, { -18, yellow() }, { -9, red() } } });

            for (int k : { 20, 14, 12 })
                s.push_back ({ "k" + juce::String (k), "K-" + juce::String (k), "K-" + juce::String (k) + "  (Katz, AES17 RMS, 0 = -" + juce::String (k) + " dBFS)",
                               "K", Kind::rms, 0, 0, 0.3f,
                               (float) -k - 40.0f, 0.0f, (float) -k, relativeMarks ((float) -k, { 0, -40, (float) k, -20, -10, 4, -30, 8, -6, -3 }),
                               { { -1000, green() }, { (float) -k, yellow() }, { (float) -k + 4.0f, red() } } });

            s.push_back ({ "lufs-m", "LUFS M", "Loudness, momentary  (EBU R128, LU, 0 LU = -23 LUFS)", "LU", Kind::loudness, 0, 0, 0.4f,
                           -41.0f, -14.0f, -23.0f, relativeMarks (-23.0f, { 0, 9, -18, -9, 3, -3, 6, -6, -12, -15 }),
                           { { -1000, juce::Colour (0xff3fa7cf) }, { -26, green() }, { -20, yellow() }, { -17, red() } } });
            return s;
        }();
        return list;
    }

    // The app-wide choice (an index into standards()). Read by the audio thread every block.
    inline std::atomic<int> currentStandard { 0 };

    inline const Standard& standard()
    {
        auto& list = standards();
        return list[(size_t) juce::jlimit (0, (int) list.size() - 1, currentStandard.load())];
    }

    inline void setStandard (const juce::String& id)
    {
        auto& list = standards();
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].id == id)
                currentStandard = (int) i;
    }

    inline float toDb (float value) { return juce::Decibels::gainToDecibels (value, -200.0f); }

    // What a readout shows: the value relative to the scale's zero (VU, LU, PPM dB...).
    inline float readout (float value) { return toDb (value) - standard().zeroDb; }
}

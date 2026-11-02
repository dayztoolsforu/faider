#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// Headphone correction: AutoEq's parametric EQ for a model of headphones (assets/autoeq,
// MIT licence), applied to an output so the headphones sound neutral. A profile is a preamp and
// up to ten filters (peaks and shelves). Made on the message thread; the audio thread runs it.
namespace HeadphoneEq
{
    struct Filter { char type = 'P'; float hz = 1000.0f, gainDb = 0.0f, q = 1.0f; };   // P(eak), L(ow shelf), H(igh shelf)
    struct Profile { juce::String name, measuredBy; float preampDb = 0.0f; std::vector<Filter> filters; };

    // Every bundled profile, by name (parsed once).
    const std::vector<Profile>& profiles();
    const Profile* find (const juce::String& name);

    // A profile ready to run at a sample rate: its filters for both channels.
    class Processor
    {
    public:
        Processor (const Profile& p, double rate) : name (p.name), preamp (juce::Decibels::decibelsToGain (p.preampDb))
        {
            for (auto& f : p.filters)
            {
                const auto gain = juce::Decibels::decibelsToGain (f.gainDb);
                const auto c = f.type == 'L' ? juce::IIRCoefficients::makeLowShelf (rate, f.hz, f.q, gain)
                             : f.type == 'H' ? juce::IIRCoefficients::makeHighShelf (rate, f.hz, f.q, gain)
                                             : juce::IIRCoefficients::makePeakFilter (rate, f.hz, f.q, gain);
                for (auto* side : { &left, &right })
                {
                    side->emplace_back();
                    side->back().setCoefficients (c);
                }
            }
        }

        // Audio thread: in place, both channels.
        void process (juce::AudioBuffer<float>& b, int numSamples) noexcept
        {
            b.applyGain (0, numSamples, preamp);
            for (auto& f : left)  f.processSamples (b.getWritePointer (0), numSamples);
            for (auto& f : right) f.processSamples (b.getWritePointer (1), numSamples);
        }

        const juce::String name;

    private:
        const float preamp;
        std::vector<juce::IIRFilter> left, right;
    };
}

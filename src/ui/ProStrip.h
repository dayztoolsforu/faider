#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ProStrip
{
    // A lit square key (IN, 24 dB...).
    inline void styleKey (juce::TextButton& b)
    {
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffc9cbce));        // grey square key
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xfffff0c8));      // lit cream
        b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff1d1e20));
        b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xffc32a24));        // red legend while lit
    }
}

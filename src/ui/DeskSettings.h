#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LargeFormat.h"

// The console's cards on the dark desk (SETUP's steps, the plugin panel's units): a dark panel,
// its top edge catching the light, lit grey with a blue edge when it's the one showing.
namespace DeskSettings
{
    inline juce::Colour desk()    { return juce::Colour (0xff141517); }
    inline juce::Colour cardFill(){ return Theme::get().bay; }
    inline juce::Colour text()    { return Theme::get().ink; }

    // A card: a dark panel on the desk, its top edge catching the light; `on` lit grey with a blue edge.
    inline void card (juce::Graphics& g, juce::Rectangle<float> r, bool on = false, bool hover = false)
    {
        g.setColour (on ? Theme::get().chromeOn : hover ? juce::Colour (0xff34373b) : cardFill());
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (hover ? Theme::get().accent.withAlpha (0.7f) : juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (on ? 0.14f : 0.05f));
        g.fillRect (r.reduced (4.0f, 0.0f).withHeight (1.0f).translated (0.0f, 1.0f));
        if (on)
        {
            g.setColour (Theme::get().accent);
            g.fillRoundedRectangle (r.withWidth (3.0f), 1.5f);
        }
    }
}

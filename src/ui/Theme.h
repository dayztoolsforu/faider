#pragma once

#include <juce_graphics/juce_graphics.h>

// The console's look: every colour the program paints with, in one place. Every view reads it
// (Theme::get()) when it paints. It copies a style of desk, never a maker's logos or names.
namespace Theme
{
    struct Style
    {
        // Panels and printing.
        juce::Colour background;   // behind the strips and modules
        juce::Colour panel, panelDark, rim, legend, legendDim;
        juce::Colour slot;         // recessed fields: selectors, plugin slots
        juce::Colour namePlate;

        // Keys.
        juce::Colour capTop, capBottom, capText;

        // Faders' printed plate.
        juce::Colour plateColour;

        // Readout screens.
        juce::Colour screenLit, screenGlass, screenBezel;

        // Knobs' colours by function.
        juce::Colour knobGrey, knobRed, knobGreen, knobBlue, knobBrown;

        // LEDs and lamps.
        juce::Colour ledGreen, ledYellow, ledRed, ledBlue, muteLamp, soloLamp, powerLamp;

        // The printing's typeface: one Windows ships with, with a real bold and crisp at small sizes.
        juce::String font;

        // Keys' corners.
        float keyRadius;

        // The light of the one you're working on (a tab, SEL, a chosen option).
        juce::Colour selection;
    };

    // A classic large-format studio console: flat charcoal modules printed in white, square keys
    // that light amber, knob caps by section (HF red, HMF green, LMF blue, LF black, dynamics and
    // sends grey), green LEDs, long faders on a dark bay.
    inline const Style& get()
    {
        static const Style style
        {
            juce::Colour (0xff141416), juce::Colour (0xff1e1e20), juce::Colour (0xff19191b), juce::Colour (0xff0b0b0c),
            juce::Colour (0xffdadcdd), juce::Colour (0xff8a9398), juce::Colour (0xff151618), juce::Colour (0xff2b2c30),
            juce::Colour (0xffc9ccd0), juce::Colour (0xffa6aaae), juce::Colour (0xff1d1f22),
            juce::Colour (0xff2a2b2f),
            juce::Colour (0xfff4f6f8), juce::Colour (0xff111316), juce::Colour (0xff141518),
            juce::Colour (0xffb8bcc1), juce::Colour (0xffd2352b), juce::Colour (0xff3f9a52), juce::Colour (0xff2f68b8), juce::Colour (0xff1f2023),
            juce::Colour (0xff4cff6a), juce::Colour (0xffffc83a), juce::Colour (0xffff3b30), juce::Colour (0xff3d8fe0),
            juce::Colour (0xffff4a3a), juce::Colour (0xffffd84a), juce::Colour (0xfffff0c8),   // cut red, solo yellow, power cream
            "Segoe UI", 2.0f,
            juce::Colour (0xffffc83a)   // lit keys amber
        };
        return style;
    }
}

#pragma once

#include <juce_graphics/juce_graphics.h>

// The console's look: every colour the program paints with, in one place. Every view reads it
// (Theme::get()) when it paints. It copies a style of desk, never a maker's logos or names.
//
// The look in force is a set of tokens. It starts as the one built into the program and can be
// replaced by a file in %APPDATA%\Faider\looks, so a different set of colours, a different
// typeface and different measures can be written by hand and put in place. Whatever a file
// leaves out falls back to the built-in, so a part-written file still works.
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

        // The light of the one you're working on (a tab, SEL, a chosen option).
        juce::Colour selection;

        // The shading the panels are drawn with: the shadow under a module, the light on its edge.
        juce::Colour shadow, highlight;

        // The printing's typeface: one Windows ships with, with a real bold and crisp at small sizes.
        juce::String font;
        float fontLegend, fontCaption, fontReadout;

        // Keys' corners.
        float keyRadius;
    };

    // The look built into the program.
    const Style& defaultStyle();

    // The look in force.
    const Style& get();

    // The name of the look in force ("Default" for the built-in one).
    juce::String currentLookName();

    // A number that changes whenever the look in force does. Views that draw only what changed
    // keep it in what they remember, so a new look is drawn in full.
    int generation();

    // The folder the looks are kept in: %APPDATA%\Faider\looks.
    juce::File looksFolder();

    // The looks that can be put in force: the built-in one, then every file in the folder.
    juce::StringArray lookNames();

    // Put a look in force by name ("Default" is the built-in one). False if there is no such
    // look, in which case the look in force stays as it is.
    bool useLook (const juce::String& name);

    // Write the look in force to the folder under a new name.
    bool saveLookAs (const juce::String& name);

    // Read a look from JSON: the colours as "#rrggbb" or "#rrggbbaa". Whatever the text leaves
    // out falls back to the built-in. The built-in look in force is never changed by this.
    Style styleFromJson (const juce::String& json);

    // The look in force as JSON, in the shape styleFromJson reads.
    juce::String styleToJson (const Style& style, const juce::String& name);
}

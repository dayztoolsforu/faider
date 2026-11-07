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

        // A knob's own metal: the cap's top and bottom, and the skirt it stands on.
        juce::Colour knobTop, knobBottom, skirtTop, skirtBottom;

        // The VU meter's cream face (top and bottom of its gradient) and the printing on it.
        juce::Colour vuTop, vuBottom, vuInk;

        // The tape's screen: its dark glass and the mint it prints in.
        juce::Colour tapeGlass, tapeInk;

        // LEDs and lamps.
        juce::Colour ledGreen, ledYellow, ledRed, ledBlue, muteLamp, soloLamp, powerLamp;

        // The light of the one you're working on (a tab, SEL, a chosen option).
        juce::Colour selection;

        // The desk's own blue: lit edges, a key under the pointer, the rail of the page showing.
        juce::Colour accent;

        // Printing on the desk's chrome (not the legends, which sit above the modules).
        juce::Colour ink;         // text on a key that is not lit
        juce::Colour inkDim;      // labels printed on the sidebar and the rails
        juce::Colour litInk;      // the dark ink a lit key prints with
        juce::Colour inkBright;   // the brightest printing: plugin names, a combinator's path
        juce::Colour inkFaint;    // printing on a control that is off

        // The darks the desk sinks into.
        juce::Colour recess;      // the deepest: the wells behind screens and buttons
        juce::Colour bay;         // the plate a plugin list or a slot sits on
        juce::Colour bayDark;     // the same, dimmed
        juce::Colour rail;        // the rail behind the strips, the sidebar's edge

        // A card that is not lit: off, under the pointer, and showing.
        juce::Colour chromeOff, chromeHover, chromeOn;

        // The transport's lamps and the load light: a green that says running, a red that says stop.
        juce::Colour runLamp, stopLamp;

        // What a channel, a bus and an output look like: the name bar's colour, and the wash a
        // selected one gets. Everything else about a strip comes from these.
        juce::Colour kindInput, kindBus, kindOutput;

        // The strip's own body: the Light strip's panel, the Dark strip's panel, and the section
        // a name bar sits on.
        juce::Colour stripLight, stripDark, stripBody;

        // The colours a layout file can name for a part (`"colour": "teal"`).
        juce::Colour palRed, palGreen, palBlue, palBlack, palWhite, palViolet, palGrey,
                     palCream, palOrange, palAmber, palTeal, palSalmon;

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

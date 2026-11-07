// Checks the look a theme file describes (src/ui/Theme.h): the built-in look goes through JSON
// and comes back the same, a part-written file keeps the built-in's rest, a broken file leaves
// the built-in alone, a colour with an alpha keeps it, and the palette a layout file names can be
// repainted. It also pins the built-in's own values, so the default desk cannot be re-skinned by
// accident. Prints PASS/FAIL per check; exits non-zero on any failure.
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include "../src/ui/Theme.h"
#include <iostream>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    }

    bool same (juce::Colour a, juce::Colour b)
    {
        return a.getARGB() == b.getARGB();
    }
}

int main()
{
    const auto& builtIn = Theme::defaultStyle();

    // The built-in look, written out and read back: the same look.
    {
        const auto json = Theme::styleToJson (builtIn, "Round trip");
        const auto back = Theme::styleFromJson (json);

        check (same (back.background, builtIn.background), "the background comes back");
        check (same (back.panel, builtIn.panel), "the panel comes back");
        check (same (back.panelDark, builtIn.panelDark), "the dark panel comes back");
        check (same (back.rim, builtIn.rim), "the rim comes back");
        check (same (back.legend, builtIn.legend), "the printing comes back");
        check (same (back.legendDim, builtIn.legendDim), "the dim printing comes back");
        check (same (back.slot, builtIn.slot), "the recessed fields come back");
        check (same (back.namePlate, builtIn.namePlate), "the name plate comes back");
        check (same (back.capTop, builtIn.capTop), "the key's top comes back");
        check (same (back.capBottom, builtIn.capBottom), "the key's bottom comes back");
        check (same (back.capText, builtIn.capText), "the key's legend comes back");
        check (same (back.plateColour, builtIn.plateColour), "the fader plate comes back");
        check (same (back.screenLit, builtIn.screenLit), "the readout's light comes back");
        check (same (back.screenGlass, builtIn.screenGlass), "the readout's glass comes back");
        check (same (back.screenBezel, builtIn.screenBezel), "the readout's bezel comes back");
        check (same (back.knobGrey, builtIn.knobGrey), "the grey knobs come back");
        check (same (back.knobRed, builtIn.knobRed), "the red knobs come back");
        check (same (back.knobGreen, builtIn.knobGreen), "the green knobs come back");
        check (same (back.knobBlue, builtIn.knobBlue), "the blue knobs come back");
        check (same (back.knobBrown, builtIn.knobBrown), "the brown knobs come back");
        check (same (back.ledGreen, builtIn.ledGreen), "the green LEDs come back");
        check (same (back.ledYellow, builtIn.ledYellow), "the yellow LEDs come back");
        check (same (back.ledRed, builtIn.ledRed), "the red LEDs come back");
        check (same (back.ledBlue, builtIn.ledBlue), "the blue LEDs come back");
        check (same (back.muteLamp, builtIn.muteLamp), "the cut lamp comes back");
        check (same (back.soloLamp, builtIn.soloLamp), "the solo lamp comes back");
        check (same (back.powerLamp, builtIn.powerLamp), "the power lamp comes back");
        check (same (back.selection, builtIn.selection), "the selection comes back");
        check (same (back.accent, builtIn.accent), "the desk's blue comes back");
        check (same (back.ink, builtIn.ink), "the chrome printing comes back");
        check (same (back.inkDim, builtIn.inkDim), "the dim chrome printing comes back");
        check (same (back.litInk, builtIn.litInk), "the lit key's ink comes back");
        check (same (back.recess, builtIn.recess), "the deepest dark comes back");
        check (same (back.inkBright, builtIn.inkBright), "the bright printing comes back");
        check (same (back.inkFaint, builtIn.inkFaint), "the faint printing comes back");
        check (same (back.kindInput, builtIn.kindInput), "a channel's name bar comes back");
        check (same (back.kindBus, builtIn.kindBus), "a bus's name bar comes back");
        check (same (back.kindOutput, builtIn.kindOutput), "an output's name bar comes back");
        check (same (back.stripLight, builtIn.stripLight), "the Light strip's panel comes back");
        check (same (back.stripDark, builtIn.stripDark), "the Dark strip's panel comes back");
        check (same (back.stripBody, builtIn.stripBody), "the strip's body comes back");
        check (same (back.knobTop, builtIn.knobTop), "the knob cap's top comes back");
        check (same (back.knobBottom, builtIn.knobBottom), "the knob cap's bottom comes back");
        check (same (back.skirtTop, builtIn.skirtTop), "the skirt's top comes back");
        check (same (back.skirtBottom, builtIn.skirtBottom), "the skirt's bottom comes back");
        check (same (back.vuTop, builtIn.vuTop), "the VU's face comes back");
        check (same (back.vuBottom, builtIn.vuBottom), "the VU's face at the bottom comes back");
        check (same (back.vuInk, builtIn.vuInk), "the VU's printing comes back");
        check (same (back.tapeGlass, builtIn.tapeGlass), "the tape's screen comes back");
        check (same (back.tapeInk, builtIn.tapeInk), "the tape's printing comes back");
        check (same (back.bay, builtIn.bay), "the plugin bay comes back");
        check (same (back.bayDark, builtIn.bayDark), "the dimmed bay comes back");
        check (same (back.rail, builtIn.rail), "the rail comes back");
        check (same (back.chromeOff, builtIn.chromeOff), "an off card comes back");
        check (same (back.chromeHover, builtIn.chromeHover), "a hovered card comes back");
        check (same (back.chromeOn, builtIn.chromeOn), "a showing card comes back");
        check (same (back.runLamp, builtIn.runLamp), "the running lamp comes back");
        check (same (back.stopLamp, builtIn.stopLamp), "the stop lamp comes back");
        check (same (back.palRed, builtIn.palRed), "the palette's red comes back");
        check (same (back.palGreen, builtIn.palGreen), "the palette's green comes back");
        check (same (back.palBlue, builtIn.palBlue), "the palette's blue comes back");
        check (same (back.palBlack, builtIn.palBlack), "the palette's black comes back");
        check (same (back.palWhite, builtIn.palWhite), "the palette's white comes back");
        check (same (back.palViolet, builtIn.palViolet), "the palette's violet comes back");
        check (same (back.palGrey, builtIn.palGrey), "the palette's grey comes back");
        check (same (back.palCream, builtIn.palCream), "the palette's cream comes back");
        check (same (back.palOrange, builtIn.palOrange), "the palette's orange comes back");
        check (same (back.palAmber, builtIn.palAmber), "the palette's amber comes back");
        check (same (back.palTeal, builtIn.palTeal), "the palette's teal comes back");
        check (same (back.palSalmon, builtIn.palSalmon), "the palette's salmon comes back");
        check (same (back.shadow, builtIn.shadow), "the shadow comes back");
        check (same (back.highlight, builtIn.highlight), "the highlight comes back");
        check (back.font == builtIn.font, "the typeface comes back");
        check (back.keyRadius == builtIn.keyRadius, "the keys' corners come back");
        check (back.fontLegend == builtIn.fontLegend
            && back.fontCaption == builtIn.fontCaption
            && back.fontReadout == builtIn.fontReadout, "the type sizes come back");
    }

    // A file with one colour in it: that colour is used, the rest stays as the built-in.
    // A colour's alpha comes last, as in a web page: "#rrggbbaa".
    {
        const auto json = "{ \"colours\": { \"background\": \"#102030\" } }";
        const auto back = Theme::styleFromJson (json);
        check (same (back.background, juce::Colour (0xff102030)), "the colour in the file is used");
        check (same (back.panel, builtIn.panel), "what the file leaves out stays as the built-in");
        check (back.font == builtIn.font, "the typeface stays as the built-in's");
    }

    // A file that is not a look at all: the built-in stands.
    {
        const auto back = Theme::styleFromJson ("{ this is not json ");
        check (same (back.background, builtIn.background), "a broken file leaves the built-in alone");
        check (same (back.panel, builtIn.panel), "a broken file leaves the built-in's panels alone");
    }

    // A colour with an alpha: it keeps it.
    {
        const auto json = "{ \"colours\": { \"shadow\": \"#00000080\" } }";
        const auto back = Theme::styleFromJson (json);
        check (same (back.shadow, juce::Colour (0x80000000)), "the alpha comes back");
    }

    // A colour that cannot be read: the built-in's is kept.
    {
        const auto json = "{ \"colours\": { \"background\": \"not a colour\" } }";
        const auto back = Theme::styleFromJson (json);
        check (same (back.background, builtIn.background), "a colour that can't be read is passed over");
    }

    // The built-in look's own values, so a future edit cannot quietly re-skin the default: the
    // desk's colours were these before they were tokens, and they are still these.
    {
        check (same (builtIn.accent, juce::Colour (0xff3db4e0)), "the accent is the desk's blue");
        check (same (builtIn.selection, juce::Colour (0xffffc83a)), "the selection is lit amber");
        check (same (builtIn.ink, juce::Colour (0xffc9cbce)), "the chrome prints in pale grey");
        check (same (builtIn.recess, juce::Colour (0xff0a0a0b)), "the recess is near black");
        check (same (builtIn.chromeOn, juce::Colour (0xff4a4d52)), "a showing card is mid grey");
        check (same (builtIn.palTeal, juce::Colour (0xff62d6b4)), "the palette's teal is the feeds'");
        check (same (builtIn.palOrange, juce::Colour (0xfff2a03c)), "the palette's orange is the pads'");
        check (same (builtIn.kindInput, juce::Colour (0xff3d8fd1)), "a channel's bar is the blue it was");
        check (same (builtIn.kindBus, juce::Colour (0xff9a6ad1)), "a bus's bar is the violet it was");
        check (same (builtIn.kindOutput, juce::Colour (0xffd18a3d)), "an output's bar is the amber it was");
        check (same (builtIn.stripLight, juce::Colour (0xff78797f)), "the Light strip's panel is the grey it was");
        check (same (builtIn.stripDark, juce::Colour (0xff222021)), "the Dark strip's panel is the near-black it was");
        check (same (builtIn.knobTop, juce::Colour (0xffc4c6c9)), "the knob cap is the grey it was");
        check (same (builtIn.vuTop, juce::Colour (0xfff1ead2)), "the VU's face is the cream it was");
        check (same (builtIn.tapeInk, juce::Colour (0xff6fe3a8)), "the tape prints in the mint it was");
    }

    // A look file that repaints the palette a layout names: the parts the layouts colour follow it,
    // and the colours the file does not name stay as the built-in.
    {
        const auto json = "{ \"palette\": { \"teal\": \"#00ff00\", \"orange\": \"#ff0000\" } }";
        const auto back = Theme::styleFromJson (json);
        check (same (back.palTeal, juce::Colour (0xff00ff00)), "a layout's teal comes from the file");
        check (same (back.palOrange, juce::Colour (0xffff0000)), "a layout's orange comes from the file");
        check (same (back.palSalmon, builtIn.palSalmon), "a palette colour the file leaves out stays");
        check (same (back.accent, builtIn.accent), "the rest of the look stays as the built-in's");
    }

    // The accent, taken from a file and put in force: the desk's blue is not written in by hand.
    {
        const auto json = "{ \"colours\": { \"accent\": \"#ff0000\" } }";
        const auto back = Theme::styleFromJson (json);
        check (same (back.accent, juce::Colour (0xffff0000)), "the accent can be set by a look file");
    }

    std::cout << (failures == 0 ? "All passed.\n" : juce::String (failures) + " failed.\n");
    return failures;
}

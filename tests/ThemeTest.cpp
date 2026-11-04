// Checks the look a theme file describes (src/ui/Theme.h): the built-in look goes through JSON
// and comes back the same, a part-written file keeps the built-in's rest, a broken file leaves
// the built-in alone, and a colour with an alpha keeps it. Prints PASS/FAIL per check; exits
// non-zero on any failure.
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

    std::cout << (failures == 0 ? "All passed.\n" : juce::String (failures) + " failed.\n");
    return failures;
}

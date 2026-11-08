// Proves the desk paints from the look rather than from colours written in by hand: it paints a
// strip's name bar, body and rule the way the console does (console/src/Console.cpp), once under the
// built-in look and once under a look file that changes a single colour. The pixels that must move
// move, and the ones that must not, do not. Prints PASS/FAIL per check; exits non-zero on any failure.
// The look files go in this target's own data folder (FAIDER_DATA_FOLDER="FaiderLookTest", in
// %APPDATA%), never Faider's, and that folder is deleted at the end.
#include <juce_gui_basics/juce_gui_basics.h>
#include "../src/ProductName.h"
#include "../src/ui/Theme.h"
#include <iostream>
#include <string_view>

static_assert (std::string_view (FAIDER_DATA_FOLDER) != "Faider", "LookRenderTest must not write in Faider's own folder");

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    }

    constexpr int w = 120, h = 60;

    // The strip's top: a name bar (18 tall), its body below, and the rule under the body. The
    // expressions are the console's, so a colour written in by hand here would show as a mismatch.
    juce::Image paint (bool selected, bool output)
    {
        juce::Image img (juce::Image::ARGB, w, h, true);
        juce::Graphics g (img);

        g.setColour (selected ? Theme::get().accent.withMultipliedBrightness (0.60f)
                  : output    ? Theme::get().kindOutput.withMultipliedBrightness (0.36f)
                              : Theme::get().stripBody);
        g.fillRect (0.0f, 0.0f, (float) w, 18.0f);

        g.setColour (Theme::get().stripDark);
        g.fillRect (0.0f, 18.0f, (float) w, 40.0f);

        g.setColour (Theme::get().stripLight.withMultipliedBrightness (0.68f));
        g.fillRect (0.0f, 58.0f, (float) w, 2.0f);
        return img;
    }

    juce::Colour at (const juce::Image& img, int x, int y) { return img.getPixelAt (x, y); }

    bool same (juce::Colour a, juce::Colour b) { return a.getARGB() == b.getARGB(); }

    // Put a look file in place, painted from the built-in with one colour changed.
    bool useLook (const juce::String& name, const juce::String& key, const juce::String& hex)
    {
        const auto folder = Theme::looksFolder();
        folder.createDirectory();
        const auto json = "{ \"name\": \"" + name + "\", \"colours\": { \"" + key + "\": \"" + hex + "\" } }";
        if (! folder.getChildFile (name + ".json").replaceWithText (json))
            return false;
        return Theme::useLook (name);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    Theme::useLook ("Default");

    const auto& builtIn = Theme::defaultStyle();
    const auto channel = paint (false, false);
    const auto output = paint (false, true);
    const auto selected = paint (true, false);

    // The built-in desk paints what it always painted: the amber of an output's bar, darkened; the
    // strip's own body; the Dark panel; and the Light strip's rule.
    check (same (at (output, 10, 5), builtIn.kindOutput.withMultipliedBrightness (0.36f)),
           "an output's name bar is the look's amber, darkened");
    check (same (at (channel, 10, 5), builtIn.stripBody), "a channel with no colour of its own shows the strip's body");
    check (same (at (channel, 10, 30), builtIn.stripDark), "a dark section is the look's dark strip");
    check (same (at (channel, 10, 59), builtIn.stripLight.withMultipliedBrightness (0.68f)),
           "the rule under a section is the Light strip's panel, darkened");
    check (same (at (selected, 10, 5), builtIn.accent.withMultipliedBrightness (0.60f)),
           "the channel in hand is the look's accent, darkened");

    // One colour changed in a look file: what it paints moves, and nothing else does.
    {
        const juce::String hex = "#00ff00";
        if (! useLook ("LookTest-Kind", "kindOutput", hex))
        {
            check (false, "the look file could be put in force");
        }
        else
        {
            const auto after = paint (false, true);
            const auto plain = paint (false, false);
            check (at (after, 10, 5) != at (output, 10, 5), "a look's kindOutput repaints an output's name bar");
            check (same (at (after, 10, 5), juce::Colour (0xff00ff00).withMultipliedBrightness (0.36f)),
                   "and it is that amber's own darkening, not a colour written in");
            check (same (at (after, 10, 30), builtIn.stripDark), "the section body is untouched by it");
            check (same (at (plain, 10, 5), builtIn.stripBody), "a channel's bar is untouched by it");
        }
    }

    {
        if (useLook ("LookTest-Accent", "accent", "#ff0000"))
        {
            const auto after = paint (true, false);
            check (at (after, 10, 5) != at (selected, 10, 5), "a look's accent repaints the channel in hand");
            check (same (at (after, 10, 30), builtIn.stripDark), "and only that");
        }
        else
            check (false, "the accent look could be put in force");
    }

    {
        if (useLook ("LookTest-Strip", "stripDark", "#403020"))
        {
            const auto after = paint (false, false);
            check (same (at (after, 10, 30), juce::Colour (0xff403020)), "a look's stripDark repaints the section");
            check (same (at (after, 10, 5), builtIn.stripBody), "the name bar keeps its own colour");
        }
        else
            check (false, "the strip look could be put in force");
    }

    Theme::useLook ("Default");
    Product::dataFolder().deleteRecursively();   // (the test's own folder: nothing else is in it)
    check (! Product::dataFolder().exists(), "the test's own folder is gone afterwards");

    std::cout << (failures == 0 ? "All passed.\n" : juce::String (failures) + " failed.\n");
    return failures;
}

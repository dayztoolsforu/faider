#include "Theme.h"
#include "../ProductName.h"

namespace Theme
{
    namespace
    {
        Style makeDefault()
        {
            // A classic large-format studio console: flat charcoal modules printed in white, square
            // keys that light amber, knob caps by section (HF red, HMF green, LMF blue, LF black,
            // dynamics and sends grey), green LEDs, long faders on a dark bay.
            Style s;
            s.background = juce::Colour (0xff141416);
            s.panel = juce::Colour (0xff1e1e20);
            s.panelDark = juce::Colour (0xff19191b);
            s.rim = juce::Colour (0xff0b0b0c);
            s.legend = juce::Colour (0xffdadcdd);
            s.legendDim = juce::Colour (0xff8a9398);
            s.slot = juce::Colour (0xff151618);
            s.namePlate = juce::Colour (0xff2b2c30);

            s.capTop = juce::Colour (0xffc9ccd0);
            s.capBottom = juce::Colour (0xffa6aaae);
            s.capText = juce::Colour (0xff1d1f22);

            s.plateColour = juce::Colour (0xff2a2b2f);

            s.screenLit = juce::Colour (0xfff4f6f8);
            s.screenGlass = juce::Colour (0xff111316);
            s.screenBezel = juce::Colour (0xff141518);

            s.knobGrey = juce::Colour (0xffb8bcc1);
            s.knobRed = juce::Colour (0xffd2352b);
            s.knobGreen = juce::Colour (0xff3f9a52);
            s.knobBlue = juce::Colour (0xff2f68b8);
            s.knobBrown = juce::Colour (0xff1f2023);

            s.knobTop = juce::Colour (0xffc4c6c9);
            s.knobBottom = juce::Colour (0xff85878b);
            s.skirtTop = juce::Colour (0xff6e6f73);
            s.skirtBottom = juce::Colour (0xff48494c);

            s.vuTop = juce::Colour (0xfff1ead2);
            s.vuBottom = juce::Colour (0xffd8ceaa);
            s.vuInk = juce::Colour (0xff2a2a2a);

            s.tapeGlass = juce::Colour (0xff0e1713);
            s.tapeInk = juce::Colour (0xff6fe3a8);

            s.ledGreen = juce::Colour (0xff4cff6a);
            s.ledYellow = juce::Colour (0xffffc83a);
            s.ledRed = juce::Colour (0xffff3b30);
            s.ledBlue = juce::Colour (0xff3d8fe0);
            s.muteLamp = juce::Colour (0xffff4a3a);   // cut red
            s.soloLamp = juce::Colour (0xffffd84a);   // solo yellow
            s.powerLamp = juce::Colour (0xfffff0c8);  // power cream

            s.selection = juce::Colour (0xffffc83a);  // lit keys amber

            s.accent = juce::Colour (0xff3db4e0);     // the desk's blue

            s.ink = juce::Colour (0xffc9cbce);
            s.inkDim = juce::Colour (0xff9aa0a6);
            s.litInk = juce::Colour (0xff10201b);
            s.inkBright = juce::Colour (0xffd8dadc);
            s.inkFaint = juce::Colour (0xffa6a9ae);

            s.recess = juce::Colour (0xff0a0a0b);
            s.bay = juce::Colour (0xff222427);
            s.bayDark = juce::Colour (0xff1c1d20);
            s.rail = juce::Colour (0xff17181a);

            s.chromeOff = juce::Colour (0xff313337);
            s.chromeHover = juce::Colour (0xff3f4247);
            s.chromeOn = juce::Colour (0xff4a4d52);

            s.runLamp = juce::Colour (0xff4fd07a);
            s.stopLamp = juce::Colour (0xffef5a4f);

            s.kindInput = juce::Colour (0xff3d8fd1);
            s.kindBus = juce::Colour (0xff9a6ad1);
            s.kindOutput = juce::Colour (0xffd18a3d);

            s.stripLight = juce::Colour (0xff78797f);
            s.stripDark = juce::Colour (0xff222021);
            s.stripBody = juce::Colour (0xff16181a);

            s.palRed = juce::Colour (0xff8e1f22);     s.palGreen = juce::Colour (0xff3d7a3f);
            s.palBlue = juce::Colour (0xff2c78c0);    s.palBlack = juce::Colour (0xff2b2c2f);
            s.palWhite = juce::Colour (0xffc9cacc);   s.palViolet = juce::Colour (0xffb35fa4);
            s.palGrey = juce::Colour (0xff5d5e62);    s.palCream = juce::Colour (0xfffff0c8);
            s.palOrange = juce::Colour (0xfff2a03c);  s.palAmber = juce::Colour (0xfff7a64a);
            s.palTeal = juce::Colour (0xff62d6b4);    s.palSalmon = juce::Colour (0xffe8a587);

            s.shadow = juce::Colour (0x73000000);
            s.highlight = juce::Colour (0x33ffffff);

            s.font = "Segoe UI";
            s.fontLegend = 9.0f;
            s.fontCaption = 8.0f;
            s.fontReadout = 11.0f;

            s.keyRadius = 2.0f;
            return s;
        }

        const Style& theDefault()
        {
            static const Style style = makeDefault();
            return style;
        }

        Style& theCurrent()
        {
            static Style style = makeDefault();
            return style;
        }

        juce::String& theCurrentName()
        {
            static juce::String name { "Default" };
            return name;
        }

        int& theGeneration()
        {
            static int generation = 0;
            return generation;
        }

        // A colour from the file: "#rrggbb" or "#rrggbbaa", the alpha last, as in a web page.
        // The fallback if it can't be read.
        juce::Colour colourFrom (const juce::String& text, juce::Colour fallback)
        {
            auto digits = text.trim().toLowerCase();
            if (digits.startsWith ("#"))
                digits = digits.substring (1);
            if (digits.length() != 6 && digits.length() != 8)
                return fallback;

            juce::uint32 value = 0;
            for (auto c : digits)
            {
                int d = 0;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else return fallback;
                value = (value << 4) | (juce::uint32) d;
            }

            if (digits.length() == 6)
                return juce::Colour (0xff000000u | value);   // opaque

            // "#rrggbbaa": the alpha comes last in the file and first in a colour, so it moves.
            return juce::Colour (((value & 0xffu) << 24)
                              | (((value >> 24) & 0xffu) << 16)
                              | (((value >> 16) & 0xffu) << 8)
                              | ((value >> 8) & 0xffu));
        }

        // A property of an object, or a var() if it has none.
        juce::var propertyOf (const juce::var& object, const juce::String& name)
        {
            if (! object.isObject() || object.getObject() == nullptr)
                return {};
            const auto* obj = dynamic_cast<const juce::DynamicObject*> (object.getObject());
            return obj != nullptr ? obj->getProperty (juce::Identifier (name)) : juce::var();
        }

        // A colour from an object's "colours" group.
        juce::Colour colourFrom (const juce::var& colours, const juce::String& name, juce::Colour fallback)
        {
            const auto v = propertyOf (colours, name);
            return v.isString() ? colourFrom (v.toString(), fallback) : fallback;
        }

        float floatFrom (const juce::var& object, const juce::String& name, float fallback)
        {
            const auto v = propertyOf (object, name);
            if (v.isDouble() || v.isInt())
                return (float) v;
            if (v.isString())
                return (float) v.toString().getFloatValue();
            return fallback;
        }

        juce::String stringFrom (const juce::var& object, const juce::String& name, const juce::String& fallback)
        {
            const auto v = propertyOf (object, name);
            return v.isString() ? v.toString() : fallback;
        }

        // A byte as two hex digits.
        juce::String byteToText (juce::uint32 value)
        {
            static const char* digits = "0123456789abcdef";
            const auto v = value & 0xffu;
            juce::String text;
            text += juce::String::charToString (digits[(v >> 4) & 0xf]);
            text += juce::String::charToString (digits[v & 0xf]);
            return text;
        }

        // A colour as "#rrggbb", with the alpha last ("#rrggbbaa") when it has one. The same
        // order a colour has in a web page, so a look file reads as people expect.
        juce::String colourToText (juce::Colour c)
        {
            const auto argb = (juce::uint32) c.getARGB();
            auto text = "#" + byteToText (argb >> 16) + byteToText (argb >> 8) + byteToText (argb);
            if (c.getAlpha() != 255)
                text += byteToText (argb >> 24);
            return text;
        }
    }

    const Style& defaultStyle() { return theDefault(); }
    const Style& get() { return theCurrent(); }
    juce::String currentLookName() { return theCurrentName(); }
    int generation() { return theGeneration(); }

    juce::File looksFolder()
    {
        return Product::dataFolder().getChildFile ("looks");
    }

    juce::StringArray lookNames()
    {
        juce::StringArray names { "Default" };
        const auto folder = looksFolder();
        if (folder.isDirectory())
            for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*.json"))
                if (entry.getFile().getFileNameWithoutExtension() != "Default")
                    names.add (entry.getFile().getFileNameWithoutExtension());
        return names;
    }

    bool useLook (const juce::String& name)
    {
        if (name.isEmpty())
            return false;

        if (name == "Default")
        {
            theCurrent() = theDefault();
            theCurrentName() = "Default";
            ++theGeneration();
            return true;
        }

        const auto file = looksFolder().getChildFile (name + ".json");
        if (! file.existsAsFile())
            return false;

        const auto text = file.loadFileAsString();
        juce::var parsed;
        if (juce::JSON::parse (text, parsed).failed() || ! parsed.isObject())
            return false;

        theCurrent() = styleFromJson (text);
        theCurrentName() = name;
        ++theGeneration();
        return true;
    }

    bool saveLookAs (const juce::String& name)
    {
        const auto clean = name.trim();
        if (clean.isEmpty() || clean == "Default")
            return false;

        const auto folder = looksFolder();
        if (! folder.createDirectory())
            return false;

        return folder.getChildFile (clean + ".json").replaceWithText (styleToJson (theCurrent(), clean));
    }

    Style styleFromJson (const juce::String& json)
    {
        Style s = theDefault();
        juce::var parsed;
        if (juce::JSON::parse (json, parsed).failed() || ! parsed.isObject())
            return s;

        const auto colours = propertyOf (parsed, "colours");
        s.background = colourFrom (colours, "background", s.background);
        s.panel = colourFrom (colours, "panel", s.panel);
        s.panelDark = colourFrom (colours, "panelDark", s.panelDark);
        s.rim = colourFrom (colours, "rim", s.rim);
        s.legend = colourFrom (colours, "legend", s.legend);
        s.legendDim = colourFrom (colours, "legendDim", s.legendDim);
        s.slot = colourFrom (colours, "slot", s.slot);
        s.namePlate = colourFrom (colours, "namePlate", s.namePlate);
        s.capTop = colourFrom (colours, "capTop", s.capTop);
        s.capBottom = colourFrom (colours, "capBottom", s.capBottom);
        s.capText = colourFrom (colours, "capText", s.capText);
        s.plateColour = colourFrom (colours, "plateColour", s.plateColour);
        s.screenLit = colourFrom (colours, "screenLit", s.screenLit);
        s.screenGlass = colourFrom (colours, "screenGlass", s.screenGlass);
        s.screenBezel = colourFrom (colours, "screenBezel", s.screenBezel);
        s.knobGrey = colourFrom (colours, "knobGrey", s.knobGrey);
        s.knobRed = colourFrom (colours, "knobRed", s.knobRed);
        s.knobGreen = colourFrom (colours, "knobGreen", s.knobGreen);
        s.knobBlue = colourFrom (colours, "knobBlue", s.knobBlue);
        s.knobBrown = colourFrom (colours, "knobBrown", s.knobBrown);
        s.knobTop = colourFrom (colours, "knobTop", s.knobTop);
        s.knobBottom = colourFrom (colours, "knobBottom", s.knobBottom);
        s.skirtTop = colourFrom (colours, "skirtTop", s.skirtTop);
        s.skirtBottom = colourFrom (colours, "skirtBottom", s.skirtBottom);
        s.vuTop = colourFrom (colours, "vuTop", s.vuTop);
        s.vuBottom = colourFrom (colours, "vuBottom", s.vuBottom);
        s.vuInk = colourFrom (colours, "vuInk", s.vuInk);
        s.tapeGlass = colourFrom (colours, "tapeGlass", s.tapeGlass);
        s.tapeInk = colourFrom (colours, "tapeInk", s.tapeInk);
        s.ledGreen = colourFrom (colours, "ledGreen", s.ledGreen);
        s.ledYellow = colourFrom (colours, "ledYellow", s.ledYellow);
        s.ledRed = colourFrom (colours, "ledRed", s.ledRed);
        s.ledBlue = colourFrom (colours, "ledBlue", s.ledBlue);
        s.muteLamp = colourFrom (colours, "muteLamp", s.muteLamp);
        s.soloLamp = colourFrom (colours, "soloLamp", s.soloLamp);
        s.powerLamp = colourFrom (colours, "powerLamp", s.powerLamp);
        s.selection = colourFrom (colours, "selection", s.selection);
        s.accent = colourFrom (colours, "accent", s.accent);
        s.ink = colourFrom (colours, "ink", s.ink);
        s.inkDim = colourFrom (colours, "inkDim", s.inkDim);
        s.litInk = colourFrom (colours, "litInk", s.litInk);
        s.inkBright = colourFrom (colours, "inkBright", s.inkBright);
        s.inkFaint = colourFrom (colours, "inkFaint", s.inkFaint);
        s.recess = colourFrom (colours, "recess", s.recess);
        s.bay = colourFrom (colours, "bay", s.bay);
        s.bayDark = colourFrom (colours, "bayDark", s.bayDark);
        s.rail = colourFrom (colours, "rail", s.rail);
        s.chromeOff = colourFrom (colours, "chromeOff", s.chromeOff);
        s.chromeHover = colourFrom (colours, "chromeHover", s.chromeHover);
        s.chromeOn = colourFrom (colours, "chromeOn", s.chromeOn);
        s.runLamp = colourFrom (colours, "runLamp", s.runLamp);
        s.stopLamp = colourFrom (colours, "stopLamp", s.stopLamp);
        s.kindInput = colourFrom (colours, "kindInput", s.kindInput);
        s.kindBus = colourFrom (colours, "kindBus", s.kindBus);
        s.kindOutput = colourFrom (colours, "kindOutput", s.kindOutput);
        s.stripLight = colourFrom (colours, "stripLight", s.stripLight);
        s.stripDark = colourFrom (colours, "stripDark", s.stripDark);
        s.stripBody = colourFrom (colours, "stripBody", s.stripBody);
        s.shadow = colourFrom (colours, "shadow", s.shadow);
        s.highlight = colourFrom (colours, "highlight", s.highlight);

        // The palette a layout file names from. "yellow" is the look's own lamp, not a token.
        const auto palette = propertyOf (parsed, "palette");
        s.palRed = colourFrom (palette, "red", s.palRed);
        s.palGreen = colourFrom (palette, "green", s.palGreen);
        s.palBlue = colourFrom (palette, "blue", s.palBlue);
        s.palBlack = colourFrom (palette, "black", s.palBlack);
        s.palWhite = colourFrom (palette, "white", s.palWhite);
        s.palViolet = colourFrom (palette, "violet", s.palViolet);
        s.palGrey = colourFrom (palette, "grey", s.palGrey);
        s.palCream = colourFrom (palette, "cream", s.palCream);
        s.palOrange = colourFrom (palette, "orange", s.palOrange);
        s.palAmber = colourFrom (palette, "amber", s.palAmber);
        s.palTeal = colourFrom (palette, "teal", s.palTeal);
        s.palSalmon = colourFrom (palette, "salmon", s.palSalmon);

        s.font = stringFrom (parsed, "font", s.font);
        s.keyRadius = floatFrom (parsed, "keyRadius", s.keyRadius);

        const auto sizes = propertyOf (parsed, "fontSizes");
        s.fontLegend = floatFrom (sizes, "legend", s.fontLegend);
        s.fontCaption = floatFrom (sizes, "caption", s.fontCaption);
        s.fontReadout = floatFrom (sizes, "readout", s.fontReadout);
        return s;
    }

    juce::String styleToJson (const Style& s, const juce::String& name)
    {
        struct Entry { const char* name; juce::Colour colour; };
        const Entry entries[] {
            { "background", s.background }, { "panel", s.panel }, { "panelDark", s.panelDark },
            { "rim", s.rim }, { "legend", s.legend }, { "legendDim", s.legendDim },
            { "slot", s.slot }, { "namePlate", s.namePlate },
            { "capTop", s.capTop }, { "capBottom", s.capBottom }, { "capText", s.capText },
            { "plateColour", s.plateColour },
            { "screenLit", s.screenLit }, { "screenGlass", s.screenGlass }, { "screenBezel", s.screenBezel },
            { "knobGrey", s.knobGrey }, { "knobRed", s.knobRed }, { "knobGreen", s.knobGreen },
            { "knobBlue", s.knobBlue }, { "knobBrown", s.knobBrown },
            { "knobTop", s.knobTop }, { "knobBottom", s.knobBottom },
            { "skirtTop", s.skirtTop }, { "skirtBottom", s.skirtBottom },
            { "vuTop", s.vuTop }, { "vuBottom", s.vuBottom }, { "vuInk", s.vuInk },
            { "tapeGlass", s.tapeGlass }, { "tapeInk", s.tapeInk },
            { "ledGreen", s.ledGreen }, { "ledYellow", s.ledYellow }, { "ledRed", s.ledRed },
            { "ledBlue", s.ledBlue }, { "muteLamp", s.muteLamp }, { "soloLamp", s.soloLamp },
            { "powerLamp", s.powerLamp }, { "selection", s.selection }, { "accent", s.accent },
            { "ink", s.ink }, { "inkDim", s.inkDim }, { "litInk", s.litInk },
            { "inkBright", s.inkBright }, { "inkFaint", s.inkFaint },
            { "recess", s.recess }, { "bay", s.bay }, { "bayDark", s.bayDark }, { "rail", s.rail },
            { "chromeOff", s.chromeOff }, { "chromeHover", s.chromeHover }, { "chromeOn", s.chromeOn },
            { "runLamp", s.runLamp }, { "stopLamp", s.stopLamp },
            { "kindInput", s.kindInput }, { "kindBus", s.kindBus }, { "kindOutput", s.kindOutput },
            { "stripLight", s.stripLight }, { "stripDark", s.stripDark }, { "stripBody", s.stripBody },
            { "shadow", s.shadow }, { "highlight", s.highlight }
        };
        const auto count = sizeof (entries) / sizeof (entries[0]);

        struct Entry palette[] {
            { "red", s.palRed }, { "green", s.palGreen }, { "blue", s.palBlue },
            { "black", s.palBlack }, { "white", s.palWhite }, { "violet", s.palViolet },
            { "grey", s.palGrey }, { "cream", s.palCream }, { "orange", s.palOrange },
            { "amber", s.palAmber }, { "teal", s.palTeal }, { "salmon", s.palSalmon }
        };
        const auto paletteCount = sizeof (palette) / sizeof (palette[0]);

        juce::String text;
        text << "{\n";
        text << "  \"name\": \"" << name << "\",\n";
        text << "  \"font\": \"" << s.font << "\",\n";
        text << "  \"keyRadius\": " << s.keyRadius << ",\n";
        text << "  \"fontSizes\": { \"legend\": " << s.fontLegend
             << ", \"caption\": " << s.fontCaption << ", \"readout\": " << s.fontReadout << " },\n";
        text << "  \"colours\": {\n";

        for (size_t i = 0; i < count; ++i)
            text << "    \"" << entries[i].name << "\": \"" << colourToText (entries[i].colour) << "\""
                 << (i + 1 < count ? "," : "") << "\n";

        text << "  },\n  \"palette\": { ";
        for (size_t i = 0; i < paletteCount; ++i)
            text << "\"" << palette[i].name << "\": \"" << colourToText (palette[i].colour) << "\""
                 << (i + 1 < paletteCount ? ", " : " }");

        text << "\n}\n";
        return text;
    }
}

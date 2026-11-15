#pragma once

// Part of the console: Console.cpp includes it, and nothing else does.
// The layout file read (console/layout/*.json), the colours it names, and the knob.

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../src/ui/Console.h"

namespace
{
    // A colour a layout file names. All of them come from the look now, so a look file can repaint
    // the parts the layouts put colour on (see Console::Colours::palette).
    juce::Colour named (const juce::String& name)
    {
        return Console::Colours::palette (name);
    }

    // THE KNOB: a matte cap in its
    // colour sitting high on a grey skirt seen from a little above (more skirt below than above),
    // the skirt's grip notches and black index mark turning with it, a light rim along its top, and a
    // straight pointer bar from the middle to the cap's edge. The light stays put; the knob turns.
    // `pos` 0..1 over 300 degrees; `d` its whole width.
    void knob (juce::Graphics& g, juce::Point<float> c, float d, float pos, juce::Colour cap, bool dots, juce::Colour ink)
    {
        const float R = d * 0.5f, a = juce::degreesToRadians (-150.0f + 300.0f * juce::jlimit (0.0f, 1.0f, pos));
        auto at = [] (juce::Point<float> o, float angle, float r) { return juce::Point<float> (o.x + r * std::sin (angle), o.y - r * std::cos (angle)); };
        if (dots)   // (the panel's scale round it)
        {
            g.setColour (ink.withAlpha (0.55f));
            for (int i = 0; i < 11; ++i)
                g.fillEllipse (juce::Rectangle<float> (d * 0.04f, d * 0.04f).withCentre (at (c, juce::degreesToRadians (-150.0f + 30.0f * (float) i), R + d * 0.07f)));
        }
        // Its shadow on the panel, then the skirt: grey, darker toward its foot.
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (c.translated (0.0f, d * 0.05f)).expanded (d * 0.02f));
        const auto skirt = juce::Rectangle<float> (d, d).withCentre (c);
        g.setGradientFill (juce::ColourGradient (Console::Colours::skirtTop(), c.x, skirt.getY(), Console::Colours::skirtBottom(), c.x, skirt.getBottom(), false));
        g.fillEllipse (skirt);
        // The skirt's grip: eight notches (one under the knob's foot at the middle), and the black
        // index mark under the pointer, all turning with it.
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path ring;
        ring.addEllipse (skirt);
        g.reduceClipRegion (ring);
        for (int i = 0; i < 8; ++i)
        {
            const float n = a + juce::MathConstants<float>::pi + juce::MathConstants<float>::twoPi * (float) i / 8.0f;
            const auto p1 = at (c, n, R * 0.8f), p2 = at (c, n, R * 1.02f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.drawLine (p1.x, p1.y, p2.x, p2.y, juce::jmax (1.0f, d * 0.035f));
            g.setColour (juce::Colours::white.withAlpha (0.1f));
            g.drawLine (p1.x + 1.0f, p1.y, p2.x + 1.0f, p2.y, 0.8f);
        }
        {
            const auto p1 = at (c, a, R * 0.74f), p2 = at (c, a, R * 1.02f);
            g.setColour (juce::Colour (0xff050505));
            g.drawLine (p1.x, p1.y, p2.x, p2.y, juce::jmax (1.5f, d * 0.09f));
        }
        g.setColour (juce::Colours::white.withAlpha (0.22f));   // (the skirt's top edge catching the light)
        juce::Path rim;
        rim.addCentredArc (c.x, c.y, R * 0.97f, R * 0.97f, 0.0f, -1.7f, 1.7f, true);
        g.strokePath (rim, juce::PathStrokeType (juce::jmax (0.8f, d * 0.025f)));
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawEllipse (skirt.reduced (0.5f), 0.8f);
        // The cap: matte, sitting high, a soft fall of light top to bottom.
        const float rc = R * 0.85f;
        const auto cc = c.translated (0.0f, -R * 0.12f);
        const auto face = juce::Rectangle<float> (rc * 2.0f, rc * 2.0f).withCentre (cc);
        g.setColour (juce::Colours::black.withAlpha (0.25f));   // (its edge's shadow on the skirt)
        g.fillEllipse (face.translated (0.0f, d * 0.03f));
        g.setGradientFill (juce::ColourGradient (cap.brighter (0.08f), cc.x, face.getY(), cap.darker (0.12f), cc.x, face.getBottom(), false));
        g.fillEllipse (face);
        g.setColour (juce::Colours::white.withAlpha (0.18f));   // (its rim, lit at the top)
        juce::Path top;
        top.addCentredArc (cc.x, cc.y, rc - 0.6f, rc - 0.6f, 0.0f, -1.3f, 1.3f, true);
        g.strokePath (top, juce::PathStrokeType (0.9f));
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawEllipse (face, 0.7f);
        // The pointer: a straight bar from the middle out to the cap's edge (dark on a pale cap).
        juce::Path bar;
        const float bw = juce::jmax (1.8f, rc * 0.2f);
        bar.addRoundedRectangle (-bw * 0.5f, -rc * 0.97f, bw, rc * 0.97f, bw * 0.15f);
        bar.applyTransform (juce::AffineTransform::rotation (a).translated (cc.x, cc.y));
        const bool pale = cap.getPerceivedBrightness() > 0.6f;
        g.setColour (pale ? juce::Colour (0xff3a3b3e) : juce::Colour (0xffd2d2d2));
        g.fillPath (bar);
    }

    // The layout file: sections top to bottom, each a list of parts.
    struct Item
    {
        juce::String kind, id, text, name, colour, lamp, align, ink;
        bool lit = false, single = false;
        bool compactOnly = false;   // (drawn only while the desk is COMPACT: the level meter beside the fader)
        int litCount = -1;
        juce::StringArray marks;
        juce::Rectangle<float> r;
        float size = 52.0f, textSize = 15.0f;
        int count = 5;
        juce::StringArray colours;
        juce::Path line;
    };
    // `compact`: it stays when the desk is COMPACT (the name bar and the fader); the rest fold away.
    struct Section { juce::String name; float height = 0; juce::String panel; std::vector<Item> items; bool compact = false; };
    struct Layout { float width = 214.0f; std::vector<Section> sections; juce::String error; };

    Layout load (const juce::File& file)
    {
        Layout layout;
        const auto json = juce::JSON::parse (file);
        if (! json.isObject())
        {
            layout.error = "can't read " + file.getFullPathName();
            return layout;
        }
        layout.width = (float) json.getProperty ("width", 214.0);
        auto num = [] (const juce::var& v, const char* key, double fallback = 0.0) { return (float) (double) v.getProperty (key, fallback); };
        for (auto& s : *json["sections"].getArray())
        {
            Section section { s["name"].toString(), num (s, "height"), s["panel"].toString(), {}, (bool) s.getProperty ("compact", false) };
            for (auto& v : *s["items"].getArray())
            {
                Item item;
                item.kind = v["kind"].toString();
                item.id = v["id"].toString();
                item.text = v["text"].toString();
                item.name = v["name"].toString();
                item.colour = v["colour"].toString();
                item.lamp = v["lamp"].toString();
                item.align = v["align"].toString();
                item.ink = v["ink"].toString();
                item.lit = (bool) v.getProperty ("lit", false) && ! v["lit"].isInt();
                item.litCount = v["lit"].isInt() ? (int) v["lit"] : -1;
                item.single = (bool) v.getProperty ("single", false);
                item.compactOnly = (bool) v.getProperty ("compactOnly", false);
                if (auto* list = v["marks"].getArray())
                    for (auto& m : *list)
                        item.marks.add (m.toString());
                item.size = num (v, "size", 52.0);
                item.textSize = num (v, "size", 15.0);
                item.count = (int) v.getProperty ("count", 5);
                if (auto* list = v["colours"].getArray())
                    for (auto& c : *list)
                        item.colours.add (c.toString());
                if (auto* points = v["points"].getArray())
                    for (int i = 0; i < points->size(); ++i)
                    {
                        const juce::Point<float> p ((float) (double) (*points)[i][0], (float) (double) (*points)[i][1]);
                        if (i == 0) item.line.startNewSubPath (p); else item.line.lineTo (p);
                    }
                item.r = item.kind == "knob" ? juce::Rectangle<float> (num (v, "x"), num (v, "y"), item.size, item.size)
                                             : juce::Rectangle<float> (num (v, "x"), num (v, "y"), num (v, "w"), num (v, "h"));
                section.items.push_back (std::move (item));
            }
            layout.sections.push_back (std::move (section));
        }
        return layout;
    }
}

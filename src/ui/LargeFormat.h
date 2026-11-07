#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include <unordered_map>

// LARGE FORMAT: the classic console's parts as its operator's manual draws them: the one key
// (key()), the toggle, the LED and the knob, all drawn alike wherever they appear.
namespace LargeFormat
{
    inline juce::Font print (float height, bool bold = true) { return juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain); }

    // A key's legend, as large as its cap allows (up to `maxH`): one line if it fits at a readable
    // size, else two lines when there's room, else a smaller single line (never under 8 px, never
    // squashed sideways).
    inline void legend (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float maxH)
    {
        const auto s = text.toUpperCase();
        // (the size fitted to this text in this space, worked out once: keys repaint as their lamps fade)
        static std::unordered_map<juce::String, float> fitted;   // message thread
        const auto key = s + "|" + juce::String (juce::roundToInt (area.getWidth())) + "|" + juce::String (juce::roundToInt (area.getHeight())) + "|" + juce::String (maxH);
        float h;
        if (const auto known = fitted.find (key); known != fitted.end())
            h = known->second;
        else
        {
            auto widthAt = [&s] (float size) { return juce::GlyphArrangement::getStringWidth (print (size), s); };
            const float readable = 9.0f;
            h = juce::jlimit (readable, maxH, area.getHeight() * 0.72f);
            while (h > readable && widthAt (h) > area.getWidth())
                h -= 0.5f;
            if (widthAt (h) > area.getWidth() && s.containsChar (' ') && area.getHeight() >= readable * 2.0f + 1.0f)
                h = -readable;   // (two lines at the readable size)
            else
                while (h > 8.0f && widthAt (h) > area.getWidth())
                    h -= 0.5f;
            if (fitted.size() > 4096)
                fitted.clear();
            fitted[key] = h;
        }
        if (h < 0.0f)
        {
            g.setFont (print (-h));
            g.drawFittedText (s, area.toNearestInt(), juce::Justification::centred, 2, 1.0f);
            return;
        }
        g.setFont (print (h));
        g.drawFittedText (s, area.toNearestInt(), juce::Justification::centred, 1, 0.9f);
    }

    // THE LED, the same everywhere on the desk: one size, in a dark bezel ring (so lit and unlit
    // read the same size), lit in its colour.
    constexpr float ledSize = 6.0f;
    // LIGHT as real lamps give it: a lit lens blooms, its light spilling onto the panel round it in
    // soft rings that fade with distance (`amount` 0..1).
    inline void bloom (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, float amount, float corner, float spread = 4.0f)
    {
        if (amount <= 0.01f)
            return;
        for (int i = 3; i >= 1; --i)
        {
            const float k = (float) i / 3.0f;
            g.setColour (colour.withAlpha (0.16f * amount * (1.0f - k * 0.6f)));
            g.fillRoundedRectangle (r.expanded (spread * k), corner + spread * k);
        }
    }

    // (a short bar, as the console's segment LEDs: wider than tall, in a black surround; lit, it
    // blooms and its lens shows a hot centre and a glint; dark, the lens still holds a dim tint)
    inline void led (juce::Graphics& g, juce::Point<float> c, bool lit, juce::Colour colour, float size = ledSize)
    {
        // A round LED: a dark bezel; lit, a pale hot core fading to its
        // colour at the rim and a halo on the panel; dark, a dim lens with a glint.
        const auto r = juce::Rectangle<float> (size + 1.0f, size + 1.0f).withCentre (c);
        if (lit)
            bloom (g, r, colour, 1.0f, r.getWidth() * 0.5f, 3.5f);
        g.setColour (Theme::get().rim);
        g.fillEllipse (r.expanded (1.4f));
        if (lit)
        {
            juce::ColourGradient lens (colour.interpolatedWith (juce::Colours::white, 0.6f), r.getCentreX(), r.getCentreY(),
                                       colour, r.getRight(), r.getCentreY(), true);
            g.setGradientFill (lens);
        }
        else
            g.setColour (colour.withMultipliedBrightness (0.22f).withMultipliedSaturation (0.6f));
        g.fillEllipse (r);
        g.setColour (juce::Colours::white.withAlpha (lit ? 0.5f : 0.12f));   // (its lens catching the light)
        g.fillEllipse (juce::Rectangle<float> (r.getWidth() * 0.35f, r.getHeight() * 0.25f).withCentre ({ r.getCentreX() - r.getWidth() * 0.12f, r.getY() + r.getHeight() * 0.28f }));
    }

    // THE key: every key on the desk drawn alike, as the manual's square cap in its black frame.
    // Lit, the whole cap glows in its colour (no LED squeezed beside the legend); `text` centred
    // (empty: the caller draws an icon). `press` 0..1 sinks it.
    inline void key (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, float litLevel, juce::Colour lamp, bool enabled, float press, float hover)
    {
        // A key: a pale grey bevelled cap on a soft shadow, its
        // top edge catching the light; lit, the cap takes a faint tint of its colour and the legend
        // glows in it (a lamp behind the lettering), its light spilling round the key.
        r = r.reduced (1.0f);
        const float lit = juce::jlimit (0.0f, 1.0f, litLevel), corner = 2.5f;
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.5f), corner);
        bloom (g, r, lamp, lit * 0.5f, corner, 3.0f);
        const auto cap = r.withTrimmedBottom (1.0f).translated (0.0f, press * 0.8f);
        if (! enabled)   // NOT AVAILABLE: a flat dark cap, its legend still clear (never see-through)
        {
            g.setColour (juce::Colour (0xff45474b));
            g.fillRoundedRectangle (cap, corner);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawRoundedRectangle (cap, corner, 0.9f);
            if (text.isNotEmpty())
            {
                g.setColour (juce::Colour (0xffb4b7bb));
                legend (g, text, cap.reduced (2.0f, 0.0f), 12.0f);
            }
            return;
        }
        const float shine = 0.94f + 0.14f * hover - 0.12f * juce::jmin (1.0f, press);   // (over: brighter; held: darker)
        auto top = juce::Colour (0xffbdbec1).withMultipliedBrightness (shine), bottom = juce::Colour (0xff8e8f92).withMultipliedBrightness (shine);
        if (lit > 0.01f)
        {
            top = top.interpolatedWith (lamp, 0.16f * lit).withMultipliedBrightness (1.0f - 0.06f * lit);
            bottom = bottom.interpolatedWith (lamp, 0.12f * lit).withMultipliedBrightness (1.0f - 0.08f * lit);
        }
        g.setGradientFill (juce::ColourGradient (top, cap.getX(), cap.getY(), bottom, cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, corner);
        g.setColour (juce::Colours::white.withAlpha (0.5f));   // (its top edge catching the light)
        g.fillRect (cap.reduced (corner, 0.0f).withHeight (1.0f).translated (0.0f, 0.5f));
        g.setColour (hover > 0.01f ? Theme::get().accent.withAlpha (0.35f + 0.5f * hover) : juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (cap, corner, 0.9f + 0.5f * hover);
        if (text.isEmpty())
            return;
        const auto area = cap.reduced (2.0f, 0.0f);
        if (lit > 0.05f)   // the lettering lit from behind: a soft glow, then the bright legend
        {
            const auto glowInk = lamp.withMultipliedSaturation (1.1f).brighter (0.3f);
            for (auto [dx, dy] : { std::pair { -0.8f, 0.0f }, { 0.8f, 0.0f }, { 0.0f, -0.8f }, { 0.0f, 0.8f } })
            {
                g.setColour (glowInk.withAlpha (0.28f * lit));
                legend (g, text, area.translated (dx, dy), 12.0f);
            }
        }
        const auto ink = juce::Colour (0xff2a2b2d).interpolatedWith (lamp.brighter (0.15f), lit);
        g.setColour (ink);
        legend (g, text, area, 12.0f);
    }

    // An in-line switch (BELL, FAST, PRE, S/C): a mid-grey cap
    // on a shadow, its legend printed on it; lit, the whole cap glows in its colour (red PRE, green
    // x3, a cream IN), the legend dark on a pale lamp, white on a deep one.
    inline void toggle (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, float litLevel, juce::Colour lamp, bool enabled, float press, float hover)
    {
        const float lit = juce::jlimit (0.0f, 1.0f, litLevel), corner = 2.0f, fade = 1.0f;
        r = r.reduced (0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.2f), corner);
        const auto cap = r.withTrimmedBottom (1.0f).translated (0.0f, press * 0.8f);
        bloom (g, cap, lamp, lit * 0.6f, corner, 3.0f);   // (its light on the panel)
        if (! enabled)   // (not available: flat and dark, its legend still clear)
        {
            g.setColour (juce::Colour (0xff3e4044));
            g.fillRoundedRectangle (cap, corner);
            g.setColour (juce::Colours::black.withAlpha (0.65f));
            g.drawRoundedRectangle (cap, corner, 0.8f);
            if (text.isNotEmpty())
            {
                g.setColour (juce::Colour (0xffaeb1b5));
                legend (g, text, cap.reduced (2.0f, 0.0f), 9.5f);
            }
            return;
        }
        const float shine = 0.95f + 0.16f * hover - 0.12f * juce::jmin (1.0f, press);
        auto top = juce::Colour (0xff8a8c90).withMultipliedBrightness (shine), bottom = juce::Colour (0xff5f6165).withMultipliedBrightness (shine);
        if (lit > 0.01f)
        {
            top = top.interpolatedWith (lamp.brighter (0.2f), lit);
            bottom = bottom.interpolatedWith (lamp.darker (0.25f), lit);
        }
        g.setGradientFill (juce::ColourGradient (top.withMultipliedAlpha (fade), cap.getX(), cap.getY(), bottom.withMultipliedAlpha (fade), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, corner);
        g.setColour (juce::Colours::white.withAlpha ((0.28f + 0.2f * lit) * fade));   // (its top edge catching the light)
        g.fillRect (cap.reduced (corner, 0.0f).withHeight (1.0f).translated (0.0f, 0.5f));
        g.setColour (hover > 0.01f ? Theme::get().accent.withAlpha (0.35f + 0.5f * hover) : juce::Colours::black.withAlpha (0.65f));
        g.drawRoundedRectangle (cap, corner, 0.8f + 0.5f * hover);
        if (text.isEmpty())
            return;
        const bool paleLamp = lamp.getPerceivedBrightness() > 0.68f;
        const auto ink = lit > 0.5f && ! paleLamp ? juce::Colours::white : juce::Colour (0xff1e1f21);
        g.setColour (ink.withMultipliedAlpha (0.92f));
        legend (g, text, cap.reduced (2.0f, 0.0f), 9.5f);
    }
}

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LargeFormat.h"

// THE CONSOLE BUILD's settings, as its console: cards on the dark desk, square switches with their
// names beside them, the print in the console's blue-grey.

namespace DeskSettings
{
    // A settings page that lays itself out to the width it's given (its cards in columns).
    struct WidthFill
    {
        virtual ~WidthFill() = default;
        virtual int heightFor (int width) = 0;
    };

    // CARDS: groups (a heading, then rows of parts), each a card, the cards in as many columns as the
    // width holds, each into the shortest. A row's parts share it evenly; `last` fixes the last
    // part's width (a hotkey's key at the right). Lays them out (apply) and returns the height.
    struct Row { std::vector<juce::Component*> parts; int last = 0; };
    struct Group { juce::Component* heading = nullptr; std::vector<Row> rows; };
    inline int arrange (const std::vector<Group>& groups, int w, bool apply, std::vector<juce::Rectangle<int>>& cards, int cardW = 300, int top = 14)
    {
        constexpr int pad = 14, gap = 12, head = 30, rowH = 28, rowGap = 4, inner = 12;
        const int columns = juce::jmax (1, (w - 2 * pad + gap) / (cardW + gap));
        const int colW = (w - 2 * pad - (columns - 1) * gap) / columns;
        std::vector<int> bottom ((size_t) columns, top);
        if (apply)
            cards.clear();
        for (auto& grp : groups)
        {
            const int h = (grp.heading != nullptr ? head : inner) + (int) grp.rows.size() * (rowH + rowGap) + inner;
            const auto c = (size_t) (std::min_element (bottom.begin(), bottom.end()) - bottom.begin());
            const juce::Rectangle<int> box (pad + (int) c * (colW + gap), bottom[c], colW, h);
            bottom[c] += h + gap;
            if (! apply)
                continue;
            cards.push_back (box);
            auto r = box.reduced (inner, 0);
            if (grp.heading != nullptr)
                grp.heading->setBounds (r.removeFromTop (head).withTrimmedTop (4));
            else
                r.removeFromTop (inner);
            for (auto& row : grp.rows)
            {
                auto line = r.removeFromTop (rowH);
                r.removeFromTop (rowGap);
                if (row.parts.empty())
                    continue;
                if (row.last > 0 && row.parts.size() > 1)
                {
                    row.parts.back()->setBounds (line.removeFromRight (row.last));
                    line.removeFromRight (6);
                }
                const int n = (int) row.parts.size() - (row.last > 0 && row.parts.size() > 1 ? 1 : 0);
                const int pw = (line.getWidth() - (n - 1) * 4) / juce::jmax (1, n);
                for (int k = 0; k < n; ++k)
                {
                    row.parts[(size_t) k]->setBounds (k == n - 1 ? line : line.removeFromLeft (pw));
                    line.removeFromLeft (4);
                }
            }
        }
        return *std::max_element (bottom.begin(), bottom.end()) + pad;
    }

    inline void styleHeading (juce::Label& l)
    {
        l.setFont (LargeFormat::print (12.5f));
        l.setColour (juce::Label::textColourId, juce::Colour (0xff8a9398));
    }
    inline void styleText (juce::Label& l)
    {
        l.setFont (LargeFormat::print (11.5f));
        l.setColour (juce::Label::textColourId, juce::Colour (0xffc9cbce));
    }
    inline juce::Colour desk()    { return juce::Colour (0xff141517); }
    inline juce::Colour cardFill(){ return juce::Colour (0xff222427); }
    inline juce::Colour print()   { return juce::Colour (0xff8a9398); }
    inline juce::Colour text()    { return juce::Colour (0xffc9cbce); }

    // A card: a dark panel on the desk, its top edge catching the light; `on` lit grey with a blue edge.
    inline void card (juce::Graphics& g, juce::Rectangle<float> r, bool on = false, bool hover = false)
    {
        g.setColour (on ? juce::Colour (0xff4a4d52) : hover ? juce::Colour (0xff34373b) : cardFill());
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (hover ? juce::Colour (0xff3db4e0).withAlpha (0.7f) : juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (on ? 0.14f : 0.05f));
        g.fillRect (r.reduced (4.0f, 0.0f).withHeight (1.0f).translated (0.0f, 1.0f));
        if (on)
        {
            g.setColour (juce::Colour (0xff3db4e0));
            g.fillRoundedRectangle (r.withWidth (3.0f), 1.5f);
        }
    }

    // An on/off option: the console's small square switch (cream when lit), its name beside it.
    class Switch : public juce::Button
    {
    public:
        explicit Switch (const juce::String& name) : juce::Button (name) {}
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat();
            const auto square = r.removeFromLeft (r.getHeight()).reduced (5.0f).translated (0.0f, down ? 0.6f : 0.0f);
            const bool on = getToggleState();
            const auto lamp = juce::Colour (0xfffff0c8);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (square.translated (0.0f, 1.2f), 2.0f);
            if (on)
                LargeFormat::bloom (g, square, lamp, 0.7f, 2.0f, 3.0f);
            const float shine = (over ? 1.14f : 1.0f) * (down ? 0.78f : 1.0f);   // (over: brighter; held: darker, as it sinks)
            g.setGradientFill (on ? juce::ColourGradient (lamp.brighter (0.2f).withMultipliedBrightness (shine), square.getX(), square.getY(), lamp.darker (0.2f).withMultipliedBrightness (shine), square.getX(), square.getBottom(), false)
                                  : juce::ColourGradient (juce::Colour (0xffc4c6c9).withMultipliedBrightness (shine), square.getX(), square.getY(),
                                                          juce::Colour (0xff85878b).withMultipliedBrightness (shine), square.getX(), square.getBottom(), false));
            g.fillRoundedRectangle (square, 2.0f);
            g.setColour (over ? juce::Colour (0xff3db4e0) : juce::Colours::black.withAlpha (0.6f));
            g.drawRoundedRectangle (square, 2.0f, over ? 1.4f : 0.9f);
            g.setColour (on || over ? juce::Colour (0xffeceef0) : DeskSettings::text());
            g.setFont (LargeFormat::print (11.5f));
            g.drawFittedText (getButtonText(), r.withTrimmedLeft (6.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);
        }
    };
}

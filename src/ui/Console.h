#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Backlight.h"
#include "Motion.h"
#include "DotMatrix.h"
#include "Theme.h"
#include "LargeFormat.h"

// Faider's design language ("Desk and Rack"): the desk's controls, drawn in
// the current theme (Theme.h), and the tokens every view uses by name: Type (five sizes), Space
// (a 4 px grid) and Colours (surfaces, ink, meanings, channel kinds). The rack (PluginRack.h,
// ProStrip.h) has its own fixed hardware look but uses the same Type and Space.
namespace Console
{
    // The current theme's colours, by the names the views use.
    namespace Colours
    {
        inline juce::Colour background()    { return Theme::get().background; }
        inline juce::Colour panel()         { return Theme::get().panel; }
        inline juce::Colour panelDark()     { return Theme::get().panelDark; }
        inline juce::Colour rim()           { return Theme::get().rim; }
        inline juce::Colour legend()        { return Theme::get().legend; }
        inline juce::Colour legendDim()     { return Theme::get().legendDim; }
        inline juce::Colour capTop()        { return Theme::get().capTop; }
        inline juce::Colour capBottom()     { return Theme::get().capBottom; }
        inline juce::Colour capText()       { return Theme::get().capText; }
        inline juce::Colour aluminium()     { return Theme::get().plateColour; }   // the fader plate
        inline juce::Colour slot()          { return Theme::get().slot; }         // recessed fields: selectors, plugin slots

        // Knobs.
        inline juce::Colour knobGrey()      { return Theme::get().knobGrey; }

        // LEDs and lamps.
        inline juce::Colour ledGreen()      { return Theme::get().ledGreen; }
        inline juce::Colour ledYellow()     { return Theme::get().ledYellow; }
        inline juce::Colour ledRed()        { return Theme::get().ledRed; }
        inline juce::Colour ledBlue()       { return Theme::get().ledBlue; }
        inline juce::Colour muteLamp()      { return Theme::get().muteLamp; }
        inline juce::Colour powerLamp()     { return Theme::get().powerLamp; }

        // Printed warnings (dropouts, overloads): pale red on dark panels, deep red on light ones.
        inline juce::Colour warning()       { return panel().getPerceivedBrightness() > 0.5f ? juce::Colour (0xffb8241a) : juce::Colour (0xffff7a6e); }

        // Meanings: what a light or highlight says, whatever the theme.
        inline juce::Colour selected()      { const auto s = Theme::get().selection; return s.isTransparent() ? ledGreen() : s; }   // the one you're working on: tab, SEL, chosen unit
        inline juce::Colour on()            { return powerLamp(); }   // signal passes: ON, IN, a send
        inline juce::Colour danger()        { return ledRed(); }      // muted, clipping, delete
        inline juce::Colour caution()       { return ledYellow(); }   // near a limit
        inline juce::Colour link()          { return ledBlue(); }     // built in, linked, information

        // Ink for the grid lines and scales on a screen (the EQ and dynamics graphs): the printing's
        // dim colour, or, where the printing is dark (a light theme), the screen's own light, dimmed.
        inline juce::Colour screenInk()
        {
            const auto dim = legendDim();
            return dim.getPerceivedBrightness() > 0.45f ? dim : Theme::get().screenLit.interpolatedWith (Theme::get().screenGlass, 0.35f);
        }

        // Channel kinds: colour bars, cables, scribble strips and unit stripes, the same in every view
        // (a channel's own colour replaces its kind's).
        inline juce::Colour kindInput()     { return juce::Colour (0xff3d8fd1); }
        inline juce::Colour kindBus()       { return juce::Colour (0xff9a6ad1); }
        inline juce::Colour kindOutput()    { return juce::Colour (0xffd18a3d); }

        // A strip's scribble strip (its name at the top): the printing colour as the strip,
        // the panel as the writing (white with black text on dark desks, black on light ones).
        inline juce::Colour scribble()      { return legend(); }
        inline juce::Colour scribbleText()  { return panelDark(); }
    }

    //==========================================================================
    // Icons for keys where a symbol says it better than a word (the tooltip keeps the word).
    // Our own simple shapes, drawn in the legend's colour like the text they replace.
    enum class Icon { none, prev, rewind, play, pause, stop, forward, next, shuffle, repeat, repeatOne, eject, record,
                      addFile, addFolder, folder, open, stereo, mono, loop, power, flip, tidy, cablesTidy, cablesSlack,
                      lockClosed, lockOpen, cross, stepDown, stepUp, window };

    inline void setIcon (juce::Button& b, Icon icon) { b.getProperties().set ("icon", (int) icon); b.repaint(); }
    inline Icon iconOf (const juce::Button& b) { return (Icon) (int) b.getProperties().getWithDefault ("icon", 0); }

    // `icon` centred in `area`, at most `size` across.
    inline void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float size = 16.0f)
    {
        const float s = juce::jmin (size, area.getHeight() * 0.75f, area.getWidth() * 0.8f);
        const auto box = juce::Rectangle<float> (s, s).withCentre (area.getCentre());
        auto at = [&box] (float x, float y) { return box.getRelativePoint (x, y); };
        const float line = juce::jmax (1.4f, s * 0.11f);
        juce::Path fill, stroke;

        auto triangle = [&] (float x1, float y1, float x2, float y2, float x3, float y3)
        {
            fill.startNewSubPath (at (x1, y1)); fill.lineTo (at (x2, y2)); fill.lineTo (at (x3, y3)); fill.closeSubPath();
        };
        auto bar = [&] (float x1, float y1, float x2, float y2) { fill.addRectangle (juce::Rectangle<float> (at (x1, y1), at (x2, y2))); };
        auto arrowHead = [&] (juce::Point<float> tip, float angle)   // angle: the way it points
        {
            const float l = s * 0.24f;
            fill.startNewSubPath (tip);
            fill.lineTo (tip - juce::Point<float> (std::cos (angle - 0.5f), std::sin (angle - 0.5f)) * l);
            fill.lineTo (tip - juce::Point<float> (std::cos (angle + 0.5f), std::sin (angle + 0.5f)) * l);
            fill.closeSubPath();
        };
        auto folderShape = [&]
        {
            stroke.startNewSubPath (at (0.08f, 0.82f)); stroke.lineTo (at (0.08f, 0.22f)); stroke.lineTo (at (0.38f, 0.22f));
            stroke.lineTo (at (0.46f, 0.32f)); stroke.lineTo (at (0.92f, 0.32f)); stroke.lineTo (at (0.92f, 0.82f)); stroke.closeSubPath();
        };
        auto plus = [&] (float cx, float cy)
        {
            bar (cx - 0.2f, cy - 0.05f, cx + 0.2f, cy + 0.05f);
            bar (cx - 0.05f, cy - 0.2f, cx + 0.05f, cy + 0.2f);
        };
        constexpr float pi = juce::MathConstants<float>::pi;

        switch (icon)
        {
            case Icon::none: return;
            case Icon::play:    triangle (0.22f, 0.12f, 0.88f, 0.5f, 0.22f, 0.88f); break;
            case Icon::stepDown: triangle (0.7f, 0.22f, 0.3f, 0.5f, 0.7f, 0.78f); break;
            case Icon::stepUp:   triangle (0.3f, 0.22f, 0.7f, 0.5f, 0.3f, 0.78f); break;
            case Icon::pause:   bar (0.2f, 0.14f, 0.4f, 0.86f); bar (0.6f, 0.14f, 0.8f, 0.86f); break;
            case Icon::stop:    bar (0.18f, 0.18f, 0.82f, 0.82f); break;
            case Icon::rewind:  triangle (0.5f, 0.18f, 0.06f, 0.5f, 0.5f, 0.82f); triangle (0.94f, 0.18f, 0.5f, 0.5f, 0.94f, 0.82f); break;
            case Icon::forward: triangle (0.5f, 0.18f, 0.94f, 0.5f, 0.5f, 0.82f); triangle (0.06f, 0.18f, 0.5f, 0.5f, 0.06f, 0.82f); break;
            case Icon::prev:    bar (0.12f, 0.18f, 0.24f, 0.82f); triangle (0.86f, 0.18f, 0.28f, 0.5f, 0.86f, 0.82f); break;
            case Icon::next:    bar (0.76f, 0.18f, 0.88f, 0.82f); triangle (0.14f, 0.18f, 0.72f, 0.5f, 0.14f, 0.82f); break;
            case Icon::eject:   triangle (0.5f, 0.12f, 0.9f, 0.58f, 0.1f, 0.58f); bar (0.1f, 0.7f, 0.9f, 0.84f); break;
            case Icon::record:  fill.addEllipse (box.reduced (s * 0.14f)); break;
            case Icon::shuffle:
                stroke.startNewSubPath (at (0.06f, 0.26f)); stroke.cubicTo (at (0.45f, 0.26f), at (0.45f, 0.74f), at (0.8f, 0.74f));
                stroke.startNewSubPath (at (0.06f, 0.74f)); stroke.cubicTo (at (0.45f, 0.74f), at (0.45f, 0.26f), at (0.8f, 0.26f));
                arrowHead (at (0.96f, 0.26f), 0.0f);
                arrowHead (at (0.96f, 0.74f), 0.0f);
                break;
            case Icon::repeat:
            case Icon::repeatOne:
                stroke.startNewSubPath (at (0.62f, 0.24f)); stroke.lineTo (at (0.24f, 0.24f));
                stroke.quadraticTo (at (0.08f, 0.24f), at (0.08f, 0.5f)); stroke.quadraticTo (at (0.08f, 0.76f), at (0.24f, 0.76f));
                stroke.lineTo (at (0.76f, 0.76f)); stroke.quadraticTo (at (0.92f, 0.76f), at (0.92f, 0.5f));
                stroke.quadraticTo (at (0.92f, 0.24f), at (0.76f, 0.24f));
                arrowHead (at (0.8f, 0.24f), 0.0f);
                if (icon == Icon::repeatOne)
                    bar (0.45f, 0.38f, 0.56f, 0.64f);
                break;
            case Icon::addFile:
                stroke.startNewSubPath (at (0.14f, 0.08f)); stroke.lineTo (at (0.5f, 0.08f)); stroke.lineTo (at (0.66f, 0.24f));
                stroke.lineTo (at (0.66f, 0.5f)); stroke.startNewSubPath (at (0.4f, 0.92f)); stroke.lineTo (at (0.14f, 0.92f)); stroke.closeSubPath();
                stroke.startNewSubPath (at (0.14f, 0.92f)); stroke.lineTo (at (0.14f, 0.08f));
                plus (0.74f, 0.74f);
                break;
            case Icon::addFolder: folderShape(); plus (0.76f, 0.76f); break;
            case Icon::folder:    folderShape(); break;
            case Icon::window:   // a window with its title bar: a plugin's own interface
                stroke.addRoundedRectangle (juce::Rectangle<float> (at (0.08f, 0.18f), at (0.92f, 0.84f)), s * 0.06f);
                stroke.startNewSubPath (at (0.08f, 0.36f)); stroke.lineTo (at (0.92f, 0.36f));
                break;
            case Icon::open:
                folderShape();
                stroke.startNewSubPath (at (0.4f, 0.66f)); stroke.lineTo (at (0.74f, 0.4f));
                arrowHead (at (0.84f, 0.32f), -0.66f);
                break;
            case Icon::stereo:
                stroke.addEllipse (juce::Rectangle<float> (at (0.04f, 0.2f), at (0.64f, 0.8f)));
                stroke.addEllipse (juce::Rectangle<float> (at (0.36f, 0.2f), at (0.96f, 0.8f)));
                break;
            case Icon::mono:
                stroke.addEllipse (juce::Rectangle<float> (at (0.2f, 0.2f), at (0.8f, 0.8f)));
                break;
            case Icon::loop:
                // Arcs run clockwise from 12 o'clock; at angle a the way round is (cos a, sin a).
                stroke.addCentredArc (box.getCentreX(), box.getCentreY(), s * 0.34f, s * 0.34f, 0.0f, 0.15f * pi, 1.75f * pi, true);
                arrowHead (box.getCentre() + juce::Point<float> (std::sin (1.75f * pi), -std::cos (1.75f * pi)) * s * 0.34f, 1.75f * pi);
                break;
            case Icon::power:
                stroke.addCentredArc (box.getCentreX(), box.getCentreY() + s * 0.04f, s * 0.36f, s * 0.36f, 0.0f, 0.22f * pi, 1.78f * pi, true);
                stroke.startNewSubPath (at (0.5f, 0.06f)); stroke.lineTo (at (0.5f, 0.46f));
                break;
            case Icon::lockClosed:
            case Icon::lockOpen:
            {
                // A padlock; open, the shackle lifts and its right leg comes free.
                const bool open = icon == Icon::lockOpen;
                const float top = open ? 0.2f : 0.32f;
                bar (0.18f, 0.46f, 0.82f, 0.9f);
                stroke.startNewSubPath (at (0.3f, 0.48f));
                stroke.lineTo (at (0.3f, top));
                stroke.addCentredArc (box.getCentreX(), at (0.0f, top).y, s * 0.2f, s * 0.2f, 0.0f, -0.5f * pi, 0.5f * pi, false);
                stroke.lineTo (at (0.7f, open ? top + 0.08f : 0.48f));
                break;
            }
            case Icon::cross:
                stroke.startNewSubPath (at (0.22f, 0.22f)); stroke.lineTo (at (0.78f, 0.78f));
                stroke.startNewSubPath (at (0.78f, 0.22f)); stroke.lineTo (at (0.22f, 0.78f));
                break;
            case Icon::flip:
                stroke.addCentredArc (box.getCentreX(), box.getCentreY(), s * 0.34f, s * 0.34f, 0.0f, 0.15f * pi, 0.85f * pi, true);
                stroke.addCentredArc (box.getCentreX(), box.getCentreY(), s * 0.34f, s * 0.34f, 0.0f, 1.15f * pi, 1.85f * pi, true);
                arrowHead (box.getCentre() + juce::Point<float> (std::sin (0.85f * pi), -std::cos (0.85f * pi)) * s * 0.34f, 0.85f * pi);
                arrowHead (box.getCentre() + juce::Point<float> (std::sin (1.85f * pi), -std::cos (1.85f * pi)) * s * 0.34f, 1.85f * pi);
                break;
            case Icon::tidy:
                bar (0.1f, 0.1f, 0.42f, 0.42f); bar (0.58f, 0.1f, 0.9f, 0.42f);
                bar (0.1f, 0.58f, 0.42f, 0.9f); bar (0.58f, 0.58f, 0.9f, 0.9f);
                break;
            case Icon::cablesTidy:
            case Icon::cablesSlack:
                fill.addEllipse (juce::Rectangle<float> (s * 0.2f, s * 0.2f).withCentre (at (0.12f, 0.36f)));
                fill.addEllipse (juce::Rectangle<float> (s * 0.2f, s * 0.2f).withCentre (at (0.88f, 0.36f)));
                stroke.startNewSubPath (at (0.12f, 0.36f));
                if (icon == Icon::cablesTidy) stroke.lineTo (at (0.88f, 0.36f));
                else                           stroke.cubicTo (at (0.3f, 1.0f), at (0.7f, 1.0f), at (0.88f, 0.36f));
                break;
        }

        g.setColour (colour);
        g.fillPath (fill);
        g.strokePath (stroke, juce::PathStrokeType (line, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    inline juce::Colour monoLight() { return juce::Colour (0xffe0b040); }   // amber, beside stereo's own colour

    // Names are written in capitals, like a desk's scribble strip: a renamable label types in
    // capitals (pasting too), and onTextChange should store getText() as it is.
    struct CapitalsFilter : juce::TextEditor::InputFilter
    {
        juce::String filterNewText (juce::TextEditor&, const juce::String& text) override { return text.toUpperCase(); }
    };

    inline void capitalsOnly (juce::Label& label)
    {
        label.setText (label.getText().toUpperCase(), juce::dontSendNotification);
        label.onEditorShow = [&label]
        {
            if (auto* editor = label.getCurrentTextEditor())
                editor->setInputFilter (new CapitalsFilter(), true);
        };
    }

    // Printed legend font: bold, upright, legible.
    inline juce::Font legendFont (float height) { return juce::FontOptions (height, juce::Font::bold); }

    // The five type sizes. Everything printed is legendFont in capitals.
    namespace Type
    {
        constexpr float display = 16.0f;  // page and dialog titles
        constexpr float name = 14.0f;     // strip, unit and channel names; section headings
        constexpr float body = 12.5f;     // lists, tables, menus, text fields, explanations
        constexpr float legend = 11.0f;   // silkscreen labels over controls, key legends, column headings
        constexpr float tag = 9.5f;       // scales and ticks, jack labels, the smallest notes
        constexpr float detail = body;    // (secondary lines under a name: body size, printed, dim)
    }

    // The 4 px grid.
    namespace Space
    {
        constexpr int unit = 4;       // the smallest step
        constexpr int gap = 8;        // between controls and panels
        constexpr int pad = 12;       // inside a panel
        constexpr int key = 24;       // a key's height
        constexpr int keySmall = 20;  // where rows are tight
        constexpr int row = 26;       // a list or table row
        constexpr int title = 24;     // a section's title strip
        constexpr int column = 84;    // a key's width: keys in rows above and below line up
        constexpr int bar = 32;       // a row of keys (a key, unit above and below)

        // n columns wide, with the unit gaps between them.
        constexpr int columns (int n) { return n * column + (n - 1) * unit; }
    }

    //==========================================================================
    // Keys laid out on the grid: each Space::key high, centred in the row, a
    // whole number of columns wide, a unit apart; groups a pad apart. Every row of keys in the app
    // is laid out with this, so keys line up in columns from row to row.
    //   Console::KeyRow row (bar);
    //   row.left (all).left (inputs).group().left (inserts);
    //   row.right (window, 2);
    class KeyRow
    {
    public:
        explicit KeyRow (juce::Rectangle<int> rowArea, int keyHeight = Space::key)
            : area (rowArea), height (juce::jmin (keyHeight, rowArea.getHeight())) {}

        KeyRow& left (juce::Component& c, int columns = 1)  { place (c, area.removeFromLeft (take (columns, true))); return *this; }
        KeyRow& right (juce::Component& c, int columns = 1) { place (c, area.removeFromRight (take (columns, false))); return *this; }
        KeyRow& leftWidth (juce::Component& c, int width)   { place (c, area.removeFromLeft (takeWidth (width, true))); return *this; }
        KeyRow& rightWidth (juce::Component& c, int width)  { place (c, area.removeFromRight (takeWidth (width, false))); return *this; }

        // A gap between groups of keys (at whichever end is placed next).
        KeyRow& group()      { pendingLeft = pendingRight = Space::pad; return *this; }
        KeyRow& groupLeft()  { pendingLeft = Space::pad; return *this; }
        KeyRow& groupRight() { pendingRight = Space::pad; return *this; }

        // What's left between the keys placed at each end (for a label or a meter).
        juce::Rectangle<int> rest() const { return area; }

        // The width a key's legend needs, rounded up to whole columns.
        static int columnsFor (const juce::String& text, float fontHeight = Type::legend)
        {
            const int w = juce::GlyphArrangement::getStringWidthInt (legendFont (fontHeight), text) + 2 * Space::pad;
            return juce::jmax (1, (w + Space::unit + Space::column - 1) / (Space::column + Space::unit));
        }

    private:
        int take (int columns, bool fromLeft) { return takeWidth (Space::columns (juce::jmax (1, columns)), fromLeft); }
        int takeWidth (int width, bool fromLeft)
        {
            auto& pending = fromLeft ? pendingLeft : pendingRight;
            auto& started = fromLeft ? startedLeft : startedRight;
            const int gap = started ? juce::jmax (Space::unit, pending) : 0;
            if (fromLeft) area.removeFromLeft (gap); else area.removeFromRight (gap);
            started = true;
            pending = 0;
            return juce::jmin (width, area.getWidth());
        }
        void place (juce::Component& c, juce::Rectangle<int> r) const
        {
            c.setBounds (r.withSizeKeepingCentre (r.getWidth(), height));
        }

        juce::Rectangle<int> area;
        int height;
        int pendingLeft = 0, pendingRight = 0;
        bool startedLeft = false, startedRight = false;
    };

    //==========================================================================
    // Tabs as a row of keys on the grid: every tab the same whole number of columns (the widest
    // legend's), the first cap on the content edge `edge`, caps a key high and a unit apart, so
    // tabs line up in columns with the keys in the rows below. Look::drawTabButton draws a tab's
    // cap capInset inside it, where a key fills its bounds.
    // A component appearing: it comes up out of the desk's shade. A veil the panel's colour lies
    // over it and lifts away (~0.2 s), so the component itself is never drawn see-through: drawing
    // a whole page translucent leaves gaps the window fills with white.
    inline void riseIn (juce::Component& c, int ms = 200)
    {
        struct Veil : juce::Component, juce::Timer
        {
            juce::Component::SafePointer<juce::Component> target;
            double start = 0.0, length = 0.2;
            float shade = 1.0f;

            Veil() { setInterceptsMouseClicks (false, false); }
            void paint (juce::Graphics& g) override { g.fillAll (Colours::panelDark().withAlpha (shade)); }
            void timerCallback() override
            {
                const double t = juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() * 0.001 - start) / length);
                shade = (float) std::pow (1.0 - t, 2.0);   // quickly at first, then gently
                if (auto* c = target.getComponent(); c != nullptr && t < 1.0 && c->isShowing())
                {
                    setBounds (c->getBounds());   // (it follows the page if it's laid out again)
                    toFront (false);
                    repaint();
                    return;
                }
                stopTimer();
                delete this;   // (a Component deleted: it leaves its parent)
            }
        };

        auto* parent = c.getParentComponent();
        if (parent == nullptr || c.getBounds().isEmpty() || ! c.isShowing())
            return;
        auto* veil = new Veil();
        veil->target = &c;
        veil->length = ms * 0.001;
        veil->start = juce::Time::getMillisecondCounterHiRes() * 0.001;
        parent->addAndMakeVisible (veil);
        veil->setBounds (c.getBounds());
        veil->startTimerHz (60);
    }
    struct KeyTabs : juce::TabbedComponent
    {
        static constexpr int capInsetX = 2, capInsetY = 3;
        static constexpr int depth = Space::key + 2 * capInsetY;   // caps a key high

        explicit KeyTabs (int contentEdge) : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop), edge (contentEdge)
        {
            setTabBarDepth (depth);
            setOutline (0);
        }

        void resized() override
        {
            juce::TabbedComponent::resized();
            auto& bar = getTabbedButtonBar();
            bar.setBounds (bar.getBounds().withTrimmedLeft (edge - capInsetX));
        }

        struct Tab : juce::TabBarButton
        {
            using juce::TabBarButton::TabBarButton;

            int getBestTabLength (int) override
            {
                int columns = 1;
                for (auto& name : getTabbedButtonBar().getTabNames())
                    columns = juce::jmax (columns, KeyRow::columnsFor (name.toUpperCase(), Type::name));
                return Space::columns (columns) + 2 * capInsetX;
            }

            // A card, lit grey with a blue edge while its page shows, as the
            // sidebar's pages and the settings' sections.
            void paintButton (juce::Graphics& g, bool over, bool) override
            {
                const bool on = getToggleState();
                const auto r = getLocalBounds().toFloat().reduced ((float) capInsetX, (float) capInsetY);
                g.setColour (on ? juce::Colour (0xff4a4d52) : over ? juce::Colour (0xff383b3f) : juce::Colour (0xff313337));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
                if (on)
                {
                    g.setColour (juce::Colour (0xff3db4e0));
                    g.fillRoundedRectangle (r.withWidth (3.0f), 1.5f);
                }
                g.setColour (on ? juce::Colours::white : juce::Colour (0xffc9cbce));
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawText (getButtonText().toUpperCase(), r, juce::Justification::centred, false);
            }
        };

        juce::TabBarButton* createTabButton (const juce::String& name, int) override { return new Tab (name, getTabbedButtonBar()); }

        // A page shown: it rises a little into place as it fades in.
        void currentTabChanged (int index, const juce::String& name) override
        {
            if (auto* page = getCurrentContentComponent())
                riseIn (*page);
            if (onPageShown)
                onPageShown (index, name);
        }
        std::function<void (int index, const juce::String& name)> onPageShown;

        const int edge;
    };


    //==========================================================================
    // A round LED in its bezel. `level` 0 (unlit, a dim tinted lens) .. 1 (fully lit, with a glow).
    inline void drawLED (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float level)
    {
        const auto lens = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
        if (level > 0.0f)   // the glow round a lit LED, fading out from its edge
        {
            const auto halo = lens.expanded (radius * 1.8f);
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.7f * level), centre, colour.withAlpha (0.0f), halo.getTopLeft().withY (centre.y), true));
            g.fillEllipse (halo);
        }
        g.setColour (Colours::rim());
        g.fillEllipse (lens.expanded (1.0f));

        // Lit: saturated in the function's colour with a hot centre. Unlit: dark tinted glass.
        const auto unlit = colour.withMultipliedSaturation (0.6f).darker (2.6f);
        const auto core = unlit.interpolatedWith (colour.brighter (0.9f), level);
        const auto edge = unlit.interpolatedWith (colour.brighter (0.15f), level);
        g.setGradientFill (juce::ColourGradient (core, centre, edge, lens.getTopLeft(), true));
        g.fillEllipse (lens);
        g.setColour (juce::Colours::white.withAlpha (0.12f + 0.3f * level));
        g.fillEllipse (juce::Rectangle<float> (radius * 0.6f, radius * 0.45f).withCentre (centre.translated (-radius * 0.3f, -radius * 0.35f)));
    }

    // A knob: a coloured cap on a dark skirt over a printed scale.
    inline void drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float angle, float startAngle, float endAngle,
                          juce::Colour colour, bool enabled)
    {
        const float fade = enabled ? 1.0f : 0.45f;
        const auto c = bounds.getCentre();
        const float outer = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const float r = outer - 5.0f;   // room for the dots
        auto at = [c] (float a, float radius) { return c + juce::Point<float> (std::sin (a), -std::cos (a)) * radius; };
        auto circle = [c] (float radius) { return juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (c); };

        // Printed scale: 11 dots, the ends and centre a little larger.
        g.setColour (Colours::legend().withAlpha (0.85f * fade));
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
            const float d = (i == 0 || i == 5 || i == 10) ? 2.4f : 1.7f;
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (at (a, outer - 1.6f)));
        }

        g.setColour (juce::Colours::black.withAlpha (0.5f * fade));
        g.fillEllipse (circle (r).translated (0.8f, 2.0f));

        // Coloured plastic cap on a dark skirt, with its gloss.
        g.setColour (juce::Colour (0xff1c1c1d));
        g.fillEllipse (circle (r));
        const auto capArea = circle (r * 0.84f);
        g.setGradientFill (juce::ColourGradient (colour.brighter (0.45f).withMultipliedAlpha (fade), capArea.getTopLeft(),
                                                 colour.darker (0.55f).withMultipliedAlpha (fade), capArea.getBottomRight(), false));
        g.fillEllipse (capArea);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.35f * fade), capArea.getTopLeft(),
                                                 juce::Colours::transparentWhite, capArea.getCentre(), true));
        g.fillEllipse (capArea.reduced (capArea.getWidth() * 0.12f).translated (-capArea.getWidth() * 0.08f, -capArea.getWidth() * 0.1f));
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (capArea, 0.8f);

        const juce::Line<float> pointer (at (angle, r * 0.15f), at (angle, r * 0.92f));
        g.setColour (juce::Colours::black.withAlpha (0.5f * fade));
        g.drawLine (pointer, 3.4f);
        g.setColour (juce::Colours::white.withMultipliedAlpha (fade));
        g.drawLine (pointer, 2.0f);
    }

    // A click's ripple (Motion.h) over `area`: a soft disc of light spreading from where it landed,
    // fading as it goes, kept inside the key's shape.
    inline void drawRipple (juce::Graphics& g, juce::Rectangle<float> area, float corner, const Motion::State* motion, juce::Colour ink)
    {
        if (motion == nullptr || motion->ripple >= 1.0f || area.isEmpty())
            return;
        const float t = motion->ripple, eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        const float radius = std::hypot (area.getWidth(), area.getHeight()) * (0.12f + 0.88f * eased);
        const auto at = motion->rippleAt;
        juce::Graphics::ScopedSaveState keep (g);
        juce::Path shape;
        shape.addRoundedRectangle (area, corner);
        g.reduceClipRegion (shape);
        const float alpha = 0.22f * (1.0f - t) * (1.0f - t);
        g.setGradientFill (juce::ColourGradient (ink.withAlpha (alpha * 0.6f), at, ink.withAlpha (alpha), at.translated (radius, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (at));
    }

    // A key filling `bounds`: the desk's one key (LargeFormat::key), lit `lamp` 0..1 in `lit`,
    // hovered and pressed as its `motion` (Motion.h) says. Returns where the legend goes.
    inline juce::Rectangle<float> drawCap (juce::Graphics& g, juce::Rectangle<float> bounds, bool enabled, juce::Colour lit, float lamp, const Motion::State& motion)
    {
        LargeFormat::key (g, bounds, {}, lamp, lit, enabled, juce::jlimit (0.0f, 1.0f, motion.press), enabled ? motion.hover : 0.0f);
        return bounds.reduced (2.0f);
    }

    // A fader's plate: printed brushed aluminium.
    inline void drawPlate (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (Colours::rim());
        g.fillRoundedRectangle (r, 3.0f);
        const auto face = Colours::aluminium();
        g.setGradientFill (juce::ColourGradient (face.brighter (0.08f), r.getTopLeft(), face.darker (0.12f), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r.reduced (1.0f), 2.5f);
    }

    // A long-throw fader: dark slot, chunky ribbed cap.
    // `glow` 0..1: hovered or held (Motion.h): the cap's edge lights up.
    inline void drawFader (juce::Graphics& g, juce::Rectangle<float> track, float pos, bool vertical, bool enabled, float glow = 0.0f)
    {
        const float fade = enabled ? 1.0f : 0.4f;
        if (vertical)   // the console's long fader: a black round-ended slot, a white ribbed cap
        {
            const auto slotR = juce::Rectangle<float> (track.getCentreX() - 3.5f, track.getY(), 7.0f, track.getHeight());
            g.setColour (juce::Colour (0xff0b0b0c));
            g.fillRoundedRectangle (slotR, 3.5f);
            // (a tall cream cap with ridges above and below a black index
            // band, lit from above, darker at its foot, on a deep soft shadow)
            const auto capR = juce::Rectangle<float> (28.0f, 38.0f).withCentre ({ track.getCentreX(), pos });
            for (int i = 3; i >= 1; --i)
            {
                g.setColour (juce::Colours::black.withAlpha (0.14f * fade));
                g.fillRoundedRectangle (capR.translated (0.5f, 3.5f).expanded ((float) i), 2.0f + (float) i);
            }
            const auto base = juce::Colour (0xffe6dfcb);
            juce::ColourGradient body (base.brighter (0.12f).withMultipliedAlpha (fade), capR.getX(), capR.getY(), base.darker (0.32f).withMultipliedAlpha (fade), capR.getX(), capR.getBottom(), false);
            g.setGradientFill (body);
            g.fillRoundedRectangle (capR, 2.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.18f), capR.getX(), 0.0f,   // (its sides falling away)
                                                     juce::Colours::black.withAlpha (0.0f), capR.getX() + capR.getWidth() * 0.3f, 0.0f, false));
            g.fillRect (capR.withWidth (capR.getWidth() * 0.3f));
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), capR.getRight() - capR.getWidth() * 0.3f, 0.0f,
                                                     juce::Colours::black.withAlpha (0.18f), capR.getRight(), 0.0f, false));
            g.fillRect (capR.withLeft (capR.getRight() - capR.getWidth() * 0.3f));
            for (float d : { -14.0f, -10.0f, -6.0f, 6.0f, 10.0f, 14.0f })   // the ridges: a groove and the light on its lip
            {
                g.setColour (juce::Colours::black.withAlpha (0.32f * fade));
                g.fillRect (capR.getX() + 1.5f, pos + d - 0.5f, capR.getWidth() - 3.0f, 1.0f);
                g.setColour (juce::Colours::white.withAlpha (0.45f * fade));
                g.fillRect (capR.getX() + 1.5f, pos + d + 0.6f, capR.getWidth() - 3.0f, 0.8f);
            }
            g.setColour (juce::Colour (0xff0d0d0e).withMultipliedAlpha (fade));
            g.fillRect (capR.getX() + 1.0f, pos - 1.6f, capR.getWidth() - 2.0f, 3.2f);   // the index band
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.drawRoundedRectangle (capR, 2.0f, 0.9f);
            g.setColour (juce::Colours::white.withAlpha (0.5f * fade));
            g.fillRect (capR.reduced (2.0f, 0.0f).withHeight (1.0f).translated (0.0f, 0.6f));
            if (glow > 0.0f && enabled)
            {
                g.setColour (Colours::selected().withAlpha (0.6f * glow));
                g.drawRoundedRectangle (capR.expanded (1.0f), 2.5f, 1.2f);
            }
            return;
        }
        const auto slot = vertical ? juce::Rectangle<float> (track.getCentreX() - 2.5f, track.getY(), 5.0f, track.getHeight())
                                   : juce::Rectangle<float> (track.getX(), track.getCentreY() - 2.5f, track.getWidth(), 5.0f);
        g.setColour (juce::Colour (0xff0b0b0c));
        g.fillRoundedRectangle (slot, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        if (vertical) g.drawVerticalLine ((int) slot.getRight(), slot.getY() + 2.0f, slot.getBottom() - 2.0f);
        else          g.drawHorizontalLine ((int) slot.getBottom(), slot.getX() + 2.0f, slot.getRight() - 2.0f);

        const auto cap = vertical ? juce::Rectangle<float> (34.0f, 26.0f).withCentre ({ track.getCentreX(), pos })
                                  : juce::Rectangle<float> (18.0f, juce::jmin (30.0f, track.getHeight())).withCentre ({ pos, track.getCentreY() });
        g.setColour (juce::Colours::black.withAlpha (0.45f * fade));
        g.fillRoundedRectangle (cap.translated (1.0f, 2.5f), 3.0f);

        // The body, darker toward the edges, with grip ridges either side of the index line.
        const auto edge = juce::Colour (0xff8d9196), crest = juce::Colour (0xffeef0f2), flank = juce::Colour (0xffc4c8cc);
        juce::ColourGradient body (edge.withMultipliedAlpha (fade), cap.getTopLeft(),
                                   edge.withMultipliedAlpha (fade), vertical ? cap.getBottomLeft() : cap.getTopRight(), false);
        body.addColour (0.35, crest.withMultipliedAlpha (fade));
        body.addColour (0.6, flank.withMultipliedAlpha (fade));
        g.setGradientFill (body);
        g.fillRoundedRectangle (cap, 3.0f);

        for (float d : { -8.0f, -5.0f, 5.0f, 8.0f })
        {
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            if (vertical) g.fillRect (cap.getX() + 3.0f, pos + d, cap.getWidth() - 6.0f, 1.0f);
            else          g.fillRect (pos + d * 0.6f, cap.getY() + 3.0f, 1.0f, cap.getHeight() - 6.0f);
        }
        g.setColour (juce::Colour (0xff111111).withMultipliedAlpha (fade));
        if (vertical) g.fillRect (cap.getX() + 2.0f, pos - 1.0f, cap.getWidth() - 4.0f, 2.0f);
        else          g.fillRect (pos - 1.0f, cap.getY() + 2.0f, 2.0f, cap.getHeight() - 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (cap, 3.0f, 0.9f);
        if (glow > 0.0f && enabled)
        {
            g.setColour (Colours::ledBlue().withAlpha (0.18f * glow));
            g.fillRoundedRectangle (cap.expanded (2.5f), 4.5f);
            g.setColour (Colours::ledBlue().withAlpha (0.75f * glow));
            g.drawRoundedRectangle (cap.expanded (0.5f), 3.5f, 1.2f);
        }
    }

    // A touchscreen value box (modern themes): its name small at the top, the value in white in an
    // outlined black box under it (outlined in blue while it's being dragged). A slider marked
    // with setValueBox draws as one in a modern theme (drag up or down to change it).
    inline void setValueBox (juce::Slider& s, const juce::String& title) { s.getProperties().set ("boxTitle", title); }

    inline void drawValueBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, const juce::String& value,
                              bool enabled, float active = 0.0f, float hover = 0.0f)
    {
        const float fade = enabled ? 1.0f : 0.45f;
        if (title.isNotEmpty())
        {
            const auto head = r.removeFromTop (juce::jmin (14.0f, r.getHeight() * 0.4f));
            g.setColour (Colours::legendDim().withMultipliedAlpha (fade));
            g.setFont (legendFont (Type::legend));
            g.drawFittedText (title.toUpperCase(), head.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
        g.setColour (DotMatrix::glass().withMultipliedAlpha (fade));
        g.fillRoundedRectangle (r.reduced (0.5f), 3.0f);
        // Its outline brightens as it's hovered and turns blue (fading in) while it's dragged.
        active = juce::jlimit (0.0f, 1.0f, active);
        g.setColour (DotMatrix::bezel().brighter (0.5f * hover).interpolatedWith (Colours::ledBlue(), active).withMultipliedAlpha (fade));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f + 0.6f * active);
        g.setColour (DotMatrix::lit().withMultipliedAlpha (fade));
        g.setFont (juce::FontOptions (juce::jlimit (9.0f, 15.0f, r.getHeight() * 0.6f), juce::Font::bold));
        g.drawFittedText (value, r.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }

    //==========================================================================
    // The look-and-feel every view uses. applyTheme() picks up a theme change.
    class Look : public juce::LookAndFeel_V4
    {
    public:
        Look() { applyTheme(); }

        void applyTheme()
        {
            // The theme's typeface for all printing (screens and the event log keep their own);
            // cached typefaces are dropped so text picks it up.
            if (std::exchange (fontShown, Theme::get().font) != Theme::get().font || ! fontSet)
            {
                setDefaultSansSerifTypefaceName (Theme::get().font);
                juce::Typeface::clearTypefaceCache();
                fontSet = true;
            }

            setColour (juce::ResizableWindow::backgroundColourId, Colours::panelDark());
            setColour (juce::ComboBox::backgroundColourId, Colours::slot());
            setColour (juce::ComboBox::outlineColourId, Colours::rim());
            setColour (juce::ComboBox::textColourId, Colours::legend());
            setColour (juce::ComboBox::arrowColourId, Colours::legendDim());
            setColour (juce::TextButton::textColourOffId, Colours::capText());
            setColour (juce::TextButton::textColourOnId, Colours::capText());
            setColour (juce::TextButton::buttonOnColourId, Colours::selected());
            setColour (juce::Label::textColourId, Colours::legend());
            setColour (juce::Slider::textBoxOutlineColourId, DotMatrix::bezel());
            setColour (juce::Slider::textBoxBackgroundColourId, DotMatrix::glass());
            setColour (juce::Slider::textBoxTextColourId, DotMatrix::lit());
            setColour (juce::TextEditor::backgroundColourId, Colours::slot());
            setColour (juce::TextEditor::textColourId, Colours::legend());
            setColour (juce::TextEditor::outlineColourId, Colours::rim());
            setColour (juce::ListBox::backgroundColourId, Colours::panelDark());
            setColour (juce::ScrollBar::thumbColourId, Colours::capBottom());
            setColour (juce::PopupMenu::backgroundColourId, Colours::panelDark());
            setColour (juce::PopupMenu::textColourId, Colours::legend());
            setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::panel().brighter (0.25f));
            setColour (juce::PopupMenu::highlightedTextColourId, Colours::legend());
            setColour (juce::PopupMenu::headerTextColourId, Colours::legendDim());
            setColour (juce::TooltipWindow::backgroundColourId, Colours::capTop());
            setColour (juce::TooltipWindow::textColourId, Colours::capText());
            setColour (juce::TabbedComponent::backgroundColourId, Colours::panelDark());
            setColour (juce::TabbedComponent::outlineColourId, Colours::rim());
            setColour (juce::AlertWindow::backgroundColourId, Colours::panelDark());
            setColour (juce::AlertWindow::textColourId, Colours::legend());
            setColour (juce::AlertWindow::outlineColourId, Colours::rim());
        }

        // Buttons: every one the desk's one key (LargeFormat::key), lit as its lamp says.
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool, bool down) override
        {
            const auto& m = motion->of (b);
            const float press = juce::jlimit (0.0f, 1.0f, m.press + (down ? 1.0f : 0.0f));
            auto [lit, level] = lfLamp (b);
            LargeFormat::key (g, b.getLocalBounds().toFloat(), iconOf (b) != Icon::none ? juce::String() : b.getButtonText(), level, lit, b.isEnabled(), press, m.hover);
        }

        // The key drew its legend; an icon goes on it here.
        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            if (const auto icon = iconOf (b); icon != Icon::none)
            {
                auto [lit, level] = lfLamp (b);
                auto ink = level > 0.5f && lit.getPerceivedBrightness() < 0.6f ? juce::Colours::white : juce::Colour (0xff141414);
                if (icon == Icon::record && level <= 0.5f)
                    ink = Colours::ledRed();
                drawIcon (g, icon, b.getLocalBounds().toFloat().reduced (3.0f), ink.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.6f));
            }
        }

        juce::Font getTextButtonFont (juce::TextButton&, int height) override
        {
            return legendFont (juce::jmin (12.5f, (float) height * 0.62f));
        }

        // Knobs.
        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
        {
            const auto& m = motion->of (s);
            if (const auto title = s.getProperties()["boxTitle"]; ! title.isVoid())
                return drawValueBox (g, juce::Rectangle<int> (x, y, w, h).toFloat(), title.toString(), s.getTextFromValue (s.getValue()),
                                     s.isEnabled(), m.press, m.hover);

            const auto colour = Colours::knobGrey();
            const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
            // Hovered or turned: a soft halo in its colour round it.
            if (const float glow = juce::jmax (m.hover * 0.6f, juce::jlimit (0.0f, 1.0f, m.press)); glow > 0.0f && s.isEnabled())
            {
                const float outer = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
                const auto ring = juce::Rectangle<float> (outer * 2.0f, outer * 2.0f).withCentre (area.getCentre());
                juce::ColourGradient halo (colour.withAlpha (0.0f), area.getCentre(), colour.withAlpha (0.0f), ring.getTopLeft().withY (area.getCentreY()), true);
                halo.addColour (0.72, colour.withAlpha (0.0f));
                halo.addColour (0.86, colour.withAlpha (0.35f * glow));
                g.setGradientFill (halo);
                g.fillEllipse (ring);
            }
            drawKnob (g, area, start + pos * (end - start), start, end, colour, s.isEnabled());
        }

        // Faders.
        int getSliderThumbRadius (juce::Slider& s) override
        {
            return s.getSliderStyle() == juce::Slider::LinearVertical ? 14 : s.getSliderStyle() == juce::Slider::LinearHorizontal ? 10
                                                                        : LookAndFeel_V4::getSliderThumbRadius (s);
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                               juce::Slider::SliderStyle style, juce::Slider& s) override
        {
            if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal)
                return LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);

            // Horizontal faders sit on their own plate (a vertical one's plate is drawn by its
            // strip, round its printed scale too).
            if (style == juce::Slider::LinearHorizontal)
                drawPlate (g, juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h)
                                  .withSizeKeepingCentre ((float) (w + 2 * getSliderThumbRadius (s)), juce::jmin ((float) h, 30.0f)));
            const auto& m = motion->of (s);
            drawFader (g, juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h), pos, style == juce::Slider::LinearVertical, s.isEnabled(),
                       juce::jmax (m.hover * 0.45f, juce::jlimit (0.0f, 1.0f, m.press)));
        }

        // Selectors: recessed dark fields with a legend and a chevron.
        void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
        {
            const auto& m = motion->of (box);
            const auto field = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
            drawSlotField (g, field, juce::jmax (m.hover, m.open), box.isEnabled());
            drawRipple (g, field, 2.0f, &m, Colours::legend());
            // The chevron turns over while its list is open, and brightens as it's hovered.
            const auto arrow = juce::Rectangle<float> ((float) width - 12.0f, 0.0f, 8.0f, (float) height).withSizeKeepingCentre (8.0f, 4.5f);
            juce::Path p;
            p.startNewSubPath (arrow.getX(), arrow.getY());
            p.lineTo (arrow.getCentreX(), arrow.getBottom());
            p.lineTo (arrow.getRight(), arrow.getY());
            p.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi * m.open, arrow.getCentreX(), arrow.getCentreY()));
            g.setColour (Colours::legendDim().interpolatedWith (Colours::legend(), juce::jmax (m.hover, m.open)).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.7f));
            g.strokePath (p, juce::PathStrokeType (1.4f));
        }

        juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (Type::body); }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (3, 0, box.getWidth() - 17, box.getHeight());
            label.setFont (getComboBoxFont (box));
        }

        // Readouts on screens; other labels printed in capitals, like everything on the desk
        // (a label with the "keepCase" property is left as written).
        void drawLabel (juce::Graphics& g, juce::Label& label) override
        {
            if (DotMatrix::drawReadout (g, label))
                return;
            g.fillAll (label.findColour (juce::Label::backgroundColourId));
            const float alpha = label.isEnabled() ? 1.0f : 0.7f;
            if (! label.isBeingEdited())
            {
                const auto font = getLabelFont (label);
                const auto text = label.getProperties()["keepCase"] ? label.getText() : label.getText().toUpperCase();
                const auto area = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
                g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
                g.setFont (font);
                // Room for more than one line: wrap at full width rather than squeeze the letters.
                const int lines = juce::jmax (1, (int) ((float) area.getHeight() / font.getHeight()));
                g.drawFittedText (text, area, label.getJustificationType(), lines, lines > 1 ? 1.0f : label.getMinimumHorizontalScale());
                g.setColour (label.findColour (juce::Label::outlineColourId).withMultipliedAlpha (alpha));
            }
            else if (label.isEnabled())
                g.setColour (label.findColour (juce::Label::outlineColourId));
            g.drawRect (label.getLocalBounds());
        }

        // Tabs: a row of keys, the current one lit; they don't overlap (caps a unit apart, as keys are).
        int getTabButtonOverlap (int) override { return 0; }

        void drawTabButton (juce::TabBarButton& tab, juce::Graphics& g, bool, bool) override
        {
            // A key, the current one lit; the light fades across as the page changes; the tab moves as a key does.
            const auto light = Colours::selected();   // the current page: Selected
            const float lamp = lamps->level (tab);
            const auto area = drawCap (g, tab.getLocalBounds().toFloat().reduced (2.0f, 3.0f), true, light, lamp, motion->of (tab));
            g.setColour (lamp > 0.5f && light.getPerceivedBrightness() < 0.6f ? juce::Colours::white : juce::Colour (0xff141414));
            LargeFormat::legend (g, tab.getButtonText(), area, 12.0f);   // (the one key's legend: dark, or white on a dark lamp)
        }

        int getTabButtonBestWidth (juce::TabBarButton& tab, int depth) override
        {
            return juce::GlyphArrangement::getStringWidthInt (legendFont (Type::name), tab.getButtonText().toUpperCase()) + depth + 22;
        }

        void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int, int) override {}

        // Menus: a dark panel with a rim; the row under the pointer lit by a green lamp at its
        // edge; section headers printed as legends over a rule.
        void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
        {
            g.fillAll (Colours::panelDark());
            g.setColour (Colours::rim());
            g.drawRect (0, 0, width, height, 1);
        }

        // Menus are one size at every zoom (the zoom is the desktop's scale, which menus would
        // follow): their type and rows are divided by it, so they read the same at 80% or 150%.
        static float menuScale() { return 1.0f / juce::jmax (0.25f, juce::Desktop::getInstance().getGlobalScaleFactor()); }
        static constexpr float menuText = 15.0f, menuRow = 28.0f;

        juce::Font getPopupMenuFont() override { return juce::FontOptions (menuText * menuScale()); }

        void drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& text) override
        {
            const auto r = area.reduced (juce::roundToInt (10.0f * menuScale()), 0);
            g.setFont (legendFont (Type::name * menuScale()));
            g.setColour (Colours::legendDim());
            g.drawText (text.toUpperCase(), r.withTrimmedTop (4), juce::Justification::centredLeft, true);
            g.setColour (Colours::legendDim().withAlpha (0.35f));
            g.fillRect (r.getX(), r.getBottom() - 2, r.getWidth(), 1);
        }

        void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                                const juce::Drawable* icon, const juce::Colour* textColour) override
        {
            if (isHighlighted && isActive && ! isSeparator)
            {
                const auto r = area.toFloat().reduced (3.0f, 1.0f);
                g.setColour (Colours::ledGreen().withAlpha (0.16f));
                g.fillRoundedRectangle (r, 2.0f);
                g.setColour (Colours::ledGreen());
                g.fillRect (r.withWidth (2.5f));
            }
            LookAndFeel_V4::drawPopupMenuItem (g, area, isSeparator, isActive, false, isTicked, hasSubMenu, text.toUpperCase(), shortcutKeyText, icon, textColour);
        }

        void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardHeight, int& width, int& height) override
        {
            LookAndFeel_V4::getIdealPopupMenuItemSize (text.toUpperCase(), isSeparator, standardHeight, width, height);
            height = juce::roundToInt ((isSeparator ? 9.0f : menuRow) * menuScale());
        }

        // Dialogs: a panel with a rim and a green rule along the top, the title as a legend;
        // their keys are console keys and their fields recessed slots.
        void drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& layout) override
        {
            const auto r = alert.getLocalBounds();
            g.fillAll (Colours::panel());
            g.setColour (Colours::ledGreen());
            g.fillRect (r.withHeight (3));
            g.setColour (Colours::rim());
            g.drawRect (alert.getLocalBounds(), 1);
            layout.draw (g, textArea.toFloat());
        }

        juce::Font getAlertWindowTitleFont() override   { return legendFont (Type::display); }
        juce::Font getAlertWindowMessageFont() override { return juce::FontOptions (Type::body); }
        juce::Font getAlertWindowFont() override        { return juce::FontOptions (Type::body); }
        int getAlertWindowButtonHeight() override       { return 30; }

        void fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
        {
            drawSlotField (g, juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f), false, editor.isEnabled());
        }

        void drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
        {
            if (editor.hasKeyboardFocus (true) && ! editor.isReadOnly())
            {
                g.setColour (Colours::ledGreen().withAlpha (0.7f));
                g.drawRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f), 2.0f, 1.0f);
            }
        }

        // Tooltips: a rounded card in the keys' colour on a soft drop shadow, no border. The
        // window is see-through (MainComponent), with room round the card for the shadow.
        static constexpr int tooltipShadow = 10;

        juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> area) override
        {
            const auto layout = tooltipLayout (tip);
            const int w = (int) std::ceil (layout.getWidth()) + 20 + 2 * tooltipShadow;
            const int h = (int) std::ceil (layout.getHeight()) + 12 + 2 * tooltipShadow;
            auto r = juce::Rectangle<int> (pos.x - w / 2, pos.y + 12, w, h);   // under the pointer
            if (r.getBottom() > area.getBottom())
                r.setY (pos.y - h - 6);
            return r.constrainedWithin (area);
        }

        void drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height) override
        {
            const auto card = juce::Rectangle<float> ((float) width, (float) height).reduced ((float) tooltipShadow);
            juce::Path shape;
            shape.addRoundedRectangle (card, 4.0f);
            juce::DropShadow (juce::Colours::black.withAlpha (0.45f), tooltipShadow, { 0, 3 }).drawForPath (g, shape);
            g.setColour (Colours::capTop());
            g.fillPath (shape);
            tooltipLayout (text).draw (g, card.reduced (10.0f, 6.0f));
        }

        static juce::TextLayout tooltipLayout (const juce::String& text)
        {
            juce::AttributedString s;
            s.append (text.toUpperCase(), juce::Font (juce::FontOptions (Type::body)), Colours::capText());
            s.setJustification (juce::Justification::centredLeft);
            juce::TextLayout layout;
            layout.createLayout (s, 380.0f);
            return layout;
        }

        // Scrollbars: a dark groove and a thumb in the keys' colour.
        void drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height, bool vertical,
                            int thumbStart, int thumbSize, bool, bool) override
        {
            const auto& m = motion->of (bar);
            const auto track = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (1.0f);
            g.setColour (juce::Colour (0xff141415));
            g.fillRoundedRectangle (track, 3.0f);
            const auto thumb = vertical ? juce::Rectangle<float> (track.getX(), (float) thumbStart, track.getWidth(), (float) thumbSize)
                                        : juce::Rectangle<float> ((float) thumbStart, track.getY(), (float) thumbSize, track.getHeight());
            const auto colour = Colours::capBottom().brighter (0.15f * juce::jmax (m.hover, juce::jlimit (0.0f, 1.0f, m.press)));
            g.setColour (colour);
            g.fillRoundedRectangle (thumb.reduced (1.0f), 3.0f);
        }

        // `over` 0..1: how far it's hovered (eased: Motion.h).
        static void drawSlotField (juce::Graphics& g, juce::Rectangle<float> r, float over, bool enabled)
        {
            const float fade = enabled ? 1.0f : 0.5f;
            g.setColour (Colours::slot().brighter (0.12f * over).withMultipliedAlpha (fade));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawHorizontalLine ((int) r.getY(), r.getX() + 2.0f, r.getRight() - 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawHorizontalLine ((int) r.getBottom() - 1, r.getX() + 2.0f, r.getRight() - 2.0f);
            g.setColour (Colours::rim());
            g.drawRoundedRectangle (r, 2.0f, 1.0f);
        }

    private:
        // A key's lamp colour and how lit it is now (fading with its lamp).
        std::pair<juce::Colour, float> lfLamp (juce::Button& b)
        {
            const auto lit = b.findColour (juce::TextButton::buttonOnColourId);
            if (! b.getClickingTogglesState() && ! b.getToggleState())
                return { lit, 0.0f };
            return { lit, juce::jmax (b.getToggleState() ? 0.6f : 0.0f, lamps->level (b)) };
        }

        juce::SharedResourcePointer<Backlight::Lamps> lamps;
        juce::SharedResourcePointer<Motion::Animator> motion;
        juce::String fontShown;              // applyTheme: the typeface set (it's costly to set again)
        bool fontSet = false;
    };

    // A control's motion (Motion.h), for the views that draw their own keys with drawCap.
    inline const Motion::State* motionOf (juce::Component& c)
    {
        juce::SharedResourcePointer<Motion::Animator> animator;   // (the look-and-feel keeps it alive)
        return &animator->of (c);
    }
}

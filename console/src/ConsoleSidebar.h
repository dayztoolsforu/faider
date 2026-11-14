#pragma once

// Part of the console: Console.cpp includes it, and nothing else does.
// The sidebar down the left.

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../src/ui/LargeFormat.h"
#include "../../src/engine/AudioEngine.h"
#include "../../src/ui/Console.h"
#include "ConsoleView.h"

namespace ConsoleDesk
{
    // THE SIDEBAR down the left: the pages as cards (an icon and a name, the
    // one showing lit with a blue edge), then the desk's own cards (+ CHANNEL, + BUS).
    class Sidebar : public juce::Component, private juce::Timer
    {
    public:
        Sidebar (juce::TabbedComponent& t, juce::Component& console) : tabs (t), view (dynamic_cast<View*> (&console))
        {
            startTimerHz (10);   // (the page can change from the keyboard or a shortcut; the view scrolls)
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff2b2d30));
            g.setColour (Console::Colours::rail());
            g.fillRect (getLocalBounds().removeFromRight (1));
            g.setColour (Console::Colours::inkDim());
            g.setFont (LargeFormat::print (textSize));
            g.drawText ("FAIDER", juce::Rectangle<int> (14, 10, getWidth() - 20, 20), juce::Justification::centredLeft, false);
            const auto names = tabs.getTabNames();
            for (int i = 0; i < names.size(); ++i)
                card (g, cardArea (i), names[i].toUpperCase(), iconFor (names[i]), i == tabs.getCurrentTabIndex(), i == hovered, i == pushed);
            if (! onMixer())   // (the desk's own cards only while the channels show)
                return;
            g.setColour (Console::Colours::inkDim());
            g.setFont (LargeFormat::print (textSize));
            g.drawText ("DESK", deskTitle(), juce::Justification::centredLeft, false);
            card (g, actionArea (0), "ADD CHANNEL", "plus", false, hovered == 100, pushed == 100);
            card (g, actionArea (1), "ADD APP", "plus", false, hovered == 103, pushed == 103);
            if (simpleNow())   // (SIMPLE: no buses, plugins or strip sections to show)
                return;
            card (g, actionArea (2), "ADD BUS", "plus", false, hovered == 101, pushed == 101);
            card (g, actionArea (3), "PLUGINS", "pads", view != nullptr && view->pluginsOpen(), hovered == 102, pushed == 102);
            if (view == nullptr)
                return;
            auto& b = *view->bench;
            heading (g, "SELECT", selectTitle());
            pill (g, pairArea (selectTitle(), 0), "AUTO", b.autoSelect, hovered == 110, pushed == 110);
            pill (g, pairArea (selectTitle(), 1), "FOLLOW", b.followSelect, hovered == 111, pushed == 111);
            heading (g, "METER", meterTitle());   // (SETTINGS' CHANNEL METER POINT: the source, before the fader, after it)
            for (int i = 0; i < 3; ++i)
                pill (g, tripleArea (meterTitle(), i), i == 0 ? "IN" : i == 1 ? "PRE" : "POST", AudioEngine::meterPoint.load() == i, hovered == 120 + i, pushed == 120 + i);
            heading (g, "SECTIONS", sectionsTitle());
            for (int i = 0; i < b.sectionCount(); ++i)
                sectionRow (g, sectionArea (i), b.sectionName (i), (visibleSections() >> i) & 1, hovered == 200 + i);
        }

        void mouseMove (const juce::MouseEvent& e) override { setHovered (at (e.getPosition())); }
        void mouseExit (const juce::MouseEvent&) override { setHovered (-1); }
        void mouseUp (const juce::MouseEvent&) override { pushed = -1; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = at (e.getPosition());
            pushed = i;
            if (i >= 0 && i < 100)
                tabs.setCurrentTabIndex (i);
            else if (i == 100 && view != nullptr)
            {
                tabs.setCurrentTabIndex (0);
                view->bench->addChannel (localAreaToGlobal (actionArea (0)));
            }
            else if (i == 103 && view != nullptr)
            {
                tabs.setCurrentTabIndex (0);
                view->bench->addApp (localAreaToGlobal (actionArea (1)));
            }
            else if (i == 101 && view != nullptr)
            {
                tabs.setCurrentTabIndex (0);
                view->bench->addBusMenu (localAreaToGlobal (actionArea (2)));
            }
            else if (i == 102 && view != nullptr)
                view->showPlugins (! view->pluginsOpen());
            else if (i == 110 && view != nullptr)
                view->bench->setAutoSelect (! view->bench->autoSelect);
            else if (i == 111 && view != nullptr)
                view->bench->setFollowSelect (! view->bench->followSelect);
            else if (i >= 120 && i <= 122)
                AudioEngine::meterPoint = i - 120;   // (MainComponent saves it with the settings)
            else if (i >= 200 && view != nullptr)   // a section: the view glides to it
                view->bench->glideTo (-1.0f, view->bench->sectionSpan (i - 200).getStart() - 40.0f);
            repaint();
        }

    private:
        juce::Rectangle<int> cardArea (int i) const { return { 8, 38 + i * 40, getWidth() - 16, 34 }; }
        juce::Rectangle<int> deskTitle() const { return { 14, cardArea (tabs.getNumTabs()).getY() + 14, getWidth() - 20, 20 }; }
        juce::Rectangle<int> actionArea (int i) const { return { 8, deskTitle().getBottom() + 6 + i * 40, getWidth() - 16, 34 }; }
        int at (juce::Point<int> p) const
        {
            for (int i = 0; i < tabs.getNumTabs(); ++i)
                if (cardArea (i).contains (p))
                    return i;
            static const int actions[] = { 100, 103, 101, 102 };   // (ADD CHANNEL, ADD APP, ADD BUS, PLUGINS)
            for (int i = 0; i < (simpleNow() ? 2 : 4) && onMixer(); ++i)
                if (actionArea (i).contains (p))
                    return actions[i];
            if (onMixer() && view != nullptr && ! simpleNow())
            {
                for (int i = 0; i < 2; ++i)
                    if (pairArea (selectTitle(), i).contains (p)) return 110 + i;
                for (int i = 0; i < 3; ++i)
                    if (tripleArea (meterTitle(), i).contains (p)) return 120 + i;
                for (int i = 0; i < view->bench->sectionCount(); ++i)
                    if (sectionArea (i).contains (p))
                        return 200 + i;
            }
            return -1;
        }
        bool onMixer() const { return tabs.getCurrentTabIndex() == 0; }
        bool simpleNow() const { return view != nullptr && view->isSimple(); }
        juce::Rectangle<int> titleBelow (juce::Rectangle<int> above) const { return { 14, above.getBottom() + 14, getWidth() - 20, 20 }; }
        juce::Rectangle<int> selectTitle() const { return titleBelow (actionArea (3)); }
        juce::Rectangle<int> pairArea (juce::Rectangle<int> title, int i) const
        {
            const int w = (getWidth() - 20) / 2;
            return { 8 + i * (w + 4), title.getBottom() + 6, w, 30 };
        }
        juce::Rectangle<int> tripleArea (juce::Rectangle<int> title, int i) const   // (as pairArea, three across)
        {
            const int w = (getWidth() - 24) / 3;
            return { 8 + i * (w + 4), title.getBottom() + 6, w, 30 };
        }
        juce::Rectangle<int> meterTitle() const { return titleBelow (pairArea (selectTitle(), 0)); }
        juce::Rectangle<int> sectionsTitle() const { return titleBelow (pairArea (meterTitle(), 0)); }
        juce::Rectangle<int> sectionArea (int i) const { return { 8, sectionsTitle().getBottom() + 4 + i * 24, getWidth() - 16, 22 }; }
        // The sections in view now (a bit each), under the top bar.
        int visibleSections() const
        {
            if (view == nullptr)
                return 0;
            const auto seen = view->viewArea().toFloat().withTrimmedTop (40.0f);
            int mask = 0;
            for (int i = 0; i < view->bench->sectionCount(); ++i)
                if (const auto span = view->bench->sectionSpan (i); span.getEnd() > seen.getY() && span.getStart() < seen.getBottom())
                    mask |= 1 << i;
            return mask;
        }
        static void heading (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r)
        {
            g.setColour (Console::Colours::inkDim());
            g.setFont (LargeFormat::print (textSize));
            g.drawText (text, r, juce::Justification::centredLeft, false);
        }
        // A half-width card that's on or off (lit grey with a blue edge while on).
        static void pill (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, bool on, bool hover, bool press = false)
        {
            const auto rf = r.toFloat().translated (0.0f, press ? 1.0f : 0.0f);
            g.setColour ((on ? Console::Colours::chromeOn() : hover ? Console::Colours::chromeHover() : Console::Colours::chromeOff()).darker (press ? 0.35f : 0.0f));
            g.fillRoundedRectangle (rf, 3.0f);
            g.setColour (hover ? Console::Colours::accent().withAlpha (0.7f) : juce::Colours::black.withAlpha (0.55f));
            g.drawRoundedRectangle (rf.reduced (0.5f), 3.0f, 1.0f);
            if (on)
            {
                g.setColour (Console::Colours::accent());
                g.fillRoundedRectangle (rf.withWidth (3.0f), 1.5f);
            }
            g.setColour (on || hover ? juce::Colours::white : Console::Colours::ink());
            g.setFont (LargeFormat::print (textSize));
            g.drawFittedText (text, rf.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
        }
        // A section of the strip: lit while it's in view.
        static void sectionRow (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, bool inView, bool hover)
        {
            const auto rf = r.toFloat();
            if (hover)
            {
                g.setColour (juce::Colour (0xff383b3f));
                g.fillRoundedRectangle (rf, 3.0f);
            }
            const auto bar = rf.withWidth (3.0f).reduced (0.0f, 4.0f);
            g.setColour (Console::Colours::accent());
            if (inView)
                g.fillRoundedRectangle (bar, 1.5f);
            else
                g.drawRoundedRectangle (bar.reduced (0.5f), 1.0f, 1.0f);   // (out of view: an empty bar, its name still clear)
            g.setColour (inView || hover ? juce::Colours::white : juce::Colour (0xffc2c5c9));
            g.setFont (LargeFormat::print (textSize));
            g.drawText (text, rf.withTrimmedLeft (12.0f), juce::Justification::centredLeft, false);
        }
        static constexpr float textSize = 12.0f;   // (one size of capitals: headings and cards alike)
        void setHovered (int i) { if (i != hovered) { hovered = i; repaint(); } }
        static juce::String iconFor (const juce::String& page)
        {
            const auto p = page.toLowerCase();
            return p == "mixer" ? "faders" : p == "routing" ? "nodes" : p == "setup" ? "steps" : p == "tools" ? "pads" : p == "devices" ? "speaker" : p == "settings" ? "gear" : "dot";
        }

        // A card: a dark key-like panel, lit grey with a blue edge while its page shows.
        static void card (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, const juce::String& icon, bool on, bool hover, bool press = false)
        {
            const auto rf = r.toFloat().translated (0.0f, press ? 1.0f : 0.0f);
            g.setColour ((on ? Console::Colours::chromeOn() : hover ? Console::Colours::chromeHover() : Console::Colours::chromeOff()).darker (press ? 0.35f : 0.0f));
            g.fillRoundedRectangle (rf, 3.0f);
            g.setColour (hover ? Console::Colours::accent().withAlpha (0.7f) : juce::Colours::black.withAlpha (0.55f));
            g.drawRoundedRectangle (rf.reduced (0.5f), 3.0f, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (on ? 0.14f : 0.06f));   // (its top edge catching the light)
            g.fillRect (rf.reduced (3.0f, 0.0f).withHeight (1.0f).translated (0.0f, 1.0f));
            if (on)
            {
                g.setColour (Console::Colours::accent());
                g.fillRoundedRectangle (rf.withWidth (3.0f), 1.5f);
            }
            const auto ink = on || hover ? juce::Colours::white : Console::Colours::ink();
            drawIcon (g, icon, rf.withWidth (rf.getHeight()).reduced (9.0f).translated (4.0f, 0.0f), ink);
            g.setColour (ink);
            g.setFont (LargeFormat::print (textSize));
            g.drawText (text.toUpperCase(), rf.withTrimmedLeft (rf.getHeight() + 4.0f), juce::Justification::centredLeft, false);
        }

        static void drawIcon (juce::Graphics& g, const juce::String& icon, juce::Rectangle<float> r, juce::Colour ink)
        {
            g.setColour (ink);
            const float w = r.getWidth(), h = r.getHeight();
            if (icon == "faders")   // three faders, their caps at different heights
                for (int i = 0; i < 3; ++i)
                {
                    const float x = r.getX() + w * (0.2f + 0.3f * (float) i);
                    g.fillRect (x - 0.6f, r.getY(), 1.2f, h);
                    g.fillRoundedRectangle (x - w * 0.12f, r.getY() + h * (i == 1 ? 0.15f : i == 0 ? 0.55f : 0.35f), w * 0.24f, h * 0.22f, 1.0f);
                }
            else if (icon == "nodes")   // two boxes joined by a cable
            {
                g.drawRect (r.withSize (w * 0.38f, h * 0.38f), 1.4f);
                g.drawRect (r.withSize (w * 0.38f, h * 0.38f).withPosition (r.getRight() - w * 0.38f, r.getBottom() - h * 0.38f), 1.4f);
                juce::Path p;
                p.startNewSubPath (r.getX() + w * 0.38f, r.getY() + h * 0.19f);
                p.cubicTo (r.getCentreX() + w * 0.2f, r.getY() + h * 0.19f, r.getCentreX() - w * 0.2f, r.getBottom() - h * 0.19f, r.getRight() - w * 0.38f, r.getBottom() - h * 0.19f);
                g.strokePath (p, juce::PathStrokeType (1.4f));
            }
            else if (icon == "pads")   // a grid of pads
                for (int i = 0; i < 4; ++i)
                    g.fillRoundedRectangle (r.getX() + (float) (i % 2) * w * 0.55f, r.getY() + (float) (i / 2) * h * 0.55f, w * 0.42f, h * 0.42f, 1.5f);
            else if (icon == "speaker")   // a speaker cone
            {
                juce::Path p;
                p.addRectangle (r.getX(), r.getY() + h * 0.32f, w * 0.25f, h * 0.36f);
                p.addTriangle (r.getX() + w * 0.25f, r.getY() + h * 0.32f, r.getX() + w * 0.6f, r.getY(), r.getX() + w * 0.6f, r.getY() + h * 0.68f);
                p.addTriangle (r.getX() + w * 0.25f, r.getY() + h * 0.68f, r.getX() + w * 0.6f, r.getBottom(), r.getX() + w * 0.6f, r.getY() + h * 0.32f);
                g.fillPath (p);
                juce::Path wave;
                wave.addCentredArc (r.getX() + w * 0.62f, r.getCentreY(), w * 0.3f, h * 0.38f, 0.0f, 0.5f, 2.64f, true);
                g.strokePath (wave, juce::PathStrokeType (1.3f));
            }
            else if (icon == "gear")   // a cog: a ring with teeth
            {
                const auto c = r.getCentre();
                for (int i = 0; i < 8; ++i)
                {
                    const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                    g.fillRect (juce::Rectangle<float> (w * 0.16f, h * 0.2f).withCentre ({ c.x + std::sin (a) * w * 0.42f, c.y - std::cos (a) * h * 0.42f }));
                }
                g.drawEllipse (r.reduced (w * 0.18f), w * 0.14f);
            }
            else if (icon == "steps")   // three steps, a dot and a line each
                for (int i = 0; i < 3; ++i)
                {
                    const float y = r.getY() + h * (0.18f + 0.32f * (float) i);
                    g.fillEllipse (juce::Rectangle<float> (w * 0.22f, w * 0.22f).withCentre ({ r.getX() + w * 0.12f, y }));
                    g.fillRect (r.getX() + w * 0.34f, y - 0.8f, w * 0.66f, 1.6f);
                }
            else if (icon == "plus")
            {
                g.fillRect (juce::Rectangle<float> (w, 1.6f).withCentre (r.getCentre()).reduced (w * 0.15f, 0.0f));
                g.fillRect (juce::Rectangle<float> (1.6f, h).withCentre (r.getCentre()).reduced (0.0f, h * 0.15f));
            }
            else
                g.fillEllipse (r.reduced (w * 0.3f));
        }

        void timerCallback() override
        {
            if (const int now = tabs.getCurrentTabIndex(); now != shown)
            {
                shown = now;
                repaint();
            }
            if (const int now = onMixer() ? visibleSections() : 0; now != shownSections)
            {
                shownSections = now;
                repaint();
            }
            if (simpleNow() != shownSimple)   // (SIMPLE / PRO flipped: the desk's cards change)
            {
                shownSimple = simpleNow();
                repaint();
            }
            if (const int now = AudioEngine::meterPoint.load(); now != shownMeter)   // (changed in SETTINGS too)
            {
                shownMeter = now;
                repaint();
            }
        }

        juce::TabbedComponent& tabs;
        View* view;
        bool shownSimple = false;
        int hovered = -1, pushed = -1, shown = -1, shownSections = -1, shownMeter = -1;
    };
}

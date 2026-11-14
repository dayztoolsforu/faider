#pragma once

// Part of the console: Console.cpp includes it, and nothing else does.
// The Routing page.

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../src/ui/LargeFormat.h"
#include "../../src/engine/AudioEngine.h"
#include "../../src/ui/Console.h"
#include "ConsoleView.h"

namespace ConsoleDesk
{
    // THE ROUTING PAGE: every strip a block, across where it sits on the desk (scrolling with it),
    // in rows: inputs, buses, outputs (and output devices with no output yet, after them). The
    // routes run as straight lines, each source's along a lane of its own. A click on one block
    // then another switches the route between them (a source and where it goes, either way
    // round; on a combinator, the path clicked); click it again, or Esc, to let it go.
    class RoutingView : public juce::Component, private juce::Timer
    {
    public:
        explicit RoutingView (View& v) : view (v), bench (*v.bench), bind (v.bench->binding()), engine (*v.bench->getEngine())
        {
            setWantsKeyboardFocus (true);
            frames = std::make_unique<juce::VBlankAttachment> (this, [this] { if (bench.scrolling()) { bench.stepScroll(); repaint(); } });
            startTimerHz (15);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Console::Colours::rail());
            build();
            for (int row = 0; row < 3; ++row)
            {
                const auto band = juce::Rectangle<float> (0.0f, rowTop[row], (float) getWidth(), bandH);
                g.setColour (juce::Colour (0xff1f2023));
                g.fillRect (band);
                g.setColour (Console::Colours::recess());
                g.fillRect (band.withHeight (1.0f));
                g.fillRect (band.withY (band.getBottom() - 1.0f).withHeight (1.0f));
            }
            for (auto& w : wires)
                drawWire (g, w);
            for (auto& b : blocks)
                drawBlock (g, b);
            if (moving)   // AN INPUT BEING MOVED: where it'll land marked, its block under the mouse
                if (const auto* b = blockFor (pressed))
                {
                    const float dx = (float) view.stripsView().getViewPositionX();
                    g.setColour (Console::Colours::accent());
                    g.fillRect (juce::Rectangle<float> (bench.gapX (pressed, bench.insertionGap (pressed, deskX (dragAt))) - dx - 2.0f, b->r.getY() - 6.0f, 4.0f, b->r.getHeight() + 12.0f));
                    auto lifted = *b;
                    lifted.r.setX (dragAt - grab);
                    g.setColour (juce::Colours::black.withAlpha (0.4f));
                    g.fillRoundedRectangle (lifted.r.translated (4.0f, 6.0f), 3.0f);
                    drawBlock (g, lifted);
                }
            g.setFont (LargeFormat::print (11.0f));
            g.setColour (Console::Colours::inkDim());
            for (int row = 0; row < 3; ++row)   // (each row's name, at its left)
                g.drawText (row == 0 ? "INPUTS" : row == 1 ? "BUSES" : "OUTPUTS", juce::Rectangle<float> (12.0f, rowTop[row] + 4.0f, 120.0f, 14.0f), juce::Justification::centredLeft, false);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            grabKeyboardFocus();
            build();
            const auto* b = blockAt (e.position);
            pressed = b != nullptr && b->ch >= 0 && bench.canMove (b->ch) ? b->ch : -1;   // (a drag moves it, within its row)
            grab = b != nullptr ? e.position.x - b->r.getX() : 0.0f;
            moving = false;
            if (pressed < 0)
                click (e.position);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (pressed < 0)
                return;
            moving = moving || e.getDistanceFromDragStart() > 6;
            dragAt = e.position.x;
            repaint();
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (pressed >= 0 && moving)
                bench.moveInput (pressed, bench.insertionGap (pressed, deskX (e.position.x)));
            else if (pressed >= 0)
                click (e.mouseDownPosition);   // (held without a drag: a click)
            pressed = -1;
            moving = false;
            repaint();
        }

        // A click: the first block held; another, the route between them switched.
        void click (juce::Point<float> at)
        {
            build();
            const auto* b = blockAt (at);
            const auto* held = find (armed);
            if (b == nullptr || held == nullptr || b->key == armed)
                armed = b == nullptr || b->key == armed ? juce::String() : b->key;
            else if (! link (*held, *b, pathAt (*b, at)) && ! link (*b, *held, pathAt (*held, at)))
                armed = b->key;   // (no route between them: this one held instead)
            repaint();
        }

        bool keyPressed (const juce::KeyPress& k) override
        {
            if (k != juce::KeyPress::escapeKey || armed.isEmpty())
                return false;
            armed = {};
            repaint();
            return true;
        }

        // The wheel: across, with the desk.
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            const float d = (std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY) * (w.isReversed ? -1.0f : 1.0f);
            bench.scrollBy (-d * 480.0f, 0.0f);
        }

    private:
        struct Block
        {
            int ch = -1, row = 0, paths = 0;   // (ch -1: an output device with no output yet)
            juce::String key, name, label;
            juce::Rectangle<float> r;
            bool off = false;
        };
        struct Wire { juce::Path path, arrow; juce::Colour colour; juce::String from, to; };

        static constexpr float blockH = 48.0f, bandH = 22.0f + blockH + 12.0f, top = 16.0f;

        static juce::Colour rowColour (int row) { return row == 0 ? Console::Colours::palette ("teal") : row == 1 ? juce::Colour (0xffb59cf0) : Console::Colours::palette ("orange"); }
        static juce::Colour rowFill (int row)   { return row == 0 ? juce::Colour (0xff2a2c30) : row == 1 ? Console::Colours::kindBus().withMultipliedBrightness (0.37f) : Console::Colours::kindOutput().withMultipliedBrightness (0.36f); }

        // The blocks where they are now, the rows spaced down the page for the lanes between them,
        // and the wires.
        void build()
        {
            blocks.clear();
            wires.clear();
            const bool fresh = routesStale || routes.empty();
            if (fresh)
                routes.clear();
            routesStale = false;
            const float dx = (float) view.stripsView().getViewPositionX(), w = bench.stripWidth();
            for (int ch = 0; ch < bench.stripCount(); ++ch)
            {
                Block b;
                b.ch = ch;
                b.row = bind.isMaster (ch) ? 2 : bind.isBus (ch) ? 1 : 0;
                b.name = bind.text (ch, "name", {}).toUpperCase();
                b.key = juce::String (b.row) + ":" + b.name;
                b.off = ! bind.isOn (ch, "power", true);
                if (auto* s = bind.strip (ch); s != nullptr && b.row == 1 && s->combinator)
                    b.paths = (int) s->paths.size();
                b.r = { bench.stripLeft (ch) - dx + 6.0f, 0.0f, w - 12.0f, blockH };
                blocks.push_back (b);
            }
            float x = bench.deskRight() - dx;
            for (auto& out : engine.outputChoices())
                if (engine.masterPlayingOn (out.candidate, out.firstChannel) < 0)
                {
                    Block b;
                    b.row = 2;
                    b.label = out.label;
                    b.name = out.label.upToFirstOccurrenceOf (" (", false, false).toUpperCase();
                    b.key = "device:" + out.label;
                    b.r = { x + 6.0f, 0.0f, w - 12.0f, blockH };
                    x += w + 4.0f;
                    blocks.push_back (b);
                }

            // Every route on: from which strip, to which block (and path).
            struct Link { int from; const Block* to; int path; float sx = 0, ex = 0; };
            std::vector<Link> links;
            std::vector<int> sources[2];
            for (int ch = 0; ch < bench.stripCount(); ++ch)
                if (! bind.isMaster (ch) && bind.strip (ch) != nullptr)
                {
                    if (fresh)
                        routes[ch] = bind.routes (ch);
                    bool any = false;
                    for (auto& r : routes[ch])
                    {
                        const auto* to = r.on ? (r.target >= 0 ? blockFor (r.target) : findLabel (r.label)) : nullptr;
                        if (to == nullptr)
                            continue;
                        if (to->paths > 0)
                        {
                            for (int p = 0; p < to->paths; ++p)
                                if (feedsPath (ch, to->ch, p))
                                    links.push_back ({ ch, to, p }), any = true;
                        }
                        else
                            links.push_back ({ ch, to, -1 }), any = true;
                    }
                    if (any)
                        sources[bind.isBus (ch) ? 1 : 0].push_back (ch);
                }

            // THE ROWS down the page: each gap as deep as its lanes need, the lanes spaced as the
            // page allows (each source a lane; the leftmost lowest, so the lanes don't cross the
            // drops into them; everything the desk sends goes right).
            const float pad = 22.0f;
            const int lanes = (int) (sources[0].size() + sources[1].size());
            const float room = (float) getHeight() - top - 3.0f * bandH - 4.0f * pad - 24.0f;
            const float pitch = juce::jlimit (12.0f, 48.0f, room / (float) juce::jmax (1, lanes + 2));
            const float gaps[2] = { 2.0f * pad + pitch * (float) juce::jmax (1, (int) sources[0].size()),
                                    2.0f * pad + pitch * (float) juce::jmax (1, (int) sources[1].size()) };
            rowTop[0] = top;
            rowTop[1] = rowTop[0] + bandH + gaps[0];
            rowTop[2] = rowTop[1] + bandH + gaps[1];
            for (auto& b : blocks)
                b.r.setY (rowTop[b.row] + 22.0f);
            std::map<int, float> laneY;
            for (int row = 0; row < 2; ++row)
            {
                auto& list = sources[row];
                std::sort (list.begin(), list.end(), [this] (int a, int b) { return bench.stripLeft (a) < bench.stripLeft (b); });
                const float gapBottom = rowTop[row] + bandH + gaps[row] - pad;
                for (size_t i = 0; i < list.size(); ++i)
                    laneY[list[i]] = gapBottom - pitch * ((float) i + 0.5f);
            }

            // Where each wire leaves and arrives: spread across the block (or the path's cell), in
            // order across, so wires side by side never swap over.
            auto spread = [] (std::vector<Link*>& group, juce::Rectangle<float> r, bool out, float& (*at) (Link&))
            {
                const float span = r.getWidth() * 0.8f;
                const float step = juce::jmin (10.0f, span / (float) juce::jmax (1, (int) group.size()));
                for (size_t i = 0; i < group.size(); ++i)
                    at (*group[i]) = r.getCentreX() + ((float) i - (float) (group.size() - 1) * 0.5f) * step;
                juce::ignoreUnused (out);
            };
            std::map<int, std::vector<Link*>> leaving;
            std::map<std::pair<const Block*, int>, std::vector<Link*>> arriving;
            for (auto& l : links)
            {
                leaving[l.from].push_back (&l);
                arriving[{ l.to, l.path }].push_back (&l);
            }
            for (auto& [ch, group] : leaving)   // (leaving: those going furthest on the right)
            {
                std::sort (group.begin(), group.end(), [] (Link* a, Link* b) { return a->to->r.getCentreX() < b->to->r.getCentreX(); });
                if (const auto* from = blockFor (ch))
                    spread (group, from->r, true, [] (Link& l) -> float& { return l.sx; });
            }
            for (auto& [to, group] : arriving)   // (arriving: those from furthest left on the left)
            {
                std::sort (group.begin(), group.end(), [this] (Link* a, Link* b) { return bench.stripLeft (a->from) < bench.stripLeft (b->from); });
                spread (group, to.second >= 0 ? pathCell (*to.first, to.second) : to.first->r, false, [] (Link& l) -> float& { return l.ex; });
            }

            for (auto& l : links)
            {
                const auto* from = blockFor (l.from);
                if (from == nullptr)
                    continue;
                const float ly = laneY[l.from];
                const bool below = l.to->row > from->row;
                const juce::Point<float> end { l.ex, below ? l.to->r.getY() : l.to->r.getBottom() };
                juce::Path p;
                p.startNewSubPath (l.sx, from->r.getBottom());
                p.lineTo (l.sx, ly);
                p.lineTo (end.x, ly);
                p.lineTo (end);
                Wire wire { p.createPathWithRoundedCorners (6.0f), {}, rowColour (from->row), from->key, l.to->key };
                const float dir = below ? 1.0f : -1.0f;   // (its arrowhead, into the block)
                wire.arrow.addTriangle (end.x - 4.0f, end.y - 7.0f * dir, end.x + 4.0f, end.y - 7.0f * dir, end.x, end.y);
                wires.push_back (std::move (wire));
            }
        }

        // A source feeding a combinator's path (nothing chosen: its first).
        bool feedsPath (int src, int busCh, int p) const
        {
            auto* s = bind.strip (src);
            const int c = bind.indexOf (busCh);
            if (s == nullptr || ! s->busSends[(size_t) c].load())
                return false;
            const unsigned bits = s->pathSends[(size_t) c].load();
            return bits == 0 ? p == 0 : (bits & (1u << p)) != 0;
        }

        // The route from one block to another, switched (true), or none between them (false).
        bool link (const Block& from, const Block& to, int path)
        {
            if (from.ch < 0 || from.row == 2 || &from == &to)
                return false;
            auto list = bind.routes (from.ch);
            for (auto& r : list)
                if ((to.ch >= 0 && r.target == to.ch) || (to.ch < 0 && r.label == to.label))
                {
                    if (to.paths > 0 && path >= 0)
                        togglePath (from.ch, to.ch, path, r);
                    else
                        r.toggle();
                    routesStale = true;
                    if (bind.channels() != bench.stripCount())   // (a new output made: the desk lays out again)
                        bench.repaint();
                    return true;
                }
            return false;
        }
        void togglePath (int src, int busCh, int p, Binding::Route& r)
        {
            auto* s = bind.strip (src);
            const int c = bind.indexOf (busCh);
            const bool sending = s->busSends[(size_t) c].load();
            const unsigned bits = s->pathSends[(size_t) c].load(), now = sending ? (bits == 0 ? 1u : bits) : 0u, next = now ^ (1u << p);
            if (next == 0)
            {
                r.toggle();   // (the last path: the send off)
                s->pathSends[(size_t) c] = 0;
                return;
            }
            if (! sending)
                r.toggle();   // (refused if it would loop)
            if (s->busSends[(size_t) c].load())
                s->pathSends[(size_t) c] = next;
        }

        void drawWire (juce::Graphics& g, const Wire& w) const
        {
            const bool involved = armed.isNotEmpty() && (w.from == armed || w.to == armed);
            g.setColour (w.colour.withAlpha (armed.isEmpty() ? 0.6f : involved ? 0.95f : 0.3f));   // (the rest stay visible behind the ones involved)
            g.strokePath (w.path, juce::PathStrokeType (involved ? 2.2f : 1.6f));
            g.fillPath (w.arrow);
        }

        void drawBlock (juce::Graphics& g, const Block& b) const
        {
            const bool held = b.key == armed, over = b.key == hoverKey;
            const bool linked = armed.isNotEmpty() && ! held && std::any_of (wires.begin(), wires.end(), [&] (const Wire& w) { return (w.from == armed && w.to == b.key) || (w.to == armed && w.from == b.key); });
            const auto r = b.r;
            g.setColour (b.ch < 0 ? juce::Colour (0xff26272a) : rowFill (b.row));
            g.fillRoundedRectangle (r, 3.0f);
            if (const auto colour = b.ch >= 0 ? bench.channelColour (b.ch) : juce::Colours::transparentBlack; ! colour.isTransparent())
            {
                g.setColour (colour.withAlpha (0.25f));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (colour);
                g.fillRoundedRectangle (r.withWidth (4.0f), 2.0f);
            }
            g.setColour (juce::Colours::white.withAlpha (0.07f));   // (its top edge catching the light)
            g.fillRect (r.reduced (3.0f, 0.0f).withHeight (1.0f).translated (0.0f, 1.0f));
            if (held)
            {
                LargeFormat::bloom (g, r, Console::Colours::accent(), 0.5f + 0.3f * glow, 3.0f, 6.0f);
                g.setColour (Console::Colours::accent());
                g.drawRoundedRectangle (r, 3.0f, 2.0f);
            }
            else
            {
                g.setColour (linked ? rowColour (b.row) : over ? Console::Colours::accent().withAlpha (0.75f) : juce::Colours::black.withAlpha (0.6f));
                g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, linked || over ? 1.6f : 1.0f);
            }
            if (over && ! held)   // (under the mouse: the face lifts)
            {
                g.setColour (juce::Colours::white.withAlpha (0.07f));
                g.fillRoundedRectangle (r, 3.0f);
            }
            auto text = r.reduced (8.0f, 0.0f);
            auto nameArea = b.paths > 0 ? text.removeFromTop (26.0f) : text;
            g.setColour (b.off ? Console::Colours::inkFaint() : b.ch < 0 ? juce::Colour (0xffb8bcc1) : juce::Colours::white);
            g.setFont (LargeFormat::print (12.0f));
            g.drawFittedText (b.name, nameArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            if (b.ch >= 0)
            {
                g.setColour (Console::Colours::inkDim());
                g.setFont (LargeFormat::print (10.0f));
                g.drawText (juce::String (b.ch + 1).paddedLeft ('0', 3), nameArea.withTrimmedTop (4.0f).withHeight (12.0f), juce::Justification::topRight, false);
            }
            for (int p = 0; p < b.paths; ++p)   // A COMBINATOR: its paths, each a cell to route into
            {
                const auto cell = pathCell (b, p);
                const auto* from = find (armed);
                const bool fed = from != nullptr && from->ch >= 0 && from->row < 2 && feedsPath (from->ch, b.ch, p);
                g.setColour (fed ? rowColour (b.row).withAlpha (0.9f) : juce::Colour (0xff1c1a22));
                g.fillRoundedRectangle (cell, 2.0f);
                g.setColour (rowColour (b.row).withAlpha (fed ? 1.0f : 0.5f));
                g.drawRoundedRectangle (cell, 2.0f, 1.0f);
                g.setColour (fed ? juce::Colour (0xff1c1a22) : rowColour (b.row));
                g.setFont (LargeFormat::print (10.0f));
                g.drawText ("P" + juce::String (p + 1), cell, juce::Justification::centred, false);
            }
            if (b.off)   // (switched off: dimmed, as on the desk, its name still clear)
            {
                g.setColour (Console::Colours::bayDark().withAlpha (0.4f));
                g.fillRoundedRectangle (r, 3.0f);
            }
        }

        juce::Rectangle<float> pathCell (const Block& b, int p) const
        {
            const auto strip = b.r.reduced (6.0f, 0.0f).withTrimmedTop (26.0f).withTrimmedBottom (5.0f);
            const float w = strip.getWidth() / (float) juce::jmax (1, b.paths);
            return juce::Rectangle<float> (strip.getX() + w * (float) p, strip.getY(), w, strip.getHeight()).reduced (1.5f, 0.0f);
        }
        int pathAt (const Block& b, juce::Point<float> pos) const
        {
            for (int p = 0; p < b.paths; ++p)
                if (pathCell (b, p).contains (pos))
                    return p;
            return -1;
        }

        const Block* blockAt (juce::Point<float> p) const
        {
            for (auto& b : blocks)
                if (b.r.contains (p))
                    return &b;
            return nullptr;
        }
        const Block* find (const juce::String& key) const
        {
            for (auto& b : blocks)
                if (key.isNotEmpty() && b.key == key)
                    return &b;
            return nullptr;
        }
        const Block* blockFor (int ch) const
        {
            for (auto& b : blocks)
                if (b.ch == ch)
                    return &b;
            return nullptr;
        }
        const Block* findLabel (const juce::String& label) const
        {
            for (auto& b : blocks)
                if (b.ch < 0 && b.label == label)
                    return &b;
            return nullptr;
        }

        void timerCallback() override
        {
            if (! isShowing())
                return;
            glow = 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.006f);   // (the held block breathing)
            routesStale = true;   // (routes changed elsewhere show within a tick)
            repaint();
        }

        View& view;
        Bench& bench;
        Binding& bind;
        AudioEngine& engine;
        std::vector<Block> blocks;
        std::vector<Wire> wires;
        std::map<int, std::vector<Binding::Route>> routes;
        float rowTop[3] { top, top, top };
        juce::String armed;   // (the block clicked first)
        juce::String hoverKey;   // (the block under the mouse)
        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto* b = blockAt (e.position);
            if (const auto k = b != nullptr ? b->key : juce::String(); k != hoverKey) { hoverKey = k; repaint(); }
        }
        void mouseExit (const juce::MouseEvent&) override { if (hoverKey.isNotEmpty()) { hoverKey = {}; repaint(); } }
        std::unique_ptr<juce::VBlankAttachment> frames;   // (the desk's smooth scroll, stepped while this page shows)
        bool routesStale = true;
        int pressed = -1;     // (an input's block held: dragged, it moves the channel)
        bool moving = false;
        float grab = 0.0f, dragAt = 0.0f;
        float deskX (float x) const { return x + (float) view.stripsView().getViewPositionX(); }
        float glow = 0.0f;
    };
}

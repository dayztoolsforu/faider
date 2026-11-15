#pragma once

// Part of the console: Console.cpp includes it, and nothing else does.
// Bench: the desk, its strips side by side from the layout.

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../src/ui/Theme.h"
#include "../../src/ui/LargeFormat.h"
#include "../../src/ui/DotMatrix.h"
#include "../../src/engine/AudioEngine.h"
#include "DeskOrder.h"
#include "../../src/ui/MixerComponent.h"
#include "../../src/ui/MidiMap.h"
#include "../../src/ui/MixMinusBuilder.h"
#include "../../src/ui/Console.h"
#include "ConsoleLayout.h"
#include "ConsoleBinding.h"

// The bench: `channels` strips side by side from the layout, at `zoom`.
class Bench : public juce::Component, public MidiMap::Provider, private juce::Timer
{
public:
    // MIDI MAP: every strip's fader, pan, CUT, SOLO, power and routing boxes, where they're drawn.
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> midiTargets() override
    {
        std::vector<std::pair<juce::Rectangle<int>, juce::String>> list;
        if (! live())
            return list;
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto key = stripKey (ch);
            if (key.startsWith ("ch:"))
                continue;
            for (auto& s : layoutFor (ch).sections)
                for (auto& item : s.items)
                {
                    if (! shows (s))
                        continue;   // (folded away: nothing there to bind)
                    const auto r = itemArea (item, ch);
                    if (item.kind == "fader")                        list.push_back ({ r.toNearestInt(), "fader:" + key });
                    else if (item.kind == "knob" && item.id == "pan") list.push_back ({ r.toNearestInt(), "pan:" + key });
                    else if (item.kind == "key" && item.id == "cut")  list.push_back ({ r.toNearestInt(), "mute:" + key });
                    else if (item.kind == "key" && item.id == "solo") list.push_back ({ r.toNearestInt(), "solo:" + key });
                    else if (item.kind == "name")                     list.push_back ({ r.withWidth (r.getHeight()).toNearestInt(), "on:" + key });
                    else if (item.kind == "routes")
                    {
                        const auto routes = bind.routes (ch);
                        for (int i = 0; i < (int) routes.size(); ++i)
                        {
                            const int to = routes[(size_t) i].target;
                            const auto box = routeBox (r, i, (int) routes.size());
                            if (to < 0 || box.isEmpty())
                                continue;   // (a device with no output yet: nothing to bind to)
                            const auto toKey = stripKey (to);
                            list.push_back ({ box.toNearestInt(), "send:" + key + ":" + toKey });
                        }
                    }
                }
        }
        return list;
    }

    struct Hit { const Item* item = nullptr; int ch = -1; };   // (a part, and the strip it's on)

    Bench (juce::File layoutFile, AudioEngine* e, MixerComponent* m) : file (std::move (layoutFile)), engine (e), mixer (m), bind (e)
    {
        setWantsKeyboardFocus (true);
        channels = bind.channels();
        loadSpacers();
        reload();
        setOpaque (true);
        startTimerHz (30);   // (the meters; the layout file checked a few times a second)
        lastFrame = juce::Time::getMillisecondCounterHiRes();
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { onFrame(); });   // (motion in step with the display)
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = Theme::get();
        g.fillAll (t.background);
        if (layout.error.isNotEmpty())
        {
            g.setColour (t.ledRed);
            g.drawText (layout.error, getLocalBounds().reduced (20), juce::Justification::topLeft);
            return;
        }
        const auto clip = g.getClipBounds().toFloat();
        for (auto& col : spacerColumns)   // BLANK SPACERS: breathing room, the panel left empty
        {
            if (col.x > clip.getRight() || col.x + col.w < clip.getX())
                continue;
            float y0 = 40.0f;
            const float total = stripHeight();
            const auto area = juce::Rectangle<float> (col.x, y0, col.w - 2.0f * zoom, total);
            g.setColour (juce::Colour (0xff111214));
            g.fillRect (area);
            for (auto& sec : layout.sections)   // (the sections' seams carried across, so the desk reads as one)
            {
                if (! shows (sec))
                    continue;
                g.setColour (Console::Colours::recess());
                g.fillRect (area.withY (y0).withHeight (juce::jmax (1.0f, 1.5f * zoom)));
                y0 += sec.height * zoom;
            }
            g.setColour (Console::Colours::recess());
            g.fillRect (juce::Rectangle<float> (area.getRight(), 40.0f, 2.0f * zoom, total));
        }
        for (int ch = 0; ch < channels; ++ch)
            if (ch != dragging || ! dragMoved)
                paintStrip (g, ch, drawX (ch), clip);
        if (dragging >= 0 && dragMoved)   // DRAGGING A CHANNEL: its place left dark, where it'll land marked, the strip lifted under the mouse
        {
            const float h = stripHeight();
            const float w = (layout.width + 2.0f) * zoom;
            g.setColour (juce::Colour (0xff111214));
            g.fillRect (juce::Rectangle<float> (stripX (dragging), 40.0f, w, h));
            g.setColour (Console::Colours::accent());
            g.fillRect (juce::Rectangle<float> (gapX (dragging, insertionGap (dragging, dragX)) - 2.0f * zoom, 40.0f, 4.0f * zoom, h));
            const float x = dragX - dragGrab;
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillRect (juce::Rectangle<float> (x + 8.0f * zoom, 52.0f, w, h));
            g.beginTransparencyLayer (0.9f);
            paintStrip (g, dragging, x, getLocalBounds().toFloat());
            g.endTransparencyLayer();
        }
        paintValue (g, held.item != nullptr && draggable (*held.item) ? held : hovered);   // (the knob being turned, else the one under the mouse)
    }

    void paintStrip (juce::Graphics& g, int ch, float x0, juce::Rectangle<float> clip)
    {
        if (x0 > clip.getRight() || x0 + (layout.width + 2.0f) * zoom < clip.getX())
            return;   // (off the area being drawn: the meters redraw one strip at a time)
        float y0 = 40.0f;
        light = ! darkPanel (ch);   // (the light strip, or an output's or combinator's dark one)
        for (auto& s : layoutFor (ch).sections)
        {
            if (! shows (s))
                continue;   // (COMPACT: folded away)
            const auto at = juce::AffineTransform::scale (zoom).translated (x0, y0);
            const auto area = juce::Rectangle<float> (layout.width, s.height).transformedBy (at);
            g.setColour (s.panel == "dark" ? Console::Colours::stripDark() : s.panel == "name" ? (ch == selected ? Console::Colours::accent().withMultipliedBrightness (0.60f) : bind.isMaster (ch) ? Console::Colours::kindOutput().withMultipliedBrightness (0.36f) : bind.isBus (ch) ? Console::Colours::kindBus().withMultipliedBrightness (0.37f) : Console::Colours::stripBody())
                                            : light ? Console::Colours::stripLight() : Console::Colours::stripBody());
            g.fillRect (area);
            if (const auto colour = channelColour (ch); s.panel == "name" && ! colour.isTransparent())   // (its own colour, as Faider shows it: a band along the bar)
            {
                g.setColour (colour.withAlpha (ch == selected ? 0.35f : 0.28f));
                g.fillRect (area);
                g.setColour (colour);
                g.fillRect (area.withHeight (juce::jmax (2.0f, 4.0f * zoom)));
            }
            g.setColour (light && s.panel.isEmpty() ? Console::Colours::stripLight().withMultipliedBrightness (0.68f) : Console::Colours::recess());
            g.fillRect (area.withHeight (juce::jmax (1.0f, 1.5f * zoom)));
            for (auto& item : s.items)
            {
                // (a part away from the area being drawn: skipped; a line can reach past its box)
                if (item.kind != "line" && ! clip.intersects ((item.r * zoom).translated (x0, y0).expanded (24.0f * zoom)))
                    continue;
                const bool faint = item.id == "width" && ! bind.isMaster (ch) && bind.strip (ch) != nullptr && ! bind.strip (ch)->source.stereo;
                if (faint)   // (WIDTH on a mono channel: nothing to widen)
                    g.beginTransparencyLayer (0.5f);
                draw (g, item, at, ch);
                if (faint)
                    g.endTransparencyLayer();
            }
            y0 += s.height * zoom;
        }
        if (const float off = 1.0f - powerShown (ch); off > 0.005f)   // OFF: the strip dims and greys, its name bar still lit to switch it back
        {
            const auto column = juce::Rectangle<float> (x0, 40.0f, layout.width * zoom, y0 - 40.0f);
            float nameTop = 40.0f, nameH = 0.0f;
            for (auto& sec : layoutFor (ch).sections)
            {
                if (! shows (sec))
                    continue;
                if (sec.panel == "name") { nameH = sec.height * zoom; break; }
                nameTop += sec.height * zoom;
            }
            g.setColour (Console::Colours::bayDark().withAlpha (0.5f * off));   // (dimmed, not hidden: everything on it still reads)
            g.fillRect (column.withBottom (nameTop));
            g.fillRect (column.withTop (nameTop + nameH));
            g.setColour (juce::Colour (0xff808286).withAlpha (0.14f * off));   // (the colour washed out of it)
            g.fillRect (column.withBottom (nameTop));
            g.fillRect (column.withTop (nameTop + nameH));
            const auto tag = juce::Rectangle<float> (46.0f * zoom, 18.0f * zoom).withCentre ({ column.getCentreX(), nameTop + nameH + 16.0f * zoom });
            g.setColour (juce::Colour (0xff101113).withAlpha (0.9f * off));
            g.fillRoundedRectangle (tag, 3.0f * zoom);
            g.setColour (Console::Colours::ink().withAlpha (off));
            g.setFont (LargeFormat::print (juce::jmax (9.0f, 11.0f * zoom)));
            g.drawText ("OFF", tag, juce::Justification::centred, false);
        }
        g.setColour (Console::Colours::recess());   // (the seam between strips)
        g.fillRect (juce::Rectangle<float> (x0 + layout.width * zoom, 40.0f, 2.0f * zoom, y0 - 40.0f));
        if (ch == selected)   // SEL: the strip you're on, outlined in blue
        {
            g.setColour (Console::Colours::accent());
            g.drawRect (juce::Rectangle<float> (x0, 40.0f, layout.width * zoom, y0 - 40.0f).reduced (1.0f), juce::jmax (1.5f, 2.0f * zoom));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pointerDownAt = e.position;
        trackPointer (e.position, ! e.mods.isPopupMenu());   // (the part under it sinks while held)
        if (const int sp = spacerAt (e.position); sp >= 0)   // a spacer: its menu on a right-click
        {
            if (e.mods.isPopupMenu())
                showSpacerMenu (sp);
            return;
        }
        if (e.mods.isPopupMenu())   // (right-click on a strip: its menu)
        {
            if (const int ch = stripAt (e.position); ch >= 0)
                showMenu (ch);
            return;
        }
        held = hit (e.position);
        if (held.item != nullptr && held.item->kind == "name")
        {
            const auto bar = itemArea (*held.item, held.ch);
            if (e.position.x < bar.getX() + bar.getHeight() * 1.2f)   // its power symbol: the strip on or off
            {
                bind.toggle (held.ch, "power", true);
                held = {};
                repaint();
                return;
            }
            if (canMove (held.ch))   // (a drag moves it; a click selects it, on mouse up)
            {
                dragging = held.ch;
                dragMoved = false;
                dragGrab = e.position.x - stripX (held.ch);
                dragX = e.position.x;
                held = {};
                return;
            }
            select (held.ch);   // (a click: the strip selected; its menu is a right-click)
            held = {};
            return;
        }
        if (held.item != nullptr && locked (held.ch))   // (switched off: nothing on it changes until it's back on)
        {
            held = {};
            return;
        }
        if (held.item != nullptr && autoSelect && held.ch != selected && held.item->id != "sel")   // AUTO SELECT: touching a strip selects it
            select (held.ch);
        if (held.item != nullptr && held.item->kind == "paths")   // a COMBINATOR's path: go to what feeds it, or open a plugin
        {
            for (auto& part : pathParts (held.ch, itemArea (*held.item, held.ch)))
                if (part.r.contains (e.position))
                {
                    if (part.kind == 1 && part.target >= 0)
                        goToStrip (part.target);
                    else if (part.kind == 2 && part.plugin != nullptr && mixer != nullptr)
                        mixer->openEditor (*part.plugin);
                }
            held = {};
            return;
        }
        if (held.item != nullptr && held.item->id == "editRouting")   // where its paths are wired
        {
            if (auto* tabs = findParentComponentOfClass<juce::TabbedComponent>())
                tabs->setCurrentTabIndex (tabs->getTabNames().indexOf ("Routing"));
            held = {};
            return;
        }
        if (held.item != nullptr && held.item->kind == "pads")   // THE PADS: a bank key, a pad (pressed: it plays), STOP ALL
        {
            const auto r = itemArea (*held.item, held.ch);
            auto& pads = engine->getPads();
            for (int b = 0; b < Pads::banks; ++b)
                if (padBankKey (r, b).contains (e.position)) { padBank = b; held = {}; repaint(); return; }
            if (padStopKey (r).contains (e.position)) { pads.stopAll(); held = {}; return; }
            for (int i = 0; i < Pads::perBank; ++i)
                if (padCell (r, i).contains (e.position))
                {
                    pressedPad = padBank * Pads::perBank + i;
                    pads.press (pressedPad);   // (released on mouse up: a HOLD pad plays while held)
                }
            held = {};
            return;
        }
        if (held.item != nullptr && held.item->kind == "tape")   // THE TAPE: seek, the transport, SHUFFLE, ADD, a track
        {
            const auto r = itemArea (*held.item, held.ch);
            auto& tape = engine->getTape();
            const auto bar = tapeArea (r, "bar").expanded (0.0f, 4.0f);
            if (bar.contains (e.position) && tape.length() > 0.0)
                tape.seek (tape.length() * (double) ((e.position.x - bar.getX()) / bar.getWidth()));
            else if (tapeArea (r, "PREV").contains (e.position)) tape.previous();
            else if (tapeArea (r, "REW").contains (e.position))  tape.skip (-10.0);
            else if (tapeArea (r, "STOP").contains (e.position)) tape.stop();
            else if (tapeArea (r, "PLAY").contains (e.position)) { if (! tape.isPlaying()) tape.togglePlay(); }
            else if (tapeArea (r, "PAUSE").contains (e.position)) { if (tape.isPlaying()) tape.togglePlay(); }
            else if (tapeArea (r, "FF").contains (e.position))   tape.skip (10.0);
            else if (tapeArea (r, "NEXT").contains (e.position)) tape.next();
            else if (tapeArea (r, "SHUFFLE").contains (e.position)) tape.setShuffle (! tape.getShuffle());
            else if (tapeArea (r, "ADD").contains (e.position))
            {
                chooser = std::make_unique<juce::FileChooser> ("Add to the tape", juce::File(), "*.wav;*.mp3;*.flac;*.aiff;*.aif;*.ogg");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories
                                      | juce::FileBrowserComponent::canSelectMultipleItems,
                                      [this] (const juce::FileChooser& fc) { for (auto& f : fc.getResults()) engine->getTape().add (f); repaint(); });
            }
            held = {};
            repaint();
            return;
        }
        if (held.item != nullptr && held.item->kind == "playlist")   // a track: it plays
        {
            const auto list = itemArea (*held.item, held.ch);
            auto& tape = engine->getTape();
            const float rowH = juce::jmax (16.0f, 22.0f * zoom);
            const int rows = (int) (list.getHeight() / rowH), first = juce::jlimit (0, juce::jmax (0, tape.size() - rows), tape.current() - rows / 2);
            if (const int i = first + (int) ((e.position.y - list.getY()) / rowH); juce::isPositiveAndBelow (i, tape.size()))
                tape.play (i);
            held = {};
            repaint();
            return;
        }
        if (held.item != nullptr && held.item->id == "device")   // an output's device: pick another
        {
            showDeviceMenu (held.ch);
            held = {};
            return;
        }
        if (held.item != nullptr && held.item->id == "extKey" && ! bind.isOn (held.ch, "extKey", false)
            && bind.strip (held.ch) != nullptr && bind.strip (held.ch)->keyFrom.isEmpty())   // EXT with no key yet: pick one (it then switches on)
        {
            keyFromMenu (held.ch, true).showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (itemArea (*held.item, held.ch).toNearestInt())));
            held = {};
            return;
        }
        if (held.item != nullptr && held.item->id == "addPlugin" && mixer != nullptr && bind.strip (held.ch) != nullptr)
        {
            mixer->showAddInsertMenu ({ MixerComponent::InsertTarget::strip, bind.indexOf (held.ch) }, juce::PopupMenu::Options().withMousePosition());
            held = {};
            return;
        }
        if (held.item != nullptr && held.item->kind == "pluginlist" && mixer != nullptr)   // a plugin on an app's strip: it opens
        {
            const auto r = itemArea (*held.item, held.ch);
            const int i = (int) ((e.position.y - r.getY()) / (26.0f * zoom));
            if (auto* s = bind.strip (held.ch); s != nullptr && juce::isPositiveAndBelow (i, (int) s->inserts.size()))
                mixer->openEditor (*s->inserts[(size_t) i]);
            held = {};
            return;
        }
        if (held.item != nullptr && held.item->id == "sel")   // SEL: this strip (its plugins in the panel)
        {
            select (held.ch);
            held = {};
            return;
        }
        if (held.item != nullptr && (held.item->kind == "order" || held.item->kind == "routes"))
        {
            const auto r = itemArea (*held.item, held.ch);
            if (held.item->kind == "order")
            {
                if (pluginChip (r, held.ch).contains (e.position))   // (its plugins: the panel, on this channel)
                {
                    select (held.ch);
                    if (onShowPlugins)
                        onShowPlugins();
                }
                else
                    for (int i = 0; i < 3; ++i)
                        if (orderChip (r, i).contains (e.position))
                            bind.moveUp (held.ch, i);
            }
            else
            {
                const auto list = bind.routes (held.ch);
                for (int i = 0; i < (int) list.size(); ++i)
                    if (routeBox (r, i, (int) list.size()).contains (e.position))
                    {
                        list[(size_t) i].toggle();
                        if (bind.channels() != channels) { channels = bind.channels(); resizeToFit(); }
                    }
            }
            held = {};
            repaint();
            return;
        }
        if (held.item == nullptr)
            return;
        const auto& kind = held.item->kind;
        if (kind == "key" || kind == "switch" || kind == "chip")
            bind.toggle (held.ch, held.item->id, held.item->lit);
        else
        {
            dragFrom = bind.get (held.ch, held.item->id, kind == "fader" ? 0.75f : 0.5f);
            bind.gesture (held.ch, held.item->id, true);
        }
        repaint();
    }

    void dragBench (const juce::MouseEvent& e)
    {
        if (dragging >= 0)
        {
            dragX = e.position.x;
            dragMoved = dragMoved || e.getDistanceFromDragStart() > 6;
            if (auto* view = findParentComponentOfClass<juce::Viewport>(); view != nullptr && dragMoved)
            {
                const auto at = e.getEventRelativeTo (view).getPosition();
                view->autoScroll (at.x, at.y, 60, 18);   // (near an edge: the desk scrolls on)
            }
            repaint();
            return;
        }
        if (held.item == nullptr || ! draggable (*held.item))
            return;
        const float travel = held.item->kind == "fader" ? held.item->r.getHeight() * zoom : 200.0f;
        bind.set (held.ch, held.item->id, dragFrom - (float) e.getDistanceFromDragStartY() / travel);
        repaint();
    }

    // TOUCH: the strip under the mouse is drawn again as it moves, so what it's over lights (touch()).
    void trackPointer (juce::Point<float> p, bool down)
    {
        pointer = p;
        pointerDown = down;
        int now = -1;
        for (int ch = 0; ch < channels && now < 0; ++ch)
            if (stripArea (ch).toFloat().contains (p))
                now = ch;
        if (now != pointerCh && pointerCh >= 0)
            repaint (stripArea (pointerCh));
        pointerCh = now;
        if (now >= 0)
            repaint (stripArea (now));
    }

    // How a part shows the mouse: `hover` eases in while the mouse is over it, `press` while the
    // button is held down on it (it sinks), so every part that can be pressed says so. A strip
    // switched off takes no presses (`evenOff`: its name bar, which switches it back on, does).
    struct Touch { float hover = 0.0f, press = 0.0f; };
    Touch touch (int ch, const juce::String& id, juce::Rectangle<float> box, bool evenOff = false)
    {
        const bool over = live() && (evenOff || (! locked (ch) && powerShown (ch) > 0.5f)) && box.contains (pointer);
        const bool pushed = over && pointerDown && box.contains (pointerDownAt);
        return { eased (ch, id + "#over", over ? 1.0f : 0.0f), pushed ? 1.0f : 0.0f };
    }

    // An outlined box (a chain block, a route, a path's chip): its legend in its colour at full
    // strength at rest; lit, filled in its colour with a dark legend; over, the face and edge
    // brighten; held down, it sinks and darkens.
    void drawBox (juce::Graphics& g, juce::Rectangle<float> box, juce::Colour colour, float lit, Touch t, const juce::String& text, float fontHeight, float corner)
    {
        box = box.translated (0.0f, t.press * 1.0f * zoom);
        auto fill = juce::Colour (0xff1c2422).interpolatedWith (colour.withAlpha (0.9f), lit);
        fill = fill.interpolatedWith (colour, 0.16f * t.hover * (1.0f - lit)).brighter (0.2f * t.hover * lit);
        if (t.press > 0.0f)
            fill = fill.darker (0.45f);
        g.setColour (fill);
        g.fillRoundedRectangle (box, corner);
        g.setColour (colour.brighter (0.25f * t.hover).withAlpha (juce::jmin (1.0f, 0.75f + 0.25f * juce::jmax (lit, t.hover))));
        g.drawRoundedRectangle (box, corner, 1.0f + 0.7f * t.hover);
        g.setColour (lit > 0.5f ? Console::Colours::litInk() : colour.brighter (0.3f * t.hover));
        g.setFont (LargeFormat::print (fontHeight));
        g.drawFittedText (text, box.reduced (3.0f * zoom, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        trackPointer (e.position, true);
        dragBench (e);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        trackPointer (e.position, false);
        const auto h = hit (e.position);
        const auto now = h.item != nullptr && draggable (*h.item) && ! locked (h.ch) ? h : Hit {};
        if (now.item != hovered.item || now.ch != hovered.ch)
        {
            if (hovered.item != nullptr) repaint (stripArea (hovered.ch));
            hovered = now;
            if (hovered.item != nullptr) repaint (stripArea (hovered.ch));
        }
    }
    // A fader moved by some dB (from off: from -60), up to +12; below -60, off.
    void nudgeFader (int ch, float db)
    {
        auto move = [db] (std::atomic<float>& gain)
        {
            const float now = gain.load() <= 0.0f ? -60.0f : juce::Decibels::gainToDecibels (gain.load());
            const float next = juce::jmin (12.0f, now + db);
            gain = next < -60.0f ? 0.0f : juce::Decibels::decibelsToGain (next);
        };
        if (auto* m = bind.master (ch)) move (m->gain);
        else if (auto* s = bind.strip (ch)) move (s->gain);
        repaint (stripArea (ch));
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hovered.item != nullptr)
            repaint (stripArea (hovered.ch));
        hovered = {};
        trackPointer ({ -1.0e6f, -1.0e6f }, false);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        trackPointer (e.position, false);
        if (dragging >= 0)
        {
            const int ch = dragging;
            const bool moved = dragMoved;
            dragging = -1;
            dragMoved = false;
            if (moved)
                moveInput (ch, insertionGap (ch, dragX));
            else
                select (ch);   // (a click: the strip selected; its menu is a right-click)
            repaint();
            return;
        }
        if (pressedPad >= 0 && engine != nullptr)   // (a pad let go: a HOLD pad stops)
        {
            engine->getPads().release (pressedPad);
            pressedPad = -1;
        }
        if (held.item != nullptr && draggable (*held.item))
            bind.gesture (held.ch, held.item->id, false);
        held = {};
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (const auto h = hit (e.position); h.item != nullptr && draggable (*h.item) && ! locked (h.ch))
        {
            bind.reset (h.ch, h.item->id);
            repaint();
        }
    }

    // The wheel: the strips across (with Shift, up and down).
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        auto* view = findParentComponentOfClass<juce::Viewport>();
        if (view == nullptr)
            return Component::mouseWheelMove (e, w);
        const float d = (std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY) * (w.isReversed ? -1.0f : 1.0f);
        if (e.mods.isShiftDown())
            scrollBy (0.0f, -d * 240.0f);
        else
            scrollBy (-d * 480.0f, 0.0f);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        // THE DESK'S KEYS: left / right the strip selected; M cut, S solo; up / down its fader
        // (0.5 dB, with Shift 3 dB).
        if (live() && (k.isKeyCode (juce::KeyPress::leftKey) || k.isKeyCode (juce::KeyPress::rightKey)))
        {
            const int step = k.isKeyCode (juce::KeyPress::leftKey) ? -1 : 1;
            select (juce::jlimit (0, channels - 1, selected < 0 ? 0 : selected + step));
            if (! followSelect)
                reveal (selected);
            return true;
        }
        if (live() && selected >= 0 && ! locked (selected) && ! k.getModifiers().isCtrlDown() && ! k.getModifiers().isAltDown())
        {
            const auto ch = k.getTextCharacter();
            if (ch == 'm' || ch == 'M') { bind.toggle (selected, "cut", false); repaint (stripArea (selected)); return true; }
            if ((ch == 's' || ch == 'S') && ! bind.isMaster (selected)) { bind.toggle (selected, "solo", false); repaint (stripArea (selected)); return true; }
            if (k.isKeyCode (juce::KeyPress::upKey) || k.isKeyCode (juce::KeyPress::downKey))
            {
                nudgeFader (selected, (k.isKeyCode (juce::KeyPress::upKey) ? 1.0f : -1.0f) * (k.getModifiers().isShiftDown() ? 3.0f : 0.5f));
                return true;
            }
        }
        // The layout's own keys, for working on the strips' layout files: Ctrl+Shift+O outlines every
        // part, Ctrl+Shift+R reads the files again (they also reload by themselves when saved).
        // Debug builds only: in a release, Ctrl+Shift+O belongs to the overlay.
        const auto c = k.getKeyCode();
       #if JUCE_DEBUG
        const bool layoutKey = k.getModifiers().isCtrlDown() && k.getModifiers().isShiftDown();
        if (layoutKey && (c == 'O' || c == 'o')) outlines = ! outlines;
        else if (layoutKey && (c == 'R' || c == 'r')) reload();
        else
       #endif
        if (c >= '1' && c <= '8' && (engine == nullptr)) channels = c - '0';
        else return false;
        resizeToFit();
        repaint();
        return true;
    }

    std::function<void()> onShowPlugins;   // (the PLUGINS panel opened: set by the view)

    // PEAK HOLD: the highest segment a meter reached stays lit 1.5 s, then follows the level down.
    int heldPeak (int meter, int now)
    {
        auto& [segment, at] = peaks[meter];
        const auto time = juce::Time::getMillisecondCounter();
        if (now >= segment || time - at > 1500)
        {
            segment = now;
            at = time;
        }
        return segment;
    }
    std::map<int, std::pair<int, juce::uint32>> peaks;

    // A strip by its key ("input:MIC", "bus:..."), selected and brought into view.
    void selectKey (const juce::String& key)
    {
        for (int ch = 0; ch < channels; ++ch)
            if (stripKey (ch) == key)
                return goToStrip (ch);
    }

private:

    static bool draggable (const Item& item) { return item.kind == "knob" || item.kind == "fader" || (item.kind == "screen" && item.id == "mix"); }

    bool live() const { return engine != nullptr; }

    // POWER: each strip's on/off as shown, easing toward the engine's (so switching fades).
    float powerShown (int ch) const { return juce::isPositiveAndBelow (ch, (int) power.size()) ? power[(size_t) ch] : 1.0f; }
    void stepPower()
    {
        power.resize ((size_t) juce::jmax (0, channels), 1.0f);
        for (int ch = 0; ch < channels; ++ch)
        {
            const float target = bind.isOn (ch, "power", true) ? 1.0f : 0.0f;
            auto& p = power[(size_t) ch];
            if (std::abs (target - p) > 0.004f)
            {
                p += (target - p) * ease (0.2f);
                if (std::abs (target - p) < 0.01f)
                    p = target;
                repaint (stripArea (ch));
            }
        }
    }

    // BLANK SPACERS: empty strips between the real ones, for breathing room. Each sits after a strip
    // (by its name, so it follows the strip about) or at the start; full width or narrow. Kept in
    // console.xml in the app's folder.
    struct Spacer { juce::String after; bool narrow = false; };

    // ONE CHANNEL STRIP: every channel and bus has the light strip (its INPUT carries the mic drive
    // and the input switch too). Outputs and combinators keep their dark panel.
    bool darkPanel (int ch) const
    {
        return live() && (bind.isMaster (ch) || isCombinator (ch));
    }
    // COMPACT: a section shows if the desk is whole or it's kept for COMPACT (the name bar, the
    // fader). Every walk down a strip (drawing, clicks, MIDI MAP, sizes, the sidebar's SECTIONS) asks this.
    bool shows (const Section& s) const { return ! compact || s.compact; }

    const Layout& layoutFor (int ch) const
    {
        if (bind.isMaster (ch)) return masterLayout;
        if (isCombinator (ch)) return combinatorLayout;
        if (auto* s = bind.strip (ch); s != nullptr && ! bind.isBus (ch))
        {
            if (s->source.kind == AudioEngine::Source::pads) return padsLayout;
            if (s->source.kind == AudioEngine::Source::tape) return tapeLayout;
        }
        return isApp (ch) ? appLayout : layout;
    }
    // AN APP OR A TOOL (the pads, the tape): raw audio plumbed in, so a strip of its own (routing, a
    // tool's controls, plugins, fader) with no built-in effects.
    bool isApp (int ch) const
    {
        auto* s = bind.strip (ch);
        return s != nullptr && ! bind.isBus (ch)
               && (s->source.kind == AudioEngine::Source::app || s->source.kind == AudioEngine::Source::pads || s->source.kind == AudioEngine::Source::tape);
    }
    // A COMBINATOR (a bus of parallel paths): no effects of its own; its strip says where its paths'
    // settings are (what feeds each, its plugins).
    bool isCombinator (int ch) const
    {
        auto* s = bind.strip (ch);
        return s != nullptr && bind.isBus (ch) && s->combinator;
    }
    // Its built-in effects switched out the first time it's shown as an app (its plugins kept).
    void keepAppsRaw()
    {
        for (int ch = 0; ch < channels; ++ch)
            if (auto* s = bind.strip (ch); s != nullptr && (isApp (ch) || isCombinator (ch)) && s->channel != nullptr && rawDone.insert (s).second)
                for (auto* id : { "hpfOn", "lpfOn", "nrOn", "gateOn", "compOn", "eqOn", "deessOn", "levelOn", "limitOn", "robotOn", "echoOn", "radioOn", "pitchOn", "busOn" })
                    if (auto* p = s->channel->params.getParameter (id); p != nullptr && p->getValue() > 0.5f)
                    {
                        p->setValueNotifyingHost (0.0f);
                        engine->logEvent ("console: " + s->name + " is raw audio (an app or a tool): its " + juce::String (id).dropLastCharacters (2) + " switched out");
                    }
    }
    std::set<const void*> rawDone;
    struct SpacerColumn { int index = -1; float x = 0, w = 0; };

    juce::String stripKey (int ch) const
    {
        if (auto* m = bind.master (ch))
            return "output:" + m->name;
        if (auto* s = bind.strip (ch))
            return (bind.isBus (ch) ? "bus:" : "input:") + s->name;
        return "ch:" + juce::String (ch);
    }
    float spacerWidth (const Spacer& sp) const { return (layout.width + 2.0f) * zoom * (sp.narrow ? 0.35f : 1.0f); }
    float stripX (int ch) const { return juce::isPositiveAndBelow (ch, (int) xs.size()) ? xs[(size_t) ch] : 20.0f; }
    float drawX (int ch) const
    {
        const auto it = shownX.find (stripKey (ch));
        return it != shownX.end() ? it->second : stripX (ch);
    }
    void stepShownX()
    {
        if (shownX.empty())
            return;
        std::map<juce::String, float> target;
        for (int ch = 0; ch < channels; ++ch)
            target[stripKey (ch)] = stripX (ch);
        for (auto it = shownX.begin(); it != shownX.end();)
        {
            const auto t = target.find (it->first);
            if (t == target.end() || std::abs (t->second - it->second) < 0.5f)
                it = shownX.erase (it);
            else
            {
                it->second += (t->second - it->second) * ease (0.3f);
                ++it;
            }
        }
        repaint();
    }

    // Where everything goes across: spacers at the start, then each strip and the spacers after it;
    // spacers whose strip has gone go at the end.
    void layoutColumns()
    {
        xs.assign ((size_t) juce::jmax (0, channels), 20.0f);
        spacerColumns.clear();
        std::vector<bool> placed (spacers.size(), false);
        float x = 20.0f;
        auto place = [&] (const juce::String& after)
        {
            for (size_t i = 0; i < spacers.size(); ++i)
                if (! placed[i] && spacers[i].after == after)
                {
                    placed[i] = true;
                    spacerColumns.push_back ({ (int) i, x, spacerWidth (spacers[i]) });
                    x += spacerWidth (spacers[i]);
                }
        };
        place ({});
        for (int ch = 0; ch < channels; ++ch)
        {
            xs[(size_t) ch] = x;
            x += (layout.width + 2.0f) * zoom;
            place (stripKey (ch));
        }
        for (size_t i = 0; i < spacers.size(); ++i)
            if (! placed[i])
            {
                spacerColumns.push_back ({ (int) i, x, spacerWidth (spacers[i]) });
                x += spacerWidth (spacers[i]);
            }
        deskWidth = x;
    }

    int spacerAt (juce::Point<float> p) const
    {
        if (p.y < 40.0f)
            return -1;
        for (auto& col : spacerColumns)
            if (p.x >= col.x && p.x < col.x + col.w)
                return col.index;
        return -1;
    }

    void addSpacer (int ch, bool before)
    {
        juce::String after = before ? (ch > 0 ? stripKey (ch - 1) : juce::String()) : stripKey (ch);
        spacers.push_back ({ after, false });
        saveSpacers();
        resizeToFit();
        repaint();
    }

    void showSpacerMenu (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) spacers.size()))
            return;
        juce::PopupMenu menu;
        menu.addSectionHeader ("SPACER");
        menu.addItem ("NARROW", true, spacers[(size_t) index].narrow, [this, index] { spacers[(size_t) index].narrow = ! spacers[(size_t) index].narrow; saveSpacers(); resizeToFit(); repaint(); });
        menu.addItem ("ADD ANOTHER", [this, index] { spacers.insert (spacers.begin() + index + 1, spacers[(size_t) index]); saveSpacers(); resizeToFit(); repaint(); });
        menu.addItem ("REMOVE SPACER", [this, index] { spacers.erase (spacers.begin() + index); saveSpacers(); resizeToFit(); repaint(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }

    static juce::File spacerFile()
    {
        return Product::dataFolder().getChildFile ("console.xml");
    }
    void loadSpacers()
    {
        spacers.clear();
        if (auto xml = juce::XmlDocument::parse (spacerFile()))
        {
            autoSelect = xml->getBoolAttribute ("autoSelect", true);
            followSelect = xml->getBoolAttribute ("followSelect", true);
            compact = xml->getBoolAttribute ("compact", false);   // (a file from before COMPACT: the whole desk)
            // (the meterIn earlier versions kept is ignored: METER is the engine's meter point now,
            // saved with the settings, and the next save leaves it out)
            for (auto* e : xml->getChildWithTagNameIterator ("Spacer"))
                spacers.push_back ({ e->getStringAttribute ("after"), e->getBoolAttribute ("narrow") });
            // (the <Strip> entries earlier versions kept, each strip's Light or Dark type, are ignored:
            // there is one channel strip now, and the next save leaves them out)
        }
    }
    void saveSpacers() const
    {
        juce::XmlElement xml ("Console");
        xml.setAttribute ("autoSelect", autoSelect);
        xml.setAttribute ("followSelect", followSelect);
        xml.setAttribute ("compact", compact);
        for (auto& sp : spacers)
        {
            auto* e = xml.createNewChildElement ("Spacer");
            e->setAttribute ("after", sp.after);
            e->setAttribute ("narrow", sp.narrow);
        }
        xml.writeTo (spacerFile());
    }

    // A strip brought into view and selected (a combinator's FROM chips).
    void goToStrip (int ch)
    {
        select (ch);
        glideTo (stripX (ch) - 40.0f, -1.0f);
    }

    // SEL: the strip you're on (outlined); Faider's selection follows it (its keys and rack).
    void select (int ch)
    {
        selected = ch;
        followed = stripKey (ch);
        if (mixer != nullptr && (bind.master (ch) != nullptr || bind.strip (ch) != nullptr))
            mixer->setSelected (followed);
        if (followSelect)
            reveal (ch);
        repaint();
    }

    // FOLLOW SELECT: a strip chosen elsewhere in Faider (FIND, a hotkey) is selected here too,
    // and brought into view.
    void followFaider()
    {
        if (! followSelect || mixer == nullptr || mixer->getSelected() == followed)
            return;
        followed = mixer->getSelected();
        for (int ch = 0; ch < channels; ++ch)
            if (stripKey (ch) == followed)
            {
                selected = ch;
                reveal (ch);
                repaint();
                return;
            }
    }

    // A strip brought wholly into view, the view gliding the least way.
    void reveal (int ch)
    {
        auto* view = findParentComponentOfClass<juce::Viewport>();
        if (view == nullptr || ch < 0)
            return;
        const float x0 = stripX (ch), x1 = x0 + (layout.width + 2.0f) * zoom;
        const auto area = view->getViewArea().toFloat();
        if (x0 < area.getX())
            glideTo (x0 - 20.0f, -1.0f);
        else if (x1 > area.getRight())
            glideTo (x1 + 20.0f - area.getWidth(), -1.0f);
    }

    void startGliding() { gliding = true; }

    // A FRAME (each refresh of the display): everything moving takes its step, by the time since
    // the last one, so motion is as smooth as the screen and the same speed at any frame rate.
    void onFrame()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        frameDt = (float) juce::jlimit (0.0, 0.1, (now - lastFrame) * 0.001);
        lastFrame = now;
        stepGlide();
        stepEases();
        stepPower();
        stepShownX();
    }
    // An easing step that moved `perTick` of the way each thirtieth of a second, for this frame.
    float ease (float perTick) const { return 1.0f - std::pow (1.0f - perTick, frameDt * 30.0f); }

    void stepGlide()
    {
        auto* view = findParentComponentOfClass<juce::Viewport>();
        if (! gliding || view == nullptr)
            return;
        const float dt = frameDt;
        if (view->getViewPosition() != shownAt.roundToInt())   // (moved some other way meanwhile: from there)
            shownAt = view->getViewPosition().toFloat();
        shownAt += (glide - shownAt) * (1.0f - std::exp (-dt * 16.0f));   // (about a quarter of a second, whatever the frame rate)
        if (shownAt.getDistanceFrom (glide) < 0.5f)
        {
            shownAt = glide;
            gliding = false;
        }
        view->setViewPosition (shownAt.roundToInt());
    }

    // MOTION: what's shown easing to what is (a switch lighting, a block moving to its place).
    float eased (int ch, const juce::String& id, float target)
    {
        auto& e = eases.try_emplace ({ ch, id }, Ease { target, target }).first->second;
        e.target = target;
        return e.shown;
    }
    void stepEases()
    {
        for (auto& [key, e] : eases)
            if (e.shown != e.target)
            {
                e.shown += (e.target - e.shown) * ease (0.35f);
                if (std::abs (e.target - e.shown) < 0.02f)
                    e.shown = e.target;
                repaint (stripArea (key.first));
            }
    }

    // What a part shows now, as a number: when it changes, just that part is drawn again. (0: a
    // part that only changes with the desk itself, drawn again with the whole desk each second.)
    juce::int64 partState (const Item& item, int ch)
    {
        const auto& k = item.kind;
        if (k == "knob" || k == "fader") return juce::roundToInt (bind.get (ch, item.id, 0.5f) * 1000.0f);
        if (k == "key")    return item.id == "sel" ? (ch == selected ? 1 : 0) : (bind.isOn (ch, item.id, false) ? 1 : 0);
        if (k == "name")   return bind.text (ch, "name", {}).hashCode64() ^ (bind.isOn (ch, "power", true) ? 1 : 0);
        if (k == "screen") return item.id == "device" ? 0 : bind.text (ch, item.id, item.text).hashCode64();   // (the device: looked up, so once a second)
        if (k == "lamp")   return bind.levelDb (ch, 0) > -40.0f ? 1 : 0;
        if (k == "vu")     return juce::roundToInt (bind.busReduction (ch) * 4.0f);
        if (k == "leds")   return bind.reductionLit (ch, item.id, item.litCount >= 0 ? item.litCount : (item.id == "compGr" ? 2 : 3));
        if (k == "bars")
        {
            const auto seg = [this, ch] (int side) { return (juce::int64) juce::jlimit (0, 16, (int) ((bind.levelDb (ch, side) + 48.0f) / 3.0f) + 1); };
            return seg (0) | (seg (1) << 5);
        }
        if (k == "pads" && engine != nullptr)   // (the bank shown, and each pad's lamp and progress)
        {
            auto& pads = engine->getPads();
            juce::int64 state = padBank;
            for (int i = 0; i < Pads::perBank; ++i)
            {
                const int index = padBank * Pads::perBank + i;
                state = state * 131 + (pads.pad (index).sound != nullptr ? 64 : 0)
                      + (pads.isPlaying (index) ? 1 + juce::roundToInt (pads.progress (index) * 50.0f) : 0);
            }
            return state;
        }
        if (k == "tape" && engine != nullptr)   // (the track, the transport and the clock, to a quarter second)
        {
            auto& tape = engine->getTape();
            return ((juce::int64) tape.current() << 40) ^ ((juce::int64) tape.size() << 28) ^ ((juce::int64) (tape.isPlaying() ? 1 : 0) << 26)
                 ^ ((juce::int64) (tape.getShuffle() ? 1 : 0) << 25) ^ (juce::int64) (tape.position() * 4.0);
        }
        return 0;
    }

    // A strip's parts that changed since they were drawn: drawn again, only them. The strip switched
    // on or off, selected, or turned light or dark: all of it.
    void repaintChanged (int ch)
    {
        if (shownParts.size() != (size_t) channels)
        {
            shownParts.assign ((size_t) channels, {});
            shownStrips.assign ((size_t) channels, -1);
        }
        const juce::int64 whole = (bind.isOn (ch, "power", true) ? 1 : 0) | (ch == selected ? 2 : 0) | (darkPanel (ch) ? 4 : 0)
                                | ((juce::int64) channelColour (ch).getARGB() << 3)
                                | ((juce::int64) Theme::generation() << 35);   // (a new look: all of it again)
        const bool all = std::exchange (shownStrips[(size_t) ch], whole) != whole;
        if (all)
            repaint (stripArea (ch));
        auto& drawn = shownParts[(size_t) ch];
        const float x0 = stripX (ch);
        float y0 = 40.0f;
        size_t n = 0;
        for (auto& s : layoutFor (ch).sections)
        {
            for (auto& item : s.items)
            {
                if (n == drawn.size())
                    drawn.push_back (-1);
                if (shows (s))   // (folded away: not drawn, so not watched; each part keeps its slot)
                    if (const auto now = partState (item, ch); std::exchange (drawn[n], now) != now && ! all)
                        repaint ((item.r * zoom).translated (x0, y0).expanded (16.0f * zoom).getSmallestIntegerContainer());
                ++n;
            }
            if (shows (s))
                y0 += s.height * zoom;
        }
    }
    juce::Rectangle<int> stripArea (int ch) const
    {
        return juce::Rectangle<float> (stripX (ch), 40.0f, (layout.width + 2.0f) * zoom, (float) getHeight() - 40.0f).getSmallestIntegerContainer();
    }

    // THE TOOLS' parts, inside their area on the strip.
    juce::Rectangle<float> padBankKey (juce::Rectangle<float> r, int b) const
    {
        const float w = r.getWidth() / (float) Pads::banks;
        return juce::Rectangle<float> (r.getX() + w * (float) b, r.getY(), w, 26.0f * zoom).reduced (2.0f * zoom, 0.0f);
    }
    juce::Rectangle<float> padCell (juce::Rectangle<float> r, int i) const
    {
        const auto grid = r.withTrimmedTop (34.0f * zoom).withTrimmedBottom (40.0f * zoom);
        const float side = juce::jmin (grid.getWidth(), grid.getHeight()) / 4.0f;
        const auto square = juce::Rectangle<float> (side * 4.0f, side * 4.0f).withCentre (grid.getCentre()).withY (grid.getY());
        return juce::Rectangle<float> (square.getX() + side * (float) (i % 4), square.getY() + side * (float) (i / 4), side, side).reduced (3.0f * zoom);
    }
    juce::Rectangle<float> padStopKey (juce::Rectangle<float> r) const
    {
        const auto grid = r.withTrimmedTop (34.0f * zoom).withTrimmedBottom (40.0f * zoom);
        const float side = juce::jmin (grid.getWidth(), grid.getHeight()) / 4.0f;
        return juce::Rectangle<float> (r.getX(), grid.getY() + side * 4.0f + 8.0f * zoom, r.getWidth(), 28.0f * zoom);
    }
    // A COMBINATOR's paths, as parts to draw and click: each path's heading, a chip per strip that
    // feeds it (click: that strip), a chip per plugin on it (click: it opens).
    struct PathPart { juce::Rectangle<float> r; juce::String text; int kind = 0; int target = -1; juce::AudioPluginInstance* plugin = nullptr; };   // kind 0 heading, 1 source, 2 plugin, 3 note
    std::vector<PathPart> pathParts (int ch, juce::Rectangle<float> r) const
    {
        std::vector<PathPart> parts;
        auto* bus = bind.strip (ch);
        if (bus == nullptr || engine == nullptr)
            return parts;
        const int busIndex = bind.indexOf (ch);
        const float rowH = juce::jmax (16.0f, 22.0f * zoom), gap = 4.0f * zoom;
        float y = r.getY();
        g_font_size = juce::jmax (9.0f, 10.0f * zoom);
        auto chips = [&] (const std::vector<std::pair<juce::String, std::pair<int, juce::AudioPluginInstance*>>>& items, int kind)
        {
            float x = r.getX();
            for (auto& [text, what] : items)
            {
                const float w = juce::jmin (r.getWidth(), juce::GlyphArrangement::getStringWidth (LargeFormat::print (g_font_size), text) + 14.0f);
                if (x + w > r.getRight() + 0.5f) { x = r.getX(); y += rowH + gap; }
                parts.push_back ({ { x, y, w, rowH }, text, kind, what.first, what.second });
                x += w + gap;
            }
            y += rowH + gap * 2.0f;
        };
        auto& strips = engine->getStrips();
        auto& buses = engine->getBuses();
        for (int p = 0; p < (int) bus->paths.size(); ++p)
        {
            auto& path = *bus->paths[(size_t) p];
            parts.push_back ({ { r.getX(), y, r.getWidth(), rowH }, "PATH " + juce::String (p + 1) + (path.name.isNotEmpty() ? ": " + path.name.toUpperCase() : juce::String()) + (path.mute.load() ? "  (CUT)" : ""), 0 });
            y += rowH + gap;
            std::vector<std::pair<juce::String, std::pair<int, juce::AudioPluginInstance*>>> from, plugins;
            auto feeds = [busIndex, p] (const AudioEngine::Strip& s)
            {
                if (! s.busSends[(size_t) busIndex].load())
                    return false;
                const unsigned bits = s.pathSends[(size_t) busIndex].load();
                return bits == 0 ? p == 0 : (bits & (1u << p)) != 0;
            };
            for (int i = 0; i < (int) strips.size(); ++i)
                if (feeds (*strips[(size_t) i]))
                    from.push_back ({ strips[(size_t) i]->name.toUpperCase(), { i, nullptr } });
            for (int i = 0; i < (int) buses.size(); ++i)
                if (i != busIndex && feeds (*buses[(size_t) i]))
                    from.push_back ({ buses[(size_t) i]->name.toUpperCase(), { bind.busStrip (i), nullptr } });
            for (auto& pl : path.inserts)
                plugins.push_back ({ pl->getName().toUpperCase(), { -1, pl.get() } });
            parts.push_back ({ { r.getX(), y, r.getWidth(), rowH * 0.8f }, "FROM", 3 });
            y += rowH * 0.8f;
            if (from.empty()) { parts.push_back ({ { r.getX(), y, r.getWidth(), rowH }, "NONE", 3 }); y += rowH + gap * 2.0f; }
            else chips (from, 1);
            parts.push_back ({ { r.getX(), y, r.getWidth(), rowH * 0.8f }, "PLUGINS", 3 });
            y += rowH * 0.8f;
            if (plugins.empty()) { parts.push_back ({ { r.getX(), y, r.getWidth(), rowH }, "NONE", 3 }); y += rowH + gap * 2.0f; }
            else chips (plugins, 2);
            y += gap * 2.0f;
        }
        return parts;
    }
    mutable float g_font_size = 10.0f;

public:
    static const juce::StringArray& transportKeys()
    {
        static const juce::StringArray keys { "PREV", "REW", "STOP", "PLAY", "PAUSE", "FF", "NEXT" };
        return keys;
    }

    // A transport symbol, as every deck prints them.
    static void drawTransport (juce::Graphics& g, const juce::String& k, juce::Rectangle<float> r, juce::Colour ink)
    {
        g.setColour (ink);
        juce::Path p;
        const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight(), bar = juce::jmax (1.5f, w * 0.16f);
        auto tri = [&p] (float x0, float y0, float x1, float y1, float x2, float y2) { p.addTriangle (x0, y0, x1, y1, x2, y2); };
        if (k == "PLAY")       tri (x + w * 0.15f, y, x + w * 0.15f, y + h, x + w * 0.95f, y + h * 0.5f);
        else if (k == "STOP")  p.addRectangle (r.withSizeKeepingCentre (juce::jmin (w, h) * 0.85f, juce::jmin (w, h) * 0.85f));
        else if (k == "PAUSE") { p.addRectangle (x + w * 0.2f, y, bar * 1.3f, h); p.addRectangle (x + w * 0.8f - bar * 1.3f, y, bar * 1.3f, h); }
        else if (k == "FF")    { tri (x, y, x, y + h, x + w * 0.5f, y + h * 0.5f); tri (x + w * 0.5f, y, x + w * 0.5f, y + h, x + w, y + h * 0.5f); }
        else if (k == "REW")   { tri (x + w, y, x + w, y + h, x + w * 0.5f, y + h * 0.5f); tri (x + w * 0.5f, y, x + w * 0.5f, y + h, x, y + h * 0.5f); }
        else if (k == "NEXT")  { tri (x, y, x, y + h, x + w - bar, y + h * 0.5f); p.addRectangle (x + w - bar, y, bar, h); }
        else if (k == "PREV")  { p.addRectangle (x, y, bar, h); tri (x + w, y, x + w, y + h, x + bar, y + h * 0.5f); }
        else if (k == "REC")   p.addEllipse (r.withSizeKeepingCentre (juce::jmin (w, h), juce::jmin (w, h)));
        else if (k == "LOOP")  // (a ring, its arrow turning clockwise at the top)
        {
            const float rad = juce::jmin (w, h) * 0.42f, end = juce::MathConstants<float>::twoPi - 0.25f;
            juce::Path ring;
            ring.addCentredArc (r.getCentreX(), r.getCentreY(), rad, rad, 0.0f, 0.5f, end, true);
            juce::PathStrokeType (bar).createStrokedPath (p, ring);
            const float ex = r.getCentreX() + std::sin (end) * rad, ey = r.getCentreY() - std::cos (end) * rad;
            tri (ex, ey - bar * 2.0f, ex, ey + bar * 2.0f, ex + bar * 2.4f, ey);
        }
        g.fillPath (p);
    }
private:

    juce::Rectangle<float> tapeArea (juce::Rectangle<float> r, const juce::String& part) const
    {
        const float k = 30.0f * zoom, gap = 6.0f * zoom;
        const auto screen = r.withHeight (60.0f * zoom);
        const auto bar = juce::Rectangle<float> (r.getX(), screen.getBottom() + gap, r.getWidth(), 8.0f * zoom);
        const auto transport = juce::Rectangle<float> (r.getX(), bar.getBottom() + gap * 1.5f, r.getWidth(), k);
        const auto extras = transport.translated (0.0f, k + gap);
        if (part == "screen") return screen;
        if (part == "bar") return bar;
        if (const int i = transportKeys().indexOf (part); i >= 0)   // (the transport: seven keys a row, as a deck's)
        {
            const float w = transport.getWidth() / (float) transportKeys().size();
            return transport.withWidth (w).translated (w * (float) i, 0.0f).reduced (1.5f * zoom, 0.0f);
        }
        if (part == "SHUFFLE") return extras.withWidth (extras.getWidth() * 0.5f).reduced (2.0f * zoom, 0.0f);
        if (part == "ADD") return extras.withTrimmedLeft (extras.getWidth() * 0.5f).reduced (2.0f * zoom, 0.0f);
        return r.withTop (extras.getBottom() + gap * 2.0f);   // the playlist
    }

    // Where a part is on screen, in strip `ch`.
    // THE VALUE READOUT: the knob or fader under the mouse (or being turned), its value in a dark
    // tag over its name (a fader's beside its cap).
    void paintValue (juce::Graphics& g, const Hit& h)
    {
        if (h.item == nullptr || h.item->kind == "screen")
            return;
        const auto text = bind.valueText (h.ch, h.item->id);
        if (text.isEmpty())
            return;
        const auto r = itemArea (*h.item, h.ch);
        g.setFont (LargeFormat::print (11.0f * juce::jmax (0.8f, zoom)));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 14.0f, ht = 18.0f * juce::jmax (0.8f, zoom);
        juce::Rectangle<float> tag;
        if (h.item->kind == "fader")
        {
            const float y = r.getBottom() - bind.get (h.ch, h.item->id, 0.75f) * r.getHeight();
            tag = juce::Rectangle<float> (w, ht).withCentre ({ r.getX() - w * 0.5f - 4.0f, y });
        }
        else
            tag = juce::Rectangle<float> (w, ht).withCentre ({ r.getCentreX(), r.getBottom() + ht * 0.5f + 3.0f * zoom });
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.fillRoundedRectangle (tag, 3.0f);
        g.setColour (Console::Colours::accent().withAlpha (0.8f));
        g.drawRoundedRectangle (tag, 3.0f, 1.0f);
        g.setColour (juce::Colours::white);
        g.drawText (text, tag, juce::Justification::centred, false);
    }

    juce::Rectangle<float> itemArea (const Item& item, int ch) const
    {
        const float x0 = stripX (ch);
        float y0 = 40.0f;
        for (auto& s : layoutFor (ch).sections)
        {
            if (! shows (s))
                continue;
            for (auto& it : s.items)
                if (&it == &item)
                    return (item.r * zoom).translated (x0, y0);
            y0 += s.height * zoom;
        }
        return {};   // (not on this strip, or folded away)
    }
    juce::Rectangle<float> orderChip (juce::Rectangle<float> r, int i) const
    {
        const float h = r.getHeight() / 3.0f;
        return juce::Rectangle<float> (r.getX(), r.getY() + h * (float) i, r.getWidth(), h).withSizeKeepingCentre (r.getWidth(), juce::jmin (h * 0.55f, 30.0f * zoom));
    }
    // A channel's plugins in its chain: a chip in the gap after the EQ (where they run), its first
    // plugin's name and how many more; with the EQ last, in the arrow's place under it (the chain's
    // end: pluginsAtEnd). None (or no room): empty.
    bool pluginsAtEnd (int ch) const
    {
        auto* s = bind.strip (ch);
        return s != nullptr && ! s->inserts.empty() && bind.order (ch).back() == Binding::eq;
    }
    juce::Rectangle<float> pluginChip (juce::Rectangle<float> r, int ch) const
    {
        auto* s = bind.strip (ch);
        if (s == nullptr || s->inserts.empty())
            return {};
        const auto blocks = bind.order (ch);
        const int at = (int) (std::find (blocks.begin(), blocks.end(), (int) Binding::eq) - blocks.begin());
        const float top = orderChip (r, at).getBottom(), bottom = at < 2 ? orderChip (r, at + 1).getY() : r.getBottom() + 22.0f * zoom;   // (the arrow's height under the chain)
        const float h = juce::jmin (bottom - top - 2.0f * zoom, 17.0f * zoom);
        if (h < 9.0f)
            return {};
        return { r.getX() - 2.0f * zoom, (top + bottom - h) * 0.5f, r.getWidth() + 4.0f * zoom, h };
    }
    // The routing boxes: one column while they fit at a readable height, else two.
    juce::Rectangle<float> routeBox (juce::Rectangle<float> r, int i, int count) const
    {
        const float minH = 15.0f * zoom;
        const int across = (float) count * (minH + 3.0f * zoom) > r.getHeight() ? 2 : 1;
        const int rows = (count + across - 1) / across;
        const float pitch = juce::jmin (26.0f * zoom, r.getHeight() / (float) juce::jmax (1, rows));
        if ((float) (i / across) * pitch + pitch > r.getHeight() + 0.5f)
            return {};
        const float w = r.getWidth() / (float) across;
        return juce::Rectangle<float> (r.getX() + w * (float) (i % across), r.getY() + pitch * (float) (i / across), w, pitch).reduced (1.0f * zoom, 1.5f * zoom);
    }

public:
    // The strip selected (SEL), for the plugin panel: its strip, and where its plugins are.
    AudioEngine::Strip* selectedStrip() const { return bind.strip (selected); }
    MixerComponent::InsertTarget selectedTarget() const
    {
        return { bind.isBus (selected) ? MixerComponent::InsertTarget::bus : MixerComponent::InsertTarget::strip, bind.indexOf (selected) };
    }
    AudioEngine* getEngine() const { return engine; }
    MixerComponent* getMixer() const { return mixer; }

    // THE ROUTING PAGE's view of the desk: where each strip is across, and the strips themselves.
    int stripCount() const { return channels; }
    float stripLeft (int ch) const { return drawX (ch); }
    // A channel's own colour (Faider's; transparent: none).
    juce::Colour channelColour (int ch) const
    {
        auto* s = bind.strip (ch);
        return s != nullptr && ! bind.isMaster (ch) ? s->colour : juce::Colours::transparentBlack;
    }

    // MOVING A STRIP (its name bar dragged here, or its block on the Routing page): within its own
    // group, the inputs, the buses or the outputs, to a gap between them; everything glides over.
    // Inputs move in the engine; buses and outputs only on the desk (DeskOrder.h).
    bool canMove (int ch) const
    {
        return live() && (bind.strip (ch) != nullptr || bind.master (ch) != nullptr);   // (a lock from Faider's desk doesn't hold here: a drag is meant)
    }
    std::pair<int, int> groupOf (int ch) const   // [first, past the last) of its group's strips
    {
        const int inputs = (int) engine->getStrips().size(), buses = (int) engine->getBuses().size();
        if (bind.isMaster (ch)) return { inputs + buses, channels };
        if (bind.isBus (ch))    return { inputs, inputs + buses };
        return { 0, inputs };
    }
    float gapX (int ch, int gap) const   // (`ch`: the strip being moved, whose group it is)
    {
        const auto [lo, hi] = groupOf (ch);
        return hi <= lo ? 20.0f : gap < hi ? stripX (juce::jmax (lo, gap)) : stripX (hi - 1) + (layout.width + 2.0f) * zoom;
    }
    int insertionGap (int ch, float x) const
    {
        const auto [lo, hi] = groupOf (ch);
        int best = lo;
        for (int gap = lo + 1; gap <= hi; ++gap)
            if (std::abs (x - gapX (ch, gap)) < std::abs (x - gapX (ch, best)))
                best = gap;
        return best;
    }
    void moveInput (int from, int gap)
    {
        const int to = gap > from ? gap - 1 : gap;
        if (! canMove (from) || to == from)
            return;
        if (bind.isBus (from) || bind.isMaster (from))   // (a bus or an output: its place on the desk)
        {
            std::map<juce::String, float> before;
            for (int ch = 0; ch < channels; ++ch)
                before[stripKey (ch)] = drawX (ch);
            const auto chosen = selected >= 0 ? stripKey (selected) : juce::String();
            const int lo = groupOf (from).first;
            if (bind.isBus (from)) bind.deskOrder.moveBus (*engine, from - lo, to - lo);
            else                   bind.deskOrder.moveOutput (*engine, from - lo, to - lo);
            bind.syncOrder();
            layoutColumns();
            for (auto& [key, x] : before)
                shownX[key] = x;
            for (int ch = 0; ch < channels; ++ch)
                if (stripKey (ch) == chosen)
                    selected = ch;
            eases.clear();
            repaint();
            return;
        }
        std::map<juce::String, float> before;
        for (int ch = 0; ch < channels; ++ch)
            before[stripKey (ch)] = drawX (ch);
        const auto chosen = selected >= 0 ? stripKey (selected) : juce::String();
        // (Faider's locks pin strips to their slots: let go for the move, each kept with its strip)
        std::vector<std::pair<AudioEngine::Strip*, bool>> locks;
        for (auto& st : engine->getStrips())
            locks.push_back ({ st.get(), std::exchange (st->locked, false) });
        const bool moved = engine->moveStrip (from, to);
        for (auto& [st, was] : locks)
            st->locked = was;
        if (! moved)
            return;
        layoutColumns();
        for (auto& [key, x] : before)
            shownX[key] = x;
        for (int ch = 0; ch < channels; ++ch)
            if (stripKey (ch) == chosen)
                selected = ch;
        eases.clear();   // (what's shown easing was each strip's by its place)
        repaint();
    }
    float stripWidth() const { return layout.width * zoom; }
    float deskRight() const { return deskWidth; }
    Binding& binding() { return bind; }

    // THE SIDEBAR's SELECT cards (kept in console.xml), and its SECTIONS. (Where a strip's meter reads
    // is SETTINGS > AUDIO > CHANNEL METER POINT: AudioEngine::meterPoint.)
    bool autoSelect = true, followSelect = true;
    void setAutoSelect (bool on) { autoSelect = on; saveSpacers(); }
    void setFollowSelect (bool on) { followSelect = on; saveSpacers(); followFaider(); }
    int sectionCount() const { return (int) layout.sections.size(); }
    juce::String sectionName (int i) const { return layout.sections[(size_t) i].name.toUpperCase(); }
    bool sectionShown (int i) const { return shows (layout.sections[(size_t) i]); }
    juce::Range<float> sectionSpan (int i) const   // (folded away: empty, where it would be)
    {
        float y = 40.0f;
        for (int k = 0; k < i; ++k)
            if (shows (layout.sections[(size_t) k]))
                y += layout.sections[(size_t) k].height * zoom;
        return { y, y + (sectionShown (i) ? layout.sections[(size_t) i].height * zoom : 0.0f) };
    }

    // COMPACT (the sidebar's card, FIND, kept in console.xml): every strip folds to its name bar and
    // fader, with its level meter beside the fader; the desk shrinks to them and the view goes to
    // the top. Off again: the whole strips.
    bool isCompact() const { return compact; }
    void setCompact (bool on)
    {
        if (on == compact)
            return;
        compact = on;
        saveSpacers();
        held = {};
        hovered = {};
        shownParts.clear();   // (every part's place moved: all drawn again)
        shownStrips.clear();
        resizeToFit();
        gliding = false;
        if (auto* view = findParentComponentOfClass<juce::Viewport>())
            view->setViewPosition (view->getViewPositionX(), 0);
        repaint();
    }

    // The view gliding to a place (negative: that way it stays).
    void glideTo (float x, float y)
    {
        auto* view = findParentComponentOfClass<juce::Viewport>();
        if (view == nullptr || view->getViewedComponent() == nullptr)
            return;
        const float maxX = (float) juce::jmax (0, view->getViewedComponent()->getWidth() - view->getViewWidth());
        const float maxY = (float) juce::jmax (0, view->getViewedComponent()->getHeight() - view->getViewHeight());
        if (! gliding)
            shownAt = view->getViewPosition().toFloat();
        glide = { juce::jlimit (0.0f, maxX, x < 0.0f ? (gliding ? glide.x : (float) view->getViewPositionX()) : x),
                  juce::jlimit (0.0f, maxY, y < 0.0f ? (gliding ? glide.y : (float) view->getViewPositionY()) : y) };
        startGliding();
    }

    // SMOOTH SCROLLING: the wheel (and the Routing page's) moves where the view is heading, and the
    // view eases there frame by frame, in step with the display; more turns while it moves add on.
    bool scrolling() const { return gliding; }
    void stepScroll() { stepGlide(); }   // (the Routing page drives the glide while it shows: the desk isn't on screen then)
    void scrollBy (float dx, float dy)
    {
        auto* view = findParentComponentOfClass<juce::Viewport>();
        if (view == nullptr)
            return;
        const auto from = gliding ? glide : view->getViewPosition().toFloat();
        glideTo (juce::jmax (0.0f, from.x + dx), juce::jmax (0.0f, from.y + dy));
    }

    // The sidebar's DESK cards: + CHANNEL (Faider's input menu, by the card) and + BUS.
    void addChannel (juce::Rectangle<int> near)
    {
        if (mixer != nullptr && live())
            mixer->showAddInputMenu (juce::PopupMenu::Options().withTargetScreenArea (near));
    }
    // + APP: an app playing sound now, as a channel of its own (raw: no effects, plugins if wanted).
    void addApp (juce::Rectangle<int> near)
    {
        if (mixer == nullptr || ! live())
            return;
        mixer->showAddAppMenu (juce::PopupMenu::Options().withTargetScreenArea (near), [desk = juce::Component::SafePointer<Bench> (this)]
        {
            if (desk == nullptr)
                return;
            desk->channels = desk->bind.channels();   // (the new channel is the last input: shown, and selected)
            desk->resizeToFit();
            desk->select ((int) desk->engine->getStrips().size() - 1);
        });
    }

    // A combinator path renamed.
    void renamePath (int busIndex, int p)
    {
        auto& buses = engine->getBuses();
        if (! juce::isPositiveAndBelow (busIndex, (int) buses.size()) || ! juce::isPositiveAndBelow (p, (int) buses[(size_t) busIndex]->paths.size()))
            return;
        auto w = std::make_unique<juce::AlertWindow> ("RENAME PATH", juce::String(), juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", buses[(size_t) busIndex]->paths[(size_t) p]->name);
        w->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        showRenameBox (std::move (w), [busIndex, p] (Bench& bench, const juce::String& typed)
        {
            auto& all = bench.engine->getBuses();
            if (juce::isPositiveAndBelow (busIndex, (int) all.size()) && juce::isPositiveAndBelow (p, (int) all[(size_t) busIndex]->paths.size()))
                all[(size_t) busIndex]->paths[(size_t) p]->name = typed.trim().toUpperCase();
        });
    }

    // A box with a "name" editor, shown modal: JUCE's modal manager owns it from here and deletes it
    // when it's dismissed. RENAME (1) hands what was typed to `renamed`, unless the desk went first.
    void showRenameBox (std::unique_ptr<juce::AlertWindow> box, std::function<void (Bench&, const juce::String& typed)> renamed)
    {
        auto* opened = box.release();   // (the modal manager's now: deleteWhenDismissed)
        opened->enterModalState (true, juce::ModalCallbackFunction::create (
            [desk = juce::Component::SafePointer<Bench> (this), window = juce::Component::SafePointer<juce::AlertWindow> (opened), renamed = std::move (renamed)] (int result)
        {
            if (desk == nullptr)
                return;
            if (result == 1 && window != nullptr)
                renamed (*desk, window->getTextEditorContents ("name"));
            desk->repaint();
        }), true);
    }

    // + BUS: a bus, or a combinator (two paths to start).
    void addBusMenu (juce::Rectangle<int> near)
    {
        if (! live())
            return;
        juce::PopupMenu menu;
        menu.addItem ("BUS", [this] { addBus(); });
        menu.addItem ("COMBINATOR", [this]
        {
            engine->addCombinator ("COMBINATOR " + juce::String ((int) engine->getBuses().size() + 1), 2);
            engine->refreshDucking();
            channels = bind.channels();
            resizeToFit();
            repaint();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (near));
    }

    void addBus()
    {
        if (! live())
            return;
        engine->addBus ("BUS " + juce::String ((int) engine->getBuses().size() + 1));
        engine->refreshDucking();
        channels = bind.channels();
        resizeToFit();
        repaint();
    }

private:

    int stripAt (juce::Point<float> p) const
    {
        if (p.y < 40.0f)
            return -1;
        for (int ch = 0; ch < channels; ++ch)
            if (p.x >= stripX (ch) && p.x < stripX (ch) + (layout.width + 2.0f) * zoom)
                return ch;
        return -1;
    }

    // A strip's menu: what it takes in (a channel), where it goes, mono or stereo, its name, removing it.
    // AN OUTPUT's menu: the device it plays on, its headphone correction, its alerts, renaming it, removing it.
    void showDeviceMenu (int ch)
    {
        auto* m = bind.master (ch);
        if (m == nullptr)
            return;
        const int index = bind.indexOf (ch);
        const auto places = m->places();
        juce::PopupMenu devices;
        for (auto& out : engine->outputChoices())
        {
            const bool on = ! places.empty() && places[0].candidate == out.candidate && places[0].firstChannel / 2 == out.firstChannel / 2;
            devices.addItem (out.label, true, on, [this, index, out]
            {
                auto& masters = engine->getMasters();
                if (juce::isPositiveAndBelow (index, (int) masters.size()))
                    engine->setMasterOutput (index, { out.candidate }, out.firstChannel, masters[(size_t) index]->stereo);
                repaint();
            });
        }
        devices.addSeparator();
        devices.addItem ("NO DEVICE", true, places.empty() || places[0].candidate.isEmpty(), [this, index]
        {
            auto& masters = engine->getMasters();
            if (juce::isPositiveAndBelow (index, (int) masters.size()))
                engine->setMasterOutput (index, {}, 0, masters[(size_t) index]->stereo);
            repaint();
        });
        devices.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }

    // A strip switched off: its menu offers only switching it back on, its place and its name.
    void showLockedMenu (int ch)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (bind.text (ch, "name", {}).toUpperCase() + " (OFF)");
        menu.addItem ("TURN ON", [this, ch] { bind.toggle (ch, "power", true); repaint(); });
        menu.addSeparator();
        menu.addItem ("ADD SPACER BEFORE", [this, ch] { addSpacer (ch, true); });
        menu.addItem ("ADD SPACER AFTER", [this, ch] { addSpacer (ch, false); });
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }

    // OFF: a strip switched off takes no changes but being switched back on.
    bool locked (int ch) const { return live() && ! bind.isOn (ch, "power", true); }

public:
    // A channel, bus or output renamed, from either view: its spacers stay beside it.
    void followRename (const juce::String& kind, const juce::String& oldName, const juce::String& newName)
    {
        const auto from = kind + ":" + oldName, to = kind + ":" + newName;
        for (auto& sp : spacers)
            if (sp.after == from)
                sp.after = to;
        saveSpacers();
        resizeToFit();
        repaint();
    }
private:

    void showMasterMenu (int ch)
    {
        auto* m = bind.master (ch);
        if (m == nullptr)
            return;
        const int index = bind.indexOf (ch);
        juce::PopupMenu menu;
        menu.addSectionHeader ("OUTPUT: " + m->name);
        menu.addItem ("DEVICE...", [this, ch] { showDeviceMenu (ch); });
        MixerComponent::addHeadphoneMenu (menu, *engine, index);   // (correction for the headphones it plays on)
        menu.addSeparator();
        // ALERTS: the tray icon and the event log say when it goes silent or clips (MainComponent::checkAlerts).
        auto alert = [this, index] (std::function<void (AudioEngine::Master&)> change)
        {
            auto& masters = engine->getMasters();
            if (juce::isPositiveAndBelow (index, (int) masters.size()))
                change (*masters[(size_t) index]);
        };
        juce::PopupMenu silent;   // (how long it's quiet first; a session's other time shows as its own)
        auto silentLabel = [] (int s) { return s % 60 == 0 ? juce::String (s / 60) + " MIN" : juce::String (s) + " S"; };
        std::vector<int> times { 0, 10, 30, 60, 300 };
        if (m->silenceAlert > 0 && std::find (times.begin(), times.end(), m->silenceAlert) == times.end())
            times.insert (std::upper_bound (times.begin(), times.end(), m->silenceAlert), m->silenceAlert);
        for (int t : times)
            silent.addItem (t == 0 ? juce::String ("OFF") : silentLabel (t), true, m->silenceAlert == t || (t == 0 && m->silenceAlert < 0),
                            [alert, t] { alert ([t] (AudioEngine::Master& mm) { mm.silenceAlert = t; }); });
        menu.addSubMenu ("ALERT IF SILENT", silent, true, nullptr, m->silenceAlert > 0);
        menu.addItem ("ALERT IF CLIPPING", true, m->clipAlert, [alert, on = m->clipAlert] { alert ([on] (AudioEngine::Master& mm) { mm.clipAlert = ! on; }); });   // (with LIMIT off)
        MixerComponent::addMiniMixerItem (menu, *engine, stripKey (ch));
        // (more outputs made from this desk: a feed per guest without themselves, and stems)
        menu.addItem ("MIX-MINUS AND STEMS...", [this]
        {
            MixMinusBuilder::showBuilder (*engine, [desk = juce::Component::SafePointer<Bench> (this)]
            {
                if (desk == nullptr)
                    return;
                desk->channels = desk->bind.channels();
                desk->resizeToFit();
                desk->repaint();
            }, getTopLevelComponent());
        });
        menu.addSeparator();
        menu.addItem ("ADD SPACER BEFORE", [this, ch] { addSpacer (ch, true); });
        menu.addItem ("ADD SPACER AFTER", [this, ch] { addSpacer (ch, false); });
        menu.addSeparator();
        menu.addItem ("RENAME...", [this, index]
        {
            auto& masters = engine->getMasters();
            if (! juce::isPositiveAndBelow (index, (int) masters.size()))
                return;
            auto w = std::make_unique<juce::AlertWindow> ("RENAME", juce::String(), juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", masters[(size_t) index]->name);
            w->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            showRenameBox (std::move (w), [index] (Bench& bench, const juce::String& typed)
            {
                auto& list = bench.engine->getMasters();
                const auto wanted = typed.trim();
                if (wanted.isNotEmpty() && juce::isPositiveAndBelow (index, (int) list.size()))
                {
                    auto& mm = *list[(size_t) index];
                    const auto old = mm.name;
                    mm.name = bench.engine->nameFor ("output", wanted, &mm);
                    bench.engine->renamed ("output", old, mm.name);   // (its spacers follow: followRename)
                }
            });
        });
        menu.addItem ("REMOVE", [this, index, ch]
        {
            for (auto& sp : spacers)
                if (sp.after == stripKey (ch))
                    sp.after = ch > 0 ? stripKey (ch - 1) : juce::String();
            saveSpacers();
            engine->removeMaster (index);
            engine->refreshDucking();
            channels = bind.channels();
            resizeToFit();
            repaint();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }

    // KEY FROM: the input EXT's dynamics hear, or NONE. `andOn`: picking one also switches EXT on.
    juce::PopupMenu keyFromMenu (int ch, bool andOn)
    {
        juce::PopupMenu menu;
        auto* s = bind.strip (ch);
        if (s == nullptr)
            return menu;
        auto pick = [this, bus = bind.isBus (ch), index = bind.indexOf (ch), andOn] (const juce::String& name)
        {
            auto& list = bus ? engine->getBuses() : engine->getStrips();
            if (! juce::isPositiveAndBelow (index, (int) list.size()))
                return;
            auto& st = *list[(size_t) index];
            st.keyFrom = name;
            engine->refreshDucking();
            if (andOn && name.isNotEmpty() && st.channel != nullptr)
                if (auto* p = st.channel->params.getParameter ("keyExternal"))
                    p->setValueNotifyingHost (1.0f);
            repaint();
        };
        menu.addItem ("NONE", true, s->keyFrom.isEmpty(), [pick] { pick ({}); });
        menu.addSeparator();
        for (auto& other : engine->getStrips())
            if (other.get() != s)
                menu.addItem (other->name, true, other->name == s->keyFrom, [pick, name = other->name] { pick (name); });
        return menu;
    }

    void showMenu (int ch)
    {
        if (locked (ch))
            return showLockedMenu (ch);
        if (bind.isMaster (ch))
            return showMasterMenu (ch);
        auto* s = bind.strip (ch);
        if (s == nullptr)
            return;
        auto& e = (*engine);
        const bool bus = bind.isBus (ch);
        const int index = bind.indexOf (ch);
        auto again = [this, bus, index] () -> AudioEngine::Strip*   // (looked up again when picked: it may have gone)
        {
            auto& list = bus ? engine->getBuses() : engine->getStrips();
            return juce::isPositiveAndBelow (index, (int) list.size()) ? list[(size_t) index].get() : nullptr;
        };
        juce::PopupMenu menu, input, route;
        menu.addSectionHeader ((bus ? "BUS: " : "") + s->name);

        if (! bus)
        {
            const auto& now = s->source;
            auto source = [this, index] (AudioEngine::Source next)
            {
                auto& e = (*engine);
                if (index < (int) e.getStrips().size())
                {
                    e.setStripSource (index, next);
                    e.refreshDevices();
                    e.refreshDucking();
                }
                repaint();
            };
            const auto ins = e.getInterfaceInputNames();
            juce::PopupMenu interfaceMenu;
            for (int i = 0; i < ins.size(); ++i)
            {
                const bool on = now.kind == AudioEngine::Source::interfaceInputs && now.firstChannel == i;
                interfaceMenu.addItem ("IN " + juce::String (i + 1) + (now.stereo && i + 1 < ins.size() ? "-" + juce::String (i + 2) : juce::String()) + "  " + ins[i],
                                       true, on, [source, i, stereo = now.stereo] { source ({ AudioEngine::Source::interfaceInputs, stereo ? i - i % 2 : i, {}, stereo }); });
            }
            input.addSubMenu ("INTERFACE", interfaceMenu, ins.size() > 0);
            juce::PopupMenu windowsMenu;
            windowsMenu.addItem ("WINDOWS DEFAULT", true, now.kind == AudioEngine::Source::device && now.deviceName == AudioEngine::windowsDefault,
                                 [source, stereo = now.stereo] { source ({ AudioEngine::Source::device, 0, AudioEngine::windowsDefault, stereo }); });
            for (auto& ep : e.getEndpoints())
                if (ep.api == "WASAPI" && ! ep.isOutput && ep.isActive() && ! e.isDeviceDisabled (ep.name))
                    windowsMenu.addItem (ep.name, true, now.kind == AudioEngine::Source::device && now.deviceName == ep.name,
                                         [source, name = ep.name, stereo = now.stereo] { source ({ AudioEngine::Source::device, 0, name, stereo }); });
            input.addSubMenu ("WINDOWS DEVICES", windowsMenu);
            juce::PopupMenu hearMenu;   // (what an output plays, back in: loopback)
            for (int b = 0; b < (int) e.getMasters().size(); ++b)
                hearMenu.addItem (e.getMasters()[(size_t) b]->name, true, now.kind == AudioEngine::Source::loopback && now.masterIndex == b,
                                  [source, b] { AudioEngine::Source next { AudioEngine::Source::loopback, 0, {}, true }; next.masterIndex = b; source (next); });
            input.addSubMenu ("WHAT AN OUTPUT PLAYS", hearMenu, ! e.getMasters().empty());
            input.addItem ("TEST TONE", true, now.kind == AudioEngine::Source::tone, [source] { source ({ AudioEngine::Source::tone, 0, {}, true }); });
            input.addItem ("NOTHING", true, now.kind == AudioEngine::Source::none, [source] { source ({ AudioEngine::Source::none, 0, {}, true }); });
            menu.addSubMenu ("INPUT", input);
        }

        // Where it goes (as the header's boxes).
        for (auto& r : bind.routes (ch))
            route.addItem (r.label, true, r.on, [this, toggle = r.toggle] { toggle(); repaint(); });
        menu.addSubMenu ("ROUTE TO", route);

        // Its plugins: each opens; ADD puts one on; REMOVE takes one off.
        if (mixer != nullptr)
        {
            const MixerComponent::InsertTarget target { bus ? MixerComponent::InsertTarget::bus : MixerComponent::InsertTarget::strip, index };
            juce::PopupMenu plugins, removes;
            for (int i = 0; i < (int) s->inserts.size(); ++i)
            {
                auto* plugin = s->inserts[(size_t) i].get();
                plugins.addItem (plugin->getName(), [this, plugin] { mixer->openEditor (*plugin); });
                removes.addItem (plugin->getName(), [this, target, i] { mixer->removeInsert (target, i); repaint(); });
            }
            if (! s->inserts.empty())
            {
                plugins.addSeparator();
                plugins.addSubMenu ("REMOVE", removes);
            }
            plugins.addItem ("ADD PLUGIN...", [this, target] { mixer->showAddInsertMenu (target, juce::PopupMenu::Options().withMousePosition()); });
            menu.addSubMenu ("PLUGINS", plugins);
        }
        if (! bus)
            menu.addItem (s->source.stereo ? "MAKE MONO" : "MAKE STEREO", [this, index]
            {
                auto& en = (*engine);
                if (index >= (int) en.getStrips().size())
                    return;
                auto next = en.getStrips()[(size_t) index]->source;
                next.stereo = ! next.stereo;
                if (next.stereo && next.kind == AudioEngine::Source::interfaceInputs)
                    next.firstChannel -= next.firstChannel % 2;
                en.setStripSource (index, next);
                en.refreshDevices();
            });
        if (s->channel != nullptr && ! isApp (ch) && ! isCombinator (ch))   // NOISE: the strip's NOISE key, its kind and how deep
        {
            auto& params = s->channel->params;
            auto set = [again] (const char* id, float v)
            {
                if (auto* st = again(); st != nullptr && st->channel != nullptr)
                    if (auto* p = st->channel->params.getParameter (id))
                        p->setValueNotifyingHost (p->convertTo0to1 (v));
            };
            const bool neural = params.getRawParameterValue ("nrType")->load() < 0.5f;
            const float depth = params.getRawParameterValue ("nrDepth")->load();
            juce::PopupMenu noise;
            const bool on = params.getRawParameterValue ("nrOn")->load() > 0.5f;
            noise.addItem ("ON", true, on, [this, set, on] { set ("nrOn", on ? 0.0f : 1.0f); repaint(); });
            noise.addSeparator();
            noise.addItem ("NEURAL", true, neural, [set] { set ("nrType", 0.0f); });   // (at 48 kHz; at other rates it runs SPECTRAL)
            noise.addItem ("SPECTRAL", true, ! neural, [set] { set ("nrType", 1.0f); });
            noise.addSectionHeader ("DEPTH");
            for (float db : { -6.0f, -12.0f, -18.0f, -24.0f, -30.0f })
                noise.addItem (juce::String (juce::roundToInt (db)) + " dB", true, std::abs (depth - db) < 0.5f, [set, db] { set ("nrDepth", db); });
            menu.addSubMenu ("NOISE", noise);
        }
        if (s->channel != nullptr && ! isApp (ch) && ! isCombinator (ch))   // KEY FROM: what EXT's dynamics hear
            menu.addSubMenu ("KEY FROM", keyFromMenu (ch, false), true, nullptr, s->keyFrom.isNotEmpty());
        {   // DUCK UNDER: dips while another input is heard (an app too: music under a mic)
            auto duck = [this, again] (std::function<void (AudioEngine::Strip&)> change)
            {
                if (auto* st = again())
                {
                    change (*st);
                    engine->refreshDucking();
                }
                repaint();
            };
            juce::PopupMenu under;
            under.addItem ("NONE", true, s->duckBy.isEmpty(), [duck] { duck ([] (AudioEngine::Strip& st) { st.duckBy = {}; }); });
            under.addSeparator();
            for (auto& other : e.getStrips())
                if (other.get() != s)
                    under.addItem (other->name, true, other->name == s->duckBy, [duck, name = other->name] { duck ([name] (AudioEngine::Strip& st) { st.duckBy = name; }); });
            under.addSectionHeader ("DEPTH");
            for (float db : { -6.0f, -12.0f, -18.0f, -24.0f })
                under.addItem (juce::String (juce::roundToInt (db)) + " dB", true, std::abs (s->duckDepth.load() - db) < 0.5f,
                               [duck, db] { duck ([db] (AudioEngine::Strip& st) { st.duckDepth = db; }); });
            menu.addSubMenu ("DUCK UNDER", under, true, nullptr, s->duckBy.isNotEmpty());
        }
        menu.addSeparator();
        MixerComponent::addColourMenu (menu, s->colour, [this, again] (juce::Colour c) { if (auto* st = again()) st->colour = c; repaint(); });   // (Faider's palette: it follows the channel everywhere)
        MixerComponent::addMiniMixerItem (menu, e, stripKey (ch));
        if (bus && s->combinator && mixer != nullptr)   // A COMBINATOR: its paths, each with its plugins
        {
            juce::PopupMenu paths;
            for (int p = 0; p < (int) s->paths.size(); ++p)
            {
                auto& path = *s->paths[(size_t) p];
                const MixerComponent::InsertTarget target { MixerComponent::InsertTarget::bus, index, p };
                juce::PopupMenu one, removes;
                for (int i = 0; i < (int) path.inserts.size(); ++i)
                {
                    auto* plugin = path.inserts[(size_t) i].get();
                    one.addItem (plugin->getName(), [this, plugin] { mixer->openEditor (*plugin); });
                    removes.addItem (plugin->getName(), [this, target, i] { mixer->removeInsert (target, i); repaint(); });
                }
                if (! path.inserts.empty())
                {
                    one.addSubMenu ("REMOVE PLUGIN", removes);
                    one.addSeparator();
                }
                one.addItem ("ADD PLUGIN...", [this, target] { mixer->showAddInsertMenu (target, juce::PopupMenu::Options().withMousePosition()); });
                one.addItem ("RENAME...", [this, index, p] { renamePath (index, p); });
                one.addItem ("REMOVE PATH", (int) s->paths.size() > 1, false, [this, index, p] { engine->removePath (index, p); repaint(); });
                paths.addSubMenu ("PATH " + juce::String (p + 1) + (path.name.isNotEmpty() ? ": " + path.name.toUpperCase() : juce::String()), one);
            }
            paths.addSeparator();
            paths.addItem ("ADD PATH", [this, index, n = (int) s->paths.size()] { engine->addPath (index, "PATH " + juce::String (n + 1)); repaint(); });
            menu.addSubMenu ("PATHS", paths);
        }
        menu.addItem ("ADD SPACER BEFORE", [this, ch] { addSpacer (ch, true); });
        menu.addItem ("ADD SPACER AFTER", [this, ch] { addSpacer (ch, false); });
        menu.addSeparator();
        menu.addItem ("RENAME...", [this, again, bus]
        {
            auto w = std::make_unique<juce::AlertWindow> ("RENAME", juce::String(), juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", again() != nullptr ? again()->name : juce::String());
            w->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            showRenameBox (std::move (w), [again, bus] (Bench& bench, const juce::String& typed)
            {
                auto* st = again();
                const auto wanted = typed.trim();
                if (st != nullptr && wanted.isNotEmpty())
                {
                    const auto old = st->name;
                    st->name = bench.engine->nameFor (bus ? "bus" : "input", wanted, st);
                    bench.engine->renamed (bus ? "bus" : "input", old, st->name);   // (spacers and strip type follow: followRename)
                }
            });
        });
        menu.addItem ("REMOVE", [this, bus, index, ch]
        {
            auto& en = (*engine);
            if (mixer != nullptr)
                mixer->closeEditorsFor ({ bus ? MixerComponent::InsertTarget::bus : MixerComponent::InsertTarget::strip, index });
            if (bus) en.removeBus (index);
            else   { en.forgetAppRule (index); en.removeStrip (index); }
            for (auto& sp : spacers)   // (its spacers stay where it was: after the strip before it)
                if (sp.after == stripKey (ch))
                    sp.after = ch > 0 ? stripKey (ch - 1) : juce::String();
            saveSpacers();
            en.refreshDucking();
            channels = bind.channels();
            resizeToFit();
            repaint();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }

    Hit hit (juce::Point<float> p) const
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            const float x0 = stripX (ch);
            float y0 = 40.0f;
            for (auto& s : layoutFor (ch).sections)
            {
                if (! shows (s))
                    continue;
                for (auto& item : s.items)
                {
                    const auto area = (item.r * zoom).translated (x0, y0);
                    if (item.id.isNotEmpty() && area.contains (p))
                        return { &item, ch };
                    if (item.kind == "order" && pluginChip (area, ch).contains (p))   // (the chip under the chain, with the EQ last)
                        return { &item, ch };
                }
                y0 += s.height * zoom;
            }
        }
        return {};
    }

    void draw (juce::Graphics& g, const Item& item, const juce::AffineTransform& at, int ch)
    {
        const auto& t = Theme::get();
        const auto r = item.r.transformedBy (at);
        const auto key = juce::String (ch) + ":" + item.id;
        if (item.kind == "knob")
        {
            // (its dots round it and its name under it, as the strips draw them)
            const auto area = juce::Rectangle<float> (r.getX() - 9.0f * zoom, r.getY() - 8.0f * zoom, r.getWidth() + 18.0f * zoom, r.getHeight() + 12.0f * zoom);
            knob (g, r.getCentre(), r.getWidth(), bind.get (ch, item.id, 0.5f), named (item.colour), true, ink());
            const bool turning = held.item == &item && held.ch == ch;
            if (const float over = eased (ch, item.id + "#over", turning || (hovered.item == &item && hovered.ch == ch) ? 1.0f : 0.0f); over > 0.01f)
            {
                g.setColour (Console::Colours::accent().withAlpha ((turning ? 0.95f : 0.6f) * over));   // (under the mouse: ringed; turning: brighter)
                g.drawEllipse (r.expanded (3.0f * zoom), juce::jmax (1.2f, 1.8f * zoom));
            }
            if (item.name.isNotEmpty())
            {
                g.setColour (ink());
                g.setFont (LargeFormat::print (9.0f * zoom));
                g.drawText (item.name, juce::Rectangle<float> (area.getX(), area.getBottom(), area.getWidth(), 14.0f * zoom), juce::Justification::centred, false);
            }
        }
        else if (item.kind == "switch")
        {
            // The mixer's small square switch: a grey cap, lit whole in its colour.
            const bool on = bind.isOn (ch, item.id, item.lit);
            const auto lamp = named (item.lamp);
            const float corner = 2.5f * zoom, lit = eased (ch, item.id, on ? 1.0f : 0.0f);   // (easing on and off)
            const auto touched = touch (ch, item.id, r);
            g.setColour (juce::Colours::black.withAlpha (0.55f * (1.0f - touched.press)));
            g.fillRoundedRectangle (r.translated (0.0f, 1.5f * zoom), corner);
            const auto cap = r.translated (0.0f, touched.press * 1.5f * zoom);
            if (lit > 0.01f)
                LargeFormat::bloom (g, cap, lamp, 0.8f * lit, corner, 4.0f * zoom);
            const float shine = 1.0f + 0.14f * touched.hover - 0.22f * touched.press;
            g.setGradientFill (juce::ColourGradient (Console::Colours::knobTop().interpolatedWith (lamp.brighter (0.3f), lit).withMultipliedBrightness (shine), cap.getX(), cap.getY(),
                                                     Console::Colours::knobBottom().interpolatedWith (lamp.darker (0.25f), lit).withMultipliedBrightness (shine), cap.getX(), cap.getBottom(), false));
            g.fillRoundedRectangle (cap, corner);
            g.setColour (touched.hover > 0.01f ? Console::Colours::accent().withAlpha (0.4f + 0.5f * touched.hover) : juce::Colours::black.withAlpha (0.6f));
            g.drawRoundedRectangle (cap, corner, 1.0f + 0.5f * touched.hover);
            if (lit > 0.5f && item.lamp == "orange")   // (the power symbol on the IN switch)
            {
                const auto c = cap.reduced (cap.getWidth() * 0.28f);
                juce::Path powerSymbol;
                powerSymbol.addCentredArc (c.getCentreX(), c.getCentreY(), c.getWidth() * 0.5f, c.getHeight() * 0.5f, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.6f, true);
                g.setColour (juce::Colour (0xff3a1e08));
                g.strokePath (powerSymbol, juce::PathStrokeType (1.6f * zoom));
                g.drawLine (c.getCentreX(), c.getY() - 1.0f * zoom, c.getCentreX(), c.getCentreY(), 1.6f * zoom);
            }
        }
        else if (item.kind == "chip")
        {
            // A box of the processing chain (FILT, EQ, DYN): lit, filled in its colour; else outlined.
            const auto colour = named (item.colour);
            const float lit = eased (ch, item.id, bind.isOn (ch, item.id, item.lit) ? 1.0f : 0.0f);
            drawBox (g, r, colour, lit, touch (ch, item.id, r), item.id == "device" ? bind.text (ch, "device", {}) : item.text,
                     juce::jmin (r.getHeight() * 0.55f, 12.0f * zoom + 2.0f), 2.0f * zoom);
        }
        else if (item.kind == "order")
        {
            // The three blocks top to bottom as they run, joined by the flow line.
            const auto colour = named (item.colour);
            const auto blocks = bind.order (ch);
            const float h = r.getHeight() / 3.0f, chipH = juce::jmin (h * 0.55f, 30.0f * zoom);
            g.setColour (colour.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (r.getCentreX() - 1.0f * zoom, r.getY(), 2.0f * zoom, r.getHeight()));
            for (int i = 0; i < 3; ++i)
            {
                const int block = blocks[(size_t) i];
                const auto chip = orderChip (r, 0).translated (0.0f, h * eased (ch, "place" + juce::String (block), (float) i));   // (gliding to its place)
                const float lit = eased (ch, "block" + juce::String (block), bind.blockOn (ch, block) ? 1.0f : 0.0f);
                drawBox (g, chip, colour, lit, touch (ch, "block" + juce::String (block), chip),
                         block == Binding::filters ? "FILT" : block == Binding::eq ? "EQ" : "DYN", chipH * 0.55f, 2.0f * zoom);
            }
            if (const auto pc = pluginChip (r, ch); ! pc.isEmpty())   // (its plugins, after the EQ)
            {
                const auto& inserts = bind.strip (ch)->inserts;
                const auto text = inserts.front()->getName().toUpperCase() + (inserts.size() > 1 ? " +" + juce::String ((int) inserts.size() - 1) : juce::String());
                drawBox (g, pc, Console::Colours::palette ("salmon"), 0.0f, touch (ch, "plugins", pc), text, juce::jmax (9.0f, pc.getHeight() * 0.62f), 2.0f * zoom);
            }
        }
        else if (item.kind == "routes")
        {
            // A box per bus and output, lit while it's sent there.
            const auto colour = named (item.colour);
            const auto list = bind.routes (ch);
            for (int i = 0; i < (int) list.size(); ++i)
            {
                const auto box = routeBox (r, i, (int) list.size());
                if (box.isEmpty())
                    break;
                const float lit = eased (ch, "route:" + list[(size_t) i].label, list[(size_t) i].on ? 1.0f : 0.0f);
                drawBox (g, box, colour, lit, touch (ch, "route:" + list[(size_t) i].label, box), list[(size_t) i].shortLabel,
                         juce::jmax (9.0f, box.getHeight() * 0.5f), 2.0f * zoom);
            }
        }
        else if (item.kind == "arrow")
        {
            if (pluginsAtEnd (ch))   // (its plugins' chip stands there: the chain ends with them)
                return;
            juce::Path arrow;
            arrow.addTriangle (r.getX(), r.getY(), r.getRight(), r.getY(), r.getCentreX(), r.getBottom());
            g.setColour (named (item.colour));
            g.fillPath (arrow);
        }
        else if (item.kind == "pads")
        {
            // THE PADS: a bank of 16 (A to D keys above), each lit while it plays, its progress
            // along its foot; STOP ALL under them.
            auto& pads = engine->getPads();
            for (int b = 0; b < Pads::banks; ++b)
            {
                const auto k = padBankKey (r, b);
                const auto touched = touch (ch, "bank" + juce::String (b), k);
                LargeFormat::key (g, k, juce::String::charToString ((juce::juce_wchar) ('A' + b)), b == padBank ? 1.0f : 0.0f, Console::Colours::palette ("orange"), true, touched.press * 1.5f, touched.hover);
            }
            for (int i = 0; i < Pads::perBank; ++i)
            {
                const int index = padBank * Pads::perBank + i;
                const auto& pad = pads.pad (index);
                const auto touched = touch (ch, "pad" + juce::String (index), padCell (r, i));
                const auto cell = padCell (r, i).translated (0.0f, touched.press * 1.5f * zoom);
                const bool loaded = pad.sound != nullptr, on = pads.isPlaying (index);
                const auto colour = pad.colour.isTransparent() ? Console::Colours::palette ("teal") : pad.colour;
                if (on)
                    LargeFormat::bloom (g, cell, colour, 0.9f, 3.0f, 4.0f * zoom);
                g.setColour ((on ? colour.withMultipliedBrightness (0.9f) : loaded ? juce::Colour (0xff3a3d42) : juce::Colour (0xff26282c))
                                 .brighter (0.25f * touched.hover).darker (0.4f * touched.press));
                g.fillRoundedRectangle (cell, 3.0f);
                g.setColour (touched.hover > 0.01f ? Console::Colours::accent().withAlpha (0.4f + 0.5f * touched.hover) : juce::Colours::black.withAlpha (0.65f));
                g.drawRoundedRectangle (cell, 3.0f, 1.0f + 0.5f * touched.hover);
                if (on)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.8f));
                    g.fillRect (cell.withTop (cell.getBottom() - 3.0f * zoom).withWidth (cell.getWidth() * pads.progress (index)));
                }
                g.setColour (on ? juce::Colour (0xff101214) : loaded ? Console::Colours::inkBright() : juce::Colour (0xff8a8e94));   // (empty: dimmer, still readable)
                g.setFont (LargeFormat::print (juce::jmax (9.0f, 10.0f * zoom)));
                g.drawFittedText (loaded && pad.name.isNotEmpty() ? pad.name.toUpperCase() : juce::String (index + 1), cell.reduced (3.0f).toNearestInt(), juce::Justification::centred, 2, 0.8f);
            }
            const auto stopTouch = touch (ch, "stopall", padStopKey (r));
            LargeFormat::key (g, padStopKey (r), "STOP ALL", 0.0f, Console::Colours::stopLamp(), true, stopTouch.press * 1.5f, stopTouch.hover);
        }
        else if (item.kind == "tape")
        {
            // THE TAPE: what's in the deck and its clock, a bar to seek, the transport, SHUFFLE and
            // ADD, then the playlist (the track in the deck lit).
            auto& tape = engine->getTape();
            const auto screen = tapeArea (r, "screen");
            g.setColour (Console::Colours::tapeGlass());
            g.fillRoundedRectangle (screen, 3.0f);
            g.setColour (juce::Colours::black);
            g.drawRoundedRectangle (screen, 3.0f, 1.0f);
            auto clock = [] (double t) { const int s = juce::jmax (0, (int) t); return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2); };
            g.setColour (Console::Colours::tapeInk());
            g.setFont (LargeFormat::print (juce::jmax (9.0f, 11.0f * zoom)));
            const auto title = juce::isPositiveAndBelow (tape.current(), tape.size()) ? tape.file (tape.current()).getFileNameWithoutExtension().toUpperCase() : juce::String ("NO TRACK");
            g.drawFittedText (title, screen.reduced (6.0f * zoom, 0.0f).withTrimmedBottom (screen.getHeight() * 0.45f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            g.drawText (clock (tape.position()) + " / " + clock (tape.length()), screen.reduced (6.0f * zoom, 0.0f).withTrimmedTop (screen.getHeight() * 0.5f), juce::Justification::centredLeft, false);
            const auto bar = tapeArea (r, "bar");
            g.setColour (juce::Colour (0xff0b0c0d));
            g.fillRoundedRectangle (bar, 2.0f);
            g.setColour (Console::Colours::palette ("orange"));
            if (tape.length() > 0.0)
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, tape.position() / tape.length())), 2.0f);
            for (auto& k : transportKeys())   // (as a deck's: the symbols every transport uses)
            {
                const bool lit = (k == "PLAY" && tape.isPlaying()) || (k == "PAUSE" && ! tape.isPlaying() && tape.isLoaded() && tape.position() > 0.0);
                const auto keyArea = tapeArea (r, k);
                const auto touched = touch (ch, "tape" + k, keyArea);
                LargeFormat::key (g, keyArea, {}, lit ? 1.0f : 0.0f, k == "PAUSE" ? Console::Colours::palette ("orange") : Console::Colours::runLamp(), true, touched.press * 1.5f, touched.hover);
                drawTransport (g, k, keyArea.translated (0.0f, touched.press * 1.5f).reduced (keyArea.getWidth() * 0.26f, keyArea.getHeight() * 0.28f), lit ? Console::Colours::litInk() : juce::Colour (0xff26282b));
            }
            for (auto [k, text, lit, lamp] : { std::tuple (juce::String ("SHUFFLE"), juce::String ("SHUFFLE"), tape.getShuffle(), Console::Colours::palette ("orange")),
                                               std::tuple (juce::String ("ADD"), juce::String ("ADD..."), false, Console::Colours::powerLamp()) })
            {
                const auto touched = touch (ch, "tape" + k, tapeArea (r, k));
                LargeFormat::key (g, tapeArea (r, k), text, lit ? 1.0f : 0.0f, lamp, true, touched.press * 1.5f, touched.hover);
            }
        }
        else if (item.kind == "playlist")
        {
            auto& tape = engine->getTape();
            const auto list = r;
            const float rowH = juce::jmax (16.0f, 22.0f * zoom);
            const int rows = (int) (list.getHeight() / rowH), first = juce::jlimit (0, juce::jmax (0, tape.size() - rows), tape.current() - rows / 2);
            g.setFont (LargeFormat::print (juce::jmax (9.0f, 10.0f * zoom)));
            for (int i = first; i < tape.size() && i < first + rows; ++i)
            {
                const auto row = list.withY (list.getY() + (float) (i - first) * rowH).withHeight (rowH - 2.0f);
                const bool here = i == tape.current();
                g.setColour (here ? Console::Colours::accent().withMultipliedBrightness (0.60f) : Console::Colours::bay());
                g.fillRoundedRectangle (row, 2.0f);
                g.setColour (here ? juce::Colours::white : Console::Colours::ink());
                g.drawFittedText (juce::String (i + 1) + "  " + tape.file (i).getFileNameWithoutExtension().toUpperCase(), row.reduced (5.0f * zoom, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            }
            if (tape.size() == 0)
            {
                g.setColour (ink());
                g.drawText ("NO TRACKS", list.withHeight (rowH), juce::Justification::centredLeft, false);
            }
        }
        else if (item.kind == "paths")
        {
            for (auto& part : pathParts (ch, r))
            {
                if (part.r.getBottom() > r.getBottom() + 0.5f)
                    break;
                g.setFont (LargeFormat::print (part.kind == 0 ? juce::jmax (10.0f, 12.0f * zoom) : g_font_size));
                if (part.kind == 0 || part.kind == 3)
                {
                    g.setColour (part.kind == 0 ? juce::Colour (0xffe3e5e8) : ink());
                    g.drawFittedText (part.text, part.r.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
                    continue;
                }
                const auto colour = part.kind == 1 ? Console::Colours::palette ("teal") : Console::Colours::palette ("salmon");   // (feeds: teal; plugins: salmon)
                drawBox (g, part.r, colour, 0.0f, touch (ch, "part:" + part.text, part.r), part.text, g_font_size, 3.0f);
            }
        }
        else if (item.kind == "pluginlist")
        {
            // Its plugins, one under another (click one: it opens); none: says so.
            auto* s = bind.strip (ch);
            const float rowH = 26.0f * zoom;
            g.setFont (LargeFormat::print (11.0f * juce::jmax (0.8f, zoom)));
            if (s == nullptr || s->inserts.empty())
            {
                g.setColour (ink());
                g.drawText ("NO PLUGINS", r.withHeight (rowH), juce::Justification::centredLeft, false);
            }
            else
                for (int i = 0; i < (int) s->inserts.size() && (float) (i + 1) * rowH <= r.getHeight(); ++i)
                {
                    const auto row = r.withY (r.getY() + (float) i * rowH).withHeight (rowH - 3.0f * zoom);
                    g.setColour (Console::Colours::bay());
                    g.fillRoundedRectangle (row, 3.0f);
                    g.setColour (juce::Colours::black.withAlpha (0.6f));
                    g.drawRoundedRectangle (row, 3.0f, 1.0f);
                    auto* o = engine != nullptr ? engine->insertOptions (s->inserts[(size_t) i].get()) : nullptr;
                    g.setColour (o != nullptr && o->bypassed.load() ? juce::Colour (0xff7d8086) : Console::Colours::inkBright());
                    g.drawFittedText (juce::String (i + 1) + "  " + s->inserts[(size_t) i]->getName().toUpperCase(), row.reduced (6.0f * zoom, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
                }
        }
        else if (item.kind == "vu")
        {
            // A VU-style meter: a cream face, its scale 0 to 20 dB of gain reduction, the needle falling from 0.
            const float gr = juce::jlimit (0.0f, 20.0f, -bind.busReduction (ch));
            g.setColour (Console::Colours::rim());
            g.fillRoundedRectangle (r, 4.0f * zoom);
            const auto face = r.reduced (5.0f * zoom);
            g.setGradientFill (juce::ColourGradient (Console::Colours::vuTop(), face.getX(), face.getY(), Console::Colours::vuBottom(), face.getX(), face.getBottom(), false));
            g.fillRoundedRectangle (face, 3.0f * zoom);
            const juce::Point<float> pivot (face.getCentreX(), face.getBottom() + face.getHeight() * 0.35f);
            const float radius = face.getHeight() * 1.05f, a0 = juce::degreesToRadians (40.0f), a1 = juce::degreesToRadians (-40.0f);
            auto onDial = [&pivot] (float a, float rr) { return juce::Point<float> (pivot.x + rr * std::sin (a), pivot.y - rr * std::cos (a)); };
            g.setColour (Console::Colours::vuInk());
            g.setFont (LargeFormat::print (9.0f * juce::jmax (0.8f, zoom)));
            for (int db : { 0, 3, 6, 10, 14, 20 })
            {
                const float a = a0 + (a1 - a0) * (float) db / 20.0f;
                const auto p1 = onDial (a, radius * 0.86f), p2 = onDial (a, radius * 0.95f);
                g.drawLine (p1.x, p1.y, p2.x, p2.y, 1.2f);
                g.drawText (juce::String (db), juce::Rectangle<float> (24.0f * zoom, 12.0f * zoom).withCentre (onDial (a, radius * 0.77f)), juce::Justification::centred, false);
            }
            g.drawText ("GR", face.withTrimmedTop (face.getHeight() * 0.55f), juce::Justification::centred, false);
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (face.toNearestInt());
            const float a = a0 + (a1 - a0) * gr / 20.0f;
            const auto tip = onDial (a, radius * 0.97f);
            g.setColour (juce::Colour (0xff141414));
            g.drawLine (pivot.x, pivot.y, tip.x, tip.y, 1.6f * zoom);
        }
        else if (item.kind == "lamp")
        {
            // A small LED that lights while the channel's source has signal (DUCK: while it's ducked).
            const bool on = item.id == "duck" ? bind.ducked (ch) : bind.levelDb (ch, 0) > -40.0f;
            LargeFormat::led (g, r.getCentre(), on, named (item.colour), r.getWidth() * 0.7f);
        }
        else if (item.kind == "screen")
        {
            const bool orange = item.colour == "orange";
            g.setColour (orange ? Console::Colours::palette ("orange").withMultipliedBrightness (0.14f) : Console::Colours::tapeGlass());
            g.fillRoundedRectangle (r, 2.0f * zoom);
            g.setColour (juce::Colours::black);
            g.drawRoundedRectangle (r, 2.0f * zoom, 1.0f);
            g.setColour (orange ? juce::Colour (0xfff2a24a) : Console::Colours::tapeInk());
            g.setFont (LargeFormat::print (r.getHeight() * 0.55f, false));
            g.drawText (bind.text (ch, item.id, item.text), r, juce::Justification::centred, false);
        }
        else if (item.kind == "name")
        {
            const auto icon = r.withWidth (r.getHeight()).reduced (r.getHeight() * 0.28f);
            juce::Path powerSymbol;
            powerSymbol.addCentredArc (icon.getCentreX(), icon.getCentreY(), icon.getWidth() * 0.5f, icon.getHeight() * 0.5f, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.6f, true);
            const bool on = bind.isOn (ch, "power", true);
            const auto touched = touch (ch, "power", r.withWidth (r.getHeight()), true);
            if (touched.hover > 0.01f)
            {
                g.setColour (Console::Colours::accent().withAlpha (0.25f * touched.hover + 0.2f * touched.press));
                g.fillEllipse (icon.expanded (icon.getWidth() * 0.45f));
            }
            g.setColour ((on ? Console::Colours::palette ("orange") : juce::Colour (0xff9a9da2)).brighter (0.4f * touched.hover));
            g.strokePath (powerSymbol, juce::PathStrokeType (1.6f * zoom));
            g.drawLine (icon.getCentreX(), icon.getY() - 1.0f * zoom, icon.getCentreX(), icon.getCentreY(), 1.6f * zoom);
            g.setColour (on ? Console::Colours::inkBright() : Console::Colours::inkFaint());
            g.setFont (LargeFormat::print (r.getHeight() * 0.42f));
            g.drawText (bind.text (ch, "name", {}), r.withTrimmedLeft (r.getHeight()), juce::Justification::centred, false);
        }
        else if (item.kind == "scale")
        {
            g.setColour (ink());
            g.setFont (LargeFormat::print (9.0f * zoom, false));
            const float pitch = r.getHeight() / (float) juce::jmax (1, item.marks.size() - 1);
            for (int i = 0; i < item.marks.size(); ++i)
            {
                const float y = r.getY() + pitch * (float) i;
                g.fillRect (r.getX(), y - 0.5f, 3.0f * zoom, 1.0f);
                g.drawText (item.marks[i], juce::Rectangle<float> (r.getX() + 5.0f * zoom, y - 7.0f * zoom, r.getWidth(), 14.0f * zoom), juce::Justification::centredLeft, false);
            }
        }
        else if (item.kind == "key")
        {
            const bool on = item.id == "sel" ? ch == selected : bind.isOn (ch, item.id, false);
            const float lit = eased (ch, item.id, on ? 1.0f : 0.0f);
            const auto touched = touch (ch, item.id, r);
            if (r.getHeight() < 40.0f * zoom && r.getWidth() < 60.0f * zoom)
                LargeFormat::toggle (g, r, item.text, lit, named (item.lamp), true, touched.press * 1.5f, touched.hover);
            else
            {
                LargeFormat::key (g, r, item.ink.isNotEmpty() ? juce::String() : item.text, lit, named (item.lamp), true, touched.press * 1.5f, touched.hover);
                if (item.ink.isNotEmpty())   // (CUT printed red)
                {
                    g.setColour (named (item.ink));
                    g.setFont (LargeFormat::print (11.0f * zoom));
                    g.drawText (item.text, r.translated (0.0f, touched.press * 1.5f), juce::Justification::centred, false);
                }
            }
        }
        else if (item.kind == "leds")
        {
            const float pitch = r.getHeight() / (float) item.count, d = juce::jmin (r.getWidth() - 4.0f, pitch * 0.8f);
            const int on = bind.reductionLit (ch, item.id, item.litCount >= 0 ? item.litCount : (item.id == "compGr" ? 2 : 3));
            for (int i = 0; i < item.count; ++i)
                LargeFormat::led (g, { r.getCentreX(), r.getY() + ((float) i + 0.5f) * pitch }, item.count - i <= on,
                                  named (item.colours[i]), d);
        }
        else if (item.kind == "bars")
        {
            // The level as round LEDs on the selected meter standard (SETTINGS > Audio > Meter
            // standard): its own LEDs, colour zones and printed marks (in the room at the column's
            // left), the highest lit held a moment (PEAK HOLD). One column: the louder side.
            const auto& st = Metering::standard();
            const int count = (int) st.leds.size(), columns = item.single ? 1 : 2;
            const float pitch = r.getHeight() / (float) juce::jmax (1, count), colW = r.getWidth() / (float) columns;
            const float d = juce::jmax (3.0f, juce::jmin (colW - 3.0f * zoom, pitch * 0.58f));
            for (int side = 0; side < columns; ++side)
            {
                const float db = item.single ? juce::jmax (bind.meterDb (ch, 0), bind.meterDb (ch, 1)) : bind.meterDb (ch, side);
                const int on = Metering::ledsLit (st, db);
                const int hold = heldPeak (ch * 2 + side, on);
                const float cx = r.getX() + colW * ((float) side + 0.5f);
                for (int i = 0; i < count; ++i)
                    LargeFormat::led (g, { cx, r.getBottom() - pitch * ((float) i + 0.5f) }, i < on || i == hold - 1,
                                      Metering::ledColour (st, st.leds[(size_t) i]), d);
            }
            g.setColour (ink());
            g.setFont (LargeFormat::print (7.5f * zoom, false));
            const float markX = r.getX() - 13.5f * zoom, markW = 12.5f * zoom;
            for (int i = 0; i < count; ++i)
                if (const auto mark = Metering::markAt (st, st.leds[(size_t) i]); mark.isNotEmpty())
                    g.drawFittedText (mark, juce::Rectangle<float> (markX, r.getBottom() - pitch * ((float) i + 1.0f), markW, pitch).toNearestInt(),
                                      juce::Justification::centredRight, 1, 0.6f);
        }
        else if (item.kind == "label")
        {
            const auto text = item.text.replace ("{name}", bind.text (ch, "name", {})).replace ("%", juce::String (ch + 1));   // ({name}: the channel's, % its number)
            const auto just = item.align == "right" ? juce::Justification::centredRight : item.align == "centre" ? juce::Justification::centred : juce::Justification::centredLeft;
            g.setFont (LargeFormat::print (item.textSize * zoom));
            if (light)   // (pressed into the light grey: a dark edge under the lettering)
            {
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.drawText (text, r.translated (0.0f, 1.0f), just, false);
            }
            g.setColour (ink());
            g.drawText (text, r, just, false);
        }
        else if (item.kind == "line")
        {
            g.setColour (item.colour.isNotEmpty() ? named (item.colour) : ink().withAlpha (0.55f));
            g.strokePath (item.line, juce::PathStrokeType (2.0f * zoom), at);
        }
        else if (item.kind == "display")
        {
            DotMatrix::draw (g, juce::String (ch + 1).paddedLeft ('0', 3), r);
        }
        else if (item.kind == "fader")
        {
            // The slot down the middle, and the long cream cap with its ridges at the level.
            g.setColour (Console::Colours::rim());
            g.fillRoundedRectangle (r.withSizeKeepingCentre (6.0f * zoom, r.getHeight()), 3.0f * zoom);
            const float y = r.getBottom() - bind.get (ch, item.id, 0.75f) * r.getHeight();
            const auto cap = juce::Rectangle<float> (r.getWidth() * 0.8f, 58.0f * zoom).withCentre ({ r.getCentreX(), y });
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (cap.translated (0.0f, 3.0f * zoom), 3.0f * zoom);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd2d3d5), cap.getX(), cap.getY(), juce::Colour (0xff7d7f83), cap.getX(), cap.getBottom(), false));
            g.fillRoundedRectangle (cap, 3.0f * zoom);
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            for (int i = 1; i < 8; ++i)
                g.fillRect (cap.getX() + 3.0f * zoom, cap.getY() + cap.getHeight() * (float) i / 8.0f, cap.getWidth() - 6.0f * zoom, 1.0f);
            g.setColour (juce::Colours::black);
            g.fillRect (cap.withSizeKeepingCentre (cap.getWidth(), 2.0f * zoom));
            const bool moving = held.item == &item && held.ch == ch;
            if (const float over = eased (ch, item.id + "#over", moving || (hovered.item == &item && hovered.ch == ch) ? 1.0f : 0.0f); over > 0.01f)
            {
                g.setColour (Console::Colours::accent().withAlpha ((moving ? 0.95f : 0.6f) * over));
                g.drawRoundedRectangle (cap.expanded (1.5f * zoom), 3.5f * zoom, juce::jmax (1.2f, 1.6f * zoom));
            }
        }
        else if (item.kind == "meter")
        {
            const int segments = 30, on = 18;
            const float pitch = r.getHeight() / (float) segments;
            for (int i = 0; i < segments; ++i)
            {
                const auto colour = i >= segments - 3 ? t.ledRed : (i >= segments - 8 ? t.ledYellow : t.ledGreen);
                g.setColour (i < on ? colour : colour.withMultipliedBrightness (0.2f));
                g.fillRect (juce::Rectangle<float> (r.getX(), r.getBottom() - pitch * (float) (i + 1) + 1.0f, r.getWidth(), pitch - 2.0f));
            }
        }
        if (outlines)   // (every part's box, to check the layout)
        {
            g.setColour (juce::Colour (0xffff40a0).withAlpha (0.8f));
            g.drawRect (item.kind == "line" ? item.line.getBounds().transformedBy (at) : r, 1.0f);
        }
    }

    void reload()
    {
        layout = load (file);
        masterLayout = load (file.getSiblingFile ("master.json"));
        appLayout = load (file.getSiblingFile ("app.json"));
        if (! appLayout.error.isEmpty() || appLayout.sections.size() != layout.sections.size())
            appLayout = layout;
        combinatorLayout = load (file.getSiblingFile ("combinator.json"));
        if (! combinatorLayout.error.isEmpty() || combinatorLayout.sections.size() != layout.sections.size())
            combinatorLayout = appLayout;
        padsLayout = load (file.getSiblingFile ("pads.json"));
        if (! padsLayout.error.isEmpty() || padsLayout.sections.size() != layout.sections.size())
            padsLayout = appLayout;
        tapeLayout = load (file.getSiblingFile ("tape.json"));
        if (! tapeLayout.error.isEmpty() || tapeLayout.sections.size() != layout.sections.size())
            tapeLayout = appLayout;
        if (! masterLayout.error.isEmpty() || masterLayout.sections.size() != layout.sections.size())
            masterLayout = layout;
        shown = file.getLastModificationTime();
        resizeToFit();
        repaint();
    }

    void resizeToFit()
    {
        layoutColumns();
        setSize (juce::roundToInt (deskWidth + 20.0f), juce::roundToInt (60.0f + stripHeight()));
    }
    // A strip's height on screen: its sections shown (COMPACT: the name bar and the fader).
    float stripHeight() const
    {
        float h = 0.0f;
        for (auto& s : layout.sections)
            if (shows (s))
                h += s.height;
        return h * zoom;
    }

    void timerCallback() override
    {
        bind.syncOrder();   // (a bus or output added, removed or renamed: the desk's order follows)
        if (++ticks % 8 == 0 && file.getLastModificationTime() != shown)   // (the layout file saved: drawn again)
            reload();
        if (ticks % 6 == 0)
            followFaider();
        if (ticks % 15 == 0)
            keepAppsRaw();
        if (engine != nullptr)
        {
            if (const int n = bind.channels(); n != channels)   // (a channel added or removed elsewhere)
            {
                channels = n;
                resizeToFit();
            }
            // What changed, 30 times a second: only those parts drawn again (a meter moving, a knob
            // turned from a controller, a key switched elsewhere, the pads, the tape); the whole desk
            // once a second (anything else changed elsewhere: a route, a plugin, a device).
            if (ticks % 30 == 0)
                repaint();
            else
                for (int ch = 0; ch < channels; ++ch)
                    repaintChanged (ch);
        }
    }

    juce::File file;
    Layout masterLayout, appLayout, padsLayout, tapeLayout, combinatorLayout;
    int padBank = 0;   // (the pads' bank shown: A to D)
    juce::Time shown;
    Layout layout;
    float zoom = 0.68f;   // (the layout's pixels are the plugin's at about 150%: this is its 100%)
    int channels = 8;
    bool outlines = false;
    bool compact = false;   // (COMPACT: setCompact)
    AudioEngine* engine;
    MixerComponent* mixer;   // (Faider's: its plugin picker and rack)
    Binding bind;
    int ticks = 0;
    int selected = -1;
    Hit hovered;   // (the knob or fader under the mouse: its value shows)
    juce::Point<float> pointer { -1.0e6f, -1.0e6f }, pointerDownAt;   // (the mouse over the desk, and where its button went down)
    bool pointerDown = false;
    int pointerCh = -1;   // (the strip under the mouse: drawn again as it moves, for TOUCH)
    std::vector<float> power;   // (each strip's on/off as shown: 1 on, 0 off, easing between)
    int pressedPad = -1;        // (the pad held down)
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<Spacer> spacers;
    std::vector<SpacerColumn> spacerColumns;
    std::vector<float> xs;     // each strip's left edge
    float deskWidth = 0.0f;
    std::vector<std::vector<juce::int64>> shownParts;   // (each strip's parts, as last drawn: partState)
    std::vector<juce::int64> shownStrips;               // (each strip's on/off, selection, light or dark and colour, as last drawn)
    Hit held;
    juce::String followed;   // (the selection, as Faider keys it: FOLLOW SELECT notices it change)
    int dragging = -1;       // (the channel whose name bar is held: dragged, it moves)
    bool dragMoved = false;
    float dragX = 0.0f, dragGrab = 0.0f;
    std::map<juce::String, float> shownX;   // (strips gliding to new places: where each is shown)
    juce::Point<float> glide, shownAt;   // (where the view is heading, and where it is, to the fraction)
    bool gliding = false;
    double lastFrame = 0.0;
    float frameDt = 1.0f / 60.0f;   // (the time since the last frame, s)
    std::unique_ptr<juce::VBlankAttachment> vblank;
    struct Ease { float shown = 0.0f, target = 0.0f; };
    std::map<std::pair<int, juce::String>, Ease> eases;
    float dragFrom = 0.0f;
    bool light = false;   // (the strip being drawn is a light grey one)
    // The print on the strip: light on both greys.
    juce::Colour ink() const { return light ? juce::Colour (0xffdcdde0) : Console::Colours::inkDim(); }
};

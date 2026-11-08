#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/RemoteControl.h"
#include "Console.h"

// MIDI MAP: tying a MIDI controller to the desk. Every control that can be played from MIDI is
// outlined on screen; click one, then move a knob or fader or press a key on the controller, and
// it's bound. Bound controls show what they're bound to; right-click one to clear it. The tabs and
// CONTROL key keep working, so any page can be mapped. Ctrl+L, Esc or CONTROL > MIDI MAP leaves it.
namespace MidiMap
{
    // Marks a control as bindable.
    inline void tag (juce::Component& c, const juce::String& target) { c.getProperties().set ("midiTarget", target); }

    // A component that draws its controls itself (the console's strips) lists them instead: each
    // control's area (in the component's own coordinates) and its target.
    struct Provider
    {
        virtual ~Provider() = default;
        virtual std::vector<std::pair<juce::Rectangle<int>, juce::String>> midiTargets() = 0;
    };

    inline juce::String targetOf (juce::Component& c) { return c.getProperties()["midiTarget"].toString(); }

    // A target written for people: "MIC FOCUS  FADER", "MIC FOCUS > OUT MUSIC".
    inline juce::String describe (const juce::String& target)
    {
        juce::StringArray t;
        t.addTokens (target, ":", "");
        if (t[0] == "send" && t.size() >= 5)  return t[2] + " > " + t[4];
        if (t[0] == "param" && t.size() >= 4) return t[2] + "  " + t[3].toUpperCase();
        if (t[0] == "plug" && t.size() >= 5)  return t[2] + "  " + t[3].toUpperCase() + " #" + juce::String (t[4].getIntValue() + 1);
        if (t.size() >= 3 && (t[1] == "input" || t[1] == "bus" || t[1] == "output"))
        {
            // The key as the desk prints it (the target keeps its saved name: "mute" is CUT, an
            // output's "safe" is LIMIT).
            const auto key = t[0] == "on" ? juce::String ("POWER") : t[0] == "inserts" ? juce::String ("INSERTS")
                           : t[0] == "mute" ? juce::String ("CUT") : t[0] == "safe" ? juce::String ("LIMIT") : t[0].toUpperCase();
            return t[2] + "  " + key;
        }
        return target.replaceCharacter (':', ' ').toUpperCase();
    }
}

class MidiMapOverlay : public juce::Component,
                       private juce::Timer
{
public:
    // root: the window's content (whose controls are mapped); passThrough: keys that keep working
    // under the overlay (the tabs' bar, CONTROL).
    MidiMapOverlay (RemoteControl& r, juce::Component& rootComponent, std::function<bool (juce::Point<int>)> passThroughAt)
        : remote (r), root (rootComponent), passThrough (std::move (passThroughAt))
    {
        setWantsKeyboardFocus (true);
        startTimerHz (12);
    }

    ~MidiMapOverlay() override { remote.cancelLearn(); }

    std::function<void()> onClose;

    bool hitTest (int x, int y) override { return ! lookingThrough && ! (passThrough && passThrough (getBoundsInParent().getPosition() + juce::Point<int> (x, y))); }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key != juce::KeyPress::escapeKey)
            return false;
        if (remote.learning().isNotEmpty())
            remote.cancelLearn();
        else if (onClose)
            onClose();
        return true;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.32f));
        const auto& mappings = remote.getMappings();
        const auto learning = remote.learning();
        const float pulse = 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.008f);

        for (auto& item : collect())
        {
            const auto r = item.area.toFloat().reduced (0.5f);
            juce::String bound;
            for (auto& [control, target] : mappings)
                if (target == item.target)
                    bound = RemoteControl::describe (control);

            if (item.target == learning)
            {
                g.setColour (Console::Colours::ledGreen().withAlpha (0.25f + 0.35f * pulse));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (Console::Colours::ledGreen());
                g.drawRoundedRectangle (r, 3.0f, 2.0f);
            }
            else if (bound.isNotEmpty())
            {
                g.setColour (amber.withAlpha (0.22f));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (amber);
                g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 2.0f);
            }
            else
            {
                g.setColour (cyan.withAlpha (0.14f));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (cyan);
                g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 2.0f);
            }

            if (bound.isNotEmpty() || item.target == learning)
            {
                const auto text = item.target == learning ? juce::String ("MOVE A CONTROL") : bound;
                const auto font = Console::legendFont (Console::Type::tag);
                const float w = (float) juce::GlyphArrangement::getStringWidthInt (font, text) + 8.0f;
                auto tag = juce::Rectangle<float> (r.getCentreX() - w * 0.5f, r.getY() - 7.0f, w, 14.0f);
                tag = tag.constrainedWithin (getLocalBounds().toFloat());
                g.setColour (item.target == learning ? Console::Colours::ledGreen() : amber);
                g.fillRoundedRectangle (tag, 3.0f);
                g.setColour (juce::Colour (0xff141414));
                g.setFont (font);
                g.drawText (text, tag, juce::Justification::centred, false);
            }
        }

        // The bar along the bottom: what's happening, and the last control the controller sent.
        auto bar = getLocalBounds().removeFromBottom (28);
        g.setColour (Console::Colours::panelDark().withAlpha (0.94f));
        g.fillRect (bar);
        g.setColour (Console::Colours::ledGreen());
        g.fillRect (bar.removeFromTop (2));
        bar = bar.reduced (10, 0);
        g.setFont (Console::legendFont (Console::Type::body));
        g.setColour (Console::Colours::legend());
        const auto status = learning.isNotEmpty() ? "MIDI MAP  -  " + MidiMap::describe (learning) + ": MOVE A CONTROL  -  ESC: CANCEL"
                                                  : juce::String ("MIDI MAP  -  CLICK A CONTROL  -  RIGHT-CLICK: CLEAR  -  ESC: DONE");
        g.drawText (status, bar, juce::Justification::centredLeft, true);
        if (remote.lastControl.isNotEmpty())
            g.drawText ("LAST: " + RemoteControl::describe (remote.lastControl) + "  " + juce::String (juce::roundToInt (remote.lastControlValue * 127.0f)),
                        bar, juce::Justification::centredRight, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto target = targetAt (e.getPosition());
        if (target.isEmpty())
            return;
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addSectionHeader (MidiMap::describe (target));
            menu.addItem ("Learn", [this, target] { remote.learn (target); repaint(); });
            menu.addItem ("Clear", [this, target] { remote.clearTarget (target); repaint(); });
            menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
            return;
        }
        if (remote.learning() == target)
            remote.cancelLearn();
        else
            remote.learn (target);
        repaint();
    }

    // The wheel scrolls the view under the overlay (the desk), so controls out of view can be
    // mapped; it never turns a control (a slider under the pointer is left alone).
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        lookingThrough = true;   // (hitTest lets the point through while finding what's under it)
        auto* under = root.getComponentAt (root.getLocalPoint (this, e.getPosition()));
        lookingThrough = false;
        if (under == nullptr || under == this || under == &root)
            return;
        if (auto* view = under->findParentComponentOfClass<juce::Viewport>())
        {
            // The desk scrolls itself (its strips are the view's content); any other view, by its own wheel handling.
            if (auto* content = view->getViewedComponent(); content != nullptr && dynamic_cast<MidiMap::Provider*> (content) != nullptr)
                content->mouseWheelMove (e.getEventRelativeTo (content), wheel);
            else
                view->mouseWheelMove (e.getEventRelativeTo (view), wheel);
        }
        repaint();
    }

private:
    struct Item { juce::Rectangle<int> area; juce::String target; };

    // Every showing, bindable control, in front-to-back order, where it shows on the overlay
    // (cut to what its parents let through: a strip scrolled out of view isn't drawn).
    std::vector<Item> collect() const
    {
        std::vector<Item> items;
        std::function<void (juce::Component&)> visit = [&] (juce::Component& c)
        {
            if (&c == this || ! c.isVisible())
                return;
            auto add = [&] (juce::Rectangle<int> local, const juce::String& target)
            {
                auto area = root.getLocalArea (&c, local);
                for (auto* p = &c; p != nullptr && p != &root; p = p->getParentComponent())
                    area = area.getIntersection (root.getLocalArea (p, p->getLocalBounds()));
                area = getLocalArea (&root, area).getIntersection (getLocalBounds().withTrimmedBottom (28));
                if (! area.isEmpty())
                    items.push_back ({ area, target });
            };
            if (const auto target = MidiMap::targetOf (c); target.isNotEmpty())
                add (c.getLocalBounds(), target);
            if (auto* provider = dynamic_cast<MidiMap::Provider*> (&c))   // (controls drawn, not components)
                for (auto& [local, target] : provider->midiTargets())
                    add (local, target);
            for (auto* child : c.getChildren())
                visit (*child);
        };
        visit (root);
        return items;
    }

    juce::String targetAt (juce::Point<int> p) const
    {
        juce::String found;
        for (auto& item : collect())   // the last that contains it is the front-most
            if (item.area.contains (p))
                found = item.target;
        return found;
    }

    void timerCallback() override
    {
        // Redraw while learning (the pulse) or when a mapping arrives, and follow layout changes.
        const auto now = remote.learning() + "|" + juce::String ((int) remote.getMappings().size()) + "|" + remote.lastControl
                         + juce::String (remote.lastControlValue);
        if (now != shown || remote.learning().isNotEmpty() || ++frames % 6 == 0)
        {
            shown = now;
            repaint();
        }
        if (isShowing() && juce::Component::getCurrentlyFocusedComponent() == nullptr)
            grabKeyboardFocus();   // for Esc (not taken from a field being typed in)
    }

    RemoteControl& remote;
    juce::Component& root;
    std::function<bool (juce::Point<int>)> passThrough;
    juce::String shown;
    int frames = 0;
    bool lookingThrough = false;
    const juce::Colour amber { 0xffffb030 }, cyan { 0xff50d0ff };
};

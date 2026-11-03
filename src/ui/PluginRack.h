#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../engine/AudioEngine.h"
#include "ProStrip.h"
#include "CableDraw.h"

// A channel's processing on the SELECTED page's RACK tab: 19" racks side by side (the built-in
// units' and the plugins', each running on into further racks when it's full), drawn to scale
// (the old Rack tab's design). One rack unit (U) is 1.75" and 76 px; the rails carry three
// square holes per U (EIA-310); a unit is whole U tall less a 1/32" gap, with brushed ears
// screwed through the top hole of its first U and the bottom hole of its last.
//
// The left rack starts with CHANNEL I/O (the channel's own input and its way to the fader), then
// its units; the right rack ends with ADD PLUGIN. Every built-in unit (FILTERS, GATE, COMPRESSOR,
// EQ, BUS COMP: stages of the channel's VoiceChain, ProStrip::StageUnit) is 1U; a plugin is a
// faceplate of its settings (PluginRack::Face), 1U per row of controls. Drag a unit (by its
// name) to another slot: built-in units move among the built-ins' racks, plugins among the
// plugins'. Until it's patched by hand, the sound runs down the built-ins and then the plugins. A
// plugin has LOCK (no moving or removing) and remove under its name; clicking it chooses it for
// the page's PRESETS and WINDOW.
//
// FLIP turns the racks round: every unit's back has an IN and an OUT jack (CHANNEL I/O: SOURCE
// and TO FADER). Drag from a jack to another to patch a cable; click a cable, or its plug, to pull
// it out (RE-PATCH IN ORDER, on the page, goes back to following the placement). One OUT can feed
// several INs and an IN takes several OUTs (summed): AudioEngine::RackPatch.
//
// Cables hang: each slack length is a weight on a damped spring, so it swings when it's plugged
// in, carried or its units move, and settles where gravity leaves it. A long run (across to the
// other rack, or more than a few U up or down) goes into a cable tidy beside the rack instead of
// across the units: down from its jack into the duct, along it, and out to the other jack.
class PluginRack : public juce::Component,
                   private juce::Timer
{
public:
    static constexpr float rackU = 76.0f, pxPerInch = rackU / 1.75f, panelGap = pxPerInch / 32.0f;
    static constexpr float unitHeight = rackU - panelGap;
    static constexpr int earWidth = 20, railWidth = earWidth + 3, labelWidth = 130, plateInset = Console::Space::pad,
                         rackGap = 56,      // after each rack: a cable tidy
                         margin = 8;        // the cabinet's left edge before the first rack
    // A rack's width, whatever the window's: 19" racks don't get smaller. They stand from the left;
    // more racks than the window shows scroll sideways.
    static constexpr int rackWidth = 700;

    using Unit = AudioEngine::RackUnit;
    using Patch = AudioEngine::RackPatch;

    // One unit. A plugin: `processor` (it gets a faceplate). Built in: `panel` (owned by the caller,
    // outliving this) on its faceplate, printed `name`.
    struct Mount
    {
        Unit id;
        juce::AudioProcessor* processor = nullptr;
        juce::Component* panel = nullptr;
        juce::String name;
        bool locked = false;
    };

    // `alive`: false once that plugin has gone (its faceplate stops reading it). `store`: whose
    // swinging cables and unit places these are, kept across the rack being made again: one view
    // of one channel (the same channel's rack on the desk and in the headset swing apart).
    PluginRack (Patch patchNow, std::vector<std::pair<Unit, Unit>> cablesNow, const std::vector<Mount>& mounts,
                const juce::String& channelName, const juce::String& store, std::function<bool (juce::AudioProcessor*)> alive)
        : patch (std::move (patchNow)), cables (std::move (cablesNow)), channel (channelName.toUpperCase()), storeKey (store)
    {
        // The kept swings and places pile up across a long run (every view, channel, cable and
        // unit there has been): past a few thousand they start again, which costs one settle.
        if (swayStore().size() > 4000) swayStore().clear();
        if (lastY().size() > 4000)     lastY().clear();

        for (auto& m : mounts)
        {
            auto unit = std::make_unique<UnitView>();
            unit->id = m.id;
            unit->processor = m.processor;
            unit->panel = m.panel;
            unit->locked = m.locked;
            unit->name = m.processor != nullptr ? m.processor->getName().toUpperCase() : m.name;
            if (auto* instance = dynamic_cast<juce::AudioPluginInstance*> (m.processor))
                unit->maker = instance->getPluginDescription().manufacturerName.toUpperCase();
            else if (m.processor == nullptr)
                unit->maker = "FAIDER";
            if (unit->panel != nullptr)
                addAndMakeVisible (unit->panel);
            else if (m.processor != nullptr)
            {
                unit->face = std::make_unique<Face> (*m.processor, unit->name, unit->maker, [alive, p = m.processor] { return alive (p); });
                addAndMakeVisible (*unit->face);
            }
            units.push_back (std::move (unit));
        }

        // A plugin's keys under its name (after the faces, so they sit on top).
        for (auto& u : units)
        {
            auto key = [this, id = u->id] (std::unique_ptr<juce::TextButton>& b, Console::Icon icon, const juce::String& tip, Action action, bool enabled)
            {
                b = std::make_unique<juce::TextButton>();
                ProStrip::styleKey (*b);
                b->setClickingTogglesState (false);
                Console::setIcon (*b, icon);
                b->setTooltip (tip);
                b->setEnabled (enabled);
                b->onClick = [this, id, action] { if (onAction) onAction (id, action); };
                addAndMakeVisible (*b);
            };
            if (u->processor == nullptr)   // a built-in unit: back to the channel strip, or deleted (out of the rack, switched out)
            {
                key (u->lockKey, Console::Icon::eject, "Back to the channel strip (out of the rack)", Action::toStrip, true);
                key (u->removeKey, Console::Icon::cross, "Delete from the rack (it's switched out)", Action::remove, true);
                continue;
            }
            key (u->windowKey, Console::Icon::window, "Open its own interface (the plugin's window)", Action::window, true);
            key (u->lockKey, u->locked ? Console::Icon::lockClosed : Console::Icon::lockOpen,
                 u->locked ? "Locked. Click to unlock" : "Lock in place", Action::lock, true);
            u->lockKey->setToggleState (u->locked, juce::dontSendNotification);
            key (u->removeKey, Console::Icon::cross, "Remove this plugin", Action::remove, ! u->locked);
        }

        ProStrip::styleKey (addKey);
        addKey.setClickingTogglesState (false);
        addKey.setTooltip ("Add a plugin");
        addKey.onClick = [this] { if (onAdd) onAdd (addKey); };
        addAndMakeVisible (addKey);
        ProStrip::styleKey (builtinKey);
        builtinKey.setClickingTogglesState (false);
        builtinKey.setTooltip ("Move one of the channel strip's built-in tools into the rack");
        builtinKey.onClick = [this] { if (onAddBuiltin) onAddBuiltin (builtinKey); };
        addAndMakeVisible (builtinKey);
        addMouseListener (this, true);   // clicks and drags on a unit's controls too

        // The scroll wheel moves the racks, never a unit's knob or selector (they're dragged or clicked).
        std::function<void (juce::Component&)> noWheel = [&noWheel] (juce::Component& c)
        {
            if (auto* s = dynamic_cast<juce::Slider*> (&c))   s->setScrollWheelEnabled (false);
            if (auto* b = dynamic_cast<juce::ComboBox*> (&c)) b->setScrollWheelEnabled (false);
            for (auto* child : c.getChildren())
                noWheel (*child);
        };
        noWheel (*this);
        setOpaque (true);
    }

    ~PluginRack() override
    {
        for (auto& u : units)
            if (u->panel != nullptr)
                removeChildComponent (u->panel);   // the caller deletes it
    }

    enum class Action { lock, remove, toStrip, window };   // window: the plugin's own interface
    std::function<void (Unit, Action)> onAction;             // a plugin's keys; a built-in unit's (toStrip, remove)
    std::function<void (Unit)> onSelect;                     // a plugin clicked
    std::function<void (juce::Component& near)> onAdd;       // ADD PLUGIN (near: the key, for a menu)
    std::function<void (juce::Component& near)> onAddBuiltin;   // BUILT-IN TOOL: one of the strip's moved in
    std::function<void (Patch)> onPatch;                     // a unit dragged, or a cable patched or pulled out
    void setSelected (Unit u) { selected = u; repaint(); }

    // Front (the units) or back (the jacks and cables).
    void setFlipped (bool back)
    {
        flipped = back;
        for (auto& u : units)
            for (juce::Component* c : { u->panel, (juce::Component*) u->face.get(), (juce::Component*) u->lockKey.get(), (juce::Component*) u->removeKey.get(), (juce::Component*) u->windowKey.get() })
                if (c != nullptr)
                    c->setVisible (! back);
        addKey.setVisible (! back);
        builtinKey.setVisible (! back);
        swing();
        repaint();
    }

    // How many U each rack holds (the page's height); its units carry on in another rack to the
    // right when one's full. 0: no limit (one rack each, as tall as they need).
    void setRows (int rows) { rowLimit = juce::jmax (0, rows); }

    // The width the racks need, each with its cable tidy after it.
    int widthNeeded() const { return margin + flow ([] (UnitView*, int, int, int) {}) * (rackWidth + rackGap); }

    void resized() override
    {
        ioBounds = unitBounds (0, 0, 1);
        used.clear();
        columns = flow ([this] (UnitView* u, int column, int first, int n)
        {
            if ((int) used.size() <= column)
                used.resize ((size_t) column + 1, 0);
            used[(size_t) column] = juce::jmax (used[(size_t) column], first + n);
            if (u == nullptr)   // ADD PLUGIN's panel
            {
                addBounds = unitBounds (column, first, 1);
                return;
            }
            // It glides to its slot from where it was shown (kept across the rack being made
            // again); a unit new to the rack rises into its slot.
            const auto key = storeKey + "|" + juce::String (u->id.builtin) + "@" + juce::String::toHexString ((juce::pointer_sized_int) u->id.plugin);
            u->home = unitBounds (column, first, n);
            float fromY = u->home.getY() + rackU * 0.4f;
            if (! u->bounds.isEmpty() && u->column == column)                        fromY = u->bounds.getY();
            else if (const auto it = lastY().find (key); it != lastY().end())       fromY = it->second;
            u->bounds = u->home.withY (fromY);
            lastY()[key] = u->home.getY();
            u->column = column;
            placeControls (*u);
        });
        used.resize ((size_t) columns, 0);
        used[0] = juce::jmax (used[0], 1);   // (CHANNEL I/O)
        Console::KeyRow add (addBounds.reduced ((float) earWidth, 0.0f).toNearestInt().withTrimmedLeft (labelWidth));   // where the units' controls start
        add.left (addKey, 2).left (builtinKey, 2);
        swing();   // (units moved: they glide, and their cables swing to where they hang now)
    }


    void paint (juce::Graphics& g) override
    {
        // The cabinet the racks stand in: one plain grey, lit a little from above, so nothing of the
        // desk shows round them (and no seams).
        g.setGradientFill (juce::ColourGradient (cabinetColour().brighter (0.06f), 0.0f, 0.0f, cabinetColour().darker (0.12f), 0.0f, (float) getHeight(), false));
        g.fillAll();
        for (int column = 0; column < columns; ++column)
        {
            // Each rack's opening: darker toward the back, a little light spilling in from above,
            // a bevelled edge where the cabinet's front frames it.
            const auto area = columnArea (column);
            g.setGradientFill (juce::ColourGradient (cavity().brighter (0.12f), 0.0f, 0.0f, cavity(), 0.0f, (float) getHeight(), false));
            g.fillRect (area);
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.drawRect (area.expanded (2), 2);
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.drawRect (area.expanded (3), 1);
            paintRails (g, area);
        }

        // Spare U: vented blanking panels.
        const int rows = (int) ((float) getHeight() / rackU);
        for (int column = 0; column < columns; ++column)
            for (int u = used[(size_t) column]; u < rows; ++u)
            {
                const auto blank = unitBounds (column, u, 1);
                const auto plate = blank.reduced ((float) earWidth, 0.0f);
                tile (g, flipped ? Material::rear : Material::faceplate, plate.toNearestInt());
                paintLighting (g, plate, 0.8f);
                paintBevel (g, plate);
                g.setColour (juce::Colour (0xff0b0c0d));
                for (float x = plate.getX() + 40.0f; x < plate.getRight() - 44.0f; x += 12.0f)
                    g.fillRoundedRectangle (juce::Rectangle<float> (x, plate.getY() + 22.0f, 5.0f, plate.getHeight() - 44.0f), 2.5f);
                paintEars (g, blank);
            }

        paintPlate (g, ioBounds, juce::Colour (Console::Colours::kindInput()));
        if (flipped)
        {
            paintTape (g, ioBounds.reduced ((float) earWidth, 0.0f), "CHANNEL I/O  " + channel);
            paintJack (g, jackAt (ioBounds, true), "SOURCE");
            paintJack (g, jackAt (ioBounds, false), "TO FADER");
        }
        else
        {
            auto label = ioBounds.reduced ((float) earWidth, 0.0f).toNearestInt().reduced (plateInset, Console::Space::pad);
            g.setColour (ProStrip::Colours::legend());
            g.setFont (Console::legendFont (Console::Type::name));
            g.drawFittedText ("CHANNEL I/O", label.removeFromTop (20), juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (ProStrip::Colours::legendDim());
            g.setFont (Console::legendFont (Console::Type::tag));
            g.drawFittedText (channel + ":  IN AT TOP LEFT, OUT TO THE FADER.  FLIP TO PATCH", label.removeFromTop (14), juce::Justification::centredLeft, 1, 0.8f);
        }

        for (auto& u : units)
        {
            const auto stripe = u->processor == nullptr ? ProStrip::Colours::stripe().brighter (0.2f)
                              : u->id == selected ? accent() : juce::Colour (0xff5a5f66);
            paintPlate (g, u->bounds, stripe);
            const auto plate = u->bounds.reduced ((float) earWidth, 0.0f);
            if (flipped)
            {
                paintTape (g, plate, u->name);
                paintJack (g, jackAt (u->bounds, false), "IN");
                paintJack (g, jackAt (u->bounds, true), "OUT");
            }
            else if (u->face == nullptr)   // a built-in unit: its name; its panel has the controls
            {
                auto label = plate.toNearestInt().withWidth (labelWidth).reduced (plateInset, Console::Space::pad);
                g.setColour (ProStrip::Colours::legend());
                g.setFont (Console::legendFont (Console::Type::name));
                g.drawFittedText (u->name, label.removeFromTop (20), juce::Justification::centredLeft, 1, 0.8f);
                g.setColour (ProStrip::Colours::legendDim());
                g.setFont (Console::legendFont (Console::Type::tag));
                g.drawFittedText (u->maker, label.removeFromTop (14), juce::Justification::centredLeft, 1, 0.8f);
            }
        }

        if (! flipped)
        {
            const auto plate = addBounds.reduced ((float) earWidth, 0.0f);
            tile (g, Material::faceplate, plate.toNearestInt());
            paintLighting (g, plate, 0.8f);
            paintBevel (g, plate);
            paintTape (g, plate, "INSERT");
            paintEars (g, addBounds);
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (flipped)
            paintCables (g);
        else if (dragging != nullptr)   // the unit being moved, and where it would go
        {
            const auto ghost = dragging->bounds.withPosition (dragging->bounds.getX() + (dragTo.x - dragFrom.x), dragging->bounds.getY() + (dragTo.y - dragFrom.y));
            g.setColour (accent().withAlpha (0.18f));
            g.fillRect (ghost);
            g.setColour (accent());
            g.drawRect (ghost, 2.0f);
            const auto [list, index] = dropAt (dragTo);
            const auto [column, y] = slotAt (list, index);
            const auto area = columnArea (column).toFloat();
            g.fillRect (juce::Rectangle<float> (area.getX() + (float) railWidth, y - 2.0f, area.getWidth() - 2.0f * (float) railWidth, 4.0f));
        }
    }

    // The rack listens to its own children's mouse (addMouseListener) and so hears its own clicks
    // twice: the second of a pair is let go (else pulling a cable out patches twice).
    bool heardAlready (const juce::MouseEvent& e, int kind)
    {
        const bool same = e.eventTime == lastHeard.time && kind == lastHeard.kind && e.position == lastHeard.position;
        lastHeard = { e.eventTime, kind, e.position };
        return same;
    }
    struct { juce::Time time; int kind = 0; juce::Point<float> position; } lastHeard;

    // The plug in a jack (its body hangs below the jack): the cable it's on, to pull out. Last
    // cable first: a shared jack's plugs are drawn in cable order, so that's the one on top.
    std::optional<std::pair<Unit, Unit>> plugUnder (juce::Point<float> at) const
    {
        for (auto c = cables.rbegin(); c != cables.rend(); ++c)
            for (const auto jack : { jackPoint ({ c->first, true }), jackPoint ({ c->second, false }) })
                if (juce::Rectangle<float> (jack.x - 8.0f, jack.y + 14.0f, 16.0f, plugLength - 14.0f).contains (at))   // (below the jack's own 14 px)
                    return *c;
        return std::nullopt;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (heardAlready (e, 1))
            return;
        const auto at = e.getEventRelativeTo (this).position;
        if (flipped)
        {
            if (const auto plug = plugUnder (at))   // a plug taken by its body: pulled out
            {
                auto p = manualPatch();
                p.cables.erase (std::remove (p.cables.begin(), p.cables.end(), *plug), p.cables.end());
                if (onPatch) onPatch (p);
                return;
            }
            cableFrom = jackUnder (at);
            if (cableFrom.has_value())
            {
                cableEnd = at;
                swayStore()[storeKey + "|hand"] = { wireEnd (jackPoint (*cableFrom)), {} };   // (it hangs from the plug, and swings after the hand)
                swing();
                return;
            }
            if (const auto hit = cableUnder (at))   // pulled out
            {
                auto p = manualPatch();
                p.cables.erase (std::remove (p.cables.begin(), p.cables.end(), *hit), p.cables.end());
                if (onPatch) onPatch (p);
            }
            return;
        }
        auto* u = unitUnder (at);
        if (u == nullptr)
            return;
        if (u->processor != nullptr && onSelect)
            onSelect (u->id);
        // A drag starts from the unit's name or bare faceplate, not from its controls.
        const bool onControl = e.originalComponent != this && dynamic_cast<Face*> (e.originalComponent) == nullptr;
        if (! onControl && ! u->locked)
        {
            pressed = u;
            dragFrom = dragTo = at;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto at = e.getEventRelativeTo (this).position;
        if (flipped)
        {
            if (cableFrom.has_value())
            {
                cableEnd = at;
                swing();
                repaint();
            }
            return;
        }
        if (pressed != nullptr && dragging == nullptr && at.getDistanceFrom (dragFrom) > 5.0f)
            dragging = pressed;
        if (dragging != nullptr)
        {
            dragTo = at;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (heardAlready (e, 3))
            return;
        const auto at = e.getEventRelativeTo (this).position;
        if (flipped)
        {
            if (cableFrom.has_value())
            {
                if (const auto to = jackUnder (at); to.has_value() && to->output != cableFrom->output && ! (to->unit == cableFrom->unit))
                {
                    const auto& out = cableFrom->output ? *cableFrom : *to;
                    const auto& in = cableFrom->output ? *to : *cableFrom;
                    auto p = manualPatch();
                    const std::pair<Unit, Unit> cable { out.unit, in.unit };
                    if (std::find (p.cables.begin(), p.cables.end(), cable) == p.cables.end())
                        p.cables.push_back (cable);
                    // The cable plugged in carries on swinging from where the hand let it go.
                    auto& store = swayStore();
                    if (const auto hand = store.find (storeKey + "|hand"); hand != store.end())
                        for (auto suffix : { "", "#a", "#b" })
                            store[cableKey (cable) + suffix] = hand->second;
                    if (onPatch) onPatch (p);
                }
                swayStore().erase (storeKey + "|hand");
                cableFrom.reset();
                repaint();
            }
            return;
        }
        if (dragging != nullptr)
        {
            const auto [column, index] = dropAt (at);
            auto p = patch;
            for (auto* list : { &p.left, &p.right })
                list->erase (std::remove (list->begin(), list->end(), dragging->id), list->end());
            auto& into = column == 0 ? p.left : p.right;
            // (the slot counted in the lists as they were; the unit's own place no longer counts)
            int slot = 0, seen = 0;
            for (auto& id : column == 0 ? patch.left : patch.right)
            {
                if (seen++ >= index)
                    break;
                if (! (id == dragging->id))
                    ++slot;
            }
            into.insert (into.begin() + juce::jlimit (0, (int) into.size(), slot), dragging->id);
            dragging = nullptr;
            repaint();
            if (onPatch) onPatch (p);
        }
        pressed = nullptr;
    }

    // The faceplate: the plugin's settings as console controls, a 1U row at a time. The name and
    // maker are printed at the left of the first row.
    class Face : public juce::Component, private juce::Timer
    {
    public:
        Face (juce::AudioProcessor& p, juce::String pluginName, juce::String pluginMaker, std::function<bool()> isAlive)
            : alive (std::move (isAlive)), title (std::move (pluginName)), maker (std::move (pluginMaker))
        {
            for (auto* param : p.getParameters())
            {
                // Hidden settings, and the stand-ins VST3 plugins list for MIDI controllers
                // ("MIDI CC 7"...), aren't controls of the plugin.
                const auto fullName = param->getName (100).trim();
                if (! param->isAutomatable() || fullName.isEmpty() || fullName.startsWithIgnoreCase ("MIDI"))
                    continue;

                auto control = std::make_unique<Control>();
                control->param = param;
                const auto values = param->getAllValueStrings();
                const auto label = param->getName (24).toUpperCase();
                if (param->isBoolean() || param->getNumSteps() == 2)
                {
                    control->key = std::make_unique<juce::TextButton> (label);
                    ProStrip::styleKey (*control->key);
                    control->key->setTooltip (fullName);
                    control->key->onClick = [param, k = control->key.get()]
                    {
                        param->beginChangeGesture();
                        param->setValueNotifyingHost (k->getToggleState() ? 1.0f : 0.0f);
                        param->endChangeGesture();
                    };
                    addAndMakeVisible (*control->key);
                }
                else if (param->isDiscrete() && values.size() >= 3 && values.size() <= 24)
                {
                    control->choice = std::make_unique<juce::ComboBox>();
                    for (int i = 0; i < values.size(); ++i)
                        control->choice->addItem (values[i], i + 1);
                    control->choice->setTooltip (fullName);
                    control->choice->onChange = [param, c = control->choice.get(), n = values.size()]
                    {
                        if (c->getSelectedId() <= 0)
                            return;
                        param->beginChangeGesture();
                        param->setValueNotifyingHost ((float) (c->getSelectedId() - 1) / (float) (n - 1));
                        param->endChangeGesture();
                    };
                    control->label = label;
                    addAndMakeVisible (*control->choice);
                }
                else
                {
                    control->knob = std::make_unique<ProStrip::Knob> (ProStrip::Colours::grey(), label);
                    auto& k = *control->knob;
                    k.setRange (0.0, 1.0);
                    k.setDoubleClickReturnValue (true, param->getDefaultValue());
                    k.setTooltip (fullName);
                    k.textFromValueFunction = [param] (double v)
                    {
                        const auto unit = param->getLabel().trim();
                        return param->getText ((float) v, 12) + (unit.isEmpty() ? juce::String() : " " + unit);
                    };
                    k.onDragStart = [param] { param->beginChangeGesture(); };
                    k.onDragEnd = [param] { param->endChangeGesture(); };
                    k.onValueChange = [param, &k] { if (! juce::approximatelyEqual ((float) k.getValue(), param->getValue())) param->setValueNotifyingHost ((float) k.getValue()); };
                    addAndMakeVisible (k);
                }
                controls.push_back (std::move (control));
            }
            sync();
            startTimerHz (15);
        }

        // How many 1U rows the controls take at this width.
        int rowsFor (int width) const
        {
            int rows = 1;
            layout (width, [&rows] (juce::Component*, juce::Rectangle<int>, int row) { rows = juce::jmax (rows, row + 1); });
            return rows;
        }

        void resized() override
        {
            layout (getWidth(), [] (juce::Component* c, juce::Rectangle<int> r, int) { c->setBounds (r); });
        }

        void paint (juce::Graphics& g) override
        {
            // Silkscreen: the name and maker at the left of the first row.
            auto label = juce::Rectangle<int> (plateInset, 0, labelWidth - plateInset - Console::Space::gap, (int) unitHeight).reduced (0, Console::Space::pad);
            g.setColour (ProStrip::Colours::legend());
            g.setFont (Console::legendFont (Console::Type::name));
            g.drawFittedText (title, label.removeFromTop (20), juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (ProStrip::Colours::legendDim());
            g.setFont (Console::legendFont (Console::Type::tag));
            g.drawFittedText (maker, label.removeFromTop (14), juce::Justification::centredLeft, 1, 0.8f);
            if (controls.empty())
                g.drawText ("NO SETTINGS HERE: USE ITS WINDOW", getLocalBounds().withTrimmedLeft (labelWidth).withHeight ((int) unitHeight),
                            juce::Justification::centredLeft, true);
            for (auto& c : controls)
                if (c->choice != nullptr)
                    g.drawText (c->label, c->choice->getBounds().translated (0, -16).withHeight (14), juce::Justification::centredLeft, true);
        }

    private:
        struct Control
        {
            juce::AudioProcessorParameter* param = nullptr;
            std::unique_ptr<ProStrip::Knob> knob;
            std::unique_ptr<juce::TextButton> key;
            std::unique_ptr<juce::ComboBox> choice;
            juce::String label;
        };

        // Keys, then knobs, then selectors, left to right from after the name, a new 1U row
        // when one is full. Each sits centred in its row's height.
        template <typename Place>
        void layout (int width, Place&& place) const
        {
            int x = labelWidth, row = 0;
            const int right = width - plateInset;
            auto put = [&] (juce::Component* c, int w, int h, int dy)
            {
                if (x + w > right && x > labelWidth)
                {
                    x = labelWidth;
                    ++row;
                }
                const int top = (int) ((float) row * rackU + (unitHeight - (float) h) * 0.5f) + dy;
                place (c, juce::Rectangle<int> (x, top, w, h), row);
                x += w + Console::Space::gap;
            };
            for (auto& c : controls) if (c->key != nullptr)    put (c->key.get(), 96, Console::Space::key, 0);
            for (auto& c : controls) if (c->knob != nullptr)   put (c->knob.get(), 66, (int) unitHeight - Console::Space::gap, 0);
            for (auto& c : controls) if (c->choice != nullptr) put (c->choice.get(), 150, Console::Space::key, Console::Space::gap);
        }

        // Follow the plugin: its own panel, presets, automation.
        void sync()
        {
            for (auto& c : controls)
            {
                const float v = c->param->getValue();
                if (c->knob != nullptr && ! c->knob->isMouseButtonDown() && ! juce::approximatelyEqual ((float) c->knob->getValue(), v))
                    c->knob->setValue (v, juce::dontSendNotification);
                if (c->key != nullptr && c->key->getToggleState() != (v >= 0.5f))
                    c->key->setToggleState (v >= 0.5f, juce::dontSendNotification);
                if (c->choice != nullptr)
                {
                    const int n = c->choice->getNumItems();
                    const int id = n > 1 ? juce::roundToInt (v * (float) (n - 1)) + 1 : 1;
                    if (c->choice->getSelectedId() != id)
                        c->choice->setSelectedId (id, juce::dontSendNotification);
                }
            }
        }

        void timerCallback() override
        {
            if (alive != nullptr && ! alive())
                return stopTimer();
            sync();
        }

        std::function<bool()> alive;
        juce::String title, maker;
        std::vector<std::unique_ptr<Control>> controls;
    };

private:
    struct UnitView
    {
        Unit id;
        juce::AudioProcessor* processor = nullptr;   // none: built in
        juce::Component* panel = nullptr;            // built in: its controls
        std::unique_ptr<Face> face;                  // a plugin's faceplate
        bool locked = false;
        std::unique_ptr<juce::TextButton> lockKey, removeKey, windowKey;   // a plugin's (window: plugins only)
        juce::Rectangle<float> bounds;               // where it's shown (gliding to home)
        juce::Rectangle<float> home;                 // its slot
        int column = 0;
        juce::String name, maker;
    };

    struct Jack { Unit unit; bool output = false; };   // a jack on the back: a unit's IN or OUT (CHANNEL I/O: SOURCE, TO FADER)

    // A unit's controls on its plate where it's shown now.
    void placeControls (UnitView& u)
    {
        const auto plate = u.bounds.reduced ((float) earWidth, 0.0f).toNearestInt();
        if (u.panel != nullptr)
            u.panel->setBounds (plate.withTrimmedLeft (labelWidth));
        else if (u.face != nullptr)
            u.face->setBounds (plate);
        if (u.lockKey != nullptr)   // under the name, along the bottom of its first U
        {
            auto row = juce::Rectangle<int> (plate.getX() + plateInset, (int) (u.bounds.getY() + unitHeight) - Console::Space::keySmall - Console::Space::gap,
                                             labelWidth - plateInset - Console::Space::gap, Console::Space::keySmall);
            const int keys = u.windowKey != nullptr ? 3 : 2;
            const int w = (row.getWidth() - (keys - 1) * Console::Space::unit) / keys;
            if (u.windowKey != nullptr)
            {
                u.windowKey->setBounds (row.removeFromLeft (w));
                row.removeFromLeft (Console::Space::unit);
            }
            u.lockKey->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (Console::Space::unit);
            u.removeKey->setBounds (row);
        }
    }

    // Where each unit was last laid out, by channel and unit: a rack made again glides from there.
    static std::map<juce::String, float>& lastY() { static std::map<juce::String, float> store; return store; }

    // The cabinet's materials, in the console's colours.
    static juce::Colour cavity()          { return Theme::get().rim.darker (0.6f); }
    static juce::Colour cabinetColour()   { return Theme::get().background; }
    static juce::Colour faceplateColour() { return Theme::get().panel; }
    static juce::Colour rearColour()      { return Theme::get().panelDark.darker (0.25f); }
    static juce::Colour earColour()       { return Theme::get().plateColour; }
    static juce::Colour railColour()      { return Theme::get().namePlate.darker (0.2f); }
    static juce::Colour accent()          { return Console::Colours::selected(); }

    enum class Material { faceplate, rear, ear, rail };

    // One swatch per material and pixel density, made on first use and tiled, so the grain stays
    // sharp at any zoom.
    const juce::Image& texture (Material m, float scale) const
    {
        static std::map<std::pair<int, int>, juce::Image> cache;
        auto& img = cache[{ (int) m, juce::roundToInt (scale * 100.0f) }];
        if (img.isNull())
        {
            auto px = [scale] (float units) { return juce::roundToInt (units * scale); };
            switch (m)
            {
                case Material::faceplate: img = Materials::makeMatte (px (1024.0f), px (unitHeight), faceplateColour(), 11); break;
                case Material::rear:      img = Materials::makeMatte (px (1024.0f), px (unitHeight), rearColour(), 23); break;
                case Material::ear:       img = Materials::makeBrushed (px ((float) earWidth), px (unitHeight), earColour(), true, 37); break;
                case Material::rail:      img = Materials::makeBrushed (px (14.0f), px (1024.0f), railColour(), true, 41); break;
            }
        }
        return img;
    }

    void tile (juce::Graphics& g, Material m, juce::Rectangle<int> r) const
    {
        const float scale = Materials::pixelScale (g);
        Materials::tile (g, texture (m, scale), r, scale);
    }

    // The racks from the left, each with its cable tidy after it.
    juce::Rectangle<int> columnArea (int column) const
    {
        return { margin + column * (rackWidth + rackGap), 0, rackWidth, getHeight() };
    }

    int columnAt (float x) const { return juce::jmax (0, (int) ((x - (float) margin) / (float) (rackWidth + rackGap))); }

    // Lays the units out, left rack first: CHANNEL I/O, the built-in units, then (in a rack of
    // their own) the plugins and ADD PLUGIN. A rack that's full (rowLimit) carries on in a new one
    // to the right. `place` (unit, or null for ADD PLUGIN; rack, first U, U). Returns the racks.
    template <typename Place>
    int flow (Place&& place) const
    {
        const int limit = rowLimit > 0 ? rowLimit : std::numeric_limits<int>::max();
        int column = 0, at = 1;   // (U 0 of the first rack: CHANNEL I/O)
        auto put = [&] (UnitView* u, int n)
        {
            if (at > 0 && at + n > limit)
            {
                ++column;
                at = 0;
            }
            place (u, column, at, n);
            at += n;
        };
        for (auto& id : patch.left)
            if (auto* u = find (id))
                put (u, unitsOf (*u, rackWidth));
        ++column;   // the plugins in a rack of their own
        at = 0;
        for (auto& id : patch.right)
            if (auto* u = find (id))
                put (u, unitsOf (*u, rackWidth));
        put (nullptr, 1);   // ADD PLUGIN
        return column + 1;
    }

    // `units` U from U number `first` (0: the top) in a rack, ear to ear across its rails.
    juce::Rectangle<float> unitBounds (int column, int first, int units) const
    {
        const auto area = columnArea (column).toFloat();
        return { area.getX() + 2.0f, (float) first * rackU + panelGap * 0.5f, area.getWidth() - 4.0f, (float) units * rackU - panelGap };
    }

    int unitsOf (const UnitView& u, int columnW) const
    {
        return u.face != nullptr ? u.face->rowsFor (columnW - 4 - 2 * earWidth) : 1;
    }

    UnitView* find (const Unit& id) const
    {
        for (auto& u : units)
            if (u->id == id)
                return u.get();
        return nullptr;
    }

    UnitView* unitUnder (juce::Point<float> at) const
    {
        for (auto& u : units)
            if (u->bounds.contains (at))
                return u.get();
        return nullptr;
    }

    // Where a dragged unit would land: its list (0: the built-in units, 1: the plugins) and a
    // place in it, by the rack and height the pointer's at.
    std::pair<int, int> dropAt (juce::Point<float> at) const
    {
        const int list = dragging != nullptr && dragging->processor != nullptr ? 1 : 0;
        const int column = columnAt (at.x);
        int index = 0;
        for (auto& id : list == 0 ? patch.left : patch.right)
            if (auto* u = find (id); u != nullptr && (u->column < column || (u->column == column && u->bounds.getCentreY() < at.y)))
                ++index;
        return { list, index };
    }

    // The rack and height of the `index`th slot in a list (before the unit there, or after the last).
    std::pair<int, float> slotAt (int list, int index) const
    {
        std::pair<int, float> slot { list == 0 ? 0 : 1, list == 0 ? rackU : 0.0f };
        int i = 0;
        for (auto& id : list == 0 ? patch.left : patch.right)
            if (auto* u = find (id))
            {
                if (i++ == index)
                    return { u->column, u->home.getY() - panelGap * 0.5f };
                slot = { u->column, u->home.getBottom() + panelGap * 0.5f };
            }
        return slot;
    }

    // A unit's jacks on its back, in its first U: IN, then OUT.
    static juce::Point<float> jackAt (juce::Rectangle<float> unit, bool output)
    {
        const auto plate = unit.reduced ((float) earWidth, 0.0f);
        return { plate.getRight() - (output ? 46.0f : 116.0f), unit.getY() + unitHeight * 0.42f };
    }

    juce::Point<float> jackPoint (const Jack& j) const
    {
        if (j.unit.builtin == AudioEngine::rackIn)  return jackAt (ioBounds, true);
        if (j.unit.builtin == AudioEngine::rackOut) return jackAt (ioBounds, false);
        if (auto* u = find (j.unit))                return jackAt (u->bounds, j.output);
        return {};
    }

    std::optional<Jack> jackUnder (juce::Point<float> at) const
    {
        std::vector<Jack> jacks { { { AudioEngine::rackIn, nullptr }, true }, { { AudioEngine::rackOut, nullptr }, false } };
        for (auto& u : units)
        {
            jacks.push_back ({ u->id, false });
            jacks.push_back ({ u->id, true });
        }
        for (auto& j : jacks)
            if (jackPoint (j).getDistanceFrom (at) < 14.0f)
                return j;
        return std::nullopt;
    }

    //==========================================================================
    // Hanging cables. A cable's slack lengths each have a weight (Sway) on a damped spring toward
    // where gravity leaves it (CableDraw::restingSag). The weights are kept by cable across the
    // rack being made again (patching makes it again), so a cable just plugged in carries on
    // swinging from where it was let go.
    struct Sway { juce::Point<float> pos, vel; };
    static std::map<juce::String, Sway>& swayStore() { static std::map<juce::String, Sway> store; return store; }

    // A cable's way: straight between its jacks, or (`tidy`) from `a` down into a duct at `duct`
    // (x), along it, and out to `b`.
    struct Route
    {
        juce::Point<float> a, b;
        bool tidy = false;
        float duct = 0.0f;
        juce::Point<float> inA() const { return { duct, a.y + 30.0f }; }   // where it goes into the duct
        juce::Point<float> inB() const { return { duct, b.y + 30.0f }; }   // and comes out
    };

    float ductX (int column) const { return (float) columnArea (column).getRight() + (float) rackGap * 0.5f; }   // the tidy after a rack

    // A plug stands straight down from its jack; its wire leaves the foot of the boot, there.
    static constexpr float plugLength = 40.0f;
    static juce::Point<float> wireEnd (juce::Point<float> jack) { return jack.translated (0.0f, plugLength); }

    // A long run goes in a tidy: across to another rack, or more than three U up or down. It
    // takes the tidy after the leftmost rack it reaches, in a lane of its own.
    Route routeOf (juce::Point<float> a, juce::Point<float> b, int lane) const
    {
        Route r { a, b };
        const int ca = columnAt (a.x), cb = columnAt (b.x);
        r.tidy = ca != cb || std::abs (a.y - b.y) > 3.0f * rackU;
        r.duct = ductX (juce::jmin (ca, cb)) + (float) ((lane % 5) - 2) * 6.0f;
        return r;
    }

    juce::String cableKey (const std::pair<Unit, Unit>& c) const
    {
        auto unit = [] (const Unit& u) { return juce::String (u.builtin) + "@" + juce::String::toHexString ((juce::pointer_sized_int) u.plugin); };
        return storeKey + "|" + unit (c.first) + ">" + unit (c.second);
    }

    // Each cable's slack lengths: (from, to, its weight's key).
    struct Slack { juce::Point<float> from, to; juce::String key; };
    std::vector<Slack> slackLengths() const
    {
        std::vector<Slack> list;
        int lane = 0;
        for (auto& c : cables)
        {
            const auto r = routeOf (wireEnd (jackPoint ({ c.first, true })), wireEnd (jackPoint ({ c.second, false })), lane);
            const auto key = cableKey (c);
            if (r.tidy)
            {
                ++lane;
                list.push_back ({ r.a, r.inA(), key + "#a" });
                list.push_back ({ r.inB(), r.b, key + "#b" });
            }
            else
                list.push_back ({ r.a, r.b, key });
        }
        if (cableFrom.has_value())
            list.push_back ({ wireEnd (jackPoint (*cableFrom)), wireEnd (cableEnd), storeKey + "|hand" });
        return list;
    }

    static juce::Point<float> restOf (const Slack& s) { return CableDraw::restingSag (s.from, s.to, 0.3f); }

    // A slack length's weight now (where it rests, the first time it's seen).
    juce::Point<float> weightOf (const Slack& s) const
    {
        const auto it = swayStore().find (s.key);
        return it != swayStore().end() ? it->second.pos : restOf (s);
    }

    void timerCallback() override
    {
        bool moving = false;
        // Units gliding to their slots: a quarter of the way each frame, settling.
        for (auto& u : units)
            if (! u->home.isEmpty() && u->bounds.getY() != u->home.getY())
            {
                const float y = u->bounds.getY() + (u->home.getY() - u->bounds.getY()) * 0.25f;
                u->bounds = u->home.withY (std::abs (y - u->home.getY()) < 0.5f ? u->home.getY() : y);
                placeControls (*u);
                moving = true;
            }
        if (flipped)
            for (auto& s : slackLengths())
            {
                auto [it, added] = swayStore().try_emplace (s.key, Sway { restOf (s), {} });
                moving = CableDraw::stepSway (it->second.pos, it->second.vel, restOf (s), 1.0f / 60.0f) || moving;
            }
        if (moving || cableFrom.has_value())
            repaint();
        else
            stopTimer();
    }
    void swing() { if (! isTimerRunning()) startTimerHz (60); }

    // A cable's whole path: its slack lengths hanging from their weights, and its run in a duct.
    juce::Path pathOf (const std::pair<Unit, Unit>& c, int lane) const
    {
        const auto r = routeOf (wireEnd (jackPoint ({ c.first, true })), wireEnd (jackPoint ({ c.second, false })), lane);
        const auto key = cableKey (c);
        if (! r.tidy)
            return CableDraw::hangingPath (r.a, r.b, weightOf ({ r.a, r.b, key }));
        auto p = CableDraw::hangingPath (r.a, r.inA(), weightOf ({ r.a, r.inA(), key + "#a" }));
        p.lineTo (r.inB());
        p.addPath (CableDraw::hangingPath (r.inB(), r.b, weightOf ({ r.inB(), r.b, key + "#b" })));
        return p;
    }

    std::optional<std::pair<Unit, Unit>> cableUnder (juce::Point<float> at) const
    {
        int lane = 0;
        for (auto& c : cables)
        {
            const bool tidy = routeOf (wireEnd (jackPoint ({ c.first, true })), wireEnd (jackPoint ({ c.second, false })), lane).tidy;
            juce::Point<float> nearest;
            pathOf (c, lane).getNearestPoint (at, nearest);
            lane += tidy ? 1 : 0;
            if (nearest.getDistanceFrom (at) < 6.0f)
                return c;
        }
        return std::nullopt;
    }

    // A cable tidy down beside a rack: a slotted duct with fingers each side, the cables laid in it.
    void paintDuct (juce::Graphics& g, float x, bool front) const
    {
        const float w = (float) rackGap - 14.0f, top = 4.0f, bottom = (float) getHeight() - 4.0f;
        const auto body = juce::Rectangle<float> (x - w * 0.5f, top, w, bottom - top);
        if (! front)   // its channel, under the cables
        {
            g.setColour (juce::Colour (0xff0e0f11));
            g.fillRoundedRectangle (body, 3.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawRoundedRectangle (body, 3.0f, 1.5f);
            return;
        }
        // Its fingers each side and a tie across every so often, over the cables.
        for (float y = top + 10.0f; y < bottom - 10.0f; y += 22.0f)
            for (float side : { body.getX(), body.getRight() - 6.0f })
            {
                const auto finger = juce::Rectangle<float> (side, y, 6.0f, 14.0f);
                g.setColour (juce::Colour (0xff3b3f45));
                g.fillRoundedRectangle (finger, 1.5f);
                g.setColour (juce::Colours::white.withAlpha (0.10f));
                g.fillRect (finger.withHeight (1.5f));
            }
        for (float y = top + 60.0f; y < bottom - 20.0f; y += 150.0f)
        {
            const auto tie = juce::Rectangle<float> (body.getX() + 4.0f, y, body.getWidth() - 8.0f, 5.0f);
            g.setColour (juce::Colour (0xff1c1d20));
            g.fillRoundedRectangle (tie, 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRect (tie.withHeight (1.0f));
        }
    }

    // The patch, made manual: the cables as they are now become its own.
    Patch manualPatch() const
    {
        auto p = patch;
        if (! p.manual)
        {
            p.manual = true;
            p.cables = cables;
        }
        return p;
    }

    juce::Colour unitColour (const Unit& u) const
    {
        if (u.builtin == AudioEngine::rackIn || u.builtin == AudioEngine::rackOut) return Console::Colours::kindInput();
        if (u.plugin == nullptr)                                                  return ProStrip::Colours::stripe().brighter (0.3f);
        return accent();
    }

    void paintCables (juce::Graphics& g) const
    {
        for (int d = 0; d < columns; ++d)
            paintDuct (g, ductX (d), false);
        // A plug at each end of a cable, standing straight down from its jack.
        struct Plug { juce::Point<float> tip; juce::Colour colour; };
        std::vector<Plug> plugs;
        auto plugsOf = [&plugs] (juce::Point<float> from, juce::Point<float> to, juce::Colour a, juce::Colour b)
        {
            plugs.push_back ({ from, a });
            plugs.push_back ({ to, b });
        };

        int lane = 0;
        for (auto& c : cables)
        {
            const auto from = jackPoint ({ c.first, true }), to = jackPoint ({ c.second, false });
            const bool tidy = routeOf (wireEnd (from), wireEnd (to), lane).tidy;
            const auto path = pathOf (c, lane);
            CableDraw::drawFlow (g, path, wireEnd (from), wireEnd (to), unitColour (c.first), unitColour (c.second), 1.0f, 1.0f, ! patch.manual, 3.0f, true);
            plugsOf (from, to, unitColour (c.first), unitColour (c.second));
            lane += tidy ? 1 : 0;
        }
        for (int d = 0; d < columns; ++d)
            paintDuct (g, ductX (d), true);
        for (auto& p : plugs)   // over the ducts: they're in the jacks, on the units
            CableDraw::drawPlug (g, p.tip, p.tip.translated (0.0f, plugLength), p.colour);
        if (cableFrom.has_value())   // in the hand: over everything, swinging after the pointer, a plug on each end
        {
            const auto from = jackPoint (*cableFrom);
            const Slack hand { wireEnd (from), wireEnd (cableEnd), storeKey + "|hand" };
            const auto path = CableDraw::hangingPath (hand.from, hand.to, weightOf (hand));
            const auto colour = unitColour (cableFrom->unit);
            CableDraw::drawFlow (g, path, hand.from, hand.to, colour, colour, 1.0f, 0.8f, false, 3.0f, true);
            for (auto tip : { from, cableEnd })
                CableDraw::drawPlug (g, tip, tip.translated (0.0f, plugLength), colour);
        }
    }

    // A unit's plate across the rails: faceplate (or its back), the anodised stripe, the ears.
    void paintPlate (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour stripe) const
    {
        const auto plate = bounds.reduced ((float) earWidth, 0.0f);
        tile (g, flipped ? Material::rear : Material::faceplate, plate.toNearestInt());
        paintLighting (g, plate, 1.0f);
        paintBevel (g, plate);
        if (! flipped)
        {
            const auto s = plate.withWidth (5.0f);
            g.setGradientFill (juce::ColourGradient (stripe.withMultipliedAlpha (0.9f), s.getTopLeft(), stripe.darker (0.6f), s.getBottomLeft(), false));
            g.fillRect (s);
        }
        paintEars (g, bounds);
    }

    // A panel-mount 1/4" jack: a hex nut on its bushing and the bore, labelled above it.
    static void paintJack (juce::Graphics& g, juce::Point<float> c, const juce::String& label)
    {
        juce::Path nut;
        nut.addPolygon (c, 6, 10.5f, juce::MathConstants<float>::pi / 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillPath (nut, juce::AffineTransform::translation (0.6f, 1.4f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd3d7dc), c.translated (-9.0f, -9.0f), juce::Colour (0xff50555c), c.translated (9.0f, 9.0f), false));
        g.fillPath (nut);
        const auto bushing = juce::Rectangle<float> (14.0f, 14.0f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5f646b), bushing.getTopLeft(), juce::Colour (0xffc6cbd1), bushing.getBottomRight(), false));
        g.fillEllipse (bushing);
        g.setColour (juce::Colour (0xff050505));
        g.fillEllipse (juce::Rectangle<float> (8.5f, 8.5f).withCentre (c));
        g.setColour (ProStrip::Colours::legend());
        g.setFont (Console::legendFont (Console::Type::tag));
        // (above it: a plug in it hangs down over where it would be under)
        g.drawText (label, juce::Rectangle<float> (70.0f, 12.0f).withCentre (c.translated (0.0f, -19.0f)), juce::Justification::centred, false);
    }

    // Light from above: a soft sheen at the top, falling off toward the bottom.
    static void paintLighting (juce::Graphics& g, juce::Rectangle<float> r, float strength)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f * strength), r.getTopLeft(),
                                                 juce::Colours::black.withAlpha (0.22f * strength), r.getBottomLeft(), false));
        g.fillRect (r);
    }

    // A panel's edge: light catching the top, shadow under the bottom.
    static void paintBevel (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (juce::Colours::white.withAlpha (0.13f));
        g.fillRect (r.withHeight (1.0f));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (r.withTop (r.getBottom() - 1.0f));
    }

    // Phillips pan-head screw, its slot turned however it was last done up.
    static void paintScrew (juce::Graphics& g, juce::Point<float> c)
    {
        const float r = 4.6f;
        const float angle = (float) ((int) (c.x * 7.0f + c.y * 13.0f) % 90) * juce::MathConstants<float>::pi / 180.0f;
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (juce::Rectangle<float> (2.0f * r + 1.5f, 2.0f * r + 1.5f).withCentre (c.translated (0.4f, 0.9f)));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc9ced4), c.translated (-r, -r), juce::Colour (0xff4b5057), c.translated (r, r), false));
        g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c));
        g.setColour (juce::Colour (0xff23262a));
        for (auto a : { angle, angle + juce::MathConstants<float>::halfPi })
        {
            const juce::Point<float> d (std::cos (a) * r * 0.62f, std::sin (a) * r * 0.62f);
            g.drawLine ({ c - d, c + d }, 1.3f);
        }
    }

    // Ears: brushed aluminium tabs either side of a unit, screwed to the rails through the top hole
    // of its first U and the bottom hole of its last (0.25" in from each end of its U span).
    void paintEars (juce::Graphics& g, juce::Rectangle<float> u) const
    {
        constexpr float holeIn = 0.25f * pxPerInch, half = panelGap * 0.5f;
        for (auto ear : { u.withWidth ((float) earWidth), u.withLeft (u.getRight() - (float) earWidth) })
        {
            g.setColour (earColour());   // solid metal: the rail's holes behind don't show through
            g.fillRect (ear);
            tile (g, Material::ear, ear.toNearestInt());
            paintLighting (g, ear, 1.4f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.drawRect (ear, 1.0f);
            paintScrew (g, { ear.getCentreX(), ear.getY() - half + holeIn });
            paintScrew (g, { ear.getCentreX(), ear.getBottom() + half - holeIn });
        }
    }

    // A rack's folded steel rails, three square holes to every U; the units' ears cover them, a
    // sliver of rail showing either side.
    void paintRails (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        for (int x : { area.getX(), area.getRight() - railWidth })
        {
            const juce::Rectangle<int> railArea (x, 0, railWidth, getHeight());
            tile (g, Material::rail, railArea);
            const auto r = railArea.toFloat();
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.12f), r.getTopLeft(), juce::Colours::black.withAlpha (0.35f), r.getTopRight(), false));
            g.fillRect (r);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawVerticalLine (x == area.getX() ? x + railWidth - 1 : x, 0.0f, (float) getHeight());
            for (float u = 0.0f; u < (float) getHeight(); u += rackU)
                for (float inches : { 0.25f, 0.875f, 1.5f })
                {
                    const auto hole = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getCentreX(), u + inches * pxPerInch });
                    g.setColour (juce::Colour (0xff040405));
                    g.fillRect (hole);
                    g.setColour (juce::Colours::black.withAlpha (0.7f));
                    g.fillRect (hole.withHeight (1.5f));
                    g.setColour (juce::Colours::white.withAlpha (0.22f));
                    g.fillRect (hole.withTop (hole.getBottom()).withHeight (1.0f));
                }
        }
    }

    // A strip of white tape stuck on a panel, written on (the old rack's labels).
    static void paintTape (juce::Graphics& g, juce::Rectangle<float> plate, const juce::String& text)
    {
        g.setFont (Console::legendFont (Console::Type::legend));
        const float w = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 18.0f;
        const auto tape = juce::Rectangle<float> (plate.getX() + 14.0f, plate.getY() + unitHeight * 0.5f - 8.0f, w, 16.0f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (tape.translated (0.5f, 1.0f));
        g.setColour (juce::Colour (0xffd9d4c4));
        g.fillRect (tape);
        g.setColour (juce::Colour (0xff23211c));
        g.drawText (text, tape, juce::Justification::centred, false);
    }

    Patch patch;                                   // placement (and cables, when manual)
    std::vector<std::pair<Unit, Unit>> cables;     // what's patched now (manual or following placement)
    juce::String channel, storeKey;
    std::vector<std::unique_ptr<UnitView>> units;
    juce::TextButton addKey { "+  ADD PLUGIN" }, builtinKey { "+  BUILT-IN TOOL" };
    juce::Rectangle<float> ioBounds, addBounds;
    std::vector<int> used { 1 };                   // U taken in each rack (CHANNEL I/O in the first)
    int columns = 2, rowLimit = 0;                 // racks laid out; U per rack (setRows; 0: no limit)
    bool flipped = false;
    Unit selected;                                 // the plugin PRESETS and WINDOW act on
    UnitView* pressed = nullptr;                   // front: a unit pressed (a drag may start)
    UnitView* dragging = nullptr;                  // front: the unit being moved
    juce::Point<float> dragFrom, dragTo;
    std::optional<Jack> cableFrom;                 // back: a cable being drawn from this jack
    juce::Point<float> cableEnd;
};

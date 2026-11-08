// FAIDER: the console. It draws channel strips from layout files (console/layout/*.json:
// every part's place, in a 214-wide strip's pixels), redrawn whenever a file changes. Everything
// is drawn in code (LargeFormat.h).
//
// Layout keys: Ctrl+Shift+O outlines every part (to check the layout), Ctrl+Shift+R
// reloads; with no engine, 1-8 channels across. Knobs and faders drag, keys click.

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../src/ui/Theme.h"
#include "../../src/ui/LargeFormat.h"
#include "../../src/ui/DotMatrix.h"
#include "../../src/engine/AudioEngine.h"
#include "../../src/engine/AppCapture.h"
#include "Simple.h"
#include "DeskOrder.h"
#include "../../src/ui/MixerComponent.h"
#include "../../src/ui/SettingsTab.h"   // (DeskSettings: the cards)
#include "../../src/ui/MidiMap.h"
#include "Console.h"

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
        int litCount = -1;
        juce::StringArray marks;
        juce::Rectangle<float> r;
        float size = 52.0f, textSize = 15.0f;
        int count = 5;
        juce::StringArray colours;
        juce::Path line;
    };
    struct Section { juce::String name; float height = 0; juce::String panel; std::vector<Item> items; };
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
            Section section { s["name"].toString(), num (s, "height"), s["panel"].toString(), {} };
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

// What each part drives: a channel's setting in the engine (its processing's parameters, or the
// strip's level, pan, solo and cut), or, with no engine, a value kept here so the look can still be
// judged. Knobs and faders read and set 0..1 along their travel.
class Binding
{
public:
    explicit Binding (AudioEngine* e) : engine (e) {}

    // The strips across the desk: the channels, then the buses (in the desk's order: DeskOrder.h).
    AudioEngine::Strip* strip (int ch) const
    {
        if (engine == nullptr)
            return nullptr;
        auto& strips = engine->getStrips();
        auto& buses = engine->getBuses();
        if (juce::isPositiveAndBelow (ch, (int) strips.size()))
            return strips[(size_t) ch].get();
        ch = deskOrder.bus (ch - (int) strips.size());
        return juce::isPositiveAndBelow (ch, (int) buses.size()) ? buses[(size_t) ch].get() : nullptr;
    }
    DeskOrder deskOrder;
    void syncOrder() { if (engine != nullptr) deskOrder.sync (*engine); }
    // A bus's or output's strip, from its engine index.
    int busStrip (int index) const    { return (int) engine->getStrips().size() + deskOrder.busPlace (index); }
    int outputStrip (int index) const { return (int) (engine->getStrips().size() + engine->getBuses().size()) + deskOrder.outputPlace (index); }
    int channels() const
    {
        return engine != nullptr ? (int) (engine->getStrips().size() + engine->getBuses().size() + engine->getMasters().size()) : 8;
    }
    bool isBus (int ch) const { return engine != nullptr && ch >= (int) engine->getStrips().size() && ! isMaster (ch); }
    // THE OUTPUTS: the masters, after the buses.
    bool isMaster (int ch) const { return engine != nullptr && ch >= (int) (engine->getStrips().size() + engine->getBuses().size()); }
    AudioEngine::Master* master (int ch) const
    {
        if (! isMaster (ch))
            return nullptr;
        const int m = deskOrder.output (ch - (int) (engine->getStrips().size() + engine->getBuses().size()));
        return juce::isPositiveAndBelow (m, (int) engine->getMasters().size()) ? engine->getMasters()[(size_t) m].get() : nullptr;
    }
    // Its index among the channels, the buses or the masters.
    int indexOf (int ch) const
    {
        if (isMaster (ch)) return deskOrder.output (ch - (int) (engine->getStrips().size() + engine->getBuses().size()));
        return isBus (ch) ? deskOrder.bus (ch - (int) engine->getStrips().size()) : ch;
    }

    float get (int ch, const juce::String& id, float fallback) const
    {
        if (auto* m = master (ch))
        {
            if (id == "fader") return faderPos (m->gain.load());
            if (id == "width") return m->width.load() * 0.5f;
            if (auto* p = masterParam (*m, id))
                return p->getValue();
            return fallback;
        }
        if (auto* s = strip (ch))
        {
            if (id == "fader") return faderPos (s->gain.load());
            if (id == "pan")   return (s->pan.load() + 1.0f) * 0.5f;
            if (id == "width") return s->width.load() * 0.5f;
            if (auto* p = param (*s, id))
                return p->getValue();
        }
        const auto it = values.find (key (ch, id));
        return it != values.end() ? it->second : fallback;
    }

    void set (int ch, const juce::String& id, float pos)
    {
        pos = juce::jlimit (0.0f, 1.0f, pos);
        if (auto* m = master (ch))
        {
            if (id == "fader") m->gain = faderGain (pos);
            else if (id == "width") m->width = pos * 2.0f;
            else if (auto* p = masterParam (*m, id)) p->setValueNotifyingHost (pos);
            return;
        }
        if (auto* s = strip (ch))
        {
            if (id == "fader") { s->gain = faderGain (pos); return; }
            if (id == "pan")   { s->pan = pos * 2.0f - 1.0f; return; }
            if (id == "width") { s->width = pos * 2.0f; return; }
            if (auto* p = param (*s, id))
            {
                p->setValueNotifyingHost (pos);
                if (id == "hpf") setFlag (*s, "hpfOn", pos > 0.005f);   // (all the way down: the filter out)
                if (id == "lpf") setFlag (*s, "lpfOn", pos < 0.995f);
                return;
            }
        }
        values[key (ch, id)] = pos;
    }

    void gesture (int ch, const juce::String& id, bool starting)
    {
        if (auto* m = master (ch))
        {
            if (auto* p = masterParam (*m, id))
                starting ? p->beginChangeGesture() : p->endChangeGesture();
            return;
        }
        if (auto* s = strip (ch))
            if (auto* p = param (*s, id))
                starting ? p->beginChangeGesture() : p->endChangeGesture();
    }

    // Double-click: back to where it starts.
    void reset (int ch, const juce::String& id)
    {
        if (auto* m = master (ch))
        {
            if (id == "fader") m->gain = 1.0f;
            else if (id == "width") m->width = 1.0f;
            else if (auto* p = masterParam (*m, id)) p->setValueNotifyingHost (p->getDefaultValue());
            return;
        }
        if (auto* s = strip (ch))
        {
            if (id == "fader") { s->gain = 1.0f; return; }
            if (id == "pan")   { s->pan = 0.0f; return; }
            if (id == "width") { s->width = 1.0f; return; }
            if (auto* p = param (*s, id))
                p->setValueNotifyingHost (p->getDefaultValue());
        }
    }

    bool isOn (int ch, const juce::String& id, bool fallback) const
    {
        if (auto* m = master (ch))
        {
            if (id == "power") return m->enabled.load();
            if (id == "cut")   return m->mute.load();
            if (id == "safe")  return m->safeOn.load();
            if (id == "mon")   return m->monitor.load();
            if (id == "device") return true;
            if (m->masterChain != nullptr && (id == "busIn" || id == "eqIn"))
                return m->masterChain->params.getRawParameterValue (id == "busIn" ? "busOn" : "eqOn")->load() > 0.5f;
            return fallback;
        }
        if (auto* s = strip (ch))
        {
            if (id == "power")    return s->enabled.load();
            if (id == "solo")     return s->solo.load();
            if (id == "cut")      return s->mute.load();
            if (id == "soloSafe") return s->soloIsolate.load();
            if (s->channel != nullptr)
            {
                auto v = [s] (const char* p) { return s->channel->params.getRawParameterValue (p)->load(); };
                if (id == "dynIn" || id == "chainDyn") return v ("compOn") > 0.5f || v ("gateOn") > 0.5f;
                if (id == "fast")      return v ("compAttack") < 5.0f;
                if (id == "gateFast")  return v ("gateAttack") < 0.5f;
                if (id == "chainFilt") return v ("hpfOn") > 0.5f || v ("lpfOn") > 0.5f;
                if (const auto* flag = flagFor (id))
                    return v (flag) > 0.5f;
            }
        }
        const auto it = lit.find (key (ch, id));
        return it != lit.end() ? it->second : fallback;
    }

    void toggle (int ch, const juce::String& id, bool fallback)
    {
        const bool now = ! isOn (ch, id, fallback);
        if (auto* m = master (ch))
        {
            if (id == "power")      { engine->setMasterEnabled (indexOf (ch), now); engine->refreshDevices(); }
            else if (id == "cut")   m->mute = now;
            else if (id == "safe")  m->safeOn = now;
            else if (id == "mon")   m->monitor = now;
            else if (m->masterChain != nullptr && (id == "busIn" || id == "eqIn"))
                if (auto* p = m->masterChain->params.getParameter (id == "busIn" ? "busOn" : "eqOn"))
                    p->setValueNotifyingHost (now ? 1.0f : 0.0f);
            return;
        }
        if (auto* s = strip (ch))
        {
            if (id == "power")    { engine->setStripEnabled (*s, now); engine->refreshDevices(); engine->refreshDucking(); return; }   // (off: not processed, its device or app let go)
            if (id == "solo")     { s->solo = now; return; }
            if (id == "cut")      { s->mute = now; return; }
            if (id == "soloSafe") { s->soloIsolate = now; return; }
            if (s->channel != nullptr)
            {
                if (id == "dynIn")    { setFlag (*s, "compOn", now); setFlag (*s, "gateOn", now); return; }
                if (id == "fast")     { setValue (*s, "compAttack", now ? 3.0f : 10.0f); return; }   // (the console's 3 ms fast attack)
                if (id == "gateFast") { setValue (*s, "gateAttack", now ? 0.1f : 1.5f); return; }    // (100 us against 1.5 ms)
                if (const auto* flag = flagFor (id)) { setFlag (*s, flag, now); return; }
            }
        }
        lit[key (ch, id)] = now;
    }

    // THE ORDER: the channel's three blocks (filters, EQ, dynamics) as its rack runs them.
    enum Block { filters, eq, dynamics };
    std::vector<int> order (int ch) const
    {
        std::vector<int> blocks { filters, eq, dynamics };
        if (auto* s = strip (ch))
        {
            const auto pos = positions (*s);
            std::stable_sort (blocks.begin(), blocks.end(), [&pos] (int a, int b) { return pos[(size_t) a] < pos[(size_t) b]; });
        }
        return blocks;
    }
    bool blockOn (int ch, int block) const
    {
        return isOn (ch, block == filters ? "chainFilt" : block == eq ? "chainEq" : "chainDyn", true);
    }
    // A block moves up one (the first goes to the end): the rack is laid out again in the new order,
    // its plugins after the EQ and everything else where it was. A hand-patched rack isn't touched.
    void moveUp (int ch, int at)
    {
        auto* s = strip (ch);
        if (s == nullptr)
            return;
        using E = AudioEngine;
        auto blocks = order (ch);
        if (at <= 0) std::rotate (blocks.begin(), blocks.begin() + 1, blocks.end());
        else         std::swap (blocks[(size_t) at], blocks[(size_t) at - 1]);
        auto patch = engine->getRack (*s);
        if (patch.manual)
            return;
        std::vector<E::RackUnit> all (patch.left.begin(), patch.left.end());
        all.insert (all.end(), patch.right.begin(), patch.right.end());
        patch.inRack = E::allBuiltins;   // (every built-in in the path, so the order is the console's)
        for (int b = 0; b < E::rackBuiltins; ++b)
            if (std::find (all.begin(), all.end(), E::RackUnit { b, nullptr }) == all.end())
                all.push_back ({ b, nullptr });
        auto take = [&all] (std::initializer_list<int> builtins)
        {
            std::vector<E::RackUnit> found;
            for (int b : builtins)
                for (auto& u : all)
                    if (u.plugin == nullptr && u.builtin == b)
                        found.push_back (u);
            return found;
        };
        std::vector<E::RackUnit> plugins, next;
        for (auto& u : all)
            if (u.plugin != nullptr)
                plugins.push_back (u);
        auto add = [&next] (const std::vector<E::RackUnit>& units) { next.insert (next.end(), units.begin(), units.end()); };
        add (take ({ E::rackInput, E::rackNoise }));
        for (int b : blocks)
        {
            add (b == filters ? take ({ E::rackFilters }) : b == eq ? take ({ E::rackEq }) : take ({ E::rackGate, E::rackComp }));
            if (b == eq)
                add (plugins);
        }
        add (take ({ E::rackBus, E::rackDeess, E::rackLevel, E::rackFx, E::rackLimit }));
        for (auto& u : all)
            if (std::find (next.begin(), next.end(), u) == next.end())
                next.push_back (u);
        const auto left = juce::jmin ((std::ptrdiff_t) patch.left.size(), (std::ptrdiff_t) next.size());
        patch.left.assign (next.begin(), next.begin() + left);
        patch.right.assign (next.begin() + left, next.end());
        engine->setRack (*s, patch);
    }

    // WHERE IT GOES: the buses (but itself), every output device (through the master on it, made
    // when first used), then masters on no device listed (mix-minus, stems). Each with whether it's
    // sent there, and what sending it there (or not) does.
    struct Route { juce::String label, shortLabel; bool on = false; std::function<void()> toggle; int target = -1; };   // (target: the strip it goes to; -1, a device with no output yet)
    std::vector<Route> routes (int ch)
    {
        std::vector<Route> list;
        if (isMaster (ch))
            return list;
        auto* s = strip (ch);
        if (s == nullptr)
            return list;
        auto& e = *engine;
        const bool bus = isBus (ch);
        const int index = indexOf (ch);
        auto again = [this, bus, index] () -> AudioEngine::Strip*
        {
            auto& all = bus ? engine->getBuses() : engine->getStrips();
            return juce::isPositiveAndBelow (index, (int) all.size()) ? all[(size_t) index].get() : nullptr;
        };
        auto shorten = [] (juce::String name)
        {
            name = name.upToFirstOccurrenceOf (" (", false, false).trim();   // (the adapter in brackets goes)
            return name.length() > 14 ? name.substring (0, 13) + "." : name;
        };
        auto& buses = e.getBuses();
        for (int p = 0; p < (int) buses.size(); ++p)
        {
            if (bus && p == index)
                continue;
            const bool on = s->busSends[(size_t) p].load();
            list.push_back ({ buses[(size_t) p]->name, shorten (buses[(size_t) p]->name), on, [this, again, bus, index, p, on]
            {
                if (bus)
                    engine->setBusSend (index, p, ! on);   // (refused if it would loop)
                else if (auto* st = again())
                {
                    const auto before = engine->findFeedbackLoop();
                    st->busSends[(size_t) p] = ! on;
                    if (const auto loop = engine->findFeedbackLoop(); ! on && loop.isNotEmpty() && loop != before)
                        st->busSends[(size_t) p] = false;   // (it would hear itself back)
                }
            } });
            list.back().target = busStrip (p);
        }
        auto& masters = e.getMasters();
        std::vector<bool> listed (masters.size(), false);
        for (auto& out : e.outputChoices())
        {
            const int b = e.masterPlayingOn (out.candidate, out.firstChannel);
            if (b >= 0)
                listed[(size_t) b] = true;
            const bool on = b >= 0 && s->sends[(size_t) b].load();
            list.push_back ({ out.label, shorten (out.label), on, [this, again, out, b, on]
            {
                const int m = b >= 0 ? b : engine->masterFor (out);
                if (auto* st = again(); st != nullptr && m >= 0)
                    st->sends[(size_t) m] = ! on;
            } });
            if (b >= 0)
                list.back().target = outputStrip (b);
        }
        for (int b = 0; b < (int) masters.size(); ++b)
            if (! listed[(size_t) b])
            {
                const bool on = s->sends[(size_t) b].load();
                list.push_back ({ masters[(size_t) b]->name, shorten (masters[(size_t) b]->name), on, [again, b, on] { if (auto* st = again()) st->sends[(size_t) b] = ! on; } });
                list.back().target = outputStrip (b);
            }
        return list;
    }

    // Readouts and names.
    juce::String text (int ch, const juce::String& id, const juce::String& fallback) const
    {
        if (auto* m = master (ch))
        {
            if (id == "name")    return m->name;
            if (id == "faderDb") { const float g = m->gain.load(); return g <= 0.0f ? juce::String ("-inf") : juce::String (juce::Decibels::gainToDecibels (g), 1); }
            if (id == "lufs")    { const float l = m->loudness.lufs(); return l <= -70.0f ? juce::String ("-- LUFS") : juce::String (l, 1) + " LUFS"; }
            if (id == "device")
            {
                const auto places = m->places();
                if (places.empty() || places[0].candidate.isEmpty()) return "NO DEVICE";
                for (auto& out : engine->outputChoices())
                    if (out.candidate == places[0].candidate && out.firstChannel / 2 == places[0].firstChannel / 2)
                        return out.label.upToFirstOccurrenceOf (" (", false, false).toUpperCase();
                return places[0].candidate.upToFirstOccurrenceOf (" (", false, false).toUpperCase() + (places[0].target.isEmpty() ? " (OFF)" : "");
            }
            return fallback;
        }
        if (auto* s = strip (ch))
        {
            if (id == "name")    return s->name;
            if (id == "faderDb") { const float g = s->gain.load(); return g <= 0.0f ? juce::String ("-inf") : juce::String (juce::Decibels::gainToDecibels (g), 1); }
            if (id == "mix" && s->channel != nullptr) return juce::String (s->channel->params.getRawParameterValue ("compMix")->load(), 1) + "% MIX";
        }
        if (id == "name") return "CH " + juce::String (ch + 1);
        if (id == "faderDb") return juce::String (juce::Decibels::gainToDecibels (faderGain (get (ch, "fader", 0.75f)), -100.0f), 1);
        if (id == "mix") return "100.0% MIX";
        return fallback;
    }

    // A knob's or fader's value, in its own units: dB, Hz / kHz, ms / s, a ratio, %, Q, L / C / R,
    // OUT for a filter switched out. (No engine: how far along its travel, in %.)
    juce::String valueText (int ch, const juce::String& id) const
    {
        if (auto* m = master (ch))
        {
            if (id == "fader") { const float g = m->gain.load(); return g <= 0.0f ? juce::String ("-INF") : juce::String (juce::Decibels::gainToDecibels (g), 1) + " dB"; }
            if (id == "width") return juce::String (juce::roundToInt (m->width.load() * 100.0f)) + "%";
            auto* p = masterParam (*m, id);
            if (p == nullptr)
                return {};
            const auto t = p->getCurrentValueAsText();
            if (id == "busAttack")  return t + " ms";
            if (id == "busRelease") return t == "AUTO" ? t : t + " s";
            if (id == "busThreshold" || id == "busMakeup" || id.endsWith ("Gain")) return juce::String (p->convertFrom0to1 (p->getValue()), 1) + " dB";
            if (id.endsWith ("Freq")) { const float f = p->convertFrom0to1 (p->getValue()); return f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; }
            if (id == "busMix") return juce::String (juce::roundToInt (p->convertFrom0to1 (p->getValue()))) + "%";
            return t;
        }
        auto* s = strip (ch);
        if (s == nullptr)
            return juce::String (juce::roundToInt (get (ch, id, 0.5f) * 100.0f)) + "%";
        auto db = [] (float v, int places = 1) { return (v > 0.0f ? "+" : "") + juce::String (v, places) + " dB"; };
        if (id == "fader") { const float g = s->gain.load(); return g <= 0.0f ? juce::String ("-INF") : db (juce::Decibels::gainToDecibels (g)); }
        if (id == "width") return s->source.stereo ? juce::String (juce::roundToInt (s->width.load() * 100.0f)) + "%" : juce::String ("MONO");
        if (id == "pan")
        {
            const int amount = juce::roundToInt (std::abs (s->pan.load()) * 100.0f);
            return amount == 0 ? juce::String ("C") : juce::String (s->pan.load() < 0.0f ? "L " : "R ") + juce::String (amount);
        }
        auto* p = param (*s, id);
        if (p == nullptr || s->channel == nullptr)
            return {};
        const float v = p->convertFrom0to1 (p->getValue());
        auto flag = [s] (const char* f) { return s->channel->params.getRawParameterValue (f)->load() > 0.5f; };
        auto hz = [] (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, f < 10000.0f ? 2 : 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; };
        auto time = [] (float ms) { return ms >= 1000.0f ? juce::String (ms / 1000.0f, 2) + " s" : juce::String (ms, ms < 10.0f ? 1 : 0) + " ms"; };
        if (id == "hpf") return flag ("hpfOn") ? hz (v) : juce::String ("OUT");
        if (id == "lpf") return flag ("lpfOn") ? hz (v) : juce::String ("OUT");
        if (id.endsWith ("Freq")) return hz (v);
        if (id.endsWith ("Q")) return "Q " + juce::String (v, 2);
        if (id == "ratio") return p->getValue() >= 0.999f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e:1")) : juce::String (v, 1) + ":1";
        if (id == "release" || id == "gateRelease" || id == "gateHold") return time (v);
        if (id == "mix") return juce::String (juce::roundToInt (v)) + "%";
        if (id == "range") return p->getValue() <= 0.001f ? juce::String ("SILENT") : db (v, 0);
        if (id == "drive") return juce::String (juce::roundToInt (p->getValue() * 100.0f)) + "%";
        if (id == "trim" || id.endsWith ("Gain") || id == "thresh" || id == "gateThresh") return db (v);
        return p->getCurrentValueAsText();
    }

    // Meters: gain reduction (3, 6, 10, 14, 20 dB lit), and the channel's level (dB).
    int reductionLit (int ch, const juce::String& id, int fallback) const
    {
        if (auto* s = strip (ch); s != nullptr && s->channel != nullptr)
        {
            s->channel->previewDetectors = true;   // (the ladders show what they'd do while they're out)
            const float db = id == "gateGr" ? (s->channel->gatePreviewOpen.load() - 1.0f) * 20.0f : s->channel->compPreview.load();
            int n = 0;
            for (float step : { 3.0f, 6.0f, 10.0f, 14.0f, 20.0f })
                n += -db >= step ? 1 : 0;
            return n;
        }
        return fallback;
    }
    // The bus compressor's gain reduction on an output (dB, 0 or less).
    float busReduction (int ch) const
    {
        auto* m = master (ch);
        return m != nullptr && m->masterChain != nullptr ? m->masterChain->busReduction.load() : -4.0f;
    }

    bool meterIn = false;   // METER IN: the strips' meters read what comes in, before the processing
    float levelDb (int ch, int side) const
    {
        if (auto* m = master (ch))
            return juce::Decibels::gainToDecibels (side == 0 ? m->peakL.load() : m->peakR.load(), -100.0f);
        if (auto* s = strip (ch))
            return juce::Decibels::gainToDecibels (meterIn ? (side == 0 ? s->sourcePeakL.load() : s->sourcePeakR.load())
                                                           : (side == 0 ? s->peakL.load() : s->peakR.load()), -100.0f);
        return side == 0 ? -12.0f : -15.0f;
    }

    // The fader's scale, as printed beside it: 12, 6, 0, 5, 10, 20, 30, 40, 50 evenly down, then off.
    static float faderGain (float pos)
    {
        static const float marks[] = { 12.0f, 6.0f, 0.0f, -5.0f, -10.0f, -20.0f, -30.0f, -40.0f, -50.0f };
        const float f = (1.0f - pos) * 9.0f;
        if (f >= 8.999f)
            return 0.0f;
        const int i = (int) f;
        const float db = i >= 8 ? -50.0f - (f - 8.0f) * 40.0f : marks[i] + (marks[i + 1] - marks[i]) * (f - (float) i);
        return juce::Decibels::decibelsToGain (db);
    }
    static float faderPos (float gain)
    {
        if (gain <= 0.0f)
            return 0.0f;
        float lo = 0.0f, hi = 1.0f;   // (the inverse, by halving: the law is monotonic)
        for (int i = 0; i < 24; ++i)
        {
            const float mid = (lo + hi) * 0.5f;
            (faderGain (mid) < gain ? lo : hi) = mid;
        }
        return (lo + hi) * 0.5f;
    }

private:
    static juce::String key (int ch, const juce::String& id) { return juce::String (ch) + ":" + id; }

    // Where each block sits in the rack (dynamics: its first of gate and compressor).
    std::array<int, 3> positions (AudioEngine::Strip& s) const
    {
        using E = AudioEngine;
        const auto& rack = engine->getRack (s);
        std::vector<E::RackUnit> all (rack.left.begin(), rack.left.end());
        all.insert (all.end(), rack.right.begin(), rack.right.end());
        auto at = [&all] (int b) { const auto it = std::find (all.begin(), all.end(), E::RackUnit { b, nullptr }); return it == all.end() ? 999 : (int) (it - all.begin()); };
        return { at (E::rackFilters), at (E::rackEq), juce::jmin (at (E::rackGate), at (E::rackComp)) };
    }

    static const char* paramFor (const juce::String& id)
    {
        static const std::map<juce::String, const char*> map {
            { "trim", "trim" }, { "drive", "drive" }, { "ratio", "compRatio" }, { "thresh", "compThreshold" }, { "release", "compRelease" }, { "mix", "compMix" },
            { "range", "gateRange" }, { "gateThresh", "gateThreshold" }, { "gateRelease", "gateRelease" }, { "gateHold", "gateHold" },
            { "lpf", "lpf" }, { "hpf", "hpf" }, { "hfGain", "highGain" }, { "hfFreq", "highFreq" },
            { "hmfGain", "mid2Gain" }, { "hmfFreq", "mid2Freq" }, { "hmfQ", "mid2Q" },
            { "lmfGain", "midGain" }, { "lmfFreq", "midFreq" }, { "lmfQ", "midQ" }, { "lfGain", "lowGain" }, { "lfFreq", "lowFreq" } };
        const auto it = map.find (id);
        return it != map.end() ? it->second : nullptr;
    }
    static const char* flagFor (const juce::String& id)
    {
        static const std::map<juce::String, const char*> map {
            { "polarity", "polarity" }, { "hfBell", "highBell" }, { "lfBell", "lowBell" }, { "eqIn", "eqOn" }, { "eqKey", "eqOn" }, { "chainEq", "eqOn" },
            { "exp", "gateExpand" }, { "filtKey", "filterToKey" }, { "extKey", "keyExternal" },
            { "pre", "inputOn" }, { "listen", "keyListen" }, { "pk", "compPeak" } };
        const auto it = map.find (id);
        return it != map.end() ? it->second : nullptr;
    }
    // An output's mix-bus settings (its masterChain): the bus compressor and the mix EQ.
    static juce::RangedAudioParameter* masterParam (AudioEngine::Master& m, const juce::String& id)
    {
        if (m.masterChain == nullptr)
            return nullptr;
        static const std::map<juce::String, const char*> map {
            { "busThreshold", "busThreshold" }, { "busRatio", "busRatio" }, { "busAttack", "busAttack" }, { "busRelease", "busRelease" },
            { "busMakeup", "busMakeup" }, { "busMix", "busMix" },
            { "hfGain", "highGain" }, { "hfFreq", "highFreq" }, { "lfGain", "lowGain" }, { "lfFreq", "lowFreq" } };
        const auto it = map.find (id);
        return it != map.end() ? m.masterChain->params.getParameter (it->second) : nullptr;
    }

    static juce::RangedAudioParameter* param (AudioEngine::Strip& s, const juce::String& id)
    {
        const auto* p = paramFor (id);
        return p != nullptr && s.channel != nullptr ? s.channel->params.getParameter (p) : nullptr;
    }
    static void setFlag (AudioEngine::Strip& s, const char* id, bool on)
    {
        if (auto* p = s.channel->params.getParameter (id))
            p->setValueNotifyingHost (on ? 1.0f : 0.0f);
    }
    static void setValue (AudioEngine::Strip& s, const char* id, float v)
    {
        if (auto* p = s.channel->params.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    }

    AudioEngine* engine;
    std::map<juce::String, float> values;
    std::map<juce::String, bool> lit;
};

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
            float y0 = 40.0f, total = 0.0f;
            for (auto& sec : layout.sections)
                total += sec.height * zoom;
            const auto area = juce::Rectangle<float> (col.x, y0, col.w - 2.0f * zoom, total);
            g.setColour (juce::Colour (0xff111214));
            g.fillRect (area);
            for (auto& sec : layout.sections)   // (the sections' seams carried across, so the desk reads as one)
            {
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
            float h = 0.0f;
            for (auto& sec : layout.sections)
                h += sec.height * zoom;
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
        const auto c = k.getKeyCode();
        const bool layoutKey = k.getModifiers().isCtrlDown() && k.getModifiers().isShiftDown();
        if (layoutKey && (c == 'O' || c == 'o')) outlines = ! outlines;
        else if (layoutKey && (c == 'R' || c == 'r')) reload();
        else if (c >= '1' && c <= '8' && (engine == nullptr)) channels = c - '0';
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
        menu.addItem ("Narrow", true, spacers[(size_t) index].narrow, [this, index] { spacers[(size_t) index].narrow = ! spacers[(size_t) index].narrow; saveSpacers(); resizeToFit(); repaint(); });
        menu.addItem ("Add another", [this, index] { spacers.insert (spacers.begin() + index + 1, spacers[(size_t) index]); saveSpacers(); resizeToFit(); repaint(); });
        menu.addItem ("Remove spacer", [this, index] { spacers.erase (spacers.begin() + index); saveSpacers(); resizeToFit(); repaint(); });
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
            bind.meterIn = xml->getBoolAttribute ("meterIn", false);
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
        xml.setAttribute ("meterIn", bind.meterIn);
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
                if (const auto now = partState (item, ch); std::exchange (drawn[n], now) != now && ! all)
                    repaint ((item.r * zoom).translated (x0, y0).expanded (16.0f * zoom).getSmallestIntegerContainer());
                ++n;
            }
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
            parts.push_back ({ { r.getX(), y, r.getWidth(), rowH }, "PATH " + juce::String (p + 1) + (path.name.isNotEmpty() ? ": " + path.name.toUpperCase() : juce::String()) + (path.mute.load() ? "  (MUTED)" : ""), 0 });
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
            for (auto& it : s.items)
                if (&it == &item)
                    return (item.r * zoom).translated (x0, y0);
            y0 += s.height * zoom;
        }
        return {};
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

    // THE SIDEBAR's SELECT and METER cards (kept in console.xml), and its SECTIONS.
    bool autoSelect = true, followSelect = true;
    void setAutoSelect (bool on) { autoSelect = on; saveSpacers(); }
    void setFollowSelect (bool on) { followSelect = on; saveSpacers(); followFaider(); }
    bool meterIn() const { return bind.meterIn; }
    void setMeterIn (bool on) { bind.meterIn = on; saveSpacers(); repaint(); }
    int sectionCount() const { return (int) layout.sections.size(); }
    juce::String sectionName (int i) const { return layout.sections[(size_t) i].name.toUpperCase(); }
    juce::Range<float> sectionSpan (int i) const
    {
        float y = 40.0f;
        for (int k = 0; k < i; ++k)
            y += layout.sections[(size_t) k].height * zoom;
        return { y, y + layout.sections[(size_t) i].height * zoom };
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
        if (! live())
            return;
        juce::StringArray exes;
        for (auto& a : listAudioApps())
            exes.addIfNotAlreadyThere (a.exe, true);
        exes.sortNatural();
        juce::PopupMenu menu;
        menu.addSectionHeader ("APPS PLAYING");
        for (auto& exe : exes)
        {
            const auto label = juce::File (exe).getFileNameWithoutExtension();
            const bool there = std::any_of (engine->getStrips().begin(), engine->getStrips().end(), [&exe] (auto& s)
                                            { return s->source.kind == AudioEngine::Source::app && s->source.deviceName.equalsIgnoreCase (exe); });
            menu.addItem (label.toUpperCase() + (there ? "  (ON THE DESK)" : ""), ! there, false, [this, exe, label] { addAppStrip (exe, label); });
        }
        if (exes.isEmpty())
            menu.addItem ("NO APPS PLAYING", false, false, [] {});
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (near));
    }
    void addAppStrip (const juce::String& exe, const juce::String& label)
    {
        auto name = label.toUpperCase();
        for (int i = 2; std::any_of (engine->getStrips().begin(), engine->getStrips().end(), [&name] (auto& s) { return s->name == name; }); ++i)
            name = label.toUpperCase() + " " + juce::String (i);
        engine->addStrip (name, { AudioEngine::Source::app, 0, exe, true });
        engine->refreshDevices();
        engine->logEvent ("input " + name + ": the app " + exe + " as a channel");
        channels = bind.channels();
        resizeToFit();
        select ((int) engine->getStrips().size() - 1);
    }

    // A combinator path renamed.
    void renamePath (int busIndex, int p)
    {
        auto* w = new juce::AlertWindow ("Rename path", {}, juce::MessageBoxIconType::NoIcon);
        auto& buses = engine->getBuses();
        if (! juce::isPositiveAndBelow (busIndex, (int) buses.size()) || ! juce::isPositiveAndBelow (p, (int) buses[(size_t) busIndex]->paths.size()))
            return delete w;
        w->addTextEditor ("name", buses[(size_t) busIndex]->paths[(size_t) p]->name);
        w->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, busIndex, p] (int result)
        {
            auto& all = engine->getBuses();
            if (result == 1 && juce::isPositiveAndBelow (busIndex, (int) all.size()) && juce::isPositiveAndBelow (p, (int) all[(size_t) busIndex]->paths.size()))
                all[(size_t) busIndex]->paths[(size_t) p]->name = w->getTextEditorContents ("name").trim().toUpperCase();
            repaint();
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
    // AN OUTPUT's menu: the device it plays on, renaming it, removing it.
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
        devices.addItem ("No device", true, places.empty() || places[0].candidate.isEmpty(), [this, index]
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
        menu.addItem ("Turn on", [this, ch] { bind.toggle (ch, "power", true); repaint(); });
        menu.addSeparator();
        menu.addItem ("Add spacer before", [this, ch] { addSpacer (ch, true); });
        menu.addItem ("Add spacer after", [this, ch] { addSpacer (ch, false); });
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
        menu.addItem ("Device...", [this, ch] { showDeviceMenu (ch); });
        menu.addSeparator();
        menu.addItem ("Add spacer before", [this, ch] { addSpacer (ch, true); });
        menu.addItem ("Add spacer after", [this, ch] { addSpacer (ch, false); });
        menu.addSeparator();
        menu.addItem ("Rename...", [this, index]
        {
            auto& masters = engine->getMasters();
            if (! juce::isPositiveAndBelow (index, (int) masters.size()))
                return;
            auto* w = new juce::AlertWindow ("Rename", {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", masters[(size_t) index]->name);
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, index, w] (int result)
            {
                auto& list = engine->getMasters();
                const auto wanted = w->getTextEditorContents ("name").trim();
                if (result == 1 && wanted.isNotEmpty() && juce::isPositiveAndBelow (index, (int) list.size()))
                {
                    auto& mm = *list[(size_t) index];
                    const auto old = mm.name;
                    mm.name = engine->nameFor ("output", wanted, &mm);
                    engine->renamed ("output", old, mm.name);   // (its spacers follow: followRename)
                }
                repaint();
            }), true);
        });
        menu.addItem ("Remove", [this, index, ch]
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
            input.addSubMenu ("Interface", interfaceMenu, ins.size() > 0);
            juce::PopupMenu windowsMenu;
            windowsMenu.addItem ("Windows default", true, now.kind == AudioEngine::Source::device && now.deviceName == AudioEngine::windowsDefault,
                                 [source, stereo = now.stereo] { source ({ AudioEngine::Source::device, 0, AudioEngine::windowsDefault, stereo }); });
            for (auto& ep : e.getEndpoints())
                if (ep.api == "WASAPI" && ! ep.isOutput && ep.isActive() && ! e.isDeviceDisabled (ep.name))
                    windowsMenu.addItem (ep.name, true, now.kind == AudioEngine::Source::device && now.deviceName == ep.name,
                                         [source, name = ep.name, stereo = now.stereo] { source ({ AudioEngine::Source::device, 0, name, stereo }); });
            input.addSubMenu ("Windows devices", windowsMenu);
            juce::PopupMenu hearMenu;   // (what an output plays, back in: loopback)
            for (int b = 0; b < (int) e.getMasters().size(); ++b)
                hearMenu.addItem (e.getMasters()[(size_t) b]->name, true, now.kind == AudioEngine::Source::loopback && now.masterIndex == b,
                                  [source, b] { AudioEngine::Source next { AudioEngine::Source::loopback, 0, {}, true }; next.masterIndex = b; source (next); });
            input.addSubMenu ("What an output plays", hearMenu, ! e.getMasters().empty());
            input.addItem ("Test tone", true, now.kind == AudioEngine::Source::tone, [source] { source ({ AudioEngine::Source::tone, 0, {}, true }); });
            input.addItem ("Nothing", true, now.kind == AudioEngine::Source::none, [source] { source ({ AudioEngine::Source::none, 0, {}, true }); });
            menu.addSubMenu ("Input", input);
        }

        // Where it goes (as the header's boxes).
        for (auto& r : bind.routes (ch))
            route.addItem (r.label, true, r.on, [this, toggle = r.toggle] { toggle(); repaint(); });
        menu.addSubMenu ("Route to", route);

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
                plugins.addSubMenu ("Remove", removes);
            }
            plugins.addItem ("Add plugin...", [this, target] { mixer->showAddInsertMenu (target, juce::PopupMenu::Options().withMousePosition()); });
            menu.addSubMenu ("Plugins", plugins);
        }
        if (! bus)
            menu.addItem (s->source.stereo ? "Make mono" : "Make stereo", [this, index]
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
        menu.addSeparator();
        MixerComponent::addColourMenu (menu, s->colour, [this, again] (juce::Colour c) { if (auto* st = again()) st->colour = c; repaint(); });   // (Faider's palette: it follows the channel everywhere)
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
                    one.addSubMenu ("Remove plugin", removes);
                    one.addSeparator();
                }
                one.addItem ("Add plugin...", [this, target] { mixer->showAddInsertMenu (target, juce::PopupMenu::Options().withMousePosition()); });
                one.addItem ("Rename...", [this, index, p] { renamePath (index, p); });
                one.addItem ("Remove path", (int) s->paths.size() > 1, false, [this, index, p] { engine->removePath (index, p); repaint(); });
                paths.addSubMenu ("PATH " + juce::String (p + 1) + (path.name.isNotEmpty() ? ": " + path.name.toUpperCase() : juce::String()), one);
            }
            paths.addSeparator();
            paths.addItem ("Add path", [this, index, n = (int) s->paths.size()] { engine->addPath (index, "PATH " + juce::String (n + 1)); repaint(); });
            menu.addSubMenu ("Paths", paths);
        }
        menu.addItem ("Add spacer before", [this, ch] { addSpacer (ch, true); });
        menu.addItem ("Add spacer after", [this, ch] { addSpacer (ch, false); });
        menu.addSeparator();
        menu.addItem ("Rename...", [this, again, bus]
        {
            auto* w = new juce::AlertWindow ("Rename", {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", again() != nullptr ? again()->name : juce::String());
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, again, bus, w] (int result)
            {
                auto* st = again();
                const auto wanted = w->getTextEditorContents ("name").trim();
                if (result == 1 && st != nullptr && wanted.isNotEmpty())
                {
                    const auto old = st->name;
                    st->name = engine->nameFor (bus ? "bus" : "input", wanted, st);
                    engine->renamed (bus ? "bus" : "input", old, st->name);   // (spacers and strip type follow: followRename)
                }
                repaint();
            }), true);
        });
        menu.addItem ("Remove", [this, bus, index, ch]
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
                juce::Path power;
                power.addCentredArc (c.getCentreX(), c.getCentreY(), c.getWidth() * 0.5f, c.getHeight() * 0.5f, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.6f, true);
                g.setColour (juce::Colour (0xff3a1e08));
                g.strokePath (power, juce::PathStrokeType (1.6f * zoom));
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
                const auto key = tapeArea (r, k);
                const auto touched = touch (ch, "tape" + k, key);
                LargeFormat::key (g, key, {}, lit ? 1.0f : 0.0f, k == "PAUSE" ? Console::Colours::palette ("orange") : Console::Colours::runLamp(), true, touched.press * 1.5f, touched.hover);
                drawTransport (g, k, key.translated (0.0f, touched.press * 1.5f).reduced (key.getWidth() * 0.26f, key.getHeight() * 0.28f), lit ? Console::Colours::litInk() : juce::Colour (0xff26282b));
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
            auto at = [&pivot] (float a, float rr) { return juce::Point<float> (pivot.x + rr * std::sin (a), pivot.y - rr * std::cos (a)); };
            g.setColour (Console::Colours::vuInk());
            g.setFont (LargeFormat::print (9.0f * juce::jmax (0.8f, zoom)));
            for (int db : { 0, 3, 6, 10, 14, 20 })
            {
                const float a = a0 + (a1 - a0) * (float) db / 20.0f;
                const auto p1 = at (a, radius * 0.86f), p2 = at (a, radius * 0.95f);
                g.drawLine (p1.x, p1.y, p2.x, p2.y, 1.2f);
                g.drawText (juce::String (db), juce::Rectangle<float> (24.0f * zoom, 12.0f * zoom).withCentre (at (a, radius * 0.77f)), juce::Justification::centred, false);
            }
            g.drawText ("GR", face.withTrimmedTop (face.getHeight() * 0.55f), juce::Justification::centred, false);
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (face.toNearestInt());
            const float a = a0 + (a1 - a0) * gr / 20.0f;
            const auto tip = at (a, radius * 0.97f);
            g.setColour (juce::Colour (0xff141414));
            g.drawLine (pivot.x, pivot.y, tip.x, tip.y, 1.6f * zoom);
        }
        else if (item.kind == "lamp")
        {
            // A small LED that lights while the channel's source has signal.
            LargeFormat::led (g, r.getCentre(), bind.levelDb (ch, 0) > -40.0f, named (item.colour), r.getWidth() * 0.7f);
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
            juce::Path power;
            power.addCentredArc (icon.getCentreX(), icon.getCentreY(), icon.getWidth() * 0.5f, icon.getHeight() * 0.5f, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.6f, true);
            const bool on = bind.isOn (ch, "power", true);
            const auto touched = touch (ch, "power", r.withWidth (r.getHeight()), true);
            if (touched.hover > 0.01f)
            {
                g.setColour (Console::Colours::accent().withAlpha (0.25f * touched.hover + 0.2f * touched.press));
                g.fillEllipse (icon.expanded (icon.getWidth() * 0.45f));
            }
            g.setColour ((on ? Console::Colours::palette ("orange") : juce::Colour (0xff9a9da2)).brighter (0.4f * touched.hover));
            g.strokePath (power, juce::PathStrokeType (1.6f * zoom));
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
            for (int side = 0; side < (item.single ? 1 : 2); ++side)
            {
                const auto bar = item.single ? r : r.withWidth (r.getWidth() * 0.44f).translated ((float) side * r.getWidth() * 0.56f, 0.0f);
                const float pitch = bar.getHeight() / (float) item.count;
                const int on = juce::jlimit (0, item.count, (int) ((bind.levelDb (ch, side) + 48.0f) / 3.0f) + 1);   // (from -48 dB, 3 dB a segment)
                const int hold = heldPeak (ch * 2 + side, on);   // PEAK HOLD: the highest lit a moment longer
                for (int i = 0; i < item.count; ++i)
                {
                    const auto colour = item.colour.isNotEmpty() ? named (item.colour).interpolatedWith (t.ledRed, (float) i / (float) item.count)
                                                                 : (i >= item.count - 2 ? t.ledRed : (i >= item.count - 5 ? t.ledYellow : t.ledGreen));
                    const auto seg = juce::Rectangle<float> (bar.getX(), bar.getBottom() - pitch * (float) (i + 1) + 1.0f, bar.getWidth(), pitch - 2.0f);
                    if (i < on || i == hold - 1)
                        LargeFormat::bloom (g, seg, colour, 0.8f, 1.0f, 2.0f);
                    g.setColour (i < on ? colour : colour.withMultipliedBrightness (0.22f).withMultipliedSaturation (0.6f));
                    g.fillRect (seg);
                }
            }
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
        float h = 0.0f;
        for (auto& s : layout.sections)
            h += s.height;
        layoutColumns();
        setSize (juce::roundToInt (deskWidth + 20.0f), juce::roundToInt (60.0f + h * zoom));
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

// The console as Faider's Mixer page: the strips in a scrolling view.
namespace ConsoleDesk
{
    juce::File layoutFile()
    {
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("mixer.json");
    }

    // THE PLUGIN PANEL: the selected strip's plugins as cards, in the order they run, each showing
    // what it costs (its share of the processing time, its latency) and whether it's doing anything
    // (bypassed but still loaded; unusually heavy), with BYPASS, OPEN, REMOVE and moves; ADD at the end.
    class PluginPanel : public juce::Component, private juce::Timer
    {
    public:
        explicit PluginPanel (Bench& b) : bench (b) { startTimerHz (5); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Console::Colours::rail());
            g.setColour (Console::Colours::recess());
            g.fillRect (getLocalBounds().removeFromTop (2));
            auto* s = bench.selectedStrip();
            auto* engine = bench.getEngine();
            const auto summary = summaryArea();
            g.setColour (print());
            g.setFont (LargeFormat::print (12.0f));
            if (s == nullptr || engine == nullptr)
            {
                g.drawText ("NO CHANNEL SELECTED", summary.withHeight (24), juce::Justification::centredLeft, false);
                return;
            }
            float cpu = 0.0f;
            int latency = 0;
            for (auto& p : s->inserts)
            {
                cpu += cpuOf (*p);
                latency += p->getLatencySamples();
            }
            g.setColour (juce::Colour (0xffe3e5e8));
            g.setFont (LargeFormat::print (14.0f));
            g.drawText (s->name.toUpperCase(), summary.withHeight (24), juce::Justification::centredLeft, false);
            g.setColour (print());
            g.setFont (LargeFormat::print (12.0f));
            auto line = summary.withTrimmedTop (30);
            for (auto text : { juce::String ((int) s->inserts.size()) + (s->inserts.size() == 1 ? " PLUGIN" : " PLUGINS"),
                               "CPU " + juce::String (cpu, 1) + "%",
                               "LATENCY " + juce::String (1000.0 * latency / juce::jmax (8000.0, engine->getSampleRate()), 1) + " MS",
                               juce::String (s->insertsOn.load() ? "INSERT IN" : "INSERT OUT") })
            {
                g.drawFittedText (text, line.removeFromTop (20), juce::Justification::centredLeft, 1, 0.9f);
            }
            for (int i = 0; i < (int) s->inserts.size(); ++i)
                drawCard (g, i, *s->inserts[(size_t) i], *engine);
            const auto add = cardArea ((int) s->inserts.size()).toFloat();
            DeskSettings::card (g, add, false, hovered == addHover);
            g.setColour (Console::Colours::ink());
            g.setFont (LargeFormat::print (12.0f));
            g.drawText ("+  ADD PLUGIN", add, juce::Justification::centred, false);
        }

        void mouseMove (const juce::MouseEvent& e) override { const int h = hoverAt (e.getPosition()); if (h != hovered) { hovered = h; repaint(); } }
        void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* s = bench.selectedStrip();
            auto* engine = bench.getEngine();
            auto* mixer = bench.getMixer();
            if (s == nullptr || engine == nullptr || mixer == nullptr || ! s->enabled.load())   // (a strip switched off: its plugins left as they are)
                return;
            const auto target = bench.selectedTarget();
            const auto p = e.getPosition();
            if (cardArea ((int) s->inserts.size()).contains (p))
                return mixer->showAddInsertMenu (target, juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (cardArea ((int) s->inserts.size()))));
            for (int i = 0; i < (int) s->inserts.size(); ++i)
            {
                auto* plugin = s->inserts[(size_t) i].get();
                for (auto [key, area] : keys (i))
                    if (area.contains (p))
                    {
                        if (key == "BYPASS")
                        {
                            if (auto* o = engine->insertOptions (plugin))
                                o->bypassed = ! o->bypassed.load();
                        }
                        else if (key == "OPEN")   mixer->openEditor (*plugin);
                        else if (key == "REMOVE") mixer->removeInsert (target, i);
                        else if (key == "<" && i > 0) mixer->swapInserts (target, i - 1, i);
                        else if (key == ">" && i + 1 < (int) s->inserts.size()) mixer->swapInserts (target, i, i + 1);
                        repaint();
                        return;
                    }
            }
        }

    private:
        static juce::Colour print() { return Console::Colours::legendDim(); }
        juce::Rectangle<int> summaryArea() const { return { 16, 14, 240, getHeight() - 28 }; }
        juce::Rectangle<int> cardArea (int i) const { return { 270 + i * 282, 14, 270, getHeight() - 28 }; }
        static constexpr int addHover = 1000;

        float cpuOf (const juce::AudioPluginInstance& p) const
        {
            auto* o = bench.getEngine()->insertOptions (&p);
            return o != nullptr ? (float) (o->secondsPerSample.load() * bench.getEngine()->getSampleRate() * 100.0) : 0.0f;
        }

        // Its keys: BYPASS, OPEN, REMOVE along the foot; the moves (earlier, later) by its name.
        std::vector<std::pair<juce::String, juce::Rectangle<int>>> keys (int i) const
        {
            auto card = cardArea (i).reduced (12);
            std::vector<std::pair<juce::String, juce::Rectangle<int>>> list;
            auto top = card.removeFromTop (24);
            list.push_back ({ ">", top.removeFromRight (28) });
            top.removeFromRight (4);
            list.push_back ({ "<", top.removeFromRight (28) });
            auto r = card.removeFromBottom (30);
            const int w = (r.getWidth() - 8) / 3;
            for (auto* k : { "BYPASS", "OPEN", "REMOVE" })
            {
                list.push_back ({ k, r.removeFromLeft (w) });
                r.removeFromLeft (4);
            }
            return list;
        }

        int hoverAt (juce::Point<int> p) const
        {
            auto* s = bench.selectedStrip();
            if (s == nullptr)
                return -1;
            if (cardArea ((int) s->inserts.size()).contains (p))
                return addHover;
            for (int i = 0; i < (int) s->inserts.size(); ++i)
                if (cardArea (i).contains (p))
                    return i;
            return -1;
        }

        void drawCard (juce::Graphics& g, int i, juce::AudioPluginInstance& plugin, AudioEngine& engine)
        {
            auto* o = engine.insertOptions (&plugin);
            const bool bypassed = o != nullptr && o->bypassed.load();
            const float cpu = cpuOf (plugin);
            const auto rf = cardArea (i).toFloat();
            DeskSettings::card (g, rf, false, hovered == i);
            auto r = cardArea (i).reduced (12);
            g.setColour (bypassed ? juce::Colour (0xff8a8d91) : juce::Colour (0xffe3e5e8));
            g.setFont (LargeFormat::print (13.0f));
            g.drawFittedText (juce::String (i + 1) + "  " + plugin.getName().toUpperCase(), r.removeFromTop (24).withTrimmedRight (66), juce::Justification::centredLeft, 2, 0.9f);
            g.setColour (print());
            g.setFont (LargeFormat::print (11.0f, false));
            g.drawFittedText (plugin.getPluginDescription().manufacturerName, r.removeFromTop (16), juce::Justification::centredLeft, 1, 0.85f);
            r.removeFromTop (8);
            // CPU: its share of the processing time, as a meter (green, amber past 2%, red past 5%)
            g.setFont (LargeFormat::print (11.0f));
            g.drawText ("CPU  " + juce::String (cpu, 1) + "%", r.removeFromTop (16), juce::Justification::centredLeft, false);
            const auto meter = r.removeFromTop (8).toFloat();
            g.setColour (juce::Colour (0xff0b0c0d));
            g.fillRoundedRectangle (meter, 2.0f);
            const auto colour = cpu > 5.0f ? Console::Colours::stopLamp() : cpu > 2.0f ? Console::Colours::palette ("orange") : Console::Colours::runLamp();
            g.setColour (colour);
            g.fillRoundedRectangle (meter.withWidth (juce::jmax (2.0f, meter.getWidth() * juce::jmin (1.0f, cpu / 10.0f))), 2.0f);
            r.removeFromTop (8);
            g.setColour (print());
            const int lat = plugin.getLatencySamples();
            g.drawText ("LATENCY  " + (lat == 0 ? juce::String ("NONE") : juce::String (1000.0 * lat / juce::jmax (8000.0, engine.getSampleRate()), 1) + " MS"),
                        r.removeFromTop (16), juce::Justification::centredLeft, false);
            r.removeFromTop (6);
            // Is it earning its place? Bypassed but still loaded, or heavy for one plugin.
            juce::String flag;
            if (bypassed)        flag = "BYPASSED";
            else if (cpu > 5.0f) flag = "HEAVY";
            if (flag.isNotEmpty())
            {
                g.setColour (bypassed ? Console::Colours::palette ("orange") : Console::Colours::stopLamp());
                g.setFont (LargeFormat::print (11.0f));
                g.drawFittedText (flag, r.removeFromTop (16), juce::Justification::centredLeft, 1, 0.85f);
            }
            for (auto [key, area] : keys (i))
            {
                const bool lit = key == "BYPASS" && bypassed;
                LargeFormat::key (g, area.toFloat(), key, lit ? 1.0f : 0.0f, Console::Colours::palette ("orange"), true, 0.0f, 0.0f);
            }
        }

        void timerCallback() override
        {
            if (isShowing())
                repaint();   // (the CPU readings, and a chain changed elsewhere)
        }

        Bench& bench;
        int hovered = -1;
    };

    // THE TOP BAR: the time display and the transport, driving the Tape deck
    // (rewind, stop, play, wind on, record, loop).
    class TopBar : public juce::Component, private juce::Timer
    {
    public:
        explicit TopBar (AudioEngine& e) : engine (e) { startTimerHz (30); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff222326));
            g.setColour (Console::Colours::recess());
            g.fillRect (getLocalBounds().removeFromBottom (1));
            auto& tape = engine.getTape();
            const auto screen = screenArea();
            g.setColour (juce::Colour (0xff0d1110));
            g.fillRoundedRectangle (screen, 3.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawRoundedRectangle (screen, 3.0f, 1.0f);
            const auto green = Console::Colours::palette ("teal");
            auto text = screen.reduced (10.0f, 0.0f);
            g.setColour (green);
            g.setFont (LargeFormat::print (18.0f));
            g.drawText (clock (tape.isLoaded() ? tape.position() : 0.0), text.removeFromLeft (60.0f), juce::Justification::centredLeft, false);
            g.setColour (green.withAlpha (0.55f));
            g.setFont (LargeFormat::print (12.0f));
            g.drawText (clock (tape.isLoaded() ? tape.length() : 0.0), text.removeFromLeft (46.0f), juce::Justification::centredLeft, false);
            const int now = tape.current();
            g.setColour (green.withAlpha (0.85f));
            g.drawFittedText (juce::isPositiveAndBelow (now, tape.size()) ? tape.file (now).getFileNameWithoutExtension().toUpperCase() : juce::String ("NO TRACK"),
                              text.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
            paintMode (g);
            for (int i = 0; i < keys().size(); ++i)
            {
                const auto& k = keys()[i];
                const auto key = keyArea (i);
                const auto lamp = k == "REC" ? Console::Colours::stopLamp() : k == "LOOP" ? Console::Colours::palette ("orange") : Console::Colours::runLamp();
                LargeFormat::key (g, key, {}, lit[i], lamp, true, held == i ? 1.5f : 0.0f, key.contains (over) ? 1.0f : 0.0f);
                const auto ink = lit[i] > 0.5f ? Console::Colours::litInk() : k == "REC" ? juce::Colour (0xffb8302a) : juce::Colour (0xff26282b);
                Bench::drawTransport (g, k, key.reduced (key.getWidth() * 0.3f, key.getHeight() * 0.28f), ink);
                if (k == "LOOP" && tape.repeat == Tape::Repeat::one)   // (one track over)
                {
                    g.setColour (ink);
                    g.setFont (LargeFormat::print (10.0f));
                    g.drawText ("1", key.withTrimmedLeft (key.getWidth() * 0.62f).withTrimmedTop (key.getHeight() * 0.5f), juce::Justification::centred, false);
                }
            }
        }

        // SIMPLE / PRO: two halves, the one in use cream; the other lights under the mouse.
        std::function<bool()> isSimple;
        std::function<void (bool)> setSimple;
        juce::Rectangle<float> modeArea() const { return { keyArea (keys().size() - 1).getRight() + 18.0f, 6.0f, 168.0f, (float) getHeight() - 13.0f }; }
        void paintMode (juce::Graphics& g)
        {
            if (! isSimple)
                return;
            const auto r = modeArea();
            g.setColour (juce::Colour (0xff101113));
            g.fillRoundedRectangle (r, 4.0f);
            const float slide = modeShown;   // (0 SIMPLE, 1 PRO: the cream half glides across)
            const auto thumb = r.reduced (2.0f).withWidth (r.getWidth() * 0.5f - 2.0f).translated (slide * (r.getWidth() * 0.5f - 2.0f), 0.0f);
            g.setColour (juce::Colour (0xffd6d3c7));
            g.fillRoundedRectangle (thumb, 3.0f);
            for (int i = 0; i < 2; ++i)
            {
                const auto half = r.withWidth (r.getWidth() * 0.5f).translated ((float) i * r.getWidth() * 0.5f, 0.0f);
                const bool on = (i == 1) != isSimple(), hover = half.contains (over) && ! on;
                if (hover)
                {
                    g.setColour (Console::Colours::accent().withAlpha (0.7f));
                    g.drawRoundedRectangle (half.reduced (2.0f), 3.0f, 1.2f);
                }
                g.setColour (on ? juce::Colour (0xff141517) : hover ? juce::Colours::white : juce::Colour (0xffc2c5c9));
                g.setFont (LargeFormat::print (12.0f));
                g.drawText (i == 0 ? "SIMPLE" : "PRO", half, juce::Justification::centred, false);
            }
        }
        void mouseMove (const juce::MouseEvent& e) override { over = e.position; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { over = { -1.0e6f, -1.0e6f }; repaint(); }
        void mouseUp (const juce::MouseEvent&) override { held = -1; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (isSimple && setSimple && modeArea().contains (e.position))
            {
                const bool simple = e.position.x < modeArea().getCentreX();
                if (simple != isSimple())
                    setSimple (simple);
                repaint();
                return;
            }
            auto& tape = engine.getTape();
            for (int i = 0; i < keys().size(); ++i)
                if (keyArea (i).contains (e.position))
                {
                    held = i;
                    const auto& k = keys()[i];
                    if (k == "REW")       tape.skip (-10.0);
                    else if (k == "STOP") tape.stop();
                    else if (k == "PLAY") tape.togglePlay();
                    else if (k == "FF")   tape.skip (10.0);
                    else if (k == "REC")  record();
                    else if (k == "LOOP") tape.repeat = tape.repeat == Tape::Repeat::all ? Tape::Repeat::one : tape.repeat == Tape::Repeat::one ? Tape::Repeat::off : Tape::Repeat::all;
                    repaint();
                }
        }

    private:
        static const juce::StringArray& keys()
        {
            static const juce::StringArray k { "REW", "STOP", "PLAY", "FF", "REC", "LOOP" };
            return k;
        }
        juce::Rectangle<float> screenArea() const { return { 8.0f, 6.0f, 300.0f, (float) getHeight() - 13.0f }; }
        juce::Rectangle<float> keyArea (int i) const { return { 318.0f + 46.0f * (float) i, 6.0f, 42.0f, (float) getHeight() - 13.0f }; }
        static juce::String clock (double seconds)
        {
            const int t = (int) seconds;
            return juce::String (t / 60).paddedLeft ('0', 2) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
        }

        // REC: the main output (the first) to a file, as the Tape page records; again, it stops.
        void record()
        {
            if (engine.getRecorder().isRecording())
                return engine.stopRecording();
            juce::String error;
            if (engine.getMasters().empty())
                error = "NO OUTPUT";
            else
                engine.startRecording (Recorder::Kind::output, 0, engine.getMasters().front()->name, engine.getRecorder().folder, error);
            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "REC", error);
        }

        void timerCallback() override
        {
            if (! isShowing())
                return;
            auto& tape = engine.getTape();
            const bool on[] = { false, false, tape.isPlaying(), false, engine.getRecorder().isRecording(), tape.repeat != Tape::Repeat::off };
            bool moved = false;
            if (isSimple)
                if (const float target = isSimple() ? 0.0f : 1.0f; modeShown != target)
                {
                    modeShown += (target - modeShown) * 0.35f;
                    if (std::abs (target - modeShown) < 0.02f)
                        modeShown = target;
                    moved = true;
                }
            for (int i = 0; i < 6; ++i)   // (the lamps easing on and off)
                if (const float target = on[i] ? 1.0f : 0.0f; lit[i] != target)
                {
                    lit[i] += (target - lit[i]) * 0.35f;
                    if (std::abs (target - lit[i]) < 0.02f)
                        lit[i] = target;
                    moved = true;
                }
            if (moved || ++ticks % 10 == 0)   // (the clock)
                repaint();
        }

        AudioEngine& engine;
        float lit[6] {};
        int ticks = 0, held = -1;
        float modeShown = 0.0f;
        juce::Point<float> over { -1.0e6f, -1.0e6f };
    };

    // The Mixer page: the strips in a view that scrolls both ways, and the plugin panel that slides
    // up under them (PLUGINS in the sidebar).
    class View : public juce::Component, private juce::Timer
    {
    public:
        View (AudioEngine& engine, MixerComponent& mixer, bool simpleMode, std::function<void (bool)> modeChanged, std::function<void()> openSetup)
            : bench (new Bench (layoutFile(), &engine, &mixer)), plugins (*bench), top (engine),
              simpleDesk (Simple::makeDesk (engine, mixer, std::move (openSetup))), onMode (std::move (modeChanged))
        {
            strips.setViewedComponent (bench, true);
            strips.setScrollBarsShown (true, true);
            addAndMakeVisible (strips);
            addChildComponent (*simpleDesk);
            addAndMakeVisible (top);   // (over the strips' top edge: they pass under it)
            addChildComponent (plugins);
            bench->onShowPlugins = [this] { showPlugins (true); };
            top.isSimple = [this] { return simple; };
            top.setSimple = [this] (bool s) { switchTo (s); };
            setSimple (simpleMode, false);
        }

        // SIMPLE (Simple.h) or the whole console (PRO): the same mix either way.
        bool isSimple() const { return simple; }
        void switchTo (bool s) { setSimple (s); if (onMode) onMode (s); }   // (as the top bar's switch: remembered)
        void setSimple (bool s, bool animate = true)
        {
            simple = s;
            strips.setVisible (! s);
            simpleDesk->setVisible (s);
            if (s)
                showPlugins (false);
            resized();
            if (animate)
                Console::riseIn (s ? *simpleDesk : (juce::Component&) strips);
            if (auto* p = getParentComponent())
                p->repaint();
        }

        void resized() override
        {
            auto r = getLocalBounds();
            const int h = simple ? 0 : juce::roundToInt (shown * (float) panelHeight);   // (SIMPLE has no plugin panel)
            plugins.setVisible (h > 0);
            plugins.setBounds (r.removeFromBottom (h).withHeight (panelHeight));
            strips.setBounds (r);
            simpleDesk->setBounds (r.withTrimmedTop (40));
            top.setBounds (r.getX(), r.getY(), simple ? r.getWidth() : strips.getMaximumVisibleWidth(), 40);
        }
        juce::Rectangle<int> viewArea() const { return strips.getViewArea(); }
        juce::Viewport& stripsView() { return strips; }

        bool pluginsOpen() const { return open; }
        void showPlugins (bool on)
        {
            open = on;
            startTimerHz (60);   // (it glides open and shut)
        }

        Bench* bench;

    private:
        void timerCallback() override
        {
            const float target = open ? 1.0f : 0.0f;
            shown += (target - shown) * 0.25f;
            if (std::abs (target - shown) < 0.01f)
            {
                shown = target;
                stopTimer();
            }
            resized();
        }

        static constexpr int panelHeight = 240;
        juce::Viewport strips;
        PluginPanel plugins;
        TopBar top;
        std::unique_ptr<juce::Component> simpleDesk;
        std::function<void (bool)> onMode;
        bool simple = false;
        bool open = false;
        float shown = 0.0f;
    };

    std::unique_ptr<juce::Component> make (AudioEngine& engine, MixerComponent& mixer, bool simple, std::function<void (bool)> modeChanged, std::function<void()> openSetup)
    {
        return std::make_unique<View> (engine, mixer, simple, std::move (modeChanged), std::move (openSetup));
    }

    void setSimple (juce::Component& console, bool simple)
    {
        if (auto* view = dynamic_cast<View*> (&console))
            view->setSimple (simple);
    }

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

    std::unique_ptr<juce::Component> makeRouting (juce::Component& console)
    {
        return std::make_unique<RoutingView> (dynamic_cast<View&> (console));
    }

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
            heading (g, "METER", meterTitle());
            pill (g, pairArea (meterTitle(), 0), "IN", b.meterIn(), hovered == 120, pushed == 120);
            pill (g, pairArea (meterTitle(), 1), "OUT", ! b.meterIn(), hovered == 121, pushed == 121);
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
            else if ((i == 120 || i == 121) && view != nullptr)
                view->bench->setMeterIn (i == 120);
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
                {
                    if (pairArea (selectTitle(), i).contains (p)) return 110 + i;
                    if (pairArea (meterTitle(), i).contains (p))  return 120 + i;
                }
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
        }

        juce::TabbedComponent& tabs;
        View* view;
        bool shownSimple = false;
        int hovered = -1, pushed = -1, shown = -1, shownSections = -1;
    };

    std::unique_ptr<juce::Component> makeSidebar (juce::TabbedComponent& tabs, juce::Component& console)
    {
        return std::make_unique<Sidebar> (tabs, console);
    }

    bool handleKey (juce::Component& console, const juce::KeyPress& key)
    {
        auto* view = dynamic_cast<View*> (&console);
        return view != nullptr && ! view->isSimple() && view->bench->keyPressed (key);   // (SIMPLE: the PRO desk's keys would reach strips not shown)
    }

    void select (juce::Component& console, const juce::String& key, bool plugins)
    {
        if (auto* view = dynamic_cast<View*> (&console))
        {
            if (plugins && view->isSimple())
                view->switchTo (false);   // (plugins are PRO's: it opens on them)
            view->bench->selectKey (key);
            if (plugins)
                view->showPlugins (true);
        }
    }

    void renamed (juce::Component& console, const juce::String& kind, const juce::String& oldName, const juce::String& newName)
    {
        if (auto* view = dynamic_cast<View*> (&console))
            view->bench->followRename (kind, oldName, newName);
    }
}

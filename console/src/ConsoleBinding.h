#pragma once

// Part of the console: Console.cpp includes it, and nothing else does.
// Binding: what each part of a strip drives in the engine.

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../src/engine/AudioEngine.h"
#include "DeskOrder.h"

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
                if (places.empty() || places[0].candidate.isEmpty()) return TRANS ("NO DEVICE");
                for (auto& out : engine->outputChoices())
                    if (out.candidate == places[0].candidate && out.firstChannel / 2 == places[0].firstChannel / 2)
                        return out.label.upToFirstOccurrenceOf (" (", false, false).toUpperCase();
                const auto device = places[0].candidate.upToFirstOccurrenceOf (" (", false, false).toUpperCase();
                return places[0].target.isEmpty() ? TRANS ("%1 (OFF)").replace ("%1", device) : device;
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
        if (id == "range") return p->getValue() <= 0.001f ? TRANS ("SILENT") : db (v, 0);
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

    // The level meter's value on the selected standard (SETTINGS > Audio > Meter standard), in dB on
    // its own axis, measured by the engine where the meter point says. (Without an engine: a level
    // a little under the scale's 0, to draw by.)
    float meterDb (int ch, int side) const
    {
        if (auto* m = master (ch))
            return Metering::toDb (side == 0 ? m->levels.meterL.load() : m->levels.meterR.load());
        if (auto* s = strip (ch))
            return Metering::toDb (side == 0 ? s->levels.meterL.load() : s->levels.meterR.load());
        return Metering::standard().zeroDb - (side == 0 ? 3.0f : 6.0f);
    }

    // (a strip's meter reads where the engine's meter point says: AudioEngine::meterPoint)
    float levelDb (int ch, int side) const
    {
        if (auto* m = master (ch))
            return juce::Decibels::gainToDecibels (side == 0 ? m->peakL.load() : m->peakR.load(), -100.0f);
        if (auto* s = strip (ch))
            return juce::Decibels::gainToDecibels (side == 0 ? s->peakL.load() : s->peakR.load(), -100.0f);
        return side == 0 ? -12.0f : -15.0f;
    }

    // DUCK UNDER: dipped now by more than a dB under the input it ducks under.
    bool ducked (int ch) const
    {
        auto* s = master (ch) == nullptr ? strip (ch) : nullptr;
        return s != nullptr && s->duckGain.load() < juce::Decibels::decibelsToGain (-1.0f);
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
            { "pre", "inputOn" }, { "listen", "keyListen" }, { "pk", "compPeak" }, { "noise", "nrOn" } };
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

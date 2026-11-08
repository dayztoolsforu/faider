#include "Simple.h"
#include "DeskOrder.h"
#include "../../src/engine/AudioEngine.h"
#include "../../src/engine/AppCapture.h"
#include "../../src/devices/EndpointWatcher.h"
#include "../../src/ui/MixerComponent.h"
#include "../../src/ui/Theme.h"
#include "../../src/ui/LargeFormat.h"
#include "../../src/ui/Console.h"
#include "../../src/ui/DeskSettings.h"
#include "../../src/ui/MidiMap.h"
#include <set>

namespace
{
    // The console's fader law, as printed beside its faders: 12, 6, 0, 5, 10, 20, 30, 40, 50 evenly
    // down, then off.
    float faderGain (float pos)
    {
        static const float marks[] = { 12.0f, 6.0f, 0.0f, -5.0f, -10.0f, -20.0f, -30.0f, -40.0f, -50.0f };
        const float f = (1.0f - juce::jlimit (0.0f, 1.0f, pos)) * 9.0f;
        if (f >= 8.999f)
            return 0.0f;
        const int i = (int) f;
        const float db = i >= 8 ? -50.0f - (f - 8.0f) * 40.0f : marks[i] + (marks[i + 1] - marks[i]) * (f - (float) i);
        return juce::Decibels::decibelsToGain (db);
    }
    float faderPos (float gain)
    {
        if (gain <= 0.0f)
            return 0.0f;
        float lo = 0.0f, hi = 1.0f;
        for (int i = 0; i < 24; ++i)
        {
            const float mid = (lo + hi) * 0.5f;
            (faderGain (mid) < gain ? lo : hi) = mid;
        }
        return (lo + hi) * 0.5f;
    }

    juce::String dbText (float gain)
    {
        if (gain <= 0.0f)
            return "-INF";
        const float db = juce::Decibels::gainToDecibels (gain);
        return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
    }

    // What a channel listens to, in a few words.
    juce::String sourceText (const AudioEngine& engine, const AudioEngine::Strip& s)
    {
        using K = AudioEngine::Source;
        switch (s.source.kind)
        {
            case K::device:
            {
                if (s.source.deviceName == AudioEngine::windowsDefault)
                    return "WINDOWS DEFAULT";
                for (auto& e : engine.getEndpoints())
                    if (e.name == s.source.deviceName)
                        return endpointDisplayName (e).toUpperCase();
                return s.source.deviceName.upToFirstOccurrenceOf (" (", false, false).toUpperCase();
            }
            case K::interfaceInputs: return "IN " + juce::String (s.source.firstChannel + 1) + (s.source.stereo ? "-" + juce::String (s.source.firstChannel + 2) : juce::String());
            case K::app:     return "APP: " + juce::File (s.source.deviceName).getFileNameWithoutExtension().toUpperCase();
            case K::pads:    return "PADS";
            case K::tape:    return "TAPE";
            case K::tone:    return "TEST TONE";
            case K::loopback:
            case K::bus:     return "AN OUTPUT'S SOUND";
            case K::none:    break;
        }
        return "NOTHING";
    }

    // The device an output plays on, as the console names it.
    juce::String deviceText (AudioEngine& engine, const AudioEngine::Master& m)
    {
        const auto places = m.places();
        if (places.empty() || places[0].candidate.isEmpty())
            return "NO DEVICE";
        for (auto& out : engine.outputChoices())
            if (out.candidate == places[0].candidate && out.firstChannel / 2 == places[0].firstChannel / 2)
                return out.label.upToFirstOccurrenceOf (" (", false, false).toUpperCase();
        if (places[0].candidate == AudioEngine::windowsDefault)
            return "WINDOWS DEFAULT";
        return places[0].candidate.upToFirstOccurrenceOf (" (", false, false).toUpperCase() + (places[0].target.isEmpty() ? " (OFF)" : "");
    }

    // The outputs a channel reaches through its buses (and theirs), not counting its own sends.
    std::set<int> reachedThroughBuses (const AudioEngine& engine, const AudioEngine::Strip& s)
    {
        std::set<int> masters, seen;
        std::function<void (const AudioEngine::Strip&)> walk = [&] (const AudioEngine::Strip& from)
        {
            auto& buses = engine.getBuses();
            for (int p = 0; p < (int) buses.size(); ++p)
                if (from.busSends[(size_t) p].load() && seen.insert (p).second)
                {
                    for (int m = 0; m < (int) engine.getMasters().size(); ++m)
                        if (buses[(size_t) p]->sends[(size_t) m].load())
                            masters.insert (m);
                    walk (*buses[(size_t) p]);
                }
        };
        walk (s);
        return masters;
    }

    juce::String uniqueName (const juce::StringArray& taken, const juce::String& base)
    {
        auto name = base;
        for (int i = 2; taken.contains (name); ++i)
            name = base + " " + juce::String (i);
        return name;
    }

    // The line under a device's name: its adapter, by the name given it on DEVICES if any; a device
    // that has a name of its own but an adapter without one, just what it is.
    juce::String adapterText (const EndpointInfo& e, const juce::String& kind)
    {
        if (const auto alias = deviceAlias (e.adapter); alias.isNotEmpty())
            return alias;
        return deviceAlias (e.name).isNotEmpty() ? kind : e.adapter;
    }

    bool isCable (const EndpointInfo& e, const juce::Array<EndpointInfo>& all)
    {
        return virtualMicPartner (e, all).isNotEmpty() || e.name.containsIgnoreCase ("Voicemeeter")
            || e.adapter.containsIgnoreCase ("Virtual Cable") || e.adapter.containsIgnoreCase ("VB-Audio");
    }

    //==============================================================================
    // THE SIMPLE DESK. Everything is drawn here and found again by where it was drawn (hits).
    class Desk : public juce::Component, public MidiMap::Provider, private juce::Timer
    {
    public:
        // MIDI MAP: each channel's and output's fader, CUT, SOLO, power and route keys, where they're drawn.
        std::vector<std::pair<juce::Rectangle<int>, juce::String>> midiTargets() override
        {
            std::vector<std::pair<juce::Rectangle<int>, juce::String>> list;
            for (auto& h : hits)
            {
                const auto* s = stripAt (h.index);
                const auto* m = masterAt (h.index);
                const auto in = s != nullptr ? "input:" + s->name : juce::String();
                const auto out = m != nullptr ? "output:" + m->name : juce::String();
                juce::String target;
                switch (h.kind)
                {
                    case Hit::fader:    if (s) target = "fader:" + in; break;
                    case Hit::cut:      if (s) target = "mute:" + in; break;
                    case Hit::solo:     if (s) target = "solo:" + in; break;
                    case Hit::power:    if (s) target = "on:" + in; break;
                    case Hit::route:    if (s && masterAt (h.target)) target = "send:" + in + ":output:" + masterAt (h.target)->name; break;
                    case Hit::outFader: if (m) target = "fader:" + out; break;
                    case Hit::outCut:   if (m) target = "mute:" + out; break;
                    case Hit::outPower: if (m) target = "on:" + out; break;
                    default: break;
                }
                if (target.isNotEmpty())
                    list.push_back ({ h.r.toNearestInt(), target });
            }
            return list;
        }

        Desk (AudioEngine& e, MixerComponent& m, std::function<void()> setup) : engine (e), mixer (m), openSetup (std::move (setup))
        {
            startTimerHz (30);
        }

        // The width the strips need (the view scrolls across beyond it).
        int idealWidth() const
        {
            const int inputs = (int) engine.getStrips().size(), outputs = (int) engine.getMasters().size();
            return pad + inputs * (stripW + gap) + addW + groupGap + outputs * (outW + gap) + addW + pad;
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (DeskSettings::desk());
            hits.clear();
            order.sync (engine);   // (the outputs in the desk's order, as PRO shows them: DeskOrder.h)
            auto& strips = engine.getStrips();
            auto& masters = engine.getMasters();
            if (strips.empty() && masters.empty())
                return paintEmpty (g);

            // Every strip's fader, meter and scale start on one line, under the tallest strip's top
            // part (a channel's routes, an output's device), so they read across the desk.
            body = nameH + sourceH + routesH (juce::jmax ((size_t) 1, masters.size())) + 8.0f;
            int x = pad;
            heading (g, "CHANNELS", x);
            for (int i = 0; i < (int) strips.size(); ++i, x += stripW + gap)
                paintChannel (g, i, juce::Rectangle<int> (x, top, stripW, getHeight() - top - pad));
            addKeys (g, { x, top, addW, getHeight() - top - pad }, { { "+ CHANNEL", Hit::addChannel }, { "+ APP", Hit::addApp } });
            x += addW + groupGap;
            heading (g, "OUTPUTS", x);
            for (int place = 0; place < (int) masters.size(); ++place, x += outW + gap)
                paintOutput (g, order.output (place), juce::Rectangle<int> (x, top, outW, getHeight() - top - pad));
            addKeys (g, { x, top, addW, getHeight() - top - pad }, { { "+ OUTPUT", Hit::addOutput } });
        }

        void mouseMove (const juce::MouseEvent& e) override { setOver (e.position); }
        void mouseExit (const juce::MouseEvent&) override { setOver ({ -1.0e6f, -1.0e6f }); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            pressed = hitAt (e.position);
            if (pressed < 0)
                return;
            pressedShape = shape();
            const auto h = hits[(size_t) pressed];
            if (e.mods.isPopupMenu())
            {
                pressed = -1;
                if (h.kind == Hit::name || h.kind == Hit::outName)
                    showMenu (h);
                return;
            }
            if (h.kind == Hit::fader || h.kind == Hit::outFader)
            {
                dragFrom = faderPos (gainOf (h));
                dragGain (h, e);
            }
            repaint();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (pressed >= 0 && shape() != pressedShape)
                pressed = -1;   // (a channel or output came or went under the drag: its places moved)
            if (pressed >= 0 && pressed < (int) hits.size() && (hits[(size_t) pressed].kind == Hit::fader || hits[(size_t) pressed].kind == Hit::outFader))
                dragGain (hits[(size_t) pressed], e);
        }
        // How many channels and outputs there are: a press is only good while that holds.
        int shape() const { return (int) engine.getStrips().size() * 1000 + (int) engine.getMasters().size(); }
        int pressedShape = 0;
        void mouseUp (const juce::MouseEvent& e) override
        {
            const int was = std::exchange (pressed, -1);
            if (was >= 0 && was < (int) hits.size() && shape() == pressedShape && hits[(size_t) was].r.contains (e.position))
                act (hits[(size_t) was]);
            repaint();
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (const int i = hitAt (e.position); i >= 0 && (hits[(size_t) i].kind == Hit::fader || hits[(size_t) i].kind == Hit::outFader))
                setGain (hits[(size_t) i], 1.0f);   // (back to 0 dB)
        }

    private:
        struct Hit
        {
            enum Kind { name, power, route, fader, cut, solo, outName, outPower, outFader, outCut, outDevice, addChannel, addApp, addOutput, setup };
            juce::Rectangle<float> r;
            Kind kind;
            int index = -1;   // the channel's or output's
            int target = -1;  // route: the output
        };

        static constexpr int pad = 14, top = 34, stripW = 150, outW = 160, gap = 8, groupGap = 26, addW = 110;
        static constexpr float nameH = 34.0f, sourceH = 26.0f, routeH = 36.0f;
        static float routesH (size_t outputs) { return 4.0f + routeH * (float) outputs; }
        float body = 0.0f;   // (where the faders start, from the strips' top: paint works it out)

        void heading (juce::Graphics& g, const juce::String& text, int x)
        {
            g.setColour (DeskSettings::text());
            g.setFont (LargeFormat::print (12.0f));
            g.drawText (text, x, 10, 200, 18, juce::Justification::centredLeft, false);
        }

        // Touch, as everywhere: over, held.
        float overOf (const juce::Rectangle<float>& r) const { return r.contains (over) ? 1.0f : 0.0f; }
        float pressOf (const juce::Rectangle<float>& r) const
        {
            return pressed >= 0 && pressed < (int) hits.size() && hits[(size_t) pressed].r == r && r.contains (over) ? 1.0f : 0.0f;
        }
        void key (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, float lit, juce::Colour lamp, Hit hit, bool enabled = true)
        {
            hit.r = r;
            hits.push_back (hit);
            LargeFormat::key (g, r, text, lit, lamp, enabled, pressOf (r) * 1.5f, overOf (r));
        }

        // A route: off, a plain grey key; on, filled in the output's colour with a dark legend; through a
        // bus only, outlined in its colour with VIA BUS beside the name. Every state reads at full strength.
        void routeKey (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool on, bool viaBus, juce::Colour colour, Hit hit)
        {
            hit.r = r;
            hits.push_back (hit);
            const float hover = overOf (r), press = pressOf (r);
            const auto box = r.translated (0.0f, press * 1.5f);
            auto fill = on ? colour : viaBus ? juce::Colour (0xff1d2124).interpolatedWith (colour, 0.18f) : juce::Colour (0xff2e3034);
            fill = fill.brighter (0.18f * hover).darker (0.4f * press);
            g.setColour (juce::Colours::black.withAlpha (0.5f * (1.0f - press)));
            g.fillRoundedRectangle (r.translated (0.0f, 1.5f), 3.0f);
            g.setColour (fill);
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (hover > 0.0f ? Console::Colours::accent() : on || viaBus ? colour : Console::Colours::chromeOn());
            g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, hover > 0.0f || viaBus ? 1.6f : 1.0f);
            auto label = box.reduced (8.0f, 0.0f);
            if (viaBus && ! on)
            {
                g.setColour (colour.brighter (0.2f));
                g.setFont (LargeFormat::print (9.0f));
                g.drawText ("VIA BUS", label.removeFromRight (44.0f), juce::Justification::centredRight, false);
            }
            g.setColour (on ? Console::Colours::litInk() : viaBus ? colour.brighter (0.35f) : Console::Colours::inkBright());
            g.setFont (LargeFormat::print (12.0f));
            g.drawFittedText (text, label.toNearestInt(), viaBus && ! on ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.8f);
        }

        void nameBar (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour colour, juce::Colour kindColour, bool on, Hit::Kind nameKind, Hit::Kind powerKind, int index)
        {
            g.setColour (Console::Colours::stripBody());
            g.fillRect (r);
            const auto band = colour.isTransparent() ? kindColour : colour;
            g.setColour (band.withAlpha (0.25f));
            g.fillRect (r);
            g.setColour (band);
            g.fillRect (r.withHeight (4.0f));
            const auto icon = r.withWidth (r.getHeight()).reduced (r.getHeight() * 0.3f).translated (2.0f, 1.0f);
            const auto powerArea = r.withWidth (r.getHeight());
            hits.push_back ({ powerArea, powerKind, index });
            hits.push_back ({ r.withTrimmedLeft (r.getHeight()), nameKind, index });
            if (overOf (powerArea) > 0.0f)
            {
                g.setColour (Console::Colours::accent().withAlpha (0.3f));
                g.fillEllipse (icon.expanded (icon.getWidth() * 0.45f));
            }
            juce::Path power;
            power.addCentredArc (icon.getCentreX(), icon.getCentreY(), icon.getWidth() * 0.5f, icon.getHeight() * 0.5f, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.6f, true);
            g.setColour (on ? Console::Colours::palette ("orange") : juce::Colour (0xff9a9da2));
            g.strokePath (power, juce::PathStrokeType (1.8f));
            g.drawLine (icon.getCentreX(), icon.getY() - 1.0f, icon.getCentreX(), icon.getCentreY(), 1.8f);
            g.setColour (on ? juce::Colours::white : Console::Colours::inkFaint());
            g.setFont (LargeFormat::print (12.5f));
            g.drawFittedText (text, r.withTrimmedLeft (r.getHeight()).withTrimmedRight (6.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }

        void subline (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool asKey, Hit hit)
        {
            if (asKey)
                return key (g, r, text, 0.0f, Console::Colours::powerLamp(), hit);
            g.setColour (DeskSettings::text());
            g.setFont (LargeFormat::print (10.5f));
            g.drawFittedText (text, r.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }

        // The fader beside its meter, the level under them.
        // Left to right: the scale (its figures right-aligned against their ticks), the fader's slot with
        // the cap centred on it, the two meter bars; all three share one top and bottom.
        void faderAndMeter (juce::Graphics& g, juce::Rectangle<float> r, float gain, float peakL, float peakR, Hit hit)
        {
            auto meter = r.removeFromRight (18.0f);
            r.removeFromRight (6.0f);
            const auto scale = r.removeFromLeft (28.0f).reduced (0.0f, 14.0f);
            const auto travel = r.reduced (0.0f, 14.0f);
            const float slotX = travel.getCentreX();
            g.setColour (Console::Colours::rim());
            g.fillRoundedRectangle (juce::Rectangle<float> (6.0f, travel.getHeight()).withCentre ({ slotX, travel.getCentreY() }), 3.0f);
            g.setFont (LargeFormat::print (9.5f, false));
            for (auto [db, label] : { std::pair (12.0f, "+12"), std::pair (6.0f, "+6"), std::pair (0.0f, "0"), std::pair (-10.0f, "-10"), std::pair (-20.0f, "-20"),
                                      std::pair (-30.0f, "-30"), std::pair (-50.0f, "-50") })
            {
                const float y = travel.getBottom() - faderPos (juce::Decibels::decibelsToGain (db)) * travel.getHeight();
                g.setColour (DeskSettings::text().withAlpha (db == 0.0f ? 1.0f : 0.8f));
                g.fillRect (scale.getRight() - 5.0f, y - 0.5f, 5.0f, 1.0f);
                g.drawText (label, juce::Rectangle<float> (scale.getX(), y - 6.0f, scale.getWidth() - 8.0f, 12.0f), juce::Justification::centredRight, false);
            }
            const float y = travel.getBottom() - faderPos (gain) * travel.getHeight();
            const auto cap = juce::Rectangle<float> (juce::jmin (52.0f, travel.getWidth() * 0.8f), 28.0f).withCentre ({ slotX, y });
            hit.r = travel.expanded (0.0f, 14.0f);
            hits.push_back (hit);
            const bool moving = pressOf (hit.r) > 0.0f, hovering = overOf (hit.r) > 0.0f;
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (cap.translated (0.0f, 3.0f), 3.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd2d3d5), cap.getX(), cap.getY(), juce::Colour (0xff7d7f83), cap.getX(), cap.getBottom(), false));
            g.fillRoundedRectangle (cap, 3.0f);
            g.setColour (juce::Colours::black);
            g.fillRect (cap.withSizeKeepingCentre (cap.getWidth(), 2.0f));
            if (moving || hovering)
            {
                g.setColour (Console::Colours::accent().withAlpha (moving ? 0.95f : 0.6f));
                g.drawRoundedRectangle (cap.expanded (1.5f), 3.5f, 1.6f);
            }
            // The meter: two bars, -48 dB up, 3 dB a segment.
            const auto& t = Theme::get();
            const int count = 24;
            for (int side = 0; side < 2; ++side)
            {
                const auto bar = meter.withWidth (8.0f).translated ((float) side * 10.0f, 0.0f).reduced (0.0f, 14.0f);   // (as the scale and slot)
                const float pitch = bar.getHeight() / (float) count;
                const float db = juce::Decibels::gainToDecibels (side == 0 ? peakL : peakR, -100.0f);
                const int on = juce::jlimit (0, count, (int) ((db + 48.0f) / 2.0f) + (db > -100.0f ? 1 : 0));
                const int hold = heldPeak (hit.kind * 10000 + hit.index * 2 + side, on);   // PEAK HOLD, as the console's
                for (int i = 0; i < count; ++i)
                {
                    const auto colour = i >= count - 2 ? t.ledRed : (i >= count - 5 ? t.ledYellow : t.ledGreen);
                    g.setColour (i < on || i == hold - 1 ? colour : colour.withMultipliedBrightness (0.22f).withMultipliedSaturation (0.6f));
                    g.fillRect (bar.getX(), bar.getBottom() - pitch * (float) (i + 1) + 1.0f, bar.getWidth(), pitch - 2.0f);
                }
            }
        }

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

        void readout (juce::Graphics& g, juce::Rectangle<float> r, float gain)
        {
            g.setColour (Console::Colours::stopLamp().withMultipliedBrightness (0.09f));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (juce::Colour (0xffff5a44));
            g.setFont (LargeFormat::print (13.0f));
            g.drawText (dbText (gain), r, juce::Justification::centred, false);
        }

        void paintChannel (juce::Graphics& g, int i, juce::Rectangle<int> area)
        {
            auto& s = *engine.getStrips()[(size_t) i];
            auto r = area.toFloat();
            g.setColour (Console::Colours::bay());
            g.fillRoundedRectangle (r, 4.0f);
            nameBar (g, r.removeFromTop (nameH), s.name, s.colour, Console::Colours::kindInput(), s.enabled.load(), Hit::name, Hit::power, i);
            subline (g, r.removeFromTop (sourceH).reduced (6.0f, 4.0f), sourceText (engine, s), false, {});
            auto& masters = engine.getMasters();
            const auto viaBus = reachedThroughBuses (engine, s);
            auto routes = r.removeFromTop (routesH (masters.size()));
            routes.removeFromTop (4.0f);
            for (int place = 0; place < (int) masters.size(); ++place)
            {
                const int m = order.output (place);
                const auto lamp = masters[(size_t) m]->colour.isTransparent() ? Console::Colours::palette ("teal") : masters[(size_t) m]->colour;
                routeKey (g, routes.removeFromTop (routeH).reduced (8.0f, 3.0f), masters[(size_t) m]->name, s.sends[(size_t) m].load(), viaBus.count (m) > 0, lamp, { {}, Hit::route, i, m });
            }
            r.setTop ((float) area.getY() + body);
            auto foot = r.removeFromBottom (44.0f).reduced (8.0f, 6.0f);
            key (g, foot.removeFromLeft (foot.getWidth() * 0.5f - 3.0f), "CUT", s.mute.load() ? 1.0f : 0.0f, Console::Colours::stopLamp(), { {}, Hit::cut, i });
            foot.removeFromLeft (6.0f);
            key (g, foot, "SOLO", s.solo.load() ? 1.0f : 0.0f, juce::Colour (0xfff2c14e), { {}, Hit::solo, i });
            readout (g, r.removeFromBottom (26.0f).reduced (10.0f, 2.0f), s.gain.load());
            faderAndMeter (g, r.reduced (12.0f, 4.0f), s.gain.load(), s.peakL.load(), s.peakR.load(), { {}, Hit::fader, i });
            if (! s.enabled.load())   // OFF: dimmed, still readable, its power switch still lit to switch it back
            {
                g.setColour (Console::Colours::bayDark().withAlpha (0.5f));
                g.fillRoundedRectangle (area.toFloat().withTrimmedTop (34.0f), 4.0f);
            }
        }

        void paintOutput (juce::Graphics& g, int i, juce::Rectangle<int> area)
        {
            auto& m = *engine.getMasters()[(size_t) i];
            auto r = area.toFloat();
            g.setColour (juce::Colour (0xff26241f));
            g.fillRoundedRectangle (r, 4.0f);
            nameBar (g, r.removeFromTop (nameH), m.name, m.colour, Console::Colours::kindOutput(), m.enabled.load(), Hit::outName, Hit::outPower, i);
            subline (g, r.removeFromTop (sourceH).reduced (6.0f, 4.0f), "PLAYS ON", false, {});   // (level with a channel's source)
            r.removeFromTop (4.0f);
            subline (g, r.removeFromTop (routeH).reduced (8.0f, 3.0f), deviceText (engine, m), true, { {}, Hit::outDevice, i });   // (level with its first route)
            r.setTop ((float) area.getY() + body);
            auto foot = r.removeFromBottom (44.0f).reduced (8.0f, 6.0f);
            key (g, foot, "CUT", m.mute.load() ? 1.0f : 0.0f, Console::Colours::stopLamp(), { {}, Hit::outCut, i });
            readout (g, r.removeFromBottom (26.0f).reduced (10.0f, 2.0f), m.gain.load());
            faderAndMeter (g, r.reduced (12.0f, 4.0f), m.gain.load(), m.peakL.load(), m.peakR.load(), { {}, Hit::outFader, i });
            if (! m.enabled.load())
            {
                g.setColour (Console::Colours::bayDark().withAlpha (0.5f));
                g.fillRoundedRectangle (area.toFloat().withTrimmedTop (34.0f), 4.0f);
            }
        }

        void addKeys (juce::Graphics& g, juce::Rectangle<int> area, std::initializer_list<std::pair<const char*, Hit::Kind>> keys)
        {
            auto r = area.toFloat();
            for (auto& [text, kind] : keys)
                key (g, r.removeFromTop (nameH + 6.0f).withTrimmedBottom (6.0f), text, 0.0f, Console::Colours::powerLamp(), { {}, kind });   // (level with the name bars)
        }

        void paintEmpty (juce::Graphics& g)
        {
            g.setColour (DeskSettings::text());
            g.setFont (LargeFormat::print (16.0f));
            const auto c = getLocalBounds().toFloat().getCentre();
            g.drawText ("NO CHANNELS YET", juce::Rectangle<float> (300.0f, 24.0f).withCentre (c.translated (0.0f, -30.0f)), juce::Justification::centred, false);
            key (g, juce::Rectangle<float> (160.0f, 36.0f).withCentre (c.translated (0.0f, 12.0f)), "SETUP", 0.0f, Console::Colours::accent(), { {}, Hit::setup });
        }

        int hitAt (juce::Point<float> p) const
        {
            for (int i = (int) hits.size(); --i >= 0;)
                if (hits[(size_t) i].r.contains (p))
                    return i;
            return -1;
        }
        void setOver (juce::Point<float> p)
        {
            const int before = hitAt (over), now = hitAt (p);
            over = p;
            if (before != now || pressed >= 0)
                repaint();
        }

        AudioEngine::Strip* stripAt (int i) const { auto& all = engine.getStrips(); return juce::isPositiveAndBelow (i, (int) all.size()) ? all[(size_t) i].get() : nullptr; }
        AudioEngine::Master* masterAt (int i) const { auto& all = engine.getMasters(); return juce::isPositiveAndBelow (i, (int) all.size()) ? all[(size_t) i].get() : nullptr; }

        float gainOf (const Hit& h) const
        {
            if (h.kind == Hit::fader) if (auto* s = stripAt (h.index)) return s->gain.load();
            if (h.kind == Hit::outFader) if (auto* m = masterAt (h.index)) return m->gain.load();
            return 1.0f;
        }
        void setGain (const Hit& h, float gain)
        {
            if (h.kind == Hit::fader) { if (auto* s = stripAt (h.index)) s->gain = gain; }
            else if (h.kind == Hit::outFader) { if (auto* m = masterAt (h.index)) m->gain = gain; }
            repaint();
        }
        void dragGain (const Hit& h, const juce::MouseEvent& e)
        {
            const float travel = juce::jmax (40.0f, h.r.getHeight() - 28.0f);
            setGain (h, faderGain (dragFrom - (float) e.getDistanceFromDragStartY() / travel));
        }

        void act (const Hit& h)
        {
            switch (h.kind)
            {
                case Hit::power:   if (auto* s = stripAt (h.index)) { engine.setStripEnabled (*s, ! s->enabled.load()); engine.refreshDevices(); engine.refreshDucking(); } break;
                case Hit::outPower: if (auto* m = masterAt (h.index)) { engine.setMasterEnabled (h.index, ! m->enabled.load()); engine.refreshDevices(); } break;
                case Hit::route:   if (auto* s = stripAt (h.index); s != nullptr && juce::isPositiveAndBelow (h.target, AudioEngine::maxMasters)) s->sends[(size_t) h.target] = ! s->sends[(size_t) h.target].load(); break;
                case Hit::cut:     if (auto* s = stripAt (h.index)) s->mute = ! s->mute.load(); break;
                case Hit::solo:    if (auto* s = stripAt (h.index)) s->solo = ! s->solo.load(); break;
                case Hit::outCut:  if (auto* m = masterAt (h.index)) m->mute = ! m->mute.load(); break;
                case Hit::outDevice: showDevices (h); break;
                case Hit::addChannel: mixer.showAddInputMenu (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (h.r.toNearestInt()))); break;
                case Hit::addApp:  showApps (h); break;
                case Hit::addOutput: showNewOutput (h); break;
                case Hit::setup:   if (openSetup) openSetup(); break;
                case Hit::name: case Hit::outName: case Hit::fader: case Hit::outFader: break;
            }
            repaint();
        }

        juce::PopupMenu::Options near (const Hit& h) { return juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (h.r.toNearestInt())); }

        // An output's device: any of the devices the console lists (the interface's pairs, Windows' playback devices).
        void showDevices (const Hit& h)
        {
            auto* m = masterAt (h.index);
            if (m == nullptr)
                return;
            juce::PopupMenu menu;
            menu.addSectionHeader ("PLAYS ON");
            const auto places = m->places();
            for (auto& out : engine.outputChoices())
            {
                const bool here = ! places.empty() && places[0].candidate == out.candidate && places[0].firstChannel / 2 == out.firstChannel / 2;
                menu.addItem (out.label.toUpperCase(), true, here, [this, index = h.index, out]
                {
                    if (auto* again = masterAt (index))
                        engine.setMasterOutput (index, { out.candidate }, out.firstChannel, again->stereo);
                    engine.refreshDevices();
                    repaint();
                });
            }
            menu.showMenuAsync (near (h));
        }

        // + OUTPUT: a device that has no output yet gets one.
        void showNewOutput (const Hit& h)
        {
            juce::PopupMenu menu;
            menu.addSectionHeader ("NEW OUTPUT ON");
            for (auto& out : engine.outputChoices())
                if (engine.masterPlayingOn (out.candidate, out.firstChannel) < 0)
                    menu.addItem (out.label.toUpperCase(), (int) engine.getMasters().size() < AudioEngine::maxMasters, false, [this, out]
                    {
                        // Named after its device, short (without the adapter in brackets).
                        if (const int m = engine.masterFor (out); juce::isPositiveAndBelow (m, (int) engine.getMasters().size()))
                        {
                            juce::StringArray taken;
                            for (int i = 0; i < (int) engine.getMasters().size(); ++i)
                                if (i != m) taken.add (engine.getMasters()[(size_t) i]->name);
                            engine.getMasters()[(size_t) m]->name = uniqueName (taken, out.label.upToFirstOccurrenceOf (" (", false, false).trim().toUpperCase());
                        }
                        engine.refreshDevices();
                        repaint();
                    });
            if (menu.getNumItems() <= 1)
                menu.addItem ("EVERY DEVICE HAS AN OUTPUT", false, false, [] {});
            menu.showMenuAsync (near (h));
        }

        // + APP: an app playing sound now, as a channel of its own.
        void showApps (const Hit& h)
        {
            juce::StringArray exes;
            for (auto& a : listAudioApps())
                exes.addIfNotAlreadyThere (a.exe, true);
            exes.sortNatural();
            juce::PopupMenu menu;
            menu.addSectionHeader ("APPS PLAYING");
            for (auto& exe : exes)
            {
                const auto label = juce::File (exe).getFileNameWithoutExtension();
                const bool there = std::any_of (engine.getStrips().begin(), engine.getStrips().end(), [&exe] (auto& s)
                                                { return s->source.kind == AudioEngine::Source::app && s->source.deviceName.equalsIgnoreCase (exe); });
                menu.addItem (label.toUpperCase() + (there ? "  (ON THE DESK)" : ""), ! there, false, [this, exe, label]
                {
                    juce::StringArray taken;
                    for (auto& s : engine.getStrips()) taken.add (s->name);
                    const auto name = uniqueName (taken, label.toUpperCase());
                    engine.addStrip (name, { AudioEngine::Source::app, 0, exe, true });
                    engine.refreshDevices();
                    engine.logEvent ("input " + name + ": the app " + exe + " as a channel");
                    repaint();
                });
            }
            if (exes.isEmpty())
                menu.addItem ("NO APPS PLAYING", false, false, [] {});
            menu.showMenuAsync (near (h));
        }

        // A name bar's right-click: rename, colour, remove.
        void showMenu (const Hit& h)
        {
            const bool output = h.kind == Hit::outName;
            juce::PopupMenu menu;
            menu.addItem ("RENAME...", [this, output, index = h.index] { rename (output, index); });
            const auto colour = output ? (masterAt (h.index) != nullptr ? masterAt (h.index)->colour : juce::Colour())
                                       : (stripAt (h.index) != nullptr ? stripAt (h.index)->colour : juce::Colour());
            MixerComponent::addColourMenu (menu, colour, [this, output, index = h.index] (juce::Colour c)
            {
                if (output) { if (auto* m = masterAt (index)) m->colour = c; }
                else if (auto* s = stripAt (index)) s->colour = c;
                repaint();
            });
            menu.addSeparator();
            menu.addItem ("REMOVE", [this, output, index = h.index]
            {
                if (output)
                    engine.removeMaster (index);
                else if (stripAt (index) != nullptr)
                {
                    mixer.closeEditorsFor ({ MixerComponent::InsertTarget::strip, index });   // (its plugins' windows, before the plugins go)
                    engine.forgetAppRule (index);
                    engine.removeStrip (index);
                }
                engine.refreshDevices();
                repaint();
            });
            menu.showMenuAsync (near (h));
        }

        void rename (bool output, int index)
        {
            const auto now = output ? (masterAt (index) != nullptr ? masterAt (index)->name : juce::String()) : (stripAt (index) != nullptr ? stripAt (index)->name : juce::String());
            auto* w = new juce::AlertWindow ("RENAME", {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", now);
            w->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, output, index] (int result)
            {
                const auto wanted = w->getTextEditorContents ("name").trim();
                if (result == 1 && wanted.isNotEmpty())
                {
                    // As PRO renames: a name no other has, and everything that finds it by name follows.
                    if (output)
                    {
                        if (auto* m = masterAt (index))
                        {
                            const auto old = m->name;
                            m->name = engine.nameFor ("output", wanted, m);
                            engine.renamed ("output", old, m->name);
                        }
                    }
                    else if (auto* s = stripAt (index))
                    {
                        const auto old = s->name;
                        s->name = engine.nameFor ("input", wanted, s);
                        engine.renamed ("input", old, s->name);
                    }
                    repaint();
                }
            }), true);
        }

        void timerCallback() override
        {
            if (! isShowing())
                return;
            if (const int w = idealWidth(); w != getWidth())
                setSize (w, getHeight());
            repaint();   // (the meters)
        }

        AudioEngine& engine;
        MixerComponent& mixer;
        std::function<void()> openSetup;
        std::vector<Hit> hits;
        DeskOrder order;
        juce::Point<float> over { -1.0e6f, -1.0e6f };
        int pressed = -1;
        float dragFrom = 0.0f;
    };

    // The desk in a view that scrolls across.
    class DeskView : public juce::Component
    {
    public:
        DeskView (AudioEngine& e, MixerComponent& m, std::function<void()> setup) : desk (e, m, std::move (setup))
        {
            view.setViewedComponent (&desk, false);
            view.setScrollBarsShown (false, true);
            addAndMakeVisible (view);
        }
        void resized() override
        {
            view.setBounds (getLocalBounds());
            desk.setSize (desk.idealWidth(), juce::jmax (420, getHeight() - view.getScrollBarThickness()));
        }
        void paint (juce::Graphics& g) override { g.fillAll (DeskSettings::desk()); }

    private:
        juce::Viewport view;
        Desk desk;
    };

    //==============================================================================
    // SETUP: a question a page, the answers as cards (one or several), then the mix built.
    class Setup : public juce::Component
    {
    public:
        Setup (AudioEngine& e, std::function<void()> done) : engine (e), finished (std::move (done))
        {
            for (auto* b : { &back, &next })
            {
                addAndMakeVisible (b);
                b->setWantsKeyboardFocus (false);
            }
            back.onClick = [this] { if (step > 0) show (step - 1); };
            next.onClick = [this]
            {
                if (step == apps)
                    build();
                if (step == ready)
                    return finished ? finished() : void();
                show (step + 1);
            };
        }

        void visibilityChanged() override
        {
            if (isVisible())
                show (0);   // (the questions start again with what's there now)
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (DeskSettings::desk());
            static const char* names[] = { "WHERE YOU LISTEN", "MICROPHONES", "APPS HEAR YOU", "PROGRAMS", "DONE" };
            for (int i = 0; i <= ready; ++i)
            {
                const auto r = stepCard (i).toFloat();
                DeskSettings::card (g, r, i == step, false);
                g.setColour (i < step ? Theme::get().ledGreen : i == step ? juce::Colour (0xfff2c14e) : juce::Colour (0xff55585d));
                g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ r.getX() + 18.0f, r.getCentreY() }));
                g.setColour (i == step ? juce::Colours::white : DeskSettings::text());
                g.setFont (LargeFormat::print (12.0f));
                g.drawText (names[i], r.withTrimmedLeft (32.0f), juce::Justification::centredLeft, false);
            }
            const auto pane = paneArea();
            DeskSettings::card (g, pane.toFloat());
            g.setColour (juce::Colours::white);
            g.setFont (LargeFormat::print (17.0f));
            g.drawText (question(), pane.reduced (20).removeFromTop (30), juce::Justification::centredLeft, false);
            if (step == ready)
                return paintSummary (g, pane.reduced (20).withTrimmedTop (44));
            for (int i = 0; i < (int) choices.size(); ++i)
            {
                const auto r = choiceArea (i).toFloat();
                const auto& c = choices[(size_t) i];
                const bool hover = r.contains (over) && ! c.fixed;
                g.setColour (c.on ? juce::Colour (0xff22303d) : hover ? juce::Colour (0xff2c2f33) : juce::Colour (0xff1d1f22));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (c.on ? Console::Colours::accent() : hover ? Console::Colours::accent().withAlpha (0.6f) : juce::Colour (0xff3a3d42));
                g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, c.on ? 1.6f : 1.0f);
                g.setColour (c.on ? juce::Colours::white : DeskSettings::text());
                g.setFont (LargeFormat::print (12.5f));
                g.drawFittedText (c.title, r.reduced (12.0f, 0.0f).withTrimmedTop (8.0f).withHeight (20.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
                g.setColour (DeskSettings::text().withAlpha (0.85f));
                g.setFont (juce::FontOptions (12.0f));
                g.drawFittedText (c.detail, r.reduced (12.0f, 0.0f).withTrimmedTop (30.0f).withHeight (18.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
            }
            if (choices.empty())
            {
                g.setColour (DeskSettings::text());
                g.setFont (LargeFormat::print (12.5f));
                g.drawText (step == apps ? "NO APPS PLAYING" : "NONE FOUND", pane.reduced (20).withTrimmedTop (50).removeFromTop (24), juce::Justification::centredLeft, false);
            }
        }

        void resized() override
        {
            auto keys = paneArea().reduced (20).removeFromBottom (34);
            next.setBounds (keys.removeFromRight (180));
            keys.removeFromRight (8);
            back.setBounds (keys.removeFromRight (120));
        }

        void mouseMove (const juce::MouseEvent& e) override { over = e.position; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { over = { -1.0e6f, -1.0e6f }; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            for (int i = 0; i <= ready; ++i)
                if (stepCard (i).contains (e.getPosition()) && i < step)
                    return show (i);
            for (int i = 0; i < (int) choices.size(); ++i)
                if (choiceArea (i).contains (e.getPosition()) && ! choices[(size_t) i].fixed)
                {
                    auto& c = choices[(size_t) i];
                    if (multiple())
                        c.on = ! c.on;
                    else
                        for (auto& other : choices)
                            other.on = &other == &c;
                    remember();
                    repaint();
                    return;
                }
        }

    private:
        enum Step { listen, mics, cable, apps, ready };
        struct Choice { juce::String title, detail, key; bool on = false, fixed = false; AudioEngine::OutputChoice out; AudioEngine::Source source; };

        AudioEngine& engine;
        std::function<void()> finished;
        juce::TextButton back { "BACK" }, next { "NEXT" };
        int step = 0;
        std::vector<Choice> choices;
        std::map<int, juce::StringArray> picked;   // each step's answers, by key
        std::map<int, bool> asked;                 // (a step's defaults are only chosen the first time)
        juce::StringArray built;                   // what the last build made, for DONE
        juce::Point<float> over { -1.0e6f, -1.0e6f };

        bool multiple() const { return step != cable; }
        juce::String question() const
        {
            switch (step)
            {
                case listen: return "Where do you listen?";
                case mics:   return "Which microphones do you speak into?";
                case cable:  return "How do Discord, games and OBS hear your mix?";
                case apps:   return "Which programs get a channel of their own?";
                default:     return "Your mix is ready";
            }
        }

        juce::Rectangle<int> stepCard (int i) const { return { 14, 14 + i * 40, 210, 34 }; }
        juce::Rectangle<int> paneArea() const { return getLocalBounds().withTrimmedLeft (238).reduced (14); }
        juce::Rectangle<int> choiceArea (int i) const
        {
            const auto area = paneArea().reduced (20).withTrimmedTop (50).withTrimmedBottom (50);
            const int columns = juce::jmax (1, area.getWidth() / 290), w = (area.getWidth() - (columns - 1) * 10) / columns;
            return { area.getX() + (i % columns) * (w + 10), area.getY() + (i / columns) * 66, w, 58 };
        }

        void remember()
        {
            auto& list = picked[step];
            list.clear();
            for (auto& c : choices)
                if (c.on)
                    list.add (c.key);
        }

        void show (int s)
        {
            step = s;
            choices.clear();
            const auto& endpoints = engine.getEndpoints();
            if (step == listen)
            {
                juce::String def;
                for (auto& e : endpoints)
                    if (e.api == "WASAPI" && e.isOutput && e.isDefault)
                        def = e.name;
                for (auto& out : engine.outputChoices())
                {
                    const auto* ep = std::find_if (endpoints.begin(), endpoints.end(), [&out] (const EndpointInfo& e) { return e.name == out.candidate; });
                    if (ep != endpoints.end() && isCable (*ep, endpoints))
                        continue;
                    Choice c { out.label.upToFirstOccurrenceOf (" (", false, false).toUpperCase(), ep != endpoints.end() ? adapterText (*ep, "Windows playback device") : juce::String ("Interface outputs"),
                               out.candidate + "|" + juce::String (out.firstChannel) };
                    c.out = out;
                    c.on = ! asked[step] ? (out.candidate == def || (def.isEmpty() && choices.empty())) : picked[step].contains (c.key);
                    choices.push_back (c);
                }
                if (! asked[step] && ! choices.empty() && std::none_of (choices.begin(), choices.end(), [] (const Choice& c) { return c.on; }))
                    choices.front().on = true;   // (Windows' default is the interface itself: its first pair)
            }
            else if (step == mics)
            {
                const auto ins = engine.getInterfaceInputNames();
                for (int ch = 0; ch < ins.size(); ++ch)
                {
                    Choice c { "IN " + juce::String (ch + 1), (deviceAlias (engine.getInterfaceAdapter()).isNotEmpty() ? deviceAlias (engine.getInterfaceAdapter()) : engine.getInterfaceAdapter()) + ": " + ins[ch], "in|" + juce::String (ch) };
                    c.source = { AudioEngine::Source::interfaceInputs, ch, {}, false };
                    c.on = asked[step] && picked[step].contains (c.key);
                    choices.push_back (c);
                }
                for (auto& e : endpoints)
                {
                    if (e.api != "WASAPI" || e.isOutput || ! e.isActive() || engine.isDeviceDisabled (e.name) || e.adapter.containsIgnoreCase ("VB-Audio")
                        || e.name.containsIgnoreCase ("Voicemeeter") || e.adapter.containsIgnoreCase ("Virtual Cable"))
                        continue;
                    if (engine.getClock() == AudioEngine::Clock::asio && e.adapter == engine.getInterfaceAdapter())
                        continue;   // (the interface's inputs are listed as IN 1, IN 2 above)
                    Choice c { endpointDisplayName (e).toUpperCase(), adapterText (e, "Windows recording device"), e.name };
                    c.source = { AudioEngine::Source::device, 0, e.name, false };
                    c.on = ! asked[step] ? e.isDefault : picked[step].contains (c.key);
                    choices.push_back (c);
                }
                if (! asked[step] && ! choices.empty() && std::none_of (choices.begin(), choices.end(), [] (const Choice& c) { return c.on; }))
                    choices.front().on = true;   // (Windows' default mic is the interface's: its first input)
                for (auto& c : choices)   // already a channel: shown ticked
                    if (std::any_of (engine.getStrips().begin(), engine.getStrips().end(), [this, &c] (auto& st) { return sameSource (st->source, c.source); }))
                        c.on = c.fixed = true;
            }
            else if (step == cable)
            {
                for (auto& e : endpoints)
                    if (e.api == "WASAPI" && e.isOutput && e.isActive() && virtualMicPartner (e, endpoints).isNotEmpty())
                    {
                        Choice c { endpointDisplayName (e).toUpperCase(), "Apps pick \"" + virtualMicPartner (e, endpoints) + "\" as their mic", e.name };
                        c.out = { e.name, e.name, 0 };
                        choices.push_back (c);
                    }
                choices.push_back ({ "NOT NOW", "Add it later from SETUP", "none" });
                // First time: the plain stereo cable (VB-CABLE's 16-channel way in is the same cable,
                // and only one of the two plays at a time).
                const auto& was = picked[step];
                auto first = std::find_if (choices.begin(), choices.end(), [] (const Choice& c) { return c.key != "none" && ! c.key.containsIgnoreCase ("16ch"); });
                if (first == choices.end())
                    first = choices.begin();
                for (auto& c : choices)
                    c.on = asked[step] ? was.contains (c.key) : &c == &*first;
            }
            else if (step == apps)
            {
                juce::StringArray exes;
                for (auto& a : listAudioApps())
                    exes.addIfNotAlreadyThere (a.exe, true);
                for (auto& st : engine.getStrips())
                    if (st->source.kind == AudioEngine::Source::app)
                        exes.addIfNotAlreadyThere (st->source.deviceName, true);
                exes.sortNatural();
                for (auto& exe : exes)
                {
                    const bool there = std::any_of (engine.getStrips().begin(), engine.getStrips().end(), [&exe] (auto& st)
                                                    { return st->source.kind == AudioEngine::Source::app && st->source.deviceName.equalsIgnoreCase (exe); });
                    Choice c { juce::File (exe).getFileNameWithoutExtension().toUpperCase(), there ? "On the desk" : "Playing now", exe };
                    c.on = there || (asked[step] && picked[step].contains (exe));
                    c.fixed = there;
                    choices.push_back (c);
                }
            }
            if (step != ready)
            {
                asked[step] = true;
                remember();
            }
            back.setVisible (step > 0 && step < ready);
            next.setButtonText (step == apps ? "BUILD THE MIX" : step == ready ? "OPEN THE MIXER" : "NEXT");
            resized();
            repaint();
            Console::riseIn (*this);
        }

        // The same thing captured: a channel on "Windows default" counts as the device that is the
        // default now, so SETUP doesn't add that microphone a second time.
        bool sameSource (const AudioEngine::Source& a, const AudioEngine::Source& b) const
        {
            if (a.kind != b.kind) return false;
            if (a.kind == AudioEngine::Source::interfaceInputs) return a.firstChannel == b.firstChannel;
            if (a.kind != AudioEngine::Source::device) return a.deviceName == b.deviceName;
            auto device = [this] (const juce::String& name)
            {
                if (name != AudioEngine::windowsDefault)
                    return name;
                for (auto& e : engine.getEndpoints())
                    if (e.api == "WASAPI" && ! e.isOutput && e.isActive() && e.isDefault)
                        return e.name;
                return name;
            };
            const auto x = device (a.deviceName), y = device (b.deviceName);
            return x.equalsIgnoreCase (y) || (x.isNotEmpty() && y.isNotEmpty() && (x.containsIgnoreCase (y) || y.containsIgnoreCase (x)));   // (a channel's device may be named by part of its name)
        }

        // What the answers make: an output per listening device (the first HEADPHONES), MIC FOR APPS
        // on the cable, a channel per microphone (to MIC FOR APPS; not in your own headphones), a
        // channel per program (to where you listen) with a rule so it comes back by itself. Only
        // what isn't there already.
        void build()
        {
            built.clear();
            juce::StringArray taken;
            for (auto& m : engine.getMasters()) taken.add (m->name);

            auto outputOn = [this, &taken] (const AudioEngine::OutputChoice& out, const juce::String& base) -> int
            {
                if (const int there = engine.masterPlayingOn (out.candidate, out.firstChannel); there >= 0)
                    return there;
                const auto name = uniqueName (taken, base);
                if (engine.addMaster (name, { out.candidate }, out.firstChannel) == nullptr)
                    return -1;   // (eight outputs already)
                taken.add (name);
                built.add ("OUTPUT " + name);
                return (int) engine.getMasters().size() - 1;
            };

            std::vector<int> listening;
            for (auto& key : picked[listen])
            {
                const auto candidate = key.upToLastOccurrenceOf ("|", false, false);
                const int first = key.fromLastOccurrenceOf ("|", false, false).getIntValue();
                juce::String label;
                for (auto& out : engine.outputChoices())
                    if (out.candidate == candidate && out.firstChannel == first)
                        label = out.label.upToFirstOccurrenceOf (" (", false, false).toUpperCase();
                const int m = outputOn ({ label, candidate, first }, listening.empty() ? juce::String ("HEADPHONES") : label);
                if (m >= 0)
                    listening.push_back (m);
            }

            int forApps = -1;
            if (const auto key = picked[cable].isEmpty() ? juce::String ("none") : picked[cable][0]; key != "none")
                forApps = outputOn ({ key, key, 0 }, "MIC FOR APPS");

            juce::StringArray names;
            for (auto& st : engine.getStrips()) names.add (st->name);
            for (auto& key : picked[mics])
            {
                AudioEngine::Source source;
                if (key.startsWith ("in|")) source = { AudioEngine::Source::interfaceInputs, key.fromFirstOccurrenceOf ("|", false, false).getIntValue(), {}, false };
                else                        source = { AudioEngine::Source::device, 0, key, false };
                if (std::any_of (engine.getStrips().begin(), engine.getStrips().end(), [this, &source] (auto& st) { return sameSource (st->source, source); }))
                    continue;
                const auto name = uniqueName (names, "MICROPHONE");
                names.add (name);
                auto& strip = engine.addStrip (name, source);
                if (forApps >= 0)
                    strip.sends[(size_t) forApps] = true;   // (not in your headphones: no echo of your own voice)
                built.add ("CHANNEL " + name);
            }

            for (auto& exe : picked[apps])
            {
                if (std::none_of (engine.getStrips().begin(), engine.getStrips().end(), [&exe] (auto& st)
                                  { return st->source.kind == AudioEngine::Source::app && st->source.deviceName.equalsIgnoreCase (exe); }))
                {
                    const auto name = uniqueName (names, juce::File (exe).getFileNameWithoutExtension().toUpperCase());
                    names.add (name);
                    auto& strip = engine.addStrip (name, { AudioEngine::Source::app, 0, exe, true });
                    for (int m : listening)
                        strip.sends[(size_t) m] = true;
                    built.add ("CHANNEL " + name);
                    // (a rule only for a channel made here: one already on the desk is routed as the user left it)
                    if (! listening.empty() && std::none_of (engine.appRules.begin(), engine.appRules.end(), [&exe] (auto& r) { return r.exe.equalsIgnoreCase (exe); }))
                        engine.appRules.push_back ({ exe, "output:" + engine.getMasters()[(size_t) listening.front()]->name });
                }
            }
            engine.refreshDevices();
            engine.refreshDucking();
            engine.logEvent ("setup: " + (built.isEmpty() ? juce::String ("nothing new to add") : built.joinIntoString (", ")));
        }

        void paintSummary (juce::Graphics& g, juce::Rectangle<int> r)
        {
            g.setFont (LargeFormat::print (12.5f));
            if (built.isEmpty())
            {
                g.setColour (DeskSettings::text());
                g.drawText ("EVERYTHING WAS ALREADY THERE", r.removeFromTop (24), juce::Justification::centredLeft, false);
                return;
            }
            for (auto& line : built)
            {
                g.setColour (Theme::get().ledGreen);
                g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) r.getX() + 6.0f, (float) r.getY() + 12.0f }));
                g.setColour (juce::Colours::white);
                g.drawText (line, r.removeFromTop (24).withTrimmedLeft (20), juce::Justification::centredLeft, false);
            }
        }
    };
}

namespace Simple
{
    std::unique_ptr<juce::Component> makeDesk (AudioEngine& engine, MixerComponent& mixer, std::function<void()> openSetup)
    {
        return std::make_unique<DeskView> (engine, mixer, std::move (openSetup));
    }

    std::unique_ptr<juce::Component> makeSetup (AudioEngine& engine, std::function<void()> finished)
    {
        return std::make_unique<Setup> (engine, std::move (finished));
    }
}

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
#include "../../src/ui/WindowsDefaults.h"
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
        void faderAndMeter (juce::Graphics& g, juce::Rectangle<float> r, float gain, float meterL, float meterR, Hit hit)
        {
            auto meter = r.removeFromRight (34.0f);   // (two LED columns, the standard's marks at their right)
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
            // The meter: round LEDs, left and right, on the selected meter standard (its own LEDs,
            // colour zones and marks), as the console's; the column shares the scale's top and bottom.
            const auto& st = Metering::standard();
            const int count = (int) st.leds.size();
            const auto column = meter.reduced (0.0f, 14.0f);
            const float pitch = column.getHeight() / (float) juce::jmax (1, count), d = juce::jmin (8.0f, pitch * 0.58f);
            for (int side = 0; side < 2; ++side)
            {
                const int on = Metering::ledsLit (st, Metering::toDb (side == 0 ? meterL : meterR));
                const int hold = heldPeak (hit.kind * 10000 + hit.index * 2 + side, on);   // PEAK HOLD, as the console's
                const float cx = column.getX() + 5.0f + (float) side * 10.0f;
                for (int i = 0; i < count; ++i)
                    LargeFormat::led (g, { cx, column.getBottom() - pitch * ((float) i + 0.5f) }, i < on || i == hold - 1,
                                      Metering::ledColour (st, st.leds[(size_t) i]), d);
            }
            g.setColour (DeskSettings::text().withAlpha (0.8f));
            g.setFont (LargeFormat::print (7.5f, false));
            for (int i = 0; i < count; ++i)
                if (const auto mark = Metering::markAt (st, st.leds[(size_t) i]); mark.isNotEmpty())
                    g.drawFittedText (mark, juce::Rectangle<float> (column.getX() + 21.0f, column.getBottom() - pitch * ((float) i + 1.0f), 13.0f, pitch).toNearestInt(),
                                      juce::Justification::centredLeft, 1, 0.6f);
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
            faderAndMeter (g, r.reduced (12.0f, 4.0f), s.gain.load(), s.levels.meterL.load(), s.levels.meterR.load(), { {}, Hit::fader, i });
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
            faderAndMeter (g, r.reduced (12.0f, 4.0f), m.gain.load(), m.levels.meterL.load(), m.levels.meterR.load(), { {}, Hit::outFader, i });
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
            mixer.showAddAppMenu (near (h), [desk = juce::Component::SafePointer<Desk> (this)] { if (desk != nullptr) desk->repaint(); });
        }

        // A name bar's right-click: rename, colour, headphone correction (an output), the mini mixer, remove.
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
            if (output)
                MixerComponent::addHeadphoneMenu (menu, engine, h.index);
            if (auto* m = output ? masterAt (h.index) : nullptr)
                MixerComponent::addMiniMixerItem (menu, engine, "output:" + m->name);
            else if (auto* s = output ? nullptr : stripAt (h.index))
                MixerComponent::addMiniMixerItem (menu, engine, "input:" + s->name);
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

}

namespace Simple
{
    std::unique_ptr<juce::Component> makeDesk (AudioEngine& engine, MixerComponent& mixer, std::function<void()> openSetup)
    {
        return std::make_unique<DeskView> (engine, mixer, std::move (openSetup));
    }
}

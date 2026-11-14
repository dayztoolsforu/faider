#pragma once

// Part of the console: Console.cpp includes it, and nothing else does.
// The plugin panel under the strips.

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../src/ui/LargeFormat.h"
#include "../../src/engine/AudioEngine.h"
#include "../../src/ui/SettingsTab.h"   // (DeskSettings: the cards)
#include "../../src/ui/Console.h"
#include "ConsoleBench.h"

namespace ConsoleDesk
{
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
}

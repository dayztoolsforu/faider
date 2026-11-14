#pragma once

// Part of the console: Console.cpp includes it, and nothing else does.
// The Mixer page: the desk in its scrolling view, with the top bar and the plugin panel.

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../src/engine/AudioEngine.h"
#include "Simple.h"
#include "../../src/ui/MixerComponent.h"
#include "../../src/ui/Console.h"
#include "ConsoleBench.h"
#include "ConsolePluginPanel.h"
#include "ConsoleTopBar.h"

namespace ConsoleDesk
{
    inline juce::File layoutFile()
    {
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("mixer.json");
    }

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
}

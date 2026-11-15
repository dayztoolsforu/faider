// FAIDER: the console. It draws channel strips from layout files (console/layout/*.json:
// every part's place, in a 214-wide strip's pixels), redrawn whenever a file changes. Everything
// is drawn in code (LargeFormat.h).
//
// Layout keys (Debug builds only: Ctrl+Shift+O is the overlay's key in Faider): Ctrl+Shift+O
// outlines every part (to check the layout), Ctrl+Shift+R reloads; with no engine, 1-8 channels across. Knobs and faders drag, keys click.
//
// Its parts: ConsoleLayout.h (the layout file), ConsoleBinding.h (what each part drives), ConsoleBench.h
// (the desk), ConsolePluginPanel.h, ConsoleTopBar.h, ConsoleView.h (the Mixer page), ConsoleRouting.h and
// ConsoleSidebar.h.

#include "Console.h"
#include "ConsoleView.h"
#include "ConsoleRouting.h"
#include "ConsoleSidebar.h"

// The console as Faider's Mixer page: the strips in a scrolling view.
namespace ConsoleDesk
{
    std::unique_ptr<juce::Component> make (AudioEngine& engine, MixerComponent& mixer, bool simple, std::function<void (bool)> modeChanged, std::function<void()> openSetup)
    {
        return std::make_unique<View> (engine, mixer, simple, std::move (modeChanged), std::move (openSetup));
    }

    void setSimple (juce::Component& console, bool simple)
    {
        if (auto* view = dynamic_cast<View*> (&console))
            view->setSimple (simple);
    }

    bool isCompact (juce::Component& console)
    {
        auto* view = dynamic_cast<View*> (&console);
        return view != nullptr && view->bench->isCompact();
    }

    void setCompact (juce::Component& console, bool compact)
    {
        if (auto* view = dynamic_cast<View*> (&console))
        {
            if (view->isSimple())
                view->switchTo (false);   // (COMPACT is the PRO desk's: it opens on it)
            view->bench->setCompact (compact);
        }
    }

    std::unique_ptr<juce::Component> makeRouting (juce::Component& console)
    {
        return std::make_unique<RoutingView> (dynamic_cast<View&> (console));
    }

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

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class AudioEngine;
class MixerComponent;

// FAIDER: the console (Console.cpp), as the Mixer page.
namespace ConsoleDesk
{
    // The whole console: the strips, the top bar and the plugin panel.
    std::unique_ptr<juce::Component> make (AudioEngine&, MixerComponent&);

    // COMPACT: the desk's strips folded to their name bars and faders, or whole again; remembered.
    bool isCompact (juce::Component& console);
    void setCompact (juce::Component& console, bool compact);

    // The sidebar down the app's left: the pages as cards, then the desk's
    // own (+ CHANNEL, + APP, + BUS). `console` is what make() returned.
    std::unique_ptr<juce::Component> makeSidebar (juce::TabbedComponent&, juce::Component& console);

    // The desk's keys (arrows, M, S, up / down) when the keyboard isn't on the desk itself; a strip
    // selected by its key ("input:MIC"), with its PLUGINS panel opened if `plugins`. `console` is
    // what make() returned.
    bool handleKey (juce::Component& console, const juce::KeyPress&);
    void select (juce::Component& console, const juce::String& key, bool plugins);

    // A channel, bus or output renamed (however it was): what the console keeps by name follows.
    void renamed (juce::Component& console, const juce::String& kind, const juce::String& oldName, const juce::String& newName);
    constexpr int sidebarWidth = 156;

    // The Routing page: the desk's strips as blocks, across where they are on the desk; a click on
    // one block then another routes between them. `console` is what make() returned.
    std::unique_ptr<juce::Component> makeRouting (juce::Component& console);
}

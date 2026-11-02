#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Console.h"

// The TOOLS tab: the desk's extras, each on its own tab below (PADS, TAPE). The
// panels belong to MainComponent; this only shows them.
class ToolsTab : public juce::Component
{
public:
    ToolsTab (std::initializer_list<std::pair<juce::String, juce::Component*>> pages)
    {
        for (auto& [name, page] : pages)
            tabs.addTab (name, juce::Colours::transparentBlack, page, false);
        addAndMakeVisible (tabs);
    }

    ~ToolsTab() override { tabs.clearTabs(); }

    void paint (juce::Graphics& g) override { g.fillAll (Console::Colours::panelDark()); }
    void resized() override { tabs.setBounds (getLocalBounds().withTrimmedTop (Console::Space::unit)); }

private:
    Console::KeyTabs tabs { Console::Space::pad };   // on the pages' content edge, like the page tabs above
};

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Console.h"

// A page's place on the desk, holding its panel. The tab bar shows, hides and sizes the slot, never
// the panel, so the panel can be lent elsewhere (the SteamVR dashboard) and handed back without the
// tabs taking it back or losing it: meanwhile the slot says where it is.
class PanelSlot : public juce::Component
{
public:
    explicit PanelSlot (juce::Component& p) : panel (p)
    {
        panel.getProperties().set (slottedProperty, true);
        addAndMakeVisible (panel);
    }

    juce::Component& panel;

    bool isLent() const { return panel.getParentComponent() != this; }

    // Whether `c` (a slotted panel, or anything in one) is lent out just now. What can't work in the
    // headset (a file dialog: it opens on the monitor; hotkey LEARN: no keys reach the page) asks,
    // from parentHierarchyChanged (lending and handing back call it) or a timer.
    static bool isLentOut (const juce::Component& c)
    {
        for (auto* p = &c; p != nullptr; p = p->getParentComponent())
            if (p->getProperties().contains (slottedProperty))
                return p->getParentComponent() != nullptr && dynamic_cast<const PanelSlot*> (p->getParentComponent()) == nullptr;
        return false;
    }

    void takeBack()
    {
        addAndMakeVisible (panel);
        resized();
        repaint();
    }

    void resized() override
    {
        if (! isLent())
            panel.setBounds (getLocalBounds());
    }

    void paint (juce::Graphics& g) override
    {
        if (! isLent())
            return;
        g.fillAll (Console::Colours::panelDark());
        g.setColour (Console::Colours::legend().withAlpha (0.6f));
        g.setFont (Console::legendFont (Console::Type::display));
        g.drawText ("IN THE HEADSET", getLocalBounds(), juce::Justification::centred);
    }

private:
    static constexpr const char* slottedProperty = "panelSlotted";   // on the panel: it has a slot
};

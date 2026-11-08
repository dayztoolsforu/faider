#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/RemoteControl.h"
#include "Console.h"
 #include "DeskSettings.h"

// CONTROL: everything about controllers in one place (SETTINGS > CONTROL): MIDI (map mode, and the
// MIDI page: each input on or off, its bindings) and OSC (this PC only). It follows changes made
// elsewhere (a binding learned) while it's open.
class ControlPanel : public juce::Component, private juce::Timer
                   , public DeskSettings::WidthFill
{
public:
    // What lives in the app rather than the engine.
    struct Hooks
    {
        std::function<void()> toggleMidiMap, openMidiPanel;
        std::function<bool()> midiMapOn;
    };

    ControlPanel (RemoteControl& r, Hooks h) : remote (r), hooks (std::move (h))
    {
        for (auto* l : { &midiHeading, &oscHeading })
        {
            l->setFont (Console::legendFont (Console::Type::name));   // section headings
            l->setColour (juce::Label::textColourId, Console::Colours::legendDim());
            DeskSettings::styleHeading (*l);
            addAndMakeVisible (l);
        }

        bindings.onClick = [this] { hooks.openMidiPanel(); };
        bindings.setTooltip ("Each MIDI input on or off, and what its controls are bound to");

        toggle (midiMap, "Link desk controls to MIDI (Ctrl+L)", [this] { hooks.toggleMidiMap(); });
        toggle (osc, "Listen for OSC messages",
                [this] { remote.setOscPort (remote.getOscPort() > 0 ? 0 : remote.getOscPreferredPort()); });
        addAndMakeVisible (bindings);
        refresh();
        setSize (420, heightFor (420));
        startTimerHz (4);
    }

private:
    void toggle (juce::TextButton& b, const juce::String& tip, std::function<void()> onClick)
    {
        b.setClickingTogglesState (true);   // (lights it; refresh then shows what it controls really did)
        b.setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
        b.setTooltip (tip);
        // (refreshed after the click, not in it)
        b.onClick = [this, onClick]
        {
            onClick();
            juce::MessageManager::callAsync ([self = juce::Component::SafePointer<ControlPanel> (this)] { if (self != nullptr) self->refresh(); });
        };
        addAndMakeVisible (b);
    }

    void refresh()
    {
        const int port = remote.getOscPort();
        midiMap.setToggleState (hooks.midiMapOn(), juce::dontSendNotification);
        bindings.setButtonText ("INPUTS AND BINDINGS (" + juce::String (remote.mappingCount()) + ")...");
        osc.setButtonText ("OSC ON  (PORT " + juce::String (port > 0 ? port : remote.getOscPreferredPort()) + ")");
        osc.setToggleState (port > 0, juce::dontSendNotification);
    }

    // Only while it's open: closed, the window is only hidden.
    void timerCallback() override { if (isShowing()) refresh(); }

    // A theme change: the colours set when the window was made, in the new theme's.
    void lookAndFeelChanged() override
    {
        for (auto* l : { &midiHeading, &oscHeading })
            l->setColour (juce::Label::textColourId, Console::Colours::legendDim());
        for (auto* b : { &midiMap, &osc })
            b->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
        repaint();
    }

public:
    // MIDI and OSC, each a card, in columns.
    int heightFor (int w) override { return arrangeCards (w, false); }
private:
    int arrangeCards (int w, bool apply)
    {
        std::vector<DeskSettings::Group> groups;
        groups.push_back ({ &midiHeading, { { { &midiMap } }, { { &bindings } } } });
        groups.push_back ({ &oscHeading, { { { &osc } } } });
        return DeskSettings::arrange (groups, w, apply, cards, 340);
    }
    std::vector<juce::Rectangle<int>> cards;
    void paint (juce::Graphics& g) override
    {
        g.fillAll (DeskSettings::desk());
        for (auto& c : cards)
            DeskSettings::card (g, c.toFloat());
    }
    void resized() override { arrangeCards (getWidth(), true); }

    RemoteControl& remote;
    Hooks hooks;
    juce::Label midiHeading { {}, "MIDI" }, oscHeading { {}, "OSC" };
    juce::TextButton midiMap { "MIDI MAP  (CTRL+L)" }, bindings;
    juce::TextButton osc;
};

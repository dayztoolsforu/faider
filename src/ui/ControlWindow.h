#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/RemoteControl.h"
#include "Console.h"
 #include "DeskSettings.h"

// CONTROL: everything about controllers in one place (SETTINGS > CONTROL): MIDI (map mode, inputs
// and bindings, each input on or off) and OSC (this PC only). It follows changes made elsewhere
// (a MIDI device plugged in) while it's open.
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
        noInputs.setFont (juce::FontOptions (Console::Type::body));
        noInputs.setColour (juce::Label::textColourId, Console::Colours::legendDim());
        addChildComponent (noInputs);

        bindings.onClick = [this] { hooks.openMidiPanel(); };

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
        // (refreshed after the click, not in it: a refresh can remake the key being clicked)
        b.onClick = [this, onClick]
        {
            onClick();
            juce::MessageManager::callAsync ([self = juce::Component::SafePointer<ControlPanel> (this)] { if (self != nullptr) self->refresh(); });
        };
        addAndMakeVisible (b);
    }

    void refresh()
    {
        // MIDI inputs: a key each, made again when devices come or go.
        const auto devices = remote.availableInputs();
        juce::StringArray ids;
        for (auto& d : devices)
            ids.add (d.identifier);
        if (ids != shownIds)
        {
            shownIds = ids;
            inputs.clear();
            for (auto& d : devices)
            {
                auto* b = inputs.add (new juce::TextButton (d.name));
                toggle (*b, "Listen to " + d.name, [this, id = d.identifier] { remote.setInputOn (id, ! remote.isInputOn (id)); });
            }
            noInputs.setVisible (devices.isEmpty());
            setSize (getWidth(), heightFor (getWidth()));
            resized();
        }
        for (int i = 0; i < inputs.size(); ++i)
            inputs[i]->setToggleState (remote.isInputOn (shownIds[i]), juce::dontSendNotification);

        const int port = remote.getOscPort();
        midiMap.setToggleState (hooks.midiMapOn(), juce::dontSendNotification);
        bindings.setButtonText ("INPUTS AND BINDINGS (" + juce::String (remote.mappingCount()) + ")...");
        osc.setButtonText ("OSC ON  (PORT " + juce::String (port > 0 ? port : remote.getOscPreferredPort()) + ")");
        osc.setToggleState (port > 0, juce::dontSendNotification);
    }

    // Only while it's open: closed, the window is only hidden (it would list MIDI devices four
    // times a second for the rest of the run).
    void timerCallback() override { if (isShowing()) refresh(); }

    // A theme change: the colours set when the window was made, in the new theme's.
    void lookAndFeelChanged() override
    {
        for (auto* l : { &midiHeading, &oscHeading, &noInputs })
            l->setColour (juce::Label::textColourId, Console::Colours::legendDim());
        for (auto* b : { &midiMap, &osc })
            b->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
        for (auto* b : inputs)
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
        DeskSettings::Group midi { &midiHeading, { { { &midiMap } }, { { &bindings } } } };
        if (inputs.isEmpty())
            midi.rows.push_back ({ { &noInputs } });
        for (auto* b : inputs)
            midi.rows.push_back ({ { b } });
        groups.push_back (midi);
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
    juce::OwnedArray<juce::TextButton> inputs;   // one per MIDI input, in shownIds' order
    juce::StringArray shownIds;
    juce::Label noInputs { {}, "No MIDI inputs" };
    juce::TextButton osc;
};

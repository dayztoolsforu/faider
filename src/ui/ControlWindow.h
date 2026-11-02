#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/RemoteControl.h"
#include "Console.h"
 #include "DeskSettings.h"

// CONTROL: everything about controllers in one place (SETTINGS > CONTROL): SET UP A CONTROLLER,
// MIDI (map mode, inputs and bindings, each input on or off), OSC, the deck plugins' local API
// and phone control. It follows changes made elsewhere (a MIDI device plugged in, the helper
// turning the API on) while it's open.
class ControlPanel : public juce::Component, private juce::Timer
                   , public DeskSettings::WidthFill
{
public:
    // What lives in the app rather than the engine.
    struct Hooks
    {
        std::function<void()> openHelper, toggleMidiMap, openMidiPanel;
        std::function<bool()> midiMapOn, apiOn, phoneOn;
        std::function<void (bool)> setApi, setPhone;
        int apiPort = 0;   // the port the API listens on when switched on (shown on its key)
    };

    ControlPanel (RemoteControl& r, Hooks h) : remote (r), hooks (std::move (h))
    {
        for (auto* l : { &midiHeading, &oscHeading, &apiHeading, &phoneHeading })
        {
            l->setFont (Console::legendFont (Console::Type::name));   // section headings
            l->setColour (juce::Label::textColourId, Console::Colours::legendDim());
            DeskSettings::styleHeading (*l);
            addAndMakeVisible (l);
        }
        noInputs.setFont (juce::FontOptions (Console::Type::body));
        noInputs.setColour (juce::Label::textColourId, Console::Colours::legendDim());
        addChildComponent (noInputs);

        helper.setTooltip ("Guided setup for any controller");
        helper.onClick = [this] { hooks.openHelper(); };
        bindings.onClick = [this] { hooks.openMidiPanel(); };

        toggle (midiMap, "Link desk controls to MIDI (Ctrl+L)", [this] { hooks.toggleMidiMap(); });
        toggle (osc, "Listen for OSC messages",
                [this] { remote.setOscPort (remote.getOscPort() > 0 ? 0 : remote.getOscPreferredPort()); });
        toggle (oscNetwork, "Let tablets and phones send OSC",
                [this] { remote.setOscFromNetwork (! remote.isOscFromNetwork()); });
        toggle (api, "Lets Stream Deck and Loupedeck plugins connect",
                [this] { hooks.setApi (! hooks.apiOn()); });
        toggle (phone, "Control the desk from your phone",
                [this] { hooks.setPhone (! hooks.phoneOn()); });

        api.setButtonText ("API ON  (127.0.0.1:" + juce::String (hooks.apiPort) + ")");
        for (auto* b : { &helper, &bindings })
            addAndMakeVisible (b);

        DeskSettings::styleHeading (setupHeading);
        addAndMakeVisible (setupHeading);
        helper.setButtonText ("SET UP A CONTROLLER...");
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
        const bool apiOn = hooks.apiOn();
        midiMap.setToggleState (hooks.midiMapOn(), juce::dontSendNotification);
        bindings.setButtonText ("INPUTS AND BINDINGS (" + juce::String (remote.mappingCount()) + ")...");
        osc.setButtonText ("OSC ON  (PORT " + juce::String (port > 0 ? port : remote.getOscPreferredPort()) + ")");
        osc.setToggleState (port > 0, juce::dontSendNotification);
        oscNetwork.setToggleState (remote.isOscFromNetwork(), juce::dontSendNotification);
        api.setToggleState (apiOn, juce::dontSendNotification);
        phone.setEnabled (apiOn);
        phone.setToggleState (hooks.phoneOn(), juce::dontSendNotification);
    }

    // Only while it's open: closed, the window is only hidden (it would list MIDI devices four
    // times a second for the rest of the run).
    void timerCallback() override { if (isShowing()) refresh(); }

    // A theme change: the colours set when the window was made, in the new theme's.
    void lookAndFeelChanged() override
    {
        for (auto* l : { &midiHeading, &oscHeading, &apiHeading, &phoneHeading, &noInputs })
            l->setColour (juce::Label::textColourId, Console::Colours::legendDim());
        for (auto* b : { &midiMap, &osc, &oscNetwork, &api, &phone })
            b->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
        for (auto* b : inputs)
            b->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
        repaint();
    }

public:
    // SET UP, MIDI, OSC, the deck plugins' API and phone control, each a card, in columns.
    int heightFor (int w) override { return arrangeCards (w, false); }
private:
    int arrangeCards (int w, bool apply)
    {
        std::vector<DeskSettings::Group> groups;
        groups.push_back ({ &setupHeading, { { { &helper } } } });
        DeskSettings::Group midi { &midiHeading, { { { &midiMap } }, { { &bindings } } } };
        if (inputs.isEmpty())
            midi.rows.push_back ({ { &noInputs } });
        for (auto* b : inputs)
            midi.rows.push_back ({ { b } });
        groups.push_back (midi);
        groups.push_back ({ &oscHeading, { { { &osc } }, { { &oscNetwork } } } });
        groups.push_back ({ &apiHeading, { { { &api } } } });
        groups.push_back ({ &phoneHeading, { { { &phone } } } });
        return DeskSettings::arrange (groups, w, apply, cards, 340);
    }
    std::vector<juce::Rectangle<int>> cards;
    juce::Label setupHeading { {}, "SET UP A CONTROLLER" };
    void paint (juce::Graphics& g) override
    {
        g.fillAll (DeskSettings::desk());
        for (auto& c : cards)
            DeskSettings::card (g, c.toFloat());
    }
    void resized() override { arrangeCards (getWidth(), true); }

    RemoteControl& remote;
    Hooks hooks;
    juce::TextButton helper { "SET UP A CONTROLLER (LOUPEDECK, STREAM DECK, MIDI, OSC, PHONE)..." };
    juce::Label midiHeading { {}, "MIDI" }, oscHeading { {}, "OSC" },
                apiHeading { {}, "DECK PLUGINS (LOCAL API)" }, phoneHeading { {}, "PHONE CONTROL (WEB PAGE)" };
    juce::TextButton midiMap { "MIDI MAP  (CTRL+L)" }, bindings;
    juce::OwnedArray<juce::TextButton> inputs;   // one per MIDI input, in shownIds' order
    juce::StringArray shownIds;
    juce::Label noInputs { {}, "No MIDI inputs" };
    juce::TextButton osc, oscNetwork { "OSC FROM OTHER DEVICES (TABLET, PHONE)" }, api, phone { "PHONE CONTROL ON" };
};

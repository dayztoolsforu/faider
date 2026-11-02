#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/RemoteControl.h"
#include "Console.h"
#include "DotMatrix.h"
#include "MidiMap.h"

// The MIDI page (CONTROL > MIDI...): the MIDI inputs to listen to, every binding (controller
// control and what it works, with a key to clear each), the last control the controllers sent,
// and MIDI MAP to bind more by clicking them on screen.
class MidiPanel : public juce::Component,
                  private juce::ListBoxModel,
                  private juce::Timer
{
public:
    MidiPanel (RemoteControl& r, std::function<void()> openMap) : remote (r), startMap (std::move (openMap))
    {
        for (auto* l : { &inputsLabel, &mappingsLabel })
        {
            l->setFont (Console::legendFont (Console::Type::body));
            addAndMakeVisible (l);
        }
        DotMatrix::markReadout (last);
        addAndMakeVisible (last);

        mapKey.setTooltip ("Click on-screen controls to bind them");
        mapKey.onClick = [this] { if (startMap) startMap(); };
        refreshKey.onClick = [this] { fillInputs(); };
        clearAll.onClick = [this] { remote.clearAll(); list.updateContent(); list.repaint(); };
        for (auto* b : { &mapKey, &refreshKey, &clearAll })
            addAndMakeVisible (b);

        list.setRowHeight (Console::Space::row);
        list.setColour (juce::ListBox::backgroundColourId, Console::Colours::panelDark());
        addAndMakeVisible (list);

        fillInputs();
        setSize (560, 520);
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override { g.fillAll (Console::Colours::panel()); }

    // A theme change: the colours set when the page was made, in the new theme's.
    void lookAndFeelChanged() override
    {
        DotMatrix::markReadout (last);
        list.setColour (juce::ListBox::backgroundColourId, Console::Colours::panelDark());
        for (auto* b : inputKeys)
            b->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
        repaint();
    }

    void resized() override
    {
        using namespace Console;
        auto r = getLocalBounds().reduced (Space::pad);

        // Each heading on a title strip with its keys at the right, the label filling the rest.
        auto heading = [&r] (juce::Label& label, std::initializer_list<juce::TextButton*> keys)
        {
            int columns = 1;   // peer keys the same width
            for (auto* k : keys)
                columns = juce::jmax (columns, KeyRow::columnsFor (k->getButtonText()));
            KeyRow row (r.removeFromTop (Space::title));
            for (auto* k : keys)
                row.right (*k, columns);
            label.setBounds (row.rest().withTrimmedRight (Space::pad));
        };

        heading (inputsLabel, { &refreshKey });
        r.removeFromTop (Space::unit);
        for (auto* b : inputKeys)
        {
            b->setBounds (r.removeFromTop (Space::key));
            r.removeFromTop (Space::unit);
        }
        r.removeFromTop (Space::gap);
        heading (mappingsLabel, { &clearAll, &mapKey });
        r.removeFromTop (Space::unit);
        last.setBounds (r.removeFromBottom (Space::row));
        r.removeFromBottom (Space::gap);
        list.setBounds (r);
    }

private:
    void fillInputs()
    {
        inputKeys.clear();
        for (auto& d : remote.availableInputs())
        {
            auto* b = inputKeys.add (new juce::TextButton (d.name));
            b->setClickingTogglesState (true);
            b->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
            b->setToggleState (remote.isInputOn (d.identifier), juce::dontSendNotification);
            b->onClick = [this, b, id = d.identifier] { remote.setInputOn (id, b->getToggleState()); };
            addAndMakeVisible (b);
        }
        if (inputKeys.isEmpty())
        {
            auto* b = inputKeys.add (new juce::TextButton ("NO MIDI INPUTS"));
            b->setEnabled (false);
            addAndMakeVisible (b);
        }
        resized();
    }

    int getNumRows() override { return juce::jmax (1, (int) remote.getMappings().size()); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
    {
        auto r = juce::Rectangle<int> (width, height).reduced (8, 0);
        g.setFont (Console::legendFont (Console::Type::body));
        if (remote.getMappings().empty())
        {
            g.setColour (Console::Colours::legendDim());
            g.drawText ("NOTHING BOUND YET: USE MIDI MAP", r, juce::Justification::centredLeft, true);
            return;
        }
        if (! juce::isPositiveAndBelow (row, (int) remote.getMappings().size()))
            return;   // (a binding cleared elsewhere since the list last updated)
        auto it = std::next (remote.getMappings().begin(), row);
        const bool active = it->first == remote.lastControl && juce::Time::getMillisecondCounter() - lastMoved < 400;
        if (active)
        {
            g.setColour (Console::Colours::ledGreen().withAlpha (0.18f));
            g.fillRect (0, 0, width, height);
        }
        g.setColour (Console::Colours::legend());
        g.drawText (RemoteControl::describe (it->first), r.removeFromLeft (130), juce::Justification::centredLeft, true);
        r.removeFromRight (28);   // the clear key
        g.drawText (MidiMap::describe (it->second), r, juce::Justification::centredLeft, true);
    }

    juce::Component* refreshComponentForRow (int row, bool, juce::Component* existing) override
    {
        std::unique_ptr<juce::Component> old (existing);
        if (! juce::isPositiveAndBelow (row, (int) remote.getMappings().size()))
            return nullptr;
        struct Clear : juce::Component
        {
            juce::TextButton key { "X" };
            Clear() { addAndMakeVisible (key); setInterceptsMouseClicks (false, true); }
            void resized() override   // a small square key, a unit in from the row's end
            {
                const auto end = getLocalBounds().withTrimmedRight (Console::Space::unit).removeFromRight (Console::Space::keySmall);
                key.setBounds (end.withSizeKeepingCentre (Console::Space::keySmall, Console::Space::keySmall));
            }
        };
        auto* c = dynamic_cast<Clear*> (old.get());
        if (c == nullptr)
            c = new Clear();
        else
            old.release();
        c->key.setTooltip ("Clear this binding");
        c->key.onClick = [this, control = std::next (remote.getMappings().begin(), row)->first]
        {
            remote.removeMapping (control);
            // The list refreshes after this click is done: updating it now would delete or
            // re-point this very key while its onClick is still running.
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MidiPanel> (this)]
            {
                if (safe == nullptr)
                    return;
                safe->list.updateContent();
                safe->list.repaint();
            });
        };
        return c;
    }

    void timerCallback() override
    {
        if (remote.lastControl.isNotEmpty())
        {
            const auto text = "LAST: " + RemoteControl::describe (remote.lastControl) + "  " + juce::String (juce::roundToInt (remote.lastControlValue * 127.0f));
            if (text != last.getText())
            {
                last.setText (text, juce::dontSendNotification);
                lastMoved = juce::Time::getMillisecondCounter();
            }
        }
        else if (last.getText().isEmpty())
            last.setText ("MOVE A CONTROL ON A CONTROLLER", juce::dontSendNotification);

        juce::String now;
        for (auto& [control, target] : remote.getMappings())
            now << control << '=' << target << ';';
        if (now != shown)
        {
            shown = now;
            list.updateContent();
        }
        list.repaint();
    }

    RemoteControl& remote;
    std::function<void()> startMap;
    juce::Label inputsLabel { {}, "MIDI INPUTS" }, mappingsLabel { {}, "BINDINGS" }, last;
    juce::TextButton mapKey { "MIDI MAP" }, refreshKey { "REFRESH" }, clearAll { "CLEAR ALL" };
    juce::OwnedArray<juce::TextButton> inputKeys;
    juce::ListBox list { "Bindings", this };
    juce::String shown;
    juce::uint32 lastMoved = 0;
};

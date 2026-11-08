#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Console.h"
#include "DotMatrix.h"

// Desk-style parts for the editors, where a console has keys, knobs and screens rather than
// menus and sliders.
namespace ConsoleControls
{
    // A row of lit keys, one of them on: a mode switch (ONE-SHOT | HOLD | LOOP).
    class KeyRow : public juce::Component
    {
    public:
        std::function<void (int)> onChange;

        explicit KeyRow (const juce::StringArray& names)
        {
            for (int i = 0; i < names.size(); ++i)
            {
                auto* key = keys.add (new juce::TextButton (names[i]));
                key->setClickingTogglesState (true);
                key->setRadioGroupId (1);
                key->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
                key->setWantsKeyboardFocus (false);
                key->onClick = [this, i] { if (keys[i]->getToggleState() && onChange) onChange (i); };
                addAndMakeVisible (key);
            }
        }

        void setTooltip (const juce::String& tip) { for (auto* key : keys) key->setTooltip (tip); }

        void setSelected (int index)
        {
            for (int i = 0; i < keys.size(); ++i)
                keys[i]->setToggleState (i == index, juce::dontSendNotification);
        }

        void resized() override
        {
            // Keys of one width, a unit apart, flush with the edges (the last takes the odd pixels).
            const int n = juce::jmax (1, keys.size());
            const int w = (getWidth() - (n - 1) * Console::Space::unit) / n;
            Console::KeyRow row (getLocalBounds());
            for (int i = 0; i < keys.size(); ++i)
                row.leftWidth (*keys[i], i == keys.size() - 1 ? getWidth() : w);
        }

    private:
        juce::OwnedArray<juce::TextButton> keys;
    };

    // A knob beside its screen, which shows the value as a desk prints it ("-6.0 dB", "250 ms").
    // Double-click the knob for its default.
    class ValueKnob : public juce::Component
    {
    public:
        std::function<void (double)> onChange;
        std::function<juce::String (double)> format;
        juce::Slider knob;

        ValueKnob()
        {
            knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            knob.onValueChange = [this] { show(); if (onChange) onChange (knob.getValue()); };
            DotMatrix::markReadout (screen);
            addAndMakeVisible (knob);
            addAndMakeVisible (screen);
        }

        void setTooltip (const juce::String& tip) { knob.setTooltip (tip); screen.setTooltip (tip); }
        void setValue (double v) { knob.setValue (v, juce::dontSendNotification); show(); }
        double getValue() const  { return knob.getValue(); }

        void resized() override
        {
            auto r = getLocalBounds();
            knob.setBounds (r.removeFromLeft (r.getHeight()));
            r.removeFromLeft (Console::Space::gap);
            screen.setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), Console::Space::key)));
        }

    private:
        void show() { screen.setText (format ? format (knob.getValue()) : juce::String (knob.getValue(), 1), juce::dontSendNotification); }

        juce::Label screen;
    };

    // A setting stepped through on a screen with keys either side (CHOKE OFF, 1 .. 8).
    class Stepper : public juce::Component
    {
    public:
        std::function<void (int)> onChange;

        explicit Stepper (const juce::StringArray& items) : names (items)
        {
            DotMatrix::markReadout (screen);
            Console::setIcon (down, Console::Icon::stepDown);
            Console::setIcon (up, Console::Icon::stepUp);
            down.onClick = [this] { step (-1); };
            up.onClick = [this] { step (1); };
            for (auto* c : std::initializer_list<juce::Component*> { &down, &screen, &up })
            {
                c->setWantsKeyboardFocus (false);
                addAndMakeVisible (c);
            }
            setIndex (0);
        }

        void setTooltip (const juce::String& tip) { screen.setTooltip (tip); }

        void setIndex (int i)
        {
            index = juce::jlimit (0, juce::jmax (0, names.size() - 1), i);
            screen.setText (names[index], juce::dontSendNotification);
        }

        void resized() override
        {
            // Square keys either side of the screen, a unit from it, all a key high.
            Console::KeyRow row (getLocalBounds());
            row.leftWidth (down, Console::Space::key).rightWidth (up, Console::Space::key);
            row.leftWidth (screen, row.rest().getWidth() - 2 * Console::Space::unit);
        }

    private:
        void step (int by)
        {
            const int before = index;
            setIndex (index + by);
            if (index != before && onChange)
                onChange (index);
        }

        juce::StringArray names;
        int index = 0;
        juce::TextButton down, up;
        juce::Label screen;
    };
}

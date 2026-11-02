#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../ProductName.h"
#include "../engine/AudioEngine.h"
#include "Console.h"

// The tray mixer: a small console that pops up from the tray icon, with a fader, meter and mute
// for each strip ticked "Tray mixer" on its right-click menu (the outputs if none are). OPEN
// brings up the full window. Strips are looked up by name every frame, so it's safe while the
// mix changes under it.
class TrayMixer : public juce::Component,
                  private juce::Timer
{
public:
    TrayMixer (AudioEngine& e, std::function<void()> openWindow) : engine (e), open (std::move (openWindow))
    {
        title.setFont (Console::legendFont (Console::Type::body));
        addAndMakeVisible (title);
        openKey.setTooltip (juce::String ("Open ") + Product::name);
        openKey.onClick = [this] { if (open) open(); };
        addAndMakeVisible (openKey);

        auto keys = engine.trayStrips;
        if (keys.isEmpty())
            for (auto& b : engine.getMasters())
                keys.add ("output:" + b->name);

        for (auto& key : keys)
        {
            auto* c = columns.add (new Column (key));
            c->name.setText (key.fromFirstOccurrenceOf (":", false, false), juce::dontSendNotification);
            c->name.setFont (Console::legendFont (Console::Type::legend));
            c->name.setJustificationType (juce::Justification::centred);
            c->name.setColour (juce::Label::backgroundColourId, Console::Colours::scribble());
            c->name.setColour (juce::Label::textColourId, Console::Colours::scribbleText());
            c->fader.setSliderStyle (juce::Slider::LinearVertical);
            c->fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            c->fader.setRange (-60.0, 12.0, 0.1);
            c->fader.setSkewFactorFromMidPoint (-12.0);
            c->fader.setDoubleClickReturnValue (true, 0.0);
            c->fader.setPopupDisplayEnabled (true, true, nullptr);
            c->fader.setTextValueSuffix (" dB");
            c->fader.onValueChange = [this, c]
            {
                if (auto* gain = gainOf (c->key))
                    gain->store (c->fader.getValue() <= -60.0 ? 0.0f : juce::Decibels::decibelsToGain ((float) c->fader.getValue()));
            };
            c->mute.setClickingTogglesState (true);
            c->mute.setColour (juce::TextButton::buttonOnColourId, Console::Colours::muteLamp());
            c->mute.onClick = [this, c] { if (auto* m = muteOf (c->key)) m->store (c->mute.getToggleState()); };
            for (auto* comp : std::initializer_list<juce::Component*> { &c->name, &c->fader, &c->mute })
                addAndMakeVisible (comp);
        }

        using namespace Console;
        setSize (juce::jmax (180, columns.size() * (columnWidth + Space::gap) - Space::gap + 2 * Space::pad), 300);
        timerCallback();
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Console::Colours::panel());
        for (auto* c : columns)   // each strip's meter beside its fader
        {
            const auto r = c->meter.toFloat();
            g.setColour (Theme::get().screenGlass);
            g.fillRoundedRectangle (r.expanded (1.5f), 2.0f);
            const float level = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (c->level, -60.0f) + 60.0f) / 60.0f);
            const auto lit = r.withTop (r.getBottom() - r.getHeight() * level);
            g.setGradientFill (juce::ColourGradient (Console::Colours::ledGreen(), r.getBottomLeft(), Console::Colours::ledRed(), r.getTopLeft(), false));
            g.fillRect (lit);
        }
    }

    // A theme change while it's up: the colours set when it was made, in the new theme's.
    void lookAndFeelChanged() override
    {
        for (auto* c : columns)
        {
            c->name.setColour (juce::Label::backgroundColourId, Console::Colours::scribble());
            c->name.setColour (juce::Label::textColourId, Console::Colours::scribbleText());
            c->mute.setColour (juce::TextButton::buttonOnColourId, Console::Colours::muteLamp());
        }
        repaint();
    }

    void resized() override
    {
        using namespace Console;
        auto r = getLocalBounds().reduced (Space::pad);
        KeyRow top (r.removeFromTop (Space::key));
        top.right (openKey);
        title.setBounds (top.rest());
        r.removeFromTop (Space::gap);
        for (auto* c : columns)   // a gap apart
        {
            auto col = r.removeFromLeft (columnWidth);
            r.removeFromLeft (Space::gap);
            c->name.setBounds (col.removeFromTop (Space::keySmall));
            col.removeFromTop (Space::gap);
            c->mute.setBounds (col.removeFromBottom (Space::key));
            col.removeFromBottom (Space::gap);
            c->meter = col.removeFromRight (meterWidth);
            col.removeFromRight (Space::unit);
            c->fader.setBounds (col);
        }
    }

private:
    static constexpr int columnWidth = 64, meterWidth = 8;

    struct Column
    {
        explicit Column (const juce::String& k) : key (k) {}
        juce::String key;
        juce::Label name;
        juce::Slider fader;
        juce::TextButton mute { "M" };
        juce::Rectangle<int> meter;
        float level = 0.0f;
    };

    std::atomic<float>* gainOf (const juce::String& key)
    {
        const auto kind = key.upToFirstOccurrenceOf (":", false, false), name = key.fromFirstOccurrenceOf (":", false, false);
        if (kind == "output") { for (auto& b : engine.getMasters()) if (b->name == name) return &b->gain; return nullptr; }
        for (auto& s : (kind == "bus" ? engine.getBuses() : engine.getStrips())) if (s->name == name) return &s->gain;
        return nullptr;
    }

    std::atomic<bool>* muteOf (const juce::String& key)
    {
        const auto kind = key.upToFirstOccurrenceOf (":", false, false), name = key.fromFirstOccurrenceOf (":", false, false);
        if (kind == "output") { for (auto& b : engine.getMasters()) if (b->name == name) return &b->mute; return nullptr; }
        for (auto& s : (kind == "bus" ? engine.getBuses() : engine.getStrips())) if (s->name == name) return &s->mute;
        return nullptr;
    }

    float peakOf (const juce::String& key)
    {
        const auto kind = key.upToFirstOccurrenceOf (":", false, false), name = key.fromFirstOccurrenceOf (":", false, false);
        if (kind == "output") { for (auto& b : engine.getMasters()) if (b->name == name) return juce::jmax (b->peakL.load(), b->peakR.load()); return 0.0f; }
        for (auto& s : (kind == "bus" ? engine.getBuses() : engine.getStrips())) if (s->name == name) return juce::jmax (s->peakL.load(), s->peakR.load());
        return 0.0f;
    }

    // Follow the mix (changes made elsewhere) and redraw the meters.
    void timerCallback() override
    {
        for (auto* c : columns)
        {
            if (auto* gain = gainOf (c->key); gain != nullptr && ! c->fader.isMouseButtonDown())
                c->fader.setValue (juce::Decibels::gainToDecibels (gain->load(), -60.0f), juce::dontSendNotification);
            if (auto* m = muteOf (c->key))
                c->mute.setToggleState (m->load(), juce::dontSendNotification);
            c->level = peakOf (c->key);
        }
        repaint();
    }

    AudioEngine& engine;
    std::function<void()> open;
    juce::Label title { {}, Product::nameUpper };
    juce::TextButton openKey { "OPEN" };
    juce::OwnedArray<Column> columns;
};

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../ProductName.h"
#include "../engine/AudioEngine.h"
#include "Console.h"

// The mini mixer (in its own window: MainComponent::toggleMini, from the tray icon, MINI or FIND):
// a small console with a fader, meter and CUT for each strip in the session's Tray list (the
// outputs if there's none), which IN THE MINI MIXER on a strip's menu ticks. OPEN brings up the full window; PIN keeps it on top
// of everything (a game). Strips are looked up by name every frame, so it's safe while the mix
// changes under it.
class TrayMixer : public juce::Component,
                  private juce::Timer
{
public:
    TrayMixer (AudioEngine& e, std::function<void()> openWindow, bool pinned, std::function<void (bool)> pinChanged)
        : engine (e), open (std::move (openWindow)), pin (std::move (pinChanged))
    {
        title.setFont (Console::legendFont (Console::Type::body));
        addAndMakeVisible (title);
        openKey.setTooltip (juce::String ("Open ") + Product::name);
        openKey.onClick = [this] { if (open) open(); };
        addAndMakeVisible (openKey);
        pinKey.setTooltip ("On: the mini mixer stays on top of everything");
        pinKey.setClickingTogglesState (true);
        pinKey.setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
        pinKey.setToggleState (pinned, juce::dontSendNotification);
        pinKey.onClick = [this] { if (pin) pin (pinKey.getToggleState()); };
        addAndMakeVisible (pinKey);
        build();
        startTimerHz (30);
    }

    // Its strips made again (and the window fitted to them) when the list changes: a strip ticked or
    // unticked IN THE MINI MIXER, renamed or removed.
    void build()
    {
        shown = wanted();
        columns.clear();
        for (auto& key : shown)
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
        setSize (juce::jmax (Space::columns (2) + 64 + Space::unit + 2 * Space::pad, columns.size() * (columnWidth + Space::gap) - Space::gap + 2 * Space::pad), 300);
        resized();
        timerCallback();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Console::Colours::panel());
        // Each strip's meter beside its fader: round LEDs on the selected meter standard (its own
        // LEDs and colours, as the desk's; no room here for its marks), the highest lit held a moment.
        const auto& st = Metering::standard();
        const int count = (int) st.leds.size();
        const auto now = juce::Time::getMillisecondCounter();
        for (auto* c : columns)
        {
            const auto r = c->meter.toFloat();
            const float pitch = r.getHeight() / (float) juce::jmax (1, count), d = juce::jmin (r.getWidth() - 2.0f, pitch * 0.72f);
            const int on = Metering::ledsLit (st, Metering::toDb (c->level));
            if (on >= c->held || now - c->heldAt > 1500)   // (PEAK HOLD, as the desk's)
            {
                c->held = on;
                c->heldAt = now;
            }
            for (int i = 0; i < count; ++i)
                LargeFormat::led (g, { r.getCentreX(), r.getBottom() - pitch * ((float) i + 0.5f) }, i < on || i == c->held - 1,
                                  Metering::ledColour (st, st.leds[(size_t) i]), d);
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
        top.right (openKey).right (pinKey);
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
    static constexpr int columnWidth = 64, meterWidth = 10;

    struct Column
    {
        explicit Column (const juce::String& k) : key (k) {}
        juce::String key;
        juce::Label name;
        juce::Slider fader;
        juce::TextButton mute { "CUT" };
        juce::Rectangle<int> meter;
        float level = 0.0f;              // the meter standard's value (linear), the louder side
        int held = 0;                    // PEAK HOLD: LEDs lit at the hold...
        juce::uint32 heldAt = 0;         // ...since
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

    // What a strip's meter reads: the engine's measure on the selected meter standard (the louder side).
    float levelOf (const juce::String& key)
    {
        const auto kind = key.upToFirstOccurrenceOf (":", false, false), name = key.fromFirstOccurrenceOf (":", false, false);
        if (kind == "output") { for (auto& b : engine.getMasters()) if (b->name == name) return juce::jmax (b->levels.meterL.load(), b->levels.meterR.load()); return 0.0f; }
        for (auto& s : (kind == "bus" ? engine.getBuses() : engine.getStrips())) if (s->name == name) return juce::jmax (s->levels.meterL.load(), s->levels.meterR.load());
        return 0.0f;
    }

    // The session's Tray list (the outputs if there's none), the strips still there.
    juce::StringArray wanted()
    {
        auto keys = engine.trayStrips;
        if (keys.isEmpty())
            for (auto& b : engine.getMasters())
                keys.add ("output:" + b->name);
        juce::StringArray there;
        for (auto& key : keys)
            if (gainOf (key) != nullptr)
                there.add (key);
        return there;
    }

    // Follow the mix (changes made elsewhere) and redraw the meters.
    void timerCallback() override
    {
        if (wanted() != shown)
            return build();
        for (auto* c : columns)
        {
            if (auto* gain = gainOf (c->key); gain != nullptr && ! c->fader.isMouseButtonDown())
                c->fader.setValue (juce::Decibels::gainToDecibels (gain->load(), -60.0f), juce::dontSendNotification);
            if (auto* m = muteOf (c->key))
                c->mute.setToggleState (m->load(), juce::dontSendNotification);
            c->level = levelOf (c->key);
        }
        repaint();
    }

    AudioEngine& engine;
    std::function<void()> open;
    std::function<void (bool)> pin;
    juce::Label title { {}, Product::nameUpper };
    juce::TextButton openKey { "OPEN" }, pinKey { "PIN" };
    juce::OwnedArray<Column> columns;
    juce::StringArray shown;   // the keys `columns` were made for
};

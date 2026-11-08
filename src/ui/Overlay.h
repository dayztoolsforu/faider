#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/AudioEngine.h"
#include "Console.h"

// The in-game overlay: a small panel that stays on top of everything (a game in a borderless
// window, a browser), showing each mic channel's state (LIVE or CUT, a light while you talk,
// its level) and the scenes as keys. It never takes the keyboard from the game: clicking it
// doesn't activate it (Windows' "no activate" style). Drag it anywhere; Ctrl+Shift+O (or the
// command palette) shows and hides it.
class Overlay : public juce::Component,
                private juce::Timer
{
public:
    explicit Overlay (AudioEngine& e) : engine (e)
    {
        setOpaque (false);
        setAlwaysOnTop (true);
        setWantsKeyboardFocus (false);
        startTimerHz (20);
    }

    // Shown on the desktop at `where` (its top-left), or hidden.
    void show (bool on, juce::Point<int> where)
    {
        if (! on)
        {
            removeFromDesktop();
            setVisible (false);
            return;
        }
        rebuild();
        setTopLeftPosition (where);
        setVisible (true);
        addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses
                      | juce::ComponentPeer::windowIsSemiTransparent);
        makeNoActivate();
        toFront (false);
    }
    bool isShowing() const { return isOnDesktop(); }

    std::function<void (juce::Point<int>)> onMoved;   // dragged: where it is now (to remember)

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (Console::Colours::panelDark().withAlpha (0.86f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Console::Colours::rim());
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        int y = pad;
        for (auto& m : mics)
        {
            auto row = juce::Rectangle<int> (pad, y, getWidth() - 2 * pad, rowH);
            y += rowH + Console::Space::unit;
            const auto* s = strip (m);
            if (s == nullptr)
                continue;
            const bool live = s->enabled.load() && ! s->mute.load();
            const float level = juce::jmax (s->sourcePeakL.load(), s->sourcePeakR.load());
            const bool talking = level > juce::Decibels::decibelsToGain (-40.0f);

            // Talking light, the name, LIVE / CUT, and the level.
            Console::drawLED (g, { (float) row.getX() + 6.0f, (float) row.getCentreY() }, 5.0f,
                              live ? Console::Colours::selected() : Console::Colours::danger(), talking ? 1.0f : 0.15f);
            row.removeFromLeft (16);
            auto state = row.removeFromRight (64).reduced (0, 3);
            g.setColour ((live ? Console::Colours::selected() : Console::Colours::danger()).withAlpha (talking || ! live ? 0.95f : 0.5f));
            g.fillRoundedRectangle (state.toFloat(), 3.0f);
            g.setColour (juce::Colours::black);
            g.setFont (Console::legendFont (Console::Type::legend));
            g.drawText (live ? "LIVE" : "CUT", state, juce::Justification::centred);
            row.removeFromRight (Console::Space::gap);
            auto meter = row.removeFromBottom (4);
            g.setColour (Console::Colours::legend());
            g.setFont (Console::legendFont (Console::Type::body));
            g.drawFittedText (m, row, juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (Console::Colours::slot());
            g.fillRect (meter);
            const float db = juce::Decibels::gainToDecibels (level, -60.0f);
            g.setColour (live ? Console::Colours::selected() : Console::Colours::legendDim());
            g.fillRect (meter.withWidth (juce::roundToInt (meter.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f))));
        }
        if (mics.isEmpty())
        {
            g.setColour (Console::Colours::legendDim());
            g.setFont (Console::legendFont (Console::Type::legend));
            g.drawText ("NO MIC CHANNELS", juce::Rectangle<int> (pad, pad, getWidth() - 2 * pad, rowH), juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        int y = pad + juce::jmax (1, mics.size()) * (rowH + Console::Space::unit) + Console::Space::unit;
        Console::KeyRow row ({ pad, y, getWidth() - 2 * pad, Console::Space::keySmall }, Console::Space::keySmall);
        for (auto* k : sceneKeys)
            row.leftWidth (*k, (getWidth() - 2 * pad - (sceneKeys.size() - 1) * Console::Space::unit) / juce::jmax (1, sceneKeys.size()));
    }

    void mouseDown (const juce::MouseEvent& e) override { dragger.startDraggingComponent (this, e); }
    void mouseDrag (const juce::MouseEvent& e) override { dragger.dragComponent (this, e, nullptr); }
    void mouseUp (const juce::MouseEvent&) override { if (onMoved) onMoved (getPosition()); }

private:
    static constexpr int pad = Console::Space::gap, rowH = 26, width = 240;

    const AudioEngine::Strip* strip (const juce::String& name) const
    {
        for (auto& s : engine.getStrips())
            if (s->name == name)
                return s.get();
        return nullptr;
    }

    // The mic channels and scenes as they are now (they change while it's up: it follows).
    void rebuild()
    {
        juce::StringArray nowMics, nowScenes;
        for (auto& s : engine.getStrips())
            if (s->source.kind == AudioEngine::Source::interfaceInputs || s->source.kind == AudioEngine::Source::device)
                nowMics.add (s->name);
        for (auto& sc : engine.getScenes().list)
            nowScenes.add (sc.name);
        if (nowMics == mics && nowScenes == scenes && getWidth() > 0)
            return;
        mics = nowMics;
        scenes = nowScenes;
        sceneKeys.clear();
        for (int i = 0; i < scenes.size() && i < 4; ++i)   // room for four
        {
            auto* k = sceneKeys.add (new juce::TextButton (scenes[i]));
            k->setWantsKeyboardFocus (false);
            k->setClickingTogglesState (false);
            k->onClick = [this, i] { engine.getScenes().recall (i); };
            addAndMakeVisible (k);
        }
        const int keys = sceneKeys.isEmpty() ? 0 : Console::Space::keySmall + Console::Space::unit;
        setSize (width, 2 * pad + juce::jmax (1, mics.size()) * (rowH + Console::Space::unit) + keys);
        resized();
    }

    void timerCallback() override
    {
        if (! isOnDesktop())
            return;
        rebuild();
        for (int i = 0; i < sceneKeys.size(); ++i)   // the active scene lit
            sceneKeys[i]->setColour (juce::TextButton::buttonColourId, i == engine.getScenes().active ? Console::Colours::selected().withAlpha (0.6f)
                                                                                                     : Console::Colours::capTop());
        repaint();
    }

    void makeNoActivate();   // Overlay.cpp (Windows)

    AudioEngine& engine;
    juce::StringArray mics, scenes;
    juce::OwnedArray<juce::TextButton> sceneKeys;
    juce::ComponentDragger dragger;
};

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/AudioEngine.h"
#include "../engine/Scenes.h"
#include "ProStrip.h"
#include "Console.h"
#include "Theme.h"

// COMPARE MIXES: as the console keeps every mix version (the producer's, the client's, "the
// client's mother's") to flip between live. Three slots, each a saved scene: A, B, C recall it
// at once (faders glide); under them, what that version changes against the mix as it is now.
class CompareView : public juce::Component, private juce::Timer
{
public:
    explicit CompareView (AudioEngine& e) : engine (e)
    {
        auto& scenes = engine.getScenes();
        for (int s = 0; s < 3; ++s)
        {
            for (auto& sc : scenes.list)
                pick[s].addItem (sc.name, pick[s].getNumItems() + 1);
            if (s < (int) scenes.list.size())
                pick[s].setSelectedItemIndex (s, juce::dontSendNotification);
            pick[s].onChange = [this] { repaint(); };
            addAndMakeVisible (pick[s]);
            ProStrip::styleKey (slot[s]);
            slot[s].setClickingTogglesState (false);
            slot[s].setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + s)));
            slot[s].onClick = [this, s] { recall (s); };
            addAndMakeVisible (slot[s]);
        }
        setSize (560, 420);
        startTimerHz (4);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = Theme::get();
        g.fillAll (t.panel);
        g.setColour (t.legend);
        g.setFont (Console::legendFont (Console::Type::display));
        g.drawText ("COMPARE MIXES", getLocalBounds().reduced (12).removeFromTop (26), juce::Justification::centredLeft);
        if (engine.getScenes().list.empty())
        {
            g.setFont (Console::legendFont (Console::Type::body));
            g.setColour (t.legendDim);
            g.drawText ("SAVE A FEW SCENES FIRST (SCENES +): EACH IS A MIX VERSION TO COMPARE", getLocalBounds(), juce::Justification::centred, false);
            return;
        }
        g.setFont (Console::legendFont (Console::Type::tag));
        g.setColour (t.legendDim);
        g.drawText ("WHAT " + juce::String::charToString ((juce::juce_wchar) ('A' + focus)) + " CHANGES AGAINST THE MIX NOW:", diffArea().removeFromTop (16), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (Console::Type::body));
        auto area = diffArea().withTrimmedTop (20);
        const auto lines = differences (focus);
        g.setColour (lines.isEmpty() ? t.ledGreen : t.legend);
        if (lines.isEmpty())
            g.drawText ("NOTHING: THIS IS THE MIX NOW", area.removeFromTop (18), juce::Justification::centredLeft);
        for (auto& line : lines)
        {
            if (area.getHeight() < 18)
                break;
            g.drawText (line, area.removeFromTop (18), juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12).withTrimmedTop (34);
        auto keys = r.removeFromTop (54);
        const int w = keys.getWidth() / 3;
        for (int s = 0; s < 3; ++s)
        {
            auto col = keys.removeFromLeft (w).reduced (4, 0);
            slot[s].setBounds (col.removeFromTop (28));
            col.removeFromTop (4);
            pick[s].setBounds (col.removeFromTop (22));
        }
    }

private:
    juce::Rectangle<int> diffArea() const { return getLocalBounds().reduced (12).withTrimmedTop (100); }

    int sceneFor (int s) const { return engine.getScenes().find (pick[s].getText()); }

    void recall (int s)
    {
        if (const int index = sceneFor (s); index >= 0)
        {
            engine.getScenes().recall (index);
            focus = s;
            engine.logEvent ("compare: " + juce::String::charToString ((juce::juce_wchar) ('A' + s)) + " (" + pick[s].getText() + ")");
        }
        repaint();
    }

    // A slot's scene against the mix now: per channel, bus or master, what it would change.
    juce::StringArray differences (int s) const
    {
        juce::StringArray lines;
        const int index = sceneFor (s);
        if (index < 0)
            return lines;
        const auto& scene = engine.getScenes().list[(size_t) index];
        const auto now = engine.getScenes().capture ({});
        for (auto& set : scene.settings)
        {
            const auto it = std::find_if (now.settings.begin(), now.settings.end(), [&set] (auto& x) { return x.target == set.target; });
            if (it == now.settings.end())
                continue;
            juce::StringArray parts;
            const float db = juce::Decibels::gainToDecibels (set.gain, -100.0f) - juce::Decibels::gainToDecibels (it->gain, -100.0f);
            if (std::abs (db) > 0.2f)
                parts.add ((db > 0 ? "+" : "") + juce::String (db, 1) + " dB");
            if (std::abs (set.pan - it->pan) > 0.02f)
                parts.add ("pan " + juce::String (juce::roundToInt (set.pan * 100.0f)));
            if (set.mute != it->mute) parts.add (set.mute ? "CUT" : "un-cut");
            if (set.solo != it->solo) parts.add (set.solo ? "SOLO" : "solo off");
            if (set.on != it->on) parts.add (set.on ? "switched on" : "switched off");
            for (auto& [to, on] : set.sends)
                for (auto& [toNow, onNow] : it->sends)
                    if (to == toNow && on != onNow)
                        parts.add ((on ? "to " : "not to ") + to.fromFirstOccurrenceOf (":", false, false));
            if (! parts.isEmpty())
                lines.add (set.target.fromFirstOccurrenceOf (":", false, false) + ":  " + parts.joinIntoString (", "));
        }
        return lines;
    }

    void timerCallback() override
    {
        const int active = engine.getScenes().active;
        for (int s = 0; s < 3; ++s)
            slot[s].setToggleState (active >= 0 && active == sceneFor (s), juce::dontSendNotification);
        repaint (diffArea());
    }

    AudioEngine& engine;
    juce::ComboBox pick[3];
    juce::TextButton slot[3];
    int focus = 0;
};

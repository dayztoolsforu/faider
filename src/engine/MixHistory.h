#pragma once

#include "AudioEngine.h"

// Undo for mix changes: faders, pans, mutes, solos, power, routing and IN keys (what a scene
// holds, Scenes.h). The mix is looked at five times a second; once it has settled after a change
// (0.6 s without another), the mix from before the change is kept as an undo step. Undo glides
// back to it; redo goes forward again.
//
// Strips and plugins added or removed are steps too, in the same order (addStep): the owner
// (MainComponent) says how to undo and redo each, since putting a strip back takes the session
// code. The mix seen straight after one of those is the new starting point, not a mix change.
class MixHistory : private juce::Timer
{
public:
    explicit MixHistory (AudioEngine& e) : engine (e)
    {
        last = engine.getScenes().capture ({});
        startTimerHz (5);
    }

    bool undo() { return step (true); }
    bool redo() { return step (false); }

    // What each step did, in words ("MICROPHONE fader", "paste EQ onto MIC"), oldest first: the
    // steps undo would take back, and those redo would do again (the next redo last).
    juce::StringArray undoLabels() const { return labels (undoSteps); }
    juce::StringArray redoLabels() const { return labels (redoSteps); }

    // A strip or plugin is being added or removed (right now): how to take it back and do it
    // again, and what to call it.
    void addStep (std::function<void()> undoIt, std::function<void()> redoIt, const juce::String& label = {})
    {
        if (pending)
            settle();
        undoSteps.push_back ({ {}, std::move (undoIt), std::move (redoIt), label.isNotEmpty() ? label : juce::String ("a change to the channels") });
        trim();
        redoSteps.clear();
        rebaseline = true;
    }

    // True while an undo or redo is putting strips or plugins back (their own changes aren't steps).
    bool isReplaying() const { return replaying; }

private:
    static constexpr size_t maxSteps = 50;

    // A step: a mix to glide back to, or a structural change with its own undo and redo.
    struct Step
    {
        Scenes::Scene mix;
        std::function<void()> undo, redo;
        juce::String label;
        bool structural() const { return undo != nullptr; }
    };

    static juce::StringArray labels (const std::vector<Step>& steps)
    {
        juce::StringArray list;
        for (auto& s : steps)
            list.add (s.label);
        return list;
    }

    // What changed from one mix to the next, in words: the channel and what of it, or how many.
    static juce::String describe (const Scenes::Scene& from, const Scenes::Scene& to)
    {
        juce::StringArray what;
        juce::String channel;
        int channels = 0;
        for (const auto& x : from.settings)
        {
            const auto y = std::find_if (to.settings.begin(), to.settings.end(), [&x] (const auto& s) { return s.target == x.target; });
            if (y == to.settings.end())
                continue;
            juce::StringArray here;
            if (std::abs (juce::Decibels::gainToDecibels (x.gain, -100.0f) - juce::Decibels::gainToDecibels (y->gain, -100.0f)) > 0.05f) here.add ("fader");
            if (std::abs (x.pan - y->pan) > 0.005f) here.add ("pan");
            if (x.mute != y->mute) here.add (y->mute ? "mute" : "unmute");
            if (x.solo != y->solo) here.add ("solo");
            if (x.on != y->on) here.add (y->on ? "on" : "off");
            if (x.insertsOn != y->insertsOn) here.add ("inserts");
            if (x.sends != y->sends) here.add ("routing");
            if (here.isEmpty())
                continue;
            ++channels;
            channel = x.target.fromFirstOccurrenceOf (":", false, false);
            what = here;
        }
        if (channels == 0) return "a mix change";
        if (channels > 1)  return juce::String (channels) + " channels";
        return channel + " " + what.joinIntoString (", ");
    }

    static bool sameMix (const Scenes::Scene& a, const Scenes::Scene& b)
    {
        // By channel, not by position: a channel moved along the desk isn't a change to undo.
        if (a.settings.size() != b.settings.size())
            return false;
        for (const auto& x : a.settings)
        {
            const auto match = std::find_if (b.settings.begin(), b.settings.end(), [&x] (const auto& s) { return s.target == x.target; });
            if (match == b.settings.end())
                return false;
            const auto& y = *match;
            if (x.mute != y.mute || x.solo != y.solo || x.on != y.on || x.insertsOn != y.insertsOn
                || x.sends != y.sends || std::abs (x.pan - y.pan) > 0.005f
                || std::abs (juce::Decibels::gainToDecibels (x.gain, -100.0f) - juce::Decibels::gainToDecibels (y.gain, -100.0f)) > 0.05f)
                return false;
        }
        return true;
    }

    // Same set of channels (by target), whatever their settings.
    static bool sameTargets (const Scenes::Scene& a, const Scenes::Scene& b)
    {
        if (a.settings.size() != b.settings.size())
            return false;
        for (const auto& x : a.settings)
            if (std::none_of (b.settings.begin(), b.settings.end(), [&x] (const auto& s) { return s.target == x.target; }))
                return false;
        return true;
    }

    void timerCallback() override
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now < ignoreUntil)   // an undo or redo gliding into place
            return;
        auto snap = engine.getScenes().capture ({});
        if (gliding)
        {
            // Landed. Anything touched while it glided differs from where it was going: that's a
            // change of its own (undoable back to the undo's result), not part of the undo.
            // If the channels themselves differ (a rename, a strip added or gone meanwhile), the
            // target can't be compared: what it landed on is the new starting point, not a step,
            // or undo would queue the same mix again and get stuck.
            gliding = false;
            if (sameTargets (snap, glideTarget) && ! sameMix (snap, glideTarget))
            {
                before = glideTarget;
                pending = true;
                changedAt = now;
            }
            last = std::move (snap);
            return;
        }
        if (rebaseline)   // the structure just changed
        {
            rebaseline = false;
            last = std::move (snap);
            return;
        }

        if (! sameMix (snap, last))
        {
            if (! pending)
            {
                before = last;
                pending = true;
            }
            changedAt = now;
            last = std::move (snap);
        }
        else if (pending && now - changedAt > 600.0)
            settle();
    }

    // The change has settled: what was before it becomes an undo step.
    void settle()
    {
        pending = false;
        auto label = describe (before, last);
        undoSteps.push_back ({ std::move (before), {}, {}, std::move (label) });
        trim();
        redoSteps.clear();
    }

    void trim()
    {
        if (undoSteps.size() > maxSteps)
            undoSteps.erase (undoSteps.begin());
    }

    bool step (bool undoing)
    {
        if (pending)
            settle();   // a change still settling counts as done
        auto& from = undoing ? undoSteps : redoSteps;
        auto& to = undoing ? redoSteps : undoSteps;
        if (from.empty())
            return false;

        auto s = std::move (from.back());
        from.pop_back();
        if (s.structural())
        {
            replaying = true;
            (undoing ? s.undo : s.redo)();
            replaying = false;
            rebaseline = true;
            gliding = false;   // the structure changed under any glide: start afresh from here
            ignoreUntil = 0.0;
            to.push_back (std::move (s));
            return true;
        }

        // Mid-glide the mix is half way there: the opposite step is where it was going.
        to.push_back ({ gliding ? glideTarget : engine.getScenes().capture ({}), {}, {}, s.label });
        engine.getScenes().apply (s.mix);
        ignoreUntil = juce::Time::getMillisecondCounterHiRes() + Scenes::glideMs + 300.0;
        glideTarget = s.mix;
        gliding = true;
        return true;
    }

    AudioEngine& engine;
    Scenes::Scene last, before, glideTarget;   // glideTarget: where an undo or redo is gliding to
    std::vector<Step> undoSteps, redoSteps;
    bool pending = false, rebaseline = false, replaying = false, gliding = false;
    double changedAt = 0.0, ignoreUntil = 0.0;
};

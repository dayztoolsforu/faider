#pragma once

#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <memory>
#include "TriggerWatch.h"
#include <vector>

class AudioEngine;

// Macros: one press that does several mixer things at once ("Go live": mute the music, open the
// mic, turn on the stream output). A macro is a list of steps run in order; waits and level fades
// run on a timer. Fired by a held trigger (a pad, a hotkey) with "undo on release" set, it puts
// back everything it changed when the trigger is let go (a "Duck" or "Talkback" key).
//
// Steps name what they act on, not where it is ("input:MIC FOCUS", "bus:SUMMING",
// "output:MISC", "pad:3"), so they keep working when strips are added or moved. Message thread only.
class Macros : private juce::Timer
{
public:
    enum class Action   // saved as numbers: append only
    {
        muteOn, muteOff, muteToggle,      // target
        level,                            // target to `value` dB, faded over `ms`
        sendOn, sendOff, sendToggle,      // target (an input or bus) sends to target2 (a bus or output)
        powerOn, powerOff,                // target switched on/off
        playPad, stopPad, stopAllPads,    // target "pad:N"
        tapePlayPause, tapeNext,
        recordStart, recordStop,          // the recorder's chosen source (TAPE tab)
        wait,                             // `ms`
        scene,                            // target "scene:NAME" recalled (Scenes.h)
        fxOn, fxOff, fxToggle,            // target (an input or bus) has voice FX target2 ("fx:ROBOT") in/out
        defaultOutput, defaultInput,      // Windows' default playback / recording device: target "device:NAME"
        pluginSet                         // target's insert's control target2 ("plug:PLUGIN:N") set to `value` (0-100 %)
    };

    struct Step
    {
        Action action = Action::muteToggle;
        juce::String target, target2;
        float value = 0.0f;   // level: dB
        int ms = 0;           // level: fade time; wait: how long
    };

    // Something that happens and fires the macro by itself, as a click would (TriggerWatch.h).
    using Trigger = MacroTrigger;

    struct Macro
    {
        juce::String name;
        std::vector<Step> steps;
        bool undoOnRelease = false;   // fired by a held trigger: put things back on release
        juce::String hotkey;          // a global hotkey (juce::KeyPress text), or empty
        std::vector<Trigger> triggers;
    };

    explicit Macros (AudioEngine&);
    ~Macros() override;

    std::vector<Macro> list;
    int find (const juce::String& name) const;   // -1 if there's none

    // Run a macro (a click). press/release: a held trigger; release undoes it if the macro says so.
    std::function<void (int index)> onStarted;   // a macro starting (whichever thread started it)
    void run (int index)     { start (index, false); }
    void press (int index)   { start (index, true); }
    void release (int index);
    bool isRunning (int index) const;
    void cancelAll();   // before macros are removed or reordered

    // Called after a step switches a strip on or off (the Mixer rebuilds).
    std::function<void()> onStructureChanged;

    // Once a second: runs the macros whose triggers happened since the last call. `apps`: the exe
    // names playing audio now; `devices`: the names of the devices connected now; `now`: the
    // time. The first call only notes how things are (what's already running doesn't fire).
    // Returns what fired, for the event log ("DISCORD.EXE STARTS -> GO LIVE").
    juce::StringArray checkTriggers (const juce::StringArray& apps, const juce::StringArray& devices, juce::Time now);

    static juce::String actionName (Action);
    static bool needsTarget (Action);
    static bool needsTarget2 (Action);

private:
    struct Run;
    void start (int index, bool held);
    void timerCallback() override;
    void advance (Run&);                                   // runs steps until a wait or the end
    void apply (Run&, const Step&);
    std::atomic<float>* gainOf (const juce::String& target) const;   // nullptr: no such strip
    std::atomic<bool>* muteOf (const juce::String& target) const;
    bool sendIsOn (const juce::String& from, const juce::String& to) const;
    void setSend (const juce::String& from, const juce::String& to, bool on);
    bool isOn (const juce::String& target) const;          // switched on (power)
    void setOn (const juce::String& target, bool on);

    AudioEngine& engine;
    std::vector<std::unique_ptr<Run>> running;

    TriggerWatch triggerWatch;   // checkTriggers' memory of how things were
};

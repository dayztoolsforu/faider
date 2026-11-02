#pragma once

#include <juce_events/juce_events.h>
#include <functional>
#include <optional>
#include "TriggerWatch.h"
#include <utility>
#include <vector>

class AudioEngine;

// Scenes: the whole mix saved under a name (GAMING, CALL, STREAM) and brought back with a click, a
// hotkey, the tray or a macro. Faders and pans glide to their new places; mutes, solos, power and
// sends switch at once. Devices and the strips themselves aren't part of a scene; plugins' settings
// are when the scene is set to keep them (withPlugins).
//
// Settings name what they belong to ("input:MIC FOCUS", "bus:SUMMING", "output:MISC"), so a scene
// still works when strips are moved; strips it doesn't know (added since) are left as they are.
// Message thread only.
class Scenes : private juce::Timer
{
public:
    // One strip's (or output's) part in a scene.
    struct Setting
    {
        juce::String target;
        float gain = 1.0f, pan = 0.0f;
        bool mute = false, solo = false, on = true, insertsOn = true;
        std::vector<std::pair<juce::String, bool>> sends;   // destination ("bus:X", "output:Y"), on
        std::vector<std::pair<juce::String, juce::MemoryBlock>> plugins;   // withPlugins: each insert's name and settings, in order
    };

    struct Scene
    {
        juce::String name;
        juce::String hotkey;   // a global hotkey (juce::KeyPress text), or empty
        juce::String device;   // recalled when this device (endpoint name) appears; the scene before comes back when it goes
        bool withPlugins = false;   // its plugins' settings are part of it too
        std::vector<Setting> settings;

        // Auto-scene: recalled by itself when one of these happens (an app starts, a device is
        // connected, a time of day: TriggerWatch.h). goBack: recalled by an app starting, the
        // scene before comes back when that app stops.
        std::vector<MacroTrigger> triggers;
        bool goBack = false;
    };

    // Once a second (while some scene has a trigger): recalls the scenes whose triggers happened,
    // and goes back after an app that recalled one stops. `apps`: exe names playing audio now.
    // Returns what happened, for the event log.
    juce::StringArray checkTriggers (const juce::StringArray& apps, const juce::StringArray& devices, juce::Time now);
    bool hasTriggers() const;

    // Device profiles: a scene's device became active or went away (call with every endpoint
    // change). Returns what happened for the log, or empty.
    juce::String devicesChanged (const juce::StringArray& activeNow);

    explicit Scenes (AudioEngine&);
    ~Scenes() override;

    std::vector<Scene> list;
    int find (const juce::String& name) const;   // -1 if there's none

    Scene capture (const juce::String& name, bool withPlugins = false) const;   // the mix as it is now (and its plugins' settings)
    // What a recall brings back: everything, the mutes (and solos) only, or the levels (faders and pans) only.
    enum class Part { all, mutes, levels };
    void recall (int index, Part = Part::all);        // glide to a saved scene
    std::function<void (int index)> onRecalled;       // after a recall (whichever thread recalled it)
    void apply (const Scene&, Part = Part::all);      // glide to any (a macro's undo)
    void remove (int index);

    int active = -1;   // the scene last recalled or saved, or -1
    void setActive (int);

    static constexpr double glideMs = 500.0;

    std::function<void()> onStructureChanged;   // a scene switched a strip on or off (the Mixer rebuilds)
    std::function<void()> onActiveChanged;      // the lit scene changed

private:
    void timerCallback() override;

    struct Glide { juce::String target; bool isPan; float from, to, end; };   // end: the exact value it lands on
    std::vector<Glide> glides;
    double glideStart = 0.0;

    juce::StringArray activeDevices;   // as last seen (devicesChanged)
    bool devicesSeen = false;
    juce::String sceneBeforeDevice;    // by name: what to go back to when the device goes

    TriggerWatch triggerWatch;
    struct Back { juce::String app, scene; };   // goBack: when `app` stops, `scene` comes back
    std::optional<Back> back;

    AudioEngine& engine;
};

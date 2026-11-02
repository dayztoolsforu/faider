#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <vector>

// Something that happens and fires a macro by itself (Macros::Trigger): an app starting or
// stopping playing audio (`what`: its exe), a device being connected or disconnected (`what`:
// part of its name), an output going silent for `seconds` (under -60 dBFS) or having sound again
// (`what`: its name), or a time of day (`what`: "HH:MM", once a day).
struct MacroTrigger
{
    enum class When { appStarts, appStops, deviceOn, deviceOff, outputSilent, outputSound, time };   // saved as numbers: append only
    When when = When::appStarts;
    juce::String what;
    int seconds = 10;   // outputSilent

    static juce::String whenName (When w)
    {
        switch (w)
        {
            case When::appStarts:    return "STARTS";
            case When::appStops:     return "STOPS";
            case When::deviceOn:     return "CONNECTED";
            case When::deviceOff:    return "DISCONNECTED";
            case When::outputSilent: return "SILENT";
            case When::outputSound:  return "HAS SOUND";
            case When::time:         return "AT";
        }
        return {};
    }
};

// Works out which macros' triggers happened, given how things are each second. Keeps what it
// saw last time: what changed since is what happened. The first look only notes how things are
// (an app already playing doesn't fire). No engine needed (tests/TriggerTest.cpp).
class TriggerWatch
{
public:
    struct Macro { juce::String name; const std::vector<MacroTrigger>* triggers; };
    struct Fired { int macro; juce::String what; };   // its index in the list, and "DISCORD.EXE STARTS"

    // `quiet`: each output by name, true while it's under -60 dBFS.
    std::vector<Fired> check (const std::vector<Macro>& macros, const juce::StringArray& apps, const juce::StringArray& devices,
                              const std::map<juce::String, bool>& quiet, juce::Time now)
    {
        std::map<juce::String, int> silent, before = silentSeconds;
        for (auto& [name, isQuiet] : quiet)
            silent[name] = isQuiet ? before[name] + 1 : 0;
        silentSeconds = silent;

        auto anyMatch = [] (const juce::StringArray& names, const juce::String& what, bool exact)
        {
            for (auto& n : names)
                if (exact ? n.equalsIgnoreCase (what) : n.containsIgnoreCase (what))
                    return true;
            return false;
        };

        std::vector<Fired> fired;
        if (started)
        {
            using W = MacroTrigger::When;
            const int day = now.getYear() * 400 + now.getDayOfYear();
            const auto clock = now.formatted ("%H:%M");
            for (int m = 0; m < (int) macros.size(); ++m)
                for (auto& t : *macros[(size_t) m].triggers)
                {
                    const auto what = t.what.trim();
                    if (what.isEmpty())
                        continue;
                    bool happened = false;
                    switch (t.when)
                    {
                        case W::appStarts:    happened = anyMatch (apps, what, true) && ! anyMatch (lastApps, what, true); break;
                        case W::appStops:     happened = ! anyMatch (apps, what, true) && anyMatch (lastApps, what, true); break;
                        case W::deviceOn:     happened = anyMatch (devices, what, false) && ! anyMatch (lastDevices, what, false); break;
                        case W::deviceOff:    happened = ! anyMatch (devices, what, false) && anyMatch (lastDevices, what, false); break;
                        case W::outputSilent: happened = silent.count (what) > 0 && silent[what] == juce::jmax (1, t.seconds); break;
                        case W::outputSound:  happened = silent.count (what) > 0 && silent[what] == 0 && before[what] >= 2; break;
                        case W::time:
                        {
                            const auto key = macros[(size_t) m].name + "|" + what;
                            happened = what == clock && firedOnDay[key] != day;
                            if (happened)
                                firedOnDay[key] = day;
                            break;
                        }
                    }
                    if (happened)
                    {
                        fired.push_back ({ m, (what + " " + MacroTrigger::whenName (t.when)).toUpperCase() });
                        break;   // once per look, however many of its triggers happened
                    }
                }
        }
        started = true;
        lastApps = apps;
        lastDevices = devices;
        return fired;
    }

private:
    bool started = false;
    juce::StringArray lastApps, lastDevices;
    std::map<juce::String, int> silentSeconds;   // each output's silence so far, in looks (seconds)
    std::map<juce::String, int> firedOnDay;      // "macro|HH:MM" -> the day it last fired
};

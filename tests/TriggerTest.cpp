// Checks macro triggers (src/engine/TriggerWatch.h) without the app: one look a second, as the
// app does. Apps and devices fire as they come and go (not what was already there at the start),
// an output's silence fires once at its time and its sound once when it comes back, and a time
// of day fires once that day. Prints PASS/FAIL per check; exits non-zero on failure.
#include "../src/engine/TriggerWatch.h"
#include <iostream>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    }
}

int main()
{
    using W = MacroTrigger::When;
    const std::vector<MacroTrigger> live { { W::appStarts, "Discord.exe" } }, off { { W::appStops, "discord.exe" } },
                                    headset { { W::deviceOn, "Index HMD" } }, unplug { { W::deviceOff, "Index HMD" } },
                                    dead { { W::outputSilent, "STREAM", 3 } }, back { { W::outputSound, "STREAM" } },
                                    clock { { W::time, "20:00" } }, empty { { W::appStarts, "" } };
    const std::vector<TriggerWatch::Macro> macros { { "LIVE", &live }, { "OFF", &off }, { "VR", &headset }, { "DESK", &unplug },
                                                    { "DEAD AIR", &dead }, { "BACK", &back }, { "SHOW", &clock }, { "NOTHING", &empty } };
    TriggerWatch watch;
    const auto at = [] (int h, int m, int s) { return juce::Time (2026, 9, 25, h, m, s); };   // 25 Oct (months from 0)
    std::map<juce::String, bool> quiet { { "STREAM", false } };

    auto names = [&] (const std::vector<TriggerWatch::Fired>& f)
    {
        juce::StringArray n;
        for (auto& x : f) n.add (macros[(size_t) x.macro].name);
        return n.joinIntoString (",");
    };

    // The first look: Spotify and the headset already there: nothing fires.
    auto f = watch.check (macros, { "Spotify.exe" }, { "Index HMD Headphones" }, quiet, at (19, 58, 0));
    check (f.empty(), "first look: what's already there doesn't fire (" + names (f) + ")");

    f = watch.check (macros, { "Spotify.exe", "Discord.exe" }, { "Index HMD Headphones" }, quiet, at (19, 58, 1));
    check (names (f) == "LIVE", "Discord starts: LIVE fires (" + names (f) + ")");
    check (! f.empty() && f[0].what == "DISCORD.EXE STARTS", "described as \"DISCORD.EXE STARTS\" (" + (f.empty() ? juce::String() : f[0].what) + ")");
    f = watch.check (macros, { "Spotify.exe", "Discord.exe" }, { "Index HMD Headphones" }, quiet, at (19, 58, 2));
    check (f.empty(), "still playing: nothing fires again");

    f = watch.check (macros, { "Spotify.exe" }, {}, quiet, at (19, 58, 3));
    check (names (f) == "OFF,DESK", "Discord stops (any case) and the headset goes: OFF and DESK (" + names (f) + ")");
    f = watch.check (macros, { "Spotify.exe" }, { "Index HMD Headphones" }, quiet, at (19, 58, 4));
    check (names (f) == "VR", "the headset comes back (part of its name): VR (" + names (f) + ")");

    // Silence: 3 looks quiet fires DEAD AIR once, at the third; sound back fires BACK once.
    quiet["STREAM"] = true;
    juce::StringArray seen;
    for (int s = 5; s <= 9; ++s)
        seen.add (names (watch.check (macros, { "Spotify.exe" }, { "Index HMD Headphones" }, quiet, at (19, 58, s))));
    check (seen.joinIntoString ("|") == "||DEAD AIR||", "STREAM silent: DEAD AIR once, after 3 s (" + seen.joinIntoString ("|") + ")");
    quiet["STREAM"] = false;
    f = watch.check (macros, { "Spotify.exe" }, { "Index HMD Headphones" }, quiet, at (19, 58, 10));
    check (names (f) == "BACK", "STREAM has sound again: BACK (" + names (f) + ")");
    f = watch.check (macros, { "Spotify.exe" }, { "Index HMD Headphones" }, quiet, at (19, 58, 11));
    check (f.empty(), "and only once");

    // 20:00: once, though the minute lasts 60 looks; not again later that day.
    int shows = 0;
    for (int s = 0; s < 60; ++s)
        shows += names (watch.check (macros, { "Spotify.exe" }, { "Index HMD Headphones" }, quiet, at (20, 0, s))) == "SHOW" ? 1 : 0;
    check (shows == 1, "20:00: SHOW fires once in that minute (" + juce::String (shows) + ")");
    // Everything goes: only the headset going is a trigger (Spotify isn't watched; NOTHING has no app named).
    f = watch.check (macros, { "" }, {}, quiet, at (20, 1, 0));
    check (names (f) == "DESK", "only watched things fire; a trigger with nothing named never does (" + names (f) + ")");

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (failures)) << "\n";
    return failures == 0 ? 0 : 1;
}

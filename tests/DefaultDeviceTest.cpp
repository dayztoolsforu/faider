// Checks the Windows default device lookup (src/devices/WindowsDefault.h) without changing
// anything: the devices list, the current defaults are among them, and asking to set a device
// that doesn't exist fails with a reason (so nothing is switched). Setting a real one isn't
// tried here: it would change this PC's default devices.
#include "../src/devices/WindowsDefault.h"
#include <iostream>

int main (int argc, char** argv)
{
    int failures = 0;
    auto check = [&failures] (bool ok, const juce::String& what) { std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n"; failures += ok ? 0 : 1; };

    // --swap NAME: a real switch, asked for by the user: playback to NAME for a moment, then back
    // to what it was, each change read back from Windows.
    if (argc > 2 && juce::String (argv[1]) == "--swap")
    {
        const juce::String to (argv[2]);
        const auto was = WindowsDefault::current (false);
        juce::String error;
        check (WindowsDefault::set (to, false, error), "switch to " + to + (error.isEmpty() ? juce::String() : ": " + error));
        const auto during = WindowsDefault::current (false);
        check (during.containsIgnoreCase (to), "Windows now has " + during);
        juce::Thread::sleep (1500);
        error.clear();
        check (WindowsDefault::set (was, false, error), "switch back to " + was + (error.isEmpty() ? juce::String() : ": " + error));
        const auto after = WindowsDefault::current (false);
        check (after == was, "Windows has " + after + " again");
        return failures == 0 ? 0 : 1;
    }

    for (bool recording : { false, true })
    {
        const auto kind = juce::String (recording ? "recording" : "playback");
        const auto all = WindowsDefault::devices (recording);
        const auto now = WindowsDefault::current (recording);
        check (! all.isEmpty(), kind + ": " + juce::String (all.size()) + " devices (" + all.joinIntoString (", ") + ")");
        check (now.isNotEmpty() && all.contains (now), kind + ": the default, " + now + ", is one of them");
        juce::String error;
        const bool refused = ! WindowsDefault::set ("NO SUCH DEVICE 1234", recording, error);
        check (refused && error.isNotEmpty(), kind + ": an unknown name is refused (" + error + ")");
        check (WindowsDefault::current (recording) == now, kind + ": the default is unchanged");
    }

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (failures)) << "\n";
    return failures == 0 ? 0 : 1;
}

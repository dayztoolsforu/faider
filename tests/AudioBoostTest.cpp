// Checks BOOST WINDOWS AUDIO's decisions (src/AudioBoost.h) without touching Windows audio: the
// helper's command line, PRE-FLIGHT's WINDOWS AUDIO verdict, and when the watch speaks up.
// Prints PASS/FAIL per check; exits non-zero on failure.
#include "../src/AudioBoost.h"
#include <iostream>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    }

    juce::StringArray args (const char* line) { return juce::StringArray::fromTokens (line, " ", {}); }
}

int main()
{
    using namespace AudioBoost;

    // The helper's command line.
    check (parseArgs (args ("--boost-audiodg"), 32).valid && parseArgs (args ("--boost-audiodg"), 32).pinCore == -1, "--boost-audiodg alone: High, cores left alone");
    check (parseArgs (args ("--boost-audiodg --pin-core 31"), 32).pinCore == 31, "--pin-core 31 on 32 cores pins to the last");
    check (parseArgs (args ("--pin-core 0 --boost-audiodg"), 4).pinCore == 0, "the flags in either order");
    check (! parseArgs (args ("--boost-audiodg --pin-core 32"), 32).valid, "a core the PC doesn't have is refused");
    check (! parseArgs (args ("--boost-audiodg --pin-core -1"), 32).valid, "a negative core is refused");
    check (! parseArgs (args ("--boost-audiodg --pin-core x"), 32).valid, "a core that isn't a number is refused");
    check (! parseArgs (args ("--boost-audiodg --pin-core"), 32).valid, "--pin-core without a core is refused");
    check (! parseArgs (args ("--boost-audiodg --pin-core 70"), 128).valid, "past 63 is refused (one affinity mask)");
    check (! parseArgs (args ("--boost-audiodg --realtime"), 32).valid, "anything else on the line is refused");
    check (! parseArgs (args ("--scan x"), 32).valid, "without --boost-audiodg it isn't the helper");
    check (commandLine (-1) == "--boost-audiodg" && commandLine (7) == "--boost-audiodg --pin-core 7", "the line the app starts it with");
    check (parseArgs (args (commandLine (7).toRawUTF8()), 8).pinCore == 7, "and the helper reads it back");

    // Every exit code says something plain, and UAC declined isn't called an error.
    for (int code : { (int) ok, (int) badArgs, (int) notFound, (int) accessDenied, (int) notSet, (int) notPinned, (int) declined, (int) notStarted, (int) timedOut })
        check (describe (code).isNotEmpty() && ! describe (code).containsIgnoreCase ("unexpected"), "code " + juce::String (code) + ": " + describe (code));
    check (describe (declined).containsIgnoreCase ("declined") && ! describe (declined).containsIgnoreCase ("error"), "declined is said plainly");
    check (describe (1).containsIgnoreCase ("unexpected"), "an unknown code is said as such");

    // PRE-FLIGHT.
    check (preflight (false, false, Priority::normal).ok, "only ASIO in use: fine at Normal");
    check (preflight (false, true, Priority::unknown).ok, "only ASIO in use: fine even unread");
    check (preflight (true, false, Priority::high).ok, "Windows devices, High: fine");
    const auto normal = preflight (true, false, Priority::normal);
    check (! normal.ok && normal.advisory, "Windows devices at Normal, never boosted: amber");
    const auto lost = preflight (true, true, Priority::normal);
    check (! lost.ok && ! lost.advisory, "Windows devices at Normal after a boost: red");
    check (! preflight (true, false, Priority::unknown).ok && preflight (true, false, Priority::unknown).advisory, "can't read it: amber");

    // The watch.
    Watch w;
    check (w.update (true, { 100, Priority::normal }) == Watch::News::restarted, "wanted, found at Normal (after a reboot): said");
    check (w.update (true, { 100, Priority::normal }) == Watch::News::none, "not said twice for the same audiodg");
    check (w.update (true, { 100, Priority::high }) == Watch::News::none, "boosted: nothing to say");
    check (w.update (true, { 200, Priority::normal }) == Watch::News::restarted, "a new audiodg at Normal: restarted");
    check (w.update (true, { 200, Priority::normal }) == Watch::News::none, "and once only");
    check (w.update (true, { 200, Priority::high }) == Watch::News::none, "boosted again");
    check (w.update (true, { 200, Priority::normal }) == Watch::News::droppedBack, "same audiodg back at Normal: dropped back");
    check (w.update (true, { 0, Priority::unknown }) == Watch::News::none, "not running: nothing said");
    check (w.update (true, { 300, Priority::unknown }) == Watch::News::none, "can't read it: nothing said");
    check (w.update (false, { 400, Priority::normal }) == Watch::News::none, "not wanted: never said");
    check (w.update (true, { 400, Priority::high }) == Watch::News::none && w.update (true, { 500, Priority::high }) == Watch::News::none,
           "restarted but High (something else boosted it): nothing said");

    return failures;
}

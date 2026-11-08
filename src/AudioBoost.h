#pragma once

#include <juce_core/juce_core.h>
#include <functional>

// BOOST WINDOWS AUDIO: Windows' audio engine (audiodg.exe, "Windows Audio Device Graph
// Isolation") raised from Normal, Windows' default, to High priority, which helps some PCs that
// crackle on Windows (WASAPI) devices and captured apps. ASIO interfaces go around it, so it
// doesn't touch them. Changing another account's process needs an administrator, so Faider (which
// runs with ordinary rights) starts itself once more, elevated, as `Faider.exe --boost-audiodg
// [--pin-core N]`: Windows asks first; that copy sets the priority (and, only if asked, pins
// audiodg to one core), says how it went in its exit code and goes, with no window and no audio.
// It doesn't last: Windows starting its audio again (or the PC) puts it back to Normal, and Faider
// says so. Never realtime: that can starve the rest of Windows. BACK TO NORMAL runs the same way
// with `--unboost-audiodg`: Normal priority, and every core again.
//
// What's here is the part without Windows in it (tested by tests/AudioBoostTest.cpp); the Windows
// part (reading audiodg, the helper, starting it elevated) is in AudioBoost.cpp.
namespace AudioBoost
{
    inline constexpr const char* flag = "--boost-audiodg";
    inline constexpr const char* pinFlag = "--pin-core";
    inline constexpr const char* backFlag = "--unboost-audiodg";

    // Whether a command line is the helper's (either way).
    inline bool isHelper (const juce::StringArray& args) { return args.contains (flag) || args.contains (backFlag); }

    // The helper's exit codes.
    enum Code : int
    {
        ok           = 0,   // High priority set (and pinned, if asked); back: Normal on every core
        badArgs      = 2,   // the command line wasn't --boost-audiodg [--pin-core N] (N a core this PC has) or --unboost-audiodg
        notFound     = 3,   // audiodg.exe isn't running (Windows audio stopped?)
        accessDenied = 4,   // audiodg couldn't be opened to change it (not elevated?)
        notSet       = 5,   // opened, but Windows refused the priority
        notPinned    = 6,   // High priority set, but the core pin was refused
        notUnpinned  = 7,   // back: Normal set, but every core again was refused
        // Not the helper's: what the app makes of starting it.
        declined     = -1,  // the user said no to Windows' prompt (a normal outcome)
        notStarted   = -2,  // it couldn't be started
        timedOut     = -3   // it didn't finish in time
    };

    // The priority classes Windows has, in order (unknown: it couldn't be read).
    enum class Priority { unknown, idle, belowNormal, normal, aboveNormal, high, realtime };

    inline juce::String priorityName (Priority p)
    {
        switch (p)
        {
            case Priority::idle:        return "IDLE";
            case Priority::belowNormal: return "BELOW NORMAL";
            case Priority::normal:      return "NORMAL";
            case Priority::aboveNormal: return "ABOVE NORMAL";
            case Priority::high:        return "HIGH";
            case Priority::realtime:    return "REALTIME";
            case Priority::unknown:     break;
        }
        return "UNKNOWN";
    }

    inline bool isBoosted (Priority p) { return p == Priority::high || p == Priority::realtime; }

    // audiodg as it is now: its process id (0: not running, or not found) and its priority.
    struct State
    {
        juce::uint32 pid = 0;
        Priority priority = Priority::unknown;
        int pinnedCore = -1;   // the one core it's held to, if just one (-1: more, or unknown)
    };

    // The helper's command line. `cores`: how many this PC has (a pin must be below it).
    struct Args
    {
        bool valid = false;
        bool back = false;  // --unboost-audiodg: Normal, every core
        int pinCore = -1;   // -1: leave which cores it runs on alone
    };

    inline Args parseArgs (const juce::StringArray& args, int cores)
    {
        Args a;
        if (args.contains (flag) == args.contains (backFlag))
            return a;   // (one or the other)
        a.back = args.contains (backFlag);
        for (int i = 0; i < args.size(); ++i)
        {
            if (args[i] == flag || args[i] == backFlag)
                continue;
            if (args[i] == pinFlag && a.pinCore < 0 && ! a.back)
            {
                const auto n = args[i + 1].trim();
                if (n.isEmpty() || ! n.containsOnly ("0123456789") || n.length() > 3)
                    return {};
                a.pinCore = n.getIntValue();
                if (a.pinCore >= cores || a.pinCore > 63)   // (an affinity mask holds 64)
                    return {};
                ++i;
                continue;
            }
            return {};   // anything else: not ours to guess at
        }
        a.valid = true;
        return a;
    }

    inline juce::String commandLine (int pinCore)
    {
        return juce::String (flag) + (pinCore >= 0 ? juce::String (" ") + pinFlag + " " + juce::String (pinCore) : juce::String());
    }
    inline juce::String backCommandLine() { return backFlag; }

    // What came of BOOST (or, `back`, of BACK TO NORMAL), in plain words.
    inline juce::String describe (int code, bool back = false)
    {
        if (back)
            switch (code)
            {
                case ok:          return "Windows audio is back at Normal priority, on every core.";
                case badArgs:     return "Nothing changed: the helper didn't understand the request.";
                case notSet:      return "Nothing changed: Windows refused Normal priority.";
                case notUnpinned: return "Windows audio is back at Normal priority, but Windows wouldn't let it use every core again.";
                case notStarted:  return "Nothing changed: the change back couldn't be started.";
                case timedOut:    return "The change back didn't answer in time. Look again in a moment.";
                default:          break;   // (the rest as for BOOST)
            }
        switch (code)
        {
            case ok:           return "Windows audio is at High priority.";
            case badArgs:      return "Nothing changed: the core to pin to isn't one this PC has.";
            case notFound:     return "Nothing changed: Windows audio (audiodg.exe) isn't running. Play a sound and try again.";
            case accessDenied: return "Nothing changed: Windows wouldn't let it be changed.";
            case notSet:       return "Nothing changed: Windows refused the new priority.";
            case notPinned:    return "Windows audio is at High priority, but Windows wouldn't pin it to one core.";            case declined:     return "Nothing changed: Windows' prompt was declined.";
            case notStarted:   return "Nothing changed: the boost couldn't be started.";
            case timedOut:     return "The boost didn't answer in time. Look again in a moment.";
            default:           break;
        }
        return "The boost ended unexpectedly (code " + juce::String (code) + ").";
    }

    // PRE-FLIGHT's WINDOWS AUDIO check. `windowsUsed`: Windows devices or app capture in the mix
    // (only an ASIO interface: Windows audio isn't in the way). `wanted`: BOOST was asked for.
    // Not ok and `advisory`: amber, a suggestion; not ok otherwise: red (the boost was on and isn't).
    struct Verdict
    {
        bool ok = true, advisory = false;
        juce::String detail;
    };

    inline Verdict preflight (bool windowsUsed, bool wanted, Priority p)
    {
        if (! windowsUsed)
            return { true, false, "Not used by your interface (ASIO goes around Windows audio)" };
        if (isBoosted (p))
            return { true, false, "Boosted: High priority" };
        if (p == Priority::unknown)
            return { false, true, "Couldn't read its priority. If Windows devices crackle, BOOST may help" };
        if (wanted)
            return { false, false, "Back at " + priorityName (p).toLowerCase() + " (Windows audio restarted): the boost is off" };
        return { false, true, priorityName (p).substring (0, 1) + priorityName (p).substring (1).toLowerCase()
                              + " priority, Windows' default. If Windows devices or captured apps crackle, BOOST may help" };
    }

    // Watching audiodg (every few seconds): says when the boost asked for has gone (Windows audio
    // restarted, or dropped back to Normal), once per audiodg process, again only after it's been
    // seen boosted since.
    class Watch
    {
    public:
        enum class News { none, restarted, droppedBack };

        News update (bool wanted, const State& now)
        {
            if (! wanted)
            {
                *this = {};
                return News::none;
            }
            if (now.pid == 0 || now.priority == Priority::unknown)
                return News::none;   // can't tell
            if (isBoosted (now.priority))
            {
                boostedPid = now.pid;
                toldPid = 0;
                return News::none;
            }
            if (toldPid == now.pid)
                return News::none;   // said already
            toldPid = now.pid;
            return boostedPid == now.pid ? News::droppedBack : News::restarted;
        }

    private:
        juce::uint32 boostedPid = 0;   // the process last seen boosted
        juce::uint32 toldPid = 0;      // the process already told about
    };

    // The Windows part (AudioBoost.cpp) ----------------------------------------------------------

    // audiodg as it is, read with ordinary rights (never changes it).
    State read();

    // How many cores this PC has (the last one is what PIN TO ONE CORE uses).
    int coreCount();

    // The helper's work (Main.cpp, an elevated copy started with --boost-audiodg or
    // --unboost-audiodg): returns its exit code.
    int runHelper (const juce::StringArray& args);

    // Starts the helper elevated (Windows asks first) and waits for it on a thread of its own, up
    // to 15 s; `done` gets its exit code, or declined, notStarted or timedOut, on the message thread.
    // `owner`: the window Windows' prompt belongs to (may be null).
    void boost (bool pinToLastCore, void* owner, std::function<void (int code)> done);

    // BACK TO NORMAL: the same, with --unboost-audiodg.
    void unboost (void* owner, std::function<void (int code)> done);
}

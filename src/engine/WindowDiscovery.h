#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include <vector>

// Finds what's on screen: each program's top-level windows and, for Chromium browsers (Chrome,
// Edge, Brave, Vivaldi, Helium...), the tabs in each window and which of them are playing.
//
// Windows captures audio per process, and a browser plays every tab through one process (its
// audio service), so a tab can't be captured on its own. A tab picked from a browser captures
// the whole browser; isolateTab() instead reopens the tab's page in a browser instance of its
// own (a separate profile under %APPDATA%\Patchbay\Isolated), whose audio can be captured alone.
namespace WindowDiscovery
{
    struct Tab
    {
        juce::String title;   // without the browser's state suffixes ("- Audio playing", "- Memory usage - 349 MB")
        bool playing = false;
    };

    struct Window
    {
        juce::pointer_sized_int handle = 0;
        uint32_t processId = 0;
        juce::String exePath, title;
        juce::String product;     // the browser's own name, from its title ("... - Helium")
        std::vector<Tab> tabs;    // a Chromium browser's tabs (empty for other programs)

        juce::String exeName() const { return juce::File (exePath).getFileName(); }
        bool isBrowser() const { return ! tabs.empty(); }
    };

    // Scans every few seconds on its own thread; snapshot() is the latest (empty until the first
    // scan finishes). Patchbay's own windows and its isolated browser instances are left out.
    // Browsers' tabs are read again only while Patchbay is in front; behind, the last ones are kept.
    class Scanner : private juce::Thread
    {
    public:
        Scanner();
        ~Scanner() override;

        std::vector<Window> snapshot() const;

    private:
        void run() override;

        juce::CriticalSection lock;
        std::vector<Window> latest;
    };

    //==========================================================================
    // Isolated tabs. A strip's source for one is named "isolated:<n>|<browser exe>|<address>":
    // slot n has its own profile folder, and the exe and address reopen it if it was closed.

    bool isIsolated (const juce::String& sourceName);
    juce::String isolatedFolder (const juce::String& sourceName);   // its --user-data-dir

    // "Isolated tab 2" for an isolated source, else the app's name ("chrome").
    juce::String appLabel (const juce::String& sourceName);

    // Brings the tab to the front of its window (to read its full address), then opens that
    // address in a new isolated instance, in a slot no source in `inUse` has. Returns the new
    // source name, or empty with `error` set. Call on the message thread: it waits for the page,
    // in 50 ms steps, up to about 1 s (and a call to a hung browser gives up after 1 s).
    juce::String isolateTab (const Window&, const juce::String& tabTitle, const juce::StringArray& inUse, juce::String& error);

    // Opens a closed isolated instance again at its saved address.
    bool reopen (const juce::String& sourceName);

    // The running browser process of an isolated instance (0 if closed).
    uint32_t findIsolatedProcess (const juce::String& sourceName);

    // A process's program file (its full path) and command line, empty if they can't be read.
    juce::String exePathOf (uint32_t processId);
    juce::String commandLineOf (uint32_t processId);

    // Whether a process belongs to one of Patchbay's isolated instances.
    bool isIsolatedProcess (uint32_t processId);
}

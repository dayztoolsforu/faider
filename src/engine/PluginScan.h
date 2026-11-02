#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>

// Plugins are tried out in a separate copy of Patchbay before they load into the live desk, so
// one that crashes or hangs takes down only that copy. "Patchbay.exe --scan <plugin> --out <file>"
// loads the plugin, runs a block of silence through it, closes it and writes its name and maker
// to the file. One that passes is remembered (it isn't tested again until its file changes);
// one that fails is too, with why, and isn't loaded until it's tested again.
// Kept in %APPDATA%\Patchbay\plugins.xml.
namespace PluginScan
{
    // In the child copy (Main.cpp): the test itself. Returns the exit code (0: it passed).
    int runChild (const juce::String& pluginPath, const juce::File& out);

    struct Entry
    {
        bool passed = false;
        juce::String name, maker, reason;   // reason: why it failed (on a pass, a note, if any)
    };

    // What's known about the plugin at `path` as its file is now (empty optional: never tested,
    // or changed since). Any thread.
    std::optional<Entry> find (const juce::String& path);

    // Tests the plugin in a child copy and records the result: empty if it passed, else why not.
    // Blocks until the child finishes (at most `timeoutMs`): call it off the message thread.
    juce::String test (const juce::String& path, int timeoutMs = 30000);

    // Forgets a result, so the plugin is tested again next time.
    void forget (const juce::String& path);
}

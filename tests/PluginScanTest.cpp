// Checks plugin testing in a separate copy (src/engine/PluginScan.h). This program is its own
// child copy: run with --scan it does what Faider.exe --scan does.
//  1. Faider's test plugin (Faider Test Delay) passes, with its name and maker remembered;
//  2. a folder with no plugin in it fails, and is remembered as failed;
//  3. a test that takes too long is stopped and fails;
//  4. forget() clears a result.
// The results go in %APPDATA%\FaiderPluginScanTest\plugins.xml (this target's own data folder, never
// Faider's); each is forgotten again after.
// Prints PASS/FAIL per check; exits non-zero on failure.
#include "../src/engine/PluginScan.h"

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    juce::StringArray args (argv, argc);
    if (args.contains ("--scan"))   // the child copy
        return PluginScan::runChild (args[args.indexOf ("--scan") + 1], juce::File (args[args.indexOf ("--out") + 1]));

    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto delay = exe.getParentDirectory().getParentDirectory().getParentDirectory()
                          .getChildFile ("FaiderTestDelay_artefacts").getChildFile ("Release").getChildFile ("VST3").getChildFile ("Faider Test Delay.vst3");
    check (delay.exists(), "the test plugin is built: " + delay.getFullPathName());
    const auto path = delay.getFullPathName();

    {
        const auto error = PluginScan::test (path, 1);
        const auto known = PluginScan::find (path);
        check (error.contains ("didn't finish") && known && ! known->passed, "too slow (1 ms allowed): stopped and failed (" + error + ")");
        PluginScan::forget (path);
        check (! PluginScan::find (path), "forget: no result any more");
    }

    {
        const auto error = PluginScan::test (path);
        const auto known = PluginScan::find (path);
        check (error.isEmpty() && known && known->passed && known->name.isNotEmpty(),
               "the test plugin passes: " + (known ? known->name + " by " + known->maker : error));
        PluginScan::forget (path);
    }

    {
        const auto empty = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NotAPlugin.vst3");
        empty.createDirectory();
        const auto error = PluginScan::test (empty.getFullPathName());
        const auto known = PluginScan::find (empty.getFullPathName());
        check (error.isNotEmpty() && known && ! known->passed, "no plugin in it: fails (" + error + ")");
        PluginScan::forget (empty.getFullPathName());
        empty.deleteRecursively();
    }

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (failures)) << "\n";
    return failures == 0 ? 0 : 1;
}

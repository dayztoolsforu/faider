#pragma once

#include <juce_core/juce_core.h>

#ifndef FAIDER_DATA_FOLDER
 #define FAIDER_DATA_FOLDER "Faider"
#endif

// The program's name as people see it, and its folder in %APPDATA% (the mix, settings, logs, presets).
namespace Product
{
    inline constexpr const char* name = "Faider";
    inline constexpr const char* nameUpper = "FAIDER";

    // (faider_core is linked into programs with different data folders, so it may not use one:
    // there dataFolder() doesn't exist, and code that uses it doesn't build)
#ifndef FAIDER_NO_DATA_FOLDER
    inline juce::File dataFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (FAIDER_DATA_FOLDER);
    }

    // The folder kept under the program's earlier name moves to this one, the first time (or is
    // copied, if something still holds a file in it). Before anything opens a file in either.
    inline void moveEarlierFolder()
    {
        const auto now = dataFolder();
        const auto earlier = now.getSiblingFile ("PatchbayConsole");
        if (now.exists() || ! earlier.isDirectory())
            return;
        if (! earlier.moveFileTo (now))
            earlier.copyDirectoryTo (now);
    }
#endif
}

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
}

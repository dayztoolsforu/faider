#pragma once

#include <juce_core/juce_core.h>

#ifndef PATCHBAY_DATA_FOLDER
 #define PATCHBAY_DATA_FOLDER "PatchbayConsole"
#endif

// The program's name as people see it, and its folder in %APPDATA% (the mix, settings, logs, presets).
namespace Product
{
    inline constexpr const char* name = "Patchbay Console";
    inline constexpr const char* nameUpper = "PATCHBAY CONSOLE";

    inline juce::File dataFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (PATCHBAY_DATA_FOLDER);
    }
}

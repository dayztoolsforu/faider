#pragma once

#include <juce_core/juce_core.h>

// "Start with Windows": a value under the user's Run key pointing at this exe.
namespace StartWithWindows
{
    inline const juce::String runKeys { "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\" };
    inline const juce::String runKey { runKeys + "Patchbay Console" };

    inline bool isOn() { return juce::WindowsRegistry::valueExists (runKey); }

    inline void set (bool shouldStart)
    {
        if (shouldStart)
            juce::WindowsRegistry::setValue (runKey, "\"" + juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                                                 .getFullPathName() + "\" --minimised");
        else
            juce::WindowsRegistry::deleteValue (runKey);
    }
}

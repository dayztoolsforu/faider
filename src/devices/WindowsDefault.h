#pragma once

#include <juce_core/juce_core.h>

// Windows' default audio devices: which they are, and setting one (as the Sound settings do,
// for every use: console, multimedia and communications). Devices by their name as Windows shows
// it ("Headphones (Index HMD)"); a part of a name finds the device whose name contains it.
// Message thread.
namespace WindowsDefault
{
    juce::StringArray devices (bool recording);   // connected playback (or recording) devices
    juce::String current (bool recording);        // the default one's name, or empty
    bool set (const juce::String& name, bool recording, juce::String& error);
}

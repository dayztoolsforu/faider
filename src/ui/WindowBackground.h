#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// What Windows paints in a window's area before the program draws it: while a window is being
// resized, the newly uncovered strip shows that for a moment. JUCE's windows have none (it shows
// white), so the theme's background is given instead. All of Patchbay's windows share it.
namespace WindowBackground
{
    void set (juce::ComponentPeer&, juce::Colour);
}

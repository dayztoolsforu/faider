#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Whether Windows has arranged a window: maximised, or snapped to a side or corner (Win+arrows,
// dragged to an edge, snap layouts). Its size is then Windows', and setting it would unsnap it.
namespace WindowSnap
{
    bool isArranged (juce::ComponentPeer&);
}

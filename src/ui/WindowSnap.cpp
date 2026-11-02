#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include "WindowSnap.h"

#pragma comment (lib, "dwmapi.lib")

bool WindowSnap::isArranged (juce::ComponentPeer& peer)
{
    auto* window = (HWND) peer.getNativeHandle();
    if (window == nullptr)
        return false;
    if (IsZoomed (window))
        return true;

    // Snapped: Windows keeps the size it had before as its normal place, so the two differ...
    WINDOWPLACEMENT placement { sizeof (WINDOWPLACEMENT) };
    RECT now {};
    if (! GetWindowPlacement (window, &placement) || ! GetWindowRect (window, &now))
        return false;
    const auto& normal = placement.rcNormalPosition;
    if ((normal.right - normal.left) != (now.right - now.left) || (normal.bottom - normal.top) != (now.bottom - now.top))
        return true;

    // ...unless it was that size already (put back where it was snapped last time). Then: its
    // visible frame fills the screen's height, or sits in a corner. (Taken for snapped when it
    // isn't, the window only keeps its size exactly, which is what zoom wants anyway.)
    RECT frame = now;
    DwmGetWindowAttribute (window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof (frame));   // (without the invisible borders)
    MONITORINFO monitor { sizeof (MONITORINFO) };
    if (! GetMonitorInfoW (MonitorFromWindow (window, MONITOR_DEFAULTTONEAREST), &monitor))
        return false;
    const auto& work = monitor.rcWork;
    const bool top = frame.top <= work.top, bottom = frame.bottom >= work.bottom;
    const bool left = frame.left <= work.left, right = frame.right >= work.right;
    return (top && bottom) || ((top || bottom) && (left || right));
}

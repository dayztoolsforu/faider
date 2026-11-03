#define NOMINMAX
#include <windows.h>
#include "WindowBackground.h"

void WindowBackground::set (juce::ComponentPeer& peer, juce::Colour colour)
{
    static HBRUSH brush = nullptr;
    auto* window = (HWND) peer.getNativeHandle();
    if (window == nullptr)
        return;
    auto* fresh = CreateSolidBrush (RGB (colour.getRed(), colour.getGreen(), colour.getBlue()));
    SetClassLongPtrW (window, GCLP_HBRBACKGROUND, (LONG_PTR) fresh);   // the class's: every Faider window
    if (brush != nullptr)
        DeleteObject (brush);   // the one it replaced, no longer in use
    brush = fresh;
}

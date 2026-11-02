#ifndef NOMINMAX
 #define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Overlay.h"

// Clicking the overlay mustn't take the keyboard from the game: a "no activate" window gets
// clicks without becoming the active window.
void Overlay::makeNoActivate()
{
    if (auto* peer = getPeer())
    {
        auto hwnd = (HWND) peer->getNativeHandle();
        SetWindowLongPtrW (hwnd, GWL_EXSTYLE, GetWindowLongPtrW (hwnd, GWL_EXSTYLE) | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
    }
}

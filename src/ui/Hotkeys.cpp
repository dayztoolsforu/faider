#ifndef NOMINMAX
 #define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Hotkeys.h"

namespace
{
    constexpr const wchar_t* windowClass = L"PatchbayHotkeys";
    GlobalHotkeys* instance = nullptr;   // the one receiving WM_HOTKEY (created on the message thread)

    // JUCE's key code to a Windows virtual key: letters and digits are their own codes, special
    // keys carry JUCE's extended flag over the virtual key, punctuation goes through the layout.
    unsigned int virtualKey (const juce::KeyPress& key)
    {
        const int code = key.getKeyCode();
        if ((code & 0x10000) != 0)
            return (unsigned int) (code & 0xffff);
        if (code >= 'a' && code <= 'z') return (unsigned int) (code - 'a' + 'A');
        if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) return (unsigned int) code;
        const auto scan = VkKeyScanW ((wchar_t) code);
        return scan == -1 ? 0u : (unsigned int) (scan & 0xff);
    }

    unsigned int modifiers (const juce::KeyPress& key)
    {
        const auto m = key.getModifiers();
        return (m.isCtrlDown() ? MOD_CONTROL : 0u) | (m.isAltDown() ? MOD_ALT : 0u) | (m.isShiftDown() ? MOD_SHIFT : 0u) | MOD_NOREPEAT;
    }
}

juce::pointer_sized_int GlobalHotkeys::handleMessage (void* hwnd, unsigned int message, juce::pointer_sized_int wParam, juce::pointer_sized_int lParam)
{
    if (message == WM_HOTKEY && instance != nullptr)
    {
        instance->pressed ((int) wParam);
        return 0;
    }
    return (juce::pointer_sized_int) DefWindowProcW ((HWND) hwnd, message, (WPARAM) wParam, (LPARAM) lParam);
}

GlobalHotkeys::GlobalHotkeys()
{
    WNDCLASSEXW wc { sizeof (wc) };
    wc.lpfnWndProc = [] (HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT { return (LRESULT) handleMessage (h, m, (juce::pointer_sized_int) w, (juce::pointer_sized_int) l); };
    wc.hInstance = GetModuleHandleW (nullptr);
    wc.lpszClassName = windowClass;
    RegisterClassExW (&wc);   // fails harmlessly if already registered

    // A message-only window: it never shows, it just receives WM_HOTKEY on the message thread.
    window = CreateWindowExW (0, windowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    instance = this;
}

GlobalHotkeys::~GlobalHotkeys()
{
    clear();
    if (window != nullptr)
        DestroyWindow ((HWND) window);
    if (instance == this)
        instance = nullptr;
}

bool GlobalHotkeys::usable (const juce::KeyPress& key)
{
    const auto m = key.getModifiers();
    const auto vk = virtualKey (key);
    return vk != 0 && (m.isCtrlDown() || m.isAltDown() || (vk >= VK_F1 && vk <= VK_F24));
}

bool GlobalHotkeys::add (int id, const juce::KeyPress& key, std::function<void()> onPress, std::function<void()> onRelease)
{
    remove (id);
    const auto vk = virtualKey (key);
    if (window == nullptr || vk == 0 || ! RegisterHotKey ((HWND) window, id, modifiers (key), vk))
        return false;
    entries[id] = { vk, std::move (onPress), std::move (onRelease) };
    return true;
}

void GlobalHotkeys::remove (int id)
{
    if (entries.erase (id) > 0)
        UnregisterHotKey ((HWND) window, id);
}

void GlobalHotkeys::clear()
{
    for (auto& [id, entry] : entries)
        UnregisterHotKey ((HWND) window, id);
    entries.clear();
}

void GlobalHotkeys::pressed (int id)
{
    auto it = entries.find (id);
    if (it == entries.end() || it->second.down)
        return;
    it->second.down = true;
    if (it->second.onPress)
        it->second.onPress();
    startTimer (15);   // watch for the key coming up
}

// Windows doesn't report hotkeys being let go: poll the held ones' keys.
void GlobalHotkeys::timerCallback()
{
    bool anyDown = false;
    for (auto& [id, entry] : entries)
    {
        if (! entry.down)
            continue;
        if ((GetAsyncKeyState ((int) entry.vk) & 0x8000) == 0)
        {
            entry.down = false;
            if (entry.onRelease)
                entry.onRelease();
        }
        else
        {
            anyDown = true;
        }
    }
    if (! anyDown)
        stopTimer();
}

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>

// System-wide hotkeys: they fire while any app has focus (a game, a browser), through Windows'
// RegisterHotKey. Windows only reports the press, so a held key is watched until it's let go,
// for pads that play while held.
class GlobalHotkeys : private juce::Timer
{
public:
    GlobalHotkeys();
    ~GlobalHotkeys() override;

    // Registers `key` under `id`. False if Windows refuses: another app already owns that combination.
    bool add (int id, const juce::KeyPress& key, std::function<void()> onPress, std::function<void()> onRelease);
    void remove (int id);
    void clear();

    // Whether a key can be a hotkey at all: it needs Ctrl or Alt, or be a function key (the Win
    // key isn't passed to Windows, so it can't be part of one).
    static bool usable (const juce::KeyPress&);

private:
    struct Entry { unsigned int vk = 0; std::function<void()> onPress, onRelease; bool down = false; };

    static juce::pointer_sized_int handleMessage (void* window, unsigned int message, juce::pointer_sized_int wParam, juce::pointer_sized_int lParam);
    void pressed (int id);
    void timerCallback() override;

    void* window = nullptr;
    std::map<int, Entry> entries;
};

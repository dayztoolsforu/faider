#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/AudioEngine.h"
#include "Hotkeys.h"
#include <map>
#include <set>

class MixerComponent;

// The PADS tab: the button box (Pads.h). Banks A-D of 4x4 pads, Stop all, and an editor for the
// selected pad. Click a pad to play it (an empty one asks for a sound); right-click to edit it;
// Ctrl-drag one onto another to swap them; drop sound files on pads. Pads can have global
// hotkeys. Everything plays through the PADS input strip.
class PadsPanel : public juce::Component,
                  public juce::DragAndDropContainer,
                  private juce::Timer
{
public:
    PadsPanel (AudioEngine&, MixerComponent&);
    ~PadsPanel() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    void select (int index);   // the pad shown in the editor
    void chooseSound (int index);
    void swapPads (int a, int b);

    // Gives a pad a global hotkey (an invalid KeyPress: none). False if another app owns it.
    bool setHotkey (int index, const juce::KeyPress&);
    bool setMacroHotkey (int macro, const juce::KeyPress&);   // the same for a macro
    bool setSceneHotkey (int scene, const juce::KeyPress&);   // and a scene (Scenes.h)

    // Any other global hotkey (instant replay's SAVE, the overlay), kept with these: Windows has one
    // set per app. An empty key removes it. False if it's taken. Ids from extraHotkeyBase + 1.
    bool setExtraHotkey (int id, const juce::String& key, std::function<void()> onPress, std::function<void()> onRelease);
    static constexpr int extraHotkeyBase = 3000;

    // A pad pressed or let go (a click or its hotkey): its sound, and its macro if it has one.
    void pressPad (int index);
    void releasePad (int index);

    void macrosChanged();   // added, removed or renamed: the pad editor's list and the hotkeys follow
    void scenesChanged();   // the same for scenes: their hotkeys and the macro steps' menus

    // The MACROS page (SETTINGS > MACROS): the macro editor. It's kept here,
    // with the global hotkeys macros share with the pads.
    juce::Component& getMacrosPage();

private:
    class PadButton;
    class Editor;
    class MacroEditor;
    class MacrosPage;

    void timerCallback() override;
    void showBank (int bank);
    bool hasPadsStrip() const;
    void edited();   // a pad changed: redraw it and the editor
    void registerHotkeys();   // every pad's hotkey, as saved

    GlobalHotkeys hotkeys;
    std::set<int> hotkeyWorks;   // pads (and 1000 + macros, 2000 + scenes) whose hotkey Windows accepted
    static constexpr int macroHotkeyBase = 1000, sceneHotkeyBase = 2000;
    struct Extra { juce::String key; std::function<void()> onPress, onRelease; };
    std::map<int, Extra> extras;

    AudioEngine& engine;
    MixerComponent& mixer;
    int bank = 0, selected = 0;
    juce::OwnedArray<juce::TextButton> bankButtons;
    juce::TextButton stopAll { "ALL OFF" }, addStrip { "CREATE PADS CHANNEL" };
    std::unique_ptr<MacroEditor> macroEditor;
    std::unique_ptr<MacrosPage> macrosPage;   // holds macroEditor (goes first)

    // Where the pads play: the PADS strip, or into an input strip (through its plugins and sends).
    juce::Label playIntoLabel { {}, "OUTPUT" };
    juce::ComboBox playInto;
    juce::String playIntoShown;   // the strips it lists, to refill it when they change
    void fillPlayInto();
    juce::OwnedArray<PadButton> padButtons;   // the 16 on screen (the bank's)
    std::unique_ptr<Editor> editor;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> gridArea;
};

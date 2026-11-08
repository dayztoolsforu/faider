#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/AudioEngine.h"
#include "../engine/AppCapture.h"
#include "Console.h"
#include "Hotkeys.h"
#include "MidiMap.h"

// The scene keys in the tab bar (Scenes.h): one per scene, the one in use lit. Click one to glide
// the mix to it; right-click to save the mix into it, rename it, give it a hotkey or delete it.
// + saves the mix as a new scene.
class SceneBar : public juce::Component
{
public:
    // Gives a scene a global hotkey (an invalid KeyPress: none). False if it's taken.
    std::function<bool (int scene, const juce::KeyPress&)> setHotkey;
    std::function<void()> onListChanged;   // added, removed or renamed (hotkeys and macro menus follow)

    explicit SceneBar (AudioEngine& e) : engine (e)
    {
        legend.setFont (Console::legendFont (Console::Type::body));
        legend.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (legend);

        add.setTooltip ("Store new scene");
        add.setWantsKeyboardFocus (false);
        add.onClick = [this] { askName ("STORE NEW SCENE", "SCENE " + juce::String ((int) scenes().list.size() + 1), -1); };
        addAndMakeVisible (add);

        scenes().onActiveChanged = [this] { refreshLit(); };
        rebuild();
    }

    ~SceneBar() override { scenes().onActiveChanged = nullptr; }

    // After scenes were added, removed or renamed.
    void rebuild()
    {
        keys.clear();
        auto& list = scenes().list;
        for (int i = 0; i < (int) list.size(); ++i)
        {
            auto* key = keys.add (new Key());
            key->setButtonText (list[(size_t) i].name);
            key->setClickingTogglesState (true);
            key->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
            key->setWantsKeyboardFocus (false);
            key->setTooltip ("Recall" + (list[(size_t) i].hotkey.isEmpty() ? juce::String() : "  (" + list[(size_t) i].hotkey + ")")
                             + (list[(size_t) i].device.isEmpty() ? juce::String() : "\nOn device: " + list[(size_t) i].device));
            key->onPress = [this, i] (bool menu) { if (menu) showMenu (i); else scenes().recall (i); refreshLit(); };
            MidiMap::tag (*key, "scene:" + list[(size_t) i].name);
            addAndMakeVisible (key);
        }
        refreshLit();
        resized();
    }

    void refreshLit()
    {
        for (int i = 0; i < keys.size(); ++i)
        {
            keys[i]->setToggleState (i == scenes().active, juce::dontSendNotification);
            if (i == capturing)
                keys[i]->setButtonText ("LEARN...");
        }
    }

    // The width its legend, + and keys want (the tab bar puts FLOW after them).
    int idealWidth() const
    {
        using namespace Console;
        int w = Space::column + Space::unit + Space::key + Space::unit;
        for (auto* key : keys)
            w += Space::columns (KeyRow::columnsFor (key->getButtonText())) + Space::unit;
        return w;
    }

    void resized() override
    {
        // The SCENES legend only when there's room for it and a key or two.
        using namespace Console;
        KeyRow row (getLocalBounds());
        legend.setVisible (getWidth() >= 220);
        if (legend.isVisible())
            row.leftWidth (legend, Space::column);
        row.leftWidth (add, Space::key);   // a square key

        // Each key whole columns for its name; all the same width when they don't all fit.
        juce::Array<int> widths;
        int total = 0;
        for (auto* key : keys)
        {
            widths.add (Space::columns (KeyRow::columnsFor (key->getButtonText())));
            total += widths.getLast() + Space::unit;
        }
        const int room = row.rest().getWidth();
        const int even = keys.isEmpty() ? 0 : juce::jmax (0, room / keys.size() - Space::unit);
        for (int i = 0; i < keys.size(); ++i)
            row.leftWidth (*keys[i], total > room ? even : widths[i]);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (capturing < 0)
            return false;
        if (key == juce::KeyPress::escapeKey)
            stopCapturing();
        else if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            setHotkey (capturing, {});
            stopCapturing();
        }
        else if (GlobalHotkeys::usable (key))
        {
            if (! setHotkey (capturing, key))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "HOTKEY IN USE",
                                                        key.getTextDescription() + " is already in use.");
            stopCapturing();
        }
        return true;
    }

    void focusLost (FocusChangeType) override { if (capturing >= 0) stopCapturing(); }

private:
    // A scene key: a click recalls it, a right-click opens its menu.
    struct Key : juce::TextButton
    {
        std::function<void (bool menu)> onPress;
        void clicked() override { if (onPress) onPress (false); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu())
                return TextButton::mouseDown (e);
            if (onPress)
                onPress (true);
        }
    };

    Scenes& scenes() { return engine.getScenes(); }

    void showMenu (int i)
    {
        const auto& scene = scenes().list[(size_t) i];
        juce::PopupMenu menu;
        menu.addSectionHeader (scene.name);
        // Recall parts (the console's): only the mutes, or only the levels.
        menu.addItem ("Recall cuts only", [this, i] { if (juce::isPositiveAndBelow (i, (int) scenes().list.size())) scenes().recall (i, Scenes::Part::mutes); refreshLit(); });
        menu.addItem ("Recall levels only", [this, i] { if (juce::isPositiveAndBelow (i, (int) scenes().list.size())) scenes().recall (i, Scenes::Part::levels); refreshLit(); });
        menu.addSeparator();
        menu.addItem ("Store to " + scene.name, [this, i]
        {
            if (! juce::isPositiveAndBelow (i, (int) scenes().list.size()))
                return;
            auto& s = scenes().list[(size_t) i];
            const auto name = s.name, hotkey = s.hotkey, device = s.device;
            const auto triggers = s.triggers;
            const bool goBack = s.goBack;
            s = scenes().capture (name, s.withPlugins);
            s.hotkey = hotkey;
            s.device = device;
            s.triggers = triggers;
            s.goBack = goBack;
            scenes().setActive (i);
        });
        // Plugins' settings in the scene too: switched on, it keeps them as they are now.
        menu.addItem ("Include plugin settings", true, scene.withPlugins, [this, i]
        {
            if (! juce::isPositiveAndBelow (i, (int) scenes().list.size()))
                return;
            auto& s = scenes().list[(size_t) i];
            const bool on = ! s.withPlugins;
            const auto fresh = scenes().capture (s.name, on);
            for (auto& setting : s.settings)   // the rest of the scene as it was saved
                for (auto& now : fresh.settings)
                    if (now.target == setting.target)
                        setting.plugins = now.plugins;
            s.withPlugins = on;
        });
        menu.addItem ("Rename...", [this, i, name = scene.name] { askName ("RENAME SCENE", name, i); });
        menu.addItem (scene.hotkey.isEmpty() ? juce::String ("Assign hotkey...") : "Hotkey: " + scene.hotkey + "...", [this, i]
        {
            capturing = i;
            setWantsKeyboardFocus (true);
            grabKeyboardFocus();
            refreshLit();
            resized();
        });
        // Device profile: recall this scene when a device appears (a headset), and go back after.
        juce::PopupMenu devices;
        devices.addItem ("None", true, scene.device.isEmpty(), [this, i] { setDevice (i, {}); });
        juce::StringArray listed;
        for (auto& e : engine.getEndpoints())
            if (e.api == "WASAPI" && e.state != 4 && ! listed.contains (e.name))   // 4: not present
            {
                listed.add (e.name);
                devices.addItem (endpointDisplayName (e) + (e.isOutput ? "  (out)" : "  (in)") + (e.isActive() ? juce::String() : "  (offline)"),
                                 true, scene.device == e.name, [this, i, name = e.name] { setDevice (i, name); });
            }
        if (scene.device.isNotEmpty() && ! listed.contains (scene.device))
            devices.addItem (scene.device + "  (missing)", true, true, [] {});
        menu.addSubMenu ("Recall on device", devices, true, nullptr, scene.device.isNotEmpty());

        // AUTO: recalled by itself when an app starts, a device connects or at a time of day.
        juce::PopupMenu automatic, remove;
        for (int t = 0; t < (int) scene.triggers.size(); ++t)
        {
            const auto& trig = scene.triggers[(size_t) t];
            const auto text = trig.when == MacroTrigger::When::time ? "At " + trig.what : trig.what + " " + MacroTrigger::whenName (trig.when).toLowerCase();
            automatic.addItem (text, false, true, [] {});
            remove.addItem (text, [this, i, t]
            {
                if (juce::isPositiveAndBelow (i, (int) scenes().list.size()) && juce::isPositiveAndBelow (t, (int) scenes().list[(size_t) i].triggers.size()))
                    scenes().list[(size_t) i].triggers.erase (scenes().list[(size_t) i].triggers.begin() + t);
                rebuild();
            });
        }
        if (! scene.triggers.empty())
            automatic.addSeparator();
        juce::PopupMenu apps;
        juce::StringArray running;
        for (auto& a : listAudioApps())
            running.addIfNotAlreadyThere (a.exe, true);
        for (auto& exe : running)
            apps.addItem (exe, [this, i, exe] { addTrigger (i, MacroTrigger::When::appStarts, exe); });
        apps.addItem ("Other app...", [this, i] { askTrigger (i, MacroTrigger::When::appStarts, "WHEN THIS APP STARTS", "game.exe"); });
        automatic.addSubMenu ("When an app starts playing", apps);
        automatic.addItem ("When a device connects...", [this, i] { askTrigger (i, MacroTrigger::When::deviceOn, "WHEN A DEVICE CONNECTS (part of its name)", "Headset"); });
        automatic.addItem ("At a time of day...", [this, i] { askTrigger (i, MacroTrigger::When::time, "AT A TIME OF DAY (HH:MM, 24 hour)", "09:00"); });
        automatic.addSeparator();
        automatic.addItem ("Go back when the app stops", true, scene.goBack, [this, i]
        {
            if (juce::isPositiveAndBelow (i, (int) scenes().list.size()))
                scenes().list[(size_t) i].goBack = ! scenes().list[(size_t) i].goBack;
        });
        automatic.addSubMenu ("Remove", remove, ! scene.triggers.empty());
        menu.addSubMenu ("Auto-scene", automatic, true, nullptr, ! scene.triggers.empty());
        const auto bound = engine.getRemote().controlFor ("scene:" + scene.name);
        menu.addItem ("MIDI learn" + (bound.isEmpty() ? juce::String ("...") : "   (" + bound + ")"),
                      [this, name = scene.name] { engine.getRemote().learn ("scene:" + name); });
        menu.addSeparator();
        menu.addItem ("Move left", i > 0, false, [this, i] { swap (i, i - 1); });
        menu.addItem ("Move right", i < (int) scenes().list.size() - 1, false, [this, i] { swap (i, i + 1); });
        menu.addSeparator();
        menu.addItem ("Delete", [this, i]
        {
            scenes().remove (i);
            listChanged();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (keys[i]));
    }

    void addTrigger (int i, MacroTrigger::When when, const juce::String& what)
    {
        if (! juce::isPositiveAndBelow (i, (int) scenes().list.size()) || what.trim().isEmpty())
            return;
        scenes().list[(size_t) i].triggers.push_back ({ when, what.trim(), 10 });
        engine.logEvent ("auto-scene: " + scenes().list[(size_t) i].name + " when " + what.trim()
                         + (when == MacroTrigger::When::time ? juce::String() : " " + MacroTrigger::whenName (when).toLowerCase()));
        rebuild();
    }

    void askTrigger (int i, MacroTrigger::When when, const juce::String& title, const juce::String& example)
    {
        auto* window = new juce::AlertWindow (title, "For example: " + example, juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("what", {});
        window->addButton ("ADD", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, i, when] (int result)
        {
            auto what = window->getTextEditorContents ("what").trim();
            if (when == MacroTrigger::When::time)   // "9:00" -> "09:00"
                what = what.containsChar (':') ? juce::String (what.upToFirstOccurrenceOf (":", false, false).getIntValue()).paddedLeft ('0', 2)
                                                   + ":" + juce::String (what.fromFirstOccurrenceOf (":", false, false).getIntValue()).paddedLeft ('0', 2) : juce::String();
            if (result == 1)
                addTrigger (i, when, what);
        }), true);
    }

    void setDevice (int i, const juce::String& device)
    {
        if (juce::isPositiveAndBelow (i, (int) scenes().list.size()))
            scenes().list[(size_t) i].device = device;
        rebuild();   // the tooltip shows it
    }

    void swap (int a, int b)
    {
        auto& list = scenes().list;
        if (! juce::isPositiveAndBelow (a, (int) list.size()) || ! juce::isPositiveAndBelow (b, (int) list.size()))
            return;
        std::swap (list[(size_t) a], list[(size_t) b]);
        const int active = scenes().active;
        scenes().active = active == a ? b : active == b ? a : active;
        listChanged();
    }

    // index -1: a new scene from the mix as it is.
    void askName (const juce::String& title, const juce::String& name, int index)
    {
        auto* window = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", name);
        if (auto* editor = window->getTextEditor ("name"))
            editor->setInputFilter (new Console::CapitalsFilter(), true);   // names are in capitals
        window->addButton (index < 0 ? "STORE" : "RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, index] (int result)
        {
            const auto newName = window->getTextEditorContents ("name").trim().toUpperCase();   // names are in capitals
            if (result != 1 || newName.isEmpty())
                return;
            auto& list = scenes().list;
            if (index < 0)
            {
                list.push_back (scenes().capture (newName));
                scenes().active = (int) list.size() - 1;
            }
            else if (juce::isPositiveAndBelow (index, (int) list.size()))
            {
                engine.sceneRenamed (list[(size_t) index].name, newName);   // macro steps and MIDI follow
                list[(size_t) index].name = newName;
            }
            listChanged();
        }), true);
    }

    void listChanged()
    {
        rebuild();
        if (onListChanged)
            onListChanged();
    }

    void stopCapturing()
    {
        capturing = -1;
        setWantsKeyboardFocus (false);
        rebuild();   // names and tooltips back
    }

    AudioEngine& engine;
    juce::Label legend { {}, "SCENES" };
    juce::TextButton add { "+" };
    juce::OwnedArray<Key> keys;
    int capturing = -1;   // the scene waiting for its hotkey, or -1
};

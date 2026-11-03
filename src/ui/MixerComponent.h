#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include "../engine/AudioEngine.h"
#include "../engine/WindowDiscovery.h"

// What the console, the pads, the tape and the SteamVR dashboard use from Faider's old mixer:
// the input and plugin menus, plugin windows, the colour menu, the selected channel, A / B,
// putting a channel back from a saved session, and the channel pages the headset shows. It draws
// nothing of its own.
class MixerComponent : public juce::Component
{
public:
    explicit MixerComponent (AudioEngine&);
    ~MixerComponent() override;

    // The selected channel ("input:NAME" / "bus:NAME"). The console's selection is kept here, so
    // FIND and a restart can find it.
    void setSelected (const juce::String& key) { selectedKey = key; }
    const juce::String& getSelected() const { return selectedKey; }
    std::function<void (const juce::String& key)> onSelect;

    // A channel renamed: the selection follows it.
    void renamed (const juce::String& kind, const juce::String& oldName, const juce::String& newName);

    // That channel's page made on its own, open on its RACK (the VR dashboard shows one in the
    // headset). Null if there's no such channel.
    std::unique_ptr<juce::Component> createRackPage (const juce::String& key);

    // A channel ("input:NAME" or "bus:NAME") put back as a saved session had it: its level, pan,
    // mute and whole rack (from that session's <Strip> or <Pair>). False if it isn't there now,
    // or a plugin of its is locked.
    bool revertChannel (const juce::String& key, const juce::XmlElement& saved);

    // A / B on a channel's page: flip it to settings `slot` (0 A, 1 B); which it's on now.
    void compare (const juce::String& key, int slot);
    int compareSlot (const juce::String& key) const;

    // A plugin chain: an input strip's, a bus's (a pair), or a combinator path's. Outputs have none.
    struct InsertTarget
    {
        enum Kind { strip, bus };
        Kind kind = strip;
        int index = -1;
        int path = -1;   // pair: one of a combinator's paths (-1: the pair's own chain)
    };

    // Colour picker: a palette plus "Default". Adds the entries to `menu` (as a submenu named "Colour").
    static void addColourMenu (juce::PopupMenu& menu, juce::Colour current, std::function<void (juce::Colour)> onPick);

    // A moment after setting up an app channel, a routing change with app channels, or Windows'
    // defaults changing: says if anything would be heard twice or feed back
    // (AudioEngine::findDoubling, findFeedbackLoop).
    static void checkSetupSoon (AudioEngine&);

    void showAddInputMenu (juce::PopupMenu::Options where);
    void showAddInsertMenu (InsertTarget, juce::PopupMenu::Options where);
    std::optional<InsertTarget> findTarget (const void* chain) const;   // where that chain is now (none: gone)

    void removeInsert (InsertTarget, int slot);
    void swapInserts (InsertTarget, int a, int b);         // (a locked plugin isn't moved)
    bool pluginLocked (const juce::AudioPluginInstance*) const;   // LOCK in the rack: can't be moved or removed
    void openEditor (juce::AudioProcessor&);
    void closeEditor (juce::AudioProcessor*);   // just its window (the racks keep showing it)
    void closeEditorsFor (InsertTarget);        // a chain about to go: its windows, and every page lets go of its plugins

private:
    class SelectedPage;
    class PluginWindow;
    std::vector<juce::Component::SafePointer<SelectedPage>> otherPages;   // made by createRackPage
    void releaseFromPages (juce::AudioProcessor*);   // every page showing a plugin lets go of it (called before it's removed)

    void select (const juce::String& key) { selectedKey = key; if (onSelect) onSelect (key); }
    juce::String selectedKey;
    bool resolveSource (AudioEngine::Source&, juce::String& stripName);
    void insertPlugin (InsertTarget, const juce::String& path);
    std::vector<std::unique_ptr<juce::AudioPluginInstance>>* chainOf (InsertTarget) const;

    // A / B per channel (compare): each slot's built-in processing and plugin states.
    struct CompareSlot { juce::MemoryBlock channel; std::vector<juce::MemoryBlock> plugins; bool kept = false; };
    struct Compare { int active = 0; CompareSlot slots[2]; };
    std::map<juce::String, Compare> compares;

    AudioEngine& engine;
    juce::SharedResourcePointer<WindowDiscovery::Scanner> windows;   // program windows and browser tabs for the input menus

    std::map<juce::AudioProcessor*, std::unique_ptr<PluginWindow>> editors;
    std::unique_ptr<juce::FileChooser> chooser;
};

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include "../engine/AudioEngine.h"
#include "../engine/WindowDiscovery.h"

// What the console, the pads and the tape use from Faider's old mixer: the input and plugin menus,
// plugin windows, the colour menu, the selected channel and putting a channel back from a
// saved session. It draws nothing of its own.
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

    // A channel ("input:NAME" or "bus:NAME") put back as a saved session had it: its level, pan,
    // mute and whole rack (from that session's <Strip> or <Pair>). False if it isn't there now,
    // or a plugin of its is locked.
    bool revertChannel (const juce::String& key, const juce::XmlElement& saved);

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
    void closeEditorsFor (InsertTarget);        // a chain about to go: its plugins' windows

private:
    class PluginWindow;

    void select (const juce::String& key) { selectedKey = key; if (onSelect) onSelect (key); }
    juce::String selectedKey;
    bool resolveSource (AudioEngine::Source&, juce::String& stripName);
    void insertPlugin (InsertTarget, const juce::String& path);
    std::vector<std::unique_ptr<juce::AudioPluginInstance>>* chainOf (InsertTarget) const;

    AudioEngine& engine;
    juce::SharedResourcePointer<WindowDiscovery::Scanner> windows;   // program windows and browser tabs for the input menus

    std::map<juce::AudioProcessor*, std::unique_ptr<PluginWindow>> editors;
    std::unique_ptr<juce::FileChooser> chooser;
};

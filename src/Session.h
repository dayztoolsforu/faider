#pragma once

#include "engine/AudioEngine.h"

// The whole mix as XML: master device, masters, strips, sends, levels, and each strip's plugin
// chain with the plugins' own state. Saved to %APPDATA%\Faider\session.xml and restored at
// startup, so Faider comes back exactly as it was left.
namespace Session
{
    std::unique_ptr<juce::XmlElement> save (const AudioEngine&, const juce::String& interfaceName);

    // Rebuilds masters and strips in an engine whose master is already running. Plugins that
    // fail to load are logged and skipped. Returns false if the XML isn't a session.
    bool restore (AudioEngine&, const juce::XmlElement&, const std::function<void (const juce::String&)>& log);

    juce::String getInterfaceName (const juce::XmlElement&);

    // One strip or one plugin on its own, for undo. kind: "input", "bus" or "output". A strip
    // comes back at the end of its row, with its plugins, settings and what sent to it; a plugin
    // comes back into its slot with its state. Null / -1 / false if there's no such thing.
    std::unique_ptr<juce::XmlElement> saveStrip (const AudioEngine&, const juce::String& kind, int index);
    int restoreStrip (AudioEngine&, const juce::XmlElement&, const std::function<void (const juce::String&)>& log);
    std::unique_ptr<juce::XmlElement> saveInsert (const AudioEngine&, const juce::String& kind, int index, int slot);
    bool restoreInsert (AudioEngine&, const juce::String& kind, int index, int slot, const juce::XmlElement&,
                        const std::function<void (const juce::String&)>& log);

    // Rack presets: a channel's whole rack (kind "input" or "bus") as a <Rack>: its built-in units'
    // settings, its plugins with their state and options, their placement and cables. loadRack
    // replaces the channel's rack with one (its plugins go; the preset's come in). Null / false
    // if there's no such channel or it isn't a rack.
    std::unique_ptr<juce::XmlElement> saveRackPreset (const AudioEngine&, const juce::String& kind, int index);
    bool loadRackPreset (AudioEngine&, const juce::String& kind, int index, const juce::XmlElement&,
                         const std::function<void (const juce::String&)>& log);
}

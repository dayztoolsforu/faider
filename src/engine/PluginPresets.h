#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../ProductName.h"

// Presets for any plugin (an insert's settings by name), shared by every copy of that plugin:
// files in %APPDATA%\Faider\presets\<plugin name - maker - ID>\<NAME>.preset holding the
// plugin's own state (what it saves into a session). Message thread.
namespace PluginPresets
{
    inline juce::File oldFolderFor (const juce::AudioProcessor& p)
    {
        return Product::dataFolder().getChildFile ("presets").getChildFile (juce::File::createLegalFileName (p.getName()).trim());
    }

    // Named by maker and ID as well, so two plugins that happen to share a name don't share
    // presets (and get fed each other's state). Presets saved before this sit in a folder named
    // only by the plugin's name (oldFolderFor), and are still found there.
    inline juce::File folderFor (const juce::AudioProcessor& p)
    {
        const auto* plugin = dynamic_cast<const juce::AudioPluginInstance*> (&p);
        if (plugin == nullptr)
            return oldFolderFor (p);
        const auto d = plugin->getPluginDescription();
        return oldFolderFor (p).getSiblingFile (juce::File::createLegalFileName (d.name + " - " + d.manufacturerName + " - "
                                                                                 + juce::String::toHexString (d.uniqueId)).trim());
    }

    // The preset's file: in the plugin's own folder, else in its old name-only one.
    inline juce::File fileFor (const juce::AudioProcessor& p, const juce::String& name)
    {
        const auto file = folderFor (p).getChildFile (name + ".preset");
        return file.existsAsFile() ? file : oldFolderFor (p).getChildFile (name + ".preset");
    }

    // The plugin's own presets first, then old name-only ones it hasn't got a preset of that name for.
    inline juce::StringArray list (const juce::AudioProcessor& p)
    {
        juce::StringArray names;
        for (const auto& folder : { folderFor (p), oldFolderFor (p) })
        {
            juce::StringArray found;
            for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.preset"))
                if (! names.contains (f.getFileNameWithoutExtension()))
                    found.add (f.getFileNameWithoutExtension());
            found.sortNatural();
            names.addArray (found);
        }
        return names;
    }

    inline bool save (juce::AudioProcessor& p, const juce::String& name)
    {
        const auto folder = folderFor (p);
        if (name.trim().isEmpty() || ! folder.createDirectory())
            return false;
        juce::MemoryBlock state;
        p.getStateInformation (state);
        return folder.getChildFile (juce::File::createLegalFileName (name.trim().toUpperCase()) + ".preset").replaceWithData (state.getData(), state.getSize());
    }

    inline bool load (juce::AudioProcessor& p, const juce::String& name)
    {
        juce::MemoryBlock state;
        if (! fileFor (p, name).loadFileAsData (state) || state.getSize() == 0)
            return false;
        p.setStateInformation (state.getData(), (int) state.getSize());
        return true;
    }

    // The one load would use: the plugin's own file if it has one, else the old name-only one.
    // The old folder may be shared with another plugin of the same name, so it's only touched
    // when that's where the preset is; an old copy of the same name then shows up again in its place.
    inline bool remove (const juce::AudioProcessor& p, const juce::String& name)
    {
        const auto file = fileFor (p, name);
        return file.existsAsFile() && file.deleteFile();
    }
}

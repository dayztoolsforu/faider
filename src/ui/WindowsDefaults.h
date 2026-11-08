#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../devices/EndpointWatcher.h"
#include "../devices/WindowsDefault.h"

// SET MY DEFAULTS: installing VB-CABLE makes the cable Windows' default speaker and mic, and the
// defaults for calls. A menu of the real speakers and mics puts one back as the default for every
// use (WindowsDefault::set, as the WINDOWS DEFAULT OUTPUT / INPUT macro steps do). From PRE-FLIGHT,
// SETUP and FIND.
namespace WindowsDefaults
{
    // A virtual cable (VB-CABLE, Voicemeeter): not a real speaker or mic.
    inline bool isVirtual (const EndpointInfo& e, const juce::Array<EndpointInfo>& all)
    {
        return virtualMicPartner (e, all).isNotEmpty() || e.name.containsIgnoreCase ("Voicemeeter")
            || e.adapter.containsIgnoreCase ("Virtual Cable") || e.adapter.containsIgnoreCase ("VB-Audio");
    }

    // Which of Windows' defaults are on a cable ("default speaker: CABLE Input"), or empty.
    inline juce::String onCable (const juce::Array<EndpointInfo>& all)
    {
        juce::StringArray found;
        for (auto& e : all)
            if (e.api == "WASAPI" && e.isActive() && (e.isDefault || e.isDefaultComms) && isVirtual (e, all))
                found.add (juce::String ("default ") + (e.isOutput ? "speaker" : "mic") + (e.isDefault ? "" : " for calls") + ": " + e.name);
        return found.joinIntoString ("; ");
    }

    // The real speakers, then the real mics: the one picked becomes Windows' default and its
    // default for calls. `log` hears what happened.
    inline void showMenu (const juce::Array<EndpointInfo>& all, juce::PopupMenu::Options where, std::function<void (const juce::String&)> log)
    {
        juce::PopupMenu menu;
        for (const bool output : { true, false })
        {
            menu.addSectionHeader (output ? "SPEAKERS (WINDOWS PLAYS HERE)" : "MICROPHONES (APPS HEAR THIS)");
            int listed = 0;
            for (auto& e : all)
            {
                if (e.api != "WASAPI" || e.isOutput != output || ! e.isActive() || isVirtual (e, all))
                    continue;
                ++listed;
                menu.addItem (endpointDisplayName (e).toUpperCase(), true, e.isDefault && e.isDefaultComms, [name = e.name, output, log]
                {
                    juce::String error;
                    if (WindowsDefault::set (name, ! output, error))
                        log ("windows defaults: " + name + " is the default " + (output ? "speaker" : "mic") + ", for calls too");
                    else
                        log ("windows defaults: " + error);
                });
            }
            if (listed == 0)
                menu.addItem (output ? "NO SPEAKERS FOUND" : "NO MICROPHONES FOUND", false, false, nullptr);
        }
        menu.showMenuAsync (where);
    }
}

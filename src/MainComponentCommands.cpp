// MainComponent's FIND: every command the palette offers (openCommandPalette).
#include "MainComponent.h"
#include "ProductName.h"
#include "ui/MixMinusBuilder.h"
#include "ui/AboutBox.h"
#include "../console/src/Console.h"   // (the console: the Mixer and Routing pages)

// The command palette's commands, made fresh each time it opens: pages, every channel (mute,
// select, its rack), scenes, macros, pads, themes, and the app's own actions.
void MainComponent::openCommandPalette()
{
    if (palette != nullptr)
        return palette->grabFocus();

    std::vector<CommandPalette::Command> commands;
    auto add = [&commands] (juce::String name, juce::String detail, std::function<void()> run, juce::String keywords = {})
    {
        commands.push_back ({ std::move (name), std::move (detail), std::move (run), std::move (keywords) });
    };

    const auto pages = tabs.getTabNames();
    for (int i = 0; i < pages.size(); ++i)
        add ("GO TO " + pages[i].toUpperCase(), "Page", [this, i] { tabs.setCurrentTabIndex (i); });

    // Channels, by name (so a command still finds its channel if the list changed meanwhile).
    auto channel = [this, &add] (const juce::String& kind, AudioEngine::Strip& s, const juce::String& detail)
    {
        const auto key = kind + ":" + s.name, name = s.name;
        auto strip = [this, kind, name]() -> AudioEngine::Strip*
        {
            for (auto& x : kind == "bus" ? engine.getBuses() : engine.getStrips())
                if (x->name == name)
                    return x.get();
            return nullptr;
        };
        add ((s.mute.load() ? "UNCUT " : "CUT ") + name, detail, [this, strip] { if (auto* x = strip()) { x->mute = ! x->mute.load(); } });
        add ("SELECT " + name, detail, [this, key] { tabs.setCurrentTabIndex (0); ConsoleDesk::select (*consoleDesk, key, false); });
        add ("PLUGINS: " + name, detail + " plugins", [this, key] { tabs.setCurrentTabIndex (0); ConsoleDesk::select (*consoleDesk, key, true); });
    };
    for (auto& s : engine.getStrips()) channel ("input", *s, "Input");
    for (auto& p : engine.getBuses())  channel ("bus", *p, "Bus");
    for (int i = 0; i < (int) engine.getMasters().size(); ++i)
    {
        auto& b = *engine.getMasters()[(size_t) i];
        add ((b.mute.load() ? "UNCUT " : "CUT ") + b.name, "Output", [this, name = b.name]
        {
            for (auto& x : engine.getMasters()) if (x->name == name) x->mute = ! x->mute.load();
        });
        add ((b.monitor.load() ? "STOP MONITORING " : "MONITOR ") + b.name, "Output - hear it in headphones", [this, name = b.name]
        {
            for (auto& x : engine.getMasters()) if (x->name == name) x->monitor = ! x->monitor.load();
        });
    }

    auto& scenes = engine.getScenes();
    for (int i = 0; i < (int) scenes.list.size(); ++i)
        add ("SCENE: " + scenes.list[(size_t) i].name, "Scene", [this, i] { engine.getScenes().recall (i); });
    auto& macros = engine.getMacros();
    for (int i = 0; i < (int) macros.list.size(); ++i)
        add ("MACRO: " + macros.list[(size_t) i].name, "Macro", [this, i] { engine.getMacros().run (i); });
    for (int i = 0; i < Pads::count; ++i)
        if (const auto& p = engine.getPads().pad (i); p.name.isNotEmpty())
            add ("PAD: " + p.name.toUpperCase(), "Pad " + juce::String (i + 1), [this, i]
            {
                pads->pressPad (i);
                juce::Timer::callAfterDelay (150, [self = juce::Component::SafePointer<MainComponent> (this), i] { if (self != nullptr && self->pads != nullptr) self->pads->releasePad (i); });
            });

    if (micOutputName().isNotEmpty())
        add ("HEAR WHAT THEY HEAR", "Hear your mic as apps hear it", [this] { hearWhatTheyHear(); });

    add (overlay != nullptr && overlay->isShowing() ? "HIDE OVERLAY" : "SHOW OVERLAY", "Ctrl+Shift+O - mic state on top of games", [this] { toggleOverlay(); });
    add ("SESSION TIMELINE", "Go back to an earlier mix, or one channel", [this] { showTimeline (juce::PopupMenu::Options().withTargetComponent (&findButton)); });
    add ("UNDO", "Ctrl+Z", [this] { undoRedo (false); });
    add ("REDO", "Ctrl+Y", [this] { undoRedo (true); });
    add ("SAVE SESSION AS...", "Keep this mix under a name", [this] { saveSessionAs(); });
    add ("APP RULES", "Where each app goes when it starts playing", [this] { showAppRules (juce::PopupMenu::Options().withTargetComponent (findButton)); });
    add ("SESSIONS", "Open a saved mix, export or import one", [this] { showSessions (juce::PopupMenu::Options().withTargetComponent (findButton)); });
    add ("PRE-FLIGHT CHECK", "Audio, the mic cable and route, cut mics, things heard twice", [this] { showPreflight (false); });
    add ("SETUP: BUILD A MIX", "Where you listen, your microphones, how apps hear you, your programs", [this] { showSetup(); });
    add ("WINDOWS DEFAULTS", "Your real speakers and mic as Windows' defaults, for calls too (not the cable)",
         [this] { showWindowsDefaults (juce::PopupMenu::Options().withTargetComponent (findButton)); });
    add (AudioBoost::isBoosted (windowsAudio.priority) ? "BOOST WINDOWS AUDIO (BOOSTED)" : "BOOST WINDOWS AUDIO",
         "Windows' audio engine at High priority, for crackle on Windows devices (not ASIO). Windows asks first", [this] { boostWindowsAudio(); });
    add ("ZERO DESK", "Things left set: solos, key listen, gains far off, outputs cut", [this] { showZero(); });
    add ("MIX-MINUS AND STEMS", "A feed per guest without themselves; voice, music and game stems", [this] { MixMinusBuilder::showBuilder (engine, [] {}, this); });
    add (mini != nullptr ? "HIDE MINI MIXER" : "MINI MIXER", "The outputs' faders in a small window, PIN keeps it on top", [this] { toggleMini(); });
    add ("HISTORY", "Ctrl+H - every undo step, go back to any",[this] { showHistory (juce::PopupMenu::Options().withTargetComponent (historyButton)); });
    add ("MIDI MAP", "Ctrl+L - link controls to MIDI", [this] { toggleMidiMap(); });
    // SETTINGS: each page, and each setting by its name (the same list the tab's rows come from).
    for (auto& p : settingsTab->getPages())
        add ("SETTINGS: " + p.name.toUpperCase(), "Settings page", [this, name = p.name] { openSettings (name); }, p.intro + " " + p.keywords);
    for (auto& s : settingsTab->getSettings())
        add (s.name.toUpperCase(), "Settings > " + s.page, [this, page = s.page, name = s.name] { openSetting (page, name); },
             s.description + " " + s.keywords + " " + s.section);
    add ("ADD CHANNEL", "Mic, device or app", [this]
    {
        tabs.setCurrentTabIndex (0);
        mixer->showAddInputMenu (juce::PopupMenu::Options().withTargetComponent (&findButton));
    });
    add (warnMutedTalk ? "STOP WARNING WHEN I TALK WHILE CUT" : "WARN WHEN I TALK WHILE CUT", "Setting", [this]
    {
        warnMutedTalk = ! warnMutedTalk;
        settings.setValue ("warnMutedTalk", warnMutedTalk);
        settings.saveIfNeeded();
    });
    add (ConsoleDesk::isCompact (*consoleDesk) ? "FULL DESK" : "COMPACT DESK",
         ConsoleDesk::isCompact (*consoleDesk) ? "Mixer - the whole strips again" : "Mixer - each strip only its name bar and fader, with its meter beside it",
         [this] { tabs.setCurrentTabIndex (0); ConsoleDesk::setCompact (*consoleDesk, ! ConsoleDesk::isCompact (*consoleDesk)); }, "compact full strips fold");
    add ("ZOOM IN", "Ctrl +", [this] { stepZoom (1); });
    add ("ZOOM OUT", "Ctrl -", [this] { stepZoom (-1); });
    add ("ZOOM 100%", "Ctrl 0", [this] { setZoom (1.0f); });
    add (juce::String ("ABOUT ") + Product::nameUpper, "Version, credits", [] { AboutBox::show(); });
    add ("SAVE A PROBLEM REPORT", "The logs and the last crash in a .zip you can send (nothing is sent by itself)", [this] { saveProblemReport(); });
    add ("CHECK FOR UPDATES NOW", "Ask GitHub for the newest version (one request: the app's name and version)", [this] { checkForUpdates (true); });
    if (newVersion.isNotEmpty())
        add ("GET " + juce::String (Product::nameUpper) + " " + newVersion, "Open its release page", [this] { openRelease(); });

    palette = std::make_unique<CommandPalette> (std::move (commands), [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe != nullptr) safe->palette = nullptr; });
    });
    addAndMakeVisible (*palette);
    const int w = juce::jmin (640, getWidth() - 40);
    palette->setBounds ((getWidth() - w) / 2, tabs.getTabBarDepth() + 24, w, juce::jmin (460, getHeight() - 120));
    palette->toFront (true);
    palette->grabFocus();
}

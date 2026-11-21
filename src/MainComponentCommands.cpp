// MainComponent's FIND: every command the palette offers (openCommandPalette).
#include "MainComponent.h"
#include "ProductName.h"
#include "ui/MixMinusBuilder.h"
#include "LanguageCase.h"
#include <set>
#include "../console/src/Console.h"   // (the console: the Mixer and Routing pages)

// The command palette's commands, made fresh each time it opens: pages, every channel (mute,
// select, its rack), scenes, macros, pads, themes, and the app's own actions.
void MainComponent::openCommandPalette()
{
    if (palette != nullptr)
        return palette->grabFocus();

    std::vector<CommandPalette::Command> commands;
    // A name or a detail as shown (translated) and in English: FIND matches both, and RECENT keeps
    // the English name (the same in every language). A user's name is the same in both.
    struct Said
    {
        Said() = default;
        Said (const juce::String& both) : shown (both), english (both) {}
        Said (const juce::String& s, const juce::String& e) : shown (s), english (e) {}
        juce::String shown, english;
    };
    // `english` (a NEEDS_TRANS key) translated, %1 and %2 as given (%2 first: %1, a user's name, goes in last).
    auto say = [] (const char* english, const Said& v1 = {}, const Said& v2 = {})
    {
        return Said { juce::translate (english).replace ("%2", v2.shown).replace ("%1", v1.shown),
                      juce::String (english).replace ("%2", v2.english).replace ("%1", v1.english) };
    };
    auto add = [&commands] (const Said& name, const Said& detail, std::function<void()> run, juce::String keywords = {})
    {
        commands.push_back ({ name.shown, detail.shown, std::move (run), keywords + " " + detail.english, name.english });
    };

    // (the tabs' names are English identifiers: translated to show)
    const auto pages = tabs.getTabNames();
    for (int i = 0; i < pages.size(); ++i)
        add (say (NEEDS_TRANS ("GO TO %1"), Said (Language::upper (juce::translate (pages[i])), pages[i].toUpperCase())), say (NEEDS_TRANS ("Page")),
             [this, i] { tabs.setCurrentTabIndex (i); });

    // Channels, by name (so a command still finds its channel if the list changed meanwhile).
    auto channel = [this, &add, &say] (const juce::String& kind, AudioEngine::Strip& s, const Said& detail, const Said& pluginsDetail)
    {
        const auto key = kind + ":" + s.name, name = s.name;
        auto strip = [this, kind, name]() -> AudioEngine::Strip*
        {
            for (auto& x : kind == "bus" ? engine.getBuses() : engine.getStrips())
                if (x->name == name)
                    return x.get();
            return nullptr;
        };
        add (s.mute.load() ? say (NEEDS_TRANS ("UNCUT %1"), name) : say (NEEDS_TRANS ("CUT %1"), name), detail, [this, strip] { if (auto* x = strip()) { x->mute = ! x->mute.load(); } });
        add (say (NEEDS_TRANS ("SELECT %1"), name), detail, [this, key] { tabs.setCurrentTabIndex (0); ConsoleDesk::select (*consoleDesk, key, false); });
        add (say (NEEDS_TRANS ("PLUGINS: %1"), name), pluginsDetail, [this, key] { tabs.setCurrentTabIndex (0); ConsoleDesk::select (*consoleDesk, key, true); });
    };
    for (auto& s : engine.getStrips()) channel ("input", *s, say (NEEDS_TRANS ("Input")), say (NEEDS_TRANS ("Input plugins")));
    for (auto& p : engine.getBuses())  channel ("bus", *p, say (NEEDS_TRANS ("Bus")), say (NEEDS_TRANS ("Bus plugins")));
    for (int i = 0; i < (int) engine.getMasters().size(); ++i)
    {
        auto& b = *engine.getMasters()[(size_t) i];
        add (b.mute.load() ? say (NEEDS_TRANS ("UNCUT %1"), b.name) : say (NEEDS_TRANS ("CUT %1"), b.name), say (NEEDS_TRANS ("Output")), [this, name = b.name]
        {
            for (auto& x : engine.getMasters()) if (x->name == name) x->mute = ! x->mute.load();
        });
        add (b.monitor.load() ? say (NEEDS_TRANS ("STOP MONITORING %1"), b.name) : say (NEEDS_TRANS ("MONITOR %1"), b.name), say (NEEDS_TRANS ("Output - hear it in headphones")), [this, name = b.name]
        {
            for (auto& x : engine.getMasters()) if (x->name == name) x->monitor = ! x->monitor.load();
        });
    }

    auto& scenes = engine.getScenes();
    for (int i = 0; i < (int) scenes.list.size(); ++i)
        add (say (NEEDS_TRANS ("SCENE: %1"), scenes.list[(size_t) i].name), say (NEEDS_TRANS ("Scene")), [this, i] { engine.getScenes().recall (i); });
    auto& macros = engine.getMacros();
    for (int i = 0; i < (int) macros.list.size(); ++i)
        add (say (NEEDS_TRANS ("MACRO: %1"), macros.list[(size_t) i].name), say (NEEDS_TRANS ("Macro")), [this, i] { engine.getMacros().run (i); });
    for (int i = 0; i < Pads::count; ++i)
        if (const auto& p = engine.getPads().pad (i); p.name.isNotEmpty())
            add (say (NEEDS_TRANS ("PAD: %1"), p.name.toUpperCase()), say (NEEDS_TRANS ("Pad %1"), juce::String (i + 1)), [this, i]
            {
                pads->pressPad (i);
                juce::Timer::callAfterDelay (150, [self = juce::Component::SafePointer<MainComponent> (this), i] { if (self != nullptr && self->pads != nullptr) self->pads->releasePad (i); });
            });

    if (micOutputName().isNotEmpty())
        add (say (NEEDS_TRANS ("HEAR WHAT THEY HEAR")), say (NEEDS_TRANS ("Hear your mic as apps hear it")), [this] { hearWhatTheyHear(); });

    add (overlay != nullptr && overlay->isShowing() ? say (NEEDS_TRANS ("HIDE OVERLAY")) : say (NEEDS_TRANS ("SHOW OVERLAY")),
         say (NEEDS_TRANS ("Ctrl+Shift+O - mic state on top of games")), [this] { toggleOverlay(); });
    add (say (NEEDS_TRANS ("UNDO")), Said ("Ctrl+Z"), [this] { undoRedo (false); });
    add (say (NEEDS_TRANS ("REDO")), Said ("Ctrl+Y"), [this] { undoRedo (true); });
    add (say (NEEDS_TRANS ("SAVE SESSION AS...")), say (NEEDS_TRANS ("Keep this mix under a name")), [this] { saveSessionAs(); });
    add (say (NEEDS_TRANS ("SESSIONS")), say (NEEDS_TRANS ("Open a saved mix, export or import one")), [this] { showSessions (juce::PopupMenu::Options().withTargetComponent (findButton)); });
    add (say (NEEDS_TRANS ("PRE-FLIGHT CHECK")), say (NEEDS_TRANS ("Audio, the mic cable and route, cut mics, things heard twice")), [this] { showPreflight (false); });
    add (say (NEEDS_TRANS ("WINDOWS DEFAULTS")), say (NEEDS_TRANS ("Your real speakers and mic as Windows' defaults, for calls too (not the cable)")),
         [this] { showWindowsDefaults (juce::PopupMenu::Options().withTargetComponent (findButton)); });
    add (AudioBoost::isBoosted (windowsAudio.priority) ? say (NEEDS_TRANS ("BOOST WINDOWS AUDIO (BOOSTED)")) : say (NEEDS_TRANS ("BOOST WINDOWS AUDIO")),
         say (NEEDS_TRANS ("Windows' audio engine at High priority, for crackle on Windows devices (not ASIO). Windows asks first")), [this] { boostWindowsAudio(); });
    add (say (NEEDS_TRANS ("ZERO DESK")), say (NEEDS_TRANS ("Things left set: solos, key listen, gains far off, outputs cut")), [this] { showZero(); });
    add (say (NEEDS_TRANS ("MIX-MINUS AND STEMS")), say (NEEDS_TRANS ("A feed per guest without themselves; voice, music and game stems")), [this] { MixMinusBuilder::showBuilder (engine, [] {}, this); });
    add (mini != nullptr ? say (NEEDS_TRANS ("HIDE MINI MIXER")) : say (NEEDS_TRANS ("MINI MIXER")), say (NEEDS_TRANS ("The outputs' faders in a small window, PIN keeps it on top")), [this] { toggleMini(); });
    add (say (NEEDS_TRANS ("HISTORY")), say (NEEDS_TRANS ("Ctrl+H - every undo step, go back to any")),[this] { showHistory (juce::PopupMenu::Options().withTargetComponent (historyButton)); });
    add (say (NEEDS_TRANS ("MIDI MAP")), say (NEEDS_TRANS ("Ctrl+L - link controls to MIDI")), [this] { toggleMidiMap(); });
    // SETTINGS: each page, and each setting by its name (the same list the tab's rows come from).
    // (their names are English identifiers, SettingsRegistry::shown translates them to show)
    // A setting named as a command above isn't listed twice: the command stays (MIDI MAP starts
    // mapping on the page you're on). APP RULES, SESSION TIMELINE, SETUP and ABOUT are only their rows.
    std::set<juce::String> named;
    for (auto& c : commands)
        named.insert (c.english.isNotEmpty() ? c.english : c.name);
    for (auto& p : settingsTab->getPages())
        add (say (NEEDS_TRANS ("SETTINGS: %1"), Said (Language::upper (SettingsRegistry::shown (p.name)), SettingsRegistry::english (p.name).toUpperCase())),
             say (NEEDS_TRANS ("Settings page")), [this, name = p.name] { openSettings (name); }, p.intro + " " + p.keywords);
    for (auto& s : settingsTab->getSettings())
        if (! named.count (SettingsRegistry::english (s.name).toUpperCase()))
            add (Said (Language::upper (SettingsRegistry::shown (s.name)), SettingsRegistry::english (s.name).toUpperCase()),
                 say (NEEDS_TRANS ("Settings > %1"), Said (SettingsRegistry::shown (s.page), SettingsRegistry::english (s.page))),
                 [this, page = s.page, name = s.name] { openSetting (page, name); },
                 s.description + " " + s.keywords + " " + SettingsRegistry::shown (s.section) + " " + SettingsRegistry::english (s.section));
    add (say (NEEDS_TRANS ("ADD CHANNEL")), say (NEEDS_TRANS ("Mic, device or app")), [this]
    {
        tabs.setCurrentTabIndex (0);
        mixer->showAddInputMenu (juce::PopupMenu::Options().withTargetComponent (&findButton));
    });
    add (warnMutedTalk ? say (NEEDS_TRANS ("STOP WARNING WHEN I TALK WHILE CUT")) : say (NEEDS_TRANS ("WARN WHEN I TALK WHILE CUT")), say (NEEDS_TRANS ("Setting")), [this]
    {
        warnMutedTalk = ! warnMutedTalk;
        settings.setValue ("warnMutedTalk", warnMutedTalk);
        settings.saveIfNeeded();
    });
    add (ConsoleDesk::isCompact (*consoleDesk) ? say (NEEDS_TRANS ("FULL DESK")) : say (NEEDS_TRANS ("COMPACT DESK")),
         ConsoleDesk::isCompact (*consoleDesk) ? say (NEEDS_TRANS ("Mixer - the whole strips again")) : say (NEEDS_TRANS ("Mixer - each strip only its name bar and fader")),
         [this] { tabs.setCurrentTabIndex (0); ConsoleDesk::setCompact (*consoleDesk, ! ConsoleDesk::isCompact (*consoleDesk)); }, "compact full strips fold");
    add (say (NEEDS_TRANS ("ZOOM IN")), Said ("Ctrl +"), [this] { stepZoom (1); });
    add (say (NEEDS_TRANS ("ZOOM OUT")), Said ("Ctrl -"), [this] { stepZoom (-1); });
    add (say (NEEDS_TRANS ("ZOOM 100%")), Said ("Ctrl 0"), [this] { setZoom (1.0f); });
    add (say (NEEDS_TRANS ("SAVE A PROBLEM REPORT")), say (NEEDS_TRANS ("The logs and the last crash in a .zip you can send (nothing is sent by itself)")), [this] { saveProblemReport(); });
    add (say (NEEDS_TRANS ("CHECK FOR UPDATES NOW")), say (NEEDS_TRANS ("Ask GitHub for the newest version (one request: the app's name and version)")), [this] { checkForUpdates (true); });
    if (newVersion.isNotEmpty())
        add (say (NEEDS_TRANS ("GET %1 %2"), juce::String (Product::nameUpper), newVersion), say (NEEDS_TRANS ("Open its release page")), [this] { openRelease(); });

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

// MainComponent's SETTINGS window (makeSettings) and what its rows do: looks, app rules, the clock, ffmpeg.
#include "MainComponent.h"
#include "ProductName.h"
#include "ui/AboutBox.h"
#include "ui/Theme.h"
#include "engine/PluginScan.h"
#include "StartWithWindows.h"

// The SETTINGS tab, after DEVICES: the app's preferences in one place, a section each.
void MainComponent::makeSettings()
{
    auto remember = [this] (const char* key, const juce::var& value) { settings.setValue (key, value); settings.saveIfNeeded(); };

    generalPage = std::make_unique<OptionsPage>();
    auto& general = *generalPage;
    general.heading ("LOOK");
    general.action ("LOOK...", "The console's colours and typeface", [this]
    {
        showLooks (juce::PopupMenu::Options().withMousePosition());
    });
    general.heading ("WINDOWS");
    general.toggle ("START WITH WINDOWS", juce::String ("Open ") + Product::name + " (in the tray) when you sign in",
                    [] { return StartWithWindows::isOn(); }, [] (bool on) { StartWithWindows::set (on); });
    general.heading ("DESK");
    general.toggle ("WARN IF I TALK WHILE CUT", "A notice when you speak into a cut mic",
                    [this] { return warnMutedTalk; }, [this, remember] (bool on) { warnMutedTalk = on; remember ("warnMutedTalk", on); });
    general.heading ("APPS");
    general.action ("APP RULES...", "Which bus or output an app goes to when it starts playing", [this]
    {
        showAppRules (juce::PopupMenu::Options().withMousePosition());
    });
    general.heading ("SESSIONS");
    general.action ("SAVE SESSION AS...", "Keep this mix under a name", [this] { saveSessionAs(); });
    general.action ("OPEN, EXPORT OR IMPORT...", juce::String ("A saved mix (") + Product::name + " restarts with it), or a session as a file", [this]
    {
        showSessions (juce::PopupMenu::Options().withMousePosition());
    });
    general.action ("SESSION TIMELINE...", "Go back to an earlier mix, or put one channel back as it was", [this]
    {
        showTimeline (juce::PopupMenu::Options().withMousePosition());
    });
    general.heading ("ON START");
    general.toggle ("PRE-FLIGHT: CHECK THE SETUP", juce::String ("A few seconds after ") + Product::name + " starts: the audio, the mic cable and route, cut mics (shown only if something's wrong)",
                    [this] { return settings.getBoolValue ("preflightOnStart", true); },
                    [remember] (bool on) { remember ("preflightOnStart", on); });
    general.action ("PRE-FLIGHT NOW...", "Check the setup now", [this] { showPreflight (false); });
    general.action ("ZERO DESK...", "Things left set: solos, key listen, gains far off, outputs cut", [this] { showZero(); });
    general.toggle ("TIPS", "A tip about each page, the first time it's shown",
                    [this] { return settings.getBoolValue ("tips", true); }, [remember] (bool on) { remember ("tips", on); });
    general.action ("SHOW THE TIPS AGAIN", "Each page's tip once more, as it's next shown", [this, remember]
    {
        for (auto* page : { "Mixer", "Routing", "Tools", "Devices", "Settings" })
            settings.removeValue (juce::String ("tipSeen_") + page);
        remember ("tips", true);
        showTip ("Settings");
    });
    general.heading ("NOTICES (IN THE CORNER)");
    for (auto [kind, text] : { std::pair ("device", "DEVICES CONNECTED OR GONE"), std::pair ("clip", "A CHANNEL CLIPPING"),
                               std::pair ("scene", "SCENES RECALLED"), std::pair ("macro", "MACROS RUN"), std::pair ("undo", "UNDO AND REDO") })
        general.toggle (text, "A notice in the window's corner (the event log has everything)",
                        [this, key = juce::String ("notice_") + kind] { return settings.getBoolValue (key, true); },
                        [remember, key = juce::String ("notice_") + kind] (bool on) { remember (key.toRawUTF8(), on); });
    general.heading ("ZOOM");
    {
        // The three stages (Ctrl +/- and the tab bar's - + step between them), or CUSTOM: any size
        // from 75 to 200%, kept until a stage is picked again.
        general.choice ({ "100%", "125%", "150%", "CUSTOM..." }, [this]
                        {
                            for (int i = 0; i < (int) std::size (zoomStages); ++i)
                                if (std::abs (zoomStages[i] - zoom) < 0.01f)
                                    return i;
                            return 3;
                        },
                        [this] (int i)
                        {
                            if (i < (int) std::size (zoomStages))
                                return setZoom (zoomStages[i]);
                            juce::PopupMenu sizes;
                            for (int p = 75; p <= 200; p += 5)
                                sizes.addItem (juce::String (p) + "%", true, std::abs ((float) p / 100.0f - zoom) < 0.01f, [this, p] { setZoom ((float) p / 100.0f); });
                            sizes.showMenuAsync (juce::PopupMenu::Options());
                        }, true);
    }
    general.heading ("METER STANDARD");
    {
        juce::StringArray names;
        for (auto& s : Metering::standards())
            names.add (juce::String (s.menuName).toUpperCase());
        general.choice (names, [] { return Metering::currentStandard.load(); }, [] (int i) { Metering::currentStandard = i; });
    }
    general.heading ("CHANNEL METER POINT");
    general.choice ({ "IN  (RAW SOURCE)", "PRE  (AFTER EFFECTS)", "POST  (AFTER THE FADER)" },
                    [] { return AudioEngine::meterPoint.load(); }, [] (int i) { AudioEngine::meterPoint = i; });
    general.heading ("RECORDING");
    general.actionShowing ([] { const auto f = Recorder::ffmpeg(); return "FFMPEG: " + (f.existsAsFile() ? f.getFullPathName() : juce::String ("NOT FOUND")); },
                           "MP3, AAC, Opus and ALAC recording need ffmpeg.exe: pick it here, or have it on the PATH",
                           [this] { pickFfmpeg(); });
    general.heading ("PLUGIN TEST TIME");
    {
        // How long a new plugin has to load in its test copy before it fails (PluginScan.h).
        static constexpr int seconds[] = { 30, 60, 120 };
        general.choice ({ "30 S", "60 S", "120 S" }, []
                        {
                            for (int i = 0; i < (int) std::size (seconds); ++i)
                                if (seconds[i] == PluginScan::testTime())
                                    return i;
                            return 0;
                        },
                        [remember] (int i)
                        {
                            PluginScan::setTestTime (seconds[i]);
                            remember ("pluginTestSeconds", seconds[i]);
                        }, true);
    }
    general.heading ("UPDATES");
    general.toggle ("CHECK FOR UPDATES", "Once a day, half a minute after start: one request to GitHub with the app's name and version, nothing else",
                    [this] { return settings.getBoolValue ("checkForUpdates", true); },
                    [remember] (bool on) { remember ("checkForUpdates", on); });
    general.actionShowing ([this] { return newVersion.isNotEmpty() ? "GET " + juce::String (Product::nameUpper) + " " + newVersion + "..." : juce::String ("CHECK NOW"); },
                           "Ask GitHub for the newest version now, or open the newer one's page",
                           [this] { if (newVersion.isNotEmpty()) openRelease(); else checkForUpdates (true); });
    general.heading ("ABOUT");
    general.action (juce::String ("ABOUT ") + Product::nameUpper + "...", "Version and credits", [] { AboutBox::show(); });
    general.finish();

    // Every global hotkey: the pads', macros' and scenes' (set through PADS, so Windows has one
    // set for them all), instant replay's, and the fixed ones. Each group says what its keys do and
    // where else they're set; `working`: whether Windows took the key (PadsPanel::hotkeyRegistered).
    hotkeysPage = std::make_unique<HotkeysPage> ([this]
    {
        std::vector<HotkeysPage::Entry> list;
        auto& p = engine.getPads();
        for (int i = 0; i < Pads::count; ++i)
            if (const auto& pad = p.pad (i); pad.file.isNotEmpty() || pad.hotkey.isNotEmpty())
                list.push_back ({ "PADS", "PAD " + juce::String (i + 1) + (pad.name.isNotEmpty() ? ":  " + pad.name.toUpperCase() : juce::String()),
                                  pad.hotkey, [this, i] (const juce::KeyPress& k) { return pads->setHotkey (i, k); },
                                  "PLAYS THE PAD.  ALSO SET IN TOOLS > PADS", pads->hotkeyRegistered (i) });
        auto& macros = engine.getMacros().list;
        for (int i = 0; i < (int) macros.size(); ++i)
            list.push_back ({ "MACROS", macros[(size_t) i].name.toUpperCase(), macros[(size_t) i].hotkey,
                              [this, i] (const juce::KeyPress& k) { return pads->setMacroHotkey (i, k); },
                              "RUNS THE MACRO.  ALSO SET IN SETTINGS > MACROS", pads->hotkeyRegistered (PadsPanel::macroHotkeyBase + i) });
        auto& scenes = engine.getScenes().list;
        for (int i = 0; i < (int) scenes.size(); ++i)
            list.push_back ({ "SCENES", scenes[(size_t) i].name.toUpperCase(), scenes[(size_t) i].hotkey,
                              [this, i] (const juce::KeyPress& k)
                              {
                                  const bool ok = pads->setSceneHotkey (i, k);
                                  if (ok && sceneBar != nullptr) sceneBar->repaint();
                                  return ok;
                              },
                              "RECALLS THE SCENE.  ALSO: RIGHT-CLICK ITS KEY", pads->hotkeyRegistered (PadsPanel::sceneHotkeyBase + i) });
        list.push_back ({ "TAPE", "SAVE INSTANT REPLAY", tape->replayHotkey, [this] (const juce::KeyPress& k)
        {
            const auto key = k.isValid() ? k.getTextDescription() : juce::String();
            if (! tape->setReplayHotkey (key))
                return false;
            tape->replayHotkey = key;
            return true;
        }, "ALSO: RIGHT-CLICK SAVE IN TOOLS > TAPE", pads->hotkeyRegistered (PadsPanel::extraHotkeyBase + 1) });
        const auto inApp = juce::String ("IN ") + Product::nameUpper + " (FIXED)";
        list.push_back ({ "ANYWHERE (FIXED)", "SHOW / HIDE THE IN-GAME OVERLAY", "ctrl + shift + O", nullptr, {},
                          pads->hotkeyRegistered (PadsPanel::extraHotkeyBase + 2), { "ctrl + shift + O" } });
        for (auto& fixed : PadsPanel::fixedKeys())   // (the window's own keys: a hotkey may not take one)
            list.push_back ({ inApp, fixed.name, fixed.shown, nullptr, {}, true, fixed.keys });
        list.push_back ({ "ON THE MIXER (FIXED)", "SELECT THE CHANNEL LEFT / RIGHT", "left / right", nullptr });
        list.push_back ({ "ON THE MIXER (FIXED)", "CUT / SOLO THE SELECTED CHANNEL", "M  /  S", nullptr });
        list.push_back ({ "ON THE MIXER (FIXED)", "ITS FADER UP / DOWN 0.5 DB (SHIFT: 3 DB)", "up / down", nullptr });
        return list;
    });

    // CONTROL: controllers (MIDI, OSC). Learning is on the strips' and scenes' menus.
    ControlPanel::Hooks hooks;
    hooks.toggleMidiMap = [this] { toggleMidiMap(); };
    hooks.openMidiPanel = [this] { openMidiPanel(); };
    hooks.midiMapOn = [this] { return midiMap != nullptr; };
    controlPanel = std::make_unique<ControlPanel> (engine.getRemote(), std::move (hooks));

    settingsTab = std::make_unique<SettingsTab>();
    settingsTab->addSection ("General", *generalPage, false);
    settingsTab->addSection ("Macros", pads->getMacrosPage(), true);
    settingsTab->addSection ("Hotkeys", *hotkeysPage, false);
    settingsTab->addSection ("Control", *controlPanel, false);
    tabs.addTab ("Settings", juce::Colours::transparentBlack, settingsTab.get(), false);
}

void MainComponent::openSettings (const juce::String& section)
{
    tabs.setCurrentTabIndex (tabs.getTabNames().indexOf ("Settings"));
    settingsTab->show (section);
}

// A new look: every view drawn again. The desk remembers the look's generation, so it draws
// all of itself; the rest is ordinary painting.
static void repaintEveryView (juce::Component& component)
{
    component.repaint();
    for (auto* child : component.getChildren())
        repaintEveryView (*child);
}

void MainComponent::repaintEverything()
{
    // The main window is this component. The overlay and the mini mixer paint from the look
    // every time they redraw, so they follow on their own.
    repaintEveryView (*this);
}

// LOOK: the console's colours and typeface, from %APPDATA%\Faider\looks. A look file that is
// missing or broken leaves the built-in one in place.
void MainComponent::showLooks (juce::PopupMenu::Options where)
{
    juce::PopupMenu menu;
    const auto current = Theme::currentLookName();
    for (const auto& name : Theme::lookNames())
        menu.addItem (name, true, name == current, [this, name]
        {
            if (! Theme::useLook (name))
                return;
            settings.setValue ("look", name);
            settings.saveIfNeeded();
            repaintEverything();
        });
    menu.addSeparator();
    menu.addItem ("SAVE THIS LOOK AS...", [this] { saveLookAs(); });
    menu.showMenuAsync (where);
}

void MainComponent::saveLookAs()
{
    auto* w = new juce::AlertWindow ("SAVE THIS LOOK AS", {}, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", "");
    w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w] (int result)
    {
        const auto wanted = w->getTextEditorContents ("name").trim();
        if (result == 1 && wanted.isNotEmpty() && ! Theme::saveLookAs (wanted))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "LOOK", "The look could not be written.");
    }), true);
}

// APP RULES: "DISCORD goes to CALLS" (AudioEngine::AppRule). Each rule's app and where it goes;
// a new rule for any app playing now, or one that already has a channel.
void MainComponent::showAppRules (juce::PopupMenu::Options where)
{
    auto appName = [] (const juce::String& exe) { return juce::File (exe).getFileNameWithoutExtension().toUpperCase(); };

    // A menu of buses and outputs; `pick` gets "bus:NAME" or "output:NAME".
    auto destinations = [this] (const juce::String& current, std::function<void (const juce::String&)> pick)
    {
        juce::PopupMenu m;
        m.addSectionHeader ("BUSES");
        for (auto& p : engine.getBuses())
            m.addItem (p->name, true, current == "bus:" + p->name, [pick, key = "bus:" + p->name] { pick (key); });
        m.addSectionHeader ("OUTPUTS");
        for (auto& b : engine.getMasters())
            m.addItem (b->name, true, current == "output:" + b->name, [pick, key = "output:" + b->name] { pick (key); });
        return m;
    };

    juce::PopupMenu menu;
    menu.addSectionHeader ("APP RULES");
    auto& rules = engine.appRules;
    for (int i = 0; i < (int) rules.size(); ++i)
    {
        auto sub = destinations (rules[(size_t) i].to, [this, i] (const juce::String& to)
        {
            if (juce::isPositiveAndBelow (i, (int) engine.appRules.size()))
                engine.appRules[(size_t) i].to = to;
        });
        sub.addSeparator();
        sub.addItem ("Delete rule", [this, i]
        {
            if (juce::isPositiveAndBelow (i, (int) engine.appRules.size()))
                engine.appRules.erase (engine.appRules.begin() + i);
        });
        menu.addSubMenu (appName (rules[(size_t) i].exe) + "  ->  " + rules[(size_t) i].to.fromFirstOccurrenceOf (":", false, false), sub);
    }
    if (rules.empty())
        menu.addItem ("No rules", false, false, nullptr);

    juce::StringArray apps;
    for (auto& a : listAudioApps())
        apps.addIfNotAlreadyThere (a.exe, true);
    for (auto& s : engine.getStrips())
        if (s->source.kind == AudioEngine::Source::app && ! WindowDiscovery::isIsolated (s->source.deviceName))
            apps.addIfNotAlreadyThere (s->source.deviceName, true);
    apps.sortNatural();

    juce::PopupMenu add;
    for (auto& exe : apps)
        if (std::none_of (rules.begin(), rules.end(), [&exe] (auto& r) { return r.exe.equalsIgnoreCase (exe); }))
            add.addSubMenu (appName (exe), destinations ({}, [this, exe] (const juce::String& to) { engine.appRules.push_back ({ exe, to }); }));
    menu.addSeparator();
    menu.addSubMenu ("Add rule", add, add.getNumItems() > 0);

    menu.showMenuAsync (where);
}

// FFMPEG: ffmpeg.exe picked (it's used before the PATH's). With one picked, a menu also offers to
// go back to the PATH's.
void MainComponent::pickFfmpeg()
{
    auto choose = [this]
    {
        const auto start = Recorder::ffmpeg().existsAsFile() ? Recorder::ffmpeg() : juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory);
        chooser = std::make_unique<juce::FileChooser> ("Pick ffmpeg.exe", start, "ffmpeg.exe");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto exe = fc.getResult();
            if (! exe.existsAsFile())
                return;
            Recorder::setFfmpeg (exe);
            settings.setValue ("ffmpegPath", exe.getFullPathName());
            settings.saveIfNeeded();
            logEvent ("recording: ffmpeg is " + exe.getFullPathName());
        });
    };
    if (settings.getValue ("ffmpegPath").isEmpty())
        return choose();
    juce::PopupMenu menu;
    menu.addItem ("PICK FFMPEG.EXE...", choose);
    const auto onPath = Recorder::ffmpegOnPath();
    menu.addItem (onPath.existsAsFile() ? "USE THE ONE ON THE PATH" : "FORGET IT (NONE ON THE PATH)", [this]
    {
        Recorder::setFfmpeg ({});
        settings.removeValue ("ffmpegPath");
        settings.saveIfNeeded();
        logEvent ("recording: ffmpeg from the PATH");
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

// DEVICES > CLOCK: switching the clock under a running mix (an interface's inputs and outputs
// going) isn't safe, so the choice is kept for the next start, and a restart offered.
void MainComponent::chooseClock (const juce::String& choice)
{
    if (choice == settings.getValue ("clock"))
        return;
    settings.setValue ("clock", choice);
    settings.saveIfNeeded();
    logEvent ("clock: from the next start, set by " + (choice.isEmpty() ? juce::String ("the interface") : choice == "builtin" ? juce::String ("the built-in clock")
                                                       : choice.fromFirstOccurrenceOf (AudioEngine::windowsClockPrefix, false, false)));
    if (choice == clockChosenAtStart && ! clockFromCommandLine)
        return;   // (back to what this run started on)
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::NoIcon, "CLOCK",
                                        juce::String ("The new clock takes effect when ") + Product::name + " starts again. Restart now? The sound stops for a few seconds.",
                                        "RESTART NOW", "LATER", nullptr,
                                        juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                                        { if (result == 1 && safe != nullptr) safe->restartFaider(); }));
}

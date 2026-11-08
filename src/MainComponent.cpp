#include "MainComponent.h"
#include "ProductName.h"
#include "Session.h"
#include "ui/MixMinusBuilder.h"
#include "engine/AppCapture.h"
#include "ui/WindowBackground.h"
#include "ui/WindowSnap.h"
#include "ui/AboutBox.h"
#include "ui/Theme.h"
#include "ui/WindowsDefaults.h"
#include "StartWithWindows.h"
#include "../console/src/Console.h"   // (the console: the Mixer and Routing pages)
#include "../console/src/Simple.h"     // (SIMPLE and SETUP)
#include "../console/src/DeskOrder.h"  // (a renamed bus or output keeps its place)

namespace
{
    // The program's folder in %APPDATA% (Main.cpp has already moved it from its earlier name's).
    juce::File prepareDataDir()
    {
        const auto dir = Product::dataFolder();
        dir.createDirectory();
        return dir;
    }
}

MainComponent::MainComponent()
    : dataDir (prepareDataDir()),
      eventFile (dataDir.getChildFile ("events.log")),
      statusFile (dataDir.getChildFile ("status.txt")),
      sessionFile (dataDir.getChildFile ("session.xml"))
{
    for (auto* gone : { "coaching", "proMode", "deskWear", "themeBeforePro", "theme", "flowWhyOff", "vrDashboard", "controlPort", "phoneControl", "phoneKey" })   // (LIVE COACHING, earlier desks and looks, the SteamVR page, the control API)
        settings.removeValue (gone);
    look->applyTheme();
    juce::LookAndFeel::setDefaultLookAndFeel (&look.get());
    tooltips.setOpaque (false);   // the look draws a rounded card with a drop shadow round it
    Metering::setStandard (settings.getValue ("meterStandard", "peak"));
    if (const auto exe = settings.getValue ("ffmpegPath"); exe.isNotEmpty())   // (SETTINGS > GENERAL > FFMPEG)
        Recorder::setFfmpeg (juce::File (exe));
    AudioEngine::meterPoint = juce::jlimit (0, 2, settings.getIntValue ("meterPoint", 2));

    eventLog.setMultiLine (true);
    eventLog.setReadOnly (true);
    eventLog.setFont (juce::FontOptions (12.0f));   // (the one typeface, as everywhere)
    styleEventLog();
    devicesPanel.setLog (eventLog);   // on the DEVICES tab, along its bottom
    engine.getRemote().log = [this] (const juce::String& e) { logEvent (e); };

    watcher.rescan();   // fills `endpoints` (engine ignores it until the master runs)

    // Come back as we were left: the saved session's master if it's still installed, else
    // pick one from the Windows defaults and start a fresh mix.
    const bool firstEver = ! sessionFile.existsAsFile();   // (a first start: SETUP builds the mix)
    auto session = juce::XmlDocument::parse (sessionFile);
    interfaceName = session != nullptr ? Session::getInterfaceName (*session) : juce::String();

    // An ASIO interface's mix is made for its channels; a Windows device or the built-in clock
    // only clocks the mix, so that mix comes back on whatever clocks it today.
    const auto savedName = interfaceName;
    const bool savedOnWindows = interfaceName.startsWith (AudioEngine::windowsClockPrefix);
    const bool savedOnAsio = interfaceName.isNotEmpty() && ! savedOnWindows && interfaceName != AudioEngine::builtInClock;
    const auto savedDevice = interfaceName.fromFirstOccurrenceOf (AudioEngine::windowsClockPrefix, false, false);
    const bool savedInterfacePresent = std::any_of (endpoints.begin(), endpoints.end(), [&] (auto& e)
    {
        return savedOnAsio ? e.api == "ASIO" && e.name == interfaceName
                           : savedOnWindows && e.api == "WASAPI" && e.isOutput && e.isActive() && e.name == savedDevice;
    });
    // A saved mix that isn't loaded is set aside before anything can save over it: kept next to
    // session.xml for good, and in the history so the SESSION TIMELINE brings it back.
    auto setAside = [this] (const juce::String& why)
    {
        if (! sessionFile.existsAsFile())
            return;
        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S");
        const auto kept = dataDir.getChildFile ("session-unloaded-" + stamp + ".xml");
        sessionFile.copyFileTo (kept);
        const auto history = dataDir.getChildFile ("history");
        history.createDirectory();
        sessionFile.copyFileTo (history.getChildFile ("session-" + stamp + ".xml"));
        logEvent ("session: " + why + " - your saved mix is kept as " + kept.getFileName()
                  + " (the SESSION TIMELINE brings it back)");
    };

    // --clock builtin | windows | <ASIO driver>: starts on that clock, with the saved mix. Without
    // it, DEVICES > CLOCK's choice does the same ("builtin", or "windows:" and a playback device).
    const auto args = juce::JUCEApplicationBase::getCommandLineParameterArray();
    juce::String wanted;
    if (const int i = args.indexOf ("--clock"); i >= 0 && i + 1 < args.size())
    {
        wanted = args[i + 1].unquoted();
        clockFromCommandLine = true;
    }
    else
        wanted = clockChosenAtStart = settings.getValue ("clock");
    if (wanted.startsWith (AudioEngine::windowsClockPrefix))
    {
        // A playback device by name: if it's there now, else the best Windows one.
        const auto device = wanted.fromFirstOccurrenceOf (AudioEngine::windowsClockPrefix, false, false);
        const bool there = std::any_of (endpoints.begin(), endpoints.end(), [&] (auto& e)
                                        { return e.api == "WASAPI" && e.isOutput && e.isActive() && e.name == device && ! engine.isDeviceDisabled (e.name); });
        interfaceName = there ? wanted : pickClock (false);
    }
    else if (wanted.isNotEmpty())
    {
        interfaceName = wanted.equalsIgnoreCase ("builtin") ? AudioEngine::builtInClock
                      : wanted.equalsIgnoreCase ("windows") ? pickClock (false) : wanted;
        // Part of a driver's name: its whole name - the saved mix's own driver if it's one that
        // matches (several can: a driver installed for hardware that isn't plugged in), else the first.
        if (interfaceName == wanted && ! (savedOnAsio && savedName.containsIgnoreCase (wanted)))
        {
            for (auto& e : endpoints)
                if (e.api == "ASIO" && e.name.containsIgnoreCase (wanted))
                {
                    interfaceName = e.name;
                    break;
                }
        }
        else if (interfaceName == wanted)
            interfaceName = savedName;
    }
    if (wanted.isNotEmpty())
    {
        savedMaster = savedName;   // (a run on a chosen clock leaves the mix's own interface as it was)
        logEvent ("master: " + interfaceName + (clockFromCommandLine ? " (--clock)" : " (DEVICES > CLOCK)"));
        clockForced = true;
    }
    else if (! savedInterfacePresent)
    {
        if (session != nullptr && savedOnAsio)
        {
            setAside ("the saved interface " + interfaceName.quoted() + " isn't connected, so a fresh mix was started");
            session = nullptr;
        }
        else if (session == nullptr && sessionFile.existsAsFile())
            setAside ("session.xml couldn't be read, so a fresh mix was started");
        interfaceName = pickClock (true);
    }

    bool started = false;
    if (interfaceName.isNotEmpty())
    {
        started = engine.startInterface (interfaceName, settings.getIntValue ("bufferSize", 0));

        // It wouldn't open (another app holding it, say): the next clock down, unless it's the
        // saved mix's own interface (that mix waits for it: set aside below, nothing saved over it).
        const bool mixNeedsIt = session != nullptr && savedOnAsio && savedInterfacePresent && interfaceName == savedName;
        if (! started && ! mixNeedsIt)
            for (const auto& next : { pickClock (false), AudioEngine::builtInClock })
                if (next != interfaceName && engine.startInterface (next))
                {
                    interfaceName = next;
                    started = true;
                    break;
                }
    }

    if (started)
    {
        engine.refreshDevices();   // works out which Windows adapter the master is

        if (session != nullptr && Session::restore (engine, *session, [this] (const juce::String& e) { logEvent (e); }, endpoints))
            logEvent ("session: restored " + juce::String ((int) engine.getStrips().size()) + " channels, "
                      + juce::String ((int) engine.getMasters().size()) + " masters");
        else
        {
            if (session != nullptr)
                setAside ("the saved mix couldn't be restored, so a fresh mix was started");
            if (! firstEver)
                createDefaultMix();
        }
        sessionLoaded = true;   // a mix restored or made here: saving it is safe now

        engine.refreshDevices();
        engine.refreshDucking();
    }
    else if (session != nullptr)
    {
        // The saved master is connected but wouldn't open (a DAW holding its driver, its rate or
        // buffer refused). Nothing was restored, so the empty mix must never be saved over it:
        // it's set aside, and saveSession() writes nothing for the rest of this run.
        setAside ("the saved interface " + interfaceName.quoted() + " couldn't be opened (in use by another app, or its settings refused)");
        logEvent ("session: " + juce::String (Product::name) + " is running without its interface; your mix is kept and nothing is saved over it"
                  " (free the interface and restart " + juce::String (Product::name) + ", or the SESSION TIMELINE)");
    }

    // SIMPLE or PRO, as it was left. The first time: SIMPLE, unless the mix has buses or
    // combinators (SIMPLE doesn't show them), then PRO.
    if (! settings.containsKey ("deskMode"))
        settings.setValue ("deskMode", engine.getBuses().empty() ? "simple" : "pro");

    // The look, as it was left (the built-in one the first time). A look file that has gone or
    // is broken leaves the built-in alone.
    {
        const auto chosen = settings.getValue ("look", "Default");
        if (! Theme::useLook (chosen))
        {
            Theme::useLook ("Default");
            settings.setValue ("look", "Default");
        }
    }

    mixer = std::make_unique<MixerComponent> (engine);
    consoleDesk = ConsoleDesk::make (engine, *mixer, settings.getValue ("deskMode") == "simple",
                                     [this] (bool simple) { settings.setValue ("deskMode", simple ? "simple" : "pro"); settings.saveIfNeeded(); },
                                     [this] { showSetup(); });   // (the console on the Mixer page; Faider's mixer stays behind it for its tools)
    tabs.addTab ("Mixer", juce::Colours::transparentBlack, consoleDesk.get(), false);
    consoleSidebar = ConsoleDesk::makeSidebar (tabs, *consoleDesk);   // (the pages as cards down the left; the tab caps hide)
    addAndMakeVisible (*consoleSidebar);
    tabs.getTabbedButtonBar().setVisible (false);
    // The selected channel (SEL), remembered.
    mixer->onSelect = [this] (const juce::String& key) { settings.setValue ("selectedChannel", key); settings.saveIfNeeded(); };
    if (const auto chosen = settings.getValue ("selectedChannel"); chosen.isNotEmpty())
        mixer->setSelected (chosen);
    consoleRouting = ConsoleDesk::makeRouting (*consoleDesk);
    tabs.addTab ("Routing", juce::Colours::transparentBlack, consoleRouting.get(), false);
    // SETUP is not a page: it asks its questions in its own window (showSetup), from a first start,
    // from FIND, and from the SETUP key on the SIMPLE desk. The pages are the desk, routing and work.
    pads = std::make_unique<PadsPanel> (engine, *mixer);
    tape = std::make_unique<TapePanel> (engine, *mixer);

    tools = std::make_unique<ToolsTab> (std::initializer_list<std::pair<juce::String, juce::Component*>> { { "Pads", pads.get() }, { "Tape", tape.get() } });
    tabs.addTab ("Tools", juce::Colours::transparentBlack, tools.get(), false);
    tabs.addTab ("Devices", juce::Colours::transparentBlack, &devicesPanel, false);
    devicesPanel.getTools().onDevicesChanged = [this] { sortDevices(); };
    auto& clockBar = devicesPanel.getClock();
    clockBar.choice = [this] { return settings.getValue ("clock"); };
    clockBar.choose = [this] (const juce::String& c) { chooseClock (c); };
    clockBar.note = [this]
    {
        if (clockFromCommandLine)
            return juce::String ("--clock on the command line sets it for this run");
        return settings.getValue ("clock") != clockChosenAtStart ? juce::String ("The new choice takes effect when ") + Product::name + " starts again" : juce::String();
    };

    // Devices list order: a column (DevicesTab::columns), "-" in front for descending; remembered.
    if (const auto sort = settings.getValue ("deviceSort"); sort.isNotEmpty())
    {
        sortColumn = std::abs (sort.getIntValue());
        sortAscending = ! sort.startsWith ("-");
        devicesPanel.getHeader().setSort (sortColumn, sortAscending);
        sortDevices();
    }
    devicesPanel.getHeader().onSort = [this] (int column, bool ascending)
    {
        sortColumn = column;
        sortAscending = ascending;
        settings.setValue ("deviceSort", (ascending ? "" : "-") + juce::String (column));
        settings.saveIfNeeded();
        sortDevices();
    };
    tabs.setTabBarDepth (PageTabs::depth);   // room for the tab caps
    tabs.setOutline (0);
    addAndMakeVisible (tabs);

    // Zoom: - 100% + at the right of the tab bar (click the percentage to reset), and Ctrl +/-/0.
    zoomOut.onClick = [this] { stepZoom (-1); };
    zoomIn.onClick = [this] { stepZoom (1); };
    zoomReset.onClick = [this] { setZoom (1.0f); };
    zoomOut.setTooltip ("Zoom out (Ctrl -)");
    zoomIn.setTooltip ("Zoom in (Ctrl +)");
    zoomReset.setTooltip ("Zoom 100% (Ctrl 0)");
    findButton.setTooltip ("Search everything (Ctrl+K)");
    findButton.onClick = [this] { openCommandPalette(); };
    health.onClick = [this] { tabs.setCurrentTabIndex (tabs.getTabNames().indexOf ("Devices")); };
    addAndMakeVisible (health);
    miniButton.setTooltip ("Mini mixer: the outputs' faders in a small window (the tray icon opens it too)");
    miniButton.onClick = [this] { toggleMini(); };
    addAndMakeVisible (miniButton);
    historyButton.setTooltip ("Undo history (Ctrl+H)");
    historyButton.onClick = [this] { showHistory (juce::PopupMenu::Options().withTargetComponent (historyButton)); };
    for (auto* b : { &zoomOut, &zoomReset, &zoomIn, &findButton, &historyButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    // Scenes: keys after the tabs. Their hotkeys go with the pads' and macros' (one set of global keys).
    sceneBar = std::make_unique<SceneBar> (engine);
    sceneBar->setHotkey = [this] (int scene, const juce::KeyPress& key) { return pads->setSceneHotkey (scene, key); };
    sceneBar->onListChanged = [this] { pads->scenesChanged(); resized(); };
    addAndMakeVisible (*sceneBar);

    history = std::make_unique<MixHistory> (engine);
    // A source or device switched (after its fade-out): that strip shown again, the rest left alone.
    engine.onAdded = [this] (const juce::String& kind, int index, int slot) { structureStep (kind, index, slot, true); };
    engine.onRemoving = [this] (const juce::String& kind, int index, int slot) { structureStep (kind, index, slot, false); };

    // Rack changes (units moved, cables patched or pulled out) are undo steps too: the channel by
    // its name, the rack by its placement and cables.
    engine.onRackChanged = [this] (AudioEngine::Strip& s, const AudioEngine::RackPatch& before)
    {
        if (history == nullptr || history->isReplaying())
            return;
        const bool isBus = std::any_of (engine.getBuses().begin(), engine.getBuses().end(), [&s] (auto& p) { return p.get() == &s; });
        auto put = [this, isBus, name = s.name] (const AudioEngine::RackPatch& patch)
        {
            for (auto& strip : isBus ? engine.getBuses() : engine.getStrips())
                if (strip->name == name)
                    engine.setRack (*strip, patch);
        };
        history->addStep ([put, before] { put (before); }, [put, after = s.rack] { put (after); }, s.name + " rack");
    };
    // Notices (Toasts.h) over everything in the window's corner: a channel clipping, a scene or a
    // macro (from whichever thread started it), and the ones added elsewhere (devices, undo).
    toasts.enabled = [this] (const juce::String& kind) { return settings.getBoolValue ("notice_" + kind, true); };
    addAndMakeVisible (toasts);
    settings.removeValue ("foldedSections");   // (the desk's sections no longer fold: it shows whole, scrolling as one)
    engine.getScenes().onRecalled = [safe = juce::Component::SafePointer<MainComponent> (this)] (int index)
    {
        juce::MessageManager::callAsync ([safe, index]
        {
            if (safe != nullptr && juce::isPositiveAndBelow (index, (int) safe->engine.getScenes().list.size()))
                safe->toasts.show ("scene", "Scene", safe->engine.getScenes().list[(size_t) index].name);
        });
    };
    engine.getMacros().onStarted = [safe = juce::Component::SafePointer<MainComponent> (this)] (int index)
    {
        juce::MessageManager::callAsync ([safe, index]
        {
            if (safe != nullptr && juce::isPositiveAndBelow (index, (int) safe->engine.getMacros().list.size()))
                safe->toasts.show ("macro", "Macro", safe->engine.getMacros().list[(size_t) index].name);
        });
    };

    // A renamed channel: the mixer's selection and banks follow it, and a bus or output keeps its
    // place on the desk (the engine sees to the rest).
    engine.onRenamed = [this] (const juce::String& kind, const juce::String& oldName, const juce::String& newName)
    {
        if (mixer != nullptr)
            mixer->renamed (kind, oldName, newName);
        if (consoleDesk != nullptr)
            ConsoleDesk::renamed (*consoleDesk, kind, oldName, newName);
        DeskOrder::renamed (kind, oldName, newName);
    };

    // Instant replay's hotkey, with the others; remembered in settings.
    tape->setReplayHotkey = [this] (const juce::String& key)
    {
        const bool ok = pads->setExtraHotkey (PadsPanel::extraHotkeyBase + 1, key, [this] { if (tape) tape->saveReplay(); }, {});
        if (ok)
        {
            settings.setValue ("replayHotkey", key);
            settings.saveIfNeeded();
        }
        return ok;
    };
    if (const auto key = settings.getValue ("replayHotkey"); key.isNotEmpty() && tape->setReplayHotkey (key))
        tape->replayHotkey = key;

    // The in-game overlay: Ctrl+Shift+O anywhere shows and hides it; where it was dragged is remembered.

    overlay = std::make_unique<Overlay> (engine);
    // (Remembered in screen pixels: the zoom scales window positions, so a zoom change would move it.)
    overlay->onMoved = [this] (juce::Point<int> p)
    {
        const float zoomNow = juce::Desktop::getInstance().getGlobalScaleFactor();
        settings.setValue ("overlayPx", juce::roundToInt ((float) p.x * zoomNow));
        settings.setValue ("overlayPy", juce::roundToInt ((float) p.y * zoomNow));
        settings.saveIfNeeded();
    };
    if (! pads->setExtraHotkey (PadsPanel::extraHotkeyBase + 2, "ctrl + shift + O", [this] { toggleOverlay(); }, {}))
        logEvent ("overlay: Ctrl+Shift+O is taken by another app (open it from FIND)");
    if (settings.getBoolValue ("overlayShown"))
        toggleOverlay();
    // A first start: SETUP comes before anything else, a moment after the window is up.
    if (firstEver && started)
        juce::Timer::callAfterDelay (800, [safe = juce::Component::SafePointer<MainComponent> (this)]
                                       { if (safe != nullptr && safe->isShowing()) safe->showSetup(); });
    // PRE-FLIGHT, once the audio and devices have settled, if something needs a look (in the
    // tray: a notice instead of a dialog).
    if (settings.getBoolValue ("preflightOnStart", true) && ! firstEver)   // (a first start: SETUP comes first)
        juce::Timer::callAfterDelay (6000, [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe == nullptr)
                return;
            if (safe->isShowing())
                safe->showPreflight (true);
            else if (const auto checks = safe->preflightChecks(); std::any_of (checks.begin(), checks.end(), [] (auto& c) { return ! c.ok; }))
                safe->toasts.show ("device", "Pre-flight", "Something in the setup needs a look (FIND: PRE-FLIGHT)");
        });
    // First-run tips: the first time each page is shown, a tip about it in the corner (once each;
    // SETTINGS > GENERAL turns them off or shows them again). The Mixer's comes a moment after start.
    tabs.onPageShown = [this] (int, const juce::String& name) { showTip (name); };
    juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr && safe->isShowing() && safe->tabs.getCurrentTabIndex() == 0)
            safe->showTip ("Mixer");
    });
    if (settings.getBoolValue ("miniShown"))   // (up again where it was left)
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe != nullptr && safe->mini == nullptr) safe->toggleMini(); });

    // MIDI-bound keys of the app's own (RemoteControl::appTarget): the pads (held as long as the
    // key), the tape's transport, REC and replay SAVE.
    engine.getRemote().appTarget = [this] (const juce::String& target, float, bool pressed, bool released)
    {
        const auto arg = target.fromFirstOccurrenceOf (":", false, false);
        auto& deck = engine.getTape();
        if (target.startsWith ("pad:"))
        {
            if (pressed && pads != nullptr)  pads->pressPad (arg.getIntValue());     // its sound and its macro
            if (released && pads != nullptr) pads->releasePad (arg.getIntValue());
        }
        else if (target.startsWith ("tape:") && pressed)
        {
            if (arg == "play" && ! deck.isPlaying())       deck.togglePlay();
            else if (arg == "pause" && deck.isPlaying())   deck.togglePlay();
            else if (arg == "stop")                        deck.stop();
            else if (arg == "next")                        deck.next();
            else if (arg == "prev")                        deck.previous();
        }
        else if (target == "rec" && pressed && tape != nullptr)
            tape->toggleRecording();
        else if (target == "replay" && pressed && tape != nullptr)
            tape->saveReplay();
    };

    // The saved zoom applies before the window opens, so it opens at the usual size on screen.
    zoom = juce::jlimit (minZoom, maxZoom, (float) settings.getDoubleValue ("zoom", 1.0));
    juce::Desktop::getInstance().setGlobalScaleFactor (zoom);
    zoomReset.setButtonText (juce::String (juce::roundToInt (zoom * 100.0f)) + "%");

    setSize (juce::roundToInt (1200.0f / zoom), juce::roundToInt (800.0f / zoom));
    startTimer (1000);

    warnMutedTalk = settings.getBoolValue ("warnMutedTalk", true);
    muteWatch.tick = [this] { watchMutedTalk(); watchClipping(); };
    muteWatch.startTimerHz (10);

    // CHECK FOR UPDATES: half a minute after start, at most once a day.
    juce::Timer::callAfterDelay (30000, [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe == nullptr || ! safe->settings.getBoolValue ("checkForUpdates", true))
            return;
        const auto last = safe->settings.getValue ("lastUpdateCheck").getLargeIntValue();
        const auto now = juce::Time::currentTimeMillis();
        if (now - last >= 24LL * 60 * 60 * 1000 || now < last)
            safe->checkForUpdates (false);
    });

    makeSettings();
}

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
    general.heading ("ON START");
    general.toggle ("PRE-FLIGHT: CHECK THE SETUP", juce::String ("A few seconds after ") + Product::name + " starts: the audio, the mic cable and route, cut mics (shown only if something's wrong)",
                    [this] { return settings.getBoolValue ("preflightOnStart", true); },
                    [remember] (bool on) { remember ("preflightOnStart", on); });
    general.action ("PRE-FLIGHT NOW...", "Check the setup now", [this] { showPreflight (false); });
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
    // set for them all), instant replay's, and the fixed ones.
    hotkeysPage = std::make_unique<HotkeysPage> ([this]
    {
        std::vector<HotkeysPage::Entry> list;
        auto& p = engine.getPads();
        for (int i = 0; i < Pads::count; ++i)
            if (const auto& pad = p.pad (i); pad.file.isNotEmpty() || pad.hotkey.isNotEmpty())
                list.push_back ({ "PADS", "PAD " + juce::String (i + 1) + (pad.name.isNotEmpty() ? ":  " + pad.name.toUpperCase() : juce::String()),
                                  pad.hotkey, [this, i] (const juce::KeyPress& k) { return pads->setHotkey (i, k); } });
        auto& macros = engine.getMacros().list;
        for (int i = 0; i < (int) macros.size(); ++i)
            list.push_back ({ "MACROS", macros[(size_t) i].name.toUpperCase(), macros[(size_t) i].hotkey,
                              [this, i] (const juce::KeyPress& k) { return pads->setMacroHotkey (i, k); } });
        auto& scenes = engine.getScenes().list;
        for (int i = 0; i < (int) scenes.size(); ++i)
            list.push_back ({ "SCENES", scenes[(size_t) i].name.toUpperCase(), scenes[(size_t) i].hotkey,
                              [this, i] (const juce::KeyPress& k)
                              {
                                  const bool ok = pads->setSceneHotkey (i, k);
                                  if (ok && sceneBar != nullptr) sceneBar->repaint();
                                  return ok;
                              } });
        list.push_back ({ "TAPE", "SAVE INSTANT REPLAY", tape->replayHotkey, [this] (const juce::KeyPress& k)
        {
            const auto key = k.isValid() ? k.getTextDescription() : juce::String();
            if (! tape->setReplayHotkey (key))
                return false;
            tape->replayHotkey = key;
            return true;
        } });
        list.push_back ({ "ANYWHERE (FIXED)", "SHOW / HIDE THE IN-GAME OVERLAY", "ctrl + shift + O", nullptr });
        list.push_back ({ juce::String ("IN ") + Product::nameUpper + " (FIXED)", "FIND (COMMAND PALETTE)", "ctrl + K", nullptr });
        list.push_back ({ juce::String ("IN ") + Product::nameUpper + " (FIXED)", "MIDI MAP", "ctrl + L", nullptr });
        list.push_back ({ juce::String ("IN ") + Product::nameUpper + " (FIXED)", "UNDO / REDO", "ctrl + Z  /  ctrl + Y", nullptr });
        list.push_back ({ juce::String ("IN ") + Product::nameUpper + " (FIXED)", "UNDO HISTORY", "ctrl + H", nullptr });
        list.push_back ({ juce::String ("IN ") + Product::nameUpper + " (FIXED)", "ZOOM IN / OUT / 100%", "ctrl + plus / minus / 0", nullptr });
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

// The command palette's commands, made fresh each time it opens: pages, every channel (mute,
// select, its rack), scenes, macros, pads, themes, and the app's own actions.
void MainComponent::openCommandPalette()
{
    if (palette != nullptr)
        return palette->grabFocus();

    std::vector<CommandPalette::Command> commands;
    auto add = [&commands] (juce::String name, juce::String detail, std::function<void()> run)
    {
        commands.push_back ({ std::move (name), std::move (detail), std::move (run) });
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
    add ("ZERO DESK", "Things left set: solos, key listen, gains far off, outputs cut", [this] { showZero(); });
    add ("MIX-MINUS AND STEMS", "A feed per guest without themselves; voice, music and game stems", [this] { MixMinusBuilder::showBuilder (engine, [] {}, this); });
    add (mini != nullptr ? "HIDE MINI MIXER" : "MINI MIXER", "The outputs' faders in a small window, PIN keeps it on top", [this] { toggleMini(); });
    add ("HISTORY", "Ctrl+H - every undo step, go back to any",[this] { showHistory (juce::PopupMenu::Options().withTargetComponent (historyButton)); });
    add ("MIDI MAP", "Ctrl+L - link controls to MIDI", [this] { toggleMidiMap(); });
    for (auto [section, detail] : { std::pair ("General", "Start with Windows, zoom, meters"), std::pair ("Macros", "Macros and their triggers"),
                                    std::pair ("Hotkeys", "Every global hotkey"), std::pair ("Control", "MIDI and OSC") })
        add ("SETTINGS: " + juce::String (section).toUpperCase(), detail, [this, section = juce::String (section)] { openSettings (section); });
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

// A CHANNEL CLIPPING: a channel's source or its output reaching full scale gets a notice in the
// corner (Toasts.h, "clip"), each channel at most once in 10 seconds; the event log has it too.
void MainComponent::watchClipping()
{
    const auto now = juce::Time::getMillisecondCounter();
    for (auto* list : { &engine.getStrips(), &engine.getBuses() })
        for (auto& s : *list)
        {
            if (! s->enabled.load())
                continue;
            const float peak = juce::jmax (juce::jmax (s->sourcePeakL.load(), s->sourcePeakR.load()), juce::jmax (s->peakL.load(), s->peakR.load()));
            if (peak < 0.999f)
                continue;
            auto& last = clipNoticed[s->name];
            if (last != 0 && now - last < 10000)
                continue;
            last = now;
            logEvent ("clip: " + s->name);
            toasts.show ("clip", "Clipping", s->name);
        }
}

// Talking while muted: a mic channel (a device's input) that's muted, with its source heard above
// -30 dBFS for half a second, says so: the event log, and a notice from the tray (the window may
// be behind a game). At most once every 15 seconds.
void MainComponent::watchMutedTalk()
{
    if (! warnMutedTalk)
        return;
    const auto now = juce::Time::getMillisecondCounter();
    for (auto& s : engine.getStrips())
    {
        const bool mic = s->source.kind == AudioEngine::Source::interfaceInputs || s->source.kind == AudioEngine::Source::device;
        auto& heard = talkingMuted[s->name];
        if (! mic || ! s->enabled.load() || ! s->mute.load())
        {
            heard = 0;
            continue;
        }
        const float level = juce::jmax (s->sourcePeakL.load(), s->sourcePeakR.load());
        heard = level > juce::Decibels::decibelsToGain (-30.0f) ? heard + 1 : juce::jmax (0, heard - 1);
        if (heard >= 5 && now - lastMutedWarning > 15000)
        {
            lastMutedWarning = now;
            heard = 0;
            logEvent ("mic: " + s->name + " is cut, but you're talking");
            if (onNotify)
                onNotify ("Your mic is cut", s->name + " is CUT. Press its CUT again to be heard.");
        }
    }
}


// The event log in the theme's printing colours: its text keeps the colour it was written in,
// so every line is recoloured too.
void MainComponent::styleEventLog()
{
    eventLog.setColour (juce::TextEditor::textColourId, Console::Colours::legend());
    eventLog.setColour (juce::TextEditor::highlightColourId, Console::Colours::legendDim().withAlpha (0.35f));
    eventLog.setColour (juce::TextEditor::highlightedTextColourId, Console::Colours::legend());
    eventLog.applyColourToAllText (Console::Colours::legend(), true);
}

// The tab bar and the desk's strips whole (as resized() lays them out; the event log is on the
// DEVICES tab).
int MainComponent::neededHeight() const
{
    return tabs.getTabBarDepth() + 1000;   // (a first window's height; a saved one wins, and it's kept on the screen)
}

// Scales the whole app (every view, menu and plugin window). The window keeps its size on
// screen; what's in it gets bigger or smaller.
// The next of the three zoom stages up or down from the zoom now (a CUSTOM size steps to the stage
// beyond it).
void MainComponent::stepZoom (int direction)
{
    if (direction > 0)
    {
        for (float s : zoomStages)
            if (s > zoom + 0.01f)
                return setZoom (s);
    }
    else
        for (int i = (int) std::size (zoomStages); --i >= 0;)
            if (zoomStages[i] < zoom - 0.01f)
                return setZoom (zoomStages[i]);
}

void MainComponent::setZoom (float newZoom)
{
    newZoom = juce::jlimit (minZoom, maxZoom, std::round (newZoom * 20.0f) / 20.0f);   // (5% steps: the stages and CUSTOM's sizes)
    if (juce::approximatelyEqual (newZoom, zoom))
        return;

    auto* top = getTopLevelComponent();
    auto* window = dynamic_cast<juce::ResizableWindow*> (top);
    const bool keepSize = top != this && ! (window != nullptr && (window->isFullScreen() || window->isMinimised()))
                          && ! (top->getPeer() != nullptr && WindowSnap::isArranged (*top->getPeer()));   // (maximised or snapped: Windows' size, left as it is)
    const auto onScreen = top->getBounds().toFloat() * zoom;

    zoom = newZoom;
    juce::Desktop::getInstance().setGlobalScaleFactor (zoom);

    if (keepSize && window != nullptr)
        window->setBoundsConstrained ((onScreen / zoom).toNearestInt());   // sizes that scale exactly (see MainWindow)
    else if (keepSize)
        top->setBounds ((onScreen / zoom).toNearestInt());


    zoomReset.setButtonText (juce::String (juce::roundToInt (zoom * 100.0f)) + "%");
    settings.setValue ("zoom", (double) zoom);
    settings.saveIfNeeded();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (! key.getModifiers().isCommandDown())
        return tabs.getCurrentTabIndex() == 0 && consoleDesk != nullptr && ConsoleDesk::handleKey (*consoleDesk, key);   // the desk's own keys

    // Windows hands Ctrl+letter over as its control character (Ctrl+L is 12), and twice: back to
    // the letter, and the second of the pair (within a few ms) dropped.
    auto code = key.getKeyCode();
    if (code >= 1 && code <= 26)
        code += 'A' - 1;
    const auto ch = key.getTextCharacter();
    const auto now = juce::Time::getMillisecondCounter();
    if (code == lastShortcut && now - lastShortcutAt < 40)
        return true;
    lastShortcut = code;
    lastShortcutAt = now;

    if (code == 'K' || code == 'k')   // the command palette
    {
        openCommandPalette();
        return true;
    }

    if (code == 'H' || code == 'h')   // the undo history
    {
        showHistory (juce::PopupMenu::Options().withTargetComponent (historyButton));
        return true;
    }

    if (code == 'L' || code == 'l')   // MIDI MAP ("learn"; Ctrl+M would arrive as Return)
    {
        toggleMidiMap();
        return true;
    }

    // Undo / redo for the mix.
    if (history != nullptr && (code == 'Z' || code == 'z' || code == 'Y' || code == 'y'))
    {
        undoRedo (code == 'Y' || code == 'y' || key.getModifiers().isShiftDown());
        return true;
    }

    if (code == '=' || ch == '+' || code == juce::KeyPress::numberPadAdd)
        stepZoom (1);
    else if (code == '-' || code == juce::KeyPress::numberPadSubtract)
        stepZoom (-1);
    else if (code == '0' || code == juce::KeyPress::numberPad0)
        setZoom (1.0f);
    else
        return false;

    return true;
}

// What clocks the engine, best first: the ASIO driver for the hardware Windows plays through by
// default, otherwise the first driver that isn't a virtual (VoiceMeeter) one; else a Windows
// playback device (the default, unless it's a virtual cable or a screen/headset that comes and
// goes); else the built-in clock, which needs no device at all.
juce::String MainComponent::pickClock (bool asio, bool announce)
{
    auto logEvent = [this, announce] (const juce::String& e) { if (announce) this->logEvent (e); };
    juce::String defaultAdapter;
    for (auto& e : endpoints)
        if (e.api == "WASAPI" && e.isOutput && e.isDefault)
            defaultAdapter = e.adapter;

    if (asio && engine.hasAsio())
    {
        for (auto& e : endpoints)
            if (e.api == "ASIO" && asioMatchesAdapter (e.name, defaultAdapter) && ! clockRefused.contains (e.name))
            {
                logEvent ("master: " + e.name + " (drives Windows default playback, " + defaultAdapter + ")");
                return e.name;
            }

        for (auto& e : endpoints)
            if (e.api == "ASIO" && ! e.name.containsIgnoreCase ("Voicemeeter") && ! clockRefused.contains (e.name))
            {
                logEvent ("master: " + e.name + " (no ASIO driver for " + defaultAdapter + ")");
                return e.name;
            }
    }

    auto playback = [this] (const EndpointInfo& e, bool steadyOnly)
    {
        return e.api == "WASAPI" && e.isOutput && e.isActive() && ! engine.isDeviceDisabled (e.name)
            && ! clockRefused.contains (AudioEngine::windowsClockPrefix + e.name)   // (one that wouldn't start: the next is tried)
            && virtualMicPartner (e, endpoints).isEmpty()
            && ! e.name.containsIgnoreCase ("Voicemeeter") && ! e.adapter.containsIgnoreCase ("Virtual")
            && (! steadyOnly || classifyEndpoint (e).contains ("master"));
    };
    for (const bool steadyOnly : { true, false })
    {
        for (auto& e : endpoints)
            if (e.isDefault && playback (e, steadyOnly))
            {
                logEvent ("master: " + e.name + " (Windows default playback; no ASIO driver)");
                return AudioEngine::windowsClockPrefix + e.name;
            }
        for (auto& e : endpoints)
            if (playback (e, steadyOnly))
            {
                logEvent ("master: " + e.name + " (Windows playback; no ASIO driver)");
                return AudioEngine::windowsClockPrefix + e.name;
            }
    }

    logEvent ("master: the built-in clock (no ASIO driver or playback device)");
    return AudioEngine::builtInClock;
}

// The Windows device clocking the engine is gone (unplugged, disabled): the next best clock takes
// over. On the built-in clock, a playback device that arrives takes over from it.
void MainComponent::checkClock()
{
    if (! sessionLoaded)
        return;

    const auto clock = engine.getClock();
    juce::String next;
    bool dead = false;   // the Windows device clocking it still listed, but closed under it

    if (clock == AudioEngine::Clock::windows)
    {
        const bool there = std::any_of (endpoints.begin(), endpoints.end(), [this] (const EndpointInfo& e)
                                        { return e.api == "WASAPI" && e.isOutput && e.isActive() && e.name == engine.getClockDevice(); });
        dead = ! engine.isClockOpen();
        if (! there || engine.isDeviceDisabled (engine.getClockDevice()) || dead)
            next = pickClock (true);   // (refused ones skipped: pickClock)
    }
    else if (clock == AudioEngine::Clock::builtIn && ! clockForced)
    {
        if (const auto better = pickClock (false, false); better != AudioEngine::builtInClock)
            next = better;
    }

    if (next.isEmpty() || (next == interfaceName && ! dead))   // (dead: the same device opened again, or refused and the next tried)
        return;

    if (! engine.switchInterface (next, settings.getIntValue ("bufferSize", 0)))
    {
        clockRefused.add (next);   // (not tried again until the devices change: it would be tried every second)
        next = AudioEngine::builtInClock;   // (never fails)
        engine.switchInterface (next);
    }
    interfaceName = next;
    toasts.show ("device", "Clock", engine.getClockName() + (engine.getClockDevice().isNotEmpty() ? ": " + engine.getClockDevice() : juce::String()));
}

// An undo step for a strip or plugin just added (adding) or about to be removed. Undo and redo
// find it again by name (indexes shift as other strips come and go), or by its index if renamed.
// A removed strip or plugin is kept as session XML, with its plugins' state, to put back.
void MainComponent::structureStep (const juce::String& kind, int index, int slot, bool adding)
{
    if (history == nullptr || history->isReplaying())
        return;

    auto find = [this] (const juce::String& k, const juce::String& name, int fallback)
    {
        auto look = [&] (const auto& list)
        {
            for (int i = 0; i < (int) list.size(); ++i)
                if (list[(size_t) i]->name == name)
                    return i;
            return juce::isPositiveAndBelow (fallback, (int) list.size()) ? fallback : -1;
        };
        return k == "output" ? look (engine.getMasters()) : k == "bus" ? look (engine.getBuses()) : look (engine.getStrips());
    };
    auto target = [kind] (int i)   // (outputs have no plugins)
    {
        using T = MixerComponent::InsertTarget;
        return T { kind == "bus" ? T::bus : T::strip, i, -1 };
    };
    const juce::String name = kind == "output" ? engine.getMasters()[(size_t) index]->name
                            : kind == "bus"    ? engine.getBuses()[(size_t) index]->name
                                               : engine.getStrips()[(size_t) index]->name;
    auto saved = std::make_shared<std::unique_ptr<juce::XmlElement>>();

    auto takeAway = [this, kind, name, index, slot, find, target, saved]
    {
        const int i = find (kind, name, index);
        if (i < 0)
            return;
        if (slot < 0)
        {
            *saved = Session::saveStrip (engine, kind, i);
            if (kind != "output")
                mixer->closeEditorsFor (target (i));
            if (kind == "output")   engine.removeMaster (i);
            else if (kind == "bus") engine.removeBus (i);
            else                    engine.removeStrip (i);
        }
        else
        {
            *saved = Session::saveInsert (engine, kind, i, slot);
            mixer->removeInsert (target (i), slot);
        }
    };
    auto putBack = [this, kind, name, index, slot, find, saved]
    {
        if (*saved == nullptr)
            return;
        auto log = [this] (const juce::String& e) { logEvent (e); };
        if (slot < 0)
        {
            const int i = Session::restoreStrip (engine, **saved, log);
            if (kind == "input" && i >= 0 && i != index)
                engine.moveStrip (i, index);   // back where it was (inputs can move; masters and outputs go at the end)
        }
        else
            Session::restoreInsert (engine, kind, find (kind, name, index), slot, **saved, log);
    };

    if (! adding)   // about to go: keep it now
        *saved = slot < 0 ? Session::saveStrip (engine, kind, index) : Session::saveInsert (engine, kind, index, slot);
    history->addStep (adding ? std::function<void()> (takeAway) : putBack, adding ? std::function<void()> (putBack) : takeAway,
                      juce::String (adding ? "add " : "remove ") + (slot < 0 ? name : "a plugin on " + name));
}

// Ctrl+Z / Ctrl+Y (and the palette's UNDO, REDO): the step, logged, and a notice saying what it was.
void MainComponent::undoRedo (bool redo)
{
    if (history == nullptr)
        return;
    const auto labels = redo ? history->redoLabels() : history->undoLabels();
    if (! (redo ? history->redo() : history->undo()))
        return;
    const auto what = labels.isEmpty() ? juce::String() : labels[labels.size() - 1];
    logEvent ((redo ? "redo: " : "undo: ") + what);
    toasts.show ("undo", redo ? "Redo" : "Undo", what);
}

// The HISTORY key (and Ctrl+H, and the palette): every step there is, what each did, newest at
// the top of UNDO; picking one goes back (or forward) to just before (or after) it.
void MainComponent::showHistory (juce::PopupMenu::Options where)
{
    if (history == nullptr)
        return;
    const auto undos = history->undoLabels(), redos = history->redoLabels();
    juce::PopupMenu menu;
    menu.addSectionHeader ("REDO");
    if (redos.isEmpty())
        menu.addItem ("Nothing to redo", false, false, [] {});
    for (int i = 0; i < redos.size(); ++i)   // the furthest away at the top, the next one just above NOW
        menu.addItem (redos[i], [this, steps = redos.size() - i] { for (int n = 0; n < steps; ++n) undoRedo (true); });
    menu.addSectionHeader ("NOW");
    menu.addSectionHeader ("UNDO");
    if (undos.isEmpty())
        menu.addItem ("Nothing to undo", false, false, [] {});
    for (int i = undos.size(); --i >= 0;)    // the latest first
        menu.addItem (undos[i], [this, steps = undos.size() - i] { for (int n = 0; n < steps; ++n) undoRedo (false); });
    menu.showMenuAsync (where);
}

// MIDI MAP mode (MidiMap.h): an overlay over the whole window; the tabs and SETTINGS > CONTROL still work.
void MainComponent::toggleMidiMap()
{
    if (midiMap != nullptr)
    {
        midiMap = nullptr;
        return;
    }
    midiMap = std::make_unique<MidiMapOverlay> (engine.getRemote(), *this, [this] (juce::Point<int> p)
    {
        const auto screen = localPointToGlobal (p);
        auto& bar = tabs.getTabbedButtonBar();
        for (int i = 0; i < bar.getNumTabs(); ++i)
            if (auto* b = bar.getTabButton (i); b != nullptr && b->getScreenBounds().contains (screen))
                return true;
        // and SETTINGS > CONTROL (its MIDI MAP key turns the mode off)
        return controlPanel != nullptr && controlPanel->isShowing() && controlPanel->getScreenBounds().contains (screen);
    });
    midiMap->onClose = [this] { juce::MessageManager::callAsync ([self = juce::Component::SafePointer<MainComponent> (this)] { if (self != nullptr) self->midiMap = nullptr; }); };
    addAndMakeVisible (*midiMap);
    midiMap->setBounds (getLocalBounds());
    midiMap->toFront (true);
}

void MainComponent::openMidiPanel()
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new MidiPanel (engine.getRemote(), [this] { if (midiMap == nullptr) toggleMidiMap(); }));
    options.dialogTitle = "MIDI";
    options.dialogBackgroundColour = Console::Colours::panel();
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

MainComponent::~MainComponent()
{
    // First what's declared before `look` and the engine (so destroyed after them): nothing of
    // theirs may run once those are gone.
    muteWatch.stopTimer();
    updates = nullptr;   // (a check still waiting is cancelled)
    palette = nullptr;
    engine.getScenes().onStructureChanged = nullptr;   // (it calls into the mixer)

    mini = nullptr;          // (its mixer reads the engine)
    overlay = nullptr;
    settingsTab = nullptr;
    controlPanel = nullptr;    // it reads the remote
    hotkeysPage = nullptr;     // (its list reads the pads, macros and scenes)
    midiMap = nullptr;
    engine.getRemote().appTarget = nullptr;
    engine.onAdded = nullptr;
    engine.onRemoving = nullptr;
    engine.onRoutingApplied = nullptr;
    saveSession();
    tabs.clearTabs();
    tools = nullptr;

    pads = nullptr;
    tape = nullptr;
    mixer = nullptr;   // closes plugin editors before the engine destroys the plugins
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Console::Colours::panelDark());
}

// Starting mix until project files exist, built from what Windows says is primary:
// Main plays where Windows' default playback goes (through the master if it's the master's own
// hardware), and there's a strip that follows Windows' default recording device.
void MainComponent::createDefaultMix()
{
    const EndpointInfo* defaultOut = nullptr;
    for (auto& e : endpoints)
        if (e.api == "WASAPI" && e.isOutput && e.isDefault)
            defaultOut = &e;

    if (defaultOut == nullptr || engine.isInterfaceEndpoint (*defaultOut))
        engine.addMaster ("Main", { AudioEngine::interfaceOutput });
    else
        engine.addMaster ("Main", { defaultOut->name });

    if (! engine.getInterfaceInputNames().isEmpty())   // (a Windows device or the built-in clock has none)
        engine.addStrip ("Input 1", { AudioEngine::Source::interfaceInputs, 0, {}, false });   // no sends: avoids mic feedback
    engine.addStrip ("Windows input", { AudioEngine::Source::device, 0, AudioEngine::windowsDefault, true });

    auto& tone = engine.addStrip ("Test tone", { AudioEngine::Source::tone, 0, {} });
    tone.mute = true;
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    if (midiMap != nullptr)
        midiMap->setBounds (area);
    if (consoleSidebar != nullptr)
        consoleSidebar->setBounds (area.removeFromLeft (ConsoleDesk::sidebarWidth));   // (the pages down the left)
    tabs.setBounds (area);
    toasts.setBounds (getLocalBounds());
    toasts.toFront (false);

    // In the empty end of the tab bar, to the pages' right edge: - 100% + | FIND.
    Console::KeyRow row (area.removeFromTop (tabs.getTabBarDepth()).reduced (pageEdge, 0));
    row.rightWidth (zoomIn, Console::Space::key).right (zoomReset).rightWidth (zoomOut, Console::Space::key);

    // The scene keys fill the gap between the last tab and FIND, a pad from each.
    if (sceneBar == nullptr)
        return;
    row.groupRight().right (findButton);

    // Then HISTORY, MINI and the health light, as many as fit before the tabs (a narrow window
    // drops the light first, then MINI, then HISTORY: FIND reaches them all).
    auto& tabBar = tabs.getTabbedButtonBar();
    int tabsEnd = row.rest().getX();
    if (auto* last = tabBar.getTabButton (tabBar.getNumTabs() - 1); last != nullptr && tabBar.isVisible())
        tabsEnd = tabBar.getX() + last->getRight() - PageTabs::capInsetX + Console::Space::pad;
    const int keyW = Console::Space::column + Console::Space::unit;
    for (auto [c, w] : { std::pair<juce::Component*, int> (&historyButton, keyW), std::pair<juce::Component*, int> (&miniButton, keyW),
                         std::pair<juce::Component*, int> (&health, 58 + Console::Space::unit) })
    {
        const bool fits = row.rest().getRight() - w >= tabsEnd;
        c->setVisible (fits);
        if (fits)
            row.rightWidth (*c, w - Console::Space::unit);
    }
    auto bar = row.rest();
    bar.setLeft (juce::jmin (bar.getRight(), tabsEnd));
    bar.removeFromRight (Console::Space::pad);
    sceneBar->setBounds (bar);
    sceneBar->setVisible (bar.getWidth() >= Console::Space::key);
}

// MINI: the mini mixer's channels (a session's Tray list, else the outputs) in a small window,
// where it was left; MINI again (or its close button) puts it away. PIN on it keeps it on top of
// everything (a game); OPEN brings the full window back.
void MainComponent::toggleMini()
{
    if (mini != nullptr && mini->isVisible())
    {
        mini = nullptr;
        settings.setValue ("miniShown", false);
        settings.saveIfNeeded();
        return;
    }
    showMini();
}

// The tray icon's click: the mini mixer up, or to the front. Never placed yet: by the tray (`near`).
void MainComponent::showMini (std::optional<juce::Point<int>> near)
{
    if (mini != nullptr && mini->isVisible())
    {
        mini->toFront (true);
        return;
    }
    struct MiniWindow : juce::DocumentWindow
    {
        MiniWindow (MainComponent& o, std::optional<juce::Point<int>> near)
            : juce::DocumentWindow (juce::String (Product::name) + " mini", Console::Colours::panel(), juce::DocumentWindow::closeButton), owner (o)
        {
            setUsingNativeTitleBar (true);
            const bool pinned = o.settings.getBoolValue ("miniPinned", true);
            setContentOwned (new TrayMixer (o.engine, [this]
            {
                if (auto* top = owner.getTopLevelComponent())
                {
                    top->setVisible (true);
                    if (auto* window = dynamic_cast<juce::DocumentWindow*> (top))
                        window->setMinimised (false);
                    top->toFront (true);
                }
            }, pinned, [this] (bool on)
            {
                setAlwaysOnTop (on);
                owner.settings.setValue ("miniPinned", on);
                owner.settings.saveIfNeeded();
            }), true);
            setAlwaysOnTop (pinned);
            setResizable (false, false);
            auto& displays = juce::Desktop::getInstance().getDisplays();
            const auto saved = juce::StringArray::fromTokens (o.settings.getValue ("miniPlace"), " ", {});
            if (saved.size() == 2 && displays.getTotalBounds (true).contains (juce::Point<int> (saved[0].getIntValue(), saved[1].getIntValue())))
                setTopLeftPosition (saved[0].getIntValue(), saved[1].getIntValue());
            else if (const auto* display = near.has_value() ? displays.getDisplayForPoint (*near) : nullptr)   // (above the tray, on its screen)
                setBounds (getBounds().withPosition (near->x - getWidth() / 2, near->y - getHeight() - Console::Space::gap).constrainedWithin (display->userArea));
            else
                centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::MessageManager::callAsync ([&o = owner] { o.toggleMini(); }); }
        void moved() override
        {
            DocumentWindow::moved();
            owner.settings.setValue ("miniPlace", juce::String (getX()) + " " + juce::String (getY()));
        }
        MainComponent& owner;
    };
    mini = std::make_unique<MiniWindow> (*this, near);
    settings.setValue ("miniShown", true);
    settings.saveIfNeeded();
}

void MainComponent::addSceneItems (juce::PopupMenu& menu)
{
    auto& scenes = engine.getScenes();
    if (scenes.list.empty())
        return;
    menu.addSectionHeader ("SCENE RECALL");
    for (int i = 0; i < (int) scenes.list.size(); ++i)
        menu.addItem (scenes.list[(size_t) i].name, true, i == scenes.active, [this, i] { engine.getScenes().recall (i); });
    menu.addSeparator();
}

int MainComponent::getNumRows()
{
    return endpoints.size();
}

void MainComponent::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, endpoints.size()))
        return;

    auto& e = endpoints.getReference (row);

    if (selected)
        g.fillAll (Console::Colours::panel().brighter (0.25f));

    // Off in Faider: shown in the state column, the row dimmed.
    const bool off = engine.isDeviceDisabled (e.name);
    const auto cells = deviceCells (e);

    // Fixed columns (shared with the header), each clipped with "..." if its text is too long.
    g.setColour (e.isActive() && ! off ? Console::Colours::legend() : Console::Colours::legendDim().withMultipliedAlpha (0.7f));
    g.setFont (juce::FontOptions (15.0f));
    DevicesTab::forEachColumn (width, [&] (int i, juce::Rectangle<int> r)
    {
        g.drawText (cells[i].toUpperCase(), r.withHeight (height), juce::Justification::centredLeft, true);
    });
}

// A device's row in the Devices list, one string per column (DevicesTab::columns).
juce::StringArray MainComponent::deviceCells (const EndpointInfo& e) const
{
    const auto kind = e.api == "ASIO" ? juce::String ("ASIO") : juce::String (e.isOutput ? "OUT" : "IN");
    const auto format = e.channels > 0 ? juce::String (e.channels) + "ch  " + juce::String (e.sampleRate) + " Hz" : juce::String();
    const auto label = e.api == "ASIO" ? (deviceAlias (e.name).isNotEmpty() ? deviceAlias (e.name) : e.name) : endpointDisplayName (e);
    const auto defaults = juce::String (e.isDefault ? "  DEFAULT" : "") + (e.isDefaultComms ? "  CALLS" : "");
    return { kind, label, e.adapter, format, engine.isDeviceDisabled (e.name) ? juce::String ("off in ") + Product::name : endpointStateName (e.state),
             classifyEndpoint (e).joinIntoString (",") + defaults };
}

// Orders the Devices list by the chosen column (numbers in number order, ties by name),
// keeping the same devices selected.
void MainComponent::sortDevices()
{
    sortDevices (selectedDeviceIds());
}

juce::StringArray MainComponent::selectedDeviceIds() const
{
    juce::StringArray chosen;
    for (int i = 0; i < deviceList.getNumSelectedRows(); ++i)
        if (juce::isPositiveAndBelow (deviceList.getSelectedRow (i), endpoints.size()))
            chosen.add (endpoints[deviceList.getSelectedRow (i)].id);
    return chosen;
}

// `chosen`: the devices (ids) to have selected afterwards, taken before `endpoints` changed.
void MainComponent::sortDevices (const juce::StringArray& chosen)
{
    if (sortColumn >= 0)
        std::stable_sort (endpoints.begin(), endpoints.end(), [this] (const EndpointInfo& a, const EndpointInfo& b)
        {
            const auto ca = deviceCells (a), cb = deviceCells (b);
            int order = ca[sortColumn].compareNatural (cb[sortColumn]);
            if (order == 0)
                order = ca[1].compareNatural (cb[1]);
            return sortAscending ? order < 0 : order > 0;
        });

    deviceList.updateContent();
    deviceList.deselectAllRows();
    for (int i = 0; i < endpoints.size(); ++i)
        if (chosen.contains (endpoints[i].id))
            deviceList.selectRow (i, true, false);
    deviceList.repaint();
}

void MainComponent::selectedRowsChanged (int)
{
    devicesPanel.getTools().selectionChanged();
}

void MainComponent::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    deviceList.selectRow (row);
    devicesPanel.getTools().renameSelected();
}

void MainComponent::endpointsChanged (const juce::Array<EndpointInfo>& now, const juce::StringArray& events)
{
    const auto chosen = selectedDeviceIds();   // (by the rows as they were, before they change)
    endpoints = now;
    sortDevices (chosen);

    for (auto& event : events)
    {
        logEvent (event);
        // A notice for each: plugged in, gone, changed state, or Windows' default changed.
        if (juce::Time::getMillisecondCounter() - openedAt < 8000)
            continue;
        if (event.startsWith ("added: "))
            toasts.show ("device", event.fromFirstOccurrenceOf (": ", false, false), "Connected");
        else if (event.startsWith ("removed: "))
            toasts.show ("device", event.fromFirstOccurrenceOf (": ", false, false), "Disconnected");
        else if (event.startsWith ("Windows default "))
            toasts.show ("device", event.upToFirstOccurrenceOf (": ", false, false), event.fromFirstOccurrenceOf (": ", false, false));
        else if (event.contains (" -> "))
            toasts.show ("device", event.upToLastOccurrenceOf (": ", false, false),
                         event.fromLastOccurrenceOf (": ", false, false).replace (" -> ", juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 "))));
    }

    engine.updateEndpoints (now);
    clockRefused.clear();   // (the devices changed: one that wouldn't start may now)
    checkClock();   // (the device clocking it gone, or one to take over from the built-in clock)

    // Windows' default speaker or mic changed (an installer, SteamVR, the user): say if that now
    // sends sound somewhere it shouldn't (AudioEngine::findDoubling).
    if (std::any_of (events.begin(), events.end(), [] (const juce::String& e) { return e.startsWith ("Windows default"); }))
        MixerComponent::checkSetupSoon (engine);

    // Device profiles: a scene set to follow a device comes in with it (and goes back after).
    juce::StringArray active;
    for (auto& e : now)
        if (e.api == "WASAPI" && e.isActive())
            active.addIfNotAlreadyThere (e.name);
    if (const auto happened = engine.getScenes().devicesChanged (active); happened.isNotEmpty())
        logEvent (happened);
    connectedDevices = active;   // for macro triggers
}

// Once a second: outputs with ALERTS set that have gone silent (under -60 dBFS for their set
// time) or clipped (LIMIT off) are logged once and held in `alert` for the tray icon; a silent
// one clears when its sound comes back, a clip after 10 s.
void MainComponent::checkAlerts()
{
    juce::StringArray now;
    std::map<juce::String, int> silent;
    for (auto& bus : engine.getMasters())
    {
        const bool clipped = bus->clipped.exchange (false);
        if (! bus->enabled || bus->target.isEmpty())
            continue;
        const auto& name = bus->name;

        if (bus->silenceAlert > 0)
        {
            const bool quiet = juce::jmax (bus->peakL.load(), bus->peakR.load()) < juce::Decibels::decibelsToGain (-60.0f);
            const int seconds = quiet ? silentFor[name] + 1 : 0;
            silent[name] = seconds;
            if (seconds == bus->silenceAlert)
                logEvent ("alert: " + name + " silent for " + juce::String (seconds) + " s");
            else if (seconds == 0 && silentFor[name] >= bus->silenceAlert)
                logEvent ("alert: " + name + " has sound again");
            if (seconds >= bus->silenceAlert)
                now.add (name + " SILENT");
        }

        if (bus->clipAlert && ! bus->safeOn.load())
        {
            if (clipped)
            {
                if (clipShown[name] <= 0)
                    logEvent ("alert: " + name + " clipping");
                clipShown[name] = 10;
            }
            else if (clipShown[name] > 0)
                --clipShown[name];
            if (clipShown[name] > 0)
                now.add (name + " CLIPPING");
        }
    }
    silentFor = std::move (silent);

    // A recording that ran out of room (drive nearly full, or its format's size limit): stopped
    // with its file finished; the tray says so for a minute.
    if (const auto stopped = engine.stopRecordingIfFull(); stopped.isNotEmpty())
    {
        logEvent ("alert: " + stopped);
        recordStoppedShown = 60;
    }
    if (recordStoppedShown > 0)
    {
        --recordStoppedShown;
        now.add ("RECORDING STOPPED, NO ROOM");
    }

    // The master stopped (unplugged, its driver taken or failed): nothing plays until it's back.
    if (engine.isInterfaceDown())
        now.insert (0, "INTERFACE DOWN");
    alert = now.joinIntoString (", ");
}

void MainComponent::timerCallback()
{
    // Everything here runs on the message thread, so it's spread out: a tick that costs more
    // than a frame shows as a hitch (most visibly while the window is being dragged).
    ++tick;
    checkClock();   // (also a clock device switched off in Faider, or closed by Windows)
    engine.refreshLatency();
    engine.refreshDucking();   // follows strips being moved, removed or renamed
    checkAlerts();

    // Macro triggers (apps looked up only while some macro has one).
    auto& macros = engine.getMacros();
    const bool macroTriggers = std::any_of (macros.list.begin(), macros.list.end(), [] (auto& m) { return ! m.triggers.empty(); });
    if (macroTriggers || engine.getScenes().hasTriggers())
    {
        juce::StringArray apps;
        for (auto& a : listAudioApps())
            apps.addIfNotAlreadyThere (a.exe, true);
        if (macroTriggers)
            for (auto& fired : macros.checkTriggers (apps, connectedDevices, juce::Time::getCurrentTime()))
                logEvent ("trigger: " + fired);
        for (auto& fired : engine.getScenes().checkTriggers (apps, connectedDevices, juce::Time::getCurrentTime()))
            logEvent ("auto-scene: " + fired);   // (a new scene trigger is armed only from the look after it's set)
    }

    if (tick % 3 == 0)
    {
        engine.refreshApps();   // notices an app starting or closing (picking one in the mixer is immediate)
        const auto added = engine.applyAppRules();
        for (auto& a : added)
            logEvent ("app rule: " + a);
    }

    if (tick % 5 == 3)   // a diagnostic snapshot (it includes live levels, so it always differs)
        statusFile.replaceWithText (juce::Time::getCurrentTime().toString (true, true) + "\n" + engine.getStatusText());

    if (tick % 5 == 1)
        saveSession();   // also on exit

    // App preferences chosen elsewhere (meter standard in the Mixer, buffer in Devices): remember them.
    if (AudioEngine::meterPoint.load() != settings.getIntValue ("meterPoint", 2))
    {
        settings.setValue ("meterPoint", AudioEngine::meterPoint.load());
        settings.saveIfNeeded();
    }
    if (const auto id = Metering::standard().id; id != settings.getValue ("meterStandard"))
    {
        settings.setValue ("meterStandard", id);
        settings.saveIfNeeded();
    }
    if (const int size = engine.getInterfaceBufferSize(); size > 0 && size != settings.getIntValue ("bufferSize"))
    {
        settings.setValue ("bufferSize", size);
        settings.saveIfNeeded();
    }
}

// Writes only when something changed, so this is cheap to call every second.
void MainComponent::saveSession()
{
    // Only a mix that was restored or deliberately made at start is written: anything else (no
    // master, or the master wouldn't open) would save an empty mix over the real one.
    if (interfaceName.isEmpty() || ! sessionLoaded)
        return;

    const auto xml = Session::save (engine, masterToSave())->toString();

    if (xml != lastSavedSession)
    {
        // Written whole to a file beside it and flushed to the disk before it takes the session's
        // place, so a power cut leaves the old session or the new one, never an empty file.
        const auto temp = sessionFile.getSiblingFile ("session.xml.tmp");
        {
            juce::FileOutputStream out (temp);
            if (! out.openedOk())
                return;
            out.setPosition (0);
            out.truncate();
            out.writeText (xml, false, false, nullptr);
            out.flush();   // (FlushFileBuffers on Windows)
            if (! out.getStatus().wasOk())
                return;
        }
        if (! temp.replaceFileIn (sessionFile))
            return;
        lastSavedSession = xml;
        keepHistory (xml);
    }
}

// Session safety: earlier versions in %APPDATA%\Faider\history. The first save of a run is
// kept too, so the mix as it was at start can always come back.
void MainComponent::keepHistory (const juce::String& xml)
{
    const auto now = juce::Time::getCurrentTime();
    if (lastHistory != juce::Time() && now - lastHistory < juce::RelativeTime::minutes (5))
        return;
    lastHistory = now;

    const auto dir = dataDir.getChildFile ("history");
    dir.createDirectory();
    dir.getChildFile ("session-" + now.formatted ("%Y-%m-%d_%H-%M-%S") + ".xml").replaceWithText (xml);

    const auto files = historyFiles();
    for (int i = 60; i < files.size(); ++i)
        files[i].deleteFile();
}

juce::Array<juce::File> MainComponent::historyFiles() const
{
    auto files = dataDir.getChildFile ("history").findChildFiles (juce::File::findFiles, false, "session-*.xml");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
    return files;
}

// Mixes set aside at start (the master missing or wouldn't open): beside session.xml, outside
// history/, so the history's pruning never takes them; listed after the history as "kept: ...".
juce::Array<juce::File> MainComponent::setAsideFiles() const
{
    auto files = dataDir.findChildFiles (juce::File::findFiles, false, "session-unloaded-*.xml");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
    return files;
}

void MainComponent::addSessionItems (juce::PopupMenu& menu)
{
    menu.addItem ("Session timeline...", [this] { showTimeline (juce::PopupMenu::Options().withMousePosition()); });
}

void MainComponent::addUpdateItems (juce::PopupMenu& menu)
{
    if (newVersion.isNotEmpty())
        menu.addItem ("Get " + juce::String (Product::name) + " " + newVersion + "...", [this] { openRelease(); });
}

// CHECK FOR UPDATES: the request on its own thread (UpdateCheck.h); the time is kept, so the
// check on start comes at most once a day.
void MainComponent::checkForUpdates (bool asked)
{
    if (updates == nullptr)
        updates = std::make_unique<UpdateCheck::Checker>();
    if (updates->isChecking())
        return;
    settings.setValue ("lastUpdateCheck", juce::String (juce::Time::currentTimeMillis()));
    settings.saveIfNeeded();
    updates->start (Product::name, JUCE_APPLICATION_VERSION_STRING, [safe = juce::Component::SafePointer<MainComponent> (this), asked] (UpdateCheck::Result r)
    {
        if (safe != nullptr)
            safe->updateChecked (r, asked);
    });
}

// What the check found: a failure is only logged (said too when it was asked for); a newer
// version is noticed from the tray and offered on the tray menu, in FIND and in SETTINGS.
void MainComponent::updateChecked (const UpdateCheck::Result& r, bool asked)
{
    const juce::String current (JUCE_APPLICATION_VERSION_STRING);
    if (! r.ok)
    {
        logEvent ("update check: " + r.error);
        if (asked)
            toasts.show ("update", "Couldn't check for updates", r.error);
        return;
    }
    if (! UpdateCheck::isNewer (r.version, current))
    {
        logEvent ("update check: " + current + " is the newest");
        if (asked)
            toasts.show ("update", "Up to date", juce::String (Product::name) + " " + current + " is the newest version");
        return;
    }
    newVersion = r.version;
    newVersionUrl = r.url;
    const auto title = juce::String (Product::name) + " " + newVersion + " is out";
    logEvent ("update check: " + title + " (" + newVersionUrl + ")");
    if (onNotify)
        onNotify (title, "You have " + current + ". Right-click the tray icon > Get " + Product::name + " " + newVersion + "...");
    if (asked)
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::NoIcon, title.toUpperCase(), "You have " + current + ". Open its release page?",
                                            "OPEN THE PAGE", "LATER", nullptr,
                                            juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                                            { if (result == 1 && safe != nullptr) safe->openRelease(); }));
    else
        toasts.show ("update", title, "FIND: GET " + juce::String (Product::nameUpper) + " " + newVersion);
}

void MainComponent::openRelease()
{
    if (newVersionUrl.startsWith ("https://github.com/"))
        juce::URL (newVersionUrl).launchInDefaultBrowser();
}

// SESSION TIMELINE: every kept copy of the session, newest first. Each brings back the whole mix
// (Faider restarts), or one channel just as it was then (its level, pan, mute and rack). From
// FIND, or the tray menu (where the window may be hidden: at the mouse).
void MainComponent::showTimeline (juce::PopupMenu::Options where)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("SESSION TIMELINE");
    const auto now = juce::Time::getCurrentTime();
    auto files = historyFiles();
    const int historyCount = files.size();
    files.addArray (setAsideFiles());   // the set-aside mixes after the history, as "kept: ..."
    for (int i = 0; i < files.size(); ++i)
    {
        const auto& f = files.getReference (i);
        const auto when = f.getLastModificationTime();
        const auto ago = now - when;
        const auto label = juce::String (i >= historyCount ? "kept: " : "")
                         + when.formatted ("%H:%M  %a %d %b") + "   (" + (ago.inMinutes() < 60 ? juce::String ((int) ago.inMinutes()) + " min ago"
                                                                                                   : juce::String (ago.inHours(), 1) + " h ago") + ")";
        juce::PopupMenu version;
        version.addItem ("RESTORE EVERYTHING (RESTARTS)", [this, f] { restoreSession (f); });
        version.addSeparator();
        version.addSectionHeader ("PUT ONE CHANNEL BACK");
        if (const auto xml = juce::XmlDocument::parse (f))
        {
            for (auto* tag : { "Strip", "Pair" })
                for (auto* e : xml->getChildWithTagNameIterator (tag))
                {
                    const auto key = juce::String (juce::String (tag) == "Pair" ? "bus:" : "input:") + e->getStringAttribute ("name");
                    version.addItem (e->getStringAttribute ("name"), [this, key, saved = std::make_shared<juce::XmlElement> (*e), at = when.formatted ("%H:%M")]
                    {
                        if (mixer->revertChannel (key, *saved))
                            logEvent ("timeline: " + key.fromFirstOccurrenceOf (":", false, false) + " put back as it was at " + at);
                        else
                            logEvent ("timeline: couldn't put " + key.fromFirstOccurrenceOf (":", false, false) + " back (gone, or a plugin is locked)");
                    });
                }
        }
        menu.addSubMenu (label, version);
    }
    if (menu.getNumItems() <= 1)
        menu.addItem ("NOTHING KEPT YET", false, false, [] {});
    menu.showMenuAsync (where);
}

//==============================================================================
// Named sessions (%APPDATA%\Faider\sessions\<name>.xml): the mix saved under a name to come back
// to, and sessions as files to share or move (EXPORT, IMPORT). Opening one restarts Faider with it.
juce::File MainComponent::sessionsDir() const { return dataDir.getChildFile ("sessions"); }

void MainComponent::saveSessionAs()
{
    if (interfaceName.isEmpty() || ! sessionLoaded)
        return logEvent ("sessions: nothing to save yet (no mix loaded)");
    auto* box = new juce::AlertWindow ("SAVE SESSION AS", "A name for this mix:", juce::MessageBoxIconType::NoIcon);
    box->addTextEditor ("name", "");
    box->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    box->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    box->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this), box] (int result)
    {
        const auto name = juce::File::createLegalFileName (box->getTextEditorContents ("name").trim());
        if (result != 1 || safe == nullptr || name.isEmpty())
            return;
        safe->sessionsDir().createDirectory();
        const auto file = safe->sessionsDir().getChildFile (name + ".xml");
        if (file.replaceWithText (Session::save (safe->engine, safe->masterToSave())->toString()))
        {
            safe->logEvent ("sessions: saved as " + name);
            safe->toasts.show ("scene", "Session saved", name);
        }
    }), true);
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

void MainComponent::showSessions (juce::PopupMenu::Options where)
{
    juce::PopupMenu menu;
    menu.addItem ("SAVE AS...", [this] { saveSessionAs(); });
    menu.addSectionHeader (juce::String ("OPEN (") + Product::nameUpper + " RESTARTS)");
    auto files = sessionsDir().findChildFiles (juce::File::findFiles, false, "*.xml");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    for (auto& f : files)
        menu.addItem (f.getFileNameWithoutExtension(), [this, f] { restoreSession (f, f.getFileNameWithoutExtension()); });
    if (files.isEmpty())
        menu.addItem ("NO SAVED SESSIONS YET", false, false, [] {});
    menu.addSeparator();
    menu.addItem ("EXPORT TO A FILE...", [this] { exportSession(); });
    menu.addItem ("IMPORT FROM A FILE...", [this] { importSession(); });
    menu.showMenuAsync (where);
}

void MainComponent::exportSession()
{
    if (interfaceName.isEmpty() || ! sessionLoaded)
        return;
    sessionChooser = std::make_unique<juce::FileChooser> ("Export the session", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                                    .getChildFile ("Faider session.xml"), "*.xml");
    sessionChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult().withFileExtension ("xml");
        if (fc.getResult() != juce::File() && file.replaceWithText (Session::save (engine, masterToSave())->toString()))
            logEvent ("sessions: exported to " + file.getFullPathName());
    });
}

void MainComponent::importSession()
{
    sessionChooser = std::make_unique<juce::FileChooser> ("Import a session", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.xml");
    sessionChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        const auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || xml->getChildByName ("Strip") == nullptr && xml->getChildByName ("Bus") == nullptr && xml->getChildByName ("Pair") == nullptr)
        {
            if (file != juce::File())
                logEvent ("sessions: " + file.getFileName() + " isn't a Faider session");
            return;
        }
        restoreSession (file, file.getFileNameWithoutExtension());
    });
}

// Puts an earlier session back: it becomes session.xml and Faider restarts with it.
void MainComponent::restoreSession (const juce::File& version, const juce::String& named)
{
    const auto when = version.getLastModificationTime().formatted ("%a %d %b %H:%M");
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::NoIcon, named.isNotEmpty() ? "OPEN SESSION" : "RESTORE SESSION",
                                        (named.isNotEmpty() ? "Open the session \"" + named + "\"?" : "Restore the mix from " + when + "?") + "\nFaider restarts.",
                                        named.isNotEmpty() ? "OPEN" : "RESTORE", "CANCEL", nullptr,
                                        juce::ModalCallbackFunction::create ([this, version] (int result)
    {
        if (result != 1 || ! version.existsAsFile())
            return;

        interfaceName.clear();   // nothing saves over it on the way out
        version.copyFileTo (sessionFile);

        // Start again once this copy has gone (only one Faider runs at a time): the new copy
        // waits for this one's process to end (--after), however long closing takes.
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
        juce::ChildProcess relaunch;
        relaunch.start ("\"" + exe + "\" --after " + juce::String (thisProcessId()), 0);
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }));
}

// SAVE A PROBLEM REPORT: what's there of the event log, the status snapshot and the last crash,
// with the versions, zipped where the user says (the desktop to start). Only saved, never sent.
void MainComponent::saveProblemReport()
{
    const auto name = juce::String (Product::name) + " problem report " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d") + ".zip";
    chooser = std::make_unique<juce::FileChooser> ("Save a problem report", juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (name), "*.zip");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult() == juce::File())
            return;
        const auto file = fc.getResult().withFileExtension ("zip");

        juce::ZipFile::Builder zip;
        juce::StringArray included;
        for (auto* n : { "events.log", "events.old.log", "status.txt", "crash.log", "crash.old.log", "crash.dmp" })
            if (const auto f = dataDir.getChildFile (n); f.existsAsFile())
            {
                zip.addFile (f, 9, n);
                included.add (n);
            }
        const auto about = juce::String (Product::name) + " " + JUCE_APPLICATION_VERSION_STRING + "\n"
                         + juce::SystemStats::getOperatingSystemName() + (juce::SystemStats::isOperatingSystem64Bit() ? " (64-bit)" : "") + "\n"
                         + "Made " + juce::Time::getCurrentTime().toString (true, true) + "\n";
        zip.addEntry (new juce::MemoryInputStream (about.toRawUTF8(), about.getNumBytesAsUTF8(), true), 9, "versions.txt", juce::Time::getCurrentTime());

        bool written = false;
        {
            file.deleteFile();
            juce::FileOutputStream out (file);
            written = out.openedOk() && zip.writeToStream (out, nullptr);
        }
        if (! written)
        {
            logEvent ("problem report: couldn't write " + file.getFullPathName());
            toasts.show ("device", "Problem report", "Couldn't write " + file.getFileName());
            return;
        }
        logEvent ("problem report: saved as " + file.getFullPathName() + " (" + (included.isEmpty() ? juce::String ("versions only") : included.joinIntoString (", ") + ", versions") + ")");
        file.revealToUser();
    });
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

// As RESTORE does, with the mix as it is: it's saved on the way out, and the new copy waits for
// this one's process to end (--after).
void MainComponent::restartFaider()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
    juce::ChildProcess relaunch;
    relaunch.start ("\"" + exe + "\" --after " + juce::String (thisProcessId()), 0);
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void MainComponent::logEvent (const juce::String& event)
{
    const auto line = juce::Time::getCurrentTime().toString (true, true) + "  " + event + "\n";

    // Whether someone is reading back is told by where the view is scrolled, not by the caret: a
    // wheel scroll moves the view and leaves the caret at the end. (The view is the editor's own
    // Viewport child.)
    juce::Viewport* view = nullptr;
    for (int i = 0; i < eventLog.getNumChildComponents() && view == nullptr; ++i)
        view = dynamic_cast<juce::Viewport*> (eventLog.getChildComponent (i));
    auto contentHeight = [view]
    {
        auto* content = view != nullptr ? view->getViewedComponent() : nullptr;
        return content != nullptr ? content->getHeight() : 0;
    };
    const bool following = view == nullptr || view->getViewPositionY() + view->getViewHeight() >= contentHeight() - 2;
    int viewY = view != nullptr ? view->getViewPositionY() : 0;

    // The newest lines kept on screen (the oldest dropped past about 300,000 characters). Someone
    // reading back keeps their place: the view moves up by however much was taken off the top.
    if (eventLog.getTotalNumChars() > 300000)
    {
        const int before = contentHeight();
        const auto text = eventLog.getText();
        eventLog.setText (text.substring (text.indexOf (100000, "\n") + 1), false);
        viewY = juce::jmax (0, viewY - (before - contentHeight()));
    }

    // Added at the end; the view goes back where it was if someone had scrolled up to read.
    eventLog.moveCaretToEnd();
    eventLog.insertTextAtCaret (line);
    if (! following && view != nullptr)
        view->setViewPosition (view->getViewPositionX(), viewY);

    // events.log: past 5 MB it starts again, the last one kept as events.old.log.
    if (eventFile.getSize() > 5 * 1024 * 1024)
        eventFile.moveFileTo (eventFile.getSiblingFile ("events.old.log"));
    eventFile.appendText (line);
}

// A page's tip, the first time it's seen (remembered as seen).
void MainComponent::showTip (const juce::String& page)
{
    static const std::pair<const char*, const char*> tips[] {
        { "Mixer",    "SIMPLE / PRO above the strips shows the same mix either way. Left/right selects a channel, M cuts it, S solos it, up/down moves its fader." },
        { "Routing",  "Click one block, then another, to route between them. Click again, or Esc, to let go." },
        { "Tools",    "Pads play into the PADS channel. Give pads, macros and scenes hotkeys in SETTINGS > HOTKEYS." },
        { "Devices",  "Each device's latency and drops are listed under STREAMS; the light in the top bar sums them up." },
        { "Settings", "FIND (Ctrl+K) reaches every setting and action by name. HISTORY (Ctrl+H) goes back to any step." },
    };
    if (! settings.getBoolValue ("tips", true) || settings.getBoolValue ("tipSeen_" + page))
        return;
    for (auto& [name, text] : tips)
        if (page == name)
        {
            toasts.show ("tip", "Tip: " + page, text);
            settings.setValue ("tipSeen_" + page, true);
            settings.saveIfNeeded();
        }
}

// PRE-FLIGHT's checks, as things are now (Preflight.h).
std::vector<PreflightPanel::Check> MainComponent::preflightChecks()
{
    std::vector<PreflightPanel::Check> checks;
    const bool audio = engine.getSampleRate() > 0.0;
    checks.push_back ({ "Audio", audio ? "Running at " + juce::String (engine.getSampleRate() / 1000.0, 1) + " kHz on " + engine.getClockName()
                                                      + (engine.getClockDevice().isNotEmpty() ? " (" + engine.getClockDevice() + ")" : juce::String())
                                       : "It didn't start: see DEVICES (another app may have the interface)",
                        audio, "DEVICES", [this] { tabs.setCurrentTabIndex (tabs.getTabNames().indexOf ("Devices")); } });

    const auto& allEndpoints = engine.getEndpoints();
    const bool cable = std::any_of (allEndpoints.begin(), allEndpoints.end(), [&allEndpoints] (const EndpointInfo& e)
                                    { return e.api == "WASAPI" && e.isOutput && virtualMicPartner (e, allEndpoints).isNotEmpty(); });
    checks.push_back ({ "Mic cable", cable ? "A virtual mic cable is installed (apps hear it as a mic)"
                                           : "No virtual mic cable (VB-CABLE) found: apps can't hear your mix as a mic",
                        cable, "GET VB-CABLE", [] { juce::URL ("https://vb-audio.com/Cable/").launchInDefaultBrowser(); } });

    const auto mic = micOutputName();
    int micBus = -1;
    for (int b = 0; b < (int) engine.getMasters().size(); ++b)
        if (engine.getMasters()[(size_t) b]->name == mic)
            micBus = b;
    checks.push_back ({ "Mic output", mic.isNotEmpty() ? mic + " plays into the mic cable" : "No output plays into the mic cable (CABLE Input)",
                        mic.isNotEmpty(), "ROUTING", [this] { tabs.setCurrentTabIndex (tabs.getTabNames().indexOf ("Routing")); } });

    // The mics on their way there, and whether any is muted.
    juce::StringArray muted;
    std::vector<AudioEngine::Strip*> mutedStrips;
    if (micBus >= 0)
        for (auto& s : engine.getStrips())
            if (s->enabled && (s->source.kind == AudioEngine::Source::interfaceInputs || s->source.kind == AudioEngine::Source::device)
                && engine.feeds (*s, micBus) && s->mute.load())
            {
                muted.add (s->name);
                mutedStrips.push_back (s.get());
            }
    checks.push_back ({ "Mic channels", muted.isEmpty() ? "Nothing on the way to the mic is cut" : muted.joinIntoString (", ") + " cut: no one hears you",
                        muted.isEmpty(), "UNCUT", [this, names = muted]
                        {
                            for (auto& s : engine.getStrips())
                                if (names.contains (s->name))
                                    s->mute = false;
                        } });

    // Windows' default speaker or mic (or the ones for calls) on the cable: a VB-CABLE install does that.
    const auto onCable = WindowsDefaults::onCable (allEndpoints);
    checks.push_back ({ "Windows defaults", onCable.isEmpty() ? "Windows plays and records on real devices, not the cable"
                                                              : "Windows' " + onCable + ". Pick your real ones",
                        onCable.isEmpty(), "SET MY DEFAULTS", [this] { showWindowsDefaults (juce::PopupMenu::Options().withMousePosition()); } });

    auto problems = engine.findDoubling();
    for (int i = problems.size(); --i >= 0;)   // (the cable as the default speaker: said above)
        if (problems[i].startsWith ("Windows' default speaker is"))
            problems.remove (i);
    if (const auto loop = engine.findFeedbackLoop(); loop.isNotEmpty())
        problems.insert (0, "Feedback loop: " + loop);
    checks.push_back ({ "Heard once", problems.isEmpty() ? "Nothing plays twice, nothing loops back" : problems.joinIntoString (" "),
                        problems.isEmpty(), "SOUND SETTINGS", [] { juce::URL ("ms-settings:sound").launchInDefaultBrowser(); } });
    return checks;
}

// SET MY DEFAULTS (WindowsDefaults.h): a real speaker and mic as Windows' defaults, for calls too.
void MainComponent::showWindowsDefaults (juce::PopupMenu::Options where)
{
    WindowsDefaults::showMenu (engine.getEndpoints(), where, [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::String& e)
                               { if (safe != nullptr) safe->logEvent (e); });
}

void MainComponent::showPreflight (bool onlyIfWrong)
{
    auto panel = std::make_unique<PreflightPanel> ([this] { return preflightChecks(); });
    if (onlyIfWrong && panel->allClear())
        return;
    panel->setOtherKey ("ZERO THE DESK", "Things left set: solos, key listen, gains far off, outputs cut", [this] { showZero(); });
    logEvent (onlyIfWrong ? "pre-flight: something needs a look" : "pre-flight");
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (panel.release());
    o.dialogTitle = "Pre-flight";
    o.dialogBackgroundColour = Console::Colours::panel();
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = this;
    o.launchAsync();
}

// SETUP: its own window, not a page. It asks its questions and adds what's missing; the desk goes on
// running behind it, and a window closed without pressing BUILD leaves the mix as it was.
void MainComponent::showSetup ()
{
    if (setupWindow != nullptr)
    {
        setupWindow->toFront (true);
        return;
    }

    auto panel = Simple::makeSetup (engine, [this]
    {
        settings.setValue ("deskMode", "simple");
        settings.saveIfNeeded();
        ConsoleDesk::setSimple (*consoleDesk, true);
        tabs.setCurrentTabIndex (0);
        closeSetup();
    });

    panel->setSize (940, 620);   // (as a page it was sized by the tabs; a window asks its content)
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (panel.release());
    o.dialogTitle = "Set up the mix";
    o.dialogBackgroundColour = Console::Colours::panel();
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    setupWindow = o.launchAsync();
}

// Dismissed a moment later than it is asked: the click that ends SETUP is still on the stack, and the
// window's going takes the page it holds with it.
void MainComponent::closeSetup ()
{
    if (setupWindow != nullptr)
        juce::Timer::callAfterDelay (50, [w = juce::Component::SafePointer<juce::DialogWindow> (setupWindow.getComponent())]
                                    { if (w != nullptr) w->closeButtonPressed(); });
}

// ZERO DESK: as engineers zero a console before a session, the things a last session (or a
// moment ago) can leave set that change what's heard, each with its fix.
std::vector<PreflightPanel::Check> MainComponent::zeroChecks()
{
    std::vector<PreflightPanel::Check> checks;
    auto namesWhere = [this] (std::function<bool (AudioEngine::Strip&)> test)
    {
        juce::StringArray names;
        for (auto& s : engine.getStrips())
            if (test (*s))
                names.add (s->name);
        return names;
    };
    auto row = [&checks] (const juce::String& title, const juce::StringArray& found, const juce::String& foundText, const juce::String& clearText, std::function<void()> fix)
    {
        checks.push_back ({ title, found.isEmpty() ? clearText : found.joinIntoString (", ") + ": " + foundText, found.isEmpty(), "FIX", std::move (fix) });
    };

    row ("Solos", namesWhere ([] (AudioEngine::Strip& s) { return s.solo.load(); }), "soloed: the others are silent", "Nothing soloed",
         [this] { for (auto& s : engine.getStrips()) s->solo = false; });
    row ("Key listen", namesWhere ([] (AudioEngine::Strip& s) { return s.channel != nullptr && s.channel->params.getRawParameterValue ("keyListen")->load() > 0.5f; }),
         "playing its sidechain key instead of itself", "No channel plays its key",
         [this] { for (auto& s : engine.getStrips()) if (s->channel != nullptr) if (auto* p = s->channel->params.getParameter ("keyListen")) p->setValueNotifyingHost (0.0f); });
    row ("Gain trims", namesWhere ([] (AudioEngine::Strip& s) { return s.channel != nullptr && std::abs (s.channel->params.getRawParameterValue ("trim")->load()) > 12.0f; }),
         "more than 12 dB off (left there?)", "Every channel's gain near 0",
         [this] { for (auto& s : engine.getStrips()) if (s->channel != nullptr && std::abs (s->channel->params.getRawParameterValue ("trim")->load()) > 12.0f) if (auto* p = s->channel->params.getParameter ("trim")) p->setValueNotifyingHost (p->convertTo0to1 (0.0f)); });
    juce::StringArray cutMasters;
    for (auto& m : engine.getMasters())
        if (m->mute.load())
            cutMasters.add (m->name);
    row ("Outputs", cutMasters, "cut: nothing leaves them", "No output cut", [this] { for (auto& m : engine.getMasters()) m->mute = false; });
    return checks;
}

void MainComponent::showZero()
{
    auto panel = std::make_unique<PreflightPanel> ([this] { return zeroChecks(); }, "ZERO DESK");
    logEvent ("zero desk");
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (panel.release());
    o.dialogTitle = "Zero desk";
    o.dialogBackgroundColour = Console::Colours::panel();
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = this;
    o.launchAsync();
}

juce::String MainComponent::micOutputName() const
{
    for (auto& b : engine.getMasters())   // the output playing into VB-CABLE: what apps hear as the mic
        if (b->target.containsIgnoreCase ("CABLE Input"))
            return b->name;
    return {};
}

void MainComponent::hearWhatTheyHear()
{
    const auto name = micOutputName();
    if (name.isEmpty() || engine.isPreviewPlaying())
        return;
    logEvent ("hear what they hear: speak now (5 s), then listen on the headphones");
    if (onNotify)
        onNotify ("Speak now", "Talk for 5 s, then hear it back.");
    juce::Timer::callAfterDelay (5200, [safe = juce::Component::SafePointer<MainComponent> (this), name]
    {
        if (safe == nullptr)
            return;
        auto clip = safe->engine.retroTail ("output", name, 5.0);
        if (clip.getNumSamples() == 0)
            return safe->logEvent ("hear what they hear: nothing came through " + name);
        safe->logEvent ("hear what they hear: playing " + name + " back");
        safe->engine.playPreview (std::move (clip));
    });
}

void MainComponent::toggleOverlay()
{
    if (overlay == nullptr)
        return;
    const bool on = ! overlay->isShowing();
    const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
    const float zoomNow = juce::Desktop::getInstance().getGlobalScaleFactor();
    juce::Point<int> where (area.getRight() - 260, area.getY() + 20);
    if (settings.containsKey ("overlayPx"))
        where = { juce::roundToInt ((float) settings.getIntValue ("overlayPx") / zoomNow), juce::roundToInt ((float) settings.getIntValue ("overlayPy") / zoomNow) };
    else if (settings.containsKey ("overlayX"))   // (saved before: at the zoom of the time)
        where = { settings.getIntValue ("overlayX"), settings.getIntValue ("overlayY") };
    overlay->show (on, area.contains (where) ? where : area.getTopRight().translated (-260, 20));
    settings.setValue ("overlayShown", on);
    settings.saveIfNeeded();
}

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include "devices/EndpointWatcher.h"
#include "engine/AudioEngine.h"
#include "ui/MixerComponent.h"
#include "ui/DevicesTab.h"
#include "ui/PadsPanel.h"
#include "ui/TapePanel.h"
#include "ui/ToolsTab.h"
#include "ui/Overlay.h"
#include "ui/MidiMap.h"
#include "ui/MidiPanel.h"
#include "ui/SceneBar.h"

#include "engine/MixHistory.h"
#include "ui/TrayMixer.h"
#include "ui/Console.h"
#include "ui/ControlWindow.h"
#include "ui/SettingsTab.h"
#include "ui/Toasts.h"
#include "ui/HealthLight.h"
#include "ui/Preflight.h"
#include "ui/CommandPalette.h"
#include "UpdateCheck.h"

class MainComponent : public juce::Component,
                      private juce::ListBoxModel,
                      private EndpointWatcher::Listener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;   // Ctrl +/-/0 zoom (the window forwards keys nobody used)

    // The scenes, for the tray menu: pick one to recall it.
    void addSceneItems (juce::PopupMenu&);

    // Earlier sessions (session safety), for the tray menu: "Session timeline...", the SESSION TIMELINE.
    void addSessionItems (juce::PopupMenu&);

    // The mini mixer up (or to the front), from the tray icon at `near` (a screen point): where it
    // was left, or by the tray the first time.
    void showMini (std::optional<juce::Point<int>> near = {});


    // The height that shows the desk whole: the window starts this tall (Main.cpp).
    int neededHeight() const;

    // What the outputs' ALERTS are saying now ("STREAM SILENT, CALLS CLIPPING"), or empty: the
    // tray icon flashes while it isn't.
    juce::String activeAlert() const { return alert; }

    // App preferences (the window remembers its place here).
    juce::PropertiesFile& getSettings() { return settings; }

    // A notice for when the window may be hidden (a game in front): the app shows it from the tray.
    std::function<void (const juce::String& title, const juce::String& text)> onNotify;

    // A newer Faider found (CHECK FOR UPDATES), for the tray menu: "Get Faider 0.1.6...".
    void addUpdateItems (juce::PopupMenu&);

    // SAVE A PROBLEM REPORT (the tray menu, FIND): the logs, the last crash and the versions in a
    // .zip saved where the user picks, then shown in Explorer. Nothing is sent anywhere.
    void saveProblemReport();

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    juce::StringArray deviceCells (const EndpointInfo&) const;
    void sortDevices();                                  // keeping the same devices selected
    void sortDevices (const juce::StringArray& chosenIds);
    juce::StringArray selectedDeviceIds() const;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void endpointsChanged (const juce::Array<EndpointInfo>&, const juce::StringArray& events) override;
    void timerCallback() override;
    void logEvent (const juce::String&);
    void createDefaultMix();
    juce::String pickClock (bool asio, bool announce = true);
    void checkClock();
    bool clockForced = false;   // started with --clock: the built-in clock stays (no device takes over)
    bool clockFromCommandLine = false;   // --clock given (it wins over DEVICES > CLOCK)
    juce::String clockChosenAtStart;     // DEVICES > CLOCK's choice as this run started ("" the interface, "builtin", "windows:NAME")
    void chooseClock (const juce::String& choice);   // DEVICES > CLOCK: kept, for the next start (RESTART NOW offered)
    void restartFaider();                // quits, and a new copy starts once this one has gone
    juce::String savedMaster;   // started with --clock: the mix's own interface, saved instead of the chosen clock
    juce::String masterToSave() const { return clockForced && savedMaster.isNotEmpty() ? savedMaster : interfaceName; }
    juce::StringArray clockRefused;   // clocks that wouldn't start, until the devices change (pickClock skips them)
    void saveSession();
    void keepHistory (const juce::String& xml);   // a dated copy, at most one per 5 minutes, newest 60 kept
    void showTimeline (juce::PopupMenu::Options where);
    juce::Array<juce::File> historyFiles() const;  // newest first
    juce::Array<juce::File> setAsideFiles() const; // session-unloaded-*.xml beside session.xml, newest first
    void restoreSession (const juce::File&, const juce::String& named = {});   // named: a saved session's name
    // Named sessions: SAVE AS, OPEN, EXPORT and IMPORT (MainComponent.cpp).
    juce::File sessionsDir() const;
    void saveSessionAs();
    void showSessions (juce::PopupMenu::Options where);
    void showAppRules (juce::PopupMenu::Options where);   // APP RULES: where each app goes when it starts playing
    void showLooks (juce::PopupMenu::Options where);      // LOOK: the console's colours and typeface
    void saveLookAs();
    void repaintEverything();                              // a new look: every view drawn again
    void exportSession();
    void importSession();
    std::unique_ptr<juce::FileChooser> sessionChooser;
    std::unique_ptr<juce::FileChooser> chooser;   // FFMPEG's and the problem report's
    void pickFfmpeg();                            // SETTINGS > GENERAL > FFMPEG
    juce::Time lastHistory;
    void setZoom (float);
    void stepZoom (int direction);   // the next stage up (1) or down (-1)
    void structureStep (const juce::String& kind, int index, int slot, bool adding);   // undo for strips and plugins
    void toggleMidiMap();             // MIDI MAP mode on / off
    void undoRedo (bool redo);        // a step back or forward, logged and noticed
    void showHistory (juce::PopupMenu::Options where);   // every undo step, to go back to any
    void toggleMini();                                   // MINI: the mini mixer up, or away
    std::vector<PreflightPanel::Check> preflightChecks();
    void showTip (const juce::String& page);             // a page's first-run tip (once)
    void showPreflight (bool onlyIfWrong);
    void showWindowsDefaults (juce::PopupMenu::Options where);   // SET MY DEFAULTS: real devices as Windows' defaults
    void showSetup ();                   // SETUP in its own window: not a page (first start, FIND, SIMPLE's key)
    void closeSetup ();
    std::vector<PreflightPanel::Check> zeroChecks();   // ZERO DESK: things left set (solos, DIM, the oscillator, KEY LISTEN...)
    void showZero();               // PRE-FLIGHT (on start: only if something's wrong)
    std::unique_ptr<juce::DocumentWindow> mini;
    juce::TextButton miniButton { "MINI" };
    Toasts toasts;                    // notices in the window's corner
    const juce::uint32 openedAt = juce::Time::getMillisecondCounter();   // (devices found at start-up aren't news)
    void styleEventLog();             // the event log in the theme's colours
    void openMidiPanel();             // the MIDI page: inputs and bindings
    void checkAlerts();               // outputs' ALERTS, once a second
    void watchMutedTalk();            // a muted mic channel heard: say so (ten times a second)
    void openCommandPalette();        // Ctrl+K / FIND: everything Faider can do, by name
    // CHECK FOR UPDATES (UpdateCheck.h): once a day a moment after start, or now (asked: says
    // what it found, up to date too). A newer version is kept to offer its release page.
    void checkForUpdates (bool asked);
    void updateChecked (const UpdateCheck::Result&, bool asked);
    void openRelease();
    std::unique_ptr<UpdateCheck::Checker> updates;
    juce::String newVersion, newVersionUrl;
    std::unique_ptr<CommandPalette> palette;
    struct Ticker : juce::Timer { std::function<void()> tick; void timerCallback() override { tick(); } };
    Ticker muteWatch;
    std::map<juce::String, juce::uint32> clipNoticed;   // per channel: when its last clip notice was
    void watchClipping();
    std::map<juce::String, int> talkingMuted;   // per mic channel: tenths of a second it's been heard while muted
    juce::uint32 lastMutedWarning = 0;
    bool warnMutedTalk = true;
    juce::String alert;                                    // activeAlert()
    juce::StringArray connectedDevices;                    // WASAPI devices connected now (macro triggers)
    std::map<juce::String, int> silentFor, clipShown;      // per output: seconds silent; seconds a clip stays shown
    int recordStoppedShown = 0;                            // seconds the tray still says a recording ran out of room

    // The console look for everything without its own (tabs, toolbar, Devices tab, menus).
    // First, so it outlives every component.
    juce::SharedResourcePointer<Console::Look> look;

    juce::File dataDir;
    juce::File eventFile, statusFile, sessionFile;
    juce::String interfaceName, lastSavedSession;
    bool sessionLoaded = false;   // a mix was restored or made at start; saveSession() writes only then
    int tick = 0;
    AudioEngine engine { [this] (const juce::String& e) { logEvent (e); } };
    EndpointWatcher watcher { *this };
    juce::Array<EndpointInfo> endpoints;

    juce::TooltipWindow tooltips { this, 600 };   // without one, JUCE never shows tooltips

    // Every page's content edge (Console::Space::pad): the top row lines up with the pages' keys.
    static constexpr int pageEdge = Console::Space::pad;


    // HEAR WHAT THEY HEAR: the mic output (the one playing into VB-CABLE, which apps hear as the
    // mic) recorded for 5 s from now, then played once in the headphones.
    void hearWhatTheyHear();
    juce::String micOutputName() const;

    std::unique_ptr<Overlay> overlay;   // the in-game overlay (Overlay.h)
    void toggleOverlay();

    using PageTabs = Console::KeyTabs;
    PageTabs tabs { pageEdge };
    std::unique_ptr<MixerComponent> mixer;
    std::unique_ptr<juce::Component> consoleDesk;   // (the Mixer page)
    std::unique_ptr<juce::Component> consoleSidebar;   // (and its pages as cards down the left)
    std::unique_ptr<juce::Component> consoleRouting;   // (its Routing page: the desk's strips as blocks)
    juce::Component::SafePointer<juce::DialogWindow> setupWindow;   // (SETUP, shown in its own window: Simple.h)
    std::unique_ptr<PadsPanel> pads;
    std::unique_ptr<TapePanel> tape;
    std::unique_ptr<ToolsTab> tools;      // the TOOLS tab: PADS and TAPE on tabs of their own
    std::unique_ptr<SceneBar> sceneBar;   // in the tab bar, after the tabs

    std::unique_ptr<MixHistory> history;  // Ctrl+Z / Ctrl+Y
    std::unique_ptr<MidiMapOverlay> midiMap;        // MIDI MAP mode, while on
    // SETTINGS (SettingsTab.h): GENERAL, MACROS, HOTKEYS and CONTROL.
    std::unique_ptr<SettingsTab> settingsTab;
    std::unique_ptr<OptionsPage> generalPage;
    std::unique_ptr<HotkeysPage> hotkeysPage;
    std::unique_ptr<ControlPanel> controlPanel;
    void makeSettings();
    void openSettings (const juce::String& section);
    int lastShortcut = 0;                           // keyPressed: the last Ctrl shortcut, and when
    juce::uint32 lastShortcutAt = 0;
    juce::ListBox deviceList { "Devices", this };
    DevicesTab::Panel devicesPanel { deviceList, endpoints, engine };   // VB-CABLE settings, device tools and list, stream diagnostics
    juce::TextEditor eventLog;

    // App preferences (not part of the mix): %APPDATA%\Faider\settings.xml
    juce::PropertiesFile settings { dataDir.getChildFile ("settings.xml"), juce::PropertiesFile::Options() };
    static constexpr float minZoom = 0.75f, maxZoom = 2.0f;
    static constexpr float zoomStages[] = { 1.0f, 1.25f, 1.5f };   // the three sizes - + steps between (any other: CUSTOM in SETTINGS)
    float zoom = 1.0f;
    int sortColumn = -1;        // Devices list order (-1: as Windows lists them)
    bool sortAscending = true;
    juce::TextButton zoomOut { "-" }, zoomReset { "100%" }, zoomIn { "+" };
    juce::TextButton findButton { "FIND" }, historyButton { "HISTORY" };
    HealthLight health { engine };   // the top bar's audio health light
};

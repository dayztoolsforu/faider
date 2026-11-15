// MainComponent's SETTINGS tab (makeSettings): every setting, as the one list its pages and FIND
// are built from (SettingsRegistry.h), and what its rows do: looks, app rules, the clock, ffmpeg.
#include "MainComponent.h"
#include "ProductName.h"
#include "ui/AboutBox.h"
#include "ui/Theme.h"
#include "engine/PluginScan.h"
#include "StartWithWindows.h"

namespace
{
    // A button's or a value's text that doesn't change.
    std::function<juce::String()> says (const juce::String& text) { return [text] { return text; }; }

    // "ABOVE NORMAL": "Above normal" (the names the engine and the pads keep in capitals).
    juce::String sentence (const juce::String& text)
    {
        auto s = text.toLowerCase();
        s = s.substring (0, 1).toUpperCase() + s.substring (1);
        for (auto* kept : { "MIDI", "OSC", "ASIO" })
            s = s.replace (juce::String (kept).toLowerCase(), kept);
        return s;
    }
}

// The SETTINGS tab, after DEVICES: the app's preferences in one place, a page each, down the left.
void MainComponent::makeSettings()
{
    using namespace SettingsRegistry;
    auto remember = [this] (const char* key, const juce::var& value) { settings.setValue (key, value); settings.saveIfNeeded(); };
    const juce::String ellipsis (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
    std::vector<Setting> list;
    auto add = [&list] (Setting s) { list.push_back (std::move (s)); };

    //--------------------------------------------------------------------------
    // APP > GENERAL
    add ({ .page = "General", .section = "Window", .name = "Start with Windows",
           .description = juce::String ("Open ") + Product::name + " (in the tray) when you sign in.", .keywords = "startup login boot tray",
           .kind = Kind::toggle,
           .get = [] { return StartWithWindows::isOn(); }, .set = [] (bool on) { StartWithWindows::set (on); } });
    // The three stages (Ctrl +/- and the tab bar's - + step between them), or CUSTOM: any size from
    // 75 to 200%, kept until a stage is picked again.
    add ({ .page = "General", .section = "Window", .name = "Zoom",
           .description = "The size of everything in the window. Ctrl + and Ctrl - step between 100, 125 and 150%; Custom picks any size from 75 to 200%.",
           .keywords = "scale size bigger smaller", .kind = Kind::segmented,
           .options = { "100%", "125%", "150%", "Custom" + ellipsis },
           .chosen = [this]
           {
               for (int i = 0; i < (int) std::size (zoomStages); ++i)
                   if (std::abs (zoomStages[i] - zoom) < 0.01f)
                       return i;
               return 3;
           },
           .choose = [this] (int i)
           {
               if (i < (int) std::size (zoomStages))
                   return setZoom (zoomStages[i]);
               juce::PopupMenu sizes;
               for (int p = 75; p <= 200; p += 5)
                   sizes.addItem (juce::String (p) + "%", true, std::abs ((float) p / 100.0f - zoom) < 0.01f, [this, p] { setZoom ((float) p / 100.0f); });
               sizes.showMenuAsync (juce::PopupMenu::Options());
           },
           .note = [this] { return std::none_of (std::begin (zoomStages), std::end (zoomStages), [this] (float s) { return std::abs (s - zoom) < 0.01f; })
                                       ? "Now: " + juce::String (juce::roundToInt (zoom * 100.0f)) + "%" : juce::String(); } });
    add ({ .page = "General", .section = "Window", .name = "Look",
           .description = "The console's colours and typeface. A look is a file in %APPDATA%\\Faider\\looks; Save as writes the one in force to a new file.",
           .keywords = "theme colour color typeface font skin", .kind = Kind::dropdown,
           .value = [] { return Theme::currentLookName(); },
           .menu = [this] (juce::Component& c) { showLooks (juce::PopupMenu::Options().withTargetComponent (&c)); } });
    add ({ .page = "General", .section = "Help", .name = "Tips",
           .description = "A tip about each page, the first time it's shown. Show again shows each page's tip once more.",
           .keywords = "hints help first run", .kind = Kind::toggle,
           .get = [this] { return settings.getBoolValue ("tips", true); }, .set = [remember] (bool on) { remember ("tips", on); },
           .actions = { { says ("Show again"), [this, remember] (juce::Component&)
           {
               for (auto* page : { "Mixer", "Routing", "Tools", "Devices", "Settings" })
                   settings.removeValue (juce::String ("tipSeen_") + page);
               remember ("tips", true);
               showTip ("Settings");
           } } } });

    //--------------------------------------------------------------------------
    // APP > NOTIFICATIONS
    add ({ .page = "Notifications", .name = "Warn if I talk while cut",
           .description = "A notice when you speak into a cut mic (above -30 dBFS for half a second), from the tray if the window is hidden.",
           .keywords = "mute muted mic talking", .kind = Kind::toggle,
           .get = [this] { return warnMutedTalk; }, .set = [this, remember] (bool on) { warnMutedTalk = on; remember ("warnMutedTalk", on); } });
    for (auto [kind, name, text] : { std::tuple ("device", "Devices connected or gone", "When a device is plugged in or goes, or Windows' default changes."),
                                     std::tuple ("clip", "A channel clipping", "When a channel's source or its output reaches full scale (each channel at most once in 10 s)."),
                                     std::tuple ("scene", "Scenes recalled", "When a scene is recalled, by its key, a trigger or remote control."),
                                     std::tuple ("macro", "Macros run", "When a macro runs."),
                                     std::tuple ("undo", "Undo and redo", "What Ctrl+Z or Ctrl+Y undid or redid.") })
        add ({ .page = "Notifications", .section = "Corner notices", .name = name,
               .description = juce::String (text) + " The event log has everything either way.", .keywords = "notice toast corner",
               .kind = Kind::toggle,
               .get = [this, key = juce::String ("notice_") + kind] { return settings.getBoolValue (key, true); },
               .set = [remember, key = juce::String ("notice_") + kind] (bool on) { remember (key.toRawUTF8(), on); } });

    //--------------------------------------------------------------------------
    // APP > STARTUP & CHECKS
    add ({ .page = "Startup & checks", .name = "Pre-flight on start",
           .description = juce::String ("A few seconds after ") + Product::name + " starts: the audio, the mic cable and route, cut mics. Shown only if something's wrong.",
           .keywords = "preflight check setup", .kind = Kind::toggle,
           .get = [this] { return settings.getBoolValue ("preflightOnStart", true); },
           .set = [remember] (bool on) { remember ("preflightOnStart", on); } });
    add ({ .page = "Startup & checks", .section = "Run now", .name = "Pre-flight",
           .description = "Check the setup now: the audio, the mic cable and route, cut mics, things heard twice, Windows audio.",
           .keywords = "preflight check", .actions = { { says ("Run pre-flight"), [this] (juce::Component&) { showPreflight (false); }, true } } });
    add ({ .page = "Startup & checks", .section = "Run now", .name = "Zero the desk",
           .description = "Things left set that change what's heard: solos, key listen, gains far off, outputs cut. Each with its fix.",
           .keywords = "zero desk reset solo", .actions = { { says ("Check"), [this] (juce::Component&) { showZero(); } } } });

    //--------------------------------------------------------------------------
    // APP > UPDATES & ABOUT
    add ({ .page = "Updates & about", .name = "Check for updates",
           .description = "At most once a day, half a minute after start: one request to GitHub with the app's name and version, nothing else. Nothing is downloaded or installed.",
           .keywords = "update version github new", .kind = Kind::toggle,
           .get = [this] { return settings.getBoolValue ("checkForUpdates", true); },
           .set = [remember] (bool on) { remember ("checkForUpdates", on); } });
    add ({ .page = "Updates & about", .name = "Version",
           .description = "The version running now. Check now asks GitHub at once; when a newer one is out, its release page is a click away.",
           .keywords = "update release", .status = says (juce::String (Product::name) + " " + JUCE_APPLICATION_VERSION_STRING),
           .actions = { { [this, ellipsis] { return newVersion.isNotEmpty() ? "Get " + juce::String (Product::name) + " " + newVersion + ellipsis : juce::String ("Check now"); },
                          [this] (juce::Component&) { if (newVersion.isNotEmpty()) openRelease(); else checkForUpdates (true); } } },
           .note = [this] { return updates != nullptr && updates->isChecking() ? juce::String ("Asking GitHub") + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")) : juce::String(); } });
    add ({ .page = "Updates & about", .section = "About", .name = juce::String ("About ") + Product::name,
           .description = "The version and the credits.", .keywords = "credits licence license",
           .actions = { { says ("Open"), [] (juce::Component&) { AboutBox::show(); } } } });
    add ({ .page = "Updates & about", .section = "About", .name = "Privacy",
           .description = juce::String ("What ") + Product::name + " keeps on this PC, and the one request it makes.", .keywords = "data privacy policy",
           .actions = { { says ("Read policy"), [] (juce::Component&) { juce::URL ("https://github.com/dayztoolsforu/faider/blob/main/PRIVACY.md").launchInDefaultBrowser(); } } } });
    add ({ .page = "Updates & about", .section = "About", .name = "Problem report",
           .description = "The logs, the last crash and the versions in a .zip you can send. Nothing is sent by itself.",
           .keywords = "crash bug log support zip",
           .actions = { { says ("Save report" + ellipsis), [this] (juce::Component&) { saveProblemReport(); } } } });

    //--------------------------------------------------------------------------
    // AUDIO > AUDIO
    // CLOCK: what sets the clock: the interface (ASIO, Faider's own pick, as without a choice), a
    // Windows playback device, or the built-in clock. Used from the next start, as --clock is;
    // --clock on the command line still wins.
    auto clockName = [this] (const juce::String& c)
    {
        if (c == "builtin")
            return juce::String ("The built-in clock");
        if (! c.startsWith (AudioEngine::windowsClockPrefix))
            return juce::String ("The interface (ASIO)");
        const auto name = c.fromFirstOccurrenceOf (AudioEngine::windowsClockPrefix, false, false);
        for (auto& e : endpoints)
            if (e.api == "WASAPI" && e.isOutput && e.isActive() && e.name == name)
                return endpointDisplayName (e);
        return name + " (not connected)";
    };
    add ({ .page = "Audio", .section = "Clock", .name = "Clock",
           .description = "What sets the clock, from the next start: the interface (ASIO), a Windows playback device, or the built-in clock. Changing it offers a restart.",
           .keywords = "master sample rate asio wasapi built-in", .kind = Kind::dropdown,
           .value = [this, clockName] { return clockName (settings.getValue ("clock")); },
           .menu = [this] (juce::Component& c)
           {
               const auto chosen = settings.getValue ("clock");
               juce::PopupMenu menu;
               menu.addItem ("The interface (ASIO)", true, chosen.isEmpty(), [this] { chooseClock ({}); });
               for (auto& e : endpoints)
                   if (e.api == "WASAPI" && e.isOutput && e.isActive() && ! engine.isDeviceDisabled (e.name))
                   {
                       const auto key = AudioEngine::windowsClockPrefix + e.name;
                       menu.addItem (endpointDisplayName (e), true, chosen == key, [this, key] { chooseClock (key); });
                   }
               if (const auto name = chosen.fromFirstOccurrenceOf (AudioEngine::windowsClockPrefix, false, false);
                   name.isNotEmpty() && std::none_of (endpoints.begin(), endpoints.end(), [&name] (const EndpointInfo& e) { return e.isOutput && e.isActive() && e.name == name; }))
                   menu.addItem (name + " (not connected)", true, true, [this, chosen] { chooseClock (chosen); });   // (the one chosen, while it's away)
               menu.addItem ("The built-in clock", true, chosen == "builtin", [this] { chooseClock ("builtin"); });
               menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&c));
           },
           .note = [this]
           {
               const auto device = engine.getClock() == AudioEngine::Clock::asio ? engine.getInterfaceAdapter() : engine.getClockDevice();
               auto text = "Now: " + engine.getClockName() + (device.isNotEmpty() ? " (" + device + ")" : juce::String());
               if (clockFromCommandLine)
                   text << ". --clock on the command line sets it for this run";
               else if (settings.getValue ("clock") != clockChosenAtStart)
                   text << ". The new choice takes effect when " << Product::name << " starts again";
               return text + ".";
           } });

    // WINDOWS AUDIO: Windows' audio engine (audiodg.exe) and its priority now (read every 5 s:
    // watchWindowsAudio), and BOOST (AudioBoost.h), which Windows asks about first.
    const bool canBoost = AudioBoost::canElevate();
    add ({ .page = "Audio", .section = "Clock", .name = "Windows audio",
           .description = "Windows' audio engine (audiodg.exe) at High priority instead of Normal can stop crackle on Windows devices and captured apps. "
                          "Windows asks for an administrator's approval each time, and a restart of Windows audio undoes it (you're told when).",
           .keywords = "boost audiodg priority crackle dropouts back to normal",
           .status = [this]
           {
               const auto& s = windowsAudio;
               if (s.pid == 0)
                   return juce::String ("Not running");
               if (s.priority == AudioBoost::Priority::unknown)
                   return juce::String ("Priority unknown");
               return sentence (AudioBoost::priorityName (s.priority)) + " priority" + (s.pinnedCore >= 0 ? ", core " + juce::String (s.pinnedCore) : juce::String());
           },
           .actions = { { [this, ellipsis] { return boostingWindowsAudio ? "Waiting" + ellipsis
                                                    : settings.getBoolValue ("boostAudiodg") && AudioBoost::isBoosted (windowsAudio.priority) ? juce::String ("Stop boosting") : juce::String ("Boost"); },
                          [this] (juce::Component&)
                          {
                              if (settings.getBoolValue ("boostAudiodg") && AudioBoost::isBoosted (windowsAudio.priority))
                                  stopBoostingWindowsAudio();
                              else
                                  boostWindowsAudio();
                          }, false, [this] { return ! boostingWindowsAudio; } },
                        { says ("Back to normal"), [this] (juce::Component&) { unboostWindowsAudio(); }, false,
                          [this] { return ! boostingWindowsAudio && windowsAudio.pid != 0 && (windowsAudio.priority != AudioBoost::Priority::normal || windowsAudio.pinnedCore >= 0); } } },
           .note = [this] { return boostNote.isNotEmpty() ? boostNote + "." : juce::String ("ASIO interfaces go around it, so they aren't affected."); },
           .warning = [canBoost] { return canBoost ? juce::String() : juce::String ("Needs ") + Product::name + " installed for all users"; } });
    add ({ .page = "Audio", .section = "Clock", .name = "Pin to one core",
           .description = "With Boost, Windows audio is held to the last core. Only for crackle on a USB headset; off unless you pick it. It takes effect from the next Boost.",
           .keywords = "boost affinity usb headset", .kind = Kind::toggle,
           .get = [this] { return settings.getBoolValue ("boostPinCore"); },
           .set = [this] (bool pin)
           {
               settings.setValue ("boostPinCore", pin);
               settings.saveIfNeeded();
               boostNote = pin ? "It's pinned to the last core from the next Boost"
                               : "From the next Boost it isn't pinned (a pin already made stays until Windows audio restarts)";
           } });
    add ({ .page = "Audio", .section = "Meters", .name = "Channel meter point",
           .description = "Where every channel's meter reads: IN, the source as it comes in; PRE, after the effects; POST, after the fader.",
           .keywords = "meter level in pre post", .kind = Kind::segmented,
           .options = { "IN", "PRE", "POST" },
           .chosen = [] { return AudioEngine::meterPoint.load(); }, .choose = [] (int i) { AudioEngine::meterPoint = i; } });
    add ({ .page = "Audio", .section = "Meters", .name = "Meter standard",
           .description = "How every meter reads: peak, true peak, RMS, VU, PPM, K-system or momentary loudness.",
           .keywords = "peak true peak rms vu ppm lufs k-20 ebu", .kind = Kind::dropdown,
           .value = [] { return Metering::standards()[(size_t) juce::jlimit (0, (int) Metering::standards().size() - 1, Metering::currentStandard.load())].shortName; },
           .menu = [] (juce::Component& c)
           {
               juce::PopupMenu menu;
               const auto& all = Metering::standards();
               for (int i = 0; i < (int) all.size(); ++i)
                   menu.addItem (all[(size_t) i].menuName, true, Metering::currentStandard.load() == i, [i] { Metering::currentStandard = i; });
               menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&c));
           } });
    {
        // How long a new plugin has to load in its test copy before it fails (PluginScan.h).
        static constexpr int seconds[] = { 30, 60, 120 };
        add ({ .page = "Audio", .section = "Plugins", .name = "Plugin test time",
               .description = "How long a new plugin has to load in its test before it fails. Slow plugins may need 60 or 120 s.",
               .keywords = "scan vst timeout", .kind = Kind::segmented,
               .options = { "30 S", "60 S", "120 S" },
               .chosen = []
               {
                   for (int i = 0; i < (int) std::size (seconds); ++i)
                       if (seconds[i] == PluginScan::testTime())
                           return i;
                   return 0;
               },
               .choose = [remember] (int i)
               {
                   PluginScan::setTestTime (seconds[i]);
                   remember ("pluginTestSeconds", seconds[i]);
               } });
    }

    //--------------------------------------------------------------------------
    // AUDIO > RECORDING
    add ({ .page = "Recording", .name = "FFmpeg",
           .description = "MP3, AAC, Opus and ALAC recording need ffmpeg.exe: pick it, or have it on the PATH. One picked is used before the PATH's.",
           .keywords = "ffmpeg mp3 aac opus alac recorder", .status = [] { const auto f = Recorder::ffmpeg(); return f.existsAsFile() ? f.getFullPathName() : juce::String ("Not found"); },
           .path = true,
           .actions = { { says ("Pick" + ellipsis), [this] (juce::Component&) { pickFfmpeg(); } },
                        { says ("Use PATH"), [this] (juce::Component&) { useFfmpegOnPath(); }, false, [this] { return settings.getValue ("ffmpegPath").isNotEmpty(); } } },
           .note = [this] { return settings.getValue ("ffmpegPath").isNotEmpty() ? juce::String ("Picked: used before the one on the PATH.")
                                   : Recorder::ffmpeg().existsAsFile() ? juce::String ("Found on the PATH.") : juce::String(); } });

    //--------------------------------------------------------------------------
    // MIX > APPS
    add ({ .page = "Apps", .name = "App rules",
           .description = "Which bus or output a program goes to when it starts playing. Removing a program's channel removes its rule.",
           .keywords = "program application discord game route rule", .kind = Kind::custom,
           .actions = { { says ("Add rule"), [this] (juce::Component& c) { addAppRuleMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&c)); }, true } },
           .make = [this]
           {
               return std::make_unique<SettingsUi::Table> (std::vector<SettingsUi::Table::Column> { { "Program", 0.36f }, { "Goes to", 0.30f }, { {}, 0.34f } },
                                                           "No rules yet", [this]
               {
                   std::vector<SettingsUi::Table::Line> lines;
                   for (int i = 0; i < (int) engine.appRules.size(); ++i)
                   {
                       const auto& r = engine.appRules[(size_t) i];
                       const auto to = r.to.fromFirstOccurrenceOf (":", false, false) + (r.to.startsWith ("bus:") ? " (bus)" : " (output)");
                       lines.push_back ({ { juce::File (r.exe).getFileNameWithoutExtension(), to },
                                          { { "Change", [this, i] (juce::Component& c)
                                              {
                                                  if (! juce::isPositiveAndBelow (i, (int) engine.appRules.size()))
                                                      return;
                                                  appDestinations (engine.appRules[(size_t) i].to, [this, i] (const juce::String& dest)
                                                  {
                                                      if (juce::isPositiveAndBelow (i, (int) engine.appRules.size()))
                                                          engine.appRules[(size_t) i].to = dest;
                                                  }).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&c));
                                              } },
                                            { "Delete", [this, i] (juce::Component&)
                                              {
                                                  if (juce::isPositiveAndBelow (i, (int) engine.appRules.size()))
                                                      engine.appRules.erase (engine.appRules.begin() + i);
                                              } } } });
                   }
                   return lines;
               });
           },
           .wide = true });

    //--------------------------------------------------------------------------
    // MIX > SESSIONS
    add ({ .page = "Sessions", .name = "This mix",
           .description = "Keep this mix under a name, or save it to a file to share or move.", .keywords = "save export session",
           .actions = { { says ("Save as" + ellipsis), [this] (juce::Component&) { saveSessionAs(); }, true },
                        { says ("Export" + ellipsis), [this] (juce::Component&) { exportSession(); } } } });
    add ({ .page = "Sessions", .section = "Saved sessions", .name = "Saved sessions",
           .description = juce::String ("Opening one restarts ") + Product::name + " with it. Import opens a session from a file.",
           .keywords = "open import export load named session", .kind = Kind::custom,
           .actions = { { says ("Import" + ellipsis), [this] (juce::Component&) { importSession(); } } },
           .make = [this]
           {
               return std::make_unique<SettingsUi::Table> (std::vector<SettingsUi::Table::Column> { { "Name", 0.40f }, { "Saved", 0.30f }, { {}, 0.30f } },
                                                           "No saved sessions yet", [this]
               {
                   auto files = sessionsDir().findChildFiles (juce::File::findFiles, false, "*.xml");
                   std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
                   std::vector<SettingsUi::Table::Line> lines;
                   for (auto& f : files)
                       lines.push_back ({ { f.getFileNameWithoutExtension(), f.getLastModificationTime().formatted ("%d %b %Y, %H:%M") },
                                          { { "Open", [this, f] (juce::Component&) { restoreSession (f, f.getFileNameWithoutExtension()); } },
                                            { "Export", [this, f] (juce::Component&) { exportSessionFile (f); } } } });
                   return lines;
               });
           },
           .wide = true });
    add ({ .page = "Sessions", .section = "Saved sessions", .name = "Session timeline",
           .description = "Every kept version of this mix by date: bring back the whole mix (it restarts), or put one channel back as it was.",
           .keywords = "history restore earlier version backup",
           .actions = { { says ("Open timeline"), [this] (juce::Component& c) { showTimeline (juce::PopupMenu::Options().withTargetComponent (&c)); } } } });

    //--------------------------------------------------------------------------
    // CONTROL > MIDI & OSC
    auto* remote = &engine.getRemote();
    add ({ .page = "MIDI & OSC", .section = "MIDI", .name = "MIDI map",
           .description = "Click a control on screen, then move a knob or key on your controller. Ctrl+L starts it anywhere; Esc finishes.",
           .keywords = "learn controller midi map bind", .actions = { { [this] { return juce::String (midiMap != nullptr ? "Finish mapping" : "Start mapping"); },
                                                                       [this] (juce::Component&) { toggleMidiMap(); }, true } },
           .passThrough = true });
    add ({ .page = "MIDI & OSC", .section = "MIDI", .name = "Inputs and bindings",
           .description = "Each MIDI input on or off, and what its controls are bound to.", .keywords = "midi device controller bindings",
           .actions = { { says ("Open"), [this] (juce::Component&) { openMidiPanel(); } } },
           .note = [remote] { return juce::String (remote->mappingCount()) + (remote->mappingCount() == 1 ? " binding." : " bindings."); } });
    add ({ .page = "MIDI & OSC", .section = "OSC", .name = "OSC",
           .description = "Listen for OSC messages from programs on this PC (never from the network).", .keywords = "osc remote network",
           .kind = Kind::toggle,
           .get = [remote] { return remote->getOscPort() > 0; },
           .set = [remote] (bool on) { remote->setOscPort (on ? remote->getOscPreferredPort() : 0); },
           .note = [remote] { return remote->getOscPort() > 0 ? "Listening on port " + juce::String (remote->getOscPort()) + "." : juce::String(); } });
    // OSC PORT: a number from 1024 to 65535; OSC moves there if it's on, else uses it when it's switched on.
    add ({ .page = "MIDI & OSC", .section = "OSC", .name = "OSC port",
           .description = "The port OSC listens on, 1024 to 65535. If OSC is on, it moves there at once.", .keywords = "osc port",
           .kind = Kind::custom,
           .make = [remote]
           {
               return std::make_unique<SettingsUi::NumberField> ([remote] { return remote->getOscPreferredPort(); }, [remote] (int wanted)
               {
                   if (wanted < 1024 || wanted > 65535)
                       juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "OSC port", "A port is a number from 1024 to 65535.");
                   else if (! remote->setOscPreferredPort (wanted))
                       juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "OSC port",
                                                               "Port " + juce::String (wanted) + " couldn't be opened (another program may have it). OSC is off.");
               }, 5);
           } });

    //--------------------------------------------------------------------------
    // CONTROL > HOTKEYS: every global hotkey: the pads', macros' and scenes' (set through PADS, so
    // Windows has one set for them all), instant replay's, and the fixed ones. `working`: whether
    // Windows took the key (PadsPanel::hotkeyRegistered).
    hotkeysPage = std::make_unique<HotkeysPage> ([this]
    {
        std::vector<HotkeysPage::Entry> keys;
        auto& p = engine.getPads();
        for (int i = 0; i < Pads::count; ++i)
            if (const auto& pad = p.pad (i); pad.file.isNotEmpty() || pad.hotkey.isNotEmpty())
                keys.push_back ({ "Pads", "Pad " + juce::String (i + 1) + (pad.name.isNotEmpty() ? ": " + pad.name : juce::String()),
                                  pad.hotkey, [this, i] (const juce::KeyPress& k) { return pads->setHotkey (i, k); },
                                  "Tools > Pads", pads->hotkeyRegistered (i) });
        auto& macros = engine.getMacros().list;
        for (int i = 0; i < (int) macros.size(); ++i)
            keys.push_back ({ "Macros", "Macro: " + macros[(size_t) i].name, macros[(size_t) i].hotkey,
                              [this, i] (const juce::KeyPress& k) { return pads->setMacroHotkey (i, k); },
                              "Settings > Macros", pads->hotkeyRegistered (PadsPanel::macroHotkeyBase + i) });
        auto& scenes = engine.getScenes().list;
        for (int i = 0; i < (int) scenes.size(); ++i)
            keys.push_back ({ "Scenes", "Scene: " + scenes[(size_t) i].name, scenes[(size_t) i].hotkey,
                              [this, i] (const juce::KeyPress& k)
                              {
                                  const bool ok = pads->setSceneHotkey (i, k);
                                  if (ok && sceneBar != nullptr) sceneBar->repaint();
                                  return ok;
                              },
                              "Right-click the scene's key", pads->hotkeyRegistered (PadsPanel::sceneHotkeyBase + i) });
        keys.push_back ({ "Tape", "Save instant replay", tape->replayHotkey, [this] (const juce::KeyPress& k)
        {
            const auto key = k.isValid() ? k.getTextDescription() : juce::String();
            if (! tape->setReplayHotkey (key))
                return false;
            tape->replayHotkey = key;
            return true;
        }, "Tools > Tape (right-click Save)", pads->hotkeyRegistered (PadsPanel::extraHotkeyBase + 1) });
        keys.push_back ({ "Anywhere", "Show / hide the in-game overlay", "ctrl + shift + O", nullptr, "Anywhere",
                          pads->hotkeyRegistered (PadsPanel::extraHotkeyBase + 2), { "ctrl + shift + O" } });
        for (auto& fixed : PadsPanel::fixedKeys())   // (the window's own keys: a hotkey may not take one)
            keys.push_back ({ "In Faider", sentence (fixed.name), fixed.shown, nullptr, juce::String ("In ") + Product::name, true, fixed.keys });
        keys.push_back ({ "On the mixer", "Select the channel left / right", "left / right", nullptr, "On the mixer" });
        keys.push_back ({ "On the mixer", "CUT / SOLO the selected channel", "M  /  S", nullptr, "On the mixer" });
        keys.push_back ({ "On the mixer", "Its fader up / down 0.5 dB (Shift: 3 dB)", "up / down", nullptr, "On the mixer" });
        return keys;
    });

    std::vector<Page> pages {
        { "App", "General", "The window, how it looks, and the tips." },
        { "App", "Notifications", juce::String ("What ") + Product::name + " tells you about in the window's corner." },
        { "App", "Startup & checks", juce::String ("What ") + Product::name + " checks when it starts, and the same checks now." },
        { "App", "Updates & about", "The version, the daily update check, and a report for when something goes wrong." },
        { "Audio", "Audio", "What sets the clock, Windows' own audio engine, the meters and the plugin test." },
        { "Audio", "Recording", "What the recorder needs for compressed formats." },
        { "Mix", "Apps", "Where programs go when they start playing." },
        { "Mix", "Sessions", "Mixes kept under a name, and the earlier versions of this one." },
        { "Control", "MIDI & OSC", "Controllers, and other programs on this PC, moving the desk." },
        { "Control", "Hotkeys", "Every global hotkey in one place. They work while a game has focus.", hotkeysPage.get(), false, "keys shortcut keyboard" },
        { "Control", "Macros", "Lists of actions run from a hotkey, a pad, a trigger or remote control.", &pads->getMacrosPage(), true, "macro action trigger push to talk" },
    };

    settingsTab = std::make_unique<SettingsTab> (std::move (pages), std::move (list));
    tabs.addTab ("Settings", juce::Colours::transparentBlack, settingsTab.get(), false);
}

// A page by name. The names the sections had before (GENERAL, MACROS, HOTKEYS, CONTROL) still work.
void MainComponent::openSettings (const juce::String& section)
{
    tabs.setCurrentTabIndex (tabs.getTabNames().indexOf ("Settings"));
    settingsTab->show (section.equalsIgnoreCase ("Control") ? juce::String ("MIDI & OSC") : section);
}

void MainComponent::openSetting (const juce::String& page, const juce::String& name)
{
    tabs.setCurrentTabIndex (tabs.getTabNames().indexOf ("Settings"));
    settingsTab->showSetting (page, name);
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
    menu.addItem (juce::String (juce::CharPointer_UTF8 ("Save as\xe2\x80\xa6")), [this] { saveLookAs(); });
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

// A menu of buses and outputs; `pick` gets "bus:NAME" or "output:NAME".
juce::PopupMenu MainComponent::appDestinations (const juce::String& current, std::function<void (const juce::String&)> pick)
{
    juce::PopupMenu m;
    m.addSectionHeader ("BUSES");
    for (auto& p : engine.getBuses())
        m.addItem (p->name, true, current == "bus:" + p->name, [pick, key = "bus:" + p->name] { pick (key); });
    m.addSectionHeader ("OUTPUTS");
    for (auto& b : engine.getMasters())
        m.addItem (b->name, true, current == "output:" + b->name, [pick, key = "output:" + b->name] { pick (key); });
    return m;
}

// A new rule for any app playing now, or one that already has a channel (none that has a rule).
juce::PopupMenu MainComponent::addAppRuleMenu()
{
    auto& rules = engine.appRules;
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
            add.addSubMenu (juce::File (exe).getFileNameWithoutExtension().toUpperCase(),
                            appDestinations ({}, [this, exe] (const juce::String& to) { engine.appRules.push_back ({ exe, to }); }));
    if (add.getNumItems() == 0)
        add.addItem ("No programs playing without a rule", false, false, nullptr);
    return add;
}

// APP RULES (FIND's): "DISCORD goes to CALLS" (AudioEngine::AppRule). Each rule's app and where it
// goes; a new rule for any app playing now, or one that already has a channel.
void MainComponent::showAppRules (juce::PopupMenu::Options where)
{
    auto appName = [] (const juce::String& exe) { return juce::File (exe).getFileNameWithoutExtension().toUpperCase(); };

    juce::PopupMenu menu;
    menu.addSectionHeader ("APP RULES");
    auto& rules = engine.appRules;
    for (int i = 0; i < (int) rules.size(); ++i)
    {
        auto sub = appDestinations (rules[(size_t) i].to, [this, i] (const juce::String& to)
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
    menu.addSeparator();
    menu.addSubMenu ("Add rule", addAppRuleMenu());
    menu.showMenuAsync (where);
}

// FFMPEG > PICK: ffmpeg.exe picked (it's used before the PATH's).
void MainComponent::pickFfmpeg()
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
}

// FFMPEG > USE PATH: the one picked forgotten, so the PATH's is used (if there's one).
void MainComponent::useFfmpegOnPath()
{
    Recorder::setFfmpeg ({});
    settings.removeValue ("ffmpegPath");
    settings.saveIfNeeded();
    logEvent ("recording: ffmpeg from the PATH");
}

// SETTINGS > AUDIO > CLOCK: switching the clock under a running mix (an interface's inputs and
// outputs going) isn't safe, so the choice is kept for the next start, and a restart offered.
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

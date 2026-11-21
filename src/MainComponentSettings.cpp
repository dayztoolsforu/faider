// MainComponent's SETTINGS tab (makeSettings): every setting, as the one list its pages and FIND
// are built from (SettingsRegistry.h), and what its rows do: looks, app rules, the clock, ffmpeg.
#include "MainComponent.h"
#include "ProductName.h"
#include "ui/AboutBox.h"
#include "ui/Theme.h"
#include "engine/PluginScan.h"
#include "StartWithWindows.h"
#include "Language.h"

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

    // A note that ends a sentence: a full stop, unless it has one ("...", or a language's own).
    juce::String sentenceEnd (const juce::String& note)
    {
        const auto last = note.getLastCharacter();
        return juce::String (".!?").containsChar (last) || last == 0x2026 || last == 0x3002 || last == 0xff01 || last == 0xff1f ? note : note + ".";
    }

    AudioEngine::AppRule* ruleFor (std::vector<AudioEngine::AppRule>& rules, const juce::String& exe)
    {
        for (auto& r : rules)
            if (r.exe.equalsIgnoreCase (exe))
                return &r;
        return nullptr;
    }
}

// The SETTINGS tab, after DEVICES: the app's preferences in one place, a page each, down the left.
void MainComponent::makeSettings()
{
    using namespace SettingsRegistry;
    auto remember = [this] (const char* key, const juce::var& value) { settings.setValue (key, value); settings.saveIfNeeded(); };
    std::vector<Setting> list;
    auto add = [&list] (Setting s) { list.push_back (std::move (s)); };

    //--------------------------------------------------------------------------
    // APP > GENERAL
    // LANGUAGE: the interface's (Language.h), each in its own name. Used from the next start.
    languageAtStart = settings.getValue ("language", "en");
    // (a page's, section's and setting's name is English, an identifier: NEEDS_TRANS, and the tab
    // and FIND translate it where they show it, SettingsRegistry::shown)
    add ({ .page = "General", .name = NEEDS_TRANS ("Language"),
           .description = TRANS ("The language of the interface, from the next start. The desk's keys stay in English. Changing it offers a restart."),
           .keywords = "translation locale english deutsch", .kind = Kind::dropdown,
           .value = [this] { return Language::nameOf (settings.getValue ("language", "en")); },
           .menu = [this] (juce::Component& c)
           {
               const auto chosen = settings.getValue ("language", "en");
               juce::PopupMenu menu;
               for (auto& e : Language::all())
                   menu.addItem (Language::nameOf (e.code), true, chosen == e.code, [this, code = juce::String (e.code)] { chooseLanguage (code); });
               menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&c));
           },
           .note = [this] { return settings.getValue ("language", "en") != languageAtStart
                                       ? TRANS ("The new language takes effect when %1 starts again.").replace ("%1", Product::name) : juce::String(); } });
    add ({ .page = "General", .section = NEEDS_TRANS ("Window"), .name = NEEDS_TRANS ("Start with Windows"),
           .description = TRANS ("Open %1 (in the tray) when you sign in.").replace ("%1", Product::name), .keywords = "startup login boot tray",
           .kind = Kind::toggle,
           .get = [] { return StartWithWindows::isOn(); }, .set = [] (bool on) { StartWithWindows::set (on); } });
    // The three stages (Ctrl +/- and the tab bar's - + step between them), or CUSTOM: any size from
    // 75 to 200%, kept until a stage is picked again.
    add ({ .page = "General", .section = NEEDS_TRANS ("Window"), .name = NEEDS_TRANS ("Zoom"),
           .description = TRANS ("The size of everything in the window. Ctrl + and Ctrl - step between 100, 125 and 150%; Custom picks any size from 75 to 200%."),
           .keywords = "scale size bigger smaller", .kind = Kind::segmented,
           .options = { "100%", "125%", "150%", TRANS (juce::CharPointer_UTF8 ("Custom\xe2\x80\xa6")) },
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
                                       ? TRANS ("Now: %1").replace ("%1", juce::String (juce::roundToInt (zoom * 100.0f)) + "%") : juce::String(); } });
    add ({ .page = "General", .section = NEEDS_TRANS ("Window"), .name = NEEDS_TRANS ("Look"),
           .description = TRANS ("The console's colours and typeface. A look is a file in %APPDATA%\\Faider\\looks; Save as writes the one in force to a new file."),
           .keywords = "theme colour color typeface font skin", .kind = Kind::dropdown,
           .value = [] { return juce::translate (Theme::currentLookName()); },   // (the built-in "Default" translated; a look file's name has no key, so it shows as it is)
           .menu = [this] (juce::Component& c) { showLooks (juce::PopupMenu::Options().withTargetComponent (&c)); } });
    add ({ .page = "General", .section = NEEDS_TRANS ("Help"), .name = NEEDS_TRANS ("Tips"),
           .description = TRANS ("A tip about each page, the first time it's shown. Show again shows each page's tip once more."),
           .keywords = "hints help first run", .kind = Kind::toggle,
           .get = [this] { return settings.getBoolValue ("tips", true); }, .set = [remember] (bool on) { remember ("tips", on); },
           .actions = { { says (TRANS ("Show again")),[this, remember] (juce::Component&)
           {
               for (auto* page : { "Mixer", "Routing", "Tools", "Devices", "Settings" })
                   settings.removeValue (juce::String ("tipSeen_") + page);
               remember ("tips", true);
               showTip ("Settings");
           } } } });

    //--------------------------------------------------------------------------
    // APP > NOTIFICATIONS
    add ({ .page = "Notifications", .name = NEEDS_TRANS ("Warn if I talk while cut"),
           .description = TRANS ("A notice when you speak into a cut mic (above -30 dBFS for half a second), from the tray if the window is hidden."),
           .keywords = "mute muted mic talking", .kind = Kind::toggle,
           .get = [this] { return warnMutedTalk; }, .set = [this, remember] (bool on) { warnMutedTalk = on; remember ("warnMutedTalk", on); } });
    for (auto [kind, name, text] : { std::tuple ("device", NEEDS_TRANS ("Devices connected or gone"), NEEDS_TRANS ("When a device is plugged in or goes, or Windows' default changes.")),
                                     std::tuple ("clip", NEEDS_TRANS ("A channel clipping"), NEEDS_TRANS ("When a channel's source or its output reaches full scale (each channel at most once in 10 s).")),
                                     std::tuple ("scene", NEEDS_TRANS ("Scenes recalled"), NEEDS_TRANS ("When a scene is recalled, by its key, a trigger or remote control.")),
                                     std::tuple ("macro", NEEDS_TRANS ("Macros run"), NEEDS_TRANS ("When a macro runs.")),
                                     std::tuple ("undo", NEEDS_TRANS ("Undo and redo"), NEEDS_TRANS ("What Ctrl+Z or Ctrl+Y undid or redid.")) })
        add ({ .page = "Notifications", .section = NEEDS_TRANS ("Corner notices"), .name = name,
               .description = TRANS ("%1 The event log has everything either way.").replace ("%1", juce::translate (text)), .keywords = "notice toast corner",
               .kind = Kind::toggle,
               .get = [this, key = juce::String ("notice_") + kind] { return settings.getBoolValue (key, true); },
               .set = [remember, key = juce::String ("notice_") + kind] (bool on) { remember (key.toRawUTF8(), on); } });

    //--------------------------------------------------------------------------
    // APP > STARTUP & CHECKS
    add ({ .page = "Startup & checks", .name = NEEDS_TRANS ("Pre-flight on start"),
           .description = TRANS ("A few seconds after %1 starts: the audio, the mic cable and route, cut mics. Shown only if something's wrong.").replace ("%1", Product::name),
           .keywords = "preflight check setup", .kind = Kind::toggle,
           .get = [this] { return settings.getBoolValue ("preflightOnStart", true); },
           .set = [remember] (bool on) { remember ("preflightOnStart", on); } });
    add ({ .page = "Startup & checks", .section = NEEDS_TRANS ("Run now"), .name = NEEDS_TRANS ("Pre-flight"),
           .description = TRANS ("Check the setup now: the audio, the mic cable and route, cut mics, things heard twice, Windows audio."),
           .keywords = "preflight check", .actions = { { says (TRANS ("Run pre-flight")), [this] (juce::Component&) { showPreflight (false); }, true } } });
    add ({ .page = "Startup & checks", .section = NEEDS_TRANS ("Run now"), .name = NEEDS_TRANS ("Zero the desk"),
           .description = TRANS ("Things left set that change what's heard: solos, key listen, gains far off, outputs cut. Each with its fix."),
           .keywords = "zero desk reset solo", .actions = { { says (TRANS ("Check")), [this] (juce::Component&) { showZero(); } } } });
    add ({ .page = "Startup & checks", .section = NEEDS_TRANS ("Run now"), .name = NEEDS_TRANS ("Setup: build a mix"),
           .description = TRANS ("Four questions that build your mix: where you listen, your microphones, how apps hear you, your programs. It adds what's missing and never removes anything."),
           .keywords = "setup wizard first start build mix devices microphones", .actions = { { says (TRANS ("Run setup")), [this] (juce::Component&) { showSetup(); } } } });

    //--------------------------------------------------------------------------
    // APP > UPDATES & ABOUT
    add ({ .page = "Updates & about", .name = NEEDS_TRANS ("Check for updates"),
           .description = TRANS ("At most once a day, half a minute after start: one request to GitHub with the app's name and version, nothing else. Nothing is downloaded or installed."),
           .keywords = "update version github new", .kind = Kind::toggle,
           .get = [this] { return settings.getBoolValue ("checkForUpdates", true); },
           .set = [remember] (bool on) { remember ("checkForUpdates", on); } });
    add ({ .page = "Updates & about", .name = NEEDS_TRANS ("Version"),
           .description = TRANS ("The version running now. Check now asks GitHub at once; when a newer one is out, its release page is a click away."),
           .keywords = "update release", .status = says (juce::String (Product::name) + " " + JUCE_APPLICATION_VERSION_STRING),
           .actions = { { [this] { return newVersion.isNotEmpty() ? TRANS (juce::CharPointer_UTF8 ("Get %1 %2\xe2\x80\xa6")).replace ("%1", Product::name).replace ("%2", newVersion)
                                                              : TRANS ("Check now"); },
                          [this] (juce::Component&) { if (newVersion.isNotEmpty()) openRelease(); else checkForUpdates (true); } } },
           .note = [this] { return updates != nullptr && updates->isChecking() ? TRANS (juce::CharPointer_UTF8 ("Asking GitHub\xe2\x80\xa6")) : juce::String(); } });
    // (%1 in a name is the product's name, SettingsRegistry::shown)
    add ({ .page = "Updates & about", .section = NEEDS_TRANS ("About"), .name = NEEDS_TRANS ("About %1"),
           .description = TRANS ("The version and the credits."), .keywords = "credits licence license",
           .actions = { { says (TRANS ("Open")), [] (juce::Component&) { AboutBox::show(); } } } });
    add ({ .page = "Updates & about", .section = NEEDS_TRANS ("About"), .name = NEEDS_TRANS ("Privacy"),
           .description = TRANS ("What %1 keeps on this PC, and the one request it makes.").replace ("%1", Product::name), .keywords = "data privacy policy",
           .actions = { { says (TRANS ("Read policy")), [] (juce::Component&) { juce::URL ("https://github.com/dayztoolsforu/faider/blob/main/PRIVACY.md").launchInDefaultBrowser(); } } } });
    add ({ .page = "Updates & about", .section = NEEDS_TRANS ("About"), .name = NEEDS_TRANS ("Problem report"),
           .description = TRANS ("The logs, the last crash and the versions in a .zip you can send. Nothing is sent by itself."),
           .keywords = "crash bug log support zip",
           .actions = { { says (TRANS (juce::CharPointer_UTF8 ("Save report\xe2\x80\xa6"))), [this] (juce::Component&) { saveProblemReport(); } } } });

    //--------------------------------------------------------------------------
    // AUDIO > AUDIO
    // CLOCK: what sets the clock: the interface (ASIO, Faider's own pick, as without a choice), a
    // Windows playback device, or the built-in clock. Used from the next start, as --clock is;
    // --clock on the command line still wins.
    auto clockName = [this] (const juce::String& c)
    {
        if (c == "builtin")
            return TRANS ("The built-in clock");
        if (! c.startsWith (AudioEngine::windowsClockPrefix))
            return TRANS ("The interface (ASIO)");
        const auto name = c.fromFirstOccurrenceOf (AudioEngine::windowsClockPrefix, false, false);
        for (auto& e : endpoints)
            if (e.api == "WASAPI" && e.isOutput && e.isActive() && e.name == name)
                return endpointDisplayName (e);
        return TRANS ("%1 (not connected)").replace ("%1", name);
    };
    add ({ .page = "Audio", .section = NEEDS_TRANS ("Clock"), .name = NEEDS_TRANS ("Clock"),
           .description = TRANS ("What sets the clock, from the next start: the interface (ASIO), a Windows playback device, or the built-in clock. Changing it offers a restart."),
           .keywords = "master sample rate asio wasapi built-in", .kind = Kind::dropdown,
           .value = [this, clockName] { return clockName (settings.getValue ("clock")); },
           .menu = [this] (juce::Component& c)
           {
               const auto chosen = settings.getValue ("clock");
               juce::PopupMenu menu;
               menu.addItem (TRANS ("The interface (ASIO)"), true, chosen.isEmpty(), [this] { chooseClock ({}); });
               for (auto& e : endpoints)
                   if (e.api == "WASAPI" && e.isOutput && e.isActive() && ! engine.isDeviceDisabled (e.name))
                   {
                       const auto key = AudioEngine::windowsClockPrefix + e.name;
                       menu.addItem (endpointDisplayName (e), true, chosen == key, [this, key] { chooseClock (key); });
                   }
               if (const auto name = chosen.fromFirstOccurrenceOf (AudioEngine::windowsClockPrefix, false, false);
                   name.isNotEmpty() && std::none_of (endpoints.begin(), endpoints.end(), [&name] (const EndpointInfo& e) { return e.isOutput && e.isActive() && e.name == name; }))
                   menu.addItem (TRANS ("%1 (not connected)").replace ("%1", name), true, true, [this, chosen] { chooseClock (chosen); });   // (the one chosen, while it's away)
               menu.addItem (TRANS ("The built-in clock"), true, chosen == "builtin", [this] { chooseClock ("builtin"); });
               menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&c));
           },
           .note = [this]
           {
               const auto device = engine.getClock() == AudioEngine::Clock::asio ? engine.getInterfaceAdapter() : engine.getClockDevice();
               // (the clock's name is English in the engine and the log: translated here)
               const auto now = juce::translate (engine.getClockName()) + (device.isNotEmpty() ? " (" + device + ")" : juce::String());
               if (clockFromCommandLine)
                   return TRANS ("Now: %1. --clock on the command line sets it for this run.").replace ("%1", now);
               if (settings.getValue ("clock") != clockChosenAtStart)
                   return TRANS ("Now: %1. The new choice takes effect when %2 starts again.").replace ("%2", Product::name).replace ("%1", now);
               return TRANS ("Now: %1.").replace ("%1", now);
           } });

    // WINDOWS AUDIO: Windows' audio engine (audiodg.exe) and its priority now (read every 5 s:
    // watchWindowsAudio), and BOOST (AudioBoost.h), which Windows asks about first.
    const bool canBoost = AudioBoost::canElevate();
    add ({ .page = "Audio", .section = NEEDS_TRANS ("Clock"), .name = NEEDS_TRANS ("Windows audio"),
           .description = TRANS ("Windows' audio engine (audiodg.exe) at High priority instead of Normal can stop crackle on Windows devices and captured apps. "
                                 "Windows asks for an administrator's approval each time, and a restart of Windows audio undoes it (you're told when)."),
           .keywords = "boost audiodg priority crackle dropouts back to normal",
           .status = [this]
           {
               const auto& s = windowsAudio;
               if (s.pid == 0)
                   return TRANS ("Not running");
               if (s.priority == AudioBoost::Priority::unknown)
                   return TRANS ("Priority unknown");
               // (priorityName is the log's capitals: the chip's words are their own keys, in sentence case)
               auto priorityText = [] (AudioBoost::Priority p) -> const char*
               {
                   switch (p)
                   {
                       case AudioBoost::Priority::idle:        return NEEDS_TRANS ("Idle");
                       case AudioBoost::Priority::belowNormal: return NEEDS_TRANS ("Below normal");
                       case AudioBoost::Priority::normal:      return NEEDS_TRANS ("Normal");
                       case AudioBoost::Priority::aboveNormal: return NEEDS_TRANS ("Above normal");
                       case AudioBoost::Priority::high:        return NEEDS_TRANS ("High");
                       case AudioBoost::Priority::realtime:    return NEEDS_TRANS ("Realtime");
                       case AudioBoost::Priority::unknown:     break;
                   }
                   return NEEDS_TRANS ("Unknown");
               };
               const auto priority = juce::translate (priorityText (s.priority));
               return s.pinnedCore >= 0 ? TRANS ("%1 priority, core %2").replace ("%2", juce::String (s.pinnedCore)).replace ("%1", priority)
                                        : TRANS ("%1 priority").replace ("%1", priority);
           },
           .actions = { { [this] { return boostingWindowsAudio ? TRANS (juce::CharPointer_UTF8 ("Waiting\xe2\x80\xa6"))
                                          : settings.getBoolValue ("boostAudiodg") && AudioBoost::isBoosted (windowsAudio.priority) ? TRANS ("Stop boosting") : TRANS ("Boost"); },
                          [this] (juce::Component&)
                          {
                              if (settings.getBoolValue ("boostAudiodg") && AudioBoost::isBoosted (windowsAudio.priority))
                                  stopBoostingWindowsAudio();
                              else
                                  boostWindowsAudio();
                          }, false, [this] { return ! boostingWindowsAudio; } },
                        { says (TRANS ("Back to normal")), [this] (juce::Component&) { unboostWindowsAudio(); }, false,
                          [this] { return ! boostingWindowsAudio && windowsAudio.pid != 0 && (windowsAudio.priority != AudioBoost::Priority::normal || windowsAudio.pinnedCore >= 0); } } },
           .note = [this] { return boostNote.isNotEmpty() ? sentenceEnd (boostNote) : TRANS ("ASIO interfaces go around it, so they aren't affected."); },
           .warning = [canBoost] { return canBoost ? juce::String() : TRANS ("Needs %1 installed for all users").replace ("%1", Product::name); } });
    add ({ .page = "Audio", .section = NEEDS_TRANS ("Clock"), .name = NEEDS_TRANS ("Pin to one core"),
           .description = TRANS ("With Boost, Windows audio is held to the last core. Only for crackle on a USB headset; off unless you pick it. It takes effect from the next Boost."),
           .keywords = "boost affinity usb headset", .kind = Kind::toggle,
           .get = [this] { return settings.getBoolValue ("boostPinCore"); },
           .set = [this] (bool pin)
           {
               settings.setValue ("boostPinCore", pin);
               settings.saveIfNeeded();
               boostNote = pin ? TRANS ("It's pinned to the last core from the next Boost")
                               : TRANS ("From the next Boost it isn't pinned (a pin already made stays until Windows audio restarts)");
           } });
    add ({ .page = "Audio", .section = NEEDS_TRANS ("Meters"), .name = NEEDS_TRANS ("Channel meter point"),
           .description = TRANS ("Where every channel's meter reads: IN, the source as it comes in; PRE, after the effects; POST, after the fader."),
           .keywords = "meter level in pre post", .kind = Kind::segmented,
           .options = { "IN", "PRE", "POST" },
           .chosen = [] { return AudioEngine::meterPoint.load(); }, .choose = [] (int i) { AudioEngine::meterPoint = i; } });
    add ({ .page = "Audio", .section = NEEDS_TRANS ("Meters"), .name = NEEDS_TRANS ("Meter standard"),
           .description = TRANS ("How every meter reads: peak, true peak, RMS, VU, PPM, K-system or momentary loudness."),
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
        add ({ .page = "Audio", .section = NEEDS_TRANS ("Plugins"), .name = NEEDS_TRANS ("Plugin test time"),
               .description = TRANS ("How long a new plugin has to load in its test before it fails. Slow plugins may need 60 or 120 s."),
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
           .description = TRANS ("MP3, AAC, Opus and ALAC recording need ffmpeg.exe: pick it, or have it on the PATH. One picked is used before the PATH's."),
           .keywords = "ffmpeg mp3 aac opus alac recorder", .status = [] { const auto f = Recorder::ffmpeg(); return f.existsAsFile() ? f.getFullPathName() : TRANS ("Not found"); },
           .path = true,
           .actions = { { says (TRANS (juce::CharPointer_UTF8 ("Pick\xe2\x80\xa6"))), [this] (juce::Component&) { pickFfmpeg(); } },
                        { says (TRANS ("Use PATH")), [this] (juce::Component&) { useFfmpegOnPath(); }, false, [this] { return settings.getValue ("ffmpegPath").isNotEmpty(); } } },
           .note = [this] { return settings.getValue ("ffmpegPath").isNotEmpty() ? TRANS ("Picked: used before the one on the PATH.")
                                   : Recorder::ffmpeg().existsAsFile() ? TRANS ("Found on the PATH.") : juce::String(); } });

    //--------------------------------------------------------------------------
    // MIX > APPS
    add ({ .page = "Apps", .name = NEEDS_TRANS ("App rules"),
           .description = TRANS ("Which bus or output a program goes to when it starts playing. Removing a program's channel removes its rule."),
           .keywords = "program application discord game route rule", .kind = Kind::custom,
           .actions = { { says (TRANS ("Add rule")), [this] (juce::Component& c) { addAppRuleMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&c)); }, true } },
           .make = [this]
           {
               return std::make_unique<SettingsUi::Table> (std::vector<SettingsUi::Table::Column> { { TRANS ("Program"), 0.36f }, { TRANS ("Goes to"), 0.30f }, { {}, 0.34f } },
                                                           TRANS ("No rules yet"), [this]
               {
                   std::vector<SettingsUi::Table::Line> lines;
                   // (each rule by its program: the list may change while a menu is open)
                   for (const auto& r : engine.appRules)
                   {
                       const auto where = r.to.fromFirstOccurrenceOf (":", false, false);
                       const auto to = r.to.startsWith ("bus:") ? TRANS ("%1 (bus)").replace ("%1", where) : TRANS ("%1 (output)").replace ("%1", where);
                       lines.push_back ({ { juce::File (r.exe).getFileNameWithoutExtension(), to },
                                          { { TRANS ("Change"),[this, exe = r.exe] (juce::Component& c)
                                              {
                                                  if (const auto* rule = ruleFor (engine.appRules, exe))
                                                      appDestinations (rule->to, [this, exe] (const juce::String& dest)
                                                      {
                                                          if (auto* now = ruleFor (engine.appRules, exe))
                                                              now->to = dest;
                                                      }).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&c));
                                              } },
                                            { TRANS ("Delete"), [this, exe = r.exe] (juce::Component&)
                                              {
                                                  auto& rules = engine.appRules;
                                                  rules.erase (std::remove_if (rules.begin(), rules.end(), [&exe] (const AudioEngine::AppRule& x) { return x.exe.equalsIgnoreCase (exe); }), rules.end());
                                              } } } });
                   }
                   return lines;
               });
           },
           .wide = true });

    //--------------------------------------------------------------------------
    // MIX > SESSIONS
    add ({ .page = "Sessions", .name = NEEDS_TRANS ("This mix"),
           .description = TRANS ("Keep this mix under a name, or save it to a file to share or move."), .keywords = "save export session",
           .actions = { { says (TRANS (juce::CharPointer_UTF8 ("Save as\xe2\x80\xa6"))), [this] (juce::Component&) { saveSessionAs(); }, true },
                        { says (TRANS (juce::CharPointer_UTF8 ("Export\xe2\x80\xa6"))), [this] (juce::Component&) { exportSession(); } } } });
    add ({ .page = "Sessions", .section = NEEDS_TRANS ("Saved sessions"), .name = NEEDS_TRANS ("Saved sessions"),
           .description = TRANS ("Opening one restarts %1 with it. Import opens a session from a file.").replace ("%1", Product::name),
           .keywords = "open import export load named session", .kind = Kind::custom,
           .actions = { { says (TRANS (juce::CharPointer_UTF8 ("Import\xe2\x80\xa6"))), [this] (juce::Component&) { importSession(); } } },
           .make = [this]
           {
               savedSessionsStale = true;   // (the page shown: the folder read afresh)
               return std::make_unique<SettingsUi::Table> (std::vector<SettingsUi::Table::Column> { { TRANS ("Name"), 0.40f }, { TRANS ("Saved"), 0.30f }, { {}, 0.30f } },
                                                           TRANS ("No saved sessions yet"), [this]
               {
                   std::vector<SettingsUi::Table::Line> lines;
                   for (auto& s : savedSessions())
                       lines.push_back ({ { s.file.getFileNameWithoutExtension(), s.saved.formatted ("%d %b %Y, %H:%M") },
                                          { { TRANS ("Open"), [this, f = s.file] (juce::Component&) { restoreSession (f, f.getFileNameWithoutExtension()); } },
                                            { TRANS ("Export"), [this, f = s.file] (juce::Component&) { exportSessionFile (f); } } } });
                   return lines;
               });
           },
           .wide = true });
    add ({ .page = "Sessions", .section = NEEDS_TRANS ("Saved sessions"), .name = NEEDS_TRANS ("Session timeline"),
           .description = TRANS ("Every kept version of this mix by date: bring back the whole mix (it restarts), or put one channel back as it was."),
           .keywords = "history restore earlier version backup",
           .actions = { { says (TRANS ("Open timeline")),[this] (juce::Component& c) { showTimeline (juce::PopupMenu::Options().withTargetComponent (&c)); } } } });

    //--------------------------------------------------------------------------
    // CONTROL > MIDI & OSC
    auto* remote = &engine.getRemote();
    add ({ .page = "MIDI & OSC", .section = "MIDI", .name = NEEDS_TRANS ("MIDI map"),
           .description = TRANS ("Click a control on screen, then move a knob or key on your controller. Ctrl+L starts it anywhere; Esc finishes."),
           .keywords = "learn controller midi map bind", .actions = { { [this] { return midiMap != nullptr ? TRANS ("Finish mapping") : TRANS ("Start mapping"); },
                                                                       [this] (juce::Component&) { toggleMidiMap(); }, true } },
           .passThrough = true });
    add ({ .page = "MIDI & OSC", .section = "MIDI", .name = NEEDS_TRANS ("Inputs and bindings"),
           .description = TRANS ("Each MIDI input on or off, and what its controls are bound to."), .keywords = "midi device controller bindings",
           .actions = { { says (TRANS ("Open")), [this] (juce::Component&) { openMidiPanel(); } } },
           .note = [remote] { return (remote->mappingCount() == 1 ? TRANS ("%1 binding.") : TRANS ("%1 bindings.")).replace ("%1", juce::String (remote->mappingCount())); },
           .passThrough = true });   // (MIDI & OSC's rows work while MIDI MAP is on, as the CONTROL page's did)
    add ({ .page = "MIDI & OSC", .section = "OSC", .name = "OSC",
           .description = TRANS ("Listen for OSC messages from programs on this PC (never from the network)."), .keywords = "osc remote network",
           .kind = Kind::toggle,
           .get = [remote] { return remote->getOscPort() > 0; },
           .set = [remote] (bool on) { remote->setOscPort (on ? remote->getOscPreferredPort() : 0); },
           .note = [remote] { return remote->getOscPort() > 0 ? TRANS ("Listening on port %1.").replace ("%1", juce::String (remote->getOscPort())) : juce::String(); },
           .passThrough = true });
    // OSC PORT: a number from 1024 to 65535; OSC moves there if it's on, else uses it when it's switched on.
    add ({ .page = "MIDI & OSC", .section = "OSC", .name = NEEDS_TRANS ("OSC port"),
           .description = TRANS ("The port OSC listens on, 1024 to 65535. If OSC is on, it moves there at once."), .keywords = "osc port",
           .kind = Kind::custom,
           .make = [remote]
           {
               return std::make_unique<SettingsUi::NumberField> ([remote] { return remote->getOscPreferredPort(); }, [remote] (int wanted)
               {
                   if (wanted < 1024 || wanted > 65535)
                       juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, TRANS ("OSC port"), TRANS ("A port is a number from 1024 to 65535."));
                   else if (! remote->setOscPreferredPort (wanted))
                       juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, TRANS ("OSC port"),
                                                               TRANS ("Port %1 couldn't be opened (another program may have it). OSC is off.").replace ("%1", juce::String (wanted)));
               }, 5);
           },
           .passThrough = true });

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
                // (the group isn't shown: only the page's same-list check reads it, so it stays English)
                keys.push_back ({ "Pads", pad.name.isNotEmpty() ? TRANS ("Pad %1: %2").replace ("%1", juce::String (i + 1)).replace ("%2", pad.name)
                                                                : TRANS ("Pad %1").replace ("%1", juce::String (i + 1)),
                                  pad.hotkey, [this, i] (const juce::KeyPress& k) { return pads->setHotkey (i, k); },
                                  TRANS ("Tools > Pads"), pads->hotkeyRegistered (i) });
        auto& macros = engine.getMacros().list;
        for (int i = 0; i < (int) macros.size(); ++i)
            keys.push_back ({ "Macros", TRANS ("Macro: %1").replace ("%1", macros[(size_t) i].name), macros[(size_t) i].hotkey,
                              [this, i] (const juce::KeyPress& k) { return pads->setMacroHotkey (i, k); },
                              TRANS ("Settings > Macros"), pads->hotkeyRegistered (PadsPanel::macroHotkeyBase + i) });
        auto& scenes = engine.getScenes().list;
        for (int i = 0; i < (int) scenes.size(); ++i)
            keys.push_back ({ "Scenes", TRANS ("Scene: %1").replace ("%1", scenes[(size_t) i].name), scenes[(size_t) i].hotkey,
                              [this, i] (const juce::KeyPress& k)
                              {
                                  const bool ok = pads->setSceneHotkey (i, k);
                                  if (ok && sceneBar != nullptr) sceneBar->repaint();
                                  return ok;
                              },
                              TRANS ("Right-click the scene's key"), pads->hotkeyRegistered (PadsPanel::sceneHotkeyBase + i) });
        keys.push_back ({ "Tape", TRANS ("Save instant replay"), tape->replayHotkey, [this] (const juce::KeyPress& k)
        {
            const auto key = k.isValid() ? k.getTextDescription() : juce::String();
            if (! tape->setReplayHotkey (key))
                return false;
            tape->replayHotkey = key;
            return true;
        }, TRANS ("Tools > Tape (right-click Save)"), pads->hotkeyRegistered (PadsPanel::extraHotkeyBase + 1) });
        keys.push_back ({ "Anywhere", TRANS ("Show / hide the in-game overlay"), "ctrl + shift + O", nullptr, TRANS ("Anywhere"),
                          pads->hotkeyRegistered (PadsPanel::extraHotkeyBase + 2), { "ctrl + shift + O" } });
        // (the window's own keys: a hotkey may not take one. Their names are PadsPanel's English capitals;
        // the keys are the sentence-case forms shown here, so the extractor sees them in this list)
        static constexpr const char* fixedNames[] = { NEEDS_TRANS ("Find (command palette)"), NEEDS_TRANS ("Midi map"), NEEDS_TRANS ("Undo / redo"),
                                                      NEEDS_TRANS ("Undo history"), NEEDS_TRANS ("Zoom in / out / 100%") };
        (void) fixedNames;
        for (auto& fixed : PadsPanel::fixedKeys())
            keys.push_back ({ "In Faider", juce::translate (sentence (fixed.name)),fixed.shown, nullptr, TRANS ("In %1").replace ("%1", Product::name), true, fixed.keys });
        // (the arrow keys' names are words, so they're translated; M and S are the keys themselves)
        keys.push_back ({ "On the mixer", TRANS ("Select the channel left / right"), TRANS ("left / right"), nullptr, TRANS ("On the mixer") });
        keys.push_back ({ "On the mixer", TRANS ("CUT / SOLO the selected channel"), "M  /  S", nullptr, TRANS ("On the mixer") });
        keys.push_back ({ "On the mixer", TRANS ("Its fader up / down 0.5 dB (Shift: 3 dB)"), TRANS ("up / down"), nullptr, TRANS ("On the mixer") });
        return keys;
    });

    // (the group and page names are identifiers, as the rows' .page: NEEDS_TRANS here, translated where shown)
    std::vector<Page> pages {
        { NEEDS_TRANS ("App"), NEEDS_TRANS ("General"), TRANS ("The language, the window, how it looks, and the tips.") },
        { NEEDS_TRANS ("App"), NEEDS_TRANS ("Notifications"), TRANS ("What %1 tells you about in the window's corner.").replace ("%1", Product::name) },
        { NEEDS_TRANS ("App"), NEEDS_TRANS ("Startup & checks"), TRANS ("What %1 checks when it starts, and the same checks now.").replace ("%1", Product::name) },
        { NEEDS_TRANS ("App"), NEEDS_TRANS ("Updates & about"), TRANS ("The version, the daily update check, and a report for when something goes wrong.") },
        { NEEDS_TRANS ("Audio"), NEEDS_TRANS ("Audio"), TRANS ("What sets the clock, Windows' own audio engine, the meters and the plugin test.") },
        { NEEDS_TRANS ("Audio"), NEEDS_TRANS ("Recording"), TRANS ("What the recorder needs for compressed formats.") },
        { NEEDS_TRANS ("Mix"), NEEDS_TRANS ("Apps"), TRANS ("Where programs go when they start playing.") },
        { NEEDS_TRANS ("Mix"), NEEDS_TRANS ("Sessions"), TRANS ("Mixes kept under a name, and the earlier versions of this one.") },
        { NEEDS_TRANS ("Control"), "MIDI & OSC", TRANS ("Controllers, and other programs on this PC, moving the desk.") },
        { NEEDS_TRANS ("Control"), NEEDS_TRANS ("Hotkeys"), TRANS ("Every global hotkey in one place. They work while a game has focus."), hotkeysPage.get(), false, "keys shortcut keyboard" },
        { NEEDS_TRANS ("Control"), NEEDS_TRANS ("Macros"), TRANS ("Lists of actions run from a hotkey, a pad, a trigger or remote control."), &pads->getMacrosPage(), true, "macro action trigger push to talk" },
    };

    settingsTab = std::make_unique<SettingsTab> (std::move (pages), std::move (list));
    tabs.addTab (NEEDS_TRANS ("Settings"),juce::Colours::transparentBlack, settingsTab.get(), false);
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
        menu.addItem (juce::translate (name), true, name == current, [this, name]   // (shown translated: the built-in "Default"; `name` stays English for useLook)
        {
            if (! Theme::useLook (name))
                return;
            settings.setValue ("look", name);
            settings.saveIfNeeded();
            repaintEverything();
        });
    menu.addSeparator();
    menu.addItem (TRANS (juce::CharPointer_UTF8 ("Save as\xe2\x80\xa6")), [this] { saveLookAs(); });
    menu.showMenuAsync (where);
}

void MainComponent::saveLookAs()
{
    auto* w = new juce::AlertWindow (TRANS ("SAVE THIS LOOK AS"), {}, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", "");
    w->addButton (TRANS ("SAVE"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (TRANS ("CANCEL"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w] (int result)
    {
        const auto wanted = w->getTextEditorContents ("name").trim();
        if (result == 1 && wanted.isNotEmpty() && ! Theme::saveLookAs (wanted))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, TRANS ("LOOK"), TRANS ("The look could not be written."));
    }), true);
}

// A menu of buses and outputs; `pick` gets "bus:NAME" or "output:NAME".
juce::PopupMenu MainComponent::appDestinations (const juce::String& current, std::function<void (const juce::String&)> pick)
{
    juce::PopupMenu m;
    m.addSectionHeader (TRANS ("BUSES"));
    for (auto& p : engine.getBuses())
        m.addItem (p->name, true, current == "bus:" + p->name, [pick, key = "bus:" + p->name] { pick (key); });
    m.addSectionHeader (TRANS ("OUTPUTS"));
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
        add.addItem (TRANS ("No programs playing without a rule"), false, false, nullptr);
    return add;
}

// FFMPEG > PICK: ffmpeg.exe picked (it's used before the PATH's).
void MainComponent::pickFfmpeg()
{
    const auto start = Recorder::ffmpeg().existsAsFile() ? Recorder::ffmpeg() : juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory);
    chooser = std::make_unique<juce::FileChooser> (TRANS ("Pick ffmpeg.exe"), start, "ffmpeg.exe");
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
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::NoIcon, TRANS ("CLOCK"),
                                        TRANS ("The new clock takes effect when %1 starts again. Restart now? The sound stops for a few seconds.").replace ("%1", Product::name),
                                        TRANS ("RESTART NOW"), TRANS ("LATER"), nullptr,
                                        juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                                        { if (result == 1 && safe != nullptr) safe->restartFaider(); }));
}

// SETTINGS > GENERAL > LANGUAGE: TRANS's table is set before any window is made (Main.cpp), so the
// choice is kept for the next start, and a restart offered.
void MainComponent::chooseLanguage (const juce::String& code)
{
    if (code == settings.getValue ("language", "en"))
        return;
    settings.setValue ("language", code);
    settings.saveIfNeeded();
    logEvent ("language: from the next start, " + code);
    if (code == languageAtStart)
        return;   // (back to what this run started in)
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::NoIcon, TRANS ("LANGUAGE"),
                                        TRANS ("The new language takes effect when %1 starts again. Restart now? The sound stops for a few seconds.").replace ("%1", Product::name),
                                        TRANS ("RESTART NOW"), TRANS ("LATER"), nullptr,
                                        juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                                        { if (result == 1 && safe != nullptr) safe->restartFaider(); }));
}

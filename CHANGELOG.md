# Changelog

What changed in each version of Faider, newest first. Downloads are on the
[Releases](https://github.com/dayztoolsforu/faider/releases) page.

## Unreleased

**New**
- Every channel strip's DYNAMICS has **DE-ESS** (its switch, threshold, frequency and reduction
  LEDs) and **AUTO-LEVEL** (its switch, target and range) under the gate.

**Fixed**
- The de-esser takes the sibilance down by as much as it says: it splits the sound with a crossover
  whose bands stay in phase, so 12 dB of reduction on its screen is about 9 dB off a loud "s" at 7 kHz
  (it was nearer 1.5 dB).

## 0.2.0 (21 November 2026)

**New**
- The interface in 13 languages: **Language** in SETTINGS > General picks English, Deutsch,
  Español, Français, Italiano, 日本語, 한국어, Nederlands, Polski, Português (Brasil), Svenska, Türkçe or
  简体中文, from the next start (**RESTART NOW** offers it at once). The first start follows Windows'
  display language when it's one of them. The desk's keys, the event log and product names stay
  in English.
- Level meters are columns of round LEDs, on every strip and in the mini mixer, drawn to SETTINGS >
  Audio > Meter standard: each standard has its own scale, colours and number of LEDs.
- **COMPACT** in the Mixer's sidebar (and **COMPACT DESK** / **FULL DESK** in FIND) folds every
  strip to its name bar and fader, so the desk fits on a
  short screen. It is remembered.
- **COLOUR** on an output's right-click menu, shown along its name bar and on its Routing block.
- ADD APP, ADD CHANNEL and SETUP also offer programs that are running but **NOT PLAYING YET**. The
  channel is made at once and hears the program from the moment it plays.
- **MIX-MINUS AND STEMS...** is on an output's right-click menu, **HEAR WHAT THEY HEAR** on
  PRE-FLIGHT's MIC OUTPUT row, and **SESSION TIMELINE** and **ZERO DESK** in SETTINGS, as well as
  in FIND.
- SETTINGS > HOTKEYS says what each group's keys do and where else they're set, and marks in red a
  key given to two things or one Windows refused.
- The installer can install for you only, without an administrator (**Install for me only**, or
  `/CURRENTUSER`). For all users is still the default.
- A privacy policy ([PRIVACY.md](PRIVACY.md)), an [OSC reference](docs/OSC.md) and a
  [FAQ](docs/FAQ.md). The manual lists the installer's silent switches and every command-line
  option.

**Changed**
- SETTINGS is a sidebar of pages (General, Notifications, Startup & checks, Updates & about, Audio,
  Recording, Apps, Sessions, MIDI & OSC, Hotkeys, Macros) with a search box. Each setting says what
  it does under its name, and FIND finds every setting by name. App rules and saved sessions are
  tables, a saved session can be exported, FFmpeg has **Pick...** and **Use PATH**, and MIDI MAP is
  started with **Start mapping**.
- CLOCK and WINDOWS AUDIO moved from DEVICES to SETTINGS > Audio. The sidebar's METER cards are
  gone: SETTINGS > Audio > Channel meter point sets where the meters read.
- A program's channel is made one way, from ADD APP, ADD CHANNEL, SEND AN APP TO THE MIC, SETUP or
  an app rule. They pick from one list of programs, each listed once, by name.
- FFmpeg gets ten minutes, or the recording's own length, to convert a recording. After that it is
  stopped, the WAV is kept and the event log says so.
- The DEVICES tab asks Windows about the cable and switched-off devices directly, so it works in
  any display language.
- **BOOST** and **BACK TO NORMAL** need Faider installed for all users (Program Files). From a
  copy installed for one user or built from source, Windows isn't asked and Faider says why: files
  in such a folder can be changed by any program, and would run as administrator.
- A hotkey that a pad, macro, scene, instant replay or one of Faider's fixed keys already has is
  refused wherever it's set, and the note names what has it.
- A click on the tray icon opens the mini mixer a moment later; a double-click opens only the window.
- The update check also runs while Faider keeps running, a day after GitHub last answered. A check
  that got no answer is tried again an hour later.
- An app channel's fader sets its level, and its strip has the DUCK lamp.
- PRE-FLIGHT's rows follow what changes while it is open.
- Uninstalling asks Faider to quit only once the uninstall is confirmed, and waits up to 15 seconds
  for it to close before ending it.

**Removed**
- The SIMPLE desk and the SIMPLE / PRO switch: the Mixer is always the whole console, and
  **COMPACT** is its small view. SETUP stays (a first start, **Setup: build a mix** in SETTINGS > Startup & checks, and FIND). A
  mix or settings left on SIMPLE open on the whole console.

**Fixed**
- BOOST WINDOWS AUDIO works from Windows' prompt.
- A mix file with a wrong source kind, numbers that aren't numbers or rack cables that loop loads
  safely: levels stay within their controls' ranges and the rack runs in the order it's placed.
- The clock isn't started on a device the mix excludes, and a Windows clock that goes is followed by
  another Windows device.
- The commands DEVICES runs as administrator name Windows' own programs by their full path.
- BOOST only reads and changes Windows' own audiodg.exe.
- Conversions, replay files and plugin tests still running in the background are waited for as
  Faider closes.
- The audio lock is held for less time when the mix's structure changes.
- The desk's LED meters redraw as the chosen meter standard moves them (VU, RMS, K and loudness, the
  quietest LEDs, true peak's top three) and PEAK HOLD drops on time; COMPACT ends a knob or fader
  turn cleanly and the Mixer's top bar keeps the view's width; a strip layout whose sections differ
  from mixer.json's is not used.
- SETTINGS: **Setup: build a mix** in Startup & checks runs SETUP; a typed OSC port is kept until
  **Apply**; HOTKEYS takes a key for the line clicked even as the list changes; MIDI & OSC's rows
  work during MIDI MAP; the search finds pages by name or keyword (hotkey, shortcut, macro); the
  saved sessions list reads the folder less often; Export asks before replacing another file; APP
  RULES' **Change** and **Delete** act on the rule shown; FIND lists each name once; the Windows
  audio note no longer ends in four dots.
- Translations: text in capitals is written as its language writes it (Turkish İ, German Ä, French
  É), device notices are translated, a look you named shows its own name, the buffer menu's
  milliseconds use the clock's rate, and someone updating from before languages stays in English.

## 0.1.6 (8 November 2026)

**New**
- A **NOISE** key on every channel strip switches noise reduction. The strip's menu picks NEURAL or
  SPECTRAL and how deep it goes.
- **DUCK UNDER** on a channel's, bus's or app channel's menu dips it while another channel is
  heard. Its DUCK lamp lights while it does.
- **KEY FROM** picks the channel that EXT's dynamics listen to.
- **HEADPHONE EQ** on an output's menu corrects the headphones it plays on, by model.
- **ALERT IF SILENT** (after 10 s, 30 s, 1 min or 5 min) and **ALERT IF CLIPPING** on an output's
  menu flash the tray icon and write to the event log.
- **IN THE MINI MIXER** picks the strips the mini mixer shows.
- **SET MY DEFAULTS** makes your real speakers and mic Windows' defaults again after VB-CABLE took
  them.
- **CHECK FOR UPDATES** asks GitHub once a day for the newest Faider and says from the tray when
  one is out. It can be switched off.
- **CLOCK** on DEVICES picks what sets the clock: the interface, a Windows playback device or the
  built-in clock.
- **BOOST WINDOWS AUDIO** on DEVICES raises Windows' audio engine to High priority, with **BACK TO
  NORMAL** to undo it and a WINDOWS AUDIO check in PRE-FLIGHT.
- **FFMPEG** in SETTINGS > GENERAL picks `ffmpeg.exe`, so MP3, AAC, Opus and ALAC recording works
  without the PATH.
- **PORT...** sets the port OSC listens on.
- **PLUGIN TEST TIME** gives a slow plugin 60 or 120 seconds to load in its test.
- **Save a problem report...** on the tray menu zips the logs where you choose. Nothing is sent.
- PRE-FLIGHT has a **ZERO THE DESK** key.
- Uninstalling asks whether to remove your mix and settings too (default No).

**Changed**
- One mini mixer, opened by the tray icon; **PIN** keeps it on top.
- One REC: the top bar's record key records what TOOLS > TAPE has chosen.
- One meter point: the sidebar's METER (IN, PRE, POST) and SETTINGS' CHANNEL METER POINT are the
  same setting.
- The desk says **CUT**, **OUTPUT**, **LIMIT** and **HOLD** everywhere, and its menus are in
  capitals.
- The tray menu's **Session timeline...** opens the SESSION TIMELINE.
- The manual on the website shows Faider's version.

**Removed**
- Compare Mixes, the browser's tab-by-tab list, PFL and the cue output, AUX 1-4, DIRECT, INDEPENDENT
  and LINK, pads and tape in ADD CHANNEL (their pages make them), and the VR Headset output in new
  mixes. A mix that has them still loads.

**Fixed**
- Undoing a removed output brings back the PRE sends to it too.

## 0.1.5 (8 November 2026)

**Changed**
- One channel strip: the Dark strip is gone, and its mic drive and input switch are in the strip's
  INPUT section.

**Fixed**
- Uninstalling quits a running Faider first, and ends only a copy running from the folder being
  removed.
- Started as administrator, Faider's window no longer opens behind the window it was started from.

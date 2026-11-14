# Changelog

What changed in each version of Faider, newest first. Downloads are on the
[Releases](https://github.com/dayztoolsforu/faider/releases) page.

## Unreleased

**New**
- ADD APP, ADD CHANNEL and SETUP also offer programs that are running but **NOT PLAYING YET**. The
  channel is made at once and hears the program from the moment it plays.
- **MIX-MINUS AND STEMS...** is on an output's right-click menu, **HEAR WHAT THEY HEAR** on
  PRE-FLIGHT's MIC OUTPUT row, and **SESSION TIMELINE...** and **ZERO DESK...** in SETTINGS >
  GENERAL, as well as in FIND.
- SETTINGS > HOTKEYS says what each group's keys do and where else they're set, and marks in red a
  key given to two things or one Windows refused.
- The installer can install for you only, without an administrator (**Install for me only**, or
  `/CURRENTUSER`). For all users is still the default.
- A privacy policy ([PRIVACY.md](PRIVACY.md)), an [OSC reference](docs/OSC.md) and a
  [FAQ](docs/FAQ.md). The manual lists the installer's silent switches and every command-line
  option.

**Changed**
- A program's channel is made one way, from ADD APP, ADD CHANNEL, SEND AN APP TO THE MIC, SETUP or
  an app rule. They pick from one list of programs, each listed once, by name.
- FFmpeg gets ten minutes, or the recording's own length, to convert a recording. After that it is
  stopped, the WAV is kept and the event log says so.
- The DEVICES tab asks Windows about the cable and switched-off devices directly, so it works in
  any display language.

**Fixed**
- BOOST WINDOWS AUDIO works from Windows' prompt.
- Conversions, replay files and plugin tests still running in the background are waited for as
  Faider closes.
- The audio lock is held for less time when the mix's structure changes.

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

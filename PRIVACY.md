# Faider privacy policy

Effective 14 November 2026, for Faider 0.1.6 and later.

Faider is an audio mixer that runs on your PC. It has no accounts, no analytics and no telemetry.
Your audio, your mix and your settings stay on your computer. The only thing Faider sends over the
internet by itself is one daily update check, which you can switch off.

## What Faider sends over the internet

| What | When | What is sent | Can you stop it? |
|---|---|---|---|
| Update check | At most once a day: half a minute after Faider starts, or when a day has passed while it keeps running. If GitHub doesn't answer, it tries again an hour later | One request to GitHub's API (`api.github.com`) asking for Faider's newest release. It carries only `Faider/<version>` (for example `Faider/0.1.6`). Nothing is downloaded or installed | Yes: SETTINGS > Updates & about > **Check for updates** off. Faider then never asks |

Like any web request, the update check reaches GitHub from your IP address. GitHub handles that
under the [GitHub Privacy Statement](https://docs.github.com/site-policy/privacy-policies/github-general-privacy-statement).

Nothing else is sent. Faider doesn't upload audio, recordings, your mix, logs, crash reports,
device names or anything about your PC.

## What Faider opens only when you click

These open in your web browser, and only when you press the key that says so. The site you visit
then handles your visit under its own policy.

- **GET VB-CABLE** (PRE-FLIGHT, after install): `vb-audio.com`
- **Get Faider x.y.z...** (tray menu, after an update check finds one): the release page on `github.com`
- **SOUND SETTINGS**: Windows' own Sound settings, on your PC

## What Faider keeps on your PC

Everything below stays on your computer. Nobody but you (and anyone with access to your Windows
account) can read it.

| Where | What |
|---|---|
| `%APPDATA%\Faider\session.xml`, `history\`, `sessions\` | Your mix: channel and output names, the devices and programs they use, levels and routes, plugin settings. Up to 60 earlier versions are kept so you can go back |
| `%APPDATA%\Faider\settings.xml`, `console.xml` and similar files | Your settings and desk layout, recent FIND commands, plugin test results |
| `%APPDATA%\Faider\events.log`, `status.txt` | A log of what Faider did (devices that came and went, programs captured, warnings) and a snapshot of its state, for troubleshooting. The log has the full paths of files it names (a session exported, a plugin that didn't load, a problem report saved), and a path under your user folder has your Windows user name in it |
| `%APPDATA%\Faider\crash.log`, `crash.dmp` | Written only if Faider crashes. The `.dmp` file is a copy of part of Faider's memory at that moment. It can hold anything Faider was using then: file paths (with your Windows user name), the web addresses of isolated tabs, plugins' settings, and names of devices, programs and windows |
| `%TEMP%\Faider-devices.log` | Only when DEVICES turns a device back on in Windows after you approve Windows' prompt: what Windows' own tool (pnputil) answered, which includes the device's ID. Written again each time; it isn't in `%APPDATA%\Faider`, so removing your data there leaves it (Windows' own clean-up of temporary files removes it) |
| `%APPDATA%\PatchbayConsole` | Only if you used Faider under its earlier name. The first time Faider starts it moves that folder to `%APPDATA%\Faider`. If it couldn't be moved (a file in it was in use), it was copied instead, and the old folder, with the mix and settings as they were then, stays until you delete it |
| `%APPDATA%\Faider\Isolated\` | Only if you use **ISOLATE A TAB**: a separate browser profile for each isolated tab. Faider opens that tab's web address there, and the browser keeps its own cookies and history in that profile. The tab's address is also saved in your mix so the tab can reopen |
| `Music\Faider Recordings` (or the folder you choose on TOOLS > TAPE) | Recordings and instant replays, made only when you choose to. Pad sounds and tape playlists are read from wherever you keep them |

## What Faider changes in Windows' settings

Only when you ask, and only these:

- **Start with Windows** (tray menu) adds a value named `Faider` under
  `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`, holding the path of
  Faider.exe. Switching it off removes it, and so does uninstalling.
- **APPLY** on DEVICES' VB-CABLE bar, after you approve Windows' prompt, writes the cable's sample
  rate and latency under `HKEY_LOCAL_MACHINE\SOFTWARE\VB-Audio\Cable` (VB-CABLE's own settings) and
  restarts the cable.

**Audio.** Faider processes the sound of your devices, microphones and the programs you add live in
memory. It records nothing unless you press REC or save an instant replay.

**Programs and windows.** To offer programs and browser tabs to capture, Faider looks at the names
and titles of programs playing sound and of their windows. It keeps only what you add to the mix.

**Problem reports.** **Save a problem report...** puts the logs, the status file and any crash
report (`crash.log` and `crash.dmp`) in one `.zip` where you choose. It is never sent anywhere. If
you share it (for example on a GitHub issue), look inside first. `events.log` has device, program
and window names and full file paths, which include your Windows user name. `crash.dmp` can hold
anything that was in Faider's memory when it crashed (see the table above): leave it out if you'd
rather not share that.

**OSC and MIDI.** When OSC is on, Faider listens only on your own PC (`127.0.0.1`), never on the
network. MIDI comes only from controllers connected to your PC.

## Other software Faider works with

- **VST3 plugins** you add run inside Faider and do whatever their makers built them to do, including
  their own network use. Each new plugin is tested in a separate copy of Faider first, but Faider
  doesn't control what a plugin sends. Check each plugin's own privacy policy.
- **FFmpeg**, if you use it for MP3, AAC, Opus or ALAC, is a program you install. Faider only gives
  it the recording to convert.
- **VB-CABLE** is a separate driver from VB-Audio, under its own terms.
- **BOOST WINDOWS AUDIO** changes the priority of Windows' audio engine on your PC, after Windows
  asks you. It sends nothing.

## Removing your data

- **Uninstalling** asks whether to remove your mix and settings too (`%APPDATA%\Faider`). The answer
  is No unless you choose Yes, and a silent uninstall always keeps them. Recordings in
  `Music\Faider Recordings` are never removed.
- An uninstall for all users needs an administrator. If you typed **another administrator's
  password** for it, the uninstaller runs as that administrator: its question is about that
  administrator's `%APPDATA%\Faider`, and the **Start with Windows** entry it removes is theirs,
  not yours. The question names the folder and the user. Your own stay: remove them yourself.
- You can also delete `%APPDATA%\Faider` yourself at any time (with Faider closed), and
  `%APPDATA%\PatchbayConsole` and `%TEMP%\Faider-devices.log` if they exist.

## The website

The Faider website ([dayztoolsforu.github.io/faider](https://dayztoolsforu.github.io/faider/)) is
hosted on GitHub Pages, which may log visits under the
[GitHub Privacy Statement](https://docs.github.com/site-policy/privacy-policies/github-general-privacy-statement).
Its fonts come from Google Fonts and Fontshare, so your browser contacts those services and they
see your IP address. The website uses no analytics and no cookies of its own.

## Children

Faider doesn't collect personal information from anyone, children included.

## Changes

If a future version changes what Faider sends or keeps, this policy will change with it, and the
release notes will say so. The date at the top shows when it last changed. Earlier versions are in
this file's history on GitHub.

## Contact

Questions about privacy: open an issue at
[github.com/dayztoolsforu/faider/issues](https://github.com/dayztoolsforu/faider/issues).

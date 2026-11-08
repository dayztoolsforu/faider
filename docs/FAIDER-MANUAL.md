# Faider User Manual

**Version 0.1 · Windows 10 (2004) or later**

Faider is a live audio router and mixer for Windows, built around a large-format console.
It takes sound from your audio interface, your Windows devices and individual programs, runs it
through channel strips, buses and outputs, and plays the result on any devices you choose: your
headphones, your speakers, a stream, a virtual microphone for calls and games.

This manual covers everything the app does, walks you through the common jobs step by step, and
tells you what to do when something goes wrong.

---

## Contents

1. [Before you start](#1-before-you-start)
2. [First-time setup](#2-first-time-setup)
3. [The window](#3-the-window)
4. [How routing works](#4-how-routing-works)
5. [The console](#5-the-console)
6. [The channel strip, section by section](#6-the-channel-strip-section-by-section)
7. [Raw strips: apps, pads, tape and combinators](#7-raw-strips-apps-pads-tape-and-combinators)
8. [Outputs](#8-outputs)
9. [Plugins](#9-plugins)
10. [The Routing page](#10-the-routing-page)
11. [Tools: Pads](#11-tools-pads)
12. [Tools: Tape, Recorder and Replay](#12-tools-tape-recorder-and-replay)
13. [Devices](#13-devices)
14. [Scenes, undo and sessions](#14-scenes-undo-and-sessions)
15. [Settings](#15-settings)
16. [Remote control, overlay and tray](#16-remote-control-overlay-and-tray)
17. [How-to guides](#17-how-to-guides)
18. [Troubleshooting](#18-troubleshooting)
19. [Reference](#19-reference)

---

## 1. Before you start

### What you need

| | |
|---|---|
| **Windows** | Windows 10 version 2004 or later, or Windows 11. Capturing a single program's sound needs 2004 or later |
| **An audio interface** | Optional. With its ASIO driver installed, Faider runs on the interface's clock for the lowest delay. Without one it runs on your Windows playback device, or on its built-in clock (see [Clocking](#clocking)) |
| **Headphones** | Plugged into your interface (or any Windows playback device) so you can hear your mix |
| **VB-CABLE** | Optional and free (vb-audio.com/Cable). A virtual microphone, so apps such as Discord, games and OBS can hear your mix |
| **ffmpeg** | Optional. With `ffmpeg.exe` on your PATH the recorder can also write MP3, AAC, Opus and ALAC |

### Words used in this manual

| Word | Meaning |
|---|---|
| **Channel** (input) | One source on the desk: a mic, a device, a program, the pads, the tape |
| **Strip** | The column on the console that controls a channel, bus or output |
| **Route** | A connection that sends a channel or bus somewhere: to a bus or an output |
| **Bus** | A sub-mix. Whatever is routed to it is summed, processed together and routed on |
| **Combinator** | A bus that splits its sound into parallel paths, each with its own plugins, then sums them |
| **Output** | A final mix that plays on a device (your headphones, speakers, a virtual cable) |
| **Interface** | Your ASIO audio interface, if you have one |
| **Clock** | What everything runs in time with: your interface, else your Windows playback device, else the built-in clock |

## 2. First-time setup

This chapter takes you from the installer to a working setup: your microphone in your headphones,
apps hearing your mic, and a program's sound on its own channel. Do the steps in order. Each one
takes a minute or two.

### Step 1: Install Faider

1. Run the installer, `Faider-<version>-Setup.exe`.
2. Windows SmartScreen may warn about an unrecognised app, because the installer isn't code-signed.
   Click **More info**, then **Run anyway**.
3. Accept the licence and allow the administrator prompt. If you want a desktop shortcut, tick it
   on the way (it's off by default).
4. The installer puts the app in `C:\Program Files\Faider` and adds **Faider**
   to the Start menu. Everything it needs comes with it.
5. On the last page you can start Faider straight away. If no VB-CABLE is installed, it
   also offers to open VB-CABLE's download page.

Your mix and settings live in `%APPDATA%\Faider`, separate from the program. Uninstalling
leaves them in place.

### Step 2: Install VB-CABLE (optional)

VB-CABLE gives you a virtual microphone that other apps can pick. Skip this step if you only want
to mix for your own headphones.

1. Download VB-CABLE from vb-audio.com/Cable and run its installer as administrator.
2. Restart Windows when it asks.
3. Windows now has two new devices. **CABLE Input** is a playback device: whatever plays into it
   comes out of **CABLE Output**, a recording device that apps can choose as a microphone.

> **Windows switches your sound to the cable.** Installing a virtual cable often makes it
> Windows' default: **CABLE Input** as your speakers and **CABLE Output** as your microphone. You
> then hear nothing from Windows, and apps that use the default mic hear the cable. Switch both
> back straight after installing.

**Put your devices back:**

1. Open **Settings > System > Sound** (or right-click the speaker icon in the taskbar >
   **Sound settings**).
2. Under **Output**, choose your headphones or speakers (or your interface's outputs), not
   **CABLE Input**.
3. Under **Input**, choose your real microphone, not **CABLE Output**.
4. Windows keeps a separate default for calls. Scroll down to **More sound settings**. On the
   **Playback** tab, right-click your headphones or speakers > **Set as Default Communication
   Device**. On the **Recording** tab, do the same for your microphone.

A quicker way to set the output: click the speaker icon in the taskbar, click the **>** beside the
volume slider, and pick your device.

Keep your real devices as Windows' defaults. Faider plays into the cable only where you
route it, and each app picks **CABLE Output** as its microphone itself (step 6). If Windows' default
speaker is the cable, PRE-FLIGHT reports it under **HEARD ONCE**.

### Step 3: Start Faider

Open the Start menu and click **Faider** (or use the desktop shortcut, if you made one).
The first time, **SETUP** opens in its own window.

### Step 4: Answer SETUP's four questions

![SETUP's microphones page: IN 1 and the headset mic chosen](images/console/setup.jpg)

SETUP asks four questions, one per page, and builds your mix from your answers. Click the cards to
choose. On every page except the third you can choose more than one. **NEXT** moves on, **BACK**
goes back.

1. **WHERE YOU LISTEN**: pick your headphones or speakers. Windows' default is already chosen.
2. **MICROPHONES**: pick the microphones you speak into. These are your interface's inputs
   (**IN 1**, **IN 2** …) and your Windows recording devices.
3. **APPS HEAR YOU**: pick the virtual cable apps use as their mic (**CABLE Input**, after step 2),
   or **NOT NOW**.
4. **PROGRAMS**: pick the programs playing sound now that you want on the desk.
5. Click **BUILD THE MIX**. **DONE** lists what was made.
6. Click **OPEN THE MIXER** to see it.

What each answer makes:

| Page | What it makes |
|---|---|
| **WHERE YOU LISTEN** | An output on each device. The first is called **HEADPHONES**, the others are named after their device |
| **MICROPHONES** | A channel for each, called **MICROPHONE** (MICROPHONE 2 …) |
| **APPS HEAR YOU** | An output called **MIC FOR APPS** on the cable. Every microphone is routed to it |
| **PROGRAMS** | A raw channel for each, routed to where you listen, plus an app rule that brings it back by itself whenever the program plays |

Your microphones aren't routed to your own headphones, so you don't hear a delayed echo of your
voice. To hear yourself, light the mic's **HEADPHONES** key (step 6).

SETUP is not a page: run it again from **FIND** (**SETUP: BUILD A MIX**), or the **SETUP** key in
SIMPLE, whenever you add a device or a program. It adds what's missing and never removes anything.

### Step 5: Check the audio is running

Look at the **health light** at the top right.

- **Green** with a percentage: the audio is running. Go on to step 6.
- **Red, reading OFF**: the audio isn't running. Another program may have your interface (a DAW,
  the driver's control panel). Close it and restart Faider. See
  [18.4](#184-the-interface-wont-start-or-stopped).

Click the light to open **DEVICES**. **STREAMS** shows the clock in use (ASIO, Windows or the
built-in clock; see [Clocking](#clocking)), and **BUFFER** sets its buffer size. The smallest size
that plays without crackles gives you the least delay. 256 or 512 samples suits most systems.

### Step 6: Hear yourself and let apps hear you

![The SIMPLE desk with a demo mix: six channels, each with a key for HEADPHONES and STREAM, and two outputs](images/console/simple.jpg)

The Mixer opens in **SIMPLE**. Each channel shows its name, what it listens to, a key for each
output, its fader and meter, **CUT** and **SOLO**. Each output shows the device it plays on, its
fader and meter.

| Key on a channel | Means |
|---|---|
| Plain grey | Not routed to that output |
| Filled in the output's colour | Routed there. Click to switch it off |
| Coloured edge only, **VIA BUS** | It gets there through a bus (set up in PRO). Click to route it straight there too |

1. Speak and watch your microphone's meter move.
2. To hear yourself, click the mic's **HEADPHONES** key.
3. In Discord, your game, OBS or any other app, choose **CABLE Output** as the microphone.
4. Check what they get: **FIND** (Ctrl+K) > **HEAR WHAT THEY HEAR** records 5 seconds of what apps
   receive and plays it back to you.

If you hear a program twice, see [18.7](#187-i-hear-something-twice-or-an-echo).

**+ CHANNEL** and **+ APP** add channels. **+ OUTPUT** adds an output on a device that has none.
The device key under an output's name changes where it plays. Right-click a name for **Rename...**,
the colour and **Remove**. Removing a program's channel also removes its app rule, so the channel
doesn't come back by itself.

### Step 7: PRO, for the whole console

The **SIMPLE / PRO** switch in the top bar shows the same mix either way. **PRO** is the whole
console: each channel's input, filters, dynamics and EQ, buses and combinators, plugins.

To shape your microphone, see [17.1](#171-hear-yourself-set-up-a-microphone). Every control is in
[6](#6-the-channel-strip-section-by-section). Settings you make in PRO keep working in SIMPLE.

### Step 8: Run PRE-FLIGHT

A few seconds after start, **PRE-FLIGHT** checks the setup and opens only if something needs
attention (not on the very first start, when SETUP comes first). Run it any time from
**SETTINGS > GENERAL > PRE-FLIGHT NOW...** or **FIND** (Ctrl+K). Every row should be green. See
[18.1](#181-pre-flight).

### Step 9: Finish

- **Your mix saves itself** within a second of any change. To keep a version under a name, use
  **SETTINGS > GENERAL > SAVE SESSION AS...**.
- **Start with Windows**: **SETTINGS > GENERAL > WINDOWS** starts Faider in the tray when
  you sign in, so your mic works from the start.
- **Closing the window doesn't stop the audio.** Faider keeps running in the system tray
  so your mic and headphones keep working. To stop it, right-click the tray icon and choose
  **Quit Faider**. Starting the program while it's in the tray brings the window back.

The first time each page opens, a short tip explains it. Turn tips off under
**SETTINGS > GENERAL > ON START**.

That's the setup done. The rest of this manual covers each part in detail.

## 3. The window

![The Mixer page with a demo mix: two microphones, two app channels, the pads and the tape, three buses and two outputs](images/console/mixer.jpg)

### The sidebar

The pages are cards down the left: **MIXER**, **ROUTING**, **TOOLS**, **DEVICES**,
**SETTINGS**. The page showing has a lit blue edge. While the Mixer shows, the sidebar also has
these cards (in SIMPLE, only **ADD CHANNEL** and **ADD APP**):

| Group | Card | What it does |
|---|---|---|
| **DESK** | **ADD CHANNEL** | Opens the input menu: interface inputs, Windows devices, apps, program windows, a browser (whole, or one tab isolated), the test tone. Pads and tape get their channel from their own pages (**TOOLS**) |
| | **ADD APP** | Lists the programs playing sound now. Pick one to give it a channel |
| | **ADD BUS** | A bus, or a combinator (two paths to start) |
| | **PLUGINS** | Slides the plugin panel up under the strips (see [9](#9-plugins)) |
| **SELECT** | **AUTO** | Touching any control on a strip selects that strip |
| | **FOLLOW** | The desk glides to the selected strip, also when it's chosen elsewhere (FIND, a hotkey) |
| **METER** | **IN** / **OUT** | The channel meters show the signal coming in (before processing) or going out |
| **SECTIONS** | HEADER … FADER | The sections of the strip. Lit ones are on screen. Click one to glide to it |

The SELECT and METER choices are remembered.

### The top bar

![The top bar: scenes, the health light, MINI, HISTORY, FIND and zoom](images/console/topbar.jpg)

From the right:

| Control | What it does |
|---|---|
| **+ 100% -** | Zoom. Steps between 100, 125 and 150%. Click the percentage for 100%. Ctrl + / Ctrl - / Ctrl 0 do the same |
| **FIND** (Ctrl+K) | Search every page, channel, scene, macro, pad and command by name |
| **HISTORY** (Ctrl+H) | Every change you've made. Pick one to go back to it |
| **MINI** | The mini mixer: a small mixer window (the tray icon opens it too). **PIN** on it keeps it on top |
| **Health light** | Green: audio is fine. Amber: the audio load is high or a device ran short recently. Red: an audible glitch in the last 10 seconds, or no audio running. The number is the audio load. Click it for DEVICES |
| **SCENES** | Your stored mixes. **+** stores a new one (see [14.1](#141-scenes)) |
| **SIMPLE / PRO** | The same mix, simple or as the whole console (see [Step 7](#step-7-pro-for-the-whole-console)) |

In a narrow window the health light goes first, then MINI, then HISTORY. FIND reaches them all.

### The transport

Under the top bar, on every Mixer view, you get the Tape deck's time (elapsed and track length),
the track name and its keys: **rewind 10 s**, **stop**, **play/pause**, **forward 10 s**,
**record**, **loop**.

- **Record** is the Recorder's **REC**: it records the source chosen on TOOLS > TAPE, in its format and folder, and stays lit while that recording runs. Press again to stop.
- **Loop** steps through **repeat all** → **repeat one** (marked **1**) → **off**.

---

## 4. How routing works

Routing decides where each sound goes. Nothing is heard until it's routed, and anything can be
routed to as many places as you like.

```mermaid
flowchart LR
    S[Source] --> C[Channel strip] --> P[Plugins] --> F[Fader · pan · cut]
    F --> B[Buses] --> O[Outputs]
    F --> O
    O --> D[Devices]
```

### The path of a sound

1. **A channel** takes one source and runs it through its strip (input, filters, dynamics and EQ,
   in the order its header shows), then its plugins, then its fader, pan and cut.
2. **Routes** send the channel on, to any of the buses and outputs at once (up to 8 of each). Every
   route carries the channel after its fader, so the fader and **CUT** affect everywhere it's
   routed.
3. **A bus** sums everything routed to it, runs its own strip and plugins, and is routed on to
   outputs (or into other buses) the same way.
4. **An output** sums everything routed to it, runs its bus compressor, mix EQ and **SAFE**
   limiter, and plays on its device.

### Outputs are devices

Each output plays on one device: your interface's outputs, a USB headset, speakers, **CABLE
Input**. You don't create outputs by hand. Route something to a device and an output is made on
it. That's why the routing boxes list devices as well as buses.

- There's one output per device. Route several channels to the same device and they meet in its
  output.
- Different outputs carry different mixes. Your headphones can hear game, music and voice while
  the stream output gets only music and voice.
- Unplug a device and its output goes silent until the device comes back.

### Three places to route

All three change the same routes. Use whichever is closest.

| Where | How |
|---|---|
| **Routing boxes** (each strip's header) | One box per bus and output device. Lit: routed there. Click to switch |
| **Route to** (right-click a strip) | The same list as a menu, ticked where it's routed |
| **The Routing page** | Every strip as a block, every route as a line. Click one block, then another (see [10](#10-the-routing-page)) |

### Buses

Use a bus when several channels need the same treatment or one fader. Route the channels to the
bus and the bus to the output. Then take the channels' direct routes to that output off, or they
reach it twice (once direct, once through the bus). A bus can feed another bus, and that adds no
delay.

### Combinators

A combinator is a bus split into parallel **paths** (two to start). Each path has its own plugins,
and the paths are summed at the combinator's fader. Route a source into one path or several (see
[7](#7-raw-strips-apps-pads-tape-and-combinators)).

### Rules that keep it safe

- **Feedback loops are refused.** A route that would bring a sound back to where it came from (a
  bus into a bus that feeds it, an output's sound fed back into its own input) is switched straight
  off again, and the event log names the loop.
- **Solo** on any strip silences every strip that isn't soloed, except those marked solo safe.
- **App channels and doubling.** A program plays on its own device as well as into its channel
  (see [18.7](#187-i-hear-something-twice-or-an-echo)).

### Clocking

One device sets the clock. Faider chooses it in this order when it starts:

1. **ASIO**: your audio interface, through its ASIO driver. The lowest delay.
2. **Windows**: with no ASIO driver, your Windows default playback device (a virtual cable, a
   screen's speakers or a VR headset only when nothing else is there). Your interface's outputs
   become that device's outputs, and microphones are added as Windows devices.
3. **Built-in clock**: with no playback device at all. Every device, your headphones included,
   follows it.

Every other device and every captured program runs on its own clock and joins through a small
buffer that corrects for drift, so clocks drifting apart never click. **DEVICES > STREAMS** shows
each one, and the clock in use.

If the Windows device setting the clock is unplugged, switched off or stops working, the next clock
takes over by itself and a notice tells you which. A device that won't start (because another
program holds it) is skipped until your devices change, then tried again. On the built-in clock, a
playback device that's plugged in takes over from it.

## 5. The console

### Layout

The desk shows every strip from left to right: **channels**, then **buses** (violet name bar), then
**outputs** (amber name bar). Each strip has seven sections, top to bottom:

| Section | Holds |
|---|---|
| **HEADER** | Level bars, the FILT / EQ / EXT keys, the processing order, the routing boxes, the strip number |
| **INPUT** | Trim, polarity, mic drive, the input switch and stereo width |
| **DYNAMICS** | Compressor and gate |
| **FILTERS** | Low-pass and high-pass |
| **EQ** | Four bands |
| **NAME** | The name bar and power switch |
| **FADER** | Pan, SOLO, CUT, SEL, the fader and its readout |

### Mouse and keyboard

| Action | Result |
|---|---|
| **Drag** a knob or fader up/down | Changes it. Its value shows beside it while you drag or hover |
| **Double-click** a knob or fader | Back to its default |
| **Click** a key or switch | On / off |
| **Click** the name bar | Selects the strip |
| **Drag** a strip's name bar sideways | Moves it among its own kind: channels, buses or outputs (see [17.4](#174-reorder-channels)) |
| **Click** the power symbol on the name bar | Switches the strip off or on |
| **Right-click** a strip | The strip's menu |
| **Mouse wheel** | Scrolls the desk across. **Shift + wheel** scrolls up and down |
| **Ctrl + / Ctrl -** | Zoom the whole window |

A left click never opens a menu. Menus are always on the right button.

### Selecting a strip

The selected strip is outlined in blue. Select a strip by clicking its name bar or its **SEL** key,
or by touching any of its controls with **AUTO** on. The selected strip is the one the plugin panel
shows, and the one FIND, hotkeys and remote controls act on.

With the desk focused (click it first): **Left / Right** select the previous / next strip,
**M** cuts it, **S** solos it, and **Up / Down** move its fader 0.5 dB (3 dB with **Shift**).

### Switching a strip off

Click the power symbol at the left of the name bar. The strip fades and greys out. It isn't
processed, and its device or program is let go. While it's off, **nothing on it can be changed**:
knobs, keys and the plugin panel ignore clicks, and its menu only offers **Turn on** and spacers.
Click the power symbol again to bring it back.

### Spacers

Spacers are blank strips that give you room between groups. Right-click a strip > **Add spacer
before** / **Add spacer after**. Right-click a spacer for **Narrow**, **Add another** and **Remove
spacer**. A spacer stays with the strip it follows when that strip moves.

### The strip menu (right-click)

| Item | |
|---|---|
| **Input** | Change the source: **Interface** inputs, **Windows devices** (or Windows default), **What an output plays** (loopback), **Test tone**, **Nothing** |
| **Route to** | Every bus and output device, ticked where it's sent |
| **Plugins** | Open one, **Add plugin...**, **Remove** |
| **Make mono / Make stereo** | Channels only |
| **Colour** | The channel's colour, shown along its name bar and on its Routing block |
| **Add spacer before / after** | |
| **Rename...** | |
| **Remove** | Deletes the channel or bus |

---

## 6. The channel strip, section by section

### HEADER

![HEADER on the Dark strip (HOST MIC) and the Light strip (GUEST MIC)](images/console/strip-header.jpg)

| Control | What it does |
|---|---|
| **Level bars** | The channel's level, left and right (IN or OUT, as chosen under METER) |
| **FILT** | Sends the filters into the gate/compressor's detector (key filtering) |
| **EQ** | The EQ in or out |
| **EXT** | The dynamics listen to an external key (sidechain) instead of the channel itself |
| **Processing order** | Three blocks (FILT, EQ, DYN), top to bottom in the order the channel runs them. **Click a block** to move it up one. The top one goes to the end. Lit blocks are in use |
| **Routing boxes** | One box per bus and output device. Lit: the channel is sent there. Click to switch (see [Outputs are devices](#outputs-are-devices)) |
| **Number** | The strip's place on the desk |

### INPUT

![INPUT: the Dark strip adds mic drive and the input switch, the Light strip has WIDTH](images/console/strip-input.jpg)

| Control | |
|---|---|
| **IN TRIM** | Input gain |
| **ø** | Polarity invert |
| **MIC** | Drive: adds warmth and level (shown as a percentage). The green LED beside it lights while the source has signal |
| **PRE** | The input stage in or out (out: trim, polarity and drive are bypassed) |
| **WIDTH** | Stereo width: 0 % mono, 100 % as it is, 200 % twice as wide. Faint on a mono channel |

### DYNAMICS

![DYNAMICS: the compressor above, the gate below, their gain-reduction LEDs between](images/console/strip-dynamics.jpg)

The **IN** switch (with the power symbol) puts the compressor and gate in or out together.

**Compressor**

| Control | |
|---|---|
| **MIX** | Wet/dry blend (parallel compression). Drag the green readout |
| **ratio**, **thresh**, **rel** | Ratio, threshold and release |
| **fast** | Fast attack (3 ms). Off is 10 ms |
| **pk** | Peak detection. On (the default), the compressor reacts to peaks. Off, it reacts to the average (RMS) level, which is smoother |
| **Gain reduction LEDs** | 3, 6, 10, 14 and 20 dB of reduction |

**Gate / expander**

| Control | |
|---|---|
| **range**, **thresh**, **rel**, **hold** | How far it closes, where it opens, how fast it closes, how long it holds |
| **exp** | Expander mode (a gentler gate) |
| **fast** | Fast attack (0.1 ms). Off is 1.5 ms |
| **Gate LEDs** | How far the gate is closing |

The LEDs show what the compressor and gate *would* do even while they're switched out, so you can
set them before putting them in.

### FILTERS

![FILTERS: low-pass and high-pass](images/console/strip-filters.jpg)

**lp** (low-pass) and **hp** (high-pass). Turned all the way down, the high-pass is out. Turned all
the way up, the low-pass is out.

### EQ

![EQ: HF, HMF, LMF and LF, with the EQ IN switch](images/console/strip-eq.jpg)

| Band | Controls |
|---|---|
| **HF** | gain, freq, **bell** (shelf when off) |
| **HMF** | gain, freq, q |
| **LMF** | gain, freq, q |
| **LF** | gain, freq, **bell** (shelf when off) |

The **EQ IN** switch puts the whole EQ in or out (the same as the header's EQ key).

### FADER

![The name bar and FADER section: pan, SOLO, CUT, SEL, the fader and its readout](images/console/strip-fader.jpg)

| Control | |
|---|---|
| **PAN** | Left/right. Double-click to centre |
| **SOLO** | Solo in place |
| **CUT** | Mute |
| **SEL** | Select this strip |
| **Fader** | Level, +12 dB to off. The readout below shows the level in dB |
| **safe** | Solo safe: this channel stays audible when others are soloed |
| **listen** | Listen to the dynamics' key signal |

### Voice effects

Beside the strip's own processing, every channel carries four voice effects of its own:

| Effect | What it does |
|---|---|
| **ROBOT** | Ring modulation: the voice is multiplied by a tone, so it sounds metallic |
| **ECHO** | A delay, up to a second |
| **RADIO** | Band-limited to 300 Hz - 3 kHz and driven, so it sounds like a small speaker |
| **PITCH** | The voice moved up or down, up to an octave either way |

They are switched with a macro: the action **FX ON**, **FX OFF** or **FX TOGGLE**, with the channel
as its target ([15](#15-settings)). A pad, a hotkey, a trigger or remote control can fire that
macro, so the effects come and go with the mix.

---

## 7. Raw strips: apps, pads, tape and combinators

Some channels carry sound that's already finished: a program's audio, the pads, the tape. They get
a **raw strip**, with routing, plugins if you want them, pan, fader and keys, and no built-in
effects. When one appears on the desk, its built-in effects are switched out once (and logged in
the event log). Its plugins are kept.

![Raw strips: two app channels (MUSIC, GAME), the pads and the tape](images/console/raw-strips.jpg)

![Two combinators, each showing its paths, what feeds them and their plugins](images/console/combinators.jpg)

| Strip | Its middle sections hold |
|---|---|
| **App** | The plugin list (click one to open it) and **ADD PLUGIN** |
| **Pads** | Bank keys **A–D**, the 4×4 pads (press to play; a GATE pad plays while held) and **STOP ALL** |
| **Tape** | The deck's screen and seek bar, the transport (previous, rewind, stop, play, pause, forward, next), **SHUFFLE**, **ADD...**, and the playlist (click a track to play it) |
| **Combinator** | For each path: **FROM**, the channels and buses feeding it (click one to go to its strip), and **PLUGINS** on that path (click one to open it). **EDIT IN ROUTING** opens the Routing page |

A combinator has no effects of its own. Its sound comes from what feeds it and from each path's
plugins. Right-click it > **Paths** to add a path, and, for each path, to open, add or remove its
plugins, rename it or remove it.

---

## 8. Outputs

![Two output strips: HEADPHONES and STREAM, with the bus compressor and mix EQ](images/console/outputs.jpg)

Each output (master) has its own dark strip:

| Control | |
|---|---|
| **DEVICE** | The device it plays on. Click to choose another |
| **SAFE** | A limiter after the fader: nothing leaves above -1 dBFS |
| **MON** | Monitor this output |
| **LUFS** | Short-term loudness of what leaves ("-- LUFS" when silent) |
| **WIDTH** | Stereo width |
| **BUS COMP** | In switch, the VU-style gain reduction meter, **threshold**, **ratio**, **attack**, **release**, **make-up**, **mix** |
| **MIX EQ** | In switch, HF gain and freq, LF gain and freq |
| **CUT**, **SEL**, fader | As on a channel |

Right-click an output for **Device...**, spacers, **Rename...** and **Remove**.

Outputs are made for you, one per device (see [Outputs are devices](#outputs-are-devices)).

**FIND > MIX-MINUS AND STEMS** builds a feed per guest (everything but their own voice) and voice,
music and game stems.

---

## 9. Plugins

Faider hosts **VST3** plugins on channels, buses and combinator paths, with automatic
latency compensation.

### Adding a plugin

- On a channel: right-click > **Plugins > Add plugin...**, or **ADD PLUGIN** on a raw strip, or
  **ADD** in the plugin panel.
- On a combinator path: right-click the combinator > **Paths > PATH n > Add plugin...**. Click a
  path's plugin on the combinator strip to open it.

Every plugin is **tested first in a separate copy of the program**, so a plugin that crashes or
hangs while loading can't take your mix down. The result is remembered until the plugin file
changes.

### The plugin panel

![The plugin panel for the selected strip](images/console/plugins.jpg)

Click **PLUGINS** in the sidebar. A panel slides up under the strips with the **selected strip's**
plugins in the order they run. **FIND** > **PLUGINS: name** opens it on that strip, and switches to
PRO if SIMPLE is showing. Each card shows:

- **CPU**: the plugin's share of the processing time (green, amber past 2 %, red past 5 %)
- **Latency**: the delay it adds
- **BYPASSED**: switched off but still loaded (still using memory)
- **HEAVY**: unusually expensive

Keys on each card: **BYPASS**, **OPEN** (its window), **REMOVE**, and arrows to move it earlier or
later. **ADD** at the end adds another.

---

## 10. The Routing page

The Routing page shows the whole mix as a map: every strip as a block and every route as a line.
Use it to see where everything goes, and to route by clicking two blocks.

![The Routing page with the demo mix: inputs, buses and outputs, each under its strip](images/console/routing.jpg)

### Reading the page

- **Three rows:** **INPUTS** at the top, **BUSES** in the middle, **OUTPUTS** at the bottom. Output
  devices that don't have an output yet sit at the right end of the OUTPUTS row, in grey.
- **Each block sits under its strip.** A block is at the same position across the page as its strip
  on the desk, spacers included, and the page scrolls across with the desk (mouse wheel). Rearrange
  the desk and the map follows.
- **Each block shows** the strip's name and number. A switched-off strip is dimmed. A channel with
  its own colour shows it as a tint and a band at the left.
- **Lines are routes.** Each one runs straight down from its source into a lane of its own in the
  gap below its row, across, and down into the block it feeds, ending in an arrow. Input routes
  are teal, bus routes violet. Several lines leaving or arriving at one block are spread across it
  in order, so they never stack.
- **Combinators** show their paths as cells **P1, P2, …** along the bottom of the block. Lines end
  in the path they feed.

### Routing block to block

![HOST MIC selected: its routes and the blocks it feeds stand out, the rest dims](images/console/routing-selected.jpg)

1. **Click a block.** It lights blue and pulses. The blocks it's routed to, or routed from, get a
   highlighted border, their lines brighten, and everything else dims.
2. **Click another block.** The route between them switches on (a line appears) or off (the line
   goes). Either order works: source then destination, or destination then source.
3. The first block **stays selected**, so keep clicking to route it to several places, or to take
   several routes off.
4. **Click it again**, click empty space, or press **Esc** to let go.

If the two blocks can't be routed (two outputs, say), the second one is selected instead.

### Examples

| To | Click |
|---|---|
| Hear your mic | MIC, then the headphones' output |
| Let apps hear your mic | MIC, then **CABLE Input** (in the outputs row, grey until it has an output) |
| Send a group through a bus | The bus, then each channel; then the bus, then its output |
| Take a channel out of the stream | The channel, then the stream's output (the line goes) |
| Feed one combinator path | The source, then the path cell (P1, P2…). Click more cells to feed several paths. Clicking the combinator outside its cells feeds its first path |

Clicking a grey device block makes its output, and the new output appears on the desk at once.

Drag a block sideways along its row to move its strip (see [17.4](#174-reorder-channels)). A route
that switches straight off again would make a feedback loop (see [18.8](#188-a-route-wont-switch-on)).

## 11. Tools: Pads

**TOOLS > PADS** gives you four banks (**A–D**) of 16 pads: 64 sounds, up to 32 playing at once.

![TOOLS > PADS: bank A loaded with eight sounds](images/console/pads.jpg)

![The pad editor for the selected pad](images/console/pad-editor.jpg)

### Getting sound out

Pads play into a **PADS channel**. Click **CREATE PADS CHANNEL** (it then reads **PADS CHANNEL
ACTIVE**), or choose another input in the **PLAY INTO** menu to play the pads through that
channel's plugins and routes. With no pads channel, presses are ignored.

### Loading and playing

| Action | |
|---|---|
| Click an **empty** pad | Choose a sound file |
| Click a loaded pad | Play it |
| Right-click a pad | Select it for editing without playing |
| Drop files on a pad | Loads them into that pad and the following ones in the bank |
| **Ctrl + drag** onto another pad | Swaps the two pads |
| **STOP ALL** | Fades out every pad |

Formats: WAV, FLAC, AIFF, OGG, MP3. A pad whose file has moved shows **MISSING: FILE**.

### The pad editor

| Setting | |
|---|---|
| Name | Double-click to rename |
| **LOAD**, **CLEAR**, **COLOUR** | |
| **REPLAY** | The instant-replay buffer becomes this pad's sound (saved as a WAV in the recordings folder). Arm replay in TOOLS > TAPE first |
| **MODE** | **ONE-SHOT** plays to the end (pressing again restarts). **GATE** plays while held. **LOOP** starts on one press and stops on the next |
| **CHOKE** | OFF or group 1–8. Pads in one group stop each other |
| **LEVEL** | -INF to +12 dB |
| **HOTKEY** | Click (**LEARN...**), then press the keys (see [HOTKEYS](#hotkeys)) |
| **MACRO** | A macro to run with the pad (a pad can have a macro and no sound) |
| **TRIM** | Drag the start and end lines on the waveform. Double-click for the whole file |
| **FADE IN / FADE OUT** | 0–5 s |

---

## 12. Tools: Tape, Recorder and Replay

### The tape deck

![TOOLS > TAPE: the deck, the recorder and replay, with the playlist on the right](images/console/tape.jpg)

**TOOLS > TAPE** plays a playlist into a **TAPE channel**. Click **CREATE TAPE CHANNEL** to make one.

- **ADD FILES**, **ADD FOLDER**, or drop files and folders anywhere on the page. **EJECT** clears
  the playlist.
- Double-click a track to play it, drag one to reorder, and press Delete to remove selected tracks.
- Transport: **PREV**, **REW** (back 10 s; hold to rewind fast), **PLAY**, **PAUSE**, **STOP**,
  **FF** (forward 10 s; hold to fast-forward), **NEXT**. Also **SHUFFLE**, **REPEAT** (all / one /
  off) and **LEVEL**.
- Tracks play gapless. Pauses and skips fade over 30 ms so they never click.

The top bar's transport and the tape strip on the desk control the same deck.

### The recorder

| | |
|---|---|
| **Source** | Any output, bus or input, or **ALL CHANNELS, ONE FILE EACH** (multitrack: every input, or only those with DIRECT lit, plus every output, sample-aligned) |
| **Format** | WAV 24-bit (default), 16-bit, 32-bit, 32-bit float; FLAC 24/16-bit; AIFF 24/16-bit; OGG Vorbis 128–320k. With ffmpeg: MP3, AAC (M4A), Opus, ALAC. Each shows how much time fits on the disk |
| **REC** | Starts and stops (the top bar's record key does the same). The screen shows time, size, time left, and any dropped blocks |
| **PATH** / **REVEAL** | Choose the folder / open it in Explorer |

Recordings go to `Music\Faider Recordings` unless you pick a folder. ffmpeg formats record
a WAV first and convert it when you stop (the WAV is kept if conversion fails). Recording stops by
itself when less than 1 GB is free.

### Instant replay

**ARM** keeps the last **30 s**, **1**, **2** or **5 minutes** of the recorder's source.
**SAVE** writes it to a file (right-click SAVE to give it a global hotkey). With multitrack chosen,
replay uses the first output.

---

## 13. Devices

**DEVICES** lists every Windows audio device and ASIO driver.

| Column | |
|---|---|
| **I/O** | ASIO, OUT or IN |
| **DEVICE**, **HARDWARE** | Its name and the adapter it's on |
| **FORMAT** | Channels and sample rate |
| **STATE** | Active, unplugged, disabled, or **off in Faider** |
| **TAGS** | DEFAULT and CALLS for Windows' default devices |

Click a header to sort. Select devices (Ctrl/Shift for several), then use:

| Key | |
|---|---|
| **EXCLUDE / INCLUDE** | Faider stops (or resumes) using the device. Excluded devices are never opened and are left out of lists |
| **DISABLE / ENABLE IN WINDOWS** | Turns the device off or on in Windows itself |
| **RENAME** | Your own name for the device, used throughout Faider (or double-click a row) |

### Streams

Under the list, **STREAMS** shows the audio engine's health:

- **CALLBACK LOAD** (and peak), **OVERLOADS**, **BLOCKS SKIPPED**. These turn warning-coloured if any happened
- **BUFFER**: your interface's buffer size (smaller means less delay and more load)
- Per device: buffered audio, total latency, **DRIFT** (the clock correction in ppm), **UNDER**
  (ran dry, silence played), **OVER** (overflowed, audio dropped), **RESYNC** (re-centred). A row
  that dropped audio shows red
- **RESET STATS** clears the counters

### VB-CABLE

If VB-CABLE is installed, a bar at the top sets its **sample rate** and **max latency**. **APPLY**
asks for one administrator prompt and restarts the cable.

The event log at the bottom records every device plugged in, removed or recovered.

---

## 14. Scenes, undo and sessions

### 14.1 Scenes

A scene stores every channel's fader, pan, cut, solo, power and routes (and, if you choose, plugin
settings).

- **Store:** click **+** in the top bar and give it a name.
- **Recall:** click its key. Faders and pans glide over half a second. The rest switches at once.
- **Right-click a scene** for: recall mutes only / levels only, store to it, include plugin
  settings, rename, a global hotkey, **Recall on device** (when a device appears; back when it
  goes), **Auto-scene** (when an app starts playing, a device connects, or at a time of day), MIDI
  learn, move, delete.

Devices and the channels themselves aren't part of a scene.

### 14.2 Undo and history

**Ctrl+Z** undoes, **Ctrl+Y** (or Ctrl+Shift+Z) redoes. **HISTORY** (Ctrl+H) lists every change.
Pick one to jump back to it.

### 14.3 Sessions

Your mix saves itself within a second of any change, and on exit.

| | |
|---|---|
| **Restore session** | Tray menu: earlier versions of your mix by date (Faider restarts with it) |
| **SESSION TIMELINE** | FIND: every kept version. **Restore everything**, or **PUT ONE CHANNEL BACK** (its level, pan, mute and rack) without restarting |
| **SAVE SESSION AS...** | SETTINGS > GENERAL > SESSIONS: keep the mix under a name |
| **OPEN, EXPORT OR IMPORT...** | Open a named mix (restarts), save the mix to a file, or load one from a file |

Up to 60 earlier versions are kept (at most one every 5 minutes). If your mix ever can't be loaded
(the interface is missing, the file is damaged), it's **set aside** rather than lost, and a fresh
mix starts. See [18.2](#182-my-mix-is-gone-a-fresh-mix-started).

---

## 15. Settings

**SETTINGS** has four sections, as cards down the left: **GENERAL**, **MACROS**, **HOTKEYS**,
**CONTROL**. On/off settings are square switches that glow cream when on.

![SETTINGS > GENERAL: each group of settings is a card](images/console/settings.jpg)

### GENERAL

| Card | Settings |
|---|---|
| **LOOK** | **LOOK...**: the console's colours and typeface, from a look file (see [Looks](#looks)) |
| **WINDOWS** | **START WITH WINDOWS**: opens in the tray when you sign in |
| **DESK** | **WARN IF I TALK WHILE MUTED** (a notice when a muted mic is above -30 dBFS) |
| **APPS** | **APP RULES...**: which bus or output a program goes to when it starts playing (also in **FIND**). Removing a program's channel removes its rule |
| **SESSIONS** | **SAVE SESSION AS...**; **OPEN, EXPORT OR IMPORT...** |
| **ON START** | **PRE-FLIGHT: CHECK THE SETUP**; **PRE-FLIGHT NOW...**; **TIPS**; **SHOW THE TIPS AGAIN** |
| **NOTICES (IN THE CORNER)** | Devices connected or gone, a channel clipping (its source or its output at full scale), scenes recalled, macros run, undo and redo |
| **ZOOM** | 100%, 125%, 150%, **CUSTOM...** (75–200%) |
| **METER STANDARD** | How meters read (sample peak with a fast fall by default; IEC and others) |
| **CHANNEL METER POINT** | **PRE HPF** (raw source), **PRE FADER** (after effects), **POST ON** (after the fader, default) |
| **ABOUT** | Version and credits |

### LOOKS

A **look** is every colour the console paints with, together with the typeface and the sizes that go
with it. The strips, the readouts, the knobs, the keys and the lamps all draw from the look in force,
so one look changes the whole desk at once.

**LOOK...** lists the looks you have and puts the one you choose in force; the desk is drawn again the
moment it changes. **SAVE THIS LOOK AS...** writes the look in force to a new file under a name you
give it.

A look is one file in `%APPDATA%\Faider\looks`, written as JSON, so you can write one by hand and copy
it to another machine. A file that is missing or broken leaves the built-in look in place, and whatever
a file leaves out falls back to the built-in value: a part-written file still works.

| | |
|---|---|
| `name` | The look's name, as it appears in the list |
| `font` | One typeface Windows ships with, so it stays crisp at the desk's small sizes |
| `keyRadius` | How round a key's corners are, in pixels |
| `fontSizes` | `legend`, `caption`, `readout`: the three printing sizes |
| `colours` | Every colour, as `#rrggbb`, or `#rrggbbaa` where a part is see-through |

The colours are named after the part they paint: the panels and the printing, the keys, the faders'
plate, the readouts, the knobs by function, the lamps, the strips' bodies, what a channel, a bus and an
output look like, the accent, the recesses and the shading. **SAVE THIS LOOK AS...** writes out every
name the look has, so the quickest way to begin is to change one thing on the desk, save it, and edit
that file.

A `palette` group names the colours a layout file can put on a part: `red`, `green`, `blue`, `black`,
`white`, `violet`, `grey`, `cream`, `orange`, `amber`, `teal`, `salmon`.

### MACROS

A macro is a list of **actions** run from a hotkey, a pad, a trigger or remote control.

1. Click **+ NEW**, then name it (double-click the name).
2. **+ ACTION**: MUTE ON/OFF/TOGGLE, SET FADER (dB, fade time), ASSIGN ON/OFF/TOGGLE (a route),
   CHANNEL ON/OFF, TRIGGER PAD, STOP PAD, ALL NOTES OFF, TRANSPORT PLAY/PAUSE, TRANSPORT NEXT,
   RECORD START/STOP, WAIT, SCENE RECALL, FX ON/OFF/TOGGLE (a voice effect, see
   [Voice effects](#voice-effects)), WINDOWS DEFAULT OUTPUT/INPUT, PLUGIN SETTING.
3. Optionally, **+ TRIGGER**: APP STARTS, APP STOPS, DEVICE CONNECTED, DEVICE DISCONNECTED, MASTER
   SILENT (for n seconds), MASTER HAS SOUND, TIME (24-hour).
4. Optionally, a **HOTKEY**, and **MOMENTARY** (the actions are undone when the key is released,
   for push-to-talk).
5. **FIRE** runs it now.

### HOTKEYS

Every assignable hotkey is in one place: **PADS**, **MACROS**, **SCENES**, and **SAVE INSTANT REPLAY**
under **TAPE**. Click one, then press the new keys (Esc cancels, Delete clears). Hotkeys must include
Ctrl, Alt or an F key, and they work while a game has focus. The fixed keys are listed too
([19.1](#191-keyboard-shortcuts)). The page's ON THE MIXER keys work on the console too
([5](#5-the-console)).

### CONTROL

| Card | |
|---|---|
| **MIDI** | **MIDI MAP** (Ctrl+L); **INPUTS AND BINDINGS**, where each MIDI input is switched on or off |
| **OSC** | **OSC ON (PORT 9000)**, for programs on this PC |

---

## 16. Remote control, overlay and tray

| | |
|---|---|
| **MIDI MAP** (Ctrl+L) | Click a control on screen, then move a knob or key on your controller. Each strip's fader, pan, CUT, SOLO, power and routing keys can be mapped, in SIMPLE and PRO, as well as scenes, pads and the tape. The mouse wheel scrolls the desk while mapping (Shift+wheel up and down) |
| **OSC** | Programs on this PC can send to port 9000 (it never listens to the network): `/faider/input/NAME/fader -12` (dB), `/faider/output/NAME/mute 1`, `/faider/bus/NAME/pan -0.5`, `/faider/input/NAME/solo 1`, `/faider/scene NAME`, `/faider/macro/NAME 1` (0 releases it). Write spaces in names as `_` |
| **In-game overlay** (Ctrl+Shift+O) | Each mic channel's talking light, LIVE / MUTED and level, plus up to four scene keys. Never takes focus from a game. Drag it anywhere |
| **MINI** | A small mixer window with the channels ticked **Tray mixer** (or the outputs). **PIN** keeps it on top of everything. **OPEN** returns to the main window |
| **Tray** | Click the icon for the mini mixer, double-click to open the window. Right-click: Show, scene recall, Restore session, Start with Windows, About, Quit. The icon flashes amber during an alert |

Faider always runs with your ordinary user rights, even when it's started from an
administrator window. It restarts itself with ordinary rights, because the in-game overlay can't
reach a program running as administrator. The few steps that need an administrator
(applying VB-CABLE settings, re-enabling a device Windows has switched off) ask for permission
themselves.

---

## 17. How-to guides

### 17.1 Hear yourself: set up a microphone

1. **ADD CHANNEL** > your interface input (or a Windows recording device). It appears as a new
   channel strip.
2. Speak, and set **IN TRIM** so the level bars peak around -12 to -6 dB.
3. Route it: click the routing box for your headphones' output (or use the Routing page).
4. Clean it up: **hp** to remove rumble (around 80–100 Hz), then the gate and compressor (**IN**
   on DYNAMICS), then EQ.

### 17.2 Let apps hear your mic (Discord, games, OBS)

1. Install **VB-CABLE** (PRE-FLIGHT offers a link), then set your real speakers and mic back as
   Windows' defaults ([Step 2](#step-2-install-vb-cable-optional)).
2. Route your mic channel to the **CABLE Input** device. An output is created on it.
3. In the app, choose **CABLE Output** as its microphone.
4. Run **PRE-FLIGHT NOW...** to check that nothing on the way is muted and nothing loops.

### 17.3 Bring a program into the mix

1. Start the program and make it play sound.
2. **ADD APP** > pick it. It gets a raw channel (no effects).
3. Route it where you want it heard. If you hear it twice, see
   [18.7](#187-i-hear-something-twice-or-an-echo).

### 17.4 Reorder channels

Drag a channel's **name bar** sideways on the Mixer (scroll down to the NAME section), or drag its
block on the Routing page. A blue bar shows where it will land, and the strips glide into place.
Spacers move with the strip they follow. Buses and outputs move the same way, each among its own
kind, and their routes go with them. SIMPLE shows the outputs in the same order.

### 17.5 Group channels into a bus

1. **ADD BUS**, then rename it (right-click > Rename...).
2. On the Routing page, click the bus, then click each channel to route it in.
3. Click the bus, then its output, to route the bus on.
4. If you only want those channels through the bus, take their direct routes to the output off.

### 17.6 Parallel processing with a combinator

1. **ADD BUS > COMBINATOR**. It starts with two paths. Right-click it > **Paths > Add path** for
   more.
2. On the Routing page, route sources into the path cells (**P1**, **P2**…).
3. Right-click > **Paths > PATH n > Add plugin...** for each path.

### 17.7 Make a stream mix and a headphone mix

1. Send your channels to two outputs, for example your headphones and the device your streaming
   software captures.
2. Each output has its own fader, bus compressor, EQ and **SAFE** limiter. Turn **SAFE** on for the
   stream output.
3. To keep something out of the stream, take that channel's route to the stream output off (with
   its routing box or on the Routing page).

### 17.8 Record

1. TOOLS > TAPE > **RECORDER**: choose the source and format.
2. Press **REC** (or the record key in the top bar). Press again to stop.
3. **REVEAL** opens the folder.

### 17.9 Make a push-to-talk key

1. SETTINGS > MACROS > **+ NEW**.
2. **+ ACTION** > **MUTE OFF** > your mic channel.
3. Turn **MOMENTARY** on and set a **HOTKEY**. While you hold it the mic is live, and on release it
   mutes again. Start with the mic muted.

---

## 18. Troubleshooting

### 18.1 PRE-FLIGHT

PRE-FLIGHT runs a few seconds after start (and from **PRE-FLIGHT NOW...** or FIND) and only shows
up if something is wrong. Each row has a light, the finding and a fix:

| Check | Problem | Fix |
|---|---|---|
| **AUDIO** | It didn't start (another app may have it) | **DEVICES** |
| **MIC CABLE** | No VB-CABLE: apps can't hear your mix as a mic | **GET VB-CABLE** |
| **MIC MASTER** | No output plays into the mic cable | **ROUTING** |
| **MIC CHANNELS** | Something on the way to the mic is muted | **UNMUTE** |
| **HEARD ONCE** | A feedback loop, or something heard twice | **SOUND SETTINGS** |

### 18.2 My mix is gone, a fresh mix started

Your mix isn't lost. When it can't be used at start, it's **set aside** as
`session-unloaded-<date>.xml`. The event log says why:

| Reason | What to do |
|---|---|
| The saved interface isn't connected | Connect it, then **Restore session** (tray) > the "kept:" version |
| The saved interface couldn't be opened (another program has it) | Close the other program and restart Faider. Nothing is saved over your mix meanwhile |
| session.xml couldn't be read | **Restore session** > a recent version |

### 18.3 No sound

Work along the signal path:

1. **Health light** red and reading OFF: the audio isn't running. See
   [18.2](#182-my-mix-is-gone-a-fresh-mix-started) and [18.4](#184-the-interface-wont-start-or-stopped).
2. **The channel's meter** (METER: IN): if it isn't moving, check its **Input** (right-click) and
   that the source is playing. A strip that's switched off passes nothing.
3. **CUT** is off, the fader is up, and nothing else is soloed (another strip's **SOLO** silences
   everything else).
4. **Routing:** at least one routing box is lit, or there's a line on the Routing page.
5. **The output**: not CUT, fader up, and **DEVICE** shows the right device (not "NO DEVICE" or
   "(OFF)").
6. **The device** is plugged in and not excluded on DEVICES.

### 18.4 The interface won't start, or stopped

- Another program may hold it (a DAW, the driver's control panel). Close it and restart.
- **INTERFACE DOWN** on the tray: the interface reset itself and wouldn't reopen. Change its buffer
  size on DEVICES or restart Faider.
- Rate or buffer changes made in the interface's own control panel are picked up by themselves.
- To run without the interface for a while, start Faider with
  `"C:\Program Files\Faider\Faider.exe" --clock windows` (your Windows playback
  device) or `--clock builtin` (the built-in clock). `--clock` with part of an ASIO driver's name
  (`--clock focusrite`) picks that driver. Your mix comes with it and stays saved for your
  interface: the next normal start goes back to it.

### 18.5 Clicks, crackles or dropouts

1. Look at the **health light** and **DEVICES > STREAMS**.
2. High **CALLBACK LOAD** or OVERLOADS: raise the **BUFFER**, or find expensive plugins in the
   plugin panel (**HEAVY**, high CPU) and bypass or remove them.
3. **UNDER / OVER** climbing on one device: that device is struggling. Try another USB port, close
   other programs using it, or exclude it if you don't use it.
4. Steady **DRIFT** is normal. It's the correction that keeps clocks in step.

### 18.6 An app doesn't appear in ADD APP

- The program must be **playing sound** right now. Start playback, then open the menu again.
- Programs that already have a channel are greyed out (**ON THE DESK**).
- Windows older than 10 version 2004 can't capture a single program ("Windows refused app capture").

### 18.7 I hear something twice, or an echo

- A program keeps playing on its own device as well as into its channel. If its channel is routed
  to an output on that same device, you hear it directly and through Faider. Switch that route off
  (in SIMPLE, the output's key on the channel), or move the program to another device. PRE-FLIGHT
  warns about this.
- Windows' default speaker is set to the virtual cable. PRE-FLIGHT > **HEARD ONCE** names it.

### 18.8 A route won't switch on

It would make a **feedback loop** (see [Rules that keep it safe](#rules-that-keep-it-safe)). The
event log names the loop. Remove the route that feeds back.

### 18.9 I can't change anything on a strip

The strip is **switched off** (dim and grey). Click the power symbol on its name bar.

### 18.10 Dragging a channel does nothing

Drag by the **name bar** (the NAME section, near the bottom; scroll down if needed), or drag the
block on the Routing page. A strip moves only among its own kind: a bus can't go among the
channels.

### 18.11 A plugin won't load

Each plugin is tested in a separate copy of the program first, and the reason for a failure is
reported: it didn't finish loading in 30 s, it crashed while being tested, or there's no VST3
plugin in the file. Update or reinstall the plugin. Once its file changes, it's tested again.

### 18.12 The program crashed

A report is written to `%APPDATA%\Faider\crash.log`, with a minidump `crash.dmp`. Your mix
is saved within a second of any change, so you lose at most the last moment's changes.

### 18.13 Recording stopped by itself

Recording stops when less than 1 GB of disk space is left (or the format's size limit is near).
The tray shows **RECORDING STOPPED, NO ROOM**. Free some space or choose another **PATH**.

### 18.14 No sound from Windows after installing VB-CABLE

Windows has made the cable its default device. Set your headphones or speakers as the default
output again, and your real microphone as the default input, both in **Settings > System > Sound**
and as the default communication devices. See [Step 2](#step-2-install-vb-cable-optional).

---

## 19. Reference

### 19.1 Keyboard shortcuts

| Keys | |
|---|---|
| **Ctrl+K** | FIND |
| **Ctrl+Z / Ctrl+Y** (Ctrl+Shift+Z) | Undo / redo |
| **Ctrl+H** | History |
| **Ctrl+L** | MIDI MAP |
| **Ctrl + / Ctrl - / Ctrl 0** | Zoom in / out / 100% |
| **Ctrl+Shift+O** | Show / hide the in-game overlay (works anywhere) |
| **Left / Right** | Select the previous / next strip (desk focused) |
| **M / S** | Cut / solo the selected strip |
| **Up / Down** | Selected fader ±0.5 dB (±3 dB with Shift) |
| **Esc** | Let go of the selected block (Routing) |

### 19.2 Command line

| | |
|---|---|
| `--minimised` | Start in the tray |
| `--quit` | Quit the running copy |

### 19.3 Files

In `%APPDATA%\Faider`:

| File | |
|---|---|
| `session.xml` | Your mix |
| `history\` | Earlier versions (up to 60) |
| `sessions\` | Named mixes |
| `session-unloaded-*.xml` | Mixes set aside at start |
| `settings.xml` | Preferences |
| `looks\` | The looks: one JSON file each (see [Looks](#looks)) |
| `console.xml` | Spacers, SELECT and METER choices |
| `plugins.xml` | The plugins found and how each one tested |
| `events.log` | Everything that happened (rotates at 5 MB) |
| `status.txt` | A snapshot of the engine every few seconds: devices, levels, buffers, drift |
| `crash.log`, `crash.dmp` | Crash reports |

The program itself is in `C:\Program Files\Faider`. Recordings go to
`Music\Faider Recordings` unless you choose another folder.

### 19.4 Colours

| Colour | Means |
|---|---|
| Blue outline | Selected |
| Grey name bar | Channel |
| Violet | Bus |
| Amber | Output |
| Teal line | A channel's route |
| Violet line | A bus's route |
| Green health light | Audio fine; amber: under strain; red: glitch or not running |

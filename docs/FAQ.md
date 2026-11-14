# FAQ

Short answers, taken from the [manual](https://dayztoolsforu.github.io/faider/#manual)'s
[troubleshooting](https://dayztoolsforu.github.io/faider/#18-troubleshooting) chapter. The manual
has the detail.

**Windows went silent after I installed VB-CABLE.**
Windows made the cable its default device. **SET MY DEFAULTS** (PRE-FLIGHT's WINDOWS DEFAULTS row, or
WINDOWS DEFAULTS in FIND) makes your headphones or speakers and your real microphone the defaults
again, for calls too. Or do it in Settings > System > Sound.

**I hear nothing.**
Work along the signal path. Is the health light green? Does the channel's meter move (METER: IN)? Is
CUT off, the fader up and nothing else soloed? Is a routing box lit? Is the output uncut, its fader
up and its DEVICE the right one? Is the device plugged in and not excluded on DEVICES?

**I hear a program twice, or an echo.**
A program keeps playing on its own device as well as into its channel. If the channel is routed to
an output on that same device, you hear both: switch that route off, or move the program to another
device. Or Windows' default speaker is the virtual cable: **SET MY DEFAULTS** puts your speakers
back.

**A program isn't in ADD APP.**
It has to be playing sound, or running with a window of its own (then it's under **NOT PLAYING
YET**). Programs that already have a channel are greyed out (**ON THE DESK**). Capturing one program
needs Windows 10 version 2004 or later.

**My mix is gone and a fresh one started.**
It isn't lost. A mix that can't be used at start is set aside as `session-unloaded-<date>.xml`, and
the event log says why. **SESSION TIMELINE** (SETTINGS > GENERAL, FIND or the tray) brings a kept
version back with **RESTORE EVERYTHING**.

**The interface won't start.**
Another program may hold it (a DAW, the driver's control panel): close it and restart Faider. To run
without the interface for a while, choose a Windows playback device or the built-in clock in
DEVICES > CLOCK and click **RESTART NOW**.

**It clicks, crackles or drops out.**
Look at the health light and DEVICES > STREAMS. High CALLBACK LOAD or OVERLOADS: raise the BUFFER, or
bypass plugins marked HEAVY. UNDER or OVER climbing on one device: try another USB port, or exclude
the device if you don't use it. Crackling only on Windows devices or captured apps: try **BOOST
WINDOWS AUDIO** (DEVICES > WINDOWS AUDIO).

**A route won't switch on.**
It would make a feedback loop. The event log names the loop.

**I can't change anything on a strip.**
The strip is switched off (dim and grey). Click the power symbol on its name bar.

**Dragging a channel does nothing.**
Drag it by its name bar, or drag its block on the Routing page. A strip moves only among its own
kind.

**A plugin won't load.**
Each plugin is tested in a separate copy of the program first, and the reason for a failure is
reported: it took too long (30 s, or 60 or 120 with PLUGIN TEST TIME in SETTINGS > GENERAL), it
crashed in the test, or the file holds no VST3 plugin. Update or reinstall the plugin.

**Recording stopped by itself.**
Less than 1 GB of disk space was left, or the format's size limit was near. Free some space or
choose another PATH.

**Faider crashed. What do I send?**
Right-click the tray icon > **Save a problem report...**. It puts the crash report, the event log
and the status snapshot in one .zip where you choose. Nothing is sent anywhere: attach the file to
an [issue](https://github.com/dayztoolsforu/faider/issues) yourself.

**The overlay doesn't show.**
Ctrl+Shift+O may belong to another app (SETTINGS > HOTKEYS marks it in red): use **SHOW OVERLAY** in
FIND. The overlay is made for a game in a borderless window, so set the game to borderless or
windowed.

**My MIDI controller does nothing.**
Switch its input on in SETTINGS > CONTROL > **INPUTS AND BINDINGS**, then bind its controls in
**MIDI MAP** (Ctrl+L).

**OSC does nothing.**
Light **OSC ON** in SETTINGS > CONTROL, send to the port on that key (9000 unless you changed it),
and send from this PC: Faider doesn't listen on the network. The addresses are in the
[OSC reference](OSC.md).

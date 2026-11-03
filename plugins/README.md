# Remote control

Control Faider from a Stream Deck, a Loupedeck, a MIDI controller, a phone or a script.

## Set up a controller

The quick way is SETTINGS > CONTROL > SET UP A CONTROLLER (or type it in FIND, Ctrl+K). Pick the
hardware: LOUPEDECK via LoupixDeck, STREAM DECK, MIDI CONTROLLER, PHONE / TABLET over OSC, or PHONE
through a web page. The page then shows each step with a lamp (green when done, amber when it needs
you) and a key that does it:

- the control API on
- the deck's app: found, running, or GET to download it
- Faider's plugin: INSTALL / UPDATE closes the app, copies the plugin in, and starts the app again until the plugin is loaded
- for MIDI, the inputs to listen to
- for OSC, any port (1024 to 65535) and the addresses

After that, you bind controls in the deck app's own menus. For MIDI, bind them from SETTINGS >
CONTROL (MIDI MAP, INPUTS AND BINDINGS). On that page, OSC ON switches OSC on and off on the port
last set. OSC FROM OTHER DEVICES lets other machines on the network reach it; otherwise only this PC
can.

The plugins ship with the app. The build puts the Stream Deck plugin in `plugins\` next to the
program. The LoupixDeck plugin goes there too once you've built it with .NET (then re-run CMake).

The rest of this page covers setting things up by hand.

## The control API

Both deck plugins (Stream Deck, and Loupedeck through LoupixDeck) talk to Faider's local control
API (`src/engine/ControlServer.h`): HTTP on `127.0.0.1:9123`. Switch it on with API ON under
SETTINGS > CONTROL > DECK PLUGINS. It stays off until you do. Only this PC can reach it, unless
PHONE CONTROL is on. Then the same server answers the network, and every request needs the phone
key.

PHONE CONTROL ON (same page) serves a control page to phones on your network, behind a 12-character
key. SET UP A CONTROLLER > PHONE (WEB PAGE) shows the link as a QR code.

To check it's running, open http://127.0.0.1:9123/api/state in a browser. It lists the strips,
scenes and macros.

## Stream Deck

`streamdeck/com.faider.desk.sdPlugin` runs on plain Node (the Node the Stream Deck app ships
with). There are no packages to install.

To install it, quit the Stream Deck app, copy the `com.faider.desk.sdPlugin` folder into
`%APPDATA%\Elgato\StreamDeck\Plugins\`, and start the app again. The actions are under FAIDER:

| Action | Key | Dial (Stream Deck +) |
|---|---|---|
| MUTE | Mutes / unmutes a channel; lit red while muted | |
| SOLO | Solos an input; lit yellow while soloed | |
| FADER | Sets the fader to its LEVEL; shows the fader | Turns the fader (DIAL STEP dB a tick); press: back to LEVEL |
| SCENE | Recalls a scene; lit green while it's active | |
| MACRO | Runs a macro while held | |

Each action's settings list the desk's channels, scenes and macros by name. Keys show FAIDER OFF
while Faider isn't answering.

The plugin has been tested against a stand-in for the Stream Deck app (it starts the plugin and
checks what it sends), not yet on Stream Deck hardware.

## Loupedeck (through LoupixDeck)

`loupixdeck/LoupixDeck.Plugin.Faider` is a plugin for
[LoupixDeck](https://github.com/RadiatorTwo/LoupixDeck), the open-source app for Loupedeck devices.
Use it rather than the official Loupedeck software, and don't run both: the official software holds
the device's port and LoupixDeck can't reach it.

To build it you need the .NET 10 SDK. The LoupixDeck.PluginSdk package comes from NuGet.

```
dotnet build plugins/loupixdeck/LoupixDeck.Plugin.Faider -c Release
```

To install it, copy `LoupixDeck.Plugin.Faider.dll`, `.deps.json`, `plugin.json` and `icon.png`
from `bin/Release` into `%USERPROFILE%\.config\LoupixDeck\plugins\faider\`. Then start LoupixDeck
twice: the first start enables a new plugin, the second loads it.

Commands come from the menu, which lists what's on the desk right now. On a key or touch button:

- FAIDER > INPUTS / BUSES / OUTPUTS > a channel > METER, MUTE, SOLO, FADER TO 0 DB, FADER UP / DOWN 1 DB
- FAIDER > SCENES, MACROS
- FAIDER > METER BRIDGE (ALL CHANNELS)

METER puts a channel's live meter on a touch key: left and right bars from -60 to 0 dB, green, then
amber from -18 and red from -6, with a 1.5 s peak hold and the held peak in dB. While the channel is
muted the meter turns grey and reads MUTED. Press it to mute.

METER BRIDGE opens a page with every channel's meter on the keys (inputs, then buses, then outputs).
Press a meter to mute that channel, or the back key to leave. Meters read Faider 25 times a second
while one is on show, and a key goes to the device only when what it shows changes.

On a dial, a channel's FADER + MUTE (TURN, PRESS) sets turn left, turn right and press in one go.
FADER alone is a value dial (press: 0 dB). Keys light red while muted, yellow while soloed and green
for the active scene. They show the fader, carry a level bar, and read FAIDER OFF while Faider
isn't answering.

Status: built against LoupixDeck.PluginSdk 1.34.0. A stand-in host loaded it and got the live
desk's menus, key images and dial value. LoupixDeck 1.48 loads it and registers its nine commands
(Mute, Solo, Fader, FaderUp, FaderDown, Scene, Macro, Meter, MeterBridge). It hasn't yet been tried
on the Live S itself.

## MIDI controllers

SETTINGS > CONTROL > INPUTS AND BINDINGS lists the MIDI inputs (switch on the ones to listen to),
every binding with a key to clear it, and the last control a controller sent.

MIDI MAP (Ctrl+L, or SETTINGS > CONTROL > MIDI MAP) marks every control you can play from MIDI: each
strip's fader, pan, CUT, SOLO, power and routing keys (in SIMPLE and PRO), the scene keys, the pads,
the tape's transport, REC and replay SAVE.

1. Click a control. It pulses green.
2. Move a knob or fader, or press a key, on the controller. The control is bound and shows its binding in amber.

Right-click a control to clear it. The tabs still work while mapping, so you can map every page. Esc
or Ctrl+L leaves.

Faders and knobs follow a controller's knobs and faders. Keys (notes, or a CC crossing its middle)
switch things on and off, recall scenes, and hold pads and macros while held.

## Commands for scripts

```
curl http://127.0.0.1:9123/api/state
curl http://127.0.0.1:9123/api/meters
curl -X POST -H "X-Faider: 1" "http://127.0.0.1:9123/api/input/MIC_FOCUS/mute"
curl -X POST -H "X-Faider: 1" "http://127.0.0.1:9123/api/output/OUT_MUSIC/fader?value=-20"
curl -X POST -H "X-Faider: 1" "http://127.0.0.1:9123/api/scene/GAMING"
curl -X POST -H "X-Faider: 1" "http://127.0.0.1:9123/api/macro/BRB"
curl -X POST -H "X-Faider: 1" "http://127.0.0.1:9123/api/pad/1"
```

Names are the channel, scene or macro names with `_` for spaces. Inputs, buses and outputs take
`fader?value=` (dB) and `mute`. Inputs also take `solo`, and inputs and buses take `pan`. Commands
must be POSTs with the `X-Faider: 1` header, so a web page can't send them.

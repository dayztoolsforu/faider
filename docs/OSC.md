# OSC reference

Faider takes OSC messages from programs on the same PC: faders, pan, CUT and SOLO by strip name,
scenes, macros and pads. It only receives. It sends nothing back, and an address it doesn't know is
ignored without an error.

## Switching it on

| | |
|---|---|
| On / off | The **OSC** switch in **SETTINGS > MIDI & OSC**. Off until you switch it on |
| Port | UDP 9000 to start. **OSC port** on the same page sets another, 1024 to 65535 (type it, then **Apply**). If OSC is on, it moves there at once |
| Where it listens | `127.0.0.1` only: this PC. A phone, a tablet or another PC can't reach it, and there is no setting that opens it to the network |
| Saved | On or off and the port are kept with your mix |

If the port can't be opened (another program may have it), Faider says so, OSC stays off, and the
event log has `osc: couldn't listen on port n`. When it works the log has
`osc: listening on port n (this PC only)`.

## Addresses

Every address starts with `/faider`. `KIND` is `input` (a channel), `bus` or `output`. `NAME` is the
strip's name.

| Address | Argument | What it does |
|---|---|---|
| `/faider/KIND/NAME/fader` | dB | The fader. Above +12 is +12; below -60 is off; -60 is the fader's lowest step. Ignored without an argument |
| `/faider/KIND/NAME/pan` | -1 to 1 | Pan: -1 left, 0 centre, 1 right (values outside are limited to that). `input` and `bus` only. Ignored without an argument |
| `/faider/KIND/NAME/mute` | 1 or 0 | CUT on (above 0.5) or off. Without an argument it toggles |
| `/faider/KIND/NAME/solo` | 1 or 0 | SOLO on (above 0.5) or off. Without an argument it toggles. `input` and `bus` only |
| `/faider/scene/NAME` | none | Recalls the scene |
| `/faider/scene` | the name, as a string | The same, with the name as the argument (so it can hold spaces) |
| `/faider/macro/NAME` | 1 or 0 | 1 (above 0.5) presses the macro, 0 releases it, as a hotkey held and let go (a **MOMENTARY** macro is undone on release). Without an argument the macro runs once |
| `/faider/pad/N` | 1 or 0 | Pad `N`, 1 to 64, as its key on a controller works it: 1 (above 0.5) presses, 0 releases. Without an argument it is pressed and released at once |

## Names and values

- Names match in any case: `/faider/input/microphone/fader` finds **MICROPHONE**.
- An OSC address can't hold a space, so write `_` for one: **MIC FOR APPS** is `MIC_FOR_APPS`. The
  exact name is tried first, so a name with a real `_` in it still works.
- A name with `/` in it can't be addressed (the address is split at every `/`).
- The value is the first argument, a float or an integer. A value that isn't a finite number (NaN,
  infinity) drops the message.
- Send plain messages. Messages inside an OSC bundle are not read.

## Examples

```
/faider/input/MICROPHONE/fader   -12.0     the MICROPHONE channel's fader to -12 dB
/faider/output/MIC_FOR_APPS/mute 1         cut the MIC FOR APPS output
/faider/output/MIC_FOR_APPS/mute 0         uncut it
/faider/bus/MUSIC/pan            -0.5      the MUSIC bus half left
/faider/input/GAME/solo                    toggle SOLO on GAME
/faider/scene/STREAMING                    recall the scene STREAMING
/faider/scene                    "ON AIR"  recall the scene ON AIR
/faider/macro/PUSH_TO_TALK       1         press the macro (send 0 to release it)
/faider/pad/3                    1         press pad 3 (send 0 to release it)
```

From Python, with the [python-osc](https://pypi.org/project/python-osc/) package:

```python
from pythonosc.udp_client import SimpleUDPClient

faider = SimpleUDPClient("127.0.0.1", 9000)
faider.send_message("/faider/input/MICROPHONE/fader", -12.0)
faider.send_message("/faider/output/MIC_FOR_APPS/mute", 1)
faider.send_message("/faider/scene/STREAMING", [])
```

## The earlier name

Addresses that start with `/patchbay` (the program's earlier name) are still taken, and mean the
same as `/faider`.

## When nothing happens

- The **OSC** switch (SETTINGS > MIDI & OSC) is off, or the sender uses another port than the one shown under it.
- The sender runs on another device. Faider listens on this PC only.
- The name doesn't match a strip of that kind (`input`, `bus`, `output`), or `pan` or `solo` was
  sent to an output.
- `fader` or `pan` was sent without a number.
- The message was sent inside a bundle.

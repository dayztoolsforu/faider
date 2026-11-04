# Faider

This folder holds the console: the Mixer page, the Routing page and the strip layouts. Every part is
drawn in code. The program is **Faider.exe**. The installer puts it in
`C:\Program Files\Faider`; building from source with `Restart Faider.cmd`
installs it to `C:\Faider`. Your mix and settings live in `%APPDATA%\Faider`.

The full manual is [docs/FAIDER-MANUAL.md](../docs/FAIDER-MANUAL.md). It covers
every page and control, with how-to guides and troubleshooting. Translations (Español, Français,
Deutsch, Italiano, Português (Brasil), Nederlands, Polski, Svenska, Türkçe, 日本語, 한국어 and
简体中文) are in [docs/i18n](../docs/i18n).

## The pages

The sidebar down the left shows the pages as cards: **MIXER**, **ROUTING**, **TOOLS** (Pads, Tape),
**DEVICES** and **SETTINGS**. On the Mixer page it also has these:

| Card | |
|---|---|
| **ADD CHANNEL** | The input menu: interfaces, devices, apps, pads, tape |
| **ADD APP** | An app playing sound now, as a channel of its own |
| **ADD BUS** | A new bus |
| **PLUGINS** | The selected strip's plugins, in a panel under the strips |
| **SELECT** | **AUTO**: touching a strip selects it. **FOLLOW**: the view follows the selection, also when it's made elsewhere |
| **METER** | **IN** or **OUT**: the strips' meters before or after the processing |
| **SECTIONS** | The strip's sections, lit while in view. Click one to go to it |

The top bar has the time display and the Tape deck's transport: rewind, stop, play / pause, wind
on, record (the first output) and loop (all, one, off).

## The console

Each channel has a **Light strip** or a **Dark strip**. Pick one per channel with right-click >
Strip type > Light strip / Dark strip. Microphones start on the Dark strip.

Apps, the pads and the tape get a raw strip. It has no built-in effects, takes plugins if you want
them, and carries the tool's own controls. A combinator's strip shows each path, what feeds it and
its plugins. Outputs have their own strip, with the bus compressor.

With the mouse, drag knobs and faders, and double-click one to reset it. A left click selects; menus
are on the right button. Drag a strip's name bar sideways to move it among its own kind (channels,
buses, outputs). The power symbol on the name bar switches a strip off. A strip that's off takes no
changes until you switch it back on.

Spacers are blank strips that give the desk some room, full width or narrow (right-click a strip).

The wheel scrolls the strips across; hold Shift to scroll up and down. Ctrl + and Ctrl - zoom.

## Routing

The Routing page shows every strip as a block, at its place across the desk, in rows: inputs, buses,
outputs (and output devices with no output yet). Routes run as straight lines, a lane per source.
Click a block, then another, and the route between them goes on or off. On a combinator, click the
path. Drag a block along its row to move the strip.

## Build and run

```
cmake --build build --config Release --target Faider
```

`Restart Faider.cmd` (in the repository's top folder) quits the running copy, builds,
installs to `C:\Faider` and starts it. The first time, it also makes a desktop shortcut.

## The layout files

The strips are data: `console/layout/*.json`, with positions given in the pixels of a strip 214
wide. The build copies the files beside the program, and saving one redraws
the console. Press Ctrl+Shift+O to draw a frame round every part and check the layout. Ctrl+Shift+R
reads the files again.

`{ "width": 214, "sections": [ { "name", "height", "panel"?, "items": [ ... ] } ] }`

| Kind | Fields |
|---|---|
| `knob` | `x`, `y`, `size`, `colour`, `name`, `id` |
| `key`, `switch`, `chip` | `x`, `y`, `w`, `h`, `text`, `lamp` or `colour`, `id`, `lit` |
| `leds`, `bars`, `meter` | `x`, `y`, `w`, `h`, `count`, `colours` |
| `label`, `line` | `text`, `size`, `align`; `points` |
| `fader`, `display`, `name`, `scale` | `x`, `y`, `w`, `h` (`scale`: `marks`) |
| `order`, `routes`, `pluginlist`, `paths`, `pads`, `tape`, `playlist` | `x`, `y`, `w`, `h`: drawn from the engine |

## The rules

- Every part is drawn in code (`src/ui/LargeFormat.h`). There are no image files.
- The layout is data. To move a part, change the layout file, not the drawing.
- A part looks the same wherever it's drawn.

# Contributing to Faider

Faider is free software under the [AGPLv3](LICENSE). Bugs and ideas go to
[Issues](https://github.com/dayztoolsforu/faider/issues). **Save a problem report...** on the tray
menu makes a .zip worth attaching (look inside first: it can hold device, program and window names).

## Build

You need Windows 10 (2004) or later, Visual Studio Build Tools with the C++ workload, CMake 3.22 or
later and Git.

```powershell
cmake -S . -B build
cmake --build build --config Release
```

- **JUCE.** The first configure fetches JUCE (the tag in `CMakeLists.txt`) with Git. To use a JUCE
  folder you already have, or when GitHub can't be reached, add
  `-DFETCHCONTENT_SOURCE_DIR_JUCE=C:/path/to/JUCE` to the configure.
- **ASIO SDK.** Optional. Extract Steinberg's ASIO SDK to `third_party/asiosdk_2.3.3_2019-06-14`
  (it isn't committed), or set `-DASIO_SDK_DIR=...`. Without it the build runs on Windows devices
  only. `tools\build-installer.ps1` refuses to pack such a build.
- **Warnings.** `-DFAIDER_WARNINGS_AS_ERRORS=ON` makes any compiler warning in Faider or its checks
  stop the build. The checks on GitHub build that way.
- **`Restart Faider.cmd`** quits the running copy, builds, installs to `C:\Faider` and starts it.
  It warns when the installer's copy is on the PC as well, and says which one Start with Windows
  starts.
- **The installer.** Install [Inno Setup 6](https://jrsoftware.org/isinfo.php) and run
  `tools\build-installer.ps1`. It writes `build\installer\Faider-<version>-Setup.exe`. The script
  is `installer\Faider.iss`.

## Checks

```powershell
powershell -ExecutionPolicy Bypass -File tools\run-tests.ps1
```

It builds and runs the checks in `tests\` that need no hardware. Each prints PASS or FAIL, and a
failure makes it exit non-zero. `-Tests MeterTest,PadsTest` runs only those named. AppCaptureTest
needs an audio output device, so the checks on GitHub leave it out.

The checks on GitHub (`.github/workflows/checks.yml`) build the website, check the translations
(`tools/translations.py`), build Faider without the
ASIO SDK and with warnings as errors, run the checks, and build the installer as a check (its file
and artifact are named "CI check, not for release").

## Commit messages

One plain sentence that says what Faider does now, as a user or the next developer would see it.
No prefix, no ticket number. Names on the desk are written as the desk writes them, in capitals.

```
The desk's LED meters redraw as the chosen meter standard moves them and PEAK HOLD drops on time
The uninstaller ends only a Faider running from its own folder, not another user's or a build's
```

A release is its own commit, `Version 0.2.0`, with the version changed in `CMakeLists.txt`, and a
tag, `v0.2.0`. Add the changes to [CHANGELOG.md](CHANGELOG.md).

## The manual and its translations

The manual is `docs/FAIDER-MANUAL.md`. Its 12 translations are in `docs/i18n`
(`FAIDER-MANUAL.<language>.md`). The website, `docs/index.html`, is built from them, from
`tools/manual-page` and from `docs/CREDITS.md`.

- Describe only what the code does. Names of keys, pages and menus stay in English capitals in
  every language. (The desk's keys are English in the app too; the rest of the interface follows
  SETTINGS > General > Language: see [Translations of the interface](#translations-of-the-interface).)
- Every language has the same `##` and `###` headings in the same order. The page pairs them up by
  position and gives each the English heading's id, so links work in every language. Adding,
  removing or moving a heading means doing it in all 13 files.
- A change to the English manual goes into the 12 translations too.
- The manuals say `{version}` where the page shows the version from `CMakeLists.txt`.
- Then build the page and commit `docs/index.html`, `docs/manual.html` and `docs/CREDITS.html`
  with the change:

  ```powershell
  pip install markdown==3.11.3
  python tools/manual-page/build.py
  ```

  It prints the languages it built. A translation whose headings differ, a missing image or a link
  to nowhere is printed and makes it fail. The checks on GitHub run the same build and fail when
  the committed page isn't what it builds, so use the same version of `markdown` as they do.

## Translations of the interface

The interface's English is in the code, and is the key its translations are found by: every text
shown is written `TRANS ("Save report")`, or `TRANS ("%1 is still running").replace ("%1", name)`
when it holds a value (a whole sentence, so a translator can move the value). A text that is also
used as a name inside the code (a tab's, a settings page's) is marked `NEEDS_TRANS ("Settings")`
where it's written and translated where it's shown. The desk's keys (the layout files' labels),
meter standards and units, product names, the event log and command-line output stay English.

The translations are `assets/i18n/<code>.txt`, one per language (`de`, `es`, `fr`, `it`, `ja`,
`ko`, `nl`, `pl`, `pt-BR`, `sv`, `tr`, `zh-Hans`), built into Faider.exe and set at start
(`src/Language.h`). Each line is `"English" = "translation"`, in JUCE's LocalisedStrings format
(`\"` for a quote, `\n` for a new line).

```powershell
python tools/translations.py               # check every language against the code
python tools/translations.py update de     # de.txt brought to the code's texts (new ones empty)
```

- After adding or changing a text in the code, run `update` for each language and fill in the
  empty translations. The check fails on a text missing from a language, one no longer in the code,
  an empty translation, or a translation that drops a `%1` / `%2`.
- Use the words that language's manual (`docs/i18n`) uses for the same things, keep translations
  short (the interface has little room), sentence case where the English is, ALL CAPS where the
  English is, and keep `%1`, `...`, shortcuts (Ctrl+K) and product names as they are.
- A new language: its file in `assets/i18n`, its line in `CMakeLists.txt` (FaiderAssets), in
  `Language::all()` (`src/Language.h`) and in `LANGUAGES` in `tools/translations.py`.
- `TranslationTest` (in `tools\run-tests.ps1`) loads each built-in table as the program does. The
  checks on GitHub run both.

Other documents: [docs/OSC.md](docs/OSC.md) (the OSC addresses), [docs/FAQ.md](docs/FAQ.md),
[PRIVACY.md](PRIVACY.md), [console/README.md](console/README.md).

## Where things are

```
src/engine/      the audio engine
src/devices/     Windows devices
src/ui/          the interface
src/Main.cpp     start-up and the command line
console/src/     the console and the Routing page
console/layout/  the strip layouts: mixer.json, app.json, combinator.json, master.json,
                 pads.json, tape.json
tests/           the checks
installer/       the Inno Setup script
tools/           build, install and test scripts; manual-page/ builds the website
docs/            the manual, its translations (i18n/), images and the built website
```

**Layouts.** The strips are data, `console/layout/*.json`: to move a part, change the layout file,
not the drawing code. The build copies them beside `Faider.exe`, and saving one redraws the console.
The format is in [console/README.md](console/README.md). In a Debug build, Ctrl+Shift+O draws a
frame round every part and Ctrl+Shift+R reads the files again.

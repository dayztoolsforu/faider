# Assets

What's built into Faider.exe (CMake `juce_add_binary_data`, target `FaiderAssets`):

| Path | What | Used by |
|---|---|---|
| `autoeq/profiles.txt` | Headphone EQ profiles, one line per model | Headphone EQ on an output (`src/engine/HeadphoneEq.cpp`) |

The program icons are in `icons/`. Licences: [LICENSES.md](LICENSES.md). Everything on screen is drawn in
code (`src/ui/LargeFormat.h`); no image files are used for it.

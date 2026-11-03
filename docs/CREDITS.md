# Credits

What went into Faider. The app shows the same list under SETTINGS > GENERAL > ABOUT (and in the tray menu).

## Built in

- **JUCE 8.0.12**: Raw Material Software. AGPLv3 or commercial licence. Audio and MIDI devices, VST3 hosting, audio file reading and writing, DSP building blocks, OSC, and the whole interface.
- **RNNoise 0.2**: Jean-Marc Valin, Xiph.Org Foundation, Mozilla, Amazon (Kiss FFT by Mark Borgerding). BSD 3-clause licence: [third_party/rnnoise/COPYING](../third_party/rnnoise/COPYING). The NEURAL type of each channel's NOISE REDUCTION stage: a recurrent neural network trained on speech and noise, with its built-in model. Source: https://github.com/xiph/rnnoise (release 0.2), compiled unchanged into `rnnoise.lib` (CMakeLists.txt).
- **libFLAC and libvorbis** (bundled in JUCE): Xiph.Org Foundation. BSD licence. FLAC and OGG recording.


## Standards and SDKs

- **VST3** (SDK bundled in JUCE): Steinberg Media Technologies. VST is a trademark of Steinberg Media Technologies GmbH.
- **ASIO SDK** (optional, `third_party/asiosdk_*`): Steinberg Media Technologies. GPLv3 or proprietary licence. ASIO is a trademark and software of Steinberg Media Technologies GmbH.
- **WASAPI** and per-app capture: Microsoft Windows.
- **ITU-R BS.1770 / EBU R 128**: LOUDNESS (LUFS) and true-peak metering.

## Works with (not included)

- **VB-CABLE**: VB-Audio Software. Virtual cables between apps.
- **FFmpeg**: the FFmpeg developers (LGPL/GPL). MP3, AAC, Opus and ALAC recording, only when `ffmpeg.exe` is on the PATH.

## Made with

- C++20, CMake and Microsoft Visual C++, on Windows 11.
- **Headphone correction profiles** (`assets/autoeq/profiles.txt`, built into Faider.exe): parametric EQ results from [AutoEq](https://github.com/jaakkopasanen/AutoEq) by Jaakko Pasanen, MIT licence; measurements by the sources named in each line (oratory1990, crinacle, Rtings and others).

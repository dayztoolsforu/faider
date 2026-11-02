#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class AudioEngine;
class MixerComponent;

// SIMPLE: the same mix as the console, with the processing hidden. Each channel shows its name,
// what it listens to, a big key for every output (lit: it plays there; half lit: it gets there
// through a bus), its fader, meter, CUT and SOLO; each output its device, fader and meter. Buses
// and combinators aren't shown; their settings keep working.
//
// SETUP: four questions (where you listen, your microphones, how apps hear you, which programs
// get a channel), then the mix is built from the answers. Run again, it adds what's missing and
// never removes anything.
namespace Simple
{
    std::unique_ptr<juce::Component> makeDesk (AudioEngine&, MixerComponent&, std::function<void()> openSetup);
    std::unique_ptr<juce::Component> makeSetup (AudioEngine&, std::function<void()> finished);
}

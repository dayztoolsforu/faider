#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class AudioEngine;

// SETUP: four questions (where you listen, your microphones, how apps hear you, which programs
// get a channel), then the mix is built from the answers. Run again, it adds what's missing and
// never removes anything. (Shown in its own window: MainComponent::showSetup.)
namespace MixSetup
{
    std::unique_ptr<juce::Component> make (AudioEngine&, std::function<void()> finished);
}

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_osc/juce_osc.h>
#include <map>

class AudioEngine;

// Hardware and network control.
//
// MIDI: any USB controller. MIDI learn binds a knob, fader or key to a target: right-click a
// strip (fader, pan, mute, solo) or a scene, choose MIDI LEARN, then move the control. Faders and
// knobs follow continuously; keys and notes toggle mute/solo, recall a scene, or press and
// release a macro.
//
// OSC (phones, tablets, Stream Deck OSC plugins), on a UDP port, addresses by name (spaces may be
// written as _):  /faider/input/MIC_FOCUS/fader -12.0   (dB)
//                 /faider/output/MISC/mute 1            /faider/bus/SUMMING/pan -0.5
//                 /faider/input/VR/solo 1               /faider/scene GAMING
//                 /faider/macro/GO_LIVE 1 (0 releases)
//
// Targets are strings: "fader:input:NAME", "pan:bus:NAME", "mute:output:NAME", "solo:input:NAME",
// "on:input:NAME" (power), "inserts:bus:NAME" (the INS key), "safe:output:NAME",
// "send:input:NAME:output:NAME" (a routing key), "param:input:NAME:compThreshold" (a channel's EQ,
// dynamics or FX setting), "scene:NAME", "macro:NAME", and the app's own keys through appTarget
// ("pad:3", "tape:play", "rec", "replay"). Any control on screen can be bound in MIDI MAP
// mode (MidiMap.h). Message thread, apart from the MIDI callback (handed over).
class RemoteControl : private juce::MidiInputCallback,
                      private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    explicit RemoteControl (AudioEngine&);
    ~RemoteControl() override;

    std::function<void (const juce::String&)> log;

    // MIDI inputs used (by identifier), mappings, and learning.
    juce::Array<juce::MidiDeviceInfo> availableInputs() const { return juce::MidiInput::getAvailableDevices(); }
    bool isInputOn (const juce::String& identifier) const { return enabledInputs.contains (identifier); }
    void setInputOn (const juce::String& identifier, bool on);
    void learn (const juce::String& target);   // the next control moved is bound to it
    void cancelLearn() { learnTarget.clear(); }
    juce::String learning() const { return learnTarget; }
    const std::map<juce::String, juce::String>& getMappings() const { return mapping; }   // control ("cc:1:7") -> target
    void removeMapping (const juce::String& control) { mapping.erase (control); }
    static juce::String describe (const juce::String& control);   // "CC 7 CH 1", "NOTE 36 CH 10"

    // The last control moved on any MIDI input ("cc:1:7"), and its value 0..1: the MIDI page shows it.
    juce::String lastControl;
    float lastControlValue = 0.0f;

    // Targets the engine doesn't know (the app's: "pad:3", "tape:play"...). Message thread.
    std::function<void (const juce::String& target, float value, bool pressed, bool released)> appTarget;
    void clearTarget (const juce::String& target);
    void renameTarget (const juce::String& oldKey, const juce::String& newKey);   // "input:OLD" -> "input:NEW" (also inside "fader:input:OLD")
    void clearAll() { mapping.clear(); }
    int mappingCount() const { return (int) mapping.size(); }
    juce::String controlFor (const juce::String& target) const;   // "CC 7 ch 1", or empty

    // OSC: listening on `port` (0: off). False if the port can't be opened.
    bool setOscPort (int port);
    int getOscPort() const { return oscPort; }
    int getOscPreferredPort() const { return oscPreferredPort; }   // the port last listened on (9000 to start)

    // OSC from other devices (a tablet) as well as this PC. Off: it listens on 127.0.0.1 only, so
    // nothing on the network can change the mix (OSC carries no key). Saved with the state.
    void setOscFromNetwork (bool on) { oscNetwork = on; if (oscPort > 0) setOscPort (oscPort); }
    bool isOscFromNetwork() const { return oscNetwork; }

    // One command, as OSC addresses it after /faider: { "input", "MIC FOCUS", "fader" } with a
    // value in dB, { "scene", "GAMING" }, { "macro", "GO LIVE" }. Without a value, mute and solo
    // toggle and a macro is pressed and released. False if there's no such thing, or the value
    // given isn't a finite number. Message thread.
    bool perform (const juce::StringArray& parts, bool hasValue, float value);

    // Saved with the session.
    juce::String saveState() const;
    void restoreState (const juce::String&);

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void control (const juce::String& key, float value, bool pressed, bool released);   // message thread
    void apply (const juce::String& target, float value, bool pressed, bool released, bool fromKey);   // fromKey: a note (no continuous value)
    void oscMessageReceived (const juce::OSCMessage&) override;
    void openInputs();

    AudioEngine& engine;
    std::map<juce::String, juce::String> mapping;   // "cc:1:7" / "note:1:36" -> target
    std::map<juce::String, float> lastValue;        // CCs, to see a key's press (crossing the middle)
    juce::StringArray enabledInputs;
    std::vector<std::unique_ptr<juce::MidiInput>> inputs;
    juce::String learnTarget;
    juce::OSCReceiver osc;
    std::unique_ptr<juce::DatagramSocket> oscSocket;   // what osc listens on (after osc: it goes first)
    int oscPort = 0, oscPreferredPort = 9000;
    bool oscNetwork = false;   // OSC from other devices too (else this PC only)
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);   // MIDI handed over after this has gone is dropped
};

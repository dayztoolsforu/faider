#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/AudioEngine.h"

// CONTROL > SET UP A CONTROLLER: pick the hardware and the helper takes you through what it
// needs to work with Patchbay, with each step's state lit and a key that does it:
//   LOUPEDECK (via LoupixDeck) and STREAM DECK: Patchbay's control API on; the deck's app (found,
//     running, or where to get it); Patchbay's plugin for it (installed and up to date, or INSTALL:
//     the app is closed, the plugin copied in, the app started again until the plugin is loaded).
//     Binding is then done in the deck's own menus.
//   MIDI CONTROLLER: the MIDI inputs to listen to. Binding is on the CONTROL menu (MIDI MAP).
//   PHONE / TABLET (OSC): any port to listen on, and the addresses to send to.
//   PHONE (WEB PAGE): the control API on and open to phones on the network, then a QR code of
//     the page's link (with this PC's key) to scan; NEW KEY makes old links stop working.
// The plugins come with Patchbay (plugins\ next to Patchbay.exe, put there by the build).
class ControllerHelper : public juce::Component,
                         private juce::Timer
{
public:
    struct Hooks
    {
        std::function<int()> apiPort;              // 0: off
        std::function<void()> turnApiOn;
        std::function<bool()> phoneOn;             // phone control (MainComponent::setPhoneControl)
        std::function<void (bool)> setPhone;
        std::function<juce::StringArray()> phoneLinks;   // the page's links, with the key
        std::function<void()> newPhoneKey;
    };

    enum class Device { loupedeck, streamDeck, midi, osc, phone };

    ControllerHelper (AudioEngine&, Hooks, int startOn = -1);   // startOn: a Device, or -1 for what's plugged in
    ~ControllerHelper() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static void show (AudioEngine&, Hooks, int startOn = -1);

private:
    class QrView;

    struct Step
    {
        juce::Label title, status;
        juce::TextButton action;
        int lamp = 0;   // 0 to do, 1 done, 2 needs attention
    };

    void timerCallback() override;
    void choose (Device);
    void refresh();
    void refreshSoon();
    Step& step (int index);
    void setStep (int index, const juce::String& title, const juce::String& status, int lamp,
                  const juce::String& actionText = {}, std::function<void()> action = nullptr);
    void install();   // the deck's plugin: close the app, copy, start it again until it's loaded
    void listenOnTypedPort();   // OSC on the port typed in (1024 to 65535)

    AudioEngine& engine;
    Hooks hooks;
    Device device = Device::midi;
    juce::Label heading { {}, "SET UP A CONTROLLER" }, bindText;
    juce::OwnedArray<juce::TextButton> devices;
    juce::OwnedArray<Step> steps;
    int shownSteps = 0;
    juce::OwnedArray<juce::TextButton> midiInputs;   // MIDI: one key per input
    juce::String midiShown;
    juce::Label oscPortLabel { {}, "PORT" };
    juce::TextEditor oscPort;                         // OSC: the port to listen on
    std::unique_ptr<QrView> qr;                       // PHONE: the page's link to scan

    // An install running in the background: what it's doing ("" when idle), and how it ended.
    struct Progress
    {
        juce::CriticalSection lock;
        juce::String text, result;
        std::atomic<bool> running { false };
        void set (const juce::String& t) { const juce::ScopedLock sl (lock); text = t; }
        juce::String get() { const juce::ScopedLock sl (lock); return text; }
        juce::String getResult() { const juce::ScopedLock sl (lock); return result; }
    };
    std::shared_ptr<Progress> progress = std::make_shared<Progress>();
};

#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <map>

class AudioEngine;

// The local control API, for deck plugins (Stream Deck, Loupedeck) and scripts. HTTP on
// 127.0.0.1 (nothing outside this PC can reach it) unless phone control is on, port 9123 by default.
//
//   GET  /api/state                                   what's on the desk, as JSON (below)
//   GET  /api/meters                                  every channel's L/R peaks in dB: { "input:MIC FOCUS": [-18.2, -17.9] }
//   POST /api/input|bus|output/<NAME>/fader?value=-12  fader in dB (-60 or less: off)
//   POST /api/input|bus/<NAME>/pan?value=-0.5          pan, -1 to 1
//   POST /api/input|bus|output/<NAME>/mute[?value=1]   mute on/off; no value: toggle (solo the same)
//   POST /api/scene/<NAME>                             recall a scene
//   POST /api/macro/<NAME>[?value=1|0]                 press / release; no value: press and release
//   POST /api/pad/<N>[?value=1|0]                      pad N (1-64) pressed / let go; no value: press and let go
//   GET  /                                             the phone control page (phone control on)
//
// Names are as on the strips; spaces as %20 or _, any case. Commands go through RemoteControl,
// as OSC does. A command needs POST with the header "X-Faider: 1": a web page in a browser
// can't add that header to a request here without asking first (which this server refuses), so
// a page can't work the desk.
//
// /api/state: { "inputs": [ { "name", "fader" (dB), "mute", "solo", "pan", "level" (dB peak), "on" } ],
//               "buses": [ ... no "solo" ], "outputs": [ ... no "solo" or "pan" ],
//               "scenes": [ names ], "activeScene": name or "", "macros": [ names ],
//               "pads": [ { "pad" (1-64), "name" } ] (those with a sound or a macro) }
class ControlServer : private juce::Thread
{
public:
    ControlServer (AudioEngine&);
    ~ControlServer() override;

    static constexpr int defaultPort = 9123;

    // Listening on `port` (0: off). False if the port can't be opened (in use).
    bool setPort (int port);
    int getPort() const { return port; }

    // Phone control: the server also answers other devices on the network (a phone on the same
    // Wi-Fi), each request carrying `key` (?key= or the header X-Faider-Key), and serves them
    // a control page at / (PhonePage.h). Off: this PC only, as before. Windows may ask once to
    // let Faider through its firewall. False if the port can't be opened that way: the server
    // then reopens for this PC only, and if even that fails it's off (check getPort() after).
    bool setPhone (bool on, const juce::String& key);
    bool isPhoneOn() const { return phone; }
    static juce::StringArray phoneAddresses (int port, const juce::String& key);   // "http://192.168.1.20:9123/?key=..."
    static juce::String newKey();   // 12 letters and digits
    static bool isOldKey (const juce::String& key);   // shorter than newKey's (the old 8): worth a new one

private:
    void run() override;
    void serve (juce::StreamingSocket&);
    void stop();
    bool open (int port);
    bool phone = false;
    juce::String phoneKey;
    struct Tries { juce::uint32 since = 0; int count = 0; };
    std::map<juce::String, Tries> wrongKeys;   // server thread: wrong keys per address, this minute (only addresses that sent one)
    juce::String state() const;    // message thread
    juce::String meters() const;   // message thread

    // Runs f on the message thread and waits for it (unless the server is stopping).
    bool onMessageThread (std::function<void()> f);

    AudioEngine& engine;
    std::unique_ptr<juce::StreamingSocket> listener;
    int port = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

#pragma once

#include <juce_events/juce_events.h>
#include <map>
#include <memory>

// One audio device as Windows reports it. Devices are identified by `id`, never by
// "Windows default", so SteamVR switching the default can't move our routing.
struct EndpointInfo
{
    juce::String id;        // WASAPI endpoint ID, or "asio:<CLSID>"
    juce::String api;       // "WASAPI" or "ASIO"
    juce::String name;
    bool isOutput = false;  // WASAPI only; ASIO drivers are duplex
    int state = 0;          // DEVICE_STATE_* (1 active, 2 disabled, 4 not present, 8 unplugged)
    int formFactor = -1;    // EndpointFormFactor; 9 = HDMI/DisplayPort audio
    int channels = 0;
    int sampleRate = 0;
    juce::String adapter;           // physical device it belongs to, e.g. "Focusrite USB Audio"
    bool isDefault = false;         // Windows default playback/recording device
    bool isDefaultComms = false;    // Windows default device for calls

    bool isActive() const { return state == 1; }
};

// True if an ASIO driver name and a Windows adapter name describe the same hardware,
// e.g. "Focusrite USB ASIO" and "Focusrite USB Audio" (they share "Focusrite USB").
bool asioMatchesAdapter (const juce::String& asioName, const juce::String& adapter);

// Windows' own name for an endpoint, minus labels that read backwards from Windows' side.
// VoiceMeeter names its devices from the mixer's view: "Voicemeeter Out B2" is a Windows
// *recording* device, "Voicemeeter In 3" a *playback* one. Drop the In/Out words.
// A name the user gave the device in Patchbay (if any) wins. Windows' own name is unchanged.
juce::String endpointDisplayName (const EndpointInfo&);

// Enables or disables a Windows audio device the way Sound settings does (no administrator
// prompt). `endpointId` is EndpointInfo::id. False if Windows refused.
bool setEndpointEnabled (const juce::String& endpointId, bool enabled);

// The user's names for devices (Patchbay only; saved with the session). An empty alias removes it.
void setDeviceAlias (const juce::String& deviceName, const juce::String& alias);
juce::String deviceAlias (const juce::String& deviceName);
juce::StringPairArray deviceAliases();

// For a virtual-cable playback device (VB-CABLE, CABLE-A/B, Hi-Fi Cable): the recording device
// on the same cable, which is what Discord/OBS/games pick as their mic, e.g. "CABLE Output".
// Empty for any other device.
juce::String virtualMicPartner (const EndpointInfo&, const juce::Array<EndpointInfo>& all);

// Flags stored in the device database: hdmi, vr, transient (may vanish at any time),
// master (allowed to be the master clock).
juce::StringArray classifyEndpoint (const EndpointInfo&);

juce::String endpointStateName (int state);

// Watches Windows audio endpoints (IMMNotificationClient) and ASIO drivers, and reports
// the full list plus what changed. Notifications arrive on a COM thread; they only set a
// flag, and the rescan runs on the message thread.
class EndpointWatcher : private juce::Timer
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void endpointsChanged (const juce::Array<EndpointInfo>& endpoints,
                                       const juce::StringArray& events) = 0;
    };

    explicit EndpointWatcher (Listener&);
    ~EndpointWatcher() override;

    void rescan();

private:
    void timerCallback() override;

    struct Impl;
    std::unique_ptr<Impl> impl;
    Listener& listener;
    std::map<juce::String, EndpointInfo> last;
    bool firstScan = true;
};

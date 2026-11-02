#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/AudioEngine.h"
#include "../ui/MixerComponent.h"
#include "../ui/PanelSlot.h"

namespace vr { class IVROverlay; }
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

// The desk in the headset: a PATCHBAY tab in the SteamVR dashboard. Its sections are keys along the
// bottom, where the pointer reaches easily: RACK (a channel's rack, the Mixer's RACK page, with the
// channels as keys above the sections) and the desk's own PADS, TAPE, MACROS and DEVICES, borrowed
// from their places on the desk while the dashboard shows them. The laser pointer works it as a
// mouse: the trigger clicks and drags, the stick or pad scrolls. Menus and dialogs opened from it
// open in it (a file dialog can't: the page says to finish it on the desktop), and a text box
// clicked brings up SteamVR's keyboard.
//
// It waits for SteamVR to be fully running (it never starts it), attaches its overlay then, and
// lets go when SteamVR quits or goes away. The page is drawn only while the dashboard shows it. The
// page lives off screen as a window of its own, so the pointer's clicks go through JUCE as a pen's
// would (a pointer of its own, apart from the mouse). Message thread.
class VrDashboard : private juce::Timer
{
public:
    VrDashboard (AudioEngine&, MixerComponent&);
    ~VrDashboard() override;

    void setEnabled (bool);   // off: no overlay, SteamVR left alone
    bool isEnabled() const { return enabled; }

    std::function<void (const juce::String&)> log;

    // The desk's pages the headset shows as sections after RACK (PADS, TAPE...), borrowed from their
    // slots while the dashboard is open. Set before SteamVR is joined; they must outlive this.
    struct Section { juce::String name; PanelSlot* slot; };
    std::vector<Section> sections;

private:
    class Page;

    void timerCallback() override;

    // SteamVR's processes: running (server and compositor both), and the server's process id.
    struct SteamVrState { bool up = false, any = false; unsigned long serverPid = 0; };
    SteamVrState steamVr() const;
    void scan();                          // not attached: join SteamVR when it's ready
    int upScans = 0;                      // scans in a row that found it fully up
    unsigned long quitServer = 0;         // it quit: not joined again while this server runs
    unsigned long attachedServer = 0;     // the server joined
    int retryIn = 0, retryDelay = 0;      // a failed join: scans until the next try, and the back-off
    bool failureLogged = false;

    void attach();
    void detach (bool steamVrGone = false);
    void pollEvents();
    void render();
    bool makeTexture();     // on the headset's graphics card
    void releaseTexture();

    void releasePointer();                // a press in progress let go, and the pointer moved off the page
    void hostModal();                     // a dialog opened from the headset: shown in the page
    void returnModal();                   // and back on the desktop when the dashboard closes
    void openKeyboardFor (juce::TextEditor&);
    void keyboardDone();                  // its text into the text box

    AudioEngine& engine;
    MixerComponent& mixer;
    std::unique_ptr<Page> page;
    vr::IVROverlay* overlay = nullptr;
    juce::uint64 handle = 0, thumbnail = 0;
    bool enabled = true, down = false, showing = false;
    bool pressOnButton = false;           // the press in progress began on a button
    juce::Point<float> pointer;
    int ticks = 0;
    int textureFailures = 0;              // SteamVR refusing the page's picture, ticks in a row
    juce::uint32 lastPointerUse = 0;      // the pointer's last event (dialogs opening while it's in use are for the headset)
    juce::Component::SafePointer<juce::Component> hosted;
    int hostedFlags = 0;
    bool hostedOnTop = false;             // (as it was on the desktop)
    juce::Component::SafePointer<juce::Component> elsewhere;   // a dialog that stays on the desktop (a file dialog)
    juce::Component::SafePointer<juce::TextEditor> typingInto;

    // The page goes to SteamVR as a texture on the headset's graphics card, updated in place: handing
    // it whole new pictures (SetOverlayRaw) twenty times a second made it blink in the headset.
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Texture2D* texture = nullptr;
    juce::Image frame;   // the page drawn here (B, G, R, A in memory, as the texture wants)
    bool fullFrameDue = true;   // the next frame draws the whole page (it just opened)
    int framesSinceFull = 0;    // frames since the last whole one (one every two seconds anyway)
};

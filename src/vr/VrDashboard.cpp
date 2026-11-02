#ifndef NOMINMAX
 #define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <dxgi.h>
#pragma comment (lib, "d3d11.lib")
#pragma comment (lib, "dxgi.lib")

#include "VrDashboard.h"
#include "../ProductName.h"
#include <set>
#include <string>
#include "../ui/Console.h"
#include <openvr.h>

namespace
{
    constexpr int pageWidth = 1600, pageHeight = 960;   // the page, in its own pixels
    constexpr float widthInMetres = 2.4f;               // how wide it hangs in the dashboard
    const char* overlayKey = "patchbay.rack";
    constexpr int scanTicks = 100;                      // SteamVR looked for every 5 s (20 ticks a second)
    constexpr juce::uint32 pointerInUseMs = 30000;      // a dialog opening this soon after the pointer was used is for the headset
                                                        // (a plugin's test can take a while to say it failed)
    constexpr int textureFailuresToRejoin = 20;         // a second of SteamVR refusing the picture: join again
}

//==============================================================================
// The page in the headset. Along the bottom, where the pointer reaches easily: the sections (RACK,
// then the desk's own PADS, TAPE, MACROS, DEVICES) and, on RACK, the channels above them.
//
// The other sections are the desk's own pages, borrowed from their slots on the desk (PanelSlot)
// while the dashboard shows them, and handed back when it closes or another section is picked.
class VrDashboard::Page : public juce::Component
{
public:
    Page (AudioEngine& e, MixerComponent& m, const std::vector<Section>& s) : engine (e), mixer (m), sections (s)
    {
        setOpaque (true);
        title.setText (Product::nameUpper, juce::dontSendNotification);
        title.setFont (Console::legendFont (Console::Type::display));
        title.setColour (juce::Label::textColourId, Console::Colours::legend());
        addAndMakeVisible (title);

        juce::StringArray names { "RACK" };
        for (auto& sec : sections)
            names.add (sec.name);
        for (auto& name : names)
        {
            auto* k = sectionKeys.add (makeKey (name, 2));
            k->onClick = [this, name] { showSection (name); };
        }
        sectionKeys[0]->setToggleState (true, juce::dontSendNotification);

        setSize (pageWidth, pageHeight);
        refreshKeys();
    }

    ~Page() override { lend (false); }

    void paint (juce::Graphics& g) override
    {
        if (! drawingFrame)   // (a repaint asked for by the page itself: what changed, for the headset's next frame)
            dirty = dirty.getUnion (g.getClipBounds());
        g.fillAll (Console::Colours::panel());
    }

    // What changed since the headset's last frame (and forgotten): render() draws and uploads only that.
    juce::Rectangle<int> takeDirty() { return std::exchange (dirty, {}); }
    bool drawingFrame = false;   // render() drawing the headset's frame (not a change of the page's own)

    // A dialog that stayed on the desktop (a file dialog: Windows' own window, which can't come into
    // the page) and holds the page until it's finished: said over everything, fading in.
    void setNotice (bool on)
    {
        if (on && noticeSince == 0.0)
            noticeSince = juce::Time::getMillisecondCounterHiRes();
        else if (! on)
            noticeSince = 0.0;
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (noticeSince == 0.0)
            return;
        const float fade = (float) juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - noticeSince) / 300.0);
        g.fillAll (juce::Colours::black.withAlpha (0.6f * fade));
        auto box = getLocalBounds().withSizeKeepingCentre (760, 180).translated (0, juce::roundToInt (24.0f * (1.0f - fade)));
        g.setColour (Console::Colours::panelDark().withAlpha (fade));
        g.fillRoundedRectangle (box.toFloat(), 6.0f);
        g.setColour (Console::Colours::rim().withAlpha (fade));
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 6.0f, 1.0f);
        box.reduce (Console::Space::pad, Console::Space::pad);
        g.setColour (Console::Colours::legend().withAlpha (fade));
        g.setFont (Console::legendFont (Console::Type::display));
        g.drawText ("FINISH THIS ON THE DESKTOP", box.removeFromTop (box.getHeight() / 2), juce::Justification::centredBottom);
        g.setColour (Console::Colours::legendDim().withAlpha (fade));
        g.setFont (Console::legendFont (Console::Type::body));
        g.drawText ("A WINDOW OPENED ON THE MONITOR. THE PAGE WAITS FOR IT", box, juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (Console::Space::pad);
        layRow (r.removeFromBottom (rowHeight), sectionKeys, &title);
        r.removeFromBottom (Console::Space::gap);
        if (section == "RACK")
        {
            layRow (r.removeFromBottom (rowHeight), keys, nullptr);
            r.removeFromBottom (Console::Space::gap);
        }
        content = r;
        if (rack != nullptr)
            rack->setBounds (r);
        if (lent != nullptr && lent->panel.getParentComponent() == this)
            lent->panel.setBounds (r);
    }

    // The section's page borrowed from its slot on the desk (true: the dashboard is showing), or
    // handed back.
    void lend (bool on)
    {
        if (on && lent == nullptr)
        {
            for (auto& sec : sections)
                if (sec.name == section && sec.slot != nullptr && ! sec.slot->isLent())
                {
                    lent = sec.slot;
                    addAndMakeVisible (lent->panel);
                    lent->panel.setBounds (content);
                    lent->repaint();   // (it says where its page went)
                }
        }
        else if (! on && lent != nullptr)
        {
            lent->takeBack();
            lent = nullptr;
        }
    }

    // The channel keys follow the desk (channels added, removed, renamed, or made again by a
    // session load); the chosen one's rack is made again if its channel went or was replaced.
    void refreshKeys()
    {
        juce::StringArray now;
        std::vector<const void*> ids;
        for (auto& s : engine.getStrips())
            if (s->channel != nullptr)
            {
                now.add ("input:" + s->name);
                ids.push_back (s.get());
            }
        for (auto& p : engine.getBuses())
            if (p->channel != nullptr)
            {
                now.add ("bus:" + p->name);
                ids.push_back (p.get());
            }
        const int at = now.indexOf (chosen);
        const bool chosenReplaced = at >= 0 && ids[(size_t) at] != chosenId;   // same name, a new channel
        if (now != shown)
        {
            shown = now;
            keys.clear();
            for (auto& key : shown)
            {
                auto* k = keys.add (makeKey (key.fromFirstOccurrenceOf (":", false, false), 1));
                k->onClick = [this, key] { choose (key); };
                k->setVisible (section == "RACK");
            }
        }
        else if (! chosenReplaced)
            return;

        if (at < 0)
            choose (shown.isEmpty() ? juce::String() : shown[0]);
        else if (chosenReplaced)
            choose (chosen);
        else
            lightKeys();
        resized();
    }

private:
    // Keys sized for a laser pointer at arm's length: taller than the desk's, a column wider.
    static constexpr int rowHeight = 56, keyHeight = 48;

    juce::TextButton* makeKey (const juce::String& text, int group)
    {
        auto* k = new juce::TextButton (text);
        k->setClickingTogglesState (true);
        k->setRadioGroupId (group);
        k->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
        addAndMakeVisible (k);
        return k;
    }

    static void layRow (juce::Rectangle<int> area, juce::OwnedArray<juce::TextButton>& row, juce::Component* first)
    {
        Console::KeyRow bar (area, keyHeight);
        if (first != nullptr)
            bar.leftWidth (*first, Console::Space::columns (2)).group();
        for (auto* k : row)
            bar.left (*k, Console::KeyRow::columnsFor (k->getButtonText()) + 1);
    }

    void showSection (const juce::String& name)
    {
        lend (false);
        section = name;
        for (auto* k : sectionKeys)
            k->setToggleState (k->getButtonText() == name, juce::dontSendNotification);
        for (auto* k : keys)
            k->setVisible (name == "RACK");
        if (rack != nullptr)
            rack->setVisible (name == "RACK");
        resized();
        lend (true);
    }

    void choose (const juce::String& key)
    {
        chosen = key;
        chosenId = nullptr;
        for (auto& s : engine.getStrips())
            if ("input:" + s->name == key) chosenId = s.get();
        for (auto& p : engine.getBuses())
            if ("bus:" + p->name == key) chosenId = p.get();
        rack = key.isNotEmpty() ? mixer.createRackPage (key) : nullptr;
        if (rack != nullptr)
        {
            addChildComponent (*rack);
            rack->setVisible (section == "RACK");
        }
        lightKeys();
        resized();
    }

    void lightKeys()
    {
        for (int i = 0; i < keys.size(); ++i)
            keys[i]->setToggleState (shown[i] == chosen, juce::dontSendNotification);
    }

    AudioEngine& engine;
    MixerComponent& mixer;
    const std::vector<Section>& sections;
    juce::Rectangle<int> dirty;   // repainted since the headset's last frame
    juce::Label title;
    juce::OwnedArray<juce::TextButton> keys, sectionKeys;
    juce::StringArray shown;
    const void* chosenId = nullptr;   // the chosen channel itself (a session load can replace it, same name)
    juce::String chosen, section { "RACK" };
    std::unique_ptr<juce::Component> rack;
    juce::Rectangle<int> content;
    PanelSlot* lent = nullptr;   // the desk's page on show here
    double noticeSince = 0.0;    // when "finish this on the desktop" came up (0: not up)
};

//==============================================================================
VrDashboard::VrDashboard (AudioEngine& e, MixerComponent& m) : engine (e), mixer (m)
{
    startTimerHz (20);
}

VrDashboard::~VrDashboard()
{
    stopTimer();
    detach();
}

void VrDashboard::setEnabled (bool on)
{
    enabled = on;
    if (! on)
        detach();
}

// SteamVR's processes, looked for (so Patchbay never starts SteamVR itself).
VrDashboard::SteamVrState VrDashboard::steamVr() const
{
    SteamVrState state;
    bool compositor = false;
    if (auto snap = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0); snap != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W entry { sizeof (entry) };
        for (bool more = Process32FirstW (snap, &entry); more; more = Process32NextW (snap, &entry))
        {
            if (_wcsicmp (entry.szExeFile, L"vrserver.exe") == 0)
                state.serverPid = entry.th32ProcessID;
            compositor = compositor || _wcsicmp (entry.szExeFile, L"vrcompositor.exe") == 0;
        }
        CloseHandle (snap);
    }
    state.up = state.serverPid != 0 && compositor;
    state.any = state.serverPid != 0 || compositor;
    return state;
}

// Every 5 s while not attached. Joined only once SteamVR has been fully up for two looks in a row
// (not while it's starting or shutting down: a join it refuses then crashed Patchbay a moment
// later), never again with the server that just quit, and after a failed join only after a wait
// that doubles each time.
void VrDashboard::scan()
{
    const auto state = steamVr();
    if (quitServer != 0 && state.serverPid != quitServer)
        quitServer = 0;   // that SteamVR has gone (another may have started)
    upScans = state.up ? upScans + 1 : 0;
    if (! state.any)
    {
        retryIn = retryDelay = 0;
        failureLogged = false;
    }
    if (upScans < 2 || quitServer != 0 || --retryIn > 0)
        return;

    // Without openvr_api.dll (it's loaded only when needed) there's no dashboard, and nothing else changes.
    static const bool openVrThere = LoadLibraryW (L"openvr_api.dll") != nullptr;
    if (! openVrThere)
    {
        if (log) log ("vr: openvr_api.dll is missing, so there's no SteamVR dashboard");
        enabled = false;
        return;
    }

    attach();
    if (overlay == nullptr)   // failed: back off (5 s, 10 s, 20 s ... a minute)
    {
        retryDelay = juce::jlimit (1, 12, retryDelay * 2);
        retryIn = retryDelay;
    }
}

// SteamVR's client library loads graphics drivers into Patchbay (its Vulkan interop: on AMD,
// amdvlk64 and the amdlvr64 layer) and unloads them at VR_Shutdown, while a driver timer can
// still be pending on a Windows worker thread: it then runs into unloaded code and Patchbay
// crashes a few seconds after SteamVR closes. So every module that arrived with SteamVR is pinned
// (kept loaded for good) before letting go of it.
namespace
{
    std::set<std::wstring> modulesNow()
    {
        std::set<std::wstring> paths;
        if (const auto snap = CreateToolhelp32Snapshot (TH32CS_SNAPMODULE, GetCurrentProcessId()); snap != INVALID_HANDLE_VALUE)
        {
            MODULEENTRY32W m { sizeof (m) };
            for (BOOL ok = Module32FirstW (snap, &m); ok; ok = Module32NextW (snap, &m))
                paths.insert (m.szExePath);
            CloseHandle (snap);
        }
        return paths;
    }

    std::set<std::wstring> modulesBeforeVr;

    // Returns how many were pinned, and whether SteamVR's own client library (vrclient_x64.dll:
    // the one the crash read into after it was unloaded, per the 31 Oct crash dump) was among them.
    std::pair<int, bool> pinModulesSinceVr()
    {
        int count = 0;
        bool client = false;
        for (auto& path : modulesNow())
            if (modulesBeforeVr.count (path) == 0)
            {
                HMODULE pinned = nullptr;
                if (GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_PIN, path.c_str(), &pinned))
                {
                    ++count;
                    client = client || juce::File (juce::String (path.c_str())).getFileName().equalsIgnoreCase ("vrclient_x64.dll");
                }
            }
        return { count, client };
    }
}

void VrDashboard::attach()
{
    const auto state = steamVr();   // (again, just before: VR_Init would start SteamVR if it had closed)
    if (! state.up)
        return;

    auto failed = [this] (const juce::String& why)
    {
        if (log && ! std::exchange (failureLogged, true))
            log ("vr: " + why + " (trying again now and then)");
    };

    if (modulesBeforeVr.empty())
        modulesBeforeVr = modulesNow();   // (what Patchbay had before SteamVR's libraries came)
    vr::EVRInitError error = vr::VRInitError_None;
    vr::VR_Init (&error, vr::VRApplication_Overlay);
    if (error != vr::VRInitError_None)
    {
        failed ("couldn't join SteamVR (" + juce::String (vr::VR_GetVRInitErrorAsEnglishDescription (error)) + ")");
        return;
    }
    overlay = vr::VROverlay();
    vr::VROverlayHandle_t main = vr::k_ulOverlayHandleInvalid, thumb = vr::k_ulOverlayHandleInvalid;
    if (overlay == nullptr || overlay->CreateDashboardOverlay (overlayKey, Product::name, &main, &thumb) != vr::VROverlayError_None)
    {
        failed ("SteamVR wouldn't take the dashboard (is Patchbay's already there?)");
        overlay = nullptr;
        pinModulesSinceVr();
        vr::VR_Shutdown();
        return;
    }
    handle = main;
    thumbnail = thumb;
    overlay->SetOverlayWidthInMeters (handle, widthInMetres);
    overlay->SetOverlayInputMethod (handle, vr::VROverlayInputMethod_Mouse);
    vr::HmdVector2_t scale { (float) pageWidth, (float) pageHeight };
    overlay->SetOverlayMouseScale (handle, &scale);
    overlay->SetOverlayFlag (handle, vr::VROverlayFlags_SendVRSmoothScrollEvents, true);

    // The dashboard's tab icon: the rack's green lamp on charcoal.
    juce::Image icon (juce::Image::ARGB, 64, 64, true);
    {
        juce::Graphics g (icon);
        g.setColour (juce::Colour (0xff2b2d30));
        g.fillRoundedRectangle (2.0f, 2.0f, 60.0f, 60.0f, 8.0f);
        for (int row = 0; row < 3; ++row)
        {
            g.setColour (juce::Colour (0xff6c7178));
            g.fillRect (10.0f, 12.0f + 15.0f * (float) row, 44.0f, 10.0f);
            g.setColour (juce::Colour (0xff3fe07a));
            g.fillEllipse (44.0f, 14.0f + 15.0f * (float) row, 6.0f, 6.0f);
        }
    }
    juce::HeapBlock<juce::uint8> raw (64 * 64 * 4);
    const juce::Image::BitmapData data (icon, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
        {
            const auto c = data.getPixelColour (x, y);
            auto* p = raw.get() + (y * 64 + x) * 4;
            p[0] = c.getRed(); p[1] = c.getGreen(); p[2] = c.getBlue(); p[3] = c.getAlpha();
        }
    overlay->SetOverlayRaw (thumbnail, raw.get(), 64, 64, 4);   // (once: fine for the icon)

    if (! makeTexture())
    {
        failed ("couldn't make the dashboard's texture on the headset's graphics card");
        overlay->DestroyOverlay (handle);
        overlay = nullptr;
        pinModulesSinceVr();
        vr::VR_Shutdown();
        return;
    }

    // The page: a window of its own, off screen, so the pointer's events go through JUCE.
    page = std::make_unique<Page> (engine, mixer, sections);
    showing = false;
    page->setTopLeftPosition (-30000, -30000);
    page->addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses);
    page->setVisible (true);
    if (auto* peer = page->getPeer())
    {
        auto hwnd = (HWND) peer->getNativeHandle();
        SetWindowLongPtrW (hwnd, GWL_EXSTYLE, GetWindowLongPtrW (hwnd, GWL_EXSTYLE) | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
    }
    frame = juce::Image (juce::Image::ARGB, pageWidth, pageHeight, true, juce::SoftwareImageType());
    fullFrameDue = true;

    // Menus and combo-box lists opened from the page open in it (Console::Look).
    Console::Look::menuParent = [this] (const juce::PopupMenu::Options& o) -> juce::Component*
    {
        auto* target = o.getTargetComponent();
        return page != nullptr && target != nullptr && page->isParentOf (target) ? page.get() : nullptr;
    };

    attachedServer = state.serverPid;
    retryIn = retryDelay = 0;
    failureLogged = false;
    if (log) log ("vr: Patchbay is on the SteamVR dashboard");
}

// `steamVrGone`: SteamVR went without saying (killed, crashed), so nothing is asked of it.
void VrDashboard::detach (bool steamVrGone)
{
    Console::Look::menuParent = nullptr;
    returnModal();
    if (page != nullptr)
        releasePointer();
    page = nullptr;
    typingInto = nullptr;
    elsewhere = nullptr;
    textureFailures = 0;
    releaseTexture();   // (Patchbay's picture let go before SteamVR is)
    if (overlay != nullptr)
    {
        if (! steamVrGone)
            overlay->DestroyOverlay (handle);
        overlay = nullptr;
        const auto [pinned, client] = pinModulesSinceVr();   // (its libraries and drivers stay loaded: see above)
        vr::VR_Shutdown();
        if (log)
        {
            log (steamVrGone ? "vr: SteamVR went away" : "vr: left SteamVR");
            log ("vr: kept " + juce::String (pinned) + " of SteamVR's libraries loaded" + (client ? " (its client too)" : ""));
        }
    }
    showing = false;
    upScans = 0;
}

// A texture the page's size on the graphics card the headset runs on (SteamVR says which).
bool VrDashboard::makeTexture()
{
    int32_t adapterIndex = 0;
    if (auto* system = vr::VRSystem())
        system->GetDXGIOutputInfo (&adapterIndex);

    IDXGIFactory1* factory = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    if (SUCCEEDED (CreateDXGIFactory1 (__uuidof (IDXGIFactory1), (void**) &factory)))
    {
        if (FAILED (factory->EnumAdapters1 ((UINT) juce::jmax (0, adapterIndex), &adapter)))
            adapter = nullptr;
        factory->Release();
    }
    const auto made = D3D11CreateDevice (adapter, adapter != nullptr ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                                         nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                         D3D11_SDK_VERSION, &device, nullptr, &context);
    if (adapter != nullptr)
        adapter->Release();
    if (FAILED (made))
        return false;

    D3D11_TEXTURE2D_DESC desc {};
    desc.Width = pageWidth;
    desc.Height = pageHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;   // SteamVR opens it from its own process
    if (FAILED (device->CreateTexture2D (&desc, nullptr, &texture)))
    {
        releaseTexture();
        return false;
    }
    return true;
}

void VrDashboard::releaseTexture()
{
    if (texture != nullptr) { texture->Release(); texture = nullptr; }
    if (context != nullptr) { context->Release(); context = nullptr; }
    if (device != nullptr)  { device->Release();  device = nullptr; }
}

void VrDashboard::timerCallback()
{
    if (! enabled)
        return;
    if (overlay == nullptr)
    {
        if (++ticks % scanTicks == 0)
            scan();
        return;
    }

    // Still there? Killed or crashed, SteamVR sends no Quit: let go without asking it anything.
    if (++ticks % scanTicks == 0 && steamVr().serverPid != attachedServer)
    {
        detach (true);
        return;
    }

    pollEvents();
    if (overlay == nullptr)
        return;
    if (ticks % 20 == 0 && page != nullptr)
        page->refreshKeys();

    const bool nowShowing = overlay->IsDashboardVisible() && overlay->IsActiveDashboardOverlay (handle);
    if (nowShowing != showing && page != nullptr)
    {
        if (! nowShowing)
        {
            releasePointer();   // (a drag in progress doesn't carry on when it opens again)
            returnModal();
        }
        page->lend (nowShowing);   // the desk gets its page back while the dashboard is closed
        fullFrameDue = true;
    }
    showing = nowShowing;
    if (showing)
    {
        hostModal();
        render();
    }
}

// The pointer's moves, clicks and scrolls, into the page as a pen's; the keyboard's text; SteamVR
// quitting. The overlay's own events first, so its keyboard's DONE is never read after anything
// global about that keyboard.
void VrDashboard::pollEvents()
{
    vr::VREvent_t e;
    auto* peer = page != nullptr ? page->getPeer() : nullptr;
    const float scale = page != nullptr ? page->getDesktopScaleFactor() : 1.0f;
    auto send = [&] (juce::ModifierKeys mods)
    {
        if (peer != nullptr)
            peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::pen, pointer * scale, mods,
                                    juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, juce::Time::currentTimeMillis());
    };
    while (overlay != nullptr && overlay->PollNextOverlayEvent (handle, &e, sizeof (e)))
    {
        switch (e.eventType)
        {
            case vr::VREvent_MouseMove:
                pointer = { e.data.mouse.x, (float) pageHeight - e.data.mouse.y };   // (the overlay's y runs up)
                lastPointerUse = juce::Time::getMillisecondCounter();
                send (juce::ModifierKeys (down ? juce::ModifierKeys::leftButtonModifier : 0));
                break;
            case vr::VREvent_MouseButtonDown:
                pointer = { e.data.mouse.x, (float) pageHeight - e.data.mouse.y };
                down = true;
                lastPointerUse = juce::Time::getMillisecondCounter();
                pressOnButton = false;
                if (page != nullptr)
                    for (auto* c = page->getComponentAt (pointer.toInt()); c != nullptr && ! pressOnButton; c = c->getParentComponent())
                        pressOnButton = dynamic_cast<juce::Button*> (c) != nullptr;
                send (juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier));
                break;
            case vr::VREvent_MouseButtonUp:
            {
                pointer = { e.data.mouse.x, (float) pageHeight - e.data.mouse.y };
                down = false;
                lastPointerUse = juce::Time::getMillisecondCounter();
                send (juce::ModifierKeys());
                // A text box clicked: SteamVR's keyboard to type into it (the page takes no keys).
                if (page != nullptr)
                    for (auto* c = page->getComponentAt (pointer.toInt()); c != nullptr; c = c->getParentComponent())
                        if (auto* editor = dynamic_cast<juce::TextEditor*> (c))
                        {
                            if (editor->isEnabled() && ! editor->isReadOnly())
                                openKeyboardFor (*editor);
                            break;
                        }
                break;
            }
            case vr::VREvent_FocusLeave:
            case vr::VREvent_OverlayHidden:
                releasePointer();
                break;
            case vr::VREvent_KeyboardDone:
                keyboardDone();
                break;
            case vr::VREvent_ScrollSmooth:
                lastPointerUse = juce::Time::getMillisecondCounter();
                if (peer != nullptr)
                {
                    juce::MouseWheelDetails wheel;
                    wheel.deltaX = e.data.scroll.xdelta * 0.5f;
                    wheel.deltaY = e.data.scroll.ydelta * 0.5f;
                    wheel.isReversed = false;
                    wheel.isSmooth = true;
                    wheel.isInertial = false;
                    peer->handleMouseWheel (juce::MouseInputSource::InputSourceType::pen, pointer * scale, juce::Time::currentTimeMillis(), wheel);
                }
                break;
            default:
                break;
        }
    }

    if (auto* system = vr::VRSystem())
        while (system->PollNextEvent (&e, sizeof (e)))
        {
            if (e.eventType == vr::VREvent_Quit)
            {
                // (Not AcknowledgeQuit_Exiting: that tells SteamVR this process is about to end.)
                quitServer = attachedServer;
                detach();
                return;
            }
            // (DONE may come here as well as to the overlay: only ours, and a second one finds nothing to do.)
            if (e.eventType == vr::VREvent_KeyboardDone && e.data.keyboard.overlayHandle == handle)
                keyboardDone();
        }
}

// The keyboard closing (ours or anyone's, here or globally) isn't acted on: its DONE might still be
// on its way, and only DONE puts text in. The text box stays the keyboard's until DONE, or until the
// keyboard is opened for another one.
void VrDashboard::openKeyboardFor (juce::TextEditor& editor)
{
    if (overlay->ShowKeyboardForOverlay (handle, vr::k_EGamepadTextInputModeNormal, vr::k_EGamepadTextInputLineModeSingleLine,
                                         vr::KeyboardFlag_Modal, Product::name, 256, editor.getText().toRawUTF8(), 0) == vr::VROverlayError_None)
        typingInto = &editor;   // (not if it was already up for another box: its text is that box's)
}

// DONE: what was typed replaces the text box's text, typed in as keys would be (so its filter, such
// as capitals only, and its length limit apply).
void VrDashboard::keyboardDone()
{
    if (typingInto != nullptr && overlay != nullptr)
    {
        char text[1024] = {};
        overlay->GetKeyboardText (text, sizeof (text));
        typingInto->selectAll();
        typingInto->insertTextAtCaret (juce::String::fromUTF8 (text));
    }
    typingInto = nullptr;
}

void VrDashboard::releasePointer()
{
    auto* peer = page != nullptr ? page->getPeer() : nullptr;
    if (peer == nullptr)
        return;
    const float scale = page->getDesktopScaleFactor();
    const auto now = juce::Time::currentTimeMillis();
    auto send = [&] (juce::ModifierKeys mods)
    {
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::pen, pointer * scale, mods,
                                juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, now);
    };
    const juce::Point<float> offPage { -100.0f, -100.0f };   // nothing stays lit as if pointed at
    if (std::exchange (down, false))
    {
        // A button is dragged off first, so letting go doesn't click it (a button clicks when let
        // go over itself). Anything else (a fader, a cable) is let go where it is: its value stays.
        if (std::exchange (pressOnButton, false))
        {
            pointer = offPage;
            send (juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier));
        }
        send ({});
    }
    pointer = offPage;
    send ({});
}

// A dialog (SAVE PRESET, DELETE MACRO, PLUGIN FAILED ITS TEST...) opened while the pointer is in use
// in the headset: it's for whoever is in it, so it's moved into the page rather than left on a
// monitor nobody can see (where, being modal, it would also block the page). Kept above the page's
// other parts (a rack made again comes in under it; menus it opens still go above it). A modal
// that isn't a dialog window (a file dialog: Windows' own window, with a stand-in for it here)
// can't be moved: the page says to finish it on the desktop until it's gone.
void VrDashboard::hostModal()
{
    if (page == nullptr)
        return;
    if (elsewhere != nullptr && ! elsewhere->isCurrentlyModal (false))
        elsewhere = nullptr;

    auto* modal = juce::Component::getCurrentlyModalComponent();
    const bool inUse = juce::Time::getMillisecondCounter() - lastPointerUse < pointerInUseMs;
    if (inUse && modal != nullptr && modal->isOnDesktop() && modal != page.get())
    {
        if (dynamic_cast<juce::TopLevelWindow*> (modal) == nullptr)
            elsewhere = modal;
        else if (hosted == nullptr)
        {
            hostedFlags = modal->getPeer() != nullptr ? modal->getPeer()->getStyleFlags() : 0;
            hostedOnTop = modal->isAlwaysOnTop();
            hosted = modal;
            page->addAndMakeVisible (modal);
            modal->setAlwaysOnTop (true);   // (and to the front)
            modal->setCentrePosition (page->getLocalBounds().getCentre());
        }
    }
    page->setNotice (elsewhere != nullptr);
}

// Back on the desktop, in the middle of the main monitor, if it's still open.
void VrDashboard::returnModal()
{
    if (hosted == nullptr)
        return;
    if (page != nullptr && hosted->getParentComponent() == page.get())
    {
        page->removeChildComponent (hosted.getComponent());
        hosted->setAlwaysOnTop (hostedOnTop);
        hosted->addToDesktop (hostedFlags);
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            hosted->setCentrePosition (display->userArea.getCentre());
        hosted->setVisible (true);
    }
    hosted = nullptr;
}

// The page drawn into the texture, which SteamVR keeps showing between updates.
void VrDashboard::render()
{
    if (page == nullptr || texture == nullptr)
        return;

    // Only what changed: what the page repainted (its own paint calls), or, if Windows hasn't
    // painted its off-screen window yet, the whole page while it has an update pending; all of it
    // when it opens and every two seconds anyway. Nothing changed: SteamVR keeps the last frame.
    const auto whole = juce::Rectangle<int> (pageWidth, pageHeight);
    auto area = page->takeDirty();
    if (auto* peer = page->getPeer())
        if (RECT pending; GetUpdateRect ((HWND) peer->getNativeHandle(), &pending, FALSE))
            area = whole;
    if (fullFrameDue || ++framesSinceFull >= 40)
        area = whole;
    area = area.getIntersection (whole);
    if (area.isEmpty())
        return;
    if (area == whole)
    {
        fullFrameDue = false;
        framesSinceFull = 0;
    }
    {
        juce::Graphics g (frame);
        g.reduceClipRegion (area);
        page->drawingFrame = true;
        page->paintEntireComponent (g, true);   // (an opaque page: every pixel's alpha is full)
        page->drawingFrame = false;
    }
    const juce::Image::BitmapData data (frame, juce::Image::BitmapData::readOnly);
    const D3D11_BOX box { (UINT) area.getX(), (UINT) area.getY(), 0, (UINT) area.getRight(), (UINT) area.getBottom(), 1 };
    context->UpdateSubresource (texture, 0, &box, data.getPixelPointer (area.getX(), area.getY()), (UINT) data.lineStride, 0);
    context->Flush();

    vr::Texture_t t { texture, vr::TextureType_DirectX, vr::ColorSpace_Auto };
    const auto shown = overlay->SetOverlayTexture (handle, &t);

    // The graphics card reset (a driver crash or update): join again with a new texture.
    if (device->GetDeviceRemovedReason() != S_OK)
    {
        if (log) log ("vr: the headset's picture was lost (graphics reset) - joining SteamVR again");
        detach();
        return;
    }

    // SteamVR refusing the picture: a moment's trouble is waited out; a second of it, SteamVR is
    // joined again (or let go without asking it anything, if its server has gone meanwhile).
    if (shown == vr::VROverlayError_None)
    {
        textureFailures = 0;
        return;
    }
    if (++textureFailures < textureFailuresToRejoin)
        return;
    if (steamVr().serverPid != attachedServer)
    {
        detach (true);
        return;
    }
    if (log) log ("vr: SteamVR kept refusing the headset's picture (" + juce::String (overlay->GetOverlayErrorNameFromEnum (shown))
                  + ") - joining SteamVR again");
    detach();
}

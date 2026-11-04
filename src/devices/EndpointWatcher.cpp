#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <mmreg.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <atomic>
#include <map>

#include "EndpointWatcher.h"
#include "PolicyConfig.h"

using Microsoft::WRL::ComPtr;

namespace
{
    class NotificationClient : public IMMNotificationClient
    {
    public:
        std::atomic<bool> dirty { false };

        ULONG STDMETHODCALLTYPE AddRef() override  { return ++refs; }
        ULONG STDMETHODCALLTYPE Release() override
        {
            auto r = --refs;
            if (r == 0)
                delete this;
            return r;
        }

        HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** out) override
        {
            if (riid == __uuidof (IUnknown) || riid == __uuidof (IMMNotificationClient))
            {
                *out = static_cast<IMMNotificationClient*> (this);
                AddRef();
                return S_OK;
            }

            *out = nullptr;
            return E_NOINTERFACE;
        }

        HRESULT STDMETHODCALLTYPE OnDeviceStateChanged (LPCWSTR, DWORD) override { dirty = true; return S_OK; }
        HRESULT STDMETHODCALLTYPE OnDeviceAdded (LPCWSTR) override               { dirty = true; return S_OK; }
        HRESULT STDMETHODCALLTYPE OnDeviceRemoved (LPCWSTR) override             { dirty = true; return S_OK; }

        // Routing binds to endpoint IDs; defaults only matter to strips/masters that explicitly
        // choose "Windows default", but they still need a rescan.
        HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged (EDataFlow, ERole, LPCWSTR) override   { dirty = true; return S_OK; }
        HRESULT STDMETHODCALLTYPE OnPropertyValueChanged (LPCWSTR, const PROPERTYKEY) override { return S_OK; }

    private:
        std::atomic<ULONG> refs { 1 };
    };

    juce::String defaultId (IMMDeviceEnumerator* enumerator, EDataFlow flow, ERole role)
    {
        ComPtr<IMMDevice> device;
        LPWSTR id = nullptr;

        if (FAILED (enumerator->GetDefaultAudioEndpoint (flow, role, &device)) || FAILED (device->GetId (&id)))
            return {};

        juce::String result (id);
        CoTaskMemFree (id);
        return result;
    }

    juce::Array<EndpointInfo> scanWasapi (IMMDeviceEnumerator* enumerator)
    {
        juce::Array<EndpointInfo> result;
        ComPtr<IMMDeviceCollection> devices;

        const juce::StringArray defaults { defaultId (enumerator, eRender, eConsole),
                                           defaultId (enumerator, eCapture, eConsole) };
        const juce::StringArray commsDefaults { defaultId (enumerator, eRender, eCommunications),
                                                defaultId (enumerator, eCapture, eCommunications) };

        if (FAILED (enumerator->EnumAudioEndpoints (eAll, DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED | DEVICE_STATE_UNPLUGGED, &devices)))
            return result;

        UINT count = 0;
        devices->GetCount (&count);

        for (UINT i = 0; i < count; ++i)
        {
            ComPtr<IMMDevice> device;
            if (FAILED (devices->Item (i, &device)))
                continue;

            EndpointInfo info;
            info.api = "WASAPI";

            LPWSTR id = nullptr;
            if (SUCCEEDED (device->GetId (&id)))
            {
                info.id = juce::String (id);
                CoTaskMemFree (id);
            }

            DWORD state = 0;
            device->GetState (&state);
            info.state = (int) state;

            ComPtr<IMMEndpoint> endpoint;
            if (SUCCEEDED (device.As (&endpoint)))
            {
                EDataFlow flow = eRender;
                endpoint->GetDataFlow (&flow);
                info.isOutput = (flow == eRender);
            }

            ComPtr<IPropertyStore> props;
            if (SUCCEEDED (device->OpenPropertyStore (STGM_READ, &props)))
            {
                PROPVARIANT v;

                PropVariantInit (&v);
                if (SUCCEEDED (props->GetValue (PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
                    info.name = juce::String (v.pwszVal);
                PropVariantClear (&v);

                if (SUCCEEDED (props->GetValue (PKEY_DeviceInterface_FriendlyName, &v)) && v.vt == VT_LPWSTR)
                    info.adapter = juce::String (v.pwszVal);
                PropVariantClear (&v);

                if (SUCCEEDED (props->GetValue (PKEY_AudioEndpoint_FormFactor, &v)) && v.vt == VT_UI4)
                    info.formFactor = (int) v.ulVal;
                PropVariantClear (&v);

                if (SUCCEEDED (props->GetValue (PKEY_AudioEngine_DeviceFormat, &v))
                     && v.vt == VT_BLOB && v.blob.cbSize >= sizeof (WAVEFORMATEX))
                {
                    auto* format = reinterpret_cast<const WAVEFORMATEX*> (v.blob.pBlobData);
                    info.channels = format->nChannels;
                    info.sampleRate = (int) format->nSamplesPerSec;
                }
                PropVariantClear (&v);
            }

            info.isDefault = defaults.contains (info.id);
            info.isDefaultComms = commsDefaults.contains (info.id);
            result.add (info);
        }

        return result;
    }

    // ASIO drivers register under HKLM\SOFTWARE\ASIO\<name> with a CLSID value. The registration
    // outlives the driver, so the file behind it is looked for: a driver that is registered but
    // whose file is gone is reported as not present, rather than as one that is there.
    // The file behind a registered ASIO driver: the registration outlives the driver (the app
    // it came with is uninstalled, the file is removed), and a driver whose file is gone cannot
    // be opened. Windows keeps the registration either way, so the file is the truth.
    bool asioDriverIsThere (const juce::String& clsid)
    {
        const auto keyText = juce::String ("CLSID\\") + clsid + "\\InprocServer32";

        for (auto flags : { (DWORD) 0, (DWORD) KEY_WOW64_32KEY })   // (a 32-bit driver registers in its own view)
        {
            HKEY inproc = nullptr;
            if (RegOpenKeyExW (HKEY_CLASSES_ROOT, keyText.toWideCharPointer(), 0, KEY_READ | flags, &inproc) != ERROR_SUCCESS)
                continue;

            wchar_t path[MAX_PATH] = {};
            DWORD size = sizeof (path);
            const auto found = RegGetValueW (inproc, nullptr, nullptr, RRF_RT_REG_SZ, nullptr, path, &size) == ERROR_SUCCESS;
            RegCloseKey (inproc);

            if (found && juce::File (juce::String (path)).existsAsFile())
                return true;
        }
        return false;
    }

    juce::Array<EndpointInfo> scanAsio()
    {
        juce::Array<EndpointInfo> result;
        HKEY root = nullptr;

        if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ, &root) != ERROR_SUCCESS)
            return result;

        for (DWORD i = 0;; ++i)
        {
            wchar_t name[256];
            DWORD nameLen = 256;

            if (RegEnumKeyExW (root, i, name, &nameLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;

            wchar_t clsid[64] = {};
            DWORD clsidBytes = sizeof (clsid);

            if (RegGetValueW (root, name, L"CLSID", RRF_RT_REG_SZ, nullptr, clsid, &clsidBytes) != ERROR_SUCCESS)
                continue;

            EndpointInfo info;
            info.api = "ASIO";
            info.id = "asio:" + juce::String (clsid);
            info.name = juce::String (name);
            info.state = asioDriverIsThere (juce::String (clsid)) ? 1 : 4;   // (4: not present)
            result.add (info);
        }

        RegCloseKey (root);
        return result;
    }
}

juce::StringArray classifyEndpoint (const EndpointInfo& info)
{
    juce::StringArray flags;

    if (info.formFactor == 9)   // DigitalAudioDisplayDevice: comes and goes with the display
        flags.add ("hdmi");

    for (auto* vrName : { "Valve Index", "Index HMD", "Valve VR", "Oculus", "Meta Quest", "Rift", "VIVE" })
        if (info.name.containsIgnoreCase (vrName))
        {
            flags.add ("vr");
            break;
        }

    if (flags.contains ("hdmi") || flags.contains ("vr"))
        flags.add ("transient");
    else
        flags.add ("master");

    return flags;
}

bool asioMatchesAdapter (const juce::String& asioName, const juce::String& adapter)
{
    auto words = [] (const juce::String& s)
    {
        juce::StringArray w;
        w.addTokens (s.toLowerCase().replaceCharacters ("()-_", "    "), " ", "");
        w.removeEmptyStrings();
        for (auto* generic : { "asio", "audio", "driver", "device", "2.0" })
            w.removeString (generic);
        return w;
    };

    // Every distinctive word of the ASIO name must appear in the adapter name, so
    // "Focusrite USB" matches "Focusrite USB Audio" but "Focusrite Thunderbolt" doesn't.
    const auto asioWords = words (asioName);
    const auto adapterWords = words (adapter);

    if (asioWords.isEmpty())
        return false;

    for (auto& w : asioWords)
        if (! adapterWords.contains (w))
            return false;

    return true;
}

namespace
{
    std::map<juce::String, juce::String>& aliases()   // device name -> the user's name for it (message thread)
    {
        static std::map<juce::String, juce::String> names;
        return names;
    }
}

bool setEndpointEnabled (const juce::String& endpointId, bool enabled)
{
    ComPtr<IPolicyConfig> policy;
    if (FAILED (CoCreateInstance (__uuidof (CPolicyConfigClient), nullptr, CLSCTX_ALL, __uuidof (IPolicyConfig), (void**) policy.GetAddressOf())))
        return false;
    return SUCCEEDED (policy->SetEndpointVisibility (endpointId.toWideCharPointer(), enabled ? TRUE : FALSE));
}

void setDeviceAlias (const juce::String& deviceName, const juce::String& alias)
{
    if (alias.trim().isEmpty())
        aliases().erase (deviceName);
    else
        aliases()[deviceName] = alias.trim();
}

juce::String deviceAlias (const juce::String& deviceName)
{
    auto it = aliases().find (deviceName);
    return it != aliases().end() ? it->second : juce::String();
}

juce::StringPairArray deviceAliases()
{
    juce::StringPairArray all;
    for (auto& [device, alias] : aliases())
        all.set (device, alias);
    return all;
}

juce::String endpointDisplayName (const EndpointInfo& e)
{
    if (auto alias = deviceAlias (e.name); alias.isNotEmpty())
        return alias;

    // "Speakers (2- Focusrite USB Audio)" -> "Speakers"; the adapter is shown separately.
    auto name = e.name.upToFirstOccurrenceOf (" (", false, false).trim();

    if (name.startsWithIgnoreCase ("Voicemeeter"))
    {
        name = name.replace ("Voicemeeter Out ", "Voicemeeter ", true)
                   .replace ("Voicemeeter In ", "Voicemeeter ", true);

        if (name.endsWithIgnoreCase (" Input"))
            name = name.dropLastCharacters (6);

        if (name.equalsIgnoreCase ("Voicemeeter"))
            name = "Voicemeeter Main";
    }

    return name;
}

juce::String virtualMicPartner (const EndpointInfo& e, const juce::Array<EndpointInfo>& all)
{
    if (e.api != "WASAPI" || ! e.isOutput)
        return {};

    const bool isCable = e.adapter.containsIgnoreCase ("VB-Audio Virtual Cable")
                      || e.adapter.containsIgnoreCase ("VB-Audio Cable")
                      || e.adapter.containsIgnoreCase ("VB-Audio Hi-Fi Cable");

    if (! isCable)
        return {};

    // The cable's recording side shares its adapter; VB-Audio has renamed the playback side
    // over versions ("CABLE Input", "Speakers"), so don't guess from that name.
    for (auto& other : all)
        if (other.api == "WASAPI" && ! other.isOutput && other.adapter == e.adapter)
            return endpointDisplayName (other);

    return {};
}

juce::String endpointStateName (int state)
{
    switch (state)
    {
        case 1:  return "active";
        case 2:  return "disabled";
        case 4:  return "not present";
        case 8:  return "unplugged";
        default: return "gone";
    }
}

struct EndpointWatcher::Impl
{
    Impl()
    {
        comInitialised = SUCCEEDED (CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED));

        if (SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS (&enumerator))))
        {
            client = new NotificationClient();
            enumerator->RegisterEndpointNotificationCallback (client);
        }
    }

    ~Impl()
    {
        if (client != nullptr)
        {
            enumerator->UnregisterEndpointNotificationCallback (client);
            client->Release();
        }

        enumerator.Reset();

        if (comInitialised)
            CoUninitialize();
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    NotificationClient* client = nullptr;
    bool comInitialised = false;
};

EndpointWatcher::EndpointWatcher (Listener& l)
    : impl (std::make_unique<Impl>()), listener (l)
{
    startTimer (250);
}

EndpointWatcher::~EndpointWatcher()
{
    stopTimer();
}

void EndpointWatcher::timerCallback()
{
    if (impl->client != nullptr && impl->client->dirty.exchange (false))
        rescan();
}

void EndpointWatcher::rescan()
{
    juce::Array<EndpointInfo> now;

    if (impl->enumerator != nullptr)
        now.addArray (scanWasapi (impl->enumerator.Get()));

    now.addArray (scanAsio());

    juce::StringArray events;
    std::map<juce::String, EndpointInfo> current;

    for (auto& info : now)
    {
        current[info.id] = info;
        auto previous = last.find (info.id);

        if (firstScan)
            continue;

        if (previous == last.end())
            events.add ("added: " + info.name);
        else if (previous->second.state != info.state)
            events.add (info.name + ": " + endpointStateName (previous->second.state) + " -> " + endpointStateName (info.state));

        if (info.isDefault && (previous == last.end() || ! previous->second.isDefault))
            events.add (juce::String ("Windows default ") + (info.isOutput ? "playback" : "recording") + ": " + info.name);
    }

    for (auto& [id, info] : last)
        if (current.find (id) == current.end())
            events.add ("removed: " + info.name);

    last = std::move (current);
    firstScan = false;

    listener.endpointsChanged (now, events);
}

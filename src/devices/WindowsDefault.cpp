#include "WindowsDefault.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include "PolicyConfig.h"

namespace
{
    template <typename T> struct Com   // releases on the way out
    {
        T* p = nullptr;
        ~Com() { if (p != nullptr) p->Release(); }
        T** operator&() { return &p; }
        T* operator->() const { return p; }
        explicit operator bool() const { return p != nullptr; }
    };

    struct ComThread
    {
        const bool mine;
        ComThread() : mine (SUCCEEDED (CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED))) {}
        ~ComThread() { if (mine) CoUninitialize(); }
    };

    struct Device { juce::String id, name; };

    juce::String nameOf (IMMDevice* device)
    {
        Com<IPropertyStore> props;
        if (FAILED (device->OpenPropertyStore (STGM_READ, &props)))
            return {};
        PROPVARIANT v;
        PropVariantInit (&v);
        juce::String name;
        if (SUCCEEDED (props->GetValue (PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
            name = juce::String (v.pwszVal);
        PropVariantClear (&v);
        return name;
    }

    juce::String idOf (IMMDevice* device)
    {
        LPWSTR id = nullptr;
        if (FAILED (device->GetId (&id)))
            return {};
        juce::String s (id);
        CoTaskMemFree (id);
        return s;
    }

    std::vector<Device> list (bool recording)
    {
        std::vector<Device> out;
        Com<IMMDeviceEnumerator> enumerator;
        if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &enumerator)))
            return out;
        Com<IMMDeviceCollection> collection;
        if (FAILED (enumerator->EnumAudioEndpoints (recording ? eCapture : eRender, DEVICE_STATE_ACTIVE, &collection)))
            return out;
        UINT count = 0;
        collection->GetCount (&count);
        for (UINT i = 0; i < count; ++i)
        {
            Com<IMMDevice> device;
            if (SUCCEEDED (collection->Item (i, &device)))
                out.push_back ({ idOf (device.p), nameOf (device.p) });
        }
        return out;
    }
}

juce::StringArray WindowsDefault::devices (bool recording)
{
    const ComThread com;
    juce::StringArray names;
    for (auto& d : list (recording))
        names.add (d.name);
    return names;
}

juce::String WindowsDefault::current (bool recording)
{
    const ComThread com;
    Com<IMMDeviceEnumerator> enumerator;
    Com<IMMDevice> device;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &enumerator))
        || FAILED (enumerator->GetDefaultAudioEndpoint (recording ? eCapture : eRender, eConsole, &device)))
        return {};
    return nameOf (device.p);
}

bool WindowsDefault::set (const juce::String& name, bool recording, juce::String& error)
{
    const ComThread com;
    const auto all = list (recording);
    const Device* chosen = nullptr;
    for (auto& d : all)   // its whole name first, else a part of it
        if (d.name.equalsIgnoreCase (name))
            chosen = &d;
    for (auto& d : all)
        if (chosen == nullptr && name.isNotEmpty() && d.name.containsIgnoreCase (name))
            chosen = &d;
    if (chosen == nullptr)
    {
        error = (recording ? "no recording device called " : "no playback device called ") + name;
        return false;
    }

    Com<IPolicyConfig> policy;
    if (FAILED (CoCreateInstance (__uuidof (CPolicyConfigClient), nullptr, CLSCTX_ALL, __uuidof (IPolicyConfig), (void**) &policy)))
    {
        error = "Windows wouldn't let the default device be changed";
        return false;
    }
    const auto id = chosen->id.toWideCharPointer();
    bool ok = true;
    for (auto role : { eConsole, eMultimedia, eCommunications })
        ok = SUCCEEDED (policy->SetDefaultEndpoint (id, role)) && ok;
    if (! ok)
        error = "Windows refused " + chosen->name + " as the default";
    return ok;
}

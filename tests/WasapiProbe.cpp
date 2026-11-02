// Diagnostic, not a test: opens a playback device (by part of its name) in WASAPI shared mode as
// Patchbay does, and prints what Windows answers at each step: its mix format, then
// Initialize at that format and at stereo, with the HRESULT. Usage: WasapiProbe "CABLE In 16ch"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>

using Microsoft::WRL::ComPtr;

static void tryInit (IMMDevice* device, const WAVEFORMATEX* format, const char* what)
{
    ComPtr<IAudioClient> client;
    HRESULT hr = device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) client.GetAddressOf());
    if (FAILED (hr)) { std::printf ("  %-22s Activate failed 0x%08lx\n", what, (unsigned long) hr); return; }
    WAVEFORMATEX* closest = nullptr;
    const HRESULT supported = client->IsFormatSupported (AUDCLNT_SHAREMODE_SHARED, format, &closest);
    if (closest != nullptr) CoTaskMemFree (closest);
    hr = client->Initialize (AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, format, nullptr);
    std::printf ("  %-22s %u ch %lu Hz %u-bit: IsFormatSupported 0x%08lx, Initialize 0x%08lx\n", what,
                 format->nChannels, format->nSamplesPerSec, format->wBitsPerSample, (unsigned long) supported, (unsigned long) hr);
}

// Both cable inputs streaming at once: start CABLE Input, then Initialize and Start CABLE In
// 16ch, printing each HRESULT. Usage: WasapiProbe --together
static int together()
{
    ComPtr<IMMDeviceEnumerator> devices;
    ComPtr<IMMDeviceCollection> all;
    CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS (&devices));
    devices->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &all);
    ComPtr<IMMDevice> input, sixteen;
    UINT count = 0;
    all->GetCount (&count);
    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> d;
        ComPtr<IPropertyStore> props;
        all->Item (i, &d);
        PROPVARIANT name;
        PropVariantInit (&name);
        d->OpenPropertyStore (STGM_READ, &props);
        props->GetValue (PKEY_Device_FriendlyName, &name);
        const std::wstring friendly = name.pwszVal != nullptr ? name.pwszVal : L"";
        PropVariantClear (&name);
        if (friendly.find (L"CABLE Input") == 0) input = d;
        if (friendly.find (L"CABLE In 16ch") == 0) sixteen = d;
    }
    auto stream = [] (IMMDevice* d, const char* what) -> ComPtr<IAudioClient>
    {
        ComPtr<IAudioClient> client;
        d->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) client.GetAddressOf());
        WAVEFORMATEX* mix = nullptr;
        client->GetMixFormat (&mix);
        HANDLE event = CreateEventW (nullptr, FALSE, FALSE, nullptr);
        const HRESULT init = client->Initialize (AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, mix, nullptr);
        const HRESULT handle = SUCCEEDED (init) ? client->SetEventHandle (event) : E_FAIL;
        const HRESULT start = SUCCEEDED (handle) ? client->Start() : E_FAIL;
        std::printf ("%-14s Initialize 0x%08lx  SetEventHandle 0x%08lx  Start 0x%08lx\n", what, (unsigned long) init, (unsigned long) handle, (unsigned long) start);
        CoTaskMemFree (mix);
        return client;
    };
    if (input == nullptr || sixteen == nullptr) { std::printf ("cables not found\n"); return 1; }
    auto a = stream (input.Get(), "CABLE Input");
    auto b = stream (sixteen.Get(), "CABLE In 16ch");
    Sleep (200);
    if (a) a->Stop();
    if (b) b->Stop();
    return 0;
}

int wmain (int argc, wchar_t** argv)
{
    if (argc > 1 && std::wstring (argv[1]) == L"--together")
    {
        CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        const int r = together();
        CoUninitialize();
        return r;
    }
    const std::wstring wanted = argc > 1 ? argv[1] : L"CABLE In 16ch";
    CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    ComPtr<IMMDeviceEnumerator> devices;
    ComPtr<IMMDeviceCollection> all;
    CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS (&devices));
    devices->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &all);
    UINT count = 0;
    all->GetCount (&count);
    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> d;
        ComPtr<IPropertyStore> props;
        all->Item (i, &d);
        PROPVARIANT name;
        PropVariantInit (&name);
        d->OpenPropertyStore (STGM_READ, &props);
        props->GetValue (PKEY_Device_FriendlyName, &name);
        const std::wstring friendly = name.pwszVal != nullptr ? name.pwszVal : L"";
        PropVariantClear (&name);
        if (friendly.find (wanted) == std::wstring::npos)
            continue;

        std::wprintf (L"%ls\n", friendly.c_str());
        ComPtr<IAudioClient> client;
        d->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) client.GetAddressOf());
        WAVEFORMATEX* mix = nullptr;
        const HRESULT hr = client->GetMixFormat (&mix);
        if (FAILED (hr)) { std::printf ("  GetMixFormat failed 0x%08lx\n", (unsigned long) hr); continue; }
        std::printf ("  mix format: %u ch %lu Hz %u-bit, tag 0x%04x\n", mix->nChannels, mix->nSamplesPerSec, mix->wBitsPerSample, mix->wFormatTag);
        tryInit (d.Get(), mix, "at the mix format");

        WAVEFORMATEXTENSIBLE stereo {};
        stereo.Format = { WAVE_FORMAT_EXTENSIBLE, 2, mix->nSamplesPerSec, mix->nSamplesPerSec * 8, 8, 32, 22 };
        stereo.Samples.wValidBitsPerSample = 32;
        stereo.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        stereo.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        tryInit (d.Get(), &stereo.Format, "stereo float");
        CoTaskMemFree (mix);
    }
    CoUninitialize();
    return 0;
}

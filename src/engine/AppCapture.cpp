#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <audioclientactivationparams.h>
#include <avrt.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <atomic>
#include <map>
#include <thread>

#include "AppCapture.h"
#include "WindowDiscovery.h"

#pragma comment (lib, "mmdevapi.lib")
#pragma comment (lib, "avrt.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr int captureRate = 48000;
    constexpr int captureChannels = 2;

    // ActivateAudioInterfaceAsync reports back through this; we just wait for it.
    class ActivationHandler : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                                                  Microsoft::WRL::FtmBase,
                                                                  IActivateAudioInterfaceCompletionHandler>
    {
    public:
        ActivationHandler() { done = CreateEventW (nullptr, TRUE, FALSE, nullptr); }
        ~ActivationHandler() override { CloseHandle (done); }

        STDMETHOD (ActivateCompleted) (IActivateAudioInterfaceAsyncOperation* operation) override
        {
            ComPtr<IUnknown> unknown;
            HRESULT activateResult = E_FAIL;

            if (SUCCEEDED (operation->GetActivateResult (&activateResult, &unknown)) && SUCCEEDED (activateResult))
                unknown.As (&client);

            result = activateResult;
            SetEvent (done);
            return S_OK;
        }

        HANDLE done = nullptr;
        HRESULT result = E_FAIL;
        ComPtr<IAudioClient> client;
    };
}

struct AppCapture::Impl
{
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    HANDLE event = nullptr;
    std::thread thread;
    std::atomic<bool> running { false };

    ~Impl()
    {
        running = false;

        if (thread.joinable())
            thread.join();

        if (client != nullptr)
            client->Stop();

        // The client let go before its event: Windows can still set the event it was given until
        // then, and a handle closed first can be handed to something else (a thread pool's wait),
        // which a late SetEvent would then fire. That crashed Patchbay as a captured app closed.
        capture.Reset();
        client.Reset();

        if (event != nullptr)
            CloseHandle (event);
    }

    void run (DriftFifo& fifo)
    {
        DWORD taskIndex = 0;
        auto mmcss = AvSetMmThreadCharacteristicsW (L"Pro Audio", &taskIndex);

        std::vector<float> left (captureRate), right (captureRate);

        while (running)
        {
            if (WaitForSingleObject (event, 100) != WAIT_OBJECT_0)
                continue;

            UINT32 packet = 0;

            while (running && SUCCEEDED (capture->GetNextPacketSize (&packet)) && packet > 0)
            {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;

                if (FAILED (capture->GetBuffer (&data, &frames, &flags, nullptr, nullptr)))
                    break;

                const auto n = (int) juce::jmin ((size_t) frames, left.size());
                auto* samples = reinterpret_cast<const float*> (data);

                for (int i = 0; i < n; ++i)
                {
                    const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                    left[(size_t) i]  = silent ? 0.0f : samples[i * 2];
                    right[(size_t) i] = silent ? 0.0f : samples[i * 2 + 1];
                }

                const float* channels[] = { left.data(), right.data() };
                fifo.push (channels, captureChannels, n);
                capture->ReleaseBuffer (frames);
            }
        }

        if (mmcss != nullptr)
            AvRevertMmThreadCharacteristics (mmcss);
    }
};

std::unique_ptr<AppCapture> AppCapture::open (uint32_t processId, double interfaceRate, int interfaceBlock, juce::String& error)
{
    AUDIOCLIENT_ACTIVATION_PARAMS params {};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = processId;
    params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT activation {};
    activation.vt = VT_BLOB;
    activation.blob.cbSize = sizeof (params);
    activation.blob.pBlobData = reinterpret_cast<BYTE*> (&params);

    auto handler = Microsoft::WRL::Make<ActivationHandler>();
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;

    if (FAILED (ActivateAudioInterfaceAsync (VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof (IAudioClient),
                                             &activation, handler.Get(), &operation))
        || WaitForSingleObject (handler->done, 3000) != WAIT_OBJECT_0
        || handler->client == nullptr)
    {
        error = "Windows refused app capture (needs Windows 10 2004 or later)";
        return nullptr;
    }

    // Process loopback has no mix format of its own; ask for 48 kHz float stereo and let
    // Windows convert.
    WAVEFORMATEX format {};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = captureChannels;
    format.nSamplesPerSec = captureRate;
    format.wBitsPerSample = 32;
    format.nBlockAlign = (WORD) (format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    auto impl = std::make_unique<Impl>();
    impl->client = handler->client;

    const auto flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;

    if (FAILED (impl->client->Initialize (AUDCLNT_SHAREMODE_SHARED, flags, 200000 /* 20 ms */, 0, &format, nullptr))
        || FAILED (impl->client->GetService (IID_PPV_ARGS (&impl->capture))))
    {
        error = "couldn't start capturing that app";
        return nullptr;
    }

    impl->event = CreateEventW (nullptr, FALSE, FALSE, nullptr);
    impl->client->SetEventHandle (impl->event);

    std::unique_ptr<AppCapture> capture (new AppCapture());
    capture->pid = processId;
    capture->fifo.prepare (captureChannels, captureRate, interfaceRate, captureRate / 100, interfaceBlock);   // ~10 ms packets

    if (FAILED (impl->client->Start()))
    {
        error = "couldn't start capturing that app";
        return nullptr;
    }

    impl->running = true;
    auto* implPtr = impl.get();
    auto* fifo = &capture->fifo;
    impl->thread = std::thread ([implPtr, fifo]
    {
        // Its own COM apartment for the capture calls (rather than relying on the process's).
        const bool com = SUCCEEDED (CoInitializeEx (nullptr, COINIT_MULTITHREADED));
        implPtr->run (*fifo);
        if (com)
            CoUninitialize();
    });

    capture->impl = std::move (impl);
    return capture;
}

AppCapture::~AppCapture() = default;

juce::Array<AudioApp> listAudioApps()
{
    juce::Array<AudioApp> apps;
    std::map<DWORD, bool> seen;

    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDeviceCollection> devices;

    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS (&enumerator)))
        || FAILED (enumerator->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &devices)))
        return apps;

    UINT count = 0;
    devices->GetCount (&count);

    for (UINT d = 0; d < count; ++d)
    {
        ComPtr<IMMDevice> device;
        ComPtr<IAudioSessionManager2> manager;
        ComPtr<IAudioSessionEnumerator> sessions;

        if (FAILED (devices->Item (d, &device))
            || FAILED (device->Activate (__uuidof (IAudioSessionManager2), CLSCTX_ALL, nullptr, &manager))
            || FAILED (manager->GetSessionEnumerator (&sessions)))
            continue;

        int sessionCount = 0;
        sessions->GetCount (&sessionCount);

        for (int s = 0; s < sessionCount; ++s)
        {
            ComPtr<IAudioSessionControl> control;
            ComPtr<IAudioSessionControl2> control2;
            DWORD pid = 0;

            if (FAILED (sessions->GetSession (s, &control)) || FAILED (control.As (&control2))
                || control2->IsSystemSoundsSession() == S_OK || FAILED (control2->GetProcessId (&pid)) || pid == 0)
                continue;

            if (seen[pid])
                continue;

            seen[pid] = true;
            const auto exe = juce::File (WindowDiscovery::exePathOf (pid)).getFileName();

            if (exe.isNotEmpty() && ! exe.equalsIgnoreCase (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFileName()))   // (not itself)
                apps.add ({ (uint32_t) pid, exe });
        }
    }

    return apps;
}

uint32_t findAppProcess (const juce::String& exe)
{
    // Prefer a copy that's actually playing audio (several copies of an exe may run, and
    // browsers play from a helper process). Lowest PID keeps the choice stable between calls.
    // Patchbay's isolated browser instances are captured on their own, never as the browser.
    uint32_t playing = 0;
    for (auto& a : listAudioApps())
        if (a.exe.equalsIgnoreCase (exe) && (playing == 0 || a.processId < playing) && ! WindowDiscovery::isIsolatedProcess (a.processId))
            playing = a.processId;

    if (playing != 0)
        return playing;

    auto snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    std::map<DWORD, DWORD> parentOf;
    juce::Array<DWORD> matches;

    PROCESSENTRY32W entry { sizeof (entry) };
    for (BOOL ok = Process32FirstW (snapshot, &entry); ok; ok = Process32NextW (snapshot, &entry))
    {
        parentOf[entry.th32ProcessID] = entry.th32ParentProcessID;
        if (juce::String (entry.szExeFile).equalsIgnoreCase (exe) && ! WindowDiscovery::isIsolatedProcess (entry.th32ProcessID))
            matches.add (entry.th32ProcessID);
    }

    CloseHandle (snapshot);

    // Browsers and Spotify run many copies; the root is the one whose parent isn't the same exe.
    for (auto pid : matches)
        if (! matches.contains (parentOf[pid]))
            return (uint32_t) pid;

    return matches.isEmpty() ? 0 : (uint32_t) matches.getFirst();
}

uint32_t thisProcessId() { return (uint32_t) GetCurrentProcessId(); }

bool processIsRunning (uint32_t processId)
{
    auto process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, 0, (DWORD) processId);
    if (process == nullptr)
        return false;

    DWORD code = 0;
    const bool running = GetExitCodeProcess (process, &code) && code == STILL_ACTIVE;
    CloseHandle (process);
    return running;
}

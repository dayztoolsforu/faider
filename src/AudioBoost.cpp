#include "AudioBoost.h"
#include <juce_events/juce_events.h>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <objbase.h>
#pragma comment (lib, "advapi32.lib")
#pragma comment (lib, "shell32.lib")

namespace
{
    // audiodg.exe's process id, or 0.
    DWORD findAudiodg()
    {
        HANDLE snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return 0;
        DWORD pid = 0;
        PROCESSENTRY32W entry {};
        entry.dwSize = sizeof (entry);
        for (BOOL more = Process32FirstW (snapshot, &entry); more && pid == 0; more = Process32NextW (snapshot, &entry))
            if (_wcsicmp (entry.szExeFile, L"audiodg.exe") == 0)
                pid = entry.th32ProcessID;
        CloseHandle (snapshot);
        return pid;
    }

    AudioBoost::Priority fromClass (DWORD c)
    {
        using P = AudioBoost::Priority;
        switch (c)
        {
            case IDLE_PRIORITY_CLASS:         return P::idle;
            case BELOW_NORMAL_PRIORITY_CLASS: return P::belowNormal;
            case NORMAL_PRIORITY_CLASS:       return P::normal;
            case ABOVE_NORMAL_PRIORITY_CLASS: return P::aboveNormal;
            case HIGH_PRIORITY_CLASS:         return P::high;
            case REALTIME_PRIORITY_CLASS:     return P::realtime;
            default:                          return P::unknown;
        }
    }

    // The cores this copy of Windows lets a process run on (one processor group: 64 at most).
    DWORD_PTR systemMask()
    {
        DWORD_PTR process = 0, system = 0;
        if (! GetProcessAffinityMask (GetCurrentProcess(), &process, &system))
            return 0;
        return system;
    }

    // Administrators have the right to open other accounts' processes (SeDebugPrivilege), but it
    // starts switched off: on, for this process only, when the plain open is refused.
    bool enableDebugPrivilege()
    {
        HANDLE token = nullptr;
        if (! OpenProcessToken (GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
            return false;
        TOKEN_PRIVILEGES privileges {};
        privileges.PrivilegeCount = 1;
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        const bool ok = LookupPrivilegeValueW (nullptr, L"SeDebugPrivilege", &privileges.Privileges[0].Luid)
                        && AdjustTokenPrivileges (token, FALSE, &privileges, sizeof (privileges), nullptr, nullptr)
                        && GetLastError() == ERROR_SUCCESS;
        CloseHandle (token);
        return ok;
    }
}

AudioBoost::State AudioBoost::read()
{
    State s;
    s.pid = (juce::uint32) findAudiodg();
    if (s.pid == 0)
        return s;
    HANDLE process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) s.pid);
    if (process == nullptr)
        return s;   // (its priority stays unknown)
    s.priority = fromClass (GetPriorityClass (process));
    DWORD_PTR mask = 0, system = 0;
    if (GetProcessAffinityMask (process, &mask, &system) && mask != system && mask != 0 && (mask & (mask - 1)) == 0)
        for (int core = 0; core < 64; ++core)
            if (mask == ((DWORD_PTR) 1 << core))
                s.pinnedCore = core;
    CloseHandle (process);
    return s;
}

int AudioBoost::coreCount()
{
    int n = 0;
    for (auto mask = systemMask(); mask != 0; mask &= mask - 1)
        ++n;
    return n;
}

int AudioBoost::runHelper (const juce::StringArray& args)
{
    const auto a = parseArgs (args, coreCount());
    if (! a.valid || (a.pinCore >= 0 && (systemMask() & ((DWORD_PTR) 1 << a.pinCore)) == 0))
        return badArgs;

    const DWORD pid = findAudiodg();
    if (pid == 0)
        return notFound;
    constexpr DWORD access = PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION;
    HANDLE process = OpenProcess (access, FALSE, pid);
    if (process == nullptr && GetLastError() == ERROR_ACCESS_DENIED && enableDebugPrivilege())
        process = OpenProcess (access, FALSE, pid);
    if (process == nullptr)
        return GetLastError() == ERROR_INVALID_PARAMETER ? notFound : accessDenied;   // (gone meanwhile: invalid)

    int code = ok;
    if (! SetPriorityClass (process, HIGH_PRIORITY_CLASS))   // High: never realtime
        code = notSet;
    else if (a.pinCore >= 0 && ! SetProcessAffinityMask (process, (DWORD_PTR) 1 << a.pinCore))
        code = notPinned;
    CloseHandle (process);
    return code;
}

void AudioBoost::boost (bool pinToLastCore, void* owner, std::function<void (int)> done)
{
    int lastCore = -1;
    const auto cores = systemMask();
    for (int core = 0; core < 64; ++core)
        if ((cores & ((DWORD_PTR) 1 << core)) != 0)
            lastCore = core;
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto parameters = commandLine (pinToLastCore ? lastCore : -1);

    std::thread ([exe, parameters, owner, done = std::move (done)]
    {
        const HRESULT init = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const auto file = exe.getFullPathName();
        const auto folder = exe.getParentDirectory().getFullPathName();
        SHELLEXECUTEINFOW info {};
        info.cbSize = sizeof (info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        info.hwnd = static_cast<HWND> (owner);
        info.lpVerb = L"runas";   // Windows asks first (UAC)
        info.lpFile = file.toWideCharPointer();
        info.lpParameters = parameters.toWideCharPointer();
        info.lpDirectory = folder.toWideCharPointer();
        info.nShow = SW_HIDE;

        int code = notStarted;
        if (! ShellExecuteExW (&info))
            code = GetLastError() == ERROR_CANCELLED ? declined : notStarted;
        else if (info.hProcess != nullptr)
        {
            if (WaitForSingleObject (info.hProcess, 15000) != WAIT_OBJECT_0)
                code = timedOut;
            else if (DWORD exit = 0; GetExitCodeProcess (info.hProcess, &exit))
                code = (int) exit;
            CloseHandle (info.hProcess);
        }
        if (SUCCEEDED (init))
            CoUninitialize();
        if (juce::MessageManager::getInstanceWithoutCreating() != nullptr)
            juce::MessageManager::callAsync ([done, code] { if (done) done (code); });
    }).detach();
}

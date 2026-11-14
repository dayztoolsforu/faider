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
#include <shlobj.h>
#include <knownfolders.h>
#pragma comment (lib, "shell32.lib")
#pragma comment (lib, "advapi32.lib")

namespace
{
    // Switches the debug privilege on or off in this process's own token. An administrator holds it
    // but starts with it off, and audiodg (LOCAL SERVICE) only lets an administrator change it with it
    // on. Only the elevated helper uses it, only to open audiodg, and turns it off straight after.
    bool setDebugPrivilege (bool on)
    {
        HANDLE token = nullptr;
        if (! OpenProcessToken (GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
            return false;
        TOKEN_PRIVILEGES tp {};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = on ? SE_PRIVILEGE_ENABLED : 0;
        const bool ok = LookupPrivilegeValueW (nullptr, L"SeDebugPrivilege", &tp.Privileges[0].Luid)
                        && AdjustTokenPrivileges (token, FALSE, &tp, sizeof (tp), nullptr, nullptr)
                        && GetLastError() == ERROR_SUCCESS;   // (ERROR_NOT_ALL_ASSIGNED: not held)
        CloseHandle (token);
        return ok;
    }

    juce::String systemFolder()
    {
        wchar_t dir[MAX_PATH] {};
        const UINT n = GetSystemDirectoryW (dir, MAX_PATH);
        return n > 0 && n < MAX_PATH ? juce::String (dir) : juce::String();
    }

    // Whether an open process runs Windows' own audiodg.exe (the one in the system folder).
    bool imageIsAudiodg (HANDLE process)
    {
        wchar_t path[1024] {};
        DWORD size = 1024;
        return QueryFullProcessImageNameW (process, 0, path, &size) && AudioBoost::isWindowsAudiodg (juce::String (path), systemFolder());
    }

    // A process named audiodg.exe that isn't Windows' own is passed over: one in a user's session
    // (Windows' runs in the services' session, 0, where a user can start nothing), or one whose
    // file isn't the system folder's. One that can't be opened here to see its file is taken on its
    // session; the helper looks at its file once it has it open.
    bool looksLikeAudiodg (DWORD pid)
    {
        DWORD session = 0;
        if (ProcessIdToSessionId (pid, &session) && session != 0)
            return false;
        HANDLE process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process == nullptr)
            return true;
        const bool real = imageIsAudiodg (process);
        CloseHandle (process);
        return real;
    }

    // Windows' audiodg.exe's process id, or 0.
    DWORD findAudiodg()
    {
        HANDLE snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return 0;
        DWORD pid = 0;
        PROCESSENTRY32W entry {};
        entry.dwSize = sizeof (entry);
        for (BOOL more = Process32FirstW (snapshot, &entry); more && pid == 0; more = Process32NextW (snapshot, &entry))
            if (_wcsicmp (entry.szExeFile, L"audiodg.exe") == 0 && looksLikeAudiodg (entry.th32ProcessID))
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

    // Starts Faider elevated with `parameters` (Windows asks first) and waits for it on a thread
    // of its own; `done` gets its exit code on the message thread.
    void runElevated (const juce::String& parameters, void* owner, std::function<void (int)> done)
    {
        // Only from a folder that takes an administrator to write to (canElevateFrom): else Windows
        // isn't asked, and the answer says why.
        if (! AudioBoost::canElevate())
        {
            juce::MessageManager::callAsync ([done = std::move (done)] { if (done) done (AudioBoost::needsAllUsers); });
            return;
        }
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        std::thread ([exe, parameters, owner, done = std::move (done)]
        {
            using namespace AudioBoost;
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
}

bool AudioBoost::canElevate()
{
    // (the Program Files folders as Windows keeps them, not as the environment names them: a
    // program can set %ProgramFiles% for what it starts)
    juce::StringArray programFiles;
    for (auto* id : { &FOLDERID_ProgramFiles, &FOLDERID_ProgramFilesX86, &FOLDERID_ProgramFilesX64 })
    {
        PWSTR path = nullptr;
        if (SUCCEEDED (SHGetKnownFolderPath (*id, 0, nullptr, &path)) && path != nullptr)
            programFiles.add (juce::String (path));
        CoTaskMemFree (path);
    }

    const auto folder = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getFullPathName();
    wchar_t whole[2048] {};   // (its long name, if it was started by a short one)
    const DWORD n = GetLongPathNameW (folder.toWideCharPointer(), whole, 2048);
    return canElevateFrom (n > 0 && n < 2048 ? juce::String (whole) : folder, programFiles);
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
    // audiodg's permissions let an administrator change it only with the debug privilege on, and an
    // elevated process starts with it off: refused, it's switched on for this one open and off again.
    constexpr DWORD rights = PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION;
    HANDLE process = OpenProcess (rights, FALSE, pid);
    if (process == nullptr && GetLastError() == ERROR_ACCESS_DENIED && setDebugPrivilege (true))
    {
        process = OpenProcess (rights, FALSE, pid);
        const DWORD error = GetLastError();
        setDebugPrivilege (false);
        SetLastError (error);
    }
    if (process == nullptr)
        return GetLastError() == ERROR_INVALID_PARAMETER ? notFound : accessDenied;   // (gone meanwhile: invalid)
    if (! imageIsAudiodg (process))   // (not Windows' own: never changed)
    {
        CloseHandle (process);
        return notFound;
    }

    int code = ok;
    if (a.back)   // BACK TO NORMAL: Normal, and every core the system allows
    {
        DWORD_PTR mask = 0, system = 0;
        if (! SetPriorityClass (process, NORMAL_PRIORITY_CLASS))
            code = notSet;
        else if (! GetProcessAffinityMask (process, &mask, &system) || (mask != system && ! SetProcessAffinityMask (process, system)))
            code = notUnpinned;
    }
    else if (! SetPriorityClass (process, HIGH_PRIORITY_CLASS))   // High: never realtime
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
    runElevated (commandLine (pinToLastCore ? lastCore : -1), owner, std::move (done));
}

void AudioBoost::unboost (void* owner, std::function<void (int)> done)
{
    runElevated (backCommandLine(), owner, std::move (done));
}

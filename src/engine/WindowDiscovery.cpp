#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <tlhelp32.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <string>

#include "WindowDiscovery.h"
#include "../ProductName.h"

#pragma comment (lib, "dwmapi.lib")

using Microsoft::WRL::ComPtr;

namespace WindowDiscovery
{
namespace
{
    juce::File isolatedRoot()
    {
        return Product::dataFolder().getChildFile ("Isolated");
    }

    // Chrome adds the tab's state to its accessible name: "Title - Audio playing - Memory usage - 349 MB".
    Tab cleanTab (juce::String name)
    {
        static const juce::StringArray states { "Audio playing", "Audio muted", "Tab is muted", "Pinned", "Network error", "Crashed",
                                                "Media is being recorded", "Camera in use", "Microphone in use", "Screen is being shared",
                                                "Connected to a Bluetooth device", "Connected to a USB device", "Is being casted",
                                                "Loading", "Wants your attention", "Paused" };
        Tab tab;
        for (;;)
        {
            const int at = name.lastIndexOf (" - ");
            if (at < 0)
                break;

            const auto last = name.substring (at + 3).trim();
            const auto before = name.substring (0, at);

            if (last.containsAnyOf ("0123456789") && (last.endsWithIgnoreCase ("MB") || last.endsWithIgnoreCase ("GB") || last.endsWithIgnoreCase ("KB"))
                && before.endsWithIgnoreCase ("memory usage"))
            {
                name = before.substring (0, juce::jmax (0, before.lastIndexOf (" - ")));
                continue;
            }

            if (! states.contains (last, true))
                break;

            tab.playing = tab.playing || last.equalsIgnoreCase ("Audio playing");
            name = before;
        }
        tab.title = name.trim();
        return tab;
    }

    ComPtr<IUIAutomation> automation()
    {
        ComPtr<IUIAutomation> uia;
        CoCreateInstance (__uuidof (CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&uia));
        // A hung browser answers nothing: each call gives up on it after a second (Windows 8 and
        // later; the defaults are 2 s to connect and 20 s per call).
        ComPtr<IUIAutomation2> uia2;
        if (uia != nullptr && SUCCEEDED (uia.As (&uia2)))
        {
            uia2->put_ConnectionTimeout (1000);
            uia2->put_TransactionTimeout (1000);
        }
        return uia;
    }

    juce::String nameOf (IUIAutomationElement* e)
    {
        BSTR name = nullptr;
        juce::String text;
        if (SUCCEEDED (e->get_CurrentName (&name)) && name != nullptr)
            text = juce::String (name);
        SysFreeString (name);
        return text;
    }

    // The tab strip's tabs: TabItems anywhere in the window except inside a page (Document),
    // whose own tab widgets aren't browser tabs. Bounded, so a huge window can't stall the scan.
    // `owner`: the thread running it, which may be asked to stop part way (Faider closing).
    void findTabs (IUIAutomationTreeWalker* walker, IUIAutomationElement* parent, int depth, int& budget,
                   std::vector<ComPtr<IUIAutomationElement>>& found, const juce::Thread* owner)
    {
        if (depth > 16)
            return;

        ComPtr<IUIAutomationElement> child;
        walker->GetFirstChildElement (parent, &child);
        while (child != nullptr && --budget > 0)
        {
            if (owner != nullptr && owner->threadShouldExit())
            {
                budget = 0;   // (the levels above stop too)
                return;
            }
            CONTROLTYPEID type = 0;
            child->get_CurrentControlType (&type);
            if (type == UIA_TabItemControlTypeId)
                found.push_back (child);
            else if (type != UIA_DocumentControlTypeId)
                findTabs (walker, child.Get(), depth + 1, budget, found, owner);

            ComPtr<IUIAutomationElement> next;
            walker->GetNextSiblingElement (child.Get(), &next);
            child = next;
        }
    }

    std::vector<ComPtr<IUIAutomationElement>> tabElements (IUIAutomation* uia, HWND window, const juce::Thread* owner = nullptr)
    {
        std::vector<ComPtr<IUIAutomationElement>> found;
        ComPtr<IUIAutomationElement> root;
        ComPtr<IUIAutomationTreeWalker> walker;
        if (SUCCEEDED (uia->ElementFromHandle (window, &root)) && root != nullptr && SUCCEEDED (uia->get_ControlViewWalker (&walker)))
        {
            int budget = 1500;
            findTabs (walker.Get(), root.Get(), 0, budget, found, owner);
        }
        return found;
    }

    bool isChromium (HWND window)
    {
        wchar_t cls[64] = {};
        GetClassNameW (window, cls, (int) std::size (cls));
        return juce::String (cls) == "Chrome_WidgetWin_1";
    }

    struct Candidate { HWND handle; DWORD pid; juce::String title; };

    BOOL CALLBACK collect (HWND window, LPARAM param)
    {
        auto& out = *reinterpret_cast<std::vector<Candidate>*> (param);
        if (! IsWindowVisible (window) || GetWindow (window, GW_OWNER) != nullptr
            || (GetWindowLongW (window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0)
            return TRUE;

        BOOL cloaked = FALSE;   // suspended Store apps and windows on other virtual desktops
        DwmGetWindowAttribute (window, DWMWA_CLOAKED, &cloaked, sizeof (cloaked));
        if (cloaked)
            return TRUE;

        wchar_t title[512] = {};
        if (GetWindowTextW (window, title, (int) std::size (title)) <= 0)
            return TRUE;

        DWORD pid = 0;
        GetWindowThreadProcessId (window, &pid);
        if (pid != 0 && pid != GetCurrentProcessId())
            out.push_back ({ window, pid, juce::String (title) });
        return TRUE;
    }

    // `owner`: the thread running it, which may be asked to stop part way (Faider closing).
    // `knownTabs`: browsers' tabs aren't read again (UI Automation) but kept from these windows.
    std::vector<Window> scan (const juce::Thread* owner = nullptr, const std::vector<Window>* knownTabs = nullptr)
    {
        std::vector<Candidate> candidates;
        EnumWindows (collect, (LPARAM) &candidates);

        ComPtr<IUIAutomation> uia;
        if (knownTabs == nullptr)
            uia = automation();
        std::vector<Window> windows;

        for (auto& c : candidates)
        {
            if (owner != nullptr && owner->threadShouldExit())
                break;
            if (isIsolatedProcess (c.pid))
                continue;

            Window w;
            w.handle = (juce::pointer_sized_int) c.handle;
            w.processId = c.pid;
            w.exePath = exePathOf (c.pid);
            w.title = c.title;
            if (w.exePath.isEmpty() || (w.exeName().equalsIgnoreCase ("explorer.exe") && c.title == "Program Manager"))   // the desktop
                continue;

            if (knownTabs != nullptr)
            {
                for (auto& known : *knownTabs)
                    if (known.handle == w.handle)
                        w.tabs = known.tabs;
            }
            else if (uia != nullptr && isChromium (c.handle))
                for (auto& e : tabElements (uia.Get(), c.handle, owner))
                    if (auto tab = cleanTab (nameOf (e.Get())); tab.title.isNotEmpty())
                        w.tabs.push_back (tab);

            // "Page title - Helium": the browser names itself after the last " - ".
            w.product = w.isBrowser() && c.title.contains (" - ") ? c.title.fromLastOccurrenceOf (" - ", false, false).trim()
                                                                   : juce::File (w.exePath).getFileNameWithoutExtension();
            windows.push_back (std::move (w));
        }
        return windows;
    }

    int slotOf (const juce::String& sourceName)
    {
        return sourceName.fromFirstOccurrenceOf ("isolated:", false, true).upToFirstOccurrenceOf ("|", false, false).getIntValue();
    }

    bool launch (const juce::String& exe, const juce::String& folder, const juce::String& url)
    {
        // The exe and address come from the session file when a tab is reopened: only a Chromium
        // browser that exists (the tabs come from those; isChromium), and only a web address with
        // nothing in it that could end the quoting or add a switch ("--" stops the browser reading
        // switches before it).
        static const juce::StringArray browsers { "chrome.exe", "msedge.exe", "brave.exe", "vivaldi.exe", "opera.exe", "helium.exe",
                                                  "chromium.exe", "arc.exe", "thorium.exe" };
        const juce::File program (exe);
        if (! program.existsAsFile() || ! browsers.contains (program.getFileName(), true)
            || ! (url.startsWithIgnoreCase ("http://") || url.startsWithIgnoreCase ("https://"))
            || url.containsAnyOf ("\" \t\r\n") || exe.containsChar ('"') || folder.containsChar ('"'))
            return false;
        const auto command = "\"" + exe + "\" --user-data-dir=\"" + folder + "\" --no-first-run --no-default-browser-check --new-window -- \"" + url + "\"";
        std::wstring line (command.toWideCharPointer());   // CreateProcessW may write to it
        STARTUPINFOW si { sizeof (si) };
        PROCESS_INFORMATION pi {};
        if (! CreateProcessW (nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
            return false;
        CloseHandle (pi.hThread);
        CloseHandle (pi.hProcess);
        return true;
    }
}

//==============================================================================
Scanner::Scanner() : Thread ("Window discovery") { startThread (juce::Thread::Priority::low); }
Scanner::~Scanner() { stopThread (10000); }   // (a scan stops between windows; one big browser's tabs can take a few seconds)

std::vector<Window> Scanner::snapshot() const
{
    const juce::ScopedLock sl (lock);
    return latest;
}

void Scanner::run()
{
    CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    // Browsers' tabs are read through UI Automation, which costs this thread and makes the browser
    // keep its accessibility tree built: only while Faider is in front (where its menus open).
    // Behind other windows the window list stays current and the tabs already found are kept.
    juce::uint32 lastScan = 0;
    bool wasInFront = false, first = true;
    while (! threadShouldExit())
    {
        const bool inFront = juce::Process::isForegroundProcess();
        if (first || (inFront && ! wasInFront) || juce::Time::getMillisecondCounter() - lastScan >= 3000)
        {
            const auto known = snapshot();
            auto found = scan (this, inFront || first ? nullptr : &known);
            {
                const juce::ScopedLock sl (lock);
                latest = std::move (found);
            }
            lastScan = juce::Time::getMillisecondCounter();
            first = false;
        }
        wasInFront = inFront;
        wait (250);
    }
    CoUninitialize();
}

//==============================================================================
bool isIsolated (const juce::String& sourceName) { return sourceName.startsWith ("isolated:"); }

juce::String isolatedFolder (const juce::String& sourceName)
{
    return isolatedRoot().getChildFile (juce::String (slotOf (sourceName))).getFullPathName();
}

juce::String appLabel (const juce::String& sourceName)
{
    return isIsolated (sourceName) ? "Isolated tab " + juce::String (slotOf (sourceName))
                                   : juce::File (sourceName).getFileNameWithoutExtension();
}

juce::String isolateTab (const Window& window, const juce::String& tabTitle, const juce::StringArray& inUse, juce::String& error)
{
    // (Already initialised on the message thread: this only counts up, and is counted back down.)
    struct ComScope
    {
        const bool ok = SUCCEEDED (CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED));
        ~ComScope() { if (ok) CoUninitialize(); }
    } com;
    auto uia = automation();
    if (uia == nullptr)
    {
        error = "UI Automation isn't available.";
        return {};
    }

    const auto hwnd = (HWND) window.handle;
    ComPtr<IUIAutomationElement> tab;
    for (auto& e : tabElements (uia.Get(), hwnd))
        if (cleanTab (nameOf (e.Get())).title == tabTitle)
            tab = e;

    if (tab == nullptr)
    {
        error = "The tab \"" + tabTitle + "\" isn't open any more.";
        return {};
    }

    // Only the front tab's page is readable, so bring it forward, then wait for its page to show.
    ComPtr<IUIAutomationSelectionItemPattern> select;
    if (SUCCEEDED (tab->GetCurrentPatternAs (UIA_SelectionItemPatternId, IID_PPV_ARGS (&select))) && select != nullptr)
        select->Select();

    ComPtr<IUIAutomationElement> root;
    ComPtr<IUIAutomationCondition> isDocument;
    VARIANT documentType;
    documentType.vt = VT_I4;
    documentType.lVal = UIA_DocumentControlTypeId;
    uia->ElementFromHandle (hwnd, &root);
    uia->CreatePropertyCondition (UIA_ControlTypePropertyId, documentType, &isDocument);

    // This runs on the message thread, so it waits in short steps and for 1 s at most (each UIA
    // call also gives up after 1 s: automation()). A page slower than that: try again.
    juce::String url;
    const auto started = juce::Time::getMillisecondCounter();
    while (root != nullptr && isDocument != nullptr)
    {
        ComPtr<IUIAutomationElement> page;
        root->FindFirst (TreeScope_Descendants, isDocument.Get(), &page);
        if (page != nullptr && nameOf (page.Get()) == tabTitle)
        {
            VARIANT value;
            VariantInit (&value);
            if (SUCCEEDED (page->GetCurrentPropertyValue (UIA_ValueValuePropertyId, &value)) && value.vt == VT_BSTR && value.bstrVal != nullptr)
                url = juce::String (value.bstrVal);
            VariantClear (&value);
            if (url.startsWithIgnoreCase ("http"))
                break;
        }
        if (juce::Time::getMillisecondCounter() - started > 1000)
            break;
        juce::Thread::sleep (50);
    }

    if (! url.startsWithIgnoreCase ("http"))
    {
        error = "Couldn't read the address of \"" + tabTitle + "\".";
        return {};
    }

    int slot = 1;
    auto taken = [&inUse] (int n) { for (auto& s : inUse) if (isIsolated (s) && slotOf (s) == n) return true; return false; };
    while (taken (slot))
        ++slot;

    const auto name = "isolated:" + juce::String (slot) + "|" + window.exePath + "|" + url;
    const auto folder = juce::File (isolatedFolder (name));
    folder.createDirectory();

    if (! launch (window.exePath, folder.getFullPathName(), url))
    {
        error = "Couldn't start " + window.product + ".";
        return {};
    }
    return name;
}

bool reopen (const juce::String& sourceName)
{
    if (! isIsolated (sourceName) || findIsolatedProcess (sourceName) != 0)
        return false;

    const auto exe = sourceName.fromFirstOccurrenceOf ("|", false, false).upToFirstOccurrenceOf ("|", false, false);
    const auto url = sourceName.fromFirstOccurrenceOf ("|", false, false).fromFirstOccurrenceOf ("|", false, false);
    return launch (exe, isolatedFolder (sourceName), url);
}

juce::String exePathOf (uint32_t processId)
{
    juce::String path;
    if (auto process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) processId))
    {
        wchar_t buffer[MAX_PATH * 2] = {};
        DWORD size = (DWORD) std::size (buffer);
        if (QueryFullProcessImageNameW (process, 0, buffer, &size))
            path = juce::String (buffer, (size_t) size);
        CloseHandle (process);
    }
    return path;
}

juce::String commandLineOf (uint32_t processId)
{
    using Query = LONG (WINAPI*) (HANDLE, ULONG, PVOID, ULONG, PULONG);
    static const auto query = (Query) (void*) GetProcAddress (GetModuleHandleW (L"ntdll.dll"), "NtQueryInformationProcess");
    struct Counted { USHORT length, maximumLength; PWSTR buffer; };   // UNICODE_STRING
    constexpr ULONG processCommandLineInformation = 60;

    juce::String text;
    auto process = query != nullptr ? OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) processId) : nullptr;
    if (process == nullptr)
        return text;

    ULONG size = 0;
    query (process, processCommandLineInformation, nullptr, 0, &size);
    if (size > sizeof (Counted))
    {
        juce::HeapBlock<char> buffer (size, true);
        if (query (process, processCommandLineInformation, buffer.get(), size, &size) >= 0)
        {
            const auto* s = reinterpret_cast<const Counted*> (buffer.get());
            text = juce::String (s->buffer, (size_t) (s->length / sizeof (wchar_t)));
        }
    }
    CloseHandle (process);
    return text;
}

bool isIsolatedProcess (uint32_t processId)
{
    return commandLineOf (processId).containsIgnoreCase (isolatedRoot().getFullPathName());
}

uint32_t findIsolatedProcess (const juce::String& sourceName)
{
    if (! isIsolated (sourceName))
        return 0;

    const auto folder = "--user-data-dir=\"" + isolatedFolder (sourceName) + "\"";
    const auto exe = juce::File (sourceName.fromFirstOccurrenceOf ("|", false, false).upToFirstOccurrenceOf ("|", false, false)).getFileName();
    auto snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    uint32_t found = 0;
    PROCESSENTRY32W entry { sizeof (entry) };
    for (BOOL ok = Process32FirstW (snapshot, &entry); ok && found == 0; ok = Process32NextW (snapshot, &entry))
    {
        if (! juce::String (entry.szExeFile).equalsIgnoreCase (exe))
            continue;
        const auto line = commandLineOf (entry.th32ProcessID);
        if (line.containsIgnoreCase (folder) && ! line.contains ("--type="))   // the browser itself, not a helper
            found = entry.th32ProcessID;
    }
    CloseHandle (snapshot);
    return found;
}
}

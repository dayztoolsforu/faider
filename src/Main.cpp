#include <juce_gui_basics/juce_gui_basics.h>
#include "ProductName.h"
#include <crtdbg.h>
#include <exception>
#include "MainComponent.h"
#include "StartWithWindows.h"
#include "engine/PluginScan.h"
#include "engine/AppCapture.h"
#include "ui/AboutBox.h"
#include "ui/WindowBackground.h"
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>   // (after JUCE's headers: its "small" and the like would break them)
#include <dbghelp.h>
#include <exdisp.h>
#include <shlobj.h>
#include <shldisp.h>
#include <wrl/client.h>
#pragma comment (lib, "dbghelp.lib")
#pragma comment (lib, "ole32.lib")
#pragma comment (lib, "oleaut32.lib")

// NOT AS ADMINISTRATOR. SteamVR, its dashboard and the in-game overlay run with the desktop's
// ordinary rights, and Windows keeps them from reaching (or taking input from) an elevated
// program. A copy started elevated (from an administrator terminal, say) starts itself again
// through the desktop's shell, which gives it the shell's ordinary rights, and goes. Steps that
// need an administrator (the cable's settings, re-enabling a device) ask for it themselves.
namespace Unelevated
{
    bool isElevated()
    {
        HANDLE token = nullptr;
        if (! OpenProcessToken (GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;
        TOKEN_ELEVATION elevation {};
        DWORD size = 0;
        const bool ok = GetTokenInformation (token, TokenElevation, &elevation, sizeof (elevation), &size);
        CloseHandle (token);
        return ok && elevation.TokenIsElevated != 0;
    }

    // The desktop's shell running elevated too (User Account Control off, the built-in Administrator,
    // Windows Sandbox), or no shell at all: starting again through it would bring back the same
    // rights, over and over, so there's no point.
    bool shellIsElevated()
    {
        DWORD shellId = 0;
        if (GetWindowThreadProcessId (GetShellWindow(), &shellId) == 0 || shellId == 0)
            return true;
        HANDLE shell = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shellId);
        if (shell == nullptr)
            return false;
        HANDLE token = nullptr;
        TOKEN_ELEVATION elevation {};
        DWORD size = 0;
        const bool ok = OpenProcessToken (shell, TOKEN_QUERY, &token)
                        && GetTokenInformation (token, TokenElevation, &elevation, sizeof (elevation), &size);
        if (token != nullptr)
            CloseHandle (token);
        CloseHandle (shell);
        return ok && elevation.TokenIsElevated != 0;
    }

    // Runs `exe args` as the desktop would (Explorer's ShellExecute: its rights, not ours).
    bool launchAsShell (const juce::String& exe, const juce::String& args)
    {
        using Microsoft::WRL::ComPtr;
        const HRESULT init = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
        bool launched = false;
        {
            ComPtr<IShellWindows> windows;
            ComPtr<IDispatch> desktop, background, application;
            ComPtr<IServiceProvider> services;
            ComPtr<IShellBrowser> browser;
            ComPtr<IShellView> view;
            ComPtr<IShellFolderViewDual> folderView;
            ComPtr<IShellDispatch2> shell;
            VARIANT none {};
            long hwnd = 0;
            if (SUCCEEDED (CoCreateInstance (CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS (&windows)))
                && windows->FindWindowSW (&none, &none, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &desktop) == S_OK
                && SUCCEEDED (desktop.As (&services))
                && SUCCEEDED (services->QueryService (SID_STopLevelBrowser, IID_PPV_ARGS (&browser)))
                && SUCCEEDED (browser->QueryActiveShellView (&view))
                && SUCCEEDED (view->GetItemObject (SVGIO_BACKGROUND, IID_PPV_ARGS (&background)))
                && SUCCEEDED (background.As (&folderView))
                && SUCCEEDED (folderView->get_Application (&application))
                && SUCCEEDED (application.As (&shell)))
            {
                BSTR file = SysAllocString (exe.toWideCharPointer());
                VARIANT parameters {}, directory {}, operation {}, show {};
                parameters.vt = VT_BSTR;
                parameters.bstrVal = SysAllocString (args.toWideCharPointer());
                directory.vt = VT_BSTR;
                directory.bstrVal = SysAllocString (juce::File (exe).getParentDirectory().getFullPathName().toWideCharPointer());
                show.vt = VT_I4;
                show.lVal = SW_SHOWNORMAL;
                launched = SUCCEEDED (shell->ShellExecute (file, parameters, directory, operation, show));
                SysFreeString (file);
                VariantClear (&parameters);
                VariantClear (&directory);
            }
        }
        if (SUCCEEDED (init))
            CoUninitialize();
        return launched;
    }

    // Started elevated: the same command line again through the shell, after this copy has gone
    // (--after: the new copy waits for this one's process to end). True if it's on its way.
    bool restartUnelevated (const juce::StringArray& args)
    {
        juce::StringArray next;
        for (int i = 0; i < args.size(); ++i)
        {
            if (args[i] == "--after") { ++i; continue; }
            next.add (args[i].containsChar (' ') ? args[i].quoted() : args[i]);
        }
        next.add ("--after");
        next.add (juce::String ((juce::uint64) GetCurrentProcessId()));
        return launchAsShell (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(), next.joinIntoString (" "));
    }
}

// Crashes, uncaught exceptions and debug-CRT assertion dialogs all append the message and a
// stack trace to %APPDATA%\Faider\crash.log, so a dialog closed in a hurry isn't lost.
namespace CrashLog
{
    // Found once, at install, so a crash doesn't have to ask Windows where %APPDATA% is. Writing
    // still allocates (the message, the stack trace) inside a crashed process: if the heap itself
    // is what broke, the log line can be lost or the handler can crash too. Accepted: a best try.
    juce::File logFile;

    void write (const juce::String& what)
    {
        logFile.appendText (juce::Time::getCurrentTime().toString (true, true, true, true) + "  " + what + "\n"
                            + juce::SystemStats::getStackBacktrace() + "\n");
    }

    // An address as "module+offset" (the module's file name), which a PDB or a crash dump turns
    // back into a function, whatever symbols the machine has.
    juce::String where (const void* address)
    {
        HMODULE module = nullptr;
        if (! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  static_cast<LPCWSTR> (address), &module) || module == nullptr)
            return juce::String::toHexString ((juce::pointer_sized_int) address);
        wchar_t path[MAX_PATH] {};
        GetModuleFileNameW (module, path, MAX_PATH);
        return juce::File (juce::String (path)).getFileName() + "+0x"
             + juce::String::toHexString ((juce::pointer_sized_int) ((const char*) address - (const char*) module));
    }

    // A crash: what and where, the stack as modules and offsets, and a minidump beside the log
    // (crash.dmp, the latest one) to open in a debugger with the build's PDB.
    void crashed (void* info)
    {
        auto* ep = static_cast<EXCEPTION_POINTERS*> (info);
        juce::String text = "crash";
        if (ep != nullptr && ep->ExceptionRecord != nullptr)
        {
            const auto* r = ep->ExceptionRecord;
            text << ": exception 0x" << juce::String::toHexString ((int) r->ExceptionCode) << " at " << where (r->ExceptionAddress);
            if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
                text << (r->ExceptionInformation[0] == 0 ? " reading " : r->ExceptionInformation[0] == 1 ? " writing " : " executing ")
                     << "0x" << juce::String::toHexString ((juce::pointer_sized_int) r->ExceptionInformation[1]);
            text << "  (thread " << (int) GetCurrentThreadId() << ")";
        }
        void* frames[64] {};
        const auto n = RtlCaptureStackBackTrace (0, 64, frames, nullptr);
        text << "\n  stack:";
        for (USHORT i = 0; i < n; ++i)
            text << "\n    " << where (frames[i]);
        logFile.appendText (juce::Time::getCurrentTime().toString (true, true, true, true) + "  " + text + "\n"
                            + juce::SystemStats::getStackBacktrace() + "\n");

        const auto dump = logFile.getSiblingFile ("crash.dmp");
        auto file = CreateFileW (dump.getFullPathName().toWideCharPointer(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION mei { GetCurrentThreadId(), ep, FALSE };
            MiniDumpWriteDump (GetCurrentProcess(), GetCurrentProcessId(), file,
                               (MINIDUMP_TYPE) (MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
                               ep != nullptr ? &mei : nullptr, nullptr, nullptr);
            CloseHandle (file);
        }
    }

    int crtReportHook (int, char* message, int*)
    {
        write ("CRT report: " + juce::String (message).trim());
        return 0;   // still show the dialog
    }

    void install()
    {
        // Kept from growing for ever: past a megabyte it starts again (the last one kept as crash.old.log).
        logFile = Product::dataFolder().getChildFile ("crash.log");
        if (logFile.getSize() > 1024 * 1024)
            logFile.moveFileTo (logFile.getSiblingFile ("crash.old.log"));

        juce::SystemStats::setApplicationCrashHandler ([] (void* info) { crashed (info); });
        std::set_terminate ([] { write ("std::terminate (uncaught exception?)"); std::abort(); });
        _CrtSetReportHook2 (_CRT_RPTHOOK_INSTALL, crtReportHook);
    }
}


class FaiderApplication : public juce::JUCEApplication
{
public:
    // RESTORE SESSION starts a new copy with "--after <the old copy's process id>": it waits here,
    // before looking for another copy to hand over to, until the old one has finished closing.
    FaiderApplication()
    {
        const auto args = getCommandLineParameterArray();
        // (a copy that is itself such a restart, still elevated, never starts again: no loop)
        if (Unelevated::isElevated() && ! args.contains ("--scan") && ! args.contains ("--quit") && ! args.contains ("--allow-admin")
            && ! args.contains ("--after") && ! Unelevated::shellIsElevated() && Unelevated::restartUnelevated (args))
            ExitProcess (0);   // (the copy with ordinary rights takes over)
        if (const int i = args.indexOf ("--after"); i >= 0)
            for (int waited = 0; waited < 60000 && processIsRunning ((uint32_t) args[i + 1].getLargeIntValue()); waited += 100)
                juce::Thread::sleep (100);
        startedAt = juce::Time::getMillisecondCounter();
    }

    juce::uint32 startedAt = 0;   // when the wait (if any) ended, then when initialise() finished

    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return getCommandLineParameters().contains ("--scan"); }   // a plugin test runs beside the desk

    void initialise (const juce::String& commandLine) override
    {
        // A plugin test (PluginScan.h): no window, no audio; the test, then out.
        if (commandLine.contains ("--scan"))
        {
            const auto args = getCommandLineParameterArray();
            const auto at = [&args] (const char* flag) { return args[args.indexOf (flag) + 1]; };
            setApplicationReturnValue (PluginScan::runChild (at ("--scan"), juce::File (at ("--out"))));
            quit();
            return;
        }

        // "--quit" that found no copy to quit (it had gone already): nothing to do, so don't start one.
        if (commandLine.contains ("--quit"))
        {
            quit();
            return;
        }

        Product::moveEarlierFolder();   // (before anything opens a file in it)
        StartWithWindows::moveEarlierEntry();
        CrashLog::install();
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
        tray = std::make_unique<TrayIcon> (*mainWindow);
        if (auto* content = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
            content->onNotify = [this] (const juce::String& title, const juce::String& text) { if (tray != nullptr) tray->showInfoBubble (title, text); };

        // Closing the window doesn't stop the audio, so say so.
        mainWindow->onHidden = [this]
        {
            tray->showInfoBubble (juce::String (Product::name) + " is still running",
                                  "Audio keeps playing. To stop, right-click the tray icon and Quit.");
        };

        // Started by Windows at login: stay in the tray.
        if (! commandLine.contains ("--minimised"))
            mainWindow->setVisible (true);
        startedAt = juce::Time::getMillisecondCounter();   // (loading the session can take a while)
    }

    void shutdown() override
    {
        tray = nullptr;
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

    // Launching Faider again while it's in the tray brings the window back; "Faider --quit"
    // quits the running copy fully (as the tray's Quit does), e.g. before installing a new build.
    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (mainWindow == nullptr)   // this copy is a plugin test
            return;
        if (commandLine.contains ("--quit"))
        {
            // A copy that waited for the one before it (--after) also heard what was sent to that
            // one while it waited: a quit within 2 s of it having started was meant for its predecessor.
            if (juce::Time::getMillisecondCounter() - startedAt < 2000 && getCommandLineParameters().contains ("--after"))
                return;
            systemRequestedQuit();
        }
        else
            mainWindow->bringBack();
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        // Any size (the desk's strips stretch to its height), and it can be maximised. It starts
        // as tall as the desk wants, as much as the screen has.
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colours::darkgrey, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* content = new MainComponent();
            setContentOwned (content, true);
            setResizable (true, true);
            exactScale.setMinimumSize (900, 560);
            setConstrainer (&exactScale);
            int startHeight = content->neededHeight();
            if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                startHeight = juce::jmin (startHeight, display->userArea.getHeight() - 40);
            centreWithSize (getWidth(), startHeight);

            // Back where it was last time (its left, top, width and height), if that's still on a
            // screen. (From before the height was free: left, top and width.)
            const auto saved = juce::StringArray::fromTokens (content->getSettings().getValue ("windowPlace"), " ", {});
            if (saved.size() == 3 || saved.size() == 4)
            {
                const juce::Rectangle<int> place (saved[0].getIntValue(), saved[1].getIntValue(), saved[2].getIntValue(), saved.size() == 4 ? saved[3].getIntValue() : getHeight());
                if (place.getWidth() > 200 && juce::Desktop::getInstance().getDisplays().getDisplayForRect (place) != nullptr
                    && juce::Desktop::getInstance().getDisplays().getTotalBounds (true).intersects (place.withHeight (40)))
                    setBounds (place);
            }
            setBoundsConstrained (getBounds());
            if (content->getSettings().getBoolValue ("windowMaximised"))   // (over the size it comes back to)
                setFullScreen (true);
            remembering = true;
            if (auto* peer = getPeer())   // not white where Windows shows the window before it's drawn (resizing)
                WindowBackground::set (*peer, Console::Colours::panelDark());
        }

        // With nothing focused, keys arrive here: let the app see them (Ctrl +/-/0 zoom).
        bool keyPressed (const juce::KeyPress& key) override
        {
            auto* content = getContentComponent();
            return content != nullptr && content->keyPressed (key);
        }

        void bringBack()
        {
            setVisible (true);
            setMinimised (false);
            toFront (true);
        }

        // Audio keeps running: closing the window only hides it. Quit from the tray menu.
        void closeButtonPressed() override
        {
            setVisible (false);
            if (onHidden != nullptr)
                onHidden();
        }

        std::function<void()> onHidden;

        void moved() override   { DocumentWindow::moved(); rememberPlace(); }
        void resized() override { DocumentWindow::resized(); rememberPlace(); }

    private:
        bool remembering = false;   // not while the window is first placed

        // Saved with the settings when Faider quits.
        void rememberPlace()
        {
            if (! remembering || isMinimised())
                return;
            auto* content = dynamic_cast<MainComponent*> (getContentComponent());
            if (content == nullptr)
                return;
            content->getSettings().setValue ("windowMaximised", isFullScreen());
            if (! isFullScreen())   // (maximised: keep the size it comes back to)
                content->getSettings().setValue ("windowPlace", juce::String (getX()) + " " + juce::String (getY()) + " " + juce::String (getWidth()) + " " + juce::String (getHeight()));
        }

        // At zooms like 120%, a window whose size isn't a whole number of screen pixels gets its
        // drawing stretched by a fraction to fit (ComponentPeer::handlePaint), by a different
        // fraction at every size, so fine detail (the screens' dots) shimmers and jumps while the
        // window is resized. Sizes go in steps that scale exactly: 5 app pixels = 6 screen pixels
        // at 120%. The edge being dragged moves; the opposite one stays put.
        struct ExactScaleConstrainer : juce::ComponentBoundsConstrainer
        {
            void checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous, const juce::Rectangle<int>& limits,
                              bool top, bool left, bool bottom, bool right) override
            {
                ComponentBoundsConstrainer::checkBounds (bounds, previous, limits, top, left, bottom, right);

                // Zoom and the display's own scaling together: one app pixel in screen pixels. The
                // bounds include the window's frame; it's the inside (client area) that's drawn.
                const auto* peer = window.getPeer();
                const int step = stepFor (juce::Desktop::getInstance().getGlobalScaleFactor() * (peer != nullptr ? peer->getPlatformScaleFactor() : 1.0));
                const auto frame = peer != nullptr ? peer->getFrameSize() : juce::BorderSize<int>();
                auto snapped = [step] (int size) { return juce::jmax (step, (size + step / 2) / step * step); };
                const int w = snapped (bounds.getWidth() - frame.getLeftAndRight()) + frame.getLeftAndRight();

                const int h = snapped (bounds.getHeight() - frame.getTopAndBottom()) + frame.getTopAndBottom();
                bounds = juce::Rectangle<int> (left ? bounds.getRight() - w : bounds.getX(), top ? bounds.getBottom() - h : bounds.getY(), w, h);
            }


            // The smallest size step that's a whole number of screen pixels at `s`.
            static int stepFor (double s)
            {
                for (int n = 1; n <= 20; ++n)
                    if (std::abs (n * s - std::round (n * s)) < 0.001)
                        return n;
                return 1;
            }

            explicit ExactScaleConstrainer (juce::Component& w) : window (w) {}
            juce::Component& window;
        };

        ExactScaleConstrainer exactScale { *this };
    };

    // Tray icon: click to show the window; right-click for scenes, Start with Windows and Quit.
    class TrayIcon : public juce::SystemTrayIconComponent,
                     private juce::Timer
    {
    public:
        explicit TrayIcon (MainWindow& w) : window (w)
        {
            show ({});
            startTimerHz (4);
        }

        void timerCallback() override
        {
            auto* content = dynamic_cast<MainComponent*> (window.getContentComponent());
            const auto alert = content != nullptr ? content->activeAlert() : juce::String();
            flashOn = alert.isNotEmpty() && ! flashOn;   // an alert flashes the icon, twice a second
            if (alert != shownAlert || alert.isNotEmpty())
                show (alert);
        }

        void show (const juce::String& alert)
        {
            shownAlert = alert;
            const auto icon = drawIcon (flashOn);
            setIconImage (icon, icon);
            setIconTooltip (alert.isNotEmpty() ? juce::String (Product::name) + " - ALERT: " + alert : juce::String (Product::name));
        }

        // Click: the tray mixer pops up. Double-click: the window.
        void mouseDoubleClick (const juce::MouseEvent&) override { window.bringBack(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu())
            {
                if (auto* content = dynamic_cast<MainComponent*> (window.getContentComponent()))
                {
                    const auto at = juce::Desktop::getMousePosition();
                    content->showTrayMixer (juce::Rectangle<int> (at.x - 4, at.y - 4, 8, 8), [this] { window.bringBack(); });
                }
                return;
            }

            juce::PopupMenu menu;
            menu.addItem ("Show " + juce::String (Product::name), [this] { window.bringBack(); });
            menu.addSeparator();
            if (auto* content = dynamic_cast<MainComponent*> (window.getContentComponent()))
            {
                content->addSceneItems (menu);
                content->addSessionItems (menu);
                menu.addSeparator();
            }
            menu.addItem ("Start with Windows", true, StartWithWindows::isOn(), [] { StartWithWindows::set (! StartWithWindows::isOn()); });
            menu.addItem ("About " + juce::String (Product::name) + "...", [] { AboutBox::show(); });
            menu.addSeparator();
            menu.addItem ("Quit " + juce::String (Product::name), [] { JUCEApplication::getInstance()->systemRequestedQuit(); });
            menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
        }

    private:
        // A red LED readout over two cream fader caps, as the program icon: reads as "mixer" at 16 px.
        // `alerting`: amber all over (flashed by the timer while an output's alert is on).
        static juce::Image drawIcon (bool alerting = false)
        {
            juce::Image image (juce::Image::ARGB, 32, 32, true);
            juce::Graphics g (image);
            g.setColour (alerting ? juce::Colour (0xffe0a020) : juce::Colour (0xff1b1d20));
            g.fillRoundedRectangle (0.0f, 0.0f, 32.0f, 32.0f, 6.0f);
            g.setColour (juce::Colour (0xff140807));
            g.fillRoundedRectangle (5.0f, 4.0f, 22.0f, 6.0f, 1.5f);
            g.setColour (juce::Colour (0xffff3a24));
            g.fillRoundedRectangle (7.0f, 6.0f, 18.0f, 2.0f, 1.0f);
            for (auto [x, cap] : { std::pair (11.0f, 19.0f), std::pair (21.0f, 15.0f) })
            {
                g.setColour (juce::Colour (0xff0a0a0b));
                g.fillRect (x - 1.0f, 12.0f, 2.0f, 16.0f);
                g.setColour (juce::Colour (0xffd6d3c7));
                g.fillRoundedRectangle (x - 4.0f, cap - 2.5f, 8.0f, 5.0f, 1.0f);
            }
            return image;
        }

        MainWindow& window;
        bool flashOn = false;
        juce::String shownAlert;
    };

    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<TrayIcon> tray;
};

START_JUCE_APPLICATION (FaiderApplication)

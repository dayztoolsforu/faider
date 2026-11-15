#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../ProductName.h"
#include "../engine/AudioEngine.h"
#include "../AudioBoost.h"
#include "Console.h"

// The Devices tab: VB-CABLE's settings (while it's installed) and the device tools at the top,
// every audio device in fixed columns, then the stream diagnostics (latency, dropouts) and the
// event log at the bottom.
namespace DevicesTab
{
    // Runs `command` in cmd.exe as administrator: Windows asks the user first (UAC). Returns
    // false if it couldn't start or the user said no. Doesn't wait for it to finish. cmd.exe is
    // Windows' own, by its full path, started in the system folder; the programs in `command` are
    // named the same way (systemTool), and what's put in it has passed AudioBoost::safeForCommand.
    bool runElevated (const juce::String& command);
    juce::String systemTool (const char* exe);   // "reg.exe": "C:\Windows\System32\reg.exe", quoted

    // The device instance ID of the first sound device whose description holds `description`, from
    // Windows' own list of them (SetupAPI: asked directly, so it's the same in every display
    // language and never waits on another program); empty if there's none.
    juce::String findSoundDevice (const juce::String& description);

    // Whether Windows has a device node switched off (Device Manager's "disabled"), asked of its
    // device tree directly (CfgMgr32), without elevation.
    bool nodeDisabled (const juce::String& instanceId);

    // Column x positions and widths, shared by the header and the rows. The last runs to the edge.
    // (titles in English, translated where drawn)
    struct Column { const char* title; int width; };
    inline constexpr Column columns[] = { { NEEDS_TRANS ("I/O"), 52 }, { NEEDS_TRANS ("Device"), 250 }, { NEEDS_TRANS ("Hardware"), 290 },
                                          { NEEDS_TRANS ("Format"), 130 }, { NEEDS_TRANS ("State"), 100 }, { NEEDS_TRANS ("Tags"), 0 } };

    // The settings and tools bars: a bar of keys with a unit above and below, the keys after a
    // label column of this width so they line up from bar to bar.
    inline constexpr int barHeight = Console::Space::bar + 2 * Console::Space::unit;
    inline constexpr int labelWidth = Console::Space::columns (3);

    template <typename Fn>
    void forEachColumn (int width, Fn&& fn)
    {
        int x = Console::Space::pad;   // the panels' content edge, as the keys above
        for (size_t i = 0; i < std::size (columns); ++i)
        {
            const int w = columns[i].width > 0 ? columns[i].width : juce::jmax (40, width - x - Console::Space::pad);
            fn ((int) i, juce::Rectangle<int> (x, 0, w - 8, 0));
            x += w;
        }
    }

    // Column titles; click one to sort the list by it, again to reverse. The sorted column shows an arrow.
    class Header : public juce::Component
    {
    public:
        std::function<void (int column, bool ascending)> onSort;

        void setSort (int column, bool ascending);

        void mouseUp (const juce::MouseEvent& e) override;

        void paint (juce::Graphics& g) override;

    private:
        int sortColumn = -1;
        bool sortAscending = true;
    };

    // VB-CABLE ("Mic for apps") settings, kept by its driver in HKLM\SOFTWARE\VB-Audio\Cable.
    // Changing them needs administrator approval (one UAC prompt), and the driver only reads
    // them when it starts, so Apply also restarts the cable driver; Faider reconnects to it.
    class CableSettings : public juce::Component,
                          private juce::Timer
    {
    public:
        static constexpr const char* key = "HKEY_LOCAL_MACHINE\\SOFTWARE\\VB-Audio\\Cable\\";

        CableSettings();

        bool isInstalled() const { return installed; }

        void lookAndFeelChanged() override;   // a new theme

        void resized() override;

        void paint (juce::Graphics& g) override;

    private:
        static int read (const char* name) { return juce::WindowsRegistry::getValue (juce::String (key) + name).getIntValue(); }

        void load();

        void updateApply();

        // The cable's device instance ID, from Windows' list of sound devices.
        static juce::String cableInstanceId() { return findSoundDevice ("VB-Audio Virtual Cable"); }

        void applySettings();

        void timerCallback() override;

        juce::Label title, status;
        juce::ComboBox sampleRate, latency;
        juce::TextButton apply { TRANS ("APPLY") };
        bool installed = false;
        int savedRate = 0, savedLatency = 0, checks = 0;
    };

    // Diagnostics: every open stream with the latency it adds and every dropout so far, plus
    // the master callback's load. Refreshed twice a second; anything that dropped audio is red.
    class Streams : public juce::Component,
                    private juce::Timer
    {
    public:
        explicit Streams (AudioEngine& e);

        int preferredHeight() const { return Console::Space::bar + 20 + 20 * juce::jmax (1, (int) stats.streams.size()) + Console::Space::gap; }

        void resized() override;

        void paint (juce::Graphics& g) override;

        std::function<void()> onSizeChanged;

    private:
        // The top bar: the heading, the summary and the keys, the panel's pad in from each side.
        juce::Rectangle<int> titleBar() const { return getLocalBounds().removeFromTop (Console::Space::bar).reduced (Console::Space::pad, 0); }

        void timerCallback() override { refresh(); }

        void refresh();

        // What the driver offers, each with its duration at the current rate.
        void fillBufferChoices();

        AudioEngine& engine;
        AudioEngine::Stats stats;
        juce::TextButton reset { TRANS ("RESET STATS") };
        juce::Label bufferLabel;
        juce::ComboBox buffer;
    };

    // What to do with the devices selected in the list (Ctrl/Shift-click for several):
    //   Off / On in Faider   Faider stops (or starts) using them: never opened, so they cost
    //                          nothing, and left out of its lists. Windows still has them.
    //   Disable / Enable in Windows   Turns them off (or on) in Windows itself, as Sound settings
    //                          does: one administrator prompt for all of them.
    //   Rename                 Faider's name for one device; Windows' name is unchanged.
    class DeviceTools : public juce::Component,
                        private juce::Timer
    {
    public:
        DeviceTools (juce::ListBox& deviceList, const juce::Array<EndpointInfo>& all, AudioEngine& e);

        std::function<void()> onDevicesChanged;   // menus and views that list devices should refresh

        void selectionChanged();

        void lookAndFeelChanged() override;

        void resized() override;

        void paint (juce::Graphics& g) override;

    private:
        juce::Array<EndpointInfo> selected() const;

        // The selected devices' names; Windows devices only (not ASIO drivers) for `windowsOnly`.
        juce::StringArray selectedNames (bool windowsOnly) const;

        void changed();

        // As Sound settings does it: no administrator prompt. Enabling also switches back on any
        // device whose Windows device node is off (from Device Manager, or Faider before this
        // worked this way): that part needs one administrator prompt, and its output goes to a
        // log, read back to say what Windows made of it.
        void setInWindows (bool enable);

        // How many it changed, asked of the devices themselves (pnputil's own words come in
        // Windows' display language); its report is read for why not.
        void timerCallback() override;

    public:
        void renameSelected();

    private:
        juce::ListBox& list;
        const juce::Array<EndpointInfo>& endpoints;
        AudioEngine& engine;
        juce::Label count, status;
        const juce::File windowsLog { juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Faider-devices.log") };
        int waitingFor = 0, checks = 0;
        bool windowsLogKept = true;       // pnputil's words went to windowsLog (not when its path isn't safe on a command line)
        juce::StringArray waitingNodes;   // the device nodes being switched back on (setInWindows)
        juce::TextButton offInFaider { TRANS ("EXCLUDE") }, onInFaider { TRANS ("INCLUDE") },
                         disableInWindows { TRANS ("DISABLE IN WINDOWS") }, enableInWindows { TRANS ("ENABLE IN WINDOWS") }, rename { TRANS ("RENAME") };
    };

    // The whole tab: VB-CABLE's settings, device tools, header and list, and the stream diagnostics.
    // (What sets the clock and Windows audio's priority are in SETTINGS > AUDIO.)
    class Panel : public juce::Component
    {
    public:
        Panel (juce::ListBox& deviceList, const juce::Array<EndpointInfo>& endpoints, AudioEngine& engine);

        DeviceTools& getTools() { return tools; }
        Header& getHeader() { return header; }

        // The app's event log, along the bottom of this tab.
        void setLog (juce::Component& log);

        void resized() override;

    private:
        juce::ListBox& list;
        CableSettings cable;
        DeviceTools tools;
        Header header;
        Streams streams;
        juce::Component* eventLog = nullptr;   // the app's (setLog)
    };
}

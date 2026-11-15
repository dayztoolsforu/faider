#include "DevicesTab.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#pragma comment (lib, "setupapi.lib")
#pragma comment (lib, "cfgmgr32.lib")

namespace
{
    // Windows' folder for its own programs (System32).
    juce::String systemFolder()
    {
        wchar_t dir[MAX_PATH] {};
        const UINT n = GetSystemDirectoryW (dir, MAX_PATH);
        return n > 0 && n < MAX_PATH ? juce::String (dir) : juce::String();
    }
}

juce::String DevicesTab::systemTool (const char* exe)
{
    return (systemFolder() + "\\" + exe).quoted();
}

bool DevicesTab::runElevated (const juce::String& command)
{
    // Nothing that runs as administrator is found by its bare name (the folder Faider was started
    // in, or the PATH, could hold another cmd.exe or reg.exe): cmd.exe by its full path, started in
    // the system folder, without the AutoRun commands the registry can give it (/d), and the line
    // after /c taken as it is (/s).
    const auto folder = systemFolder();
    if (folder.isEmpty() || ! AudioBoost::safeForCommand (folder))
        return false;
    const auto cmd = folder + "\\cmd.exe";
    const juce::String params ("/d /s /c \"" + command + "\"");
    const auto result = (INT_PTR) ShellExecuteW (nullptr, L"runas", cmd.toWideCharPointer(), params.toWideCharPointer(), folder.toWideCharPointer(), SW_HIDE);
    return result > 32;   // ShellExecute: <= 32 is an error, including "the user declined"
}

juce::String DevicesTab::findSoundDevice (const juce::String& description)
{
    juce::String found;
    const HDEVINFO devices = SetupDiGetClassDevsW (&GUID_DEVCLASS_MEDIA, nullptr, nullptr, 0);   // (every sound device, connected or not)
    if (devices == INVALID_HANDLE_VALUE)
        return found;

    SP_DEVINFO_DATA info {};
    info.cbSize = sizeof (info);
    for (DWORD i = 0; found.isEmpty() && SetupDiEnumDeviceInfo (devices, i, &info); ++i)
    {
        // Its description, or the name it shows under (a USB device's own name is there).
        auto holds = [&] (DWORD property)
        {
            wchar_t text[512] {};
            return SetupDiGetDeviceRegistryPropertyW (devices, &info, property, nullptr, (PBYTE) text, sizeof (text) - sizeof (wchar_t), nullptr)
                && juce::String (text).contains (description);
        };
        wchar_t id[MAX_DEVICE_ID_LEN + 1] {};
        if ((holds (SPDRP_DEVICEDESC) || holds (SPDRP_FRIENDLYNAME))
            && SetupDiGetDeviceInstanceIdW (devices, &info, id, MAX_DEVICE_ID_LEN, nullptr))
            found = juce::String (id);
    }
    SetupDiDestroyDeviceInfoList (devices);
    return found;
}

bool DevicesTab::nodeDisabled (const juce::String& instanceId)
{
    DEVINST node = 0;
    if (CM_Locate_DevNodeW (&node, const_cast<DEVINSTID_W> (instanceId.toWideCharPointer()), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
        return false;
    ULONG status = 0, problem = 0;
    return CM_Get_DevNode_Status (&status, &problem, node, 0) == CR_SUCCESS
        && (status & DN_HAS_PROBLEM) != 0 && problem == CM_PROB_DISABLED;
}

namespace DevicesTab
{
    void Header::setSort (int column, bool ascending)
    {
        sortColumn = column;
        sortAscending = ascending;
        repaint();
    }

    void Header::mouseUp (const juce::MouseEvent& e)
    {
        forEachColumn (getWidth(), [&] (int i, juce::Rectangle<int> r)
        {
            if (e.x >= r.getX() && e.x < r.getX() + r.getWidth() + 8)
            {
                setSort (i, i == sortColumn ? ! sortAscending : true);
                if (onSort)
                    onSort (sortColumn, sortAscending);
            }
        });
    }

    void Header::paint (juce::Graphics& g)
    {
        g.fillAll (Console::Colours::panelDark().darker (0.25f));
        const juce::Font font (juce::FontOptions (Console::Type::body, juce::Font::bold));
        g.setFont (font);
        forEachColumn (getWidth(), [&] (int i, juce::Rectangle<int> r)
        {
            const auto title = juce::String (columns[(size_t) i].title);
            const bool sorted = i == sortColumn;
            g.setColour (sorted ? Console::Colours::legend() : Console::Colours::legendDim());
            g.drawText (title.toUpperCase(), r.withHeight (getHeight()), juce::Justification::centredLeft, true);

            if (sorted)   // a small arrow after the title
            {
                const float x = (float) r.getX() + (float) juce::GlyphArrangement::getStringWidthInt (font, title) + 6.0f, y = (float) getHeight() * 0.5f;
                juce::Path arrow;
                arrow.addTriangle (x, sortAscending ? y + 2.5f : y - 2.5f, x + 7.0f, sortAscending ? y + 2.5f : y - 2.5f, x + 3.5f, sortAscending ? y - 2.5f : y + 2.5f);
                g.fillPath (arrow);
            }
        });
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

    CableSettings::CableSettings()
    {
        title.setText ("VB-CABLE  (Mic for apps)", juce::dontSendNotification);
        title.setFont (juce::FontOptions (Console::Type::name, juce::Font::bold));
        addAndMakeVisible (title);

        for (int rate : { 44100, 48000, 88200, 96000, 176400, 192000 })
            sampleRate.addItem (juce::String (rate) + " Hz", rate);
        for (int smp : { 512, 1024, 2048, 3072, 4096, 5120, 6144, 7168 })
            latency.addItem (juce::String (smp) + " samples max", smp);
        sampleRate.setTooltip ("Cable sample rate");
        latency.setTooltip ("Cable max latency");
        for (auto* c : { &sampleRate, &latency })
        {
            c->onChange = [this] { updateApply(); };
            addAndMakeVisible (c);
        }

        apply.setTooltip ("Apply and restart the cable");
        apply.onClick = [this] { applySettings(); };
        addAndMakeVisible (apply);

        status.setFont (juce::FontOptions (Console::Type::body));
        status.setColour (juce::Label::textColourId, Console::Colours::legendDim());
        addAndMakeVisible (status);

        load();
    }

    void CableSettings::lookAndFeelChanged()
    {
        status.setColour (juce::Label::textColourId, Console::Colours::legendDim());
    }

    void CableSettings::resized()
    {
        Console::KeyRow row (getLocalBounds().reduced (Console::Space::pad, 0));
        row.leftWidth (title, labelWidth).left (sampleRate, 2).left (latency, 2).left (apply);
        const auto rest = row.rest().withTrimmedLeft (Console::Space::pad);
        status.setBounds (rest.withSizeKeepingCentre (rest.getWidth(), Console::Space::key));
    }

    void CableSettings::paint (juce::Graphics& g)
    {
        g.fillAll (Console::Colours::panel());
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

    void CableSettings::load()
    {
        installed = juce::WindowsRegistry::valueExists (juce::String (key) + "VBAudioCableWDM_SR");
        savedRate = read ("VBAudioCableWDM_SR");
        savedLatency = read ("VBAudioCableWDM_Latency");

        for (auto* c : { &sampleRate, &latency })
            c->setEnabled (installed);

        if (! installed)
        {
            status.setText ("VB-CABLE isn't installed.", juce::dontSendNotification);
            apply.setEnabled (false);
            return;
        }

        if (sampleRate.indexOfItemId (savedRate) < 0) sampleRate.addItem (juce::String (savedRate) + " Hz", savedRate);
        if (latency.indexOfItemId (savedLatency) < 0) latency.addItem (juce::String (savedLatency) + " samples max", savedLatency);
        sampleRate.setSelectedId (savedRate, juce::dontSendNotification);
        latency.setSelectedId (savedLatency, juce::dontSendNotification);
        updateApply();
    }

    void CableSettings::updateApply()
    {
        const bool changed = sampleRate.getSelectedId() != savedRate || latency.getSelectedId() != savedLatency;
        apply.setEnabled (installed && changed);
        status.setText (changed ? "Not applied yet." : "Current: " + juce::String (savedRate) + " Hz, " + juce::String (savedLatency) + " samples max.",
                        juce::dontSendNotification);
    }

    void CableSettings::applySettings()
    {
        // (the cable's ID goes on the line only as plain text: one that isn't, and the cable isn't
        // restarted here; restarting Windows does it)
        const auto cable = cableInstanceId();
        const auto id = AudioBoost::safeForCommand (cable) ? cable : juce::String();
        const auto reg = systemTool ("reg.exe") + " add \"HKLM\\SOFTWARE\\VB-Audio\\Cable\" /f /t REG_DWORD /v ";
        auto command = reg + "VBAudioCableWDM_SR /d " + juce::String (sampleRate.getSelectedId())
                     + " && " + reg + "VBAudioCableWDM_Latency /d " + juce::String (latency.getSelectedId());
        if (id.isNotEmpty())
            command << " && " << systemTool ("pnputil.exe") << " /restart-device \"" << id << "\"";

        // One elevated cmd: Windows shows its UAC prompt; nothing changes unless it's approved.
        if (! runElevated (command))
        {
            status.setText ("Cancelled. Nothing changed.", juce::dontSendNotification);
            return;
        }

        status.setText ("Applying..." + juce::String (id.isEmpty() ? "  Restart Windows to finish." : ""), juce::dontSendNotification);
        apply.setEnabled (false);
        checks = 0;
        startTimer (1000);   // see when the values land
    }

    void CableSettings::timerCallback()
    {
        const bool landed = read ("VBAudioCableWDM_SR") == sampleRate.getSelectedId()
                         && read ("VBAudioCableWDM_Latency") == latency.getSelectedId();
        if (landed || ++checks > 30)
        {
            stopTimer();
            load();
            if (landed)
                status.setText ("Applied: " + juce::String (savedRate) + " Hz, " + juce::String (savedLatency) + " samples max.",
                                juce::dontSendNotification);
        }
    }

    Streams::Streams (AudioEngine& e) : engine (e)
    {
        reset.setTooltip ("Reset stats");
        reset.onClick = [this] { engine.resetStats(); refresh(); };
        addAndMakeVisible (reset);

        bufferLabel.setText ("Buffer", juce::dontSendNotification);
        bufferLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (bufferLabel);
        buffer.setTooltip ("Buffer size");
        buffer.onChange = [this]
        {
            if (const int size = buffer.getSelectedId(); size > 0 && size != engine.getInterfaceBufferSize())
            {
                engine.setInterfaceBufferSize (size);
                engine.resetStats();   // counters from the old size would only confuse
            }
            fillBufferChoices();
        };
        addAndMakeVisible (buffer);

        startTimerHz (10);   // fast enough to watch problems as they happen
        refresh();
    }

    void Streams::resized()
    {
        Console::KeyRow row (titleBar());
        row.right (reset, Console::KeyRow::columnsFor (reset.getButtonText())).groupRight()
           .right (buffer, 2).rightWidth (bufferLabel, Console::Space::column);
    }

    void Streams::paint (juce::Graphics& g)
    {
        g.fillAll (Console::Colours::panel());
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (0, 0, getWidth(), 1);

        // Summary line.
        g.setFont (juce::FontOptions (Console::Type::name, juce::Font::bold));
        g.setColour (Console::Colours::legend());
        auto bar = titleBar().withRight (bufferLabel.getX() - Console::Space::pad);
        g.drawText ("STREAMS", bar.removeFromLeft (Console::Space::column), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (Console::Type::body));
        const bool hurt = stats.overloads > 0 || stats.skippedBlocks > 0;
        g.setColour (hurt ? Console::Colours::warning() : Console::Colours::legendDim());
        g.drawText ("CALLBACK LOAD " + juce::String (juce::roundToInt (stats.loadPercent)) + "% (PEAK " + juce::String (juce::roundToInt (stats.peakLoadPercent)) + "%)   "
                    + "OVERLOADS " + juce::String (stats.overloads) + "   BLOCKS SKIPPED " + juce::String (stats.skippedBlocks),
                    bar.withTrimmedLeft (Console::Space::unit), juce::Justification::centredLeft, true);

        // Table.
        static constexpr std::pair<const char*, int> cols[] = { { "Stream", 300 }, { "Dir", 44 }, { "Format", 130 }, { "Driver", 80 },
                                                                 { "Buffered", 80 }, { "Total", 80 }, { "Drift", 80 }, { "Under / over / resync", 160 }, { "Xruns", 0 } };
        auto cell = [&] (int row, auto&& draw)
        {
            int x = Console::Space::pad;
            for (size_t i = 0; i < std::size (cols); ++i)
            {
                const int w = cols[i].second > 0 ? cols[i].second : getWidth() - x - Console::Space::pad;
                draw ((int) i, juce::Rectangle<int> (x, Console::Space::bar + row * 20, w - 8, 20));
                x += w;
            }
        };

        g.setFont (juce::FontOptions (Console::Type::body, juce::Font::bold));
        g.setColour (Console::Colours::legendDim());
        cell (0, [&] (int i, juce::Rectangle<int> r) { g.drawText (juce::String (cols[(size_t) i].first).toUpperCase(), r, juce::Justification::centredLeft, true); });

        g.setFont (juce::FontOptions (Console::Type::body));
        auto msText = [] (double v) { return v < 0.0 ? juce::String ("n/a") : juce::String (v, 1) + " ms"; };

        for (size_t row = 0; row < stats.streams.size(); ++row)
        {
            const auto& s = stats.streams[row];
            const bool dropped = s.underruns > 0 || s.overruns > 0 || s.resyncs > 0 || s.xruns > 0;
            const juce::String values[] = {
                s.name, s.direction,
                juce::String (s.rate, 0) + " Hz / " + juce::String (s.block),
                msText (s.driverMs), s.direction == "out" || s.direction == "in" || s.direction == "app" ? msText (s.bufferedMs) : juce::String(),
                msText (s.totalMs), juce::String (s.driftPpm, 1) + " ppm",
                juce::String (s.underruns) + " / " + juce::String (s.overruns) + " / " + juce::String (s.resyncs),
                s.xruns < 0 ? juce::String ("n/a") : juce::String (s.xruns) };

            g.setColour (dropped ? Console::Colours::warning() : Console::Colours::legend());
            cell ((int) row + 1, [&] (int i, juce::Rectangle<int> r) { g.drawText (values[i].toUpperCase(), r, juce::Justification::centredLeft, true); });
        }
    }

    void Streams::refresh()
    {
        const auto rows = stats.streams.size();
        stats = engine.getStats();
        if (rows != stats.streams.size() && onSizeChanged)
            onSizeChanged();
        if (buffer.getNumItems() == 0 || buffer.getSelectedId() != engine.getInterfaceBufferSize())
            fillBufferChoices();
        repaint();
    }

    void Streams::fillBufferChoices()
    {
        const auto sizes = engine.getInterfaceBufferSizes();
        double rate = 48000.0;
        for (auto& s : stats.streams)
            if (s.name.contains ("(interface"))
                rate = s.rate;

        buffer.clear (juce::dontSendNotification);
        for (int size : sizes)
            buffer.addItem (juce::String (size) + " smp  (" + juce::String (1000.0 * size / rate, 1) + " ms)", size);
        buffer.setSelectedId (engine.getInterfaceBufferSize(), juce::dontSendNotification);
    }

    DeviceTools::DeviceTools (juce::ListBox& deviceList, const juce::Array<EndpointInfo>& all, AudioEngine& e)
        : list (deviceList), endpoints (all), engine (e)
    {
        offInFaider.setTooltip (juce::String ("Don't use in ") + Product::name);
        onInFaider.setTooltip (juce::String ("Use in ") + Product::name);
        disableInWindows.setTooltip ("Turn off in Windows");
        enableInWindows.setTooltip ("Turn on in Windows");
        rename.setTooltip (juce::String ("Rename in ") + Product::name);

        offInFaider.onClick = [this] { engine.setDeviceDisabled (selectedNames (false), true); changed(); };
        onInFaider.onClick = [this] { engine.setDeviceDisabled (selectedNames (false), false); changed(); };
        disableInWindows.onClick = [this] { setInWindows (false); };
        enableInWindows.onClick = [this] { setInWindows (true); };
        rename.onClick = [this] { renameSelected(); };

        for (auto* b : { &offInFaider, &onInFaider, &disableInWindows, &enableInWindows, &rename })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }
        for (auto* l : { &count, &status })
        {
            l->setFont (juce::FontOptions (Console::Type::body));
            addAndMakeVisible (l);
        }
        selectionChanged();
    }

    void DeviceTools::selectionChanged()
    {
        const int n = list.getNumSelectedRows();
        count.setText (n == 0 ? juce::String ("Select devices (Ctrl/Shift-click for several)") : juce::String (n) + " selected",
                       juce::dontSendNotification);
        const bool any = ! selectedNames (false).isEmpty(), anyWindows = ! selectedNames (true).isEmpty();
        offInFaider.setEnabled (any);
        onInFaider.setEnabled (any);
        disableInWindows.setEnabled (anyWindows);
        enableInWindows.setEnabled (anyWindows);
        rename.setEnabled (n == 1);
    }

    void DeviceTools::lookAndFeelChanged()
    {
        count.setColour (juce::Label::textColourId, Console::Colours::legendDim());
        status.setColour (juce::Label::textColourId, Console::Colours::legendDim());
    }

    void DeviceTools::resized()
    {
        using Console::KeyRow;
        auto widest = [] (juce::TextButton& a, juce::TextButton& b) { return juce::jmax (KeyRow::columnsFor (a.getButtonText()), KeyRow::columnsFor (b.getButtonText())); };
        const int inFaider = widest (offInFaider, onInFaider), inWindows = widest (disableInWindows, enableInWindows);

        KeyRow row (getLocalBounds().reduced (Console::Space::pad, 0));
        row.leftWidth (count, labelWidth)
           .left (offInFaider, inFaider).left (onInFaider, inFaider).group()
           .left (disableInWindows, inWindows).left (enableInWindows, inWindows).group()
           .left (rename, KeyRow::columnsFor (rename.getButtonText()));
        const auto rest = row.rest().withTrimmedLeft (Console::Space::pad);
        status.setBounds (rest.withSizeKeepingCentre (rest.getWidth(), Console::Space::key));
    }

    void DeviceTools::paint (juce::Graphics& g)
    {
        g.fillAll (Console::Colours::panel());
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

    juce::Array<EndpointInfo> DeviceTools::selected() const
    {
        juce::Array<EndpointInfo> rows;
        for (int i = 0; i < list.getNumSelectedRows(); ++i)
            if (juce::isPositiveAndBelow (list.getSelectedRow (i), endpoints.size()))
                rows.add (endpoints[list.getSelectedRow (i)]);
        return rows;
    }

    juce::StringArray DeviceTools::selectedNames (bool windowsOnly) const
    {
        juce::StringArray names;
        for (auto& e : selected())
            if (! windowsOnly || e.api == "WASAPI")
                names.add (e.name);
        return names;
    }

    void DeviceTools::changed()
    {
        list.repaint();
        if (onDevicesChanged)
            onDevicesChanged();
    }

    void DeviceTools::setInWindows (bool enable)
    {
        int changed = 0, total = 0;
        juce::StringArray commands, nodes;
        for (auto& e : selected())
        {
            if (e.api != "WASAPI")
                continue;
            ++total;
            if (setEndpointEnabled (e.id, enable))
                ++changed;
            if (const auto node = "SWD\\MMDEVAPI\\" + e.id; enable && nodeDisabled (node) && AudioBoost::safeForCommand (node))   // (its ID as plain text only)
            {
                commands.add (systemTool ("pnputil.exe") + " /enable-device \"" + node + "\"");
                nodes.add (node);
            }
        }

        if (total == 0)
            return;
        status.setText (juce::String (changed) + " of " + juce::String (total) + (enable ? " enabled" : " disabled") + " in Windows.",
                        juce::dontSendNotification);
        if (commands.isEmpty())
            return;

        // pnputil's words go to a log in %TEMP%, read back below: only if that path is plain text on
        // the line (else they aren't kept, and the devices themselves say how it went).
        windowsLog.deleteFile();
        windowsLogKept = AudioBoost::safeForCommand (windowsLog.getFullPathName());
        const auto command = windowsLogKept ? "(" + commands.joinIntoString (" & ") + ") > \"" + windowsLog.getFullPathName() + "\" 2>&1"
                                            : commands.joinIntoString (" & ");
        if (! runElevated (command))
        {
            status.setText ("Cancelled. Nothing changed.", juce::dontSendNotification);
            return;
        }

        status.setText ("Turning " + juce::String (commands.size()) + " device(s) back on...", juce::dontSendNotification);
        waitingFor = commands.size();
        waitingNodes = nodes;
        checks = 0;
        startTimer (500);
    }

    void DeviceTools::timerCallback()
    {
        const auto report = windowsLog.loadFileAsString();
        int succeeded = 0;
        for (auto& node : waitingNodes)
            if (! nodeDisabled (node))
                ++succeeded;

        if (succeeded < waitingFor && ++checks < 20 && ! report.containsIgnoreCase ("failed"))
            return;   // still running

        stopTimer();
        juce::String text = juce::String (succeeded) + " of " + juce::String (waitingFor) + " changed in Windows.";
        for (auto& line : juce::StringArray::fromLines (report))
            if (line.containsIgnoreCase ("failed") || line.containsIgnoreCase ("denied") || line.containsIgnoreCase ("not "))
            {
                text << "  " << line.trim();
                break;
            }
        if (windowsLogKept && report.trim().isEmpty())
            text = "Windows didn't report back (see " + windowsLog.getFullPathName() + ").";
        status.setText (text, juce::dontSendNotification);
    }

    void DeviceTools::renameSelected()
    {
        const auto rows = selected();
        if (rows.size() != 1)
            return;

        const auto device = rows[0].name;
        auto* window = new juce::AlertWindow ("RENAME DEVICE", device + "\nLeave empty to use the Windows name",
                                              juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", deviceAlias (device));
        if (auto* editor = window->getTextEditor ("name"))
            editor->setInputFilter (new Console::CapitalsFilter(), true);   // names are in capitals
        window->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, device] (int result)
        {
            if (result == 1)
            {
                setDeviceAlias (device, window->getTextEditorContents ("name"));
                changed();
            }
        }), true);
    }

    Panel::Panel (juce::ListBox& deviceList, const juce::Array<EndpointInfo>& endpoints, AudioEngine& engine)
        : list (deviceList), tools (deviceList, endpoints, engine), streams (engine)
    {
        addAndMakeVisible (cable);
        addAndMakeVisible (tools);
        addAndMakeVisible (header);
        addAndMakeVisible (list);
        addAndMakeVisible (streams);
        streams.onSizeChanged = [this] { resized(); };
        list.setMultipleSelectionEnabled (true);
    }

    void Panel::setLog (juce::Component& log)
    {
        eventLog = &log;
        addAndMakeVisible (log);
        resized();
    }

    void Panel::resized()
    {
        auto r = getLocalBounds();
        if (eventLog != nullptr)
            eventLog->setBounds (r.removeFromBottom (juce::jmin (160, r.getHeight() / 3)));
        if (cable.isInstalled())
            cable.setBounds (r.removeFromTop (barHeight));
        tools.setBounds (r.removeFromTop (barHeight));
        streams.setBounds (r.removeFromBottom (juce::jmin (streams.preferredHeight(), r.getHeight() / 2)));
        header.setBounds (r.removeFromTop (22));
        list.setBounds (r);
    }
}

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../ProductName.h"
#include "../engine/AudioEngine.h"
#include "../AudioBoost.h"
#include "Console.h"

// The Devices tab: what sets the clock, Windows audio's priority (BOOST WINDOWS AUDIO), VB-CABLE's settings (while it's installed) and the device tools at the top,
// every audio device in fixed columns, then the stream diagnostics (latency, dropouts) and the
// event log at the bottom.
namespace DevicesTab
{
    // Runs `command` in cmd.exe as administrator: Windows asks the user first (UAC). Returns
    // false if it couldn't start or the user said no. Doesn't wait for it to finish.
    bool runElevated (const juce::String& command);

    // Column x positions and widths, shared by the header and the rows. The last runs to the edge.
    struct Column { const char* title; int width; };
    inline constexpr Column columns[] = { { "I/O", 52 }, { "Device", 250 }, { "Hardware", 290 }, { "Format", 130 }, { "State", 100 }, { "Tags", 0 } };

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

        void setSort (int column, bool ascending)
        {
            sortColumn = column;
            sortAscending = ascending;
            repaint();
        }

        void mouseUp (const juce::MouseEvent& e) override
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

        void paint (juce::Graphics& g) override
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

        CableSettings()
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

        bool isInstalled() const { return installed; }

        void lookAndFeelChanged() override   // a new theme
        {
            status.setColour (juce::Label::textColourId, Console::Colours::legendDim());
        }

        void resized() override
        {
            Console::KeyRow row (getLocalBounds().reduced (Console::Space::pad, 0));
            row.leftWidth (title, labelWidth).left (sampleRate, 2).left (latency, 2).left (apply);
            const auto rest = row.rest().withTrimmedLeft (Console::Space::pad);
            status.setBounds (rest.withSizeKeepingCentre (rest.getWidth(), Console::Space::key));
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Console::Colours::panel());
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRect (0, getHeight() - 1, getWidth(), 1);
        }

    private:
        static int read (const char* name) { return juce::WindowsRegistry::getValue (juce::String (key) + name).getIntValue(); }

        void load()
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

        void updateApply()
        {
            const bool changed = sampleRate.getSelectedId() != savedRate || latency.getSelectedId() != savedLatency;
            apply.setEnabled (installed && changed);
            status.setText (changed ? "Not applied yet." : "Current: " + juce::String (savedRate) + " Hz, " + juce::String (savedLatency) + " samples max.",
                            juce::dontSendNotification);
        }

        // The cable's device instance ID, from Windows' list of sound devices.
        static juce::String cableInstanceId()
        {
            juce::ChildProcess pnputil;
            if (! pnputil.start ("pnputil /enum-devices /class MEDIA"))
                return {};

            juce::String id;
            for (auto& line : juce::StringArray::fromLines (pnputil.readAllProcessOutput()))
            {
                if (line.trimStart().startsWith ("Instance ID:"))
                    id = line.fromFirstOccurrenceOf (":", false, false).trim();
                else if (line.contains ("Device Description:") && line.contains ("VB-Audio Virtual Cable"))
                    return id;
            }
            return {};
        }

        void applySettings()
        {
            const auto id = cableInstanceId();
            const auto reg = juce::String ("reg add \"HKLM\\SOFTWARE\\VB-Audio\\Cable\" /f /t REG_DWORD /v ");
            auto command = reg + "VBAudioCableWDM_SR /d " + juce::String (sampleRate.getSelectedId())
                         + " && " + reg + "VBAudioCableWDM_Latency /d " + juce::String (latency.getSelectedId());
            if (id.isNotEmpty())
                command << " && pnputil /restart-device \"" << id << "\"";

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

        void timerCallback() override
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

        juce::Label title, status;
        juce::ComboBox sampleRate, latency;
        juce::TextButton apply { "APPLY" };
        bool installed = false;
        int savedRate = 0, savedLatency = 0, checks = 0;
    };

    // CLOCK: what sets the clock: the interface (ASIO, Faider's own pick, as without a choice), a
    // Windows playback device, or the built-in clock. Kept by the app (`choice`, `choose`) and
    // used from the next start, as --clock is; --clock on the command line still wins.
    class ClockBar : public juce::Component,
                     private juce::Timer
    {
    public:
        ClockBar (AudioEngine& e, const juce::Array<EndpointInfo>& all) : engine (e), endpoints (all)
        {
            title.setText ("CLOCK", juce::dontSendNotification);
            title.setFont (juce::FontOptions (Console::Type::name, juce::Font::bold));
            addAndMakeVisible (title);
            pick.setTooltip ("What sets the clock, from the next start");
            pick.onClick = [this] { showMenu(); };
            addAndMakeVisible (pick);
            status.setFont (juce::FontOptions (Console::Type::body));
            status.setColour (juce::Label::textColourId, Console::Colours::legendDim());
            addAndMakeVisible (status);
            startTimerHz (2);
        }

        std::function<juce::String()> choice;             // "" (the interface), "builtin" or "windows:NAME"
        std::function<void (const juce::String&)> choose;
        std::function<juce::String()> note;               // after the state now: a choice waiting for the next start, --clock

        void lookAndFeelChanged() override { status.setColour (juce::Label::textColourId, Console::Colours::legendDim()); }

        void resized() override
        {
            Console::KeyRow row (getLocalBounds().reduced (Console::Space::pad, 0));
            row.leftWidth (title, labelWidth).left (pick, 4);
            const auto rest = row.rest().withTrimmedLeft (Console::Space::pad);
            status.setBounds (rest.withSizeKeepingCentre (rest.getWidth(), Console::Space::key));
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Console::Colours::panel());
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRect (0, getHeight() - 1, getWidth(), 1);
        }

    private:
        static juce::String keyText (const juce::String& c)
        {
            if (c == "builtin")
                return "SET BY: THE BUILT-IN CLOCK";
            if (c.startsWith (AudioEngine::windowsClockPrefix))
                return "SET BY: " + c.fromFirstOccurrenceOf (AudioEngine::windowsClockPrefix, false, false).toUpperCase();
            return "SET BY: THE INTERFACE";
        }

        void timerCallback() override
        {
            const auto c = choice ? choice() : juce::String();
            if (const auto text = keyText (c); text != pick.getButtonText())
                pick.setButtonText (text);
            const auto device = engine.getClock() == AudioEngine::Clock::asio ? engine.getInterfaceAdapter() : engine.getClockDevice();
            auto text = "Now: " + engine.getClockName() + (device.isNotEmpty() ? " (" + device + ")" : juce::String());
            if (note)
                if (const auto more = note(); more.isNotEmpty())
                    text << ". " << more;
            if (text != status.getText())
                status.setText (text, juce::dontSendNotification);
        }

        void showMenu()
        {
            const auto c = choice ? choice() : juce::String();
            auto set = [this] (const juce::String& to) { if (choose) choose (to); timerCallback(); };
            juce::PopupMenu menu, devices;
            menu.addSectionHeader ("WHAT SETS THE CLOCK (FROM THE NEXT START)");
            menu.addItem ("THE INTERFACE (ASIO)", true, c.isEmpty(), [set] { set ({}); });
            for (auto& e : endpoints)
                if (e.api == "WASAPI" && e.isOutput && e.isActive() && ! engine.isDeviceDisabled (e.name))
                {
                    const auto key = AudioEngine::windowsClockPrefix + e.name;
                    devices.addItem (endpointDisplayName (e).toUpperCase(), true, c == key, [set, key] { set (key); });
                }
            if (const auto name = c.fromFirstOccurrenceOf (AudioEngine::windowsClockPrefix, false, false);
                name.isNotEmpty() && std::none_of (endpoints.begin(), endpoints.end(), [&name] (const EndpointInfo& e) { return e.isOutput && e.isActive() && e.name == name; }))
                devices.addItem (name.toUpperCase() + " (NOT CONNECTED)", true, true, [set, c] { set (c); });   // (the one chosen, while it's away)
            menu.addSubMenu ("A WINDOWS PLAYBACK DEVICE", devices, devices.getNumItems() > 0, juce::Image(), c.startsWith (AudioEngine::windowsClockPrefix));
            menu.addItem ("THE BUILT-IN CLOCK", true, c == "builtin", [set] { set ("builtin"); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (pick));
        }

        AudioEngine& engine;
        const juce::Array<EndpointInfo>& endpoints;
        juce::Label title, status;
        juce::TextButton pick;
    };

    // WINDOWS AUDIO: Windows' audio engine (audiodg.exe) and its priority now, and BOOST WINDOWS
    // AUDIO (AudioBoost.h), which the app runs (`boost`: Windows asks first) and keeps (`wanted`,
    // `pinned`). ASIO interfaces go around it.
    class WindowsAudioBar : public juce::Component,
                            private juce::Timer
    {
    public:
        WindowsAudioBar()
        {
            title.setText ("WINDOWS AUDIO", juce::dontSendNotification);
            title.setFont (juce::FontOptions (Console::Type::name, juce::Font::bold));
            addAndMakeVisible (title);
            key.setTooltip ("Windows' audio engine (audiodg.exe) at High priority instead of Normal, Windows' default: it can stop crackle "
                            "on Windows devices and captured apps. ASIO interfaces don't go through it. Windows asks for an administrator's "
                            "approval each time, and a restart of Windows audio (or the PC) undoes it: you're told when. BACK TO NORMAL undoes it now. "
                            "PIN TO ONE CORE (off unless you pick it) holds it to the last core: only for crackle on a USB headset.");
            key.onClick = [this] { showMenu(); };
            addAndMakeVisible (key);
            status.setFont (juce::FontOptions (Console::Type::body));
            status.setColour (juce::Label::textColourId, Console::Colours::legendDim());
            addAndMakeVisible (status);
            startTimerHz (2);
        }

        std::function<AudioBoost::State()> state;   // as the app last read it
        std::function<bool()> wanted, pinned;       // BOOST asked for; PIN TO ONE CORE with it
        std::function<void()> boost;                // BOOST now (Windows asks first)
        std::function<void()> stopBoosting;         // no longer wanted (audiodg stays as it is until it restarts)
        std::function<void()> backToNormal;         // BACK TO NORMAL now: Normal, every core (Windows asks first)
        std::function<void (bool)> setPinned;
        std::function<juce::String()> note;         // what came of the last BOOST, or one under way

        void lookAndFeelChanged() override { status.setColour (juce::Label::textColourId, Console::Colours::legendDim()); }

        void resized() override
        {
            Console::KeyRow row (getLocalBounds().reduced (Console::Space::pad, 0));
            row.leftWidth (title, labelWidth).left (key, 4);
            const auto rest = row.rest().withTrimmedLeft (Console::Space::pad);
            status.setBounds (rest.withSizeKeepingCentre (rest.getWidth(), Console::Space::key));
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Console::Colours::panel());
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRect (0, getHeight() - 1, getWidth(), 1);
        }

    private:
        AudioBoost::State now() const { return state ? state() : AudioBoost::State(); }

        void timerCallback() override
        {
            const auto s = now();
            const juce::String keyText (AudioBoost::isBoosted (s.priority) ? "BOOSTED" : "BOOST WINDOWS AUDIO");
            if (keyText != key.getButtonText())
                key.setButtonText (keyText);

            juce::String text;
            if (s.pid == 0)
                text = "Now: not running (it starts with the first sound)";
            else if (s.priority == AudioBoost::Priority::unknown)
                text = "Now: UNKNOWN (its priority couldn't be read)";
            else
                text = "Now: " + AudioBoost::priorityName (s.priority) + " priority"
                     + (s.priority == AudioBoost::Priority::normal ? juce::String (", Windows' default") : juce::String())
                     + (s.pinnedCore >= 0 ? ", on core " + juce::String (s.pinnedCore) + " only" : juce::String());
            const auto more = note ? note() : juce::String();
            text << ". " << (more.isNotEmpty() ? more : juce::String ("ASIO interfaces aren't affected"));
            if (text != status.getText())
                status.setText (text, juce::dontSendNotification);
        }

        void showMenu()
        {
            const auto s = now();
            const bool want = wanted && wanted(), pin = pinned && pinned();
            juce::PopupMenu menu;
            menu.addSectionHeader ("WINDOWS AUDIO (AUDIODG.EXE): " + AudioBoost::priorityName (s.priority));
            menu.addItem ("BOOST (HIGH PRIORITY)", true, want, [this, want, boosted = AudioBoost::isBoosted (s.priority)]
            {
                if (want && boosted) { if (stopBoosting) stopBoosting(); }
                else if (boost) boost();
            });
            menu.addItem ("PIN TO ONE CORE (ONLY FOR USB-HEADSET CRACKLE)", true, pin, [this, pin] { if (setPinned) setPinned (! pin); });
            menu.addItem ("BACK TO NORMAL", s.pid != 0 && (s.priority != AudioBoost::Priority::normal || s.pinnedCore >= 0), false,
                          [this] { if (backToNormal) backToNormal(); });
            menu.addSeparator();
            menu.addItem ("WINDOWS ASKS FOR AN ADMINISTRATOR EACH TIME", false, false, nullptr);
            menu.addItem ("IT DOESN'T AFFECT ASIO INTERFACES", false, false, nullptr);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (key));
            timerCallback();
        }

        juce::Label title, status;
        juce::TextButton key { "BOOST WINDOWS AUDIO" };
    };

    // Diagnostics: every open stream with the latency it adds and every dropout so far, plus
    // the master callback's load. Refreshed twice a second; anything that dropped audio is red.
    class Streams : public juce::Component,
                    private juce::Timer
    {
    public:
        explicit Streams (AudioEngine& e) : engine (e)
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

        int preferredHeight() const { return Console::Space::bar + 20 + 20 * juce::jmax (1, (int) stats.streams.size()) + Console::Space::gap; }

        void resized() override
        {
            Console::KeyRow row (titleBar());
            row.right (reset, Console::KeyRow::columnsFor (reset.getButtonText())).groupRight()
               .right (buffer, 2).rightWidth (bufferLabel, Console::Space::column);
        }

        void paint (juce::Graphics& g) override
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

        std::function<void()> onSizeChanged;

    private:
        // The top bar: the heading, the summary and the keys, the panel's pad in from each side.
        juce::Rectangle<int> titleBar() const { return getLocalBounds().removeFromTop (Console::Space::bar).reduced (Console::Space::pad, 0); }

        void timerCallback() override { refresh(); }

        void refresh()
        {
            const auto rows = stats.streams.size();
            stats = engine.getStats();
            if (rows != stats.streams.size() && onSizeChanged)
                onSizeChanged();
            if (buffer.getNumItems() == 0 || buffer.getSelectedId() != engine.getInterfaceBufferSize())
                fillBufferChoices();
            repaint();
        }

        // What the driver offers, each with its duration at the current rate.
        void fillBufferChoices()
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

        AudioEngine& engine;
        AudioEngine::Stats stats;
        juce::TextButton reset { "RESET STATS" };
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
        DeviceTools (juce::ListBox& deviceList, const juce::Array<EndpointInfo>& all, AudioEngine& e)
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

        std::function<void()> onDevicesChanged;   // menus and views that list devices should refresh

        void selectionChanged()
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

        void lookAndFeelChanged() override
        {
            count.setColour (juce::Label::textColourId, Console::Colours::legendDim());
            status.setColour (juce::Label::textColourId, Console::Colours::legendDim());
        }

        void resized() override
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

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Console::Colours::panel());
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRect (0, getHeight() - 1, getWidth(), 1);
        }

    private:
        juce::Array<EndpointInfo> selected() const
        {
            juce::Array<EndpointInfo> rows;
            for (int i = 0; i < list.getNumSelectedRows(); ++i)
                if (juce::isPositiveAndBelow (list.getSelectedRow (i), endpoints.size()))
                    rows.add (endpoints[list.getSelectedRow (i)]);
            return rows;
        }

        // The selected devices' names; Windows devices only (not ASIO drivers) for `windowsOnly`.
        juce::StringArray selectedNames (bool windowsOnly) const
        {
            juce::StringArray names;
            for (auto& e : selected())
                if (! windowsOnly || e.api == "WASAPI")
                    names.add (e.name);
            return names;
        }

        void changed()
        {
            list.repaint();
            if (onDevicesChanged)
                onDevicesChanged();
        }

        // As Sound settings does it: no administrator prompt. Enabling also switches back on any
        // device whose Windows device node is off (from Device Manager, or Faider before this
        // worked this way): that part needs one administrator prompt, and its output goes to a
        // log, read back to say what Windows made of it.
        void setInWindows (bool enable)
        {
            int changed = 0, total = 0;
            juce::StringArray commands;
            for (auto& e : selected())
            {
                if (e.api != "WASAPI")
                    continue;
                ++total;
                if (setEndpointEnabled (e.id, enable))
                    ++changed;
                if (enable && nodeDisabled ("SWD\\MMDEVAPI\\" + e.id))
                    commands.add ("pnputil /enable-device \"SWD\\MMDEVAPI\\" + e.id + "\"");
            }

            if (total == 0)
                return;
            status.setText (juce::String (changed) + " of " + juce::String (total) + (enable ? " enabled" : " disabled") + " in Windows.",
                            juce::dontSendNotification);
            if (commands.isEmpty())
                return;

            windowsLog.deleteFile();
            const auto command = "(" + commands.joinIntoString (" & ") + ") > \"" + windowsLog.getFullPathName() + "\" 2>&1";
            if (! runElevated (command))
            {
                status.setText ("Cancelled. Nothing changed.", juce::dontSendNotification);
                return;
            }

            status.setText ("Turning " + juce::String (commands.size()) + " device(s) back on...", juce::dontSendNotification);
            waitingFor = commands.size();
            checks = 0;
            startTimer (500);
        }

        // Whether Windows has a device node switched off (pnputil reads this without elevation).
        static bool nodeDisabled (const juce::String& instanceId)
        {
            juce::ChildProcess pnputil;
            if (! pnputil.start ("pnputil /enum-devices /instanceid \"" + instanceId + "\""))
                return false;
            for (auto& line : juce::StringArray::fromLines (pnputil.readAllProcessOutput()))
                if (line.trimStart().startsWith ("Status:"))
                    return line.containsIgnoreCase ("Disabled");
            return false;
        }

        // Reads pnputil's report: how many it changed, or why not.
        void timerCallback() override
        {
            const auto report = windowsLog.loadFileAsString();
            int succeeded = 0;
            for (auto& line : juce::StringArray::fromLines (report))
                if (line.containsIgnoreCase ("successfully"))
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
            if (report.trim().isEmpty())
                text = "Windows didn't report back (see " + windowsLog.getFullPathName() + ").";
            status.setText (text, juce::dontSendNotification);
        }

    public:
        void renameSelected()
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

    private:
        juce::ListBox& list;
        const juce::Array<EndpointInfo>& endpoints;
        AudioEngine& engine;
        juce::Label count, status;
        const juce::File windowsLog { juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Faider-devices.log") };
        int waitingFor = 0, checks = 0;
        juce::TextButton offInFaider { "EXCLUDE" }, onInFaider { "INCLUDE" },
                         disableInWindows { "DISABLE IN WINDOWS" }, enableInWindows { "ENABLE IN WINDOWS" }, rename { "RENAME" };
    };

    // The whole tab: settings, device tools, header and list, and the stream diagnostics.
    class Panel : public juce::Component
    {
    public:
        Panel (juce::ListBox& deviceList, const juce::Array<EndpointInfo>& endpoints, AudioEngine& engine)
            : list (deviceList), clock (engine, endpoints), tools (deviceList, endpoints, engine), streams (engine)
        {
            addAndMakeVisible (clock);
            addAndMakeVisible (windowsAudio);
            addAndMakeVisible (cable);
            addAndMakeVisible (tools);
            addAndMakeVisible (header);
            addAndMakeVisible (list);
            addAndMakeVisible (streams);
            streams.onSizeChanged = [this] { resized(); };
            list.setMultipleSelectionEnabled (true);
        }

        DeviceTools& getTools() { return tools; }
        ClockBar& getClock() { return clock; }
        WindowsAudioBar& getWindowsAudio() { return windowsAudio; }
        Header& getHeader() { return header; }

        // The app's event log, along the bottom of this tab.
        void setLog (juce::Component& log)
        {
            eventLog = &log;
            addAndMakeVisible (log);
            resized();
        }

        void resized() override
        {
            auto r = getLocalBounds();
            if (eventLog != nullptr)
                eventLog->setBounds (r.removeFromBottom (juce::jmin (160, r.getHeight() / 3)));
            clock.setBounds (r.removeFromTop (barHeight));
            windowsAudio.setBounds (r.removeFromTop (barHeight));
            if (cable.isInstalled())
                cable.setBounds (r.removeFromTop (barHeight));
            tools.setBounds (r.removeFromTop (barHeight));
            streams.setBounds (r.removeFromBottom (juce::jmin (streams.preferredHeight(), r.getHeight() / 2)));
            header.setBounds (r.removeFromTop (22));
            list.setBounds (r);
        }

    private:
        juce::ListBox& list;
        ClockBar clock;
        WindowsAudioBar windowsAudio;
        CableSettings cable;
        DeviceTools tools;
        Header header;
        Streams streams;
        juce::Component* eventLog = nullptr;   // the app's (setLog)
    };
}

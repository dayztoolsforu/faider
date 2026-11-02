#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

#include "ControllerHelper.h"
#include "Console.h"
#include "QrCode.h"

namespace
{
    // ---- Other programs: running?, where from, close, start, bring forward ----
    std::vector<DWORD> processesNamed (const juce::String& exe)
    {
        std::vector<DWORD> ids;
        auto snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return ids;
        PROCESSENTRY32W entry { sizeof (entry) };
        for (BOOL ok = Process32FirstW (snapshot, &entry); ok; ok = Process32NextW (snapshot, &entry))
            if (exe.equalsIgnoreCase (juce::String (entry.szExeFile)))
                ids.push_back (entry.th32ProcessID);
        CloseHandle (snapshot);
        return ids;
    }

    bool isRunning (const juce::String& exe) { return ! processesNamed (exe).empty(); }

    juce::File pathOf (const juce::String& exe)
    {
        for (auto pid : processesNamed (exe))
            if (auto process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
            {
                wchar_t buffer[MAX_PATH * 2] = {};
                DWORD size = (DWORD) std::size (buffer);
                const bool ok = QueryFullProcessImageNameW (process, 0, buffer, &size);
                CloseHandle (process);
                if (ok)
                    return juce::File (juce::String (buffer, (size_t) size));
            }
        return {};
    }

    // Asks its windows to close, then ends it if it's still there after `ms`.
    void closeProgram (const juce::String& exe, int ms)
    {
        for (auto pid : processesNamed (exe))
            EnumWindows ([] (HWND w, LPARAM p) -> BOOL
            {
                DWORD owner = 0;
                GetWindowThreadProcessId (w, &owner);
                if (owner == (DWORD) p && GetWindow (w, GW_OWNER) == nullptr)
                    PostMessageW (w, WM_CLOSE, 0, 0);
                return TRUE;
            }, (LPARAM) pid);
        for (int waited = 0; waited < ms && isRunning (exe); waited += 100)
            juce::Thread::sleep (100);
        for (auto pid : processesNamed (exe))
            if (auto process = OpenProcess (PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid))
            {
                TerminateProcess (process, 0);
                WaitForSingleObject (process, 3000);
                CloseHandle (process);
            }
    }

    // Its open main windows (visible, with a title bar). A window it has hidden (an app sitting in
    // the tray) is its own business: showing it from outside can crash it (LoupixDeck does).
    std::vector<HWND> openWindowsOf (const juce::String& exe)
    {
        struct Search { DWORD pid; std::vector<HWND> found; };
        std::vector<HWND> all;
        for (auto pid : processesNamed (exe))
        {
            Search search { pid, {} };
            EnumWindows ([] (HWND w, LPARAM p) -> BOOL
            {
                auto& s = *reinterpret_cast<Search*> (p);
                DWORD owner = 0;
                GetWindowThreadProcessId (w, &owner);
                if (owner == s.pid && GetWindow (w, GW_OWNER) == nullptr && IsWindowVisible (w) && GetWindowTextLengthW (w) > 0
                    && (GetWindowLongW (w, GWL_STYLE) & WS_CAPTION) == WS_CAPTION)
                    s.found.push_back (w);
                return TRUE;
            }, (LPARAM) &search);
            all.insert (all.end(), search.found.begin(), search.found.end());
        }
        return all;
    }

    // Brings its open window forward (restored if minimised); nothing if it has none open.
    void bringForward (const juce::String& exe)
    {
        for (auto w : openWindowsOf (exe))
        {
            if (IsIconic (w))
                ShowWindow (w, SW_RESTORE);
            SetForegroundWindow (w);
        }
    }

    juce::String readVersion (const juce::File& json, const juce::String& key)
    {
        return juce::JSON::parse (json.loadFileAsString()).getProperty (key, {}).toString();
    }

    // ---- The decks' apps and Patchbay's plugins for them ----
    juce::File home() { return juce::File::getSpecialLocation (juce::File::userHomeDirectory); }
    juce::File bundled() { return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("plugins"); }

    struct Deck
    {
        juce::String app, exe, url;
        juce::File installedExe, pluginDir, bundledDir, versionFile;
        juce::String versionKey;
        juce::String bindHelp;
    };

    Deck loupix()
    {
        Deck d;
        d.app = "LOUPIXDECK";
        d.exe = "LoupixDeck.exe";
        d.url = "https://github.com/RadiatorTwo/LoupixDeck/releases";
        d.installedExe = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getSiblingFile ("Local")
                             .getChildFile ("LoupixDeck").getChildFile ("LoupixDeck.exe");
        if (auto running = pathOf (d.exe); running.existsAsFile())
            d.installedExe = running;
        d.pluginDir = home().getChildFile (".config").getChildFile ("LoupixDeck").getChildFile ("plugins").getChildFile ("patchbay");
        d.bundledDir = bundled().getChildFile ("loupixdeck");
        d.versionFile = juce::File ("plugin.json");
        d.versionKey = "version";
        d.bindHelp = "IN LOUPIXDECK, OPEN A KEY OR DIAL'S COMMAND MENU AND PICK A PATCHBAY CHANNEL, SCENE OR MACRO.";
        return d;
    }

    Deck streamDeck()
    {
        Deck d;
        d.app = "STREAM DECK";
        d.exe = "StreamDeck.exe";
        d.url = "https://www.elgato.com/downloads";
        d.installedExe = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Elgato\\StreamDeck\\StreamDeck.exe");
        if (auto running = pathOf (d.exe); running.existsAsFile())
            d.installedExe = running;
        d.pluginDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                          .getChildFile ("Elgato\\StreamDeck\\Plugins\\com.patchbay.desk.sdPlugin");
        d.bundledDir = bundled().getChildFile ("streamdeck").getChildFile ("com.patchbay.desk.sdPlugin");
        d.versionFile = juce::File ("manifest.json");
        d.versionKey = "Version";
        d.bindHelp = "IN STREAM DECK, DRAG PATCHBAY ACTIONS ONTO KEYS, THEN PICK A CHANNEL, SCENE OR MACRO.";
        return d;
    }

    juce::String version (const Deck& d, bool installed)
    {
        const auto dir = installed ? d.pluginDir : d.bundledDir;
        return readVersion (dir.getChildFile (d.versionFile.getFileName()), d.versionKey);
    }

    // LoupixDeck: patchbay among the enabled plugins, and loaded this run (its line in the startup
    // log, which LoupixDeck starts afresh each time).
    juce::File loupixConfig() { return home().getChildFile (".config").getChildFile ("LoupixDeck"); }
    bool loupixEnabled()
    {
        for (auto& f : loupixConfig().findChildFiles (juce::File::findFiles, false, "config_*.json"))
            if (auto plugins = juce::JSON::parse (f.loadFileAsString()).getProperty ("EnabledPlugins", {}); plugins.isArray())
                if (plugins.getArray()->contains ("patchbay"))
                    return true;
        return false;
    }
    bool loupixLoaded() { return isRunning ("LoupixDeck.exe") && loupixConfig().getChildFile ("loupixdeck-startup.log").loadFileAsString().contains ("[plugin:patchbay]"); }

    bool copyPlugin (const Deck& d)
    {
        if (! d.bundledDir.isDirectory())
            return false;
        d.pluginDir.createDirectory();
        for (auto& f : d.bundledDir.findChildFiles (juce::File::findFiles, true))
        {
            auto target = d.pluginDir.getChildFile (f.getRelativePathFrom (d.bundledDir));
            target.getParentDirectory().createDirectory();
            if (! f.copyFileTo (target))
                return false;
        }
        return true;
    }
}

//==============================================================================
// A QR code of a link, dark on white with its quiet zone, as big as fits.
class ControllerHelper::QrView : public juce::Component
{
public:
    void setText (const juce::String& t)
    {
        if (t == text)
            return;
        text = t;
        code = QrCode::encode (t);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const int n = code.getSize();
        if (n == 0)
            return;
        const int cell = juce::jmax (1, juce::jmin (getWidth(), getHeight()) / (n + 8));   // whole pixels per module
        const auto area = juce::Rectangle<int> ((n + 8) * cell, (n + 8) * cell).withCentre (getLocalBounds().getCentre());
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (area.toFloat(), 4.0f);
        g.setColour (juce::Colours::black);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (code.isDark (x, y))
                    g.fillRect (area.getX() + (x + 4) * cell, area.getY() + (y + 4) * cell, cell, cell);
    }

private:
    juce::String text;
    QrCode code = QrCode::encode ({});
};

ControllerHelper::ControllerHelper (AudioEngine& e, Hooks h, int startOn) : engine (e), hooks (std::move (h))
{
    heading.setFont (Console::legendFont (Console::Type::display));
    addAndMakeVisible (heading);
    qr = std::make_unique<QrView>();
    addChildComponent (*qr);

    const char* names[] = { "LOUPEDECK (LOUPIXDECK)", "STREAM DECK", "MIDI CONTROLLER", "PHONE / TABLET (OSC)", "PHONE (WEB PAGE)" };
    for (int i = 0; i < 5; ++i)
    {
        auto* b = devices.add (new juce::TextButton (names[i]));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
        b->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
        b->onClick = [this, i] { choose ((Device) i); };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < 4; ++i)
    {
        auto* s = steps.add (new Step());
        s->title.setFont (Console::legendFont (Console::Type::body));
        s->status.setFont (juce::FontOptions (Console::Type::body));
        s->status.setColour (juce::Label::textColourId, Console::Colours::legendDim());
        s->status.setJustificationType (juce::Justification::topLeft);
        for (auto* c : std::initializer_list<juce::Component*> { &s->title, &s->status, &s->action })
            addChildComponent (c);
    }
    oscPortLabel.setFont (Console::legendFont (Console::Type::body));
    oscPortLabel.setJustificationType (juce::Justification::centredRight);   // against its field
    oscPort.setInputRestrictions (5, "0123456789");
    oscPort.setJustification (juce::Justification::centred);
    oscPort.onReturnKey = [this] { listenOnTypedPort(); oscPort.giveAwayKeyboardFocus(); refresh(); };
    addChildComponent (oscPortLabel);
    addChildComponent (oscPort);

    bindText.setFont (juce::FontOptions (Console::Type::body));
    bindText.setJustificationType (juce::Justification::topLeft);
    bindText.setColour (juce::Label::textColourId, Console::Colours::legend());
    addChildComponent (bindText);

    // Start where the hardware is: a Loupedeck on MIDI, or LoupixDeck installed, picks that.
    bool loupedeck = loupix().installedExe.existsAsFile();
    for (auto& d : juce::MidiInput::getAvailableDevices())
        loupedeck = loupedeck || d.name.containsIgnoreCase ("loupedeck");
    auto first = loupedeck ? Device::loupedeck : streamDeck().installedExe.existsAsFile() ? Device::streamDeck : Device::midi;
    if (juce::isPositiveAndBelow (startOn, devices.size()))
        first = (Device) startOn;
    devices[(int) first]->setToggleState (true, juce::dontSendNotification);
    choose (first);

    setSize (760, 560);
    startTimer (1000);
}

ControllerHelper::~ControllerHelper() = default;

void ControllerHelper::show (AudioEngine& engine, Hooks hooks, int startOn)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new ControllerHelper (engine, std::move (hooks), startOn));
    options.dialogTitle = "Set up a controller";
    options.dialogBackgroundColour = Console::Colours::panel();
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void ControllerHelper::paint (juce::Graphics& g)
{
    g.fillAll (Console::Colours::panel());
    // Each step's lamp: green done, amber needs you, dark to do.
    for (int i = 0; i < shownSteps; ++i)
    {
        auto& s = *steps[i];
        const auto r = juce::Rectangle<float> ((float) s.title.getX() - 22.0f, (float) s.title.getBounds().getCentreY() - 6.0f, 12.0f, 12.0f);
        const auto colour = s.lamp == 1 ? Console::Colours::ledGreen() : s.lamp == 2 ? Console::Colours::caution() : Console::Colours::rim();
        g.setColour (colour);
        g.fillEllipse (r);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawEllipse (r, 1.0f);
        g.setColour (Console::Colours::legendDim());
        g.setFont (Console::legendFont (Console::Type::legend));
        g.drawText (juce::String (i + 1), r.translated (-18.0f, 0.0f).withWidth (14.0f), juce::Justification::centredRight);
    }
}

void ControllerHelper::resized()
{
    using namespace Console;
    auto r = getLocalBounds().reduced (Space::pad);
    heading.setBounds (r.removeFromTop (Space::title));
    r.removeFromTop (Space::gap);

    // The devices: a column of keys, a unit apart.
    auto left = r.removeFromLeft (Space::columns (2));
    for (auto* b : devices)
    {
        b->setBounds (left.removeFromTop (Space::key));
        left.removeFromTop (Space::unit);
    }
    r.removeFromLeft (4 * Space::pad);   // room for the step numbers and lamps

    // The steps: the title with its key (2 columns) at the right, the status under the title.
    const int actionWidth = Space::columns (2);
    for (int i = 0; i < shownSteps; ++i)
    {
        auto& s = *steps[i];
        KeyRow head (r.removeFromTop (Space::key));
        head.right (s.action, 2);
        if (device == Device::osc && i == 0)   // OSC: the port beside LISTEN
            head.groupRight().right (oscPort).rightWidth (oscPortLabel, Space::column / 2);
        s.title.setBounds (head.rest());
        r.removeFromTop (Space::unit);
        s.status.setBounds (r.removeFromTop (2 * Space::key).withTrimmedRight (actionWidth + Space::pad));   // three lines
        r.removeFromTop (Space::gap);
    }
    if (device == Device::midi)
        for (auto* b : midiInputs)
        {
            b->setBounds (r.removeFromTop (Space::key).withTrimmedRight (actionWidth + Space::pad));
            r.removeFromTop (Space::unit);
        }
    if (device == Device::phone)   // the QR code, with the notes beside it
    {
        const int side = juce::jmin (220, r.getHeight());
        qr->setBounds (r.removeFromLeft (side).withHeight (side));
        r.removeFromLeft (Space::pad);
    }
    bindText.setBounds (r);
}

ControllerHelper::Step& ControllerHelper::step (int index) { return *steps[index]; }

void ControllerHelper::listenOnTypedPort()
{
    const int port = oscPort.getText().getIntValue();
    if (port < 1024 || port > 65535)
        oscPort.setText (juce::String (engine.getRemote().getOscPreferredPort()), false);
    else
    {
        engine.getRemote().setOscFromNetwork (true);   // this page is for a tablet or phone
        engine.getRemote().setOscPort (port);          // logs if the port's taken
    }
}

void ControllerHelper::setStep (int index, const juce::String& title, const juce::String& status, int lamp,
                                const juce::String& actionText, std::function<void()> action)
{
    auto& s = step (index);
    if (s.title.getText() != title)   s.title.setText (title, juce::dontSendNotification);
    if (s.status.getText() != status) s.status.setText (status, juce::dontSendNotification);
    if (s.lamp != lamp) { s.lamp = lamp; repaint(); }
    s.action.setButtonText (actionText);
    s.action.setVisible (actionText.isNotEmpty());
    s.action.setEnabled (! progress->running);
    s.action.onClick = std::move (action);
    for (auto* c : std::initializer_list<juce::Component*> { &s.title, &s.status })
        c->setVisible (true);
}

void ControllerHelper::choose (Device d)
{
    device = d;
    midiShown.clear();
    refresh();
}

void ControllerHelper::timerCallback() { refresh(); }

// After a click, not in it: refresh() can remake the key being clicked, or give it a new onClick.
void ControllerHelper::refreshSoon()
{
    juce::MessageManager::callAsync ([self = juce::Component::SafePointer<ControllerHelper> (this)] { if (self != nullptr) self->refresh(); });
}

void ControllerHelper::refresh()
{
    int count = 0;
    const auto busy = progress->get();

    if (device == Device::loupedeck || device == Device::streamDeck)
    {
        const auto deck = device == Device::loupedeck ? loupix() : streamDeck();
        const bool api = hooks.apiPort && hooks.apiPort() > 0;
        setStep (count++, "PATCHBAY CONTROL API", api ? "ON (127.0.0.1:" + juce::String (hooks.apiPort()) + ")." : "OFF: THE PLUGIN CAN'T REACH PATCHBAY.",
                 api ? 1 : 2, api ? juce::String() : "TURN ON", [this] { if (hooks.turnApiOn) hooks.turnApiOn(); refreshSoon(); });

        const bool installedApp = deck.installedExe.existsAsFile(), running = isRunning (deck.exe);
        const bool open = running && ! openWindowsOf (deck.exe).empty();
        const bool official = device == Device::loupedeck && isRunning ("Loupedeck2.exe");
        juce::String appStatus = ! installedApp ? deck.app + " ISN'T INSTALLED. GET IT FREE, THEN COME BACK."
                               : open ? deck.app + " IS RUNNING."
                               : running ? deck.app + " IS IN THE TRAY: OPEN IT FROM THERE."
                               : deck.app + " IS INSTALLED BUT NOT RUNNING.";
        if (official)
            appStatus << "\nQUIT THE OFFICIAL LOUPEDECK APP: IT HOLDS THE DEVICE.";
        setStep (count++, deck.app, appStatus, installedApp && ! official ? (running ? 1 : 0) : 2,
                 ! installedApp ? "GET " + deck.app : open ? "SHOW " + deck.app : running ? juce::String() : "START " + deck.app,
                 [this, deck, installedApp, open]
                 {
                     if (! installedApp)      juce::URL (deck.url).launchInDefaultBrowser();
                     else if (open)           bringForward (deck.exe);
                     else                     deck.installedExe.startAsProcess();
                 });

        const auto have = version (deck, true), offered = version (deck, false);
        const bool installed = have.isNotEmpty(), current = installed && (offered.isEmpty() || have == offered);
        const bool loaded = device == Device::streamDeck ? installed && running : installed && loupixLoaded() && loupixEnabled();
        juce::String pluginStatus = busy.isNotEmpty() ? busy
                                  : offered.isEmpty() && ! installed ? "THIS BUILD HAS NO PLUGIN FOR " + deck.app + "."
                                  : ! installed ? juce::String ("NOT INSTALLED.")
                                  : ! current ? "VERSION " + have + " INSTALLED; " + offered + " IS AVAILABLE."
                                  : ! loaded ? "VERSION " + have + " INSTALLED. RESTART " + deck.app + " TO LOAD IT."
                                  : "VERSION " + have + ", LOADED.";
        if (const auto result = progress->getResult(); result.isNotEmpty() && busy.isEmpty())
            pluginStatus << "\n" << result;
        setStep (count++, "PATCHBAY PLUGIN", pluginStatus, busy.isNotEmpty() ? 2 : current && loaded ? 1 : installed ? 2 : 0,
                 offered.isEmpty() || ! installedApp ? juce::String() : ! installed ? "INSTALL" : ! current ? "UPDATE" : ! loaded ? "RESTART " + deck.app : "REINSTALL",
                 [this] { install(); });

        bindText.setText (deck.bindHelp, juce::dontSendNotification);
        bindText.setVisible (true);
        midiInputs.clear();
    }
    else if (device == Device::midi)
    {
        auto& remote = engine.getRemote();
        const auto available = remote.availableInputs();
        int on = 0;
        juce::String now;
        for (auto& d : available)
        {
            now << d.identifier << (remote.isInputOn (d.identifier) ? "1" : "0");
            on += remote.isInputOn (d.identifier) ? 1 : 0;
        }
        setStep (count++, "MIDI INPUTS", available.isEmpty() ? "NO MIDI INPUTS: PLUG THE CONTROLLER IN."
                                                             : "TURN ON YOUR CONTROLLER.", on > 0 ? 1 : 2);
        bindText.setText ("THEN LINK ITS CONTROLS WITH MIDI MAP (CTRL+L).", juce::dontSendNotification);
        bindText.setVisible (true);

        if (now != midiShown)   // the inputs' keys
        {
            midiShown = now;
            midiInputs.clear();
            for (auto& d : available)
            {
                auto* b = midiInputs.add (new juce::TextButton (d.name));
                b->setClickingTogglesState (true);
                b->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
                b->setToggleState (remote.isInputOn (d.identifier), juce::dontSendNotification);
                b->onClick = [this, b, id = d.identifier] { engine.getRemote().setInputOn (id, b->getToggleState()); refreshSoon(); };
                addAndMakeVisible (b);
            }
        }
    }
    else if (device == Device::phone)
    {
        const bool api = hooks.apiPort && hooks.apiPort() > 0;
        const bool on = api && hooks.phoneOn && hooks.phoneOn();
        const auto links = hooks.phoneLinks ? hooks.phoneLinks() : juce::StringArray();
        setStep (count++, "PATCHBAY CONTROL API", api ? "ON." : "OFF: THE PAGE CAN'T REACH PATCHBAY.",
                 api ? 1 : 2, api ? juce::String() : "TURN ON", [this] { if (hooks.turnApiOn) hooks.turnApiOn(); refreshSoon(); });
        setStep (count++, "OPEN TO PHONES ON THIS NETWORK", on ? "ON: PHONES ON THIS NETWORK CAN CONNECT."
                                                               : "OFF: ONLY THIS PC CAN REACH PATCHBAY.",
                 on ? 1 : 2, ! api ? juce::String() : on ? "TURN OFF" : "TURN ON", [this, on] { if (hooks.setPhone) hooks.setPhone (! on); refreshSoon(); });
        setStep (count++, "SCAN IT WITH THE PHONE", ! on ? "TURN IT ON TO SEE THE CODE."
                                                         : links.isEmpty() ? "THIS PC ISN'T ON A NETWORK." : links.joinIntoString ("\n"),
                 on && ! links.isEmpty() ? 1 : 0, on ? "NEW KEY" : juce::String(), [this] { if (hooks.newPhoneKey) hooks.newPhoneKey(); refreshSoon(); });
        step (2).status.getProperties().set ("keepCase", true);   // the link as it must be typed
        qr->setText (on && ! links.isEmpty() ? links[0] : juce::String());
        qr->setVisible (on && ! links.isEmpty());
        bindText.setText ("SCAN THE CODE WITH THE PHONE'S CAMERA AND OPEN THE LINK. THE PHONE MUST BE ON THIS NETWORK, "
                          "NOT MOBILE DATA OR GUEST WI-FI.\n\nANYONE WITH THE LINK CAN USE THE DESK. NEW KEY MAKES A NEW LINK.\n\n"
                          "WON'T CONNECT? ALLOW PATCHBAY IN THE WINDOWS FIREWALL FOR PRIVATE NETWORKS.",
                          juce::dontSendNotification);
        bindText.setVisible (true);
        midiInputs.clear();
    }
    else
    {
        auto& remote = engine.getRemote();
        const int port = remote.getOscPort();
        const auto ip = juce::IPAddress::getLocalAddress().toString();
        if (! oscPort.hasKeyboardFocus (false))
            oscPort.setText (juce::String (port > 0 ? port : remote.getOscPreferredPort()), false);
        oscPort.setVisible (true);
        oscPortLabel.setVisible (true);
        setStep (count++, "LISTEN FOR OSC", (port > 0 ? "ON: THIS PC (" + ip + "), PORT " + juce::String (port) + "." : juce::String ("OFF."))
                 + "  TYPE A PORT (1024-65535), THEN ENTER.", port > 0 ? 1 : 2,
                 port > 0 ? "STOP" : "LISTEN", [this, port] { if (port > 0) engine.getRemote().setOscPort (0); else listenOnTypedPort(); refreshSoon(); });
        setStep (count++, "THE PHONE OR TABLET APP", "IN THE APP, SEND TO " + ip + " PORT "
                 + oscPort.getText() + ".", 0);
        // Addresses in a column, what they do beside them (names as on the strips, spaces as _).
        const std::pair<const char*, const char*> addresses[] = {
            { "/patchbay/input/MIC_FOCUS/fader -12", "FADER, IN DB" }, { "/patchbay/output/MISC/mute 1", "MUTE (0: UNMUTE)" },
            { "/patchbay/bus/SUMMING/pan -0.5", "PAN, -1 TO 1" },      { "/patchbay/input/VR/solo 1", "SOLO (0: OFF)" },
            { "/patchbay/scene GAMING", "RECALL A SCENE" },             { "/patchbay/macro/GO_LIVE 1", "PRESS A MACRO (0: RELEASE)" } };
        juce::String text ("NAMES AS ON THE STRIPS, SPACES AS _\n\n");
        for (auto& [address, meaning] : addresses)
            text << juce::String (address).paddedRight (' ', 38) << meaning << "\n";
        bindText.setText (text.trimEnd(), juce::dontSendNotification);
        bindText.setFont (juce::FontOptions (12.0f));   // (the one typeface, as everywhere)
        bindText.setVisible (true);
        midiInputs.clear();
    }

    if (device != Device::osc)
    {
        bindText.setFont (juce::FontOptions (Console::Type::body));
        oscPort.setVisible (false);
        oscPortLabel.setVisible (false);
    }
    if (device != Device::phone)
    {
        qr->setVisible (false);
        step (2).status.getProperties().remove ("keepCase");
    }
    bindText.getProperties().set ("keepCase", device == Device::osc);   // OSC addresses as typed
    for (int i = count; i < steps.size(); ++i)
        for (auto* c : std::initializer_list<juce::Component*> { &steps[i]->title, &steps[i]->status, &steps[i]->action })
            c->setVisible (false);
    if (count != shownSteps)
    {
        shownSteps = count;
        repaint();
    }
    resized();
}

// Close the deck's app, copy the plugin in, start the app again. LoupixDeck enables a new plugin on
// one start and loads it on the next, so a first install starts it twice. On a thread: the waits
// take seconds; the steps show how it's going.
void ControllerHelper::install()
{
    if (progress->running)
        return;
    const auto deck = device == Device::loupedeck ? loupix() : streamDeck();
    const bool loupixDeck = device == Device::loupedeck;
    progress->running = true;
    {
        const juce::ScopedLock sl (progress->lock);
        progress->result.clear();
    }
    refreshSoon();   // (reached from the INSTALL key's onClick: refresh() would remake that key)

    juce::Thread::launch ([p = progress, deck, loupixDeck]
    {
        auto wait = [] (std::function<bool()> done, int seconds)
        {
            for (int i = 0; i < seconds * 4 && ! done(); ++i)
                juce::Thread::sleep (250);
            return done();
        };
        juce::String result;
        if (isRunning (deck.exe))
        {
            p->set ("CLOSING " + deck.app + "...");
            closeProgram (deck.exe, 5000);
        }
        p->set ("COPYING THE PLUGIN...");
        if (! copyPlugin (deck))
            result = "COULDN'T COPY THE PLUGIN INTO " + deck.pluginDir.getFullPathName().toUpperCase() + ".";
        else if (! deck.installedExe.existsAsFile())
            result = "INSTALLED. START " + deck.app + " TO LOAD IT.";
        else
        {
            const bool wasEnabled = ! loupixDeck || loupixEnabled();
            p->set ("STARTING " + deck.app + "...");
            deck.installedExe.startAsProcess();
            if (loupixDeck && ! wasEnabled)
            {
                p->set ("STARTING " + deck.app + "...");
                wait ([] { return loupixEnabled(); }, 25);
                juce::Thread::sleep (2000);
                p->set ("STARTING " + deck.app + " AGAIN...");
                closeProgram (deck.exe, 5000);
                deck.installedExe.startAsProcess();
            }
            if (loupixDeck)
            {
                p->set ("WAITING FOR " + deck.app + " TO LOAD IT...");
                result = wait ([] { return loupixLoaded(); }, 25) ? juce::String ("DONE: LOADED.") : "STARTED, BUT THE PLUGIN DIDN'T LOAD. SEE ITS LOG.";
            }
            else
                result = "DONE: " + deck.app + " STARTED WITH THE PLUGIN.";
        }
        {
            const juce::ScopedLock sl (p->lock);
            p->result = result;
            p->text.clear();
        }
        p->running = false;
    });
}

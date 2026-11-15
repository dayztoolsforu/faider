// MainComponent's checks and helpers: PRE-FLIGHT, SETUP, ZERO DESK, the Windows defaults, HEAR WHAT THEY HEAR
// and BOOST WINDOWS AUDIO.
#include "MainComponent.h"
#include "ui/WindowsDefaults.h"
#include "../console/src/Console.h"   // (the console: the Mixer and Routing pages)
#include "../console/src/Simple.h"     // (SIMPLE and SETUP)

void MainComponent::addBoostItems (juce::PopupMenu& menu)
{
    if (settings.getBoolValue ("boostAudiodg") && windowsAudio.pid != 0 && ! AudioBoost::isBoosted (windowsAudio.priority))
        menu.addItem ("Boost Windows audio again...", ! boostingWindowsAudio, false, [this] { boostWindowsAudio(); });
}

// BOOST WINDOWS AUDIO's watch, every 5 s: audiodg read again (a process list and a look at its
// priority: cheap), and, while the boost is wanted, a notice when it's gone. Once per restart of
// Windows audio; boosting again needs the user to press BOOST (Windows asks again), never by itself.
void MainComponent::watchWindowsAudio()
{
    if (boostingWindowsAudio)
        return;   // (its answer reads it)
    windowsAudio = AudioBoost::read();
    const auto news = windowsAudioWatch.update (settings.getBoolValue ("boostAudiodg"), windowsAudio);
    if (news == AudioBoost::Watch::News::none)
        return;
    const juce::String title (news == AudioBoost::Watch::News::restarted ? "Windows audio restarted: the boost is off"
                                                                          : "Windows audio is back at Normal: the boost is off");
    logEvent ("windows audio: " + title + " (audiodg at " + AudioBoost::priorityName (windowsAudio.priority) + ")");
    boostNote = "The boost is off: BOOST WINDOWS AUDIO again";
    if (onNotify)
        onNotify (title, "To boost it again: right-click the tray icon > Boost Windows audio again... (Windows asks first)");
    toasts.show ("device", title, "FIND: BOOST WINDOWS AUDIO, or SETTINGS > AUDIO > WINDOWS AUDIO");
}

// BOOST: the helper started elevated (Windows asks first), its answer said in plain words. Done
// (High set), the boost is kept as wanted and watched.
void MainComponent::boostWindowsAudio()
{
    if (boostingWindowsAudio)
        return;
    boostingWindowsAudio = true;
    boostNote = "Waiting for Windows' approval...";
    logEvent ("windows audio: boost asked for" + juce::String (settings.getBoolValue ("boostPinCore") ? " (pinned to one core)" : ""));
    auto* peer = getPeer();
    AudioBoost::boost (settings.getBoolValue ("boostPinCore"), peer != nullptr && isShowing() ? peer->getNativeHandle() : nullptr,
                       [safe = juce::Component::SafePointer<MainComponent> (this)] (int code)
    {
        if (safe == nullptr)
            return;
        auto& self = *safe;
        self.boostingWindowsAudio = false;
        const auto text = AudioBoost::describe (code);
        const bool boosted = code == AudioBoost::ok || code == AudioBoost::notPinned;
        self.logEvent ("windows audio: " + text + " (" + juce::String (code) + ")");
        if (boosted)
        {
            self.settings.setValue ("boostAudiodg", true);
            self.settings.saveIfNeeded();
        }
        self.windowsAudio = AudioBoost::read();
        self.windowsAudioWatch.update (self.settings.getBoolValue ("boostAudiodg"), self.windowsAudio);
        self.boostNote = text.upToLastOccurrenceOf (".", false, false);
        const juce::String title (boosted ? "Windows audio boosted" : "Windows audio");
        if (self.isShowing())
            self.toasts.show ("device", title, text);
        else if (self.onNotify)
            self.onNotify (title, text);
    });
}

// No longer wanted: not watched any more. audiodg itself stays as it is until Windows audio
// restarts, or BACK TO NORMAL (the administrator again).
void MainComponent::stopBoostingWindowsAudio()
{
    settings.setValue ("boostAudiodg", false);
    settings.saveIfNeeded();
    windowsAudioWatch.update (false, windowsAudio);
    boostNote = "Not watched any more. It stays at " + AudioBoost::priorityName (windowsAudio.priority) + " until Windows audio or the PC restarts, or BACK TO NORMAL";
    logEvent ("windows audio: boost no longer wanted");
}

// BACK TO NORMAL: the helper started elevated with --unboost-audiodg (Windows asks first).
// Once it's at Normal (or not running), the boost is no longer wanted, so the watch says nothing.
void MainComponent::unboostWindowsAudio()
{
    if (boostingWindowsAudio)
        return;
    boostingWindowsAudio = true;
    boostNote = "Waiting for Windows' approval...";
    logEvent ("windows audio: back to normal asked for");
    auto* peer = getPeer();
    AudioBoost::unboost (peer != nullptr && isShowing() ? peer->getNativeHandle() : nullptr,
                         [safe = juce::Component::SafePointer<MainComponent> (this)] (int code)
    {
        if (safe == nullptr)
            return;
        auto& self = *safe;
        self.boostingWindowsAudio = false;
        const auto text = AudioBoost::describe (code, true);
        self.logEvent ("windows audio: " + text + " (" + juce::String (code) + ")");
        if (code == AudioBoost::ok || code == AudioBoost::notUnpinned || code == AudioBoost::notFound)
        {
            self.settings.setValue ("boostAudiodg", false);
            self.settings.saveIfNeeded();
        }
        self.windowsAudio = AudioBoost::read();
        self.windowsAudioWatch.update (self.settings.getBoolValue ("boostAudiodg"), self.windowsAudio);
        self.boostNote = text.upToLastOccurrenceOf (".", false, false);
        const juce::String title ("Windows audio");
        if (self.isShowing())
            self.toasts.show ("device", title, text);
        else if (self.onNotify)
            self.onNotify (title, text);
    });
}

// Whether Windows audio carries any of the mix: a Windows device or an app captured, an output
// on a Windows device, or a Windows device setting the clock. Only an ASIO interface: it doesn't.
bool MainComponent::windowsAudioUsed() const
{
    if (engine.getClock() == AudioEngine::Clock::windows)
        return true;
    for (auto& s : engine.getStrips())
        if (s->enabled && (s->source.kind == AudioEngine::Source::device || s->source.kind == AudioEngine::Source::loopback
                           || s->source.kind == AudioEngine::Source::app))
            return true;
    for (auto& m : engine.getMasters())
    {
        if (! m->enabled.load())
            continue;
        if (m->target.isNotEmpty() && m->target != AudioEngine::interfaceOutput)
            return true;
        for (auto& d : m->alsoOn)
            if (! d.toInterface && d.target.isNotEmpty() && d.target != AudioEngine::interfaceOutput)
                return true;
    }
    return false;
}

// PRE-FLIGHT's checks, as things are now (Preflight.h).
std::vector<PreflightPanel::Check> MainComponent::preflightChecks()
{
    std::vector<PreflightPanel::Check> checks;
    const bool audio = engine.getSampleRate() > 0.0;
    checks.push_back ({ "Audio", audio ? "Running at " + juce::String (engine.getSampleRate() / 1000.0, 1) + " kHz on " + engine.getClockName()
                                                      + (engine.getClockDevice().isNotEmpty() ? " (" + engine.getClockDevice() + ")" : juce::String())
                                       : "It didn't start: see DEVICES (another app may have the interface)",
                        audio, "DEVICES", [this] { tabs.setCurrentTabIndex (tabs.getTabNames().indexOf ("Devices")); } });

    const auto& allEndpoints = engine.getEndpoints();
    const bool cable = std::any_of (allEndpoints.begin(), allEndpoints.end(), [&allEndpoints] (const EndpointInfo& e)
                                    { return e.api == "WASAPI" && e.isOutput && virtualMicPartner (e, allEndpoints).isNotEmpty(); });
    checks.push_back ({ "Mic cable", cable ? "A virtual mic cable is installed (apps hear it as a mic)"
                                           : "No virtual mic cable (VB-CABLE) found: apps can't hear your mix as a mic",
                        cable, "GET VB-CABLE", [] { juce::URL ("https://vb-audio.com/Cable/").launchInDefaultBrowser(); } });

    const auto mic = micOutputName();
    int micBus = -1;
    for (int b = 0; b < (int) engine.getMasters().size(); ++b)
        if (engine.getMasters()[(size_t) b]->name == mic)
            micBus = b;
    checks.push_back ({ "Mic output", mic.isNotEmpty() ? mic + " plays into the mic cable" : "No output plays into the mic cable (CABLE Input)",
                        mic.isNotEmpty(), "ROUTING", [this] { tabs.setCurrentTabIndex (tabs.getTabNames().indexOf ("Routing")); },
                        false, "HEAR WHAT THEY HEAR", [this] { hearWhatTheyHear(); } });

    // The mics on their way there, and whether any is muted.
    juce::StringArray muted;
    std::vector<AudioEngine::Strip*> mutedStrips;
    if (micBus >= 0)
        for (auto& s : engine.getStrips())
            if (s->enabled && (s->source.kind == AudioEngine::Source::interfaceInputs || s->source.kind == AudioEngine::Source::device)
                && engine.feeds (*s, micBus) && s->mute.load())
            {
                muted.add (s->name);
                mutedStrips.push_back (s.get());
            }
    checks.push_back ({ "Mic channels", muted.isEmpty() ? "Nothing on the way to the mic is cut" : muted.joinIntoString (", ") + " cut: no one hears you",
                        muted.isEmpty(), "UNCUT", [this, names = muted]
                        {
                            for (auto& s : engine.getStrips())
                                if (names.contains (s->name))
                                    s->mute = false;
                        } });

    // Windows' default speaker or mic (or the ones for calls) on the cable: a VB-CABLE install does that.
    const auto onCable = WindowsDefaults::onCable (allEndpoints);
    checks.push_back ({ "Windows defaults", onCable.isEmpty() ? "Windows plays and records on real devices, not the cable"
                                                              : "Windows' " + onCable + ". Pick your real ones",
                        onCable.isEmpty(), "SET MY DEFAULTS", [this] { showWindowsDefaults (juce::PopupMenu::Options().withMousePosition()); } });

    auto problems = engine.findDoubling();
    for (int i = problems.size(); --i >= 0;)   // (the cable as the default speaker: said above)
        if (problems[i].startsWith ("Windows' default speaker is"))
            problems.remove (i);
    if (const auto loop = engine.findFeedbackLoop(); loop.isNotEmpty())
        problems.insert (0, "Feedback loop: " + loop);
    checks.push_back ({ "Heard once", problems.isEmpty() ? "Nothing plays twice, nothing loops back" : problems.joinIntoString (" "),
                        problems.isEmpty(), "SOUND SETTINGS", [] { juce::URL ("ms-settings:sound").launchInDefaultBrowser(); } });

    // Windows' audio engine at Normal while Windows devices or captured apps are in the mix: yellow
    // (a suggestion), red if the boost was on and a restart undid it. Only ASIO: it isn't in the way.
    if (! boostingWindowsAudio)
        windowsAudio = AudioBoost::read();
    const auto boost = AudioBoost::preflight (windowsAudioUsed(), settings.getBoolValue ("boostAudiodg"), windowsAudio.priority);
    checks.push_back ({ "Windows audio", boost.detail, boost.ok, "BOOST", [this] { boostWindowsAudio(); }, boost.advisory });
    return checks;
}

// SET MY DEFAULTS (WindowsDefaults.h): a real speaker and mic as Windows' defaults, for calls too.
void MainComponent::showWindowsDefaults (juce::PopupMenu::Options where)
{
    WindowsDefaults::showMenu (engine.getEndpoints(), where, [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::String& e)
                               { if (safe != nullptr) safe->logEvent (e); });
}

void MainComponent::showPreflight (bool onlyIfWrong)
{
    auto panel = std::make_unique<PreflightPanel> ([this] { return preflightChecks(); });
    if (onlyIfWrong && panel->allClear())
        return;
    panel->setOtherKey ("ZERO THE DESK", "Things left set: solos, key listen, gains far off, outputs cut", [this] { showZero(); });
    logEvent (onlyIfWrong ? "pre-flight: something needs a look" : "pre-flight");
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (panel.release());
    o.dialogTitle = "Pre-flight";
    o.dialogBackgroundColour = Console::Colours::panel();
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = this;
    o.launchAsync();
}

// SETUP: its own window, not a page. It asks its questions and adds what's missing; the desk goes on
// running behind it, and a window closed without pressing BUILD leaves the mix as it was.
void MainComponent::showSetup ()
{
    if (setupWindow != nullptr)
    {
        setupWindow->toFront (true);
        return;
    }

    auto panel = Simple::makeSetup (engine, [this]
    {
        settings.setValue ("deskMode", "simple");
        settings.saveIfNeeded();
        ConsoleDesk::setSimple (*consoleDesk, true);
        tabs.setCurrentTabIndex (0);
        closeSetup();
    });

    panel->setSize (940, 620);   // (as a page it was sized by the tabs; a window asks its content)
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (panel.release());
    o.dialogTitle = "Set up the mix";
    o.dialogBackgroundColour = Console::Colours::panel();
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    setupWindow = o.launchAsync();
}

// Dismissed a moment later than it is asked: the click that ends SETUP is still on the stack, and the
// window's going takes the page it holds with it.
void MainComponent::closeSetup ()
{
    if (setupWindow != nullptr)
        juce::Timer::callAfterDelay (50, [w = juce::Component::SafePointer<juce::DialogWindow> (setupWindow.getComponent())]
                                    { if (w != nullptr) w->closeButtonPressed(); });
}

// ZERO DESK: as engineers zero a console before a session, the things a last session (or a
// moment ago) can leave set that change what's heard, each with its fix.
std::vector<PreflightPanel::Check> MainComponent::zeroChecks()
{
    std::vector<PreflightPanel::Check> checks;
    auto namesWhere = [this] (std::function<bool (AudioEngine::Strip&)> test)
    {
        juce::StringArray names;
        for (auto& s : engine.getStrips())
            if (test (*s))
                names.add (s->name);
        return names;
    };
    auto row = [&checks] (const juce::String& title, const juce::StringArray& found, const juce::String& foundText, const juce::String& clearText, std::function<void()> fix)
    {
        checks.push_back ({ title, found.isEmpty() ? clearText : found.joinIntoString (", ") + ": " + foundText, found.isEmpty(), "FIX", std::move (fix) });
    };

    row ("Solos", namesWhere ([] (AudioEngine::Strip& s) { return s.solo.load(); }), "soloed: the others are silent", "Nothing soloed",
         [this] { for (auto& s : engine.getStrips()) s->solo = false; });
    row ("Key listen", namesWhere ([] (AudioEngine::Strip& s) { return s.channel != nullptr && s.channel->params.getRawParameterValue ("keyListen")->load() > 0.5f; }),
         "playing its sidechain key instead of itself", "No channel plays its key",
         [this] { for (auto& s : engine.getStrips()) if (s->channel != nullptr) if (auto* p = s->channel->params.getParameter ("keyListen")) p->setValueNotifyingHost (0.0f); });
    row ("Gain trims", namesWhere ([] (AudioEngine::Strip& s) { return s.channel != nullptr && std::abs (s.channel->params.getRawParameterValue ("trim")->load()) > 12.0f; }),
         "more than 12 dB off (left there?)", "Every channel's gain near 0",
         [this] { for (auto& s : engine.getStrips()) if (s->channel != nullptr && std::abs (s->channel->params.getRawParameterValue ("trim")->load()) > 12.0f) if (auto* p = s->channel->params.getParameter ("trim")) p->setValueNotifyingHost (p->convertTo0to1 (0.0f)); });
    juce::StringArray cutMasters;
    for (auto& m : engine.getMasters())
        if (m->mute.load())
            cutMasters.add (m->name);
    row ("Outputs", cutMasters, "cut: nothing leaves them", "No output cut", [this] { for (auto& m : engine.getMasters()) m->mute = false; });
    return checks;
}

void MainComponent::showZero()
{
    auto panel = std::make_unique<PreflightPanel> ([this] { return zeroChecks(); }, "ZERO DESK");
    logEvent ("zero desk");
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (panel.release());
    o.dialogTitle = "Zero desk";
    o.dialogBackgroundColour = Console::Colours::panel();
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = this;
    o.launchAsync();
}

juce::String MainComponent::micOutputName() const
{
    for (auto& b : engine.getMasters())   // the output playing into VB-CABLE: what apps hear as the mic
        if (b->target.containsIgnoreCase ("CABLE Input"))
            return b->name;
    return {};
}

void MainComponent::hearWhatTheyHear()
{
    const auto name = micOutputName();
    if (name.isEmpty() || hearingBack || engine.isPreviewPlaying())
        return;
    hearingBack = true;   // (pressed again while these 5 s run: once)
    logEvent ("hear what they hear: speak now (5 s), then listen on the headphones");
    if (onNotify)
        onNotify ("Speak now", "Talk for 5 s, then hear it back.");
    juce::Timer::callAfterDelay (5200, [safe = juce::Component::SafePointer<MainComponent> (this), name]
    {
        if (safe == nullptr)
            return;
        safe->hearingBack = false;
        auto clip = safe->engine.retroTail ("output", name, 5.0);
        if (clip.getNumSamples() == 0)
            return safe->logEvent ("hear what they hear: nothing came through " + name);
        safe->logEvent ("hear what they hear: playing " + name + " back");
        safe->engine.playPreview (std::move (clip));
    });
}

// AudioEngine's devices: the master (the clock) opened, switched and re-prepared, the other devices
// opened and closed as they come and go (updateEndpoints), and the programs captured (refreshApps).
#include "AudioEngine.h"
#include "AudioEngineInternal.h"
#include "BuiltInClock.h"
#include "WindowDiscovery.h"

//==============================================================================
bool AudioEngine::openInterface (const juce::String& nameContains, int preferredBufferSize)
{
    master = nullptr;
    clock = Clock::none;
    clockDevice.clear();
    juce::String name;

    if (nameContains == builtInClock)
    {
        master = std::make_unique<BuiltInClock>();
        name = BuiltInClock::deviceName;
    }
    else if (nameContains.startsWith (windowsClockPrefix))
    {
        // A Windows playback device, playback only (its microphone, like any other, is a device
        // of its own). Shared, so everything else on the PC keeps playing through it too.
        name = nameContains.fromFirstOccurrenceOf (windowsClockPrefix, false, false);
        if (wasapiType == nullptr)
            return false;
        wasapiType->scanForDevices();
        if (! wasapiType->getDeviceNames (false).contains (name))
        {
            log ("engine: the playback device " + name.quoted() + " isn't connected");
            return false;
        }
        master.reset (wasapiType->createDevice (name, {}));
    }
    else
    {
        if (asioType == nullptr)
        {
            log ("engine: built without ASIO support");
            return false;
        }

        asioType->scanForDevices();

        for (auto& n : asioType->getDeviceNames())
            if (n.containsIgnoreCase (nameContains))
                name = n;

        if (name.isEmpty())
        {
            log ("engine: no ASIO driver matching \"" + nameContains + "\"");
            return false;
        }

        master.reset (asioType->createDevice (name, name));
    }

    if (master == nullptr)
    {
        log ("engine: couldn't create the device " + name);
        return false;
    }

    juce::BigInteger ins, outs;
    ins.setRange (0, master->getInputChannelNames().size(), true);
    outs.setRange (0, master->getOutputChannelNames().size(), true);

    const auto rates = master->getAvailableSampleRates();
    const double rate = rates.contains (48000.0) ? 48000.0 : (rates.isEmpty() ? 0.0 : rates[0]);

    const int size = master->getAvailableBufferSizes().contains (preferredBufferSize) ? preferredBufferSize : master->getDefaultBufferSize();
    const auto error = master->open (ins, outs, rate, size);

    if (error.isNotEmpty())
    {
        log ("engine: " + name + " failed to open: " + error);
        master = nullptr;
        return false;
    }

    clock = nameContains == builtInClock ? Clock::builtIn : nameContains.startsWith (windowsClockPrefix) ? Clock::windows : Clock::asio;
    if (clock == Clock::windows)
        clockDevice = name;
    return true;
}

bool AudioEngine::startInterface (const juce::String& nameContains, int preferredBufferSize)
{
    if (! openInterface (nameContains, preferredBufferSize))
        return false;

    ownStart = true;
    master->start (this);
    ownStart = false;
    preparedRate = interfaceRate;   // (everything made from here on is made for it)
    log ("engine: clock " + getClockName() + ", " + master->getName() + " @ " + juce::String (interfaceRate) + " Hz, "
         + juce::String (interfaceBlock) + " samples, latency in/out "
         + juce::String (master->getInputLatencyInSamples()) + "/" + juce::String (master->getOutputLatencyInSamples()));
    return true;
}

bool AudioEngine::switchInterface (const juce::String& name, int preferredBufferSize)
{
    const auto before = master != nullptr ? master->getName() : juce::String ("none");
    if (master != nullptr)
    {
        master->stop();
        master->close();
    }

    if (! openInterface (name, preferredBufferSize))
        return false;

    // Everything made for the old one's rate and block made again (the new one isn't running yet).
    interfaceRate = master->getCurrentSampleRate();
    interfaceBlock = master->getCurrentBufferSizeSamples();
    reprepareAll();

    ownStart = true;
    master->start (this);
    ownStart = false;
    log ("engine: clock " + before + " -> " + getClockName() + ", " + master->getName() + " @ " + juce::String (interfaceRate)
         + " Hz, " + juce::String (interfaceBlock) + " samples");

    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        for (auto& bus : masters)   // (updateEndpoints puts each where it plays now)
        {
            bus->output = nullptr;
            bus->toInterface = false;
            for (auto& d : bus->alsoOn)
            {
                d.output = nullptr;
                d.toInterface = false;
            }
        }
    }
    for (auto& bus : masters)   // (the names are the message thread's own: let go outside the lock)
    {
        bus->target.clear();
        for (auto& d : bus->alsoOn)
            d.target.clear();
    }
    updateEndpoints (lastEndpoints);
    refreshLatency();
    return true;
}

juce::String AudioEngine::getClockName() const
{
    switch (clock)
    {
        case Clock::asio:    return "ASIO";
        case Clock::windows: return "Windows";
        case Clock::builtIn: return "Built-in clock";
        case Clock::none:    break;
    }
    return "None";
}

bool AudioEngine::isInterfaceEndpoint (const EndpointInfo& e) const
{
    if (e.api != "WASAPI")
        return false;
    if (clock == Clock::asio)
        return interfaceAdapter.isNotEmpty() && e.adapter == interfaceAdapter;
    if (clock == Clock::windows)
        return e.isOutput && e.name == clockDevice;
    return false;
}

juce::Array<int> AudioEngine::getInterfaceBufferSizes() const
{
    return master != nullptr ? master->getAvailableBufferSizes() : juce::Array<int>();
}

// Reopens the master at a new buffer size. Everything sized by the master's block is
// re-prepared while the audio is stopped: plugins, the Windows devices' drift buffers (each
// device paused briefly), and app captures (reopened).
bool AudioEngine::setInterfaceBufferSize (int size)
{
    if (master == nullptr || size == interfaceBlock || ! master->getAvailableBufferSizes().contains (size) || size > maxBlock)
        return false;

    const auto ins = master->getActiveInputChannels(), outs = master->getActiveOutputChannels();
    const double rate = master->getCurrentSampleRate();
    const int previous = interfaceBlock;

    master->stop();
    master->close();

    if (const auto error = master->open (ins, outs, rate, size); error.isNotEmpty())
    {
        log ("engine: couldn't use a " + juce::String (size) + "-sample buffer (" + error + "), back to " + juce::String (previous));
        if (const auto again = master->open (ins, outs, rate, previous); again.isNotEmpty())
        {
            log ("engine: the interface " + master->getName() + " wouldn't reopen (" + again + ") - no sound until Faider restarts");
            interfaceDown = true;
            return false;
        }
    }

    interfaceRate = master->getCurrentSampleRate();
    interfaceBlock = master->getCurrentBufferSizeSamples();
    reprepareAll();

    ownStart = true;
    master->start (this);
    ownStart = false;
    refreshApps();
    refreshLatency();

    log ("engine: buffer " + juce::String (previous) + " -> " + juce::String (interfaceBlock) + " samples ("
         + juce::String (1000.0 * interfaceBlock / interfaceRate, 1) + " ms), latency in/out "
         + juce::String (master->getInputLatencyInSamples()) + "/" + juce::String (master->getOutputLatencyInSamples()));
    return interfaceBlock == size;
}

// Everything sized by the master's rate and block prepared again, with the master stopped:
// plugins, channels, the Windows devices' drift buffers (each paused briefly), and app captures
// (dropped; refreshApps reopens them).
void AudioEngine::reprepareAll()
{
    // A new rate: what's tuned to it too. Loudness filters, headphone correction's filters, and
    // the retro buffers (their length is in seconds) are made again for it.
    if (interfaceRate != preparedRate)
    {
        preparedRate = interfaceRate;
        for (int i = 0; i < (int) masters.size(); ++i)
        {
            auto& bus = *masters[(size_t) i];
            bus.loudness.prepare (interfaceRate);
            if (bus.retro.capacity() != retroFrames())
                bus.retro.prepare (retroFrames());
            if (bus.headphoneProfile.isNotEmpty())
                setHeadphoneEq (i, bus.headphoneProfile);
        }

        // A file is written at one rate: a recording running at the old one is finished here.
        // The replay's memory is let go; the TAPE tab re-arms it at the new rate on its next
        // tick (keepReplayArmed), so a save never mixes the two rates.
        if (recorder.isRecording())
        {
            stopRecording();
            log ("engine: the recording stopped - the rate changed to " + juce::String (interfaceRate) + " Hz");
        }
        if (recorder.replayArmed())
            recorder.armReplay (Recorder::Kind::output, -1, 0.0, interfaceRate, graphLock);
    }

    auto prepareChain = [this] (std::vector<std::unique_ptr<juce::AudioPluginInstance>>& chain)
    {
        for (auto& plugin : chain)
        {
            plugin->releaseResources();
            plugin->prepareToPlay (interfaceRate, interfaceBlock);
        }
    };
    for (auto& s : strips) { prepareChain (s->inserts); s->channel->prepareToPlay (interfaceRate, interfaceBlock); }
    for (auto& p : buses)
    {
        prepareChain (p->inserts);
        p->channel->prepareToPlay (interfaceRate, interfaceBlock);
        for (auto& path : p->paths)
            prepareChain (path->inserts);
    }
    for (auto& b : masters)
        if (b->masterChain != nullptr)
            b->masterChain->prepareToPlay (interfaceRate, interfaceBlock);

    for (auto& [key, sec] : secondaries)
    {
        if (sec->device == nullptr)
            continue;
        sec->device->stop();
        const double deviceRate = sec->device->getCurrentSampleRate();
        const int deviceBlock = sec->device->getCurrentBufferSizeSamples();
        // (all its channels, as openSecondary prepared it: a multichannel device keeps its higher buses)
        if (sec->isOutput) sec->fifo.prepare (sec->channels, interfaceRate, deviceRate, interfaceBlock, deviceBlock);
        else               sec->fifo.prepare (sec->channels, deviceRate, interfaceRate, deviceBlock, interfaceBlock);
        sec->device->start (sec.get());
    }

    for (auto& s : strips)
        s->app = nullptr;
    appCaptures.clear();   // the audio is stopped, so nothing is using them
    failedApps.clear();
    quietApps.clear();
}

juce::StringArray AudioEngine::getInterfaceInputNames() const
{
    return master != nullptr ? master->getInputChannelNames() : juce::StringArray();
}

juce::StringArray AudioEngine::getInterfaceOutputNames() const
{
    return master != nullptr ? master->getOutputChannelNames() : juce::StringArray();
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // JUCE's ASIO driver restarts itself (a reset request) even when its reopen failed: then it
    // plays nothing, and the screen says so (isInterfaceDown) until a buffer change reopens it.
    interfaceDown = ! device->isOpen();
    if (interfaceDown)
    {
        log ("engine: the interface " + device->getName() + " restarted itself but wouldn't reopen ("
             + device->getLastError() + ") - no sound until its buffer is changed or Faider restarts");
        return;
    }

    const double rate = device->getCurrentSampleRate();
    const int block = device->getCurrentBufferSizeSamples();
    const bool changedByDriver = ! ownStart && master != nullptr && (rate != interfaceRate || block != interfaceBlock);
    interfaceRate = rate;
    interfaceBlock = block;

    if (! changedByDriver)
        return;

    // The interface restarted itself at another rate or buffer (changed in its own control
    // panel): everything prepared for the old one is prepared again before a block at the new
    // size reaches it. JUCE's ASIO reset runs from a Timer, so this is the message thread with
    // our callback not yet attached: prepared here, as setInterfaceBufferSize does with the master
    // stopped. Called from anywhere else, blocks go out silent (holdAudio) until the message
    // thread has stopped the master and done it.
    if (juce::MessageManager::existsAndIsCurrentThread())
        reprepareAll();
    else
        holdAudio = true;

    juce::MessageManager::callAsync ([this, guard = std::weak_ptr<bool> (alive)]
    {
        if (guard.lock() == nullptr || master == nullptr)
            return;
        if (holdAudio)
        {
            master->stop();
            reprepareAll();
            holdAudio = false;
            ownStart = true;
            master->start (this);
            ownStart = false;
        }
        refreshApps();      // (app captures were dropped)
        refreshLatency();   // (plugins may report another latency at the new size)
        log ("engine: the interface changed to " + juce::String (interfaceRate) + " Hz, " + juce::String (interfaceBlock)
             + " samples - everything was prepared again");
    });
}

// Picking an output as a source means "listen to it", so switch its loopback on. The output's
// LOOPBACK button can still turn it off, which silences every strip fed from it.
void AudioEngine::enableLoopbackFor (const Source& source)
{
    if (source.kind == Source::loopback && juce::isPositiveAndBelow (source.masterIndex, (int) masters.size()))
        masters[(size_t) source.masterIndex]->loopback = true;
}

void AudioEngine::setDeviceHidden (const juce::String& deviceName, bool hidden)
{
    if (hidden)
        hiddenDevices.addIfNotAlreadyThere (deviceName);
    else
        hiddenDevices.removeString (deviceName);
}

void AudioEngine::setDeviceDisabled (const juce::StringArray& deviceNames, bool disabled, bool sayIt)
{
    for (auto& name : deviceNames)
    {
        if (disabled)
            disabledDevices.addIfNotAlreadyThere (name);
        else
            disabledDevices.removeString (name);
        if (sayIt)
            log ("device " + name + (disabled ? ": off in Faider" : ": on in Faider"));
    }
    refreshDevices();   // closes the streams of devices now off, reopens ones back on
}

//==============================================================================
AudioEngine::Secondary* AudioEngine::openSecondary (const juce::String& deviceName, bool isOutput)
{
    const auto key = deviceName + (isOutput ? "|out" : "|in");

    if (auto it = secondaries.find (key); it != secondaries.end())
        return it->second.get();

    if (wasapiType == nullptr || master == nullptr)
        return nullptr;

    wasapiType->scanForDevices();

    auto s = std::make_unique<Secondary>();
    s->name = deviceName;
    s->isOutput = isOutput;
    s->device.reset (isOutput ? wasapiType->createDevice (deviceName, {}) : wasapiType->createDevice ({}, deviceName));

    if (s->device == nullptr)
    {
        log ("engine: couldn't create WASAPI device " + deviceName);
        return nullptr;
    }

    // Every channel it has, so outputs and inputs can each use their own pair of a multichannel
    // device (a 16-channel cable is 8 stereo buses). Windows may list more than it will open
    // (a cable it has set to 2 channels in Sound settings): then stereo, as before.
    const int all = juce::jlimit (1, pluginMaxChannels * 2, isOutput ? s->device->getOutputChannelNames().size()
                                                                     : s->device->getInputChannelNames().size());
    const auto rates = s->device->getAvailableSampleRates();
    const double rate = rates.contains (interfaceRate) ? interfaceRate : (rates.isEmpty() ? 0.0 : rates[0]);
    auto openWith = [&] (int count)
    {
        juce::BigInteger channels;
        channels.setRange (0, count, true);
        return isOutput ? s->device->open ({}, channels, rate, s->device->getDefaultBufferSize())
                        : s->device->open (channels, {}, rate, s->device->getDefaultBufferSize());
    };

    auto error = openWith (all);
    if (error.isNotEmpty() && all > 2)
    {
        s->device->close();
        error = openWith (2);
        if (error.isEmpty())
            log ("engine: " + deviceName + " lists " + juce::String (all) + " channels but opens with 2 (set it to "
                 + juce::String (all) + " in Windows Sound settings, or the cable's control panel, for the rest)");
    }

    if (error.isNotEmpty())
    {
        // Another way into the same device already open here (VB-CABLE's "CABLE Input" and
        // "CABLE In 16ch" are one cable: it plays from only one at a time): say so.
        juce::String adapter, sibling;
        for (auto& e : lastEndpoints)
            if (e.name == deviceName)
                adapter = e.adapter;
        for (auto& [secondaryKey, open] : secondaries)
            for (auto& e : lastEndpoints)
                if (adapter.isNotEmpty() && e.name == open->name && e.adapter == adapter && open->isOutput == isOutput && open->name != deviceName)
                    sibling = open->name;
        log ("engine: " + deviceName + " failed to open: " + error
             + (sibling.isNotEmpty() ? " (" + sibling + " is open, and it's the same device: it plays from only one of them at a time)" : juce::String()));
        return nullptr;
    }

    const double deviceRate = s->device->getCurrentSampleRate();
    const int deviceBlock = s->device->getCurrentBufferSizeSamples();
    s->channels = juce::jmax (1, (isOutput ? s->device->getActiveOutputChannels() : s->device->getActiveInputChannels()).countNumberOfSetBits());
    s->mix.setSize (s->channels, maxBlock);
    s->inBlock.setSize (s->channels, maxBlock);
    s->inBlock.clear();

    if (isOutput)
        s->fifo.prepare (s->channels, interfaceRate, deviceRate, interfaceBlock, deviceBlock);
    else
        s->fifo.prepare (s->channels, deviceRate, interfaceRate, deviceBlock, interfaceBlock);

    s->device->start (s.get());

    log ("engine: opened " + deviceName + (isOutput ? " (out) @ " : " (in) @ ") + juce::String (deviceRate)
         + " Hz, " + juce::String (deviceBlock) + " samples, " + juce::String (s->channels) + " ch");

    auto* raw = s.get();
    secondaries[key] = std::move (s);
    return raw;
}

void AudioEngine::updateEndpoints (const juce::Array<EndpointInfo>& endpoints)
{
    lastEndpoints = endpoints;

    if (master == nullptr)
        return;

    interfaceAdapter = clock == Clock::windows ? clockDevice : juce::String();
    if (clock == Clock::asio)
        for (auto& e : endpoints)
            if (e.api == "WASAPI" && asioMatchesAdapter (master->getName(), e.adapter))
                interfaceAdapter = e.adapter;

    auto findDefault = [&] (bool isOutput) -> const EndpointInfo*
    {
        for (auto& e : endpoints)
            if (e.api == "WASAPI" && e.isOutput == isOutput && e.isActive() && e.isDefault && ! isDeviceDisabled (e.name))
                return &e;
        return nullptr;
    };

    auto findActive = [&] (const juce::String& nameContains, bool isOutput) -> juce::String
    {
        if (nameContains == windowsDefault)
        {
            auto* e = findDefault (isOutput);
            return e != nullptr ? e->name : juce::String();
        }

        for (auto& e : endpoints)
            if (e.api == "WASAPI" && e.isOutput == isOutput && e.isActive() && e.name.containsIgnoreCase (nameContains) && ! isDeviceDisabled (e.name))
                return e.name;
        return {};
    };

    // Where an output's device is now: the device's name and stream, interfaceOutput, or nothing.
    auto resolve = [&] (const juce::StringArray& candidates, bool enabled) -> std::pair<juce::String, Secondary*>
    {
        for (auto& candidate : candidates)
        {
            if (! enabled)
                break;   // disabled: no device (closeUnused releases it)

            // The Windows default may be the master's own hardware: play it through the master.
            auto* def = candidate == windowsDefault ? findDefault (true) : nullptr;

            if (candidate == interfaceOutput || (def != nullptr && isInterfaceEndpoint (*def)))
                return { interfaceOutput, nullptr };

            const auto deviceName = findActive (candidate, true);

            if (clock == Clock::windows && deviceName == clockDevice)   // (the device clocking it, named)
                return { interfaceOutput, nullptr };

            if (deviceName.isNotEmpty())
                if (auto* s = openSecondary (deviceName, true))
                    return { deviceName, s };
        }
        return {};
    };

    for (auto& bus : masters)
    {
        const auto [newTarget, newOutput] = resolve (bus->candidates, bus->enabled);
        const bool newToInterface = (newTarget == interfaceOutput);

        // Its other devices. One that turns out to be a device it already plays on (the Windows
        // default moving onto it, say) stays quiet rather than playing there twice.
        // The same goes for the master interface's channels (its own device, and the Windows
        // default when that's the master's hardware, both landing on the same pair).
        std::vector<Secondary*> playing { newOutput };
        std::vector<int> onInterface;
        if (newToInterface)
            onInterface.push_back (bus->firstChannel);
        for (auto& d : bus->alsoOn)
        {
            auto [dTarget, dOutput] = resolve ({ d.candidate }, bus->enabled);
            if (dOutput != nullptr && std::find (playing.begin(), playing.end(), dOutput) != playing.end())
                dOutput = nullptr;
            playing.push_back (dOutput);
            bool dToInterface = dTarget == interfaceOutput;
            if (dToInterface)
            {
                if (std::find (onInterface.begin(), onInterface.end(), d.firstChannel) != onInterface.end())
                    dToInterface = false;   // already playing on those channels
                else
                    onInterface.push_back (d.firstChannel);
            }

            if (dOutput != d.output || dToInterface != d.toInterface)
            {
                const juce::SpinLock::ScopedLockType sl (graphLock);
                d.output = dOutput;
                d.toInterface = dToInterface;
            }

            if (dTarget != d.target)
            {
                log ("master " + bus->name + ": also " + (d.target.isEmpty() ? "none" : d.target)
                     + " -> " + (dTarget.isEmpty() ? "none" : dTarget));
                d.target = dTarget;
            }
        }

        if (newOutput != bus->output || newToInterface != bus->toInterface)
        {
            const juce::SpinLock::ScopedLockType sl (graphLock);
            bus->output = newOutput;
            bus->toInterface = newToInterface;
        }

        if (newTarget != bus->target)
        {
            log ("master " + bus->name + ": " + (bus->target.isEmpty() ? "none" : bus->target)
                 + " -> " + (newTarget.isEmpty() ? "none" : newTarget));
            bus->target = newTarget;
        }
    }

    for (auto& strip : strips)
    {
        if (strip->source.kind != Source::device)
            continue;

        const auto deviceName = strip->enabled ? findActive (strip->source.deviceName, false) : juce::String();   // off: released
        auto* input = deviceName.isNotEmpty() ? openSecondary (deviceName, false) : nullptr;

        if (input != strip->input)
        {
            const juce::SpinLock::ScopedLockType sl (graphLock);
            strip->input = input;
        }
    }

    closeUnused();
    refreshApps();
}

void AudioEngine::refreshApps()
{
    if (master == nullptr)
        return;

    juce::Array<AudioApp> playingNow;   // (looked up once, and only for a program that was silent)
    bool playingKnown = false;

    for (auto& strip : strips)
    {
        if (strip->source.kind != Source::app)
        {
            if (strip->app != nullptr)   // no longer an app: lets go (its capture may be dropped below)
            {
                const juce::SpinLock::ScopedLockType sl (graphLock);
                strip->app = nullptr;
            }
            continue;
        }

        if (! strip->enabled)   // off: lets go of its capture (dropped below if nothing else uses it)
        {
            if (strip->app != nullptr)
            {
                const juce::SpinLock::ScopedLockType sl (graphLock);
                strip->app = nullptr;
            }
            continue;
        }

        const auto key = strip->source.deviceName.toLowerCase();
        auto it = appCaptures.find (key);

        // A program that wasn't playing when it was looked up, playing now: its capture has to be
        // on the copy that plays. One on the copy first found hears it only if that copy started
        // the one playing; one Windows refused while silent is tried again.
        bool lookAgain = false;
        if (quietApps.count (key) > 0)
        {
            if (! std::exchange (playingKnown, true))
                playingNow = listAudioApps();
            const auto captured = it != appCaptures.end() ? it->second->getProcessId() : 0u;
            bool plays = false, heard = false;
            for (auto& a : playingNow)
                if (a.exe.equalsIgnoreCase (strip->source.deviceName) && ! WindowDiscovery::isIsolatedProcess (a.processId))
                {
                    plays = true;
                    heard = heard || (captured != 0 && processIsWithin (a.processId, captured));
                }
            if (plays)
            {
                quietApps.erase (key);
                lookAgain = ! heard;
                if (lookAgain)
                    failedApps.erase (key);
            }
        }

        // Already capturing a process that's still running: skip the lookup (a scan of every
        // process and audio session, ~10 ms on the message thread).
        if (! lookAgain && it != appCaptures.end() && processIsRunning (it->second->getProcessId()))
        {
            if (strip->app != it->second.get())
            {
                const juce::SpinLock::ScopedLockType sl (graphLock);
                strip->app = it->second.get();
            }
            continue;
        }

        bool playing = true;   // (an isolated tab's browser is its own: there's no other copy to look for)
        const auto pid = WindowDiscovery::isIsolated (strip->source.deviceName) ? WindowDiscovery::findIsolatedProcess (strip->source.deviceName)
                                                                               : findAppProcess (strip->source.deviceName, &playing);

        // Running with the process we're already capturing: nothing to do.
        if (it != appCaptures.end() && pid != 0 && it->second->getProcessId() == pid)
        {
            if (strip->app != it->second.get())
            {
                const juce::SpinLock::ScopedLockType sl (graphLock);
                strip->app = it->second.get();
            }
            continue;
        }

        AppCapture* capture = nullptr;

        if (pid != 0 && failedApps[key] != pid)
        {
            juce::String error;
            auto opened = AppCapture::open (pid, interfaceRate, interfaceBlock, error);
            if (playing) quietApps.erase (key);
            else         quietApps.insert (key);

            if (opened != nullptr)
            {
                log ("engine: capturing " + strip->source.deviceName + " (process " + juce::String (pid) + (playing ? ")" : ", not playing yet)"));
                capture = opened.get();

                std::vector<Strip*> takers;   // (found here: no names are compared under the lock)
                for (auto& s : strips)
                    if (s->source.kind == Source::app && s->enabled && s->source.deviceName.toLowerCase() == key)
                        takers.push_back (s.get());
                {
                    const juce::SpinLock::ScopedLockType sl (graphLock);
                    for (auto* s : takers)
                        s->app = capture;
                }

                appCaptures[key] = std::move (opened);   // replaces (and stops) any old capture
                continue;
            }

            log ("engine: " + strip->source.deviceName + ": " + error);
            failedApps[key] = pid;   // don't retry every second for the same process
        }

        if (strip->app != capture)
        {
            const juce::SpinLock::ScopedLockType sl (graphLock);
            strip->app = capture;
        }
    }

    // Drop captures for apps that closed or that no strip uses any more.
    for (auto it = appCaptures.begin(); it != appCaptures.end();)
    {
        const bool used = std::any_of (strips.begin(), strips.end(), [&it] (auto& s) { return s->app == it->second.get(); });

        if (used)
        {
            ++it;
            continue;
        }

        log ("engine: stopped capturing " + it->first);
        it = appCaptures.erase (it);
    }
}

void AudioEngine::closeUnused()
{
    for (auto it = secondaries.begin(); it != secondaries.end();)
    {
        auto* s = it->second.get();

        const bool used = s->isOutput
            ? std::any_of (masters.begin(), masters.end(), [s] (auto& b)
                           { return b->output == s || std::any_of (b->alsoOn.begin(), b->alsoOn.end(), [s] (auto& d) { return d.output == s; }); })
            : std::any_of (strips.begin(), strips.end(), [s] (auto& st) { return st->input == s; });

        if (used)
        {
            ++it;
            continue;
        }

        log ("engine: closed " + s->name + " (underruns " + juce::String (s->fifo.getUnderruns())
             + ", overruns " + juce::String (s->fifo.getOverruns()) + ")");
        it = secondaries.erase (it);   // the audio thread no longer references it
    }
}

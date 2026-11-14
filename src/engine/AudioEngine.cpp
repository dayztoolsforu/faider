#include "AudioEngine.h"
#include "AudioEngineInternal.h"
#include "VoiceChain.h"
#include "WindowDiscovery.h"

void AudioEngine::setHeadphoneEq (int index, const juce::String& profile)
{
    if (! juce::isPositiveAndBelow (index, (int) masters.size()))
        return;
    auto& bus = *masters[(size_t) index];
    const auto* p = HeadphoneEq::find (profile);
    bus.headphoneProfile = p != nullptr ? profile : juce::String();
    auto next = p != nullptr ? std::make_shared<HeadphoneEq::Processor> (*p, juce::jmax (8000.0, interfaceRate)) : nullptr;
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        std::swap (bus.headphoneEq, next);
    }
    log ("master " + bus.name + ": headphone correction " + (p != nullptr ? "for " + profile : juce::String ("off")));
}

juce::AudioBuffer<float> AudioEngine::retroTail (const juce::String& kind, const juce::String& name, double seconds) const
{
    const int frames = (int) (seconds * interfaceRate);
    if (kind == "output")
        for (auto& b : masters)
            if (b->name == name)
                return b->retro.newest (frames);
    return juce::AudioBuffer<float> (2, 0);
}

void AudioEngine::playPreview (juce::AudioBuffer<float> clip)
{
    auto next = std::make_shared<juce::AudioBuffer<float>> (std::move (clip));
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        std::swap (preview, next);
        previewPos = 0;
        previewPlaying = preview->getNumSamples() > 0;
    }
    // (the clip played before goes here, off the audio thread)
}

bool AudioEngine::startAudition (Strip& s)
{
    if (s.channel == nullptr || s.committed != nullptr)
        return false;
    auto copy = std::make_shared<VoiceChain>();
    juce::MemoryBlock state;
    s.channel->getStateInformation (state);
    copy->setStateInformation (state.getData(), (int) state.getSize());
    copy->prepareToPlay (interfaceRate, interfaceBlock);
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        s.committed = std::move (copy);
    }
    log ("audition: " + s.name + " (the outputs keep its settings until COMMIT)");
    return true;
}

void AudioEngine::endAudition (Strip& s, bool commit)
{
    std::shared_ptr<VoiceChain> old;
    if (! commit && s.committed != nullptr && s.channel != nullptr)   // CANCEL: the settings back as they were
    {
        juce::MemoryBlock state;
        s.committed->getStateInformation (state);
        s.channel->setStateInformation (state.getData(), (int) state.getSize());
    }
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        std::swap (old, s.committed);
    }
    s.auditionB = false;
    log ("audition: " + s.name + (commit ? " committed" : " cancelled"));
    // (the old copy goes here, off the audio thread)
}

AudioEngine::AudioEngine (std::function<void (const juce::String&)> logFn)
    : log (std::move (logFn))
{
    padsMix.setSize (2, maxBlock);
    tapeMix.setSize (2, maxBlock);
    monitorMix.setSize (2, maxBlock);
    auditionMix.setSize (2, maxBlock);
    auditionMix.clear();

    asioType.reset (juce::AudioIODeviceType::createAudioIODeviceType_ASIO());
    wasapiType.reset (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));

    if (wasapiType != nullptr)
        wasapiType->scanForDevices();
}

AudioEngine::~AudioEngine()
{
    if (master != nullptr)
        master->close();
    recorder.stop (graphLock);   // finishes the file

    strips.clear();
    buses.clear();
    masters.clear();
    secondaries.clear();
    appCaptures.clear();
}

juce::String AudioEngine::nameFor (const juce::String& kind, const juce::String& wanted, const void* self) const
{
    juce::String own;
    auto taken = [&] (const juce::String& n)
    {
        auto check = [&] (auto& list)
        {
            for (auto& x : list)
            {
                if (x.get() == self) own = x->name;
                else if (x->name == n) return true;
            }
            return false;
        };
        return kind == "input" ? check (strips) : kind == "bus" ? check (buses) : check (masters);
    };
    const auto base = wanted.trim().toUpperCase();
    taken (base);   // (finds its own name)
    if (base.isEmpty())
        return own;
    auto name = base;
    for (int i = 2; taken (name); ++i)
        name = base + " " + juce::String (i);
    return name;
}

void AudioEngine::renamed (const juce::String& kind, const juce::String& oldName, const juce::String& newName)
{
    if (oldName == newName || oldName.isEmpty())
        return;

    const auto oldKey = kind + ":" + oldName, newKey = kind + ":" + newName;
    auto follow = [&] (juce::String& key) { if (key == oldKey) key = newKey; };

    if (kind == "input")
        for (auto* list : { &strips, &buses })
            for (auto& s : *list)
            {
                if (s->duckBy == oldName)
                    s->duckBy = newName;
                if (s->keyFrom == oldName)
                    s->keyFrom = newName;
            }
    for (auto& rule : appRules)
        follow (rule.to);
    for (auto& bus : masters)
    {
        if (kind == "output" && bus->minusOf == oldName)   bus->minusOf = newName;
        if (kind == "input" && bus->minusInput == oldName) bus->minusInput = newName;
    }
    for (auto& key : trayStrips)
        follow (key);
    follow (recorder.chosen);
    for (auto& scene : scenes.list)
        for (auto& setting : scene.settings)
        {
            follow (setting.target);
            for (auto& send : setting.sends)
                follow (send.first);
        }
    for (auto& macro : macros.list)
    {
        for (auto& step : macro.steps)
        {
            follow (step.target);
            follow (step.target2);
        }
        for (auto& t : macro.triggers)   // an output's silence or sound, by its name
            if (kind == "output" && t.what == oldName
                && (t.when == Macros::Trigger::When::outputSilent || t.when == Macros::Trigger::When::outputSound))
                t.what = newName;
    }
    remote.renameTarget (oldKey, newKey);

    refreshDucking();
    if (onRenamed)
        onRenamed (kind, oldName, newName);
}

void AudioEngine::sceneRenamed (const juce::String& oldName, const juce::String& newName)
{
    for (auto& macro : macros.list)
        for (auto& step : macro.steps)
            if (step.target == "scene:" + oldName)
                step.target = "scene:" + newName;
    remote.renameTarget ("scene:" + oldName, "scene:" + newName);
}

void AudioEngine::macroRenamed (const juce::String& oldName, const juce::String& newName)
{
    for (int i = 0; i < Pads::count; ++i)
        if (pads.pad (i).macro == oldName)
            pads.pad (i).macro = newName;
    remote.renameTarget ("macro:" + oldName, "macro:" + newName);
}

std::vector<AudioEngine::AppChoice> AudioEngine::appChoices (const std::vector<WindowDiscovery::Window>& windows) const
{
    std::vector<AppChoice> list;
    auto add = [this, &list] (const juce::String& exe, const juce::String& title, bool playing)
    {
        if (exe.isEmpty() || std::any_of (list.begin(), list.end(), [&exe] (const AppChoice& c) { return c.exe.equalsIgnoreCase (exe); }))
            return;
        const bool onDesk = std::any_of (strips.begin(), strips.end(), [&exe] (auto& s)
                                         { return s->source.kind == Source::app && s->source.deviceName.equalsIgnoreCase (exe); });
        list.push_back ({ exe, title, playing, onDesk });
    };

    for (auto& a : listAudioApps())
        add (a.exe, {}, true);
    for (auto& w : windows)   // (a Store app's window belongs to Windows' frame host, which plays nothing)
        if (! w.exeName().equalsIgnoreCase ("ApplicationFrameHost.exe"))
            add (w.exeName(), w.title, false);

    std::stable_sort (list.begin(), list.end(), [] (const AppChoice& a, const AppChoice& b)
                      { return a.playing != b.playing ? a.playing : a.exe.compareNatural (b.exe) < 0; });
    return list;
}

AudioEngine::Strip& AudioEngine::addAppStrip (const juce::String& exe, const juce::String& name)
{
    // (addStrip writes the name in capitals and numbers one that's taken)
    return addStrip (name.isNotEmpty() ? name : juce::File::createFileWithoutCheckingPath (exe).getFileNameWithoutExtension(),
                     { Source::app, 0, exe, true });
}

void AudioEngine::forgetAppRule (int stripIndex)
{
    if (! juce::isPositiveAndBelow (stripIndex, (int) strips.size()) || strips[(size_t) stripIndex]->source.kind != Source::app)
        return;
    const auto exe = strips[(size_t) stripIndex]->source.deviceName;
    appRules.erase (std::remove_if (appRules.begin(), appRules.end(), [&exe] (const AppRule& r) { return r.exe.equalsIgnoreCase (exe); }), appRules.end());
}

juce::StringArray AudioEngine::applyAppRules()
{
    juce::StringArray added;
    if (appRules.empty())
        return added;

    const auto playing = listAudioApps();
    for (auto& rule : appRules)
    {
        const bool isPlaying = std::any_of (playing.begin(), playing.end(), [&] (const AudioApp& a) { return a.exe.equalsIgnoreCase (rule.exe); });
        const bool hasChannel = std::any_of (strips.begin(), strips.end(), [&] (auto& s)
                                             { return s->source.kind == Source::app && s->source.deviceName.equalsIgnoreCase (rule.exe); });
        if (! isPlaying || hasChannel)
            continue;

        ++quietStructure;   // not an undo step: the rule made it
        auto& strip = addAppStrip (rule.exe, juce::File (rule.exe).getFileNameWithoutExtension());
        --quietStructure;
        const auto kind = rule.to.upToFirstOccurrenceOf (":", false, false), name = rule.to.fromFirstOccurrenceOf (":", false, false);
        for (int i = 0; i < (int) masters.size(); ++i)
            if (kind == "output" && masters[(size_t) i]->name == name)
                strip.sends[(size_t) i] = true;
        for (int i = 0; i < (int) buses.size(); ++i)
            if (kind == "bus" && buses[(size_t) i]->name == name)
                strip.busSends[(size_t) i] = true;
        added.add (strip.name + " -> " + name);
    }

    if (! added.isEmpty())
        refreshApps();   // starts the new captures now
    return added;
}

// Name compares only (no allocation, no locking): removeStrip, moveStrip and removeMaster call it
// inside the graphLock hold that moves the indexes, so no block runs with stale ones.
void AudioEngine::refreshDucking()
{
    for (auto* list : { &strips, &buses })
        for (auto& s : *list)
        {
            int found = -1;
            if (s->duckBy.isNotEmpty())
                for (int i = 0; i < (int) strips.size(); ++i)
                    if (strips[(size_t) i]->name == s->duckBy && strips[(size_t) i].get() != s.get())
                        found = i;
            s->duckSource = found;
            int key = -1;
            if (s->keyFrom.isNotEmpty())
                for (int i = 0; i < (int) strips.size(); ++i)
                    if (strips[(size_t) i]->name == s->keyFrom && strips[(size_t) i].get() != s.get())
                        key = i;
            s->keySource = key;
        }

    for (auto& bus : masters)
    {
        int main = -1, minus = -1;
        for (int i = 0; i < (int) masters.size(); ++i)
            if (bus->minusOf.isNotEmpty() && masters[(size_t) i]->name == bus->minusOf && masters[(size_t) i].get() != bus.get())
                main = i;
        for (int i = 0; i < (int) strips.size(); ++i)
            if (bus->minusInput.isNotEmpty() && strips[(size_t) i]->name == bus->minusInput)
                minus = i;
        bus->minusMain = main;
        bus->minusSource = minus;
    }
}

std::vector<AudioEngine::OutputChoice> AudioEngine::outputChoices() const
{
    std::vector<OutputChoice> list;
    const auto outs = getInterfaceOutputNames();
    // (a name given on DEVICES shows instead of the adapter's or the device's own)
    const auto adapter = deviceAlias (interfaceAdapter).isNotEmpty() ? deviceAlias (interfaceAdapter) : interfaceAdapter.isNotEmpty() ? interfaceAdapter : juce::String ("Interface");
    for (int ch = 0; ch < outs.size(); ch += 2)
        list.push_back ({ adapter + " Out " + juce::String (ch + 1) + (ch + 1 < outs.size() ? "-" + juce::String (ch + 2) : juce::String()), interfaceOutput, ch });
    for (auto& e : lastEndpoints)
        if (e.api == "WASAPI" && e.isOutput && e.isActive() && ! isInterfaceEndpoint (e)
            && ! disabledDevices.contains (e.name) && ! hiddenDevices.contains (e.name))
            list.push_back ({ deviceAlias (e.name).isNotEmpty() ? deviceAlias (e.name) : e.name, e.name, 0 });
    return list;
}

int AudioEngine::masterPlayingOn (const juce::String& candidate, int firstChannel) const
{
    for (int pass = 0; pass < 2; ++pass)   // (one whose own device it is first, then one also playing there)
        for (int i = 0; i < (int) masters.size(); ++i)
        {
            const auto places = masters[(size_t) i]->places();
            for (size_t p = 0; p < places.size(); ++p)
                if ((pass == 1 || p == 0) && places[p].candidate == candidate && places[p].firstChannel / 2 == firstChannel / 2)
                    return i;
        }
    return -1;
}

int AudioEngine::masterFor (const OutputChoice& out)
{
    if (const int i = masterPlayingOn (out.candidate, out.firstChannel); i >= 0)
        return i;
    if (addMaster (out.label, { out.candidate }, out.firstChannel, true) == nullptr)
    {
        log ("auto-route: no master free for " + out.label + " (all " + juce::String (maxMasters) + " in use)");
        return -1;
    }
    refreshDevices();   // opens the device
    log ("auto-route: master " + masters.back()->name + " made for " + out.label);
    return (int) masters.size() - 1;
}

void AudioEngine::setMixMinus (Master& bus, const juce::String& of, const juce::String& without)
{
    bus.minusOf = of;
    bus.minusInput = of.isEmpty() ? juce::String() : without;
    refreshDucking();
    log ("master " + bus.name + ": " + (of.isEmpty() ? juce::String ("mix-minus off")
                                        : "mix-minus of " + of + (bus.minusInput.isEmpty() ? juce::String() : " without " + bus.minusInput)));
}

int AudioEngine::getPadsTarget() const
{
    for (int i = 0; i < (int) strips.size(); ++i)
        if (strips[(size_t) i]->padsInto.load())
            return i;
    return -1;
}

void AudioEngine::setPadsTarget (int stripIndex)
{
    for (int i = 0; i < (int) strips.size(); ++i)
        strips[(size_t) i]->padsInto = i == stripIndex;
    log ("pads: playing into " + (juce::isPositiveAndBelow (stripIndex, (int) strips.size()) ? strips[(size_t) stripIndex]->name : juce::String ("the PADS strip")));
}

// Whether a strip's sound gets to output `bus`: sent there, or to a pair (bus) that does (audio thread).
bool AudioEngine::reachesOutput (const Strip& s, int bus, int depth) const
{
    if (! juce::isPositiveAndBelow (bus, (int) masters.size()) || depth > maxBuses)
        return false;
    if (s.sends[(size_t) bus])
        return true;
    for (size_t p = 0; p < buses.size(); ++p)
        if (s.busSends[p] && buses[p]->enabled && reachesOutput (*buses[p], bus, depth + 1))
            return true;
    return false;
}

juce::String AudioEngine::findFeedbackLoop (const Strip* changing, const Source* newSource) const
{
    const auto& endpoints = lastEndpoints;

    for (auto& s : strips)
    {
        const auto& source = s.get() == changing && newSource != nullptr ? *newSource : s->source;
        if (! s->enabled || source.kind != Source::device)
            continue;

        // The recording device it reads, as a cable's playback side names its partner.
        juce::String mic;
        for (auto& e : endpoints)
            if (e.api == "WASAPI" && ! e.isOutput && e.name == source.deviceName)
                mic = endpointDisplayName (e);
        if (mic.isEmpty())
            continue;

        for (int b = 0; b < (int) masters.size(); ++b)
        {
            if (! masters[(size_t) b]->enabled || ! reachesOutput (*s, b, 0))
                continue;
            for (auto& place : masters[(size_t) b]->places())
                for (auto& e : endpoints)
                    if (e.api == "WASAPI" && e.isOutput && (e.name == place.target || e.name == place.candidate)
                        && virtualMicPartner (e, endpoints) == mic)
                        return s->name + " > " + masters[(size_t) b]->name + " > " + endpointDisplayName (e).toUpperCase() + " > " + s->name;
        }
    }
    return {};
}

juce::StringArray AudioEngine::findDoubling() const
{
    juce::StringArray found;

    // Windows' default speaker (it drifts: installers and SteamVR change it) being a cable
    // Faider feeds as apps' mic sends every app, and a call's own sound, into the mic.
    for (auto& e : lastEndpoints)
    {
        if (e.api != "WASAPI" || ! e.isOutput || ! e.isDefault || ! e.isActive() || virtualMicPartner (e, lastEndpoints).isEmpty())
            continue;
        juce::String feeder;
        for (auto& b : masters)
            for (auto& place : b->places())
                if (feeder.isEmpty() && (place.target == e.name || place.candidate == e.name))
                    feeder = b->name;
        if (feeder.isNotEmpty())
            found.add ("Windows' default speaker is " + endpointDisplayName (e) + " (fed by " + feeder
                       + "), so every app goes into the mic. Make your headphones or speakers the default.");
    }

    // An app on a channel of its own still plays on its own device, so its channel belongs only
    // on the way to a mic cable: sent to an output you listen to, it's heard twice.
    auto isMicCable = [this] (const juce::String& device)
    {
        return std::any_of (lastEndpoints.begin(), lastEndpoints.end(), [&] (const EndpointInfo& e)
                            { return e.api == "WASAPI" && e.isOutput && e.name == device && virtualMicPartner (e, lastEndpoints).isNotEmpty(); });
    };
    for (auto& s : strips)
    {
        if (! s->enabled || s->source.kind != Source::app)
            continue;
        for (int b = 0; b < (int) masters.size(); ++b)
        {
            if (! masters[(size_t) b]->enabled || ! reachesOutput (*s, b, 0))
                continue;
            const auto places = masters[(size_t) b]->places();
            if (std::any_of (places.begin(), places.end(), [&] (auto& place) { return ! isMicCable (place.target.isNotEmpty() ? place.target : place.candidate); }))
                found.add (s->name + " reaches " + masters[(size_t) b]->name + " and also plays on its own, so you hear it twice. "
                           "Send apps only to the mic.");
        }
    }
    return found;
}

AudioEngine::Stats AudioEngine::getStats() const
{
    Stats st;
    st.loadPercent = loadAverage;
    st.peakLoadPercent = loadPeak;
    st.overloads = overloadCount;
    st.skippedBlocks = skippedCount;

    const double blockMs = 1000.0 * interfaceBlock / juce::jmax (1.0, interfaceRate);
    auto ms = [] (double samples, double rate) { return rate > 0.0 ? 1000.0 * samples / rate : 0.0; };

    if (master != nullptr)
    {
        for (const bool out : { false, true })
        {
            StreamStats s;
            s.name = master->getName() + " (clock, " + getClockName() + ")";
            s.direction = out ? "out" : "in";
            s.rate = interfaceRate;
            s.block = interfaceBlock;
            s.driverMs = ms (out ? master->getOutputLatencyInSamples() : master->getInputLatencyInSamples(), interfaceRate);
            s.totalMs = s.driverMs + blockMs;
            s.xruns = master->getXRunCount();
            st.streams.push_back (s);
        }
    }

    // Outputs: the buffer holds master-rate audio waiting for the device. Inputs: device-rate
    // audio waiting for the master.
    for (auto& [key, sec] : secondaries)
    {
        StreamStats s;
        s.name = sec->name;
        s.direction = sec->isOutput ? "out" : "in";
        if (sec->device != nullptr)
        {
            s.rate = sec->device->getCurrentSampleRate();
            s.block = sec->device->getCurrentBufferSizeSamples();
            s.driverMs = ms (sec->isOutput ? sec->device->getOutputLatencyInSamples() : sec->device->getInputLatencyInSamples(), s.rate);
            s.xruns = sec->device->getXRunCount();
        }
        s.bufferedMs = ms (sec->fifo.getFill(), sec->isOutput ? interfaceRate : s.rate);
        s.totalMs = juce::jmax (0.0, s.driverMs) + s.bufferedMs + blockMs;
        s.driftPpm = sec->fifo.getCorrectionPpm();
        s.underruns = sec->fifo.getUnderruns();
        s.overruns = sec->fifo.getOverruns();
        s.resyncs = sec->fifo.getResyncs();
        st.streams.push_back (s);
    }

    for (auto& [exe, cap] : appCaptures)
    {
        StreamStats s;
        s.name = juce::File (exe).getFileNameWithoutExtension() + " (app capture)";
        s.direction = "app";
        s.rate = interfaceRate;
        s.bufferedMs = ms (cap->fifo.getFill(), interfaceRate);
        s.totalMs = s.bufferedMs + blockMs;
        s.driftPpm = cap->fifo.getCorrectionPpm();
        s.underruns = cap->fifo.getUnderruns();
        s.overruns = cap->fifo.getOverruns();
        s.resyncs = cap->fifo.getResyncs();
        st.streams.push_back (s);
    }

    return st;
}

void AudioEngine::resetStats()
{
    loadPeak = 0.0f;
    overloadCount = 0;
    skippedCount = 0;
    for (auto& [key, sec] : secondaries)
        sec->fifo.resetCounters();
    for (auto& [exe, cap] : appCaptures)
        cap->fifo.resetCounters();
}

juce::String AudioEngine::getStatusText() const
{
    auto db = [] (float gain) { return juce::String (juce::Decibels::gainToDecibels (gain, -100.0f), 1) + " dB"; };
    juce::String s;

    s << "master: " << (master != nullptr ? master->getName() + " (" + getClockName() + ") @ " + juce::String (interfaceRate) + " Hz / "
                                              + juce::String (interfaceBlock) + " smp" + (interfaceDown ? " (DOWN: wouldn't reopen)" : "")
                                          : juce::String ("not running"))
      << "  adapter: " << (interfaceAdapter.isEmpty() ? "?" : interfaceAdapter) << "\n";

    if (master != nullptr)
        s << "  master in:  " << master->getInputChannelNames().joinIntoString (" | ") << "\n"
          << "  master out: " << master->getOutputChannelNames().joinIntoString (" | ") << "\n";

    for (auto& strip : strips)
    {
        juce::StringArray sends, chain;

        for (size_t b = 0; b < masters.size(); ++b)
            if (strip->sends[b])
                sends.add (masters[b]->name);

        for (auto& plugin : strip->inserts)
            chain.add (plugin->getName() + " (" + juce::String (plugin->getLatencySamples()) + " smp)");

        s << "strip " << strip->name << (strip->source.stereo ? "  stereo" : "  mono")
          << "  level L " << db (strip->peakL) << " R " << db (strip->peakR) << (strip->mute ? "  MUTED" : "")
          << "  gain " << db (strip->gain) << "  pan " << juce::String (strip->pan.load(), 2)
          << "  sends [" << sends.joinIntoString (", ") << "]"
          << "  inserts [" << chain.joinIntoString (", ") << "]  compensation " << strip->compensation << " smp\n";
    }

    for (auto& pair : buses)
    {
        juce::StringArray to;
        for (size_t b = 0; b < masters.size(); ++b)
            if (pair->sends[b])
                to.add (masters[b]->name);
        for (size_t p = 0; p < buses.size(); ++p)
            if (pair->busSends[p])
                to.add ("bus " + buses[p]->name);

        s << "bus " << pair->name << (pair->source.stereo ? "  stereo" : "  mono")
          << "  level L " << db (pair->peakL) << " R " << db (pair->peakR) << (pair->mute ? "  MUTED" : "")
          << "  sends [" << to.joinIntoString (", ") << "]  inserts " << (int) pair->inserts.size() << "\n";
    }

    for (auto& bus : masters)
    {
        s << "master " << bus->name << (bus->stereo ? "  stereo" : "  mono") << " -> "
          << (bus->target.isEmpty() ? "none" : bus->target)
          << (bus->target == interfaceOutput ? " ch " + juce::String (bus->firstChannel + 1) : juce::String());
        for (auto& d : bus->alsoOn)
            s << ", " << (d.target.isEmpty() ? "none (" + d.candidate + ")" : d.target)
              << (d.target == interfaceOutput ? " ch " + juce::String (d.firstChannel + 1) : juce::String());
        s << "  level L " << db (bus->peakL) << " R " << db (bus->peakR) << (bus->mute ? "  MUTED" : "") << "\n";
    }

    for (auto& [key, sec] : secondaries)
        s << (sec->isOutput ? "  out " : "  in  ") << sec->name
          << "  fill " << sec->fifo.getFill() << "/" << sec->fifo.getTarget()
          << "  drift " << juce::String (sec->fifo.getCorrectionPpm(), 1) << " ppm"
          << "  underruns " << sec->fifo.getUnderruns()
          << "  overruns " << sec->fifo.getOverruns()
          << "  resyncs " << sec->fifo.getResyncs() << "\n";

    for (auto& [exe, cap] : appCaptures)
        s << "  app " << exe << " (pid " << (int) cap->getProcessId() << ")"
          << "  fill " << cap->fifo.getFill() << "/" << cap->fifo.getTarget()
          << "  drift " << juce::String (cap->fifo.getCorrectionPpm(), 1) << " ppm"
          << "  underruns " << cap->fifo.getUnderruns()
          << "  overruns " << cap->fifo.getOverruns()
          << "  resyncs " << cap->fifo.getResyncs() << "\n";

    return s;
}

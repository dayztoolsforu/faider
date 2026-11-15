// AudioEngine's structure (message thread): strips, outputs and their devices, pairs, combinator
// paths and plugins added, removed and moved, and the latency worked out again (refreshLatency).
#include "AudioEngine.h"
#include "AudioEngineInternal.h"
#include "VoiceChain.h"
#include "EngineText.h"

namespace
{
    // A plugin that wouldn't go in: said in English (the session's log keeps it so), translated
    // where it's shown (AudioEngine::translateInsertError).
    constexpr const char* noPluginIn = NEEDS_TRANS ("no VST3 plugin found in %1");
    constexpr const char* pluginFailed = NEEDS_TRANS ("plugin failed to load");
    constexpr const char* tooManyChannels = NEEDS_TRANS ("%1 needs more than %2 channels");
    constexpr const char* noSuchInput = NEEDS_TRANS ("no such input");
    constexpr const char* noSuchBus = NEEDS_TRANS ("no such bus");
    constexpr const char* noSuchPath = NEEDS_TRANS ("no such path");

    // Where each index goes when the one at `removed` is taken out (-1: that one).
    std::function<int (int)> removedAt (int removed)
    {
        return [removed] (int i) { return i == removed ? -1 : i > removed ? i - 1 : i; };
    }

    // Adds to a list the audio thread reads (and does `also` in the same hold). A list that has to
    // grow gets its new storage before the lock is taken and its old freed after it: inside, only
    // pointers move.
    template <typename T, typename Also>
    void pushLocked (juce::SpinLock& lock, std::vector<T>& list, T item, Also&& also)
    {
        std::vector<T> grown;
        if (list.size() == list.capacity())
            grown.reserve (juce::jmax ((size_t) 8, list.capacity() * 2));
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            if (grown.capacity() > 0)
            {
                for (auto& x : list)
                    grown.push_back (std::move (x));
                list.swap (grown);
            }
            list.push_back (std::move (item));
            also();
        }
    }   // (the old storage goes here, outside the lock)

    template <typename T>
    void pushLocked (juce::SpinLock& lock, std::vector<T>& list, T item)
    {
        pushLocked (lock, list, std::move (item), [] {});
    }

    // Pairs in processing order: each after every pair that feeds it (`feeds (a, b)`: pair a sends
    // to pair b). Worked out before the lock is taken; the result is swapped into busOrder under it.
    template <typename Feeds>
    std::vector<int> busOrderFor (int count, Feeds&& feeds)
    {
        std::vector<int> incoming ((size_t) count, 0), order, ready;
        order.reserve ((size_t) count);

        for (int a = 0; a < count; ++a)
            for (int b = 0; b < count; ++b)
                if (a != b && feeds (a, b))
                    ++incoming[(size_t) b];

        for (int p = 0; p < count; ++p)
            if (incoming[(size_t) p] == 0)
                ready.push_back (p);

        while (! ready.empty())
        {
            const int p = ready.front();
            ready.erase (ready.begin());
            order.push_back (p);

            for (int b = 0; b < count; ++b)
                if (b != p && feeds (p, b) && --incoming[(size_t) b] == 0)
                    ready.push_back (b);
        }
        return order;
    }
}

//==============================================================================
void AudioEngine::prepareStrip (Strip& s) const
{
    s.buffer.setSize (2, maxBlock);
    s.rawBuffer.setSize (2, maxBlock);
    s.rawBuffer.clear();
    s.preBuffer.setSize (2, maxBlock);
    s.preBuffer.clear();
    s.keyTap.setSize (2, maxBlock);
    s.keyTap.clear();
    s.auditionBuf.setSize (2, maxBlock);
    s.auditionBuf.clear();
    s.pluginBuffer.setSize (pluginMaxChannels, maxBlock);
    s.delayLine.setSize (2, maxCompensation + maxBlock);
    s.delayLine.clear();
    s.midi.ensureSize (2048);

    if (s.channel == nullptr)
    {
        s.channel = std::make_shared<VoiceChain>();
        s.channel->setAllStages (false);
    }
    s.channel->prepareToPlay (interfaceRate, interfaceBlock);
}

AudioEngine::Strip& AudioEngine::addStrip (const juce::String& name, const Source& source)
{
    auto strip = std::make_unique<Strip>();
    // Names are written in capitals, like a desk's scribble strip, and no two inputs share one
    // (scenes and macros find them by name): "TEST TONE" taken, this is "TEST TONE 2".
    strip->name = nameFor ("input", name.trim().isEmpty() ? juce::String ("INPUT") : name, nullptr);
    strip->source = source;
    prepareStrip (*strip);
    enableLoopbackFor (source);

    auto& ref = *strip;
    pushLocked (graphLock, strips, std::move (strip));
    refreshLatency();   // its rack compiled, and it lined up with the others from the start
    structureAdded ("input", (int) strips.size() - 1, -1);
    return ref;
}

void AudioEngine::removeStrip (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) strips.size()))
        return;
    structureRemoving ("input", index, -1);

    std::unique_ptr<Strip> removed;
    bool recordingLost = false;
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        removed = std::move (strips[(size_t) index]);
        strips.erase (strips.begin() + index);
        recordingLost = recorder.remapLocked (Recorder::Kind::input, removedAt (index));
        refreshDucking();   // (ducks and mix-minus are kept by position: in the same hold)
    }
    if (recordingLost)
        stopRecording();

    for (auto& plugin : removed->inserts)
        plugin->releaseResources();
    forgetOptions (*removed);

    closeUnused();
    refreshApps();
    refreshLatency();
}

bool AudioEngine::moveStrip (int from, int to)
{
    const int count = (int) strips.size();

    if (! juce::isPositiveAndBelow (from, count) || ! juce::isPositiveAndBelow (to, count) || from == to
        || strips[(size_t) from]->locked || strips[(size_t) to]->locked)
        return false;

    // Reorder only the unlocked slots, so locked strips stay exactly where they are.
    std::vector<int> slots;
    for (int i = 0; i < count; ++i)
        if (! strips[(size_t) i]->locked)
            slots.push_back (i);

    const auto pos = [&slots] (int index) { return (int) (std::find (slots.begin(), slots.end(), index) - slots.begin()); };
    const int pf = pos (from), pt = pos (to);

    std::vector<std::unique_ptr<Strip>> moving;
    moving.reserve (slots.size());

    // Where each strip ends up, for the recorder to follow its source.
    std::vector<int> order (slots.begin(), slots.end()), newIndex ((size_t) count);
    order.erase (order.begin() + pf);
    order.insert (order.begin() + pt, slots[(size_t) pf]);
    for (int i = 0; i < count; ++i)
        newIndex[(size_t) i] = i;
    for (size_t i = 0; i < slots.size(); ++i)
        newIndex[(size_t) order[i]] = slots[i];

    {
        // The audio thread must never see a slot mid-shuffle, so hold the lock throughout.
        const juce::SpinLock::ScopedLockType sl (graphLock);

        for (int s : slots)
            moving.push_back (std::move (strips[(size_t) s]));

        auto item = std::move (moving[(size_t) pf]);
        moving.erase (moving.begin() + pf);
        moving.insert (moving.begin() + pt, std::move (item));

        for (size_t i = 0; i < slots.size(); ++i)
            strips[(size_t) slots[i]] = std::move (moving[i]);

        recorder.remapLocked (Recorder::Kind::input, [&newIndex] (int i) { return juce::isPositiveAndBelow (i, (int) newIndex.size()) ? newIndex[(size_t) i] : -1; });
        refreshDucking();   // (ducks and mix-minus are kept by position: in the same hold)
    }
    return true;
}

void AudioEngine::setStripSource (int index, const Source& source)
{
    if (! juce::isPositiveAndBelow (index, (int) strips.size()))
        return;

    // Playing something now: fade it out first, switch, then fade the new source in. Picked again
    // while that fade runs: the switch takes the latest pick (one fade, never a cut).
    auto* strip = strips[(size_t) index].get();
    if (strip->sourceFadeOut.load())
    {
        strip->sourceNext = source;
        return;
    }
    const bool fadeFirst = strip->enabled.load() && strip->source.kind != Source::none;
    auto change = [this, strip, source, fadeFirst]
    {
        if (std::none_of (strips.begin(), strips.end(), [strip] (auto& s) { return s.get() == strip; }))
            return;   // removed while fading
        const auto chosen = strip->sourceNext.value_or (source);
        strip->sourceNext.reset();
        auto next = chosen;   // (swapped in: the old source's name is let go outside the lock)
        {
            const juce::SpinLock::ScopedLockType sl (graphLock);
            std::swap (strip->source, next);
            strip->input = nullptr;   // updateEndpoints reopens the right device
            strip->app = nullptr;     // and refreshApps the right app's capture
        }
        enableLoopbackFor (chosen);
        refreshDevices();
        strip->sourceFadeOut = false;
        strip->sourceFadeRestart = true;
        juce::ignoreUnused (fadeFirst);
        if (onRoutingApplied)
            for (int i = 0; i < (int) strips.size(); ++i)
                if (strips[(size_t) i].get() == strip)
                    onRoutingApplied (false, i);
    };
    if (fadeFirst)
    {
        strip->sourceFadeOut = true;
        afterFadeOut (change);
    }
    else
        change();
}

bool AudioEngine::waitForDeviceFade (Master& bus, std::function<void (int index)> retry)
{
    if (! bus.deviceFadeOut.load())
        return false;
    deviceQueue[&bus].push_back ([this, b = &bus, retry = std::move (retry)]
    {
        for (int i = 0; i < (int) masters.size(); ++i)
            if (masters[(size_t) i].get() == b)
                retry (i);
    });
    return true;
}

void AudioEngine::afterFadeOut (std::function<void()> change)
{
    juce::Timer::callAfterDelay (juce::roundToInt (fadeOutSeconds * 1000.0) + 20,
                                 [guard = std::weak_ptr<bool> (alive), change = std::move (change)] { if (guard.lock() != nullptr) change(); });
}

AudioEngine::Master* AudioEngine::addMaster (const juce::String& name, const juce::StringArray& candidates, int firstChannel, bool stereo)
{
    if ((int) masters.size() >= maxMasters)
        return nullptr;

    auto bus = std::make_unique<Master>();
    bus->name = nameFor ("output", name.trim().isEmpty() ? juce::String ("OUTPUT") : name, nullptr);   // (unique, as inputs')
    bus->candidates = candidates;
    bus->firstChannel = firstChannel;
    bus->stereo = stereo;
    bus->buffer.setSize (2, maxBlock);
    bus->lastBlock.setSize (2, maxBlock);
    bus->retro.prepare (retroFrames());
    bus->lastBlock.clear();
    bus->loudness.prepare (interfaceRate);
    bus->masterChain = std::make_shared<VoiceChain>();
    bus->masterChain->setAllStages (false);   // (the mix bus starts flat: nothing in)
    bus->masterChain->prepareToPlay (interfaceRate, interfaceBlock);

    auto* raw = bus.get();
    pushLocked (graphLock, masters, std::move (bus));
    structureAdded ("output", (int) masters.size() - 1, -1);
    return raw;
}

void AudioEngine::removeMaster (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) masters.size()))
        return;
    structureRemoving ("output", index, -1);
    deviceQueue.erase (masters[(size_t) index].get());

    bool recordingLost = false;
    std::unique_ptr<Master> removed;   // freed after the lock (its buffers and retro ring are large)
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        removed = std::move (masters[(size_t) index]);
        masters.erase (masters.begin() + index);
        recordingLost = recorder.remapLocked (Recorder::Kind::output, removedAt (index));

        // Sends (and their RAW keys) are indexed by bus position, inputs' and buses' alike, so
        // shift the later ones down.
        auto shift = [index] (Strip& s)
        {
            for (int b = index; b < maxMasters - 1; ++b)
            {
                s.sends[(size_t) b] = s.sends[(size_t) b + 1].load();
                s.rawSends[(size_t) b] = s.rawSends[(size_t) b + 1].load();
                s.preSends[(size_t) b] = s.preSends[(size_t) b + 1].load();
                s.sendDb[(size_t) b] = s.sendDb[(size_t) b + 1].load();
                s.sendDbNow[(size_t) b] = s.sendDbNow[(size_t) b + 1];
                s.sendFade[(size_t) b] = s.sendFade[(size_t) b + 1];
            }
            s.sends[maxMasters - 1] = false;
            s.rawSends[maxMasters - 1] = false;
            s.preSends[maxMasters - 1] = false;
            s.sendDb[maxMasters - 1] = 0.0f;
            s.sendDbNow[maxMasters - 1] = 0.0f;
            s.sendFade[maxMasters - 1] = 0.0f;
        };
        for (auto& s : strips) shift (*s);
        for (auto& p : buses)  shift (*p);

        // Loopback sources too.
        for (auto& s : strips)
            if (s->source.kind == Source::loopback)
            {
                if (s->source.masterIndex == index)
                    s->source.kind = Source::none;
                else if (s->source.masterIndex > index)
                    --s->source.masterIndex;
            }
        refreshDucking();   // (mix-minus is kept by position: in the same hold)
    }
    removed = nullptr;
    if (recordingLost)
        stopRecording();

    closeUnused();
}

void AudioEngine::setMasterEnabled (int index, bool enabled)
{
    if (! juce::isPositiveAndBelow (index, (int) masters.size()))
        return;

    auto& bus = *masters[(size_t) index];
    bus.enabled = enabled;
    bus.peakL = bus.peakR = 0.0f;
    bus.levels.meterL = bus.levels.meterR = 0.0f;   // an output that's off isn't processed, so its meters would freeze
    log ("master " + bus.name + (enabled ? ": enabled" : ": disabled"));
    refreshDevices();   // opens or releases its device
}

void AudioEngine::setStripEnabled (Strip& strip, bool enabled)
{
    strip.enabled = enabled;
    strip.peakL = strip.peakR = 0.0f;
    strip.levels.meterL = strip.levels.meterR = 0.0f;   // not processed while off, so its meters would freeze
    log ((strip.source.kind == Source::bus ? "bus " : "input ") + strip.name + (enabled ? ": enabled" : ": disabled"));
    refreshDevices();   // opens or releases its device or app capture
}

void AudioEngine::setMasterOutput (int index, const juce::StringArray& candidates, int firstChannel, bool stereo)
{
    if (! juce::isPositiveAndBelow (index, (int) masters.size()))
        return;

    // Playing on a device now: fade it out first, switch, then fade back in.
    auto* bus = masters[(size_t) index].get();
    if (waitForDeviceFade (*bus, [this, candidates, firstChannel, stereo] (int i) { setMasterOutput (i, candidates, firstChannel, stereo); }))
        return;
    const bool fadeFirst = bus->enabled.load() && bus->target.isNotEmpty();
    auto change = [this, bus, candidates, firstChannel, stereo, fadeFirst]
    {
        if (std::none_of (masters.begin(), masters.end(), [bus] (auto& b) { return b.get() == bus; }))
            return;   // removed while fading
        // Its other devices follow mono/stereo (master channels in buses), and drop any that is
        // now its own device. No device at all: silent everywhere. (Both lists are made here and
        // swapped in, so nothing is copied or freed under the lock.)
        auto nextCandidates = candidates;
        std::vector<Master::Device> nextAlso;
        if (! candidates.isEmpty())
            for (auto d : bus->alsoOn)
            {
                if (stereo && d.candidate == interfaceOutput)
                    d.firstChannel -= d.firstChannel % 2;
                if (! samePlace (d.candidate, d.firstChannel, candidates[0], firstChannel, stereo))
                    nextAlso.push_back (std::move (d));
            }
        {
            const juce::SpinLock::ScopedLockType sl (graphLock);
            bus->candidates.swapWith (nextCandidates);
            bus->firstChannel = firstChannel;
            bus->stereo = stereo;
            bus->output = nullptr;
            bus->toInterface = false;
            bus->alsoOn.swap (nextAlso);
        }
        refreshDevices();   // output was cleared above, so this re-resolves it
        bus->deviceFadeOut = false;
        bus->deviceFadeRestart = true;
        if (auto queued = deviceQueue.find (bus); queued != deviceQueue.end())   // what waited behind this
        {
            auto next = std::move (queued->second);
            deviceQueue.erase (queued);
            for (auto& f : next)
                f();
        }
        if (onRoutingApplied)
            for (int i = 0; i < (int) masters.size(); ++i)
                if (masters[(size_t) i].get() == bus)
                    onRoutingApplied (true, i);
    };
    if (fadeFirst)
    {
        bus->deviceFadeOut = true;
        afterFadeOut (change);
    }
    else
        change();
}

bool AudioEngine::samePlace (const juce::String& a, int firstA, const juce::String& b, int firstB, bool stereo)
{
    if (a != b)
        return false;
    return stereo ? firstA / 2 == firstB / 2 : firstA == firstB;   // the same channels (a stereo device: always 0)
}

void AudioEngine::addMasterDevice (int index, const juce::String& candidate, int firstChannel)
{
    if (! juce::isPositiveAndBelow (index, (int) masters.size()))
        return;

    auto& bus = *masters[(size_t) index];
    if (waitForDeviceFade (bus, [this, candidate, firstChannel] (int i) { addMasterDevice (i, candidate, firstChannel); }))
        return;
    if (bus.candidates.isEmpty())
    {
        setMasterOutput (index, { candidate }, firstChannel, bus.stereo);
        return;
    }

    for (auto& p : bus.places())
        if (samePlace (p.candidate, p.firstChannel, candidate, firstChannel, bus.stereo))
            return;   // already plays there

    pushLocked (graphLock, bus.alsoOn, Master::Device { candidate, bus.stereo && candidate == interfaceOutput ? firstChannel - firstChannel % 2 : firstChannel });
    refreshDevices();   // opens it
}

void AudioEngine::removeMasterDevice (int index, const juce::String& candidate, int firstChannel)
{
    if (! juce::isPositiveAndBelow (index, (int) masters.size()))
        return;

    auto& bus = *masters[(size_t) index];
    if (waitForDeviceFade (bus, [this, candidate, firstChannel] (int i) { removeMasterDevice (i, candidate, firstChannel); }))
        return;

    // Its own device: the next one takes over, or it goes silent.
    if (! bus.candidates.isEmpty() && samePlace (bus.candidates[0], bus.firstChannel, candidate, firstChannel, bus.stereo))
    {
        if (bus.alsoOn.empty())
        {
            setMasterOutput (index, {}, 0, bus.stereo);
            return;
        }
        const auto next = bus.alsoOn.front();
        setMasterOutput (index, { next.candidate }, next.firstChannel, bus.stereo);   // drops it from alsoOn
        return;
    }

    std::vector<Master::Device> kept;   // (made here and swapped in: the one dropped is freed outside the lock)
    kept.reserve (bus.alsoOn.size());
    for (auto& d : bus.alsoOn)
        if (! samePlace (d.candidate, d.firstChannel, candidate, firstChannel, bus.stereo))
            kept.push_back (d);
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        bus.alsoOn.swap (kept);
    }
    refreshDevices();   // releases it if nothing else plays there
}

//==============================================================================
juce::StringArray AudioEngine::findInstalledPlugins()
{
    auto paths = vst3.getDefaultLocationsToSearch();

    // Per-user VST3 folder from the VST3 spec; needs no admin rights to install into.
    paths.addIfNotAlreadyThere (juce::File::getSpecialLocation (juce::File::windowsLocalAppData)
                                    .getChildFile ("Programs").getChildFile ("Common").getChildFile ("VST3"));

    return vst3.searchPathsForPlugins (paths, true, false);
}

std::unique_ptr<juce::AudioPluginInstance> AudioEngine::createPlugin (const juce::String& pluginPath, juce::String& error)
{
    // Every plugin made gets fresh insert options (replacing any left by an old plugin that
    // happened to live at the same address).
    auto withOptions = [this] (std::unique_ptr<juce::AudioPluginInstance> made)
    {
        auto options = std::make_unique<InsertOptions>();
        options->dry.setSize (2, maxBlock);
        options->line.setSize (2, maxCompensation + maxBlock);
        options->line.clear();
        // Its place in the table is made here, in a table of its own, and moved across under the
        // lock; options left by an old plugin come out the same way and are freed after it.
        InsertOptionsTable fresh;
        fresh.emplace (made.get(), std::move (options));
        auto node = fresh.extract (fresh.begin());
        {
            const juce::SpinLock::ScopedLockType sl (graphLock);
            if (const auto it = optionsTable.find (made.get()); it != optionsTable.end())
                std::swap (it->second, node.mapped());
            else
                optionsTable.insert (std::move (node));
        }
        return made;
    };

    if (pluginPath == VoiceChain::identifier)
    {
        auto chain = std::make_unique<VoiceChain>();
        chain->prepareToPlay (interfaceRate, interfaceBlock);
        return withOptions (std::move (chain));
    }

    juce::OwnedArray<juce::PluginDescription> types;
    vst3.findAllTypesForFile (types, pluginPath);

    if (types.isEmpty())
    {
        error = juce::String (noPluginIn).replace ("%1", pluginPath);
        return nullptr;
    }

    auto plugin = vst3.createInstanceFromDescription (*types[0], interfaceRate, interfaceBlock, error);

    if (plugin == nullptr)
    {
        if (error.isEmpty())
            error = pluginFailed;
        return nullptr;
    }

    // Strips and masters are stereo: ask for stereo main masters and switch off sidechains/aux masters.
    auto layout = plugin->getBusesLayout();

    for (auto* busList : { &layout.inputBuses, &layout.outputBuses })
        for (int i = 0; i < busList->size(); ++i)
            busList->getReference (i) = (i == 0) ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();

    if (! plugin->setBusesLayout (layout))
        log ("engine: " + plugin->getName() + " rejected a stereo layout; using its default");

    if (juce::jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()) > pluginMaxChannels)
    {
        error = juce::String (tooManyChannels).replace ("%2", juce::String (pluginMaxChannels)).replace ("%1", plugin->getName());
        return nullptr;
    }

    plugin->prepareToPlay (interfaceRate, interfaceBlock);
    return withOptions (std::move (plugin));
}

juce::String AudioEngine::addInsertTo (std::vector<std::unique_ptr<juce::AudioPluginInstance>>& chain,
                                       const juce::String& owner, const juce::String& pluginPath)
{
    juce::String error;
    auto plugin = createPlugin (pluginPath, error);
    if (plugin == nullptr)
        return error;

    const auto name = plugin->getName();
    pushLocked (graphLock, chain, std::move (plugin));

    log (owner + ": inserted " + name);
    refreshLatency();
    return {};
}

void AudioEngine::removeInsertFrom (std::vector<std::unique_ptr<juce::AudioPluginInstance>>& chain,
                                    const juce::String& owner, int slot)
{
    if (! juce::isPositiveAndBelow (slot, (int) chain.size()))
        return;

    std::unique_ptr<juce::AudioPluginInstance> removed;
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        removed = std::move (chain[(size_t) slot]);
        chain.erase (chain.begin() + slot);
    }

    log (owner + ": removed " + removed->getName());
    refreshLatency();   // first: the racks stop running it

    // (A plan that still names it, should a rack ever not compile: taken out before the plugin is
    // freed, so the audio thread can't call it. The channel runs its own stages until the next compile.)
    for (auto* list : { &strips, &buses })
        for (auto& s : *list)
            if (s->rackPlan != nullptr && std::any_of (s->rackPlan->nodes.begin(), s->rackPlan->nodes.end(),
                                                       [&removed] (const RackPlan::Node& n) { return n.plugin == removed.get(); }))
            {
                std::shared_ptr<RackPlan> old;
                {
                    const juce::SpinLock::ScopedLockType sl (graphLock);
                    std::swap (s->rackPlan, old);
                }
                s->rackCompiled.clear();
            }

    removed->releaseResources();
    forgetOptions (removed.get());
}

// A plugin gone for good: its IN/OUT/MIX settings and their delay lines (about half a megabyte)
// go too, taken out under the lock and freed after it.
void AudioEngine::forgetOptions (const juce::AudioPluginInstance* plugin)
{
    InsertOptionsTable::node_type gone;   // (its place in the table too)
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        if (const auto it = optionsTable.find (plugin); it != optionsTable.end())
            gone = optionsTable.extract (it);
    }
}

void AudioEngine::forgetOptions (const Strip& s)
{
    for (auto& plugin : s.inserts)
        forgetOptions (plugin.get());
    for (auto& path : s.paths)
        for (auto& plugin : path->inserts)
            forgetOptions (plugin.get());
}

void AudioEngine::swapInserts (std::vector<std::unique_ptr<juce::AudioPluginInstance>>& chain, int a, int b)
{
    if (a == b || ! juce::isPositiveAndBelow (a, (int) chain.size()) || ! juce::isPositiveAndBelow (b, (int) chain.size()))
        return;

    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        std::swap (chain[(size_t) a], chain[(size_t) b]);
    }

    log ("inserts: swapped " + chain[(size_t) b]->getName() + " and " + chain[(size_t) a]->getName());
    refreshLatency();   // (the racks follow)
}

juce::String AudioEngine::translateInsertError (const juce::String& error)
{
    return EngineText::translate (error, { noPluginIn, pluginFailed, tooManyChannels, noSuchInput, noSuchBus, noSuchPath });
}

juce::String AudioEngine::addInsert (int i, const juce::String& path)
{
    if (! juce::isPositiveAndBelow (i, (int) strips.size()))
        return noSuchInput;
    auto& chain = strips[(size_t) i]->inserts;
    const auto error = addInsertTo (chain, "input " + strips[(size_t) i]->name, path);
    if (error.isEmpty())
        structureAdded ("input", i, (int) chain.size() - 1);
    return error;
}

void AudioEngine::removeInsert (int i, int slot)
{
    if (! juce::isPositiveAndBelow (i, (int) strips.size()) || ! juce::isPositiveAndBelow (slot, (int) strips[(size_t) i]->inserts.size()))
        return;
    structureRemoving ("input", i, slot);
    removeInsertFrom (strips[(size_t) i]->inserts, "input " + strips[(size_t) i]->name, slot);
}

juce::String AudioEngine::addBusInsert (int i, const juce::String& path)
{
    if (! juce::isPositiveAndBelow (i, (int) buses.size()))
        return noSuchBus;
    auto& chain = buses[(size_t) i]->inserts;
    const auto error = addInsertTo (chain, "bus " + buses[(size_t) i]->name, path);
    if (error.isEmpty())
        structureAdded ("bus", i, (int) chain.size() - 1);
    return error;
}

void AudioEngine::removeBusInsert (int i, int slot)
{
    if (! juce::isPositiveAndBelow (i, (int) buses.size()) || ! juce::isPositiveAndBelow (slot, (int) buses[(size_t) i]->inserts.size()))
        return;
    structureRemoving ("bus", i, slot);
    removeInsertFrom (buses[(size_t) i]->inserts, "bus " + buses[(size_t) i]->name, slot);
}

//==============================================================================
AudioEngine::Strip* AudioEngine::addBus (const juce::String& name)
{
    if ((int) buses.size() >= maxBuses)
        return nullptr;

    auto pair = std::make_unique<Strip>();
    pair->name = nameFor ("bus", name.trim().isEmpty() ? juce::String ("BUS") : name, nullptr);   // (unique, as inputs')
    pair->source.kind = Source::bus;
    prepareStrip (*pair);
    pair->busInput.setSize (2, maxBlock);
    pair->busInput.clear();

    auto* raw = pair.get();
    // (the order with it in, worked out before it is)
    auto at = [this, raw] (int i) -> const Strip& { return i < (int) buses.size() ? *buses[(size_t) i] : *raw; };
    auto order = busOrderFor ((int) buses.size() + 1, [&at] (int a, int b) { return at (a).busSends[(size_t) b].load(); });
    pushLocked (graphLock, buses, std::move (pair), [this, &order] { busOrder.swap (order); });
    structureAdded ("bus", (int) buses.size() - 1, -1);
    return raw;
}

AudioEngine::Strip* AudioEngine::addCombinator (const juce::String& name, int paths)
{
    auto* pair = addBus (name);
    if (pair == nullptr)
        return nullptr;

    juce::AudioBuffer<float> sum (2, maxBlock);   // (made here, swapped in under the lock)
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        std::swap (pair->pathSum, sum);
        pair->combinator = true;
    }

    const int index = (int) buses.size() - 1;
    for (int p = 0; p < paths; ++p)
        addPath (index, "Path " + juce::String (p + 1));

    if (paths > 0)   // a session restore adds its paths afterwards, and logs its own summary
        log ("combinator " + name + ": added with " + juce::String (paths) + " paths");
    return pair;
}

void AudioEngine::addPath (int busIndex, const juce::String& name)
{
    if (! juce::isPositiveAndBelow (busIndex, (int) buses.size()) || ! buses[(size_t) busIndex]->combinator
        || (int) buses[(size_t) busIndex]->paths.size() >= maxPaths)
        return;

    auto path = std::make_unique<Path>();
    path->name = name.toUpperCase();
    path->input.setSize (2, maxBlock);
    path->input.clear();
    path->buffer.setSize (2, maxBlock);
    path->pluginBuffer.setSize (pluginMaxChannels, maxBlock);
    path->delayLine.setSize (2, maxCompensation + maxBlock);
    path->delayLine.clear();
    path->midi.ensureSize (2048);

    pushLocked (graphLock, buses[(size_t) busIndex]->paths, std::move (path));
}

void AudioEngine::removePath (int busIndex, int pathIndex)
{
    if (! juce::isPositiveAndBelow (busIndex, (int) buses.size())
        || ! juce::isPositiveAndBelow (pathIndex, (int) buses[(size_t) busIndex]->paths.size()))
        return;

    std::unique_ptr<Path> removed;
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        auto& paths = buses[(size_t) busIndex]->paths;
        removed = std::move (paths[(size_t) pathIndex]);
        paths.erase (paths.begin() + pathIndex);

        // Sends into the later paths move down with them; one only into this path now feeds none
        // (so the first: the send stays, as the cable to the combinator does).
        auto shift = [busIndex, pathIndex] (Strip& s)
        {
            const unsigned mask = s.pathSends[(size_t) busIndex].load();
            const unsigned below = mask & ((1u << pathIndex) - 1u);
            s.pathSends[(size_t) busIndex] = below | ((mask >> (pathIndex + 1)) << pathIndex);
        };
        for (auto& s : strips) shift (*s);
        for (auto& p : buses)  shift (*p);
    }

    refreshLatency();   // (first: the racks stop running them)
    for (auto& plugin : removed->inserts)
    {
        plugin->releaseResources();
        forgetOptions (plugin.get());
    }
}

juce::String AudioEngine::addPathInsert (int busIndex, int pathIndex, const juce::String& pluginPath)
{
    if (! juce::isPositiveAndBelow (busIndex, (int) buses.size())
        || ! juce::isPositiveAndBelow (pathIndex, (int) buses[(size_t) busIndex]->paths.size()))
        return noSuchPath;

    auto& pair = *buses[(size_t) busIndex];
    return addInsertTo (pair.paths[(size_t) pathIndex]->inserts, "combinator " + pair.name + " / " + pair.paths[(size_t) pathIndex]->name, pluginPath);
}

void AudioEngine::removePathInsert (int busIndex, int pathIndex, int slot)
{
    if (juce::isPositiveAndBelow (busIndex, (int) buses.size())
        && juce::isPositiveAndBelow (pathIndex, (int) buses[(size_t) busIndex]->paths.size()))
    {
        auto& pair = *buses[(size_t) busIndex];
        removeInsertFrom (pair.paths[(size_t) pathIndex]->inserts, "combinator " + pair.name + " / " + pair.paths[(size_t) pathIndex]->name, slot);
    }
}

void AudioEngine::removeBus (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) buses.size()))
        return;
    structureRemoving ("bus", index, -1);

    // The order without it, worked out before it goes: the later pairs are one place down.
    auto was = [index] (int i) { return i < index ? i : i + 1; };
    auto order = busOrderFor ((int) buses.size() - 1, [this, &was] (int a, int b)
                              { return buses[(size_t) was (a)]->busSends[(size_t) was (b)].load(); });

    std::unique_ptr<Strip> removed;
    bool recordingLost = false;
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        removed = std::move (buses[(size_t) index]);
        buses.erase (buses.begin() + index);
        recordingLost = recorder.remapLocked (Recorder::Kind::bus, removedAt (index));

        // Pair sends are indexed by pair position, so shift the later ones down everywhere.
        auto shift = [index] (Strip& s)
        {
            for (int p = index; p < maxBuses - 1; ++p)
            {
                s.busSends[(size_t) p] = s.busSends[(size_t) p + 1].load();
                s.pathSends[(size_t) p] = s.pathSends[(size_t) p + 1].load();
                s.busSendDb[(size_t) p] = s.busSendDb[(size_t) p + 1].load();
                s.busSendDbNow[(size_t) p] = s.busSendDbNow[(size_t) p + 1];
                s.busSendFade[(size_t) p] = s.busSendFade[(size_t) p + 1];
            }
            s.busSends[maxBuses - 1] = false;
            s.pathSends[maxBuses - 1] = 0;
            s.busSendDb[maxBuses - 1] = 0.0f;
            s.busSendDbNow[maxBuses - 1] = 0.0f;
            s.busSendFade[maxBuses - 1] = 0.0f;
        };

        for (auto& s : strips) shift (*s);
        for (auto& p : buses)  shift (*p);
        busOrder.swap (order);
    }
    if (recordingLost)
        stopRecording();

    for (auto& plugin : removed->inserts)
        plugin->releaseResources();
    forgetOptions (*removed);

    log ("bus " + removed->name + ": removed");
}

void AudioEngine::setBusStereo (int index, bool stereo)
{
    if (! juce::isPositiveAndBelow (index, (int) buses.size()))
        return;

    const juce::SpinLock::ScopedLockType sl (graphLock);
    buses[(size_t) index]->source.stereo = stereo;
}

// True if audio from pair `from` can already reach pair `to` through pair-to-pair sends.
bool AudioEngine::busReaches (int from, int to) const
{
    std::vector<int> stack { from };
    std::vector<bool> seen (buses.size(), false);

    while (! stack.empty())
    {
        const int p = stack.back();
        stack.pop_back();

        if (p == to)
            return true;

        if (seen[(size_t) p])
            continue;

        seen[(size_t) p] = true;
        for (int next = 0; next < (int) buses.size(); ++next)
            if (buses[(size_t) p]->busSends[(size_t) next])
                stack.push_back (next);
    }

    return false;
}

bool AudioEngine::setBusSend (int fromBus, int toBus, bool on)
{
    if (! juce::isPositiveAndBelow (fromBus, (int) buses.size()) || ! juce::isPositiveAndBelow (toBus, (int) buses.size()))
        return false;

    // Sending A -> B is a loop if B already reaches A (or it's the same pair).
    if (on && (fromBus == toBus || busReaches (toBus, fromBus)))
    {
        log ("bus " + buses[(size_t) fromBus]->name + " -> " + buses[(size_t) toBus]->name + ": refused, it would make a loop");
        return false;
    }

    // (the order with the send as it'll be, worked out before the lock)
    auto order = busOrderFor ((int) buses.size(), [&] (int a, int b)
                              { return a == fromBus && b == toBus ? on : buses[(size_t) a]->busSends[(size_t) b].load(); });

    const juce::SpinLock::ScopedLockType sl (graphLock);
    buses[(size_t) fromBus]->busSends[(size_t) toBus] = on;
    busOrder.swap (order);
    return true;
}

bool AudioEngine::setPathSend (Strip& from, int toBus, int path, bool on)
{
    if (! juce::isPositiveAndBelow (toBus, (int) buses.size()) || ! buses[(size_t) toBus]->combinator
        || ! juce::isPositiveAndBelow (path, (int) buses[(size_t) toBus]->paths.size()))
        return false;

    int fromBus = -1;
    for (int p = 0; p < (int) buses.size(); ++p)
        if (buses[(size_t) p].get() == &from)
            fromBus = p;

    auto send = [&] (bool sendOn) { return fromBus >= 0 ? setBusSend (fromBus, toBus, sendOn) : (from.busSends[(size_t) toBus] = sendOn, true); };

    const unsigned bit = 1u << path;
    const unsigned saved = from.pathSends[(size_t) toBus].load();
    const unsigned before = from.busSends[(size_t) toBus].load() ? (saved != 0 ? saved : 1u) : 0u;   // none: the first
    const unsigned mask = on ? before | bit : before & ~bit;

    // The paths first, then the send, so no block feeds the wrong path in between.
    if (mask != 0)
    {
        from.pathSends[(size_t) toBus] = mask;
        if (send (true))
            return true;
        from.pathSends[(size_t) toBus] = saved;   // refused: a loop
        return false;
    }
    send (false);
    from.pathSends[(size_t) toBus] = 0;
    return true;
}

void AudioEngine::refreshLatency()
{
    int longest = 0;

    // A VoiceChain as a plugin, in any chain (a strip's, a pair's or a combinator path's).
    auto applyChain = [] (std::vector<std::unique_ptr<juce::AudioPluginInstance>>& chain)
    {
        for (auto& plugin : chain)
            if (auto* voice = dynamic_cast<VoiceChain*> (plugin.get()))
                voice->applyLatency();
    };

    for (auto* list : { &strips, &buses })
        for (auto& s : *list)
        {
            if (s->channel != nullptr)
                s->channel->applyLatency();   // (noise reduction switched on or off since)
            applyChain (s->inserts);          // (the same, as a plugin)
            for (auto& path : s->paths)
                applyChain (path->inserts);
            compileRack (*s);
        }

    for (auto& s : strips)
    {
        s->chainLatency = 0;
        if (s->source.kind != Source::app)   // a captured program's chain isn't run (processStrip)
        {
            // Noise reduction's frame: in the rack's latency when it's a unit there, else the strip's own.
            const bool noiseInRack = ((s->rack.inRack >> rackNoise) & 1u) != 0;
            s->chainLatency = s->channel != nullptr && ! noiseInRack ? s->channel->getLatencySamples() : 0;
            if (s->rackPlan != nullptr)
                s->chainLatency += s->rackPlan->latency;
        }

        // Switched off or not: switching a strip on or off then moves nobody else's compensation
        // (which clears their delay lines: a gap on every channel). An off strip with the most
        // latency still holds the others back.
        longest = juce::jmax (longest, s->chainLatency);
    }

    for (auto& s : strips)
    {
        const int wanted = juce::jlimit (0, maxCompensation, longest - s->chainLatency);

        if (wanted != s->compensation)
        {
            const juce::SpinLock::ScopedLockType sl (graphLock);
            s->compensation = wanted;
            s->delayLine.clear();
            s->delayPos = 0;
        }
    }

    // Each combinator's paths line up with its slowest path.
    for (auto& pair : buses)
    {
        int slowest = 0;
        for (auto& path : pair->paths)
        {
            path->latency = 0;
            for (auto& plugin : path->inserts)
                path->latency += plugin->getLatencySamples();
            slowest = juce::jmax (slowest, path->latency);
        }

        for (auto& path : pair->paths)
        {
            const int wanted = juce::jlimit (0, maxCompensation, slowest - path->latency);
            if (wanted != path->compensation)
            {
                const juce::SpinLock::ScopedLockType sl (graphLock);
                path->compensation = wanted;
                path->delayLine.clear();
                path->delayPos = 0;
            }
        }
    }
}

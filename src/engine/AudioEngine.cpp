#include "AudioEngine.h"
#include "BuiltInClock.h"
#include "Metering.h"
#include "VoiceChain.h"
#include "WindowDiscovery.h"

namespace
{
    constexpr int pluginMaxChannels = 16;
    constexpr int maxCompensation   = 48000;   // 1 s at 48 kHz
    constexpr int maxBlock          = 8192;    // every per-block buffer is sized for this

    // Where each index goes when the one at `removed` is taken out (-1: that one).
    std::function<int (int)> removedAt (int removed)
    {
        return [removed] (int i) { return i == removed ? -1 : i > removed ? i - 1 : i; };
    }

    // Delays the first two channels of `buf` by `delay` samples, through the ring `line`
    // (latency compensation).
    void delayBlock (juce::AudioBuffer<float>& line, int& pos, int delay, juce::AudioBuffer<float>& buf, int numSamples)
    {
        if (delay <= 0)
            return;

        const int size = line.getNumSamples();
        for (int i = 0; i < numSamples; ++i)
        {
            const int readPos = (pos - delay + size) % size;
            for (int ch = 0; ch < 2; ++ch)
            {
                line.setSample (ch, pos, buf.getSample (ch, i));
                buf.setSample (ch, i, line.getSample (ch, readPos));
            }
            pos = (pos + 1) % size;
        }
    }

    // Peak meter (see Metering.h): instant attack, `release` per block of fall-off.
    void updatePeak (std::atomic<float>& peak, float level, float release)
    {
        peak = juce::jmax (level, peak.load() * release);
    }

    // A NaN or infinity (a misbehaving plugin) becomes silence, so it can't stick in the meters,
    // filters and loudness that follow or reach a device.
    void keepFinite (juce::AudioBuffer<float>& buf, int numSamples) noexcept
    {
        for (int ch = 0; ch < juce::jmin (2, buf.getNumChannels()); ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                if (! std::isfinite (d[i]))
                    d[i] = 0.0f;
        }
    }

    // Runs a stereo buffer through one plugin. Plugins with other channel counts get a mono fold
    // or the stereo pair padded with silence, via a scratch buffer. False if it couldn't run.
    bool runPlugin (juce::AudioPluginInstance* plugin, juce::AudioBuffer<float>& scratch, juce::MidiBuffer& midi,
                    juce::AudioBuffer<float>& buf, int numSamples)
    {
        {
            const int ins = plugin->getTotalNumInputChannels();
            const int outs = plugin->getTotalNumOutputChannels();
            const int channels = juce::jmax (ins, outs);

            if (channels == 0 || channels > scratch.getNumChannels() || plugin->isSuspended())
                return false;

            scratch.clear (0, numSamples);

            if (ins == 1)
            {
                scratch.addFrom (0, 0, buf, 0, 0, numSamples, 0.5f);
                scratch.addFrom (0, 0, buf, 1, 0, numSamples, 0.5f);
            }
            else if (ins >= 2)
            {
                scratch.copyFrom (0, 0, buf, 0, 0, numSamples);
                scratch.copyFrom (1, 0, buf, 1, 0, numSamples);
            }

            juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), channels, numSamples);
            midi.clear();

            {
                const juce::ScopedLock pluginLock (plugin->getCallbackLock());
                plugin->processBlock (view, midi);
            }

            if (outs >= 2)
            {
                buf.copyFrom (0, 0, view, 0, 0, numSamples);
                buf.copyFrom (1, 0, view, 1, 0, numSamples);
            }
            else if (outs == 1)
            {
                buf.copyFrom (0, 0, view, 0, 0, numSamples);
                buf.copyFrom (1, 0, view, 0, 0, numSamples);
            }
            keepFinite (buf, numSamples);
        }
        return true;
    }

    // Runs a stereo buffer through one plugin with its insert options (AudioEngine::InsertOptions):
    // IN gain, the plugin, OUT gain, then MIX against the sound as it came in (delayed by the
    // plugin's latency to stay in time). A bypassed plugin (or every plugin, `chainOn` false: the
    // INSERT key out) isn't run; the delayed sound goes on in its place. Every change ramps
    // across the block.
    void processInsert (juce::AudioPluginInstance* plugin, const AudioEngine::InsertOptionsTable& table,
                        juce::AudioBuffer<float>& scratch, juce::MidiBuffer& midi, juce::AudioBuffer<float>& buf, int numSamples,
                        bool chainOn = true)
    {
        {
            const auto it = table.find (plugin);
            if (it == table.end())
            {
                runPlugin (plugin, scratch, midi, buf, numSamples);
                return;
            }
            auto& o = *it->second;

            for (int ch = 0; ch < 2; ++ch)
                o.dry.copyFrom (ch, 0, buf, ch, 0, numSamples);
            delayBlock (o.line, o.pos, juce::jmin (plugin->getLatencySamples(), maxCompensation), o.dry, numSamples);

            const float blendTo = o.bypassed.load() || ! chainOn ? 0.0f : o.mix.load();
            if (blendTo == 0.0f && o.blend == 0.0f)
            {
                for (int ch = 0; ch < 2; ++ch)
                    buf.copyFrom (ch, 0, o.dry, ch, 0, numSamples);
                o.secondsPerSample = 0.0f;
                return;
            }

            const float inTo = o.inGain.load(), outTo = o.outGain.load();
            if (o.in != 1.0f || inTo != 1.0f)
                for (int ch = 0; ch < 2; ++ch)
                    buf.applyGainRamp (ch, 0, numSamples, o.in, inTo);
            o.in = inTo;

            const auto started = juce::Time::getHighResolutionTicks();
            const bool ran = runPlugin (plugin, scratch, midi, buf, numSamples);
            const auto took = (float) juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - started);
            o.secondsPerSample = o.secondsPerSample.load() * 0.95f + (ran ? took / (float) numSamples : 0.0f) * 0.05f;

            if (o.out != 1.0f || outTo != 1.0f)
                for (int ch = 0; ch < 2; ++ch)
                    buf.applyGainRamp (ch, 0, numSamples, o.out, outTo);
            o.out = outTo;

            if (o.blend != 1.0f || blendTo != 1.0f)
                for (int ch = 0; ch < 2; ++ch)
                {
                    buf.applyGainRamp (ch, 0, numSamples, o.blend, blendTo);
                    buf.addFromWithRamp (ch, 0, o.dry.getReadPointer (ch), numSamples, 1.0f - o.blend, 1.0f - blendTo);
                }
            o.blend = blendTo;
        }
    }

    // A plugin chain in order (a combinator path's).
    void processInserts (std::vector<std::unique_ptr<juce::AudioPluginInstance>>& chain, const AudioEngine::InsertOptionsTable& table,
                         juce::AudioBuffer<float>& scratch, juce::MidiBuffer& midi, juce::AudioBuffer<float>& buf, int numSamples)
    {
        for (auto& plugin : chain)
            processInsert (plugin.get(), table, scratch, midi, buf, numSamples);
    }
}

//==============================================================================
// A rack's patch, compiled (compileRack) for the audio thread: its units in an order where each
// comes after everything feeding it, each feed delayed so parallel paths arrive together.
struct AudioEngine::RackPlan
{
    struct Feed
    {
        int from = -1;     // a node's index, or -1: the channel's input
        int delay = 0;     // samples, to line up with the slowest feed into the same place
        int line = -1;     // its delay line (in `lines`), or -1
    };
    struct Node
    {
        int builtin = -1;                              // a RackBuiltin, or
        juce::AudioPluginInstance* plugin = nullptr;   // a plugin
        std::vector<Feed> feeds;
    };
    struct Line { juce::AudioBuffer<float> ring; int pos = 0; };

    std::vector<Node> nodes;
    std::vector<Feed> out;                            // into the fader
    std::vector<juce::AudioBuffer<float>> buffers;    // one per node
    std::vector<Line> lines;
    juce::AudioBuffer<float> scratch, result;
    int latency = 0;                                  // the channel's input to the fader
    unsigned before = 0, after = 0;                   // the strip's own stages (not in the rack): before it, and after it
};

juce::String AudioEngine::rackUnitName (const RackUnit& u)
{
    if (u.plugin != nullptr)
        return u.plugin->getName();
    switch (u.builtin)
    {
        case rackFilters: return "FILTERS";
        case rackGate:    return "GATE";
        case rackComp:    return "COMPRESSOR";
        case rackEq:      return "EQ";
        case rackBus:     return "BUS COMP";
        case rackInput:   return "INPUT";
        case rackNoise:   return "NOISE REDUCTION";
        case rackDeess:   return "DE-ESSER";
        case rackLevel:   return "AUTO-LEVEL";
        case rackFx:      return "VOICE FX";
        case rackLimit:   return "LIMITER";
        case rackIn:      return "CHANNEL IN";
        case rackOut:     return "TO FADER";
        default:          return {};
    }
}

unsigned AudioEngine::rackStages (int builtin)
{
    switch (builtin)
    {
        case rackFilters: return VoiceChain::stageFilters;
        case rackGate:    return VoiceChain::stageGate;
        case rackComp:    return VoiceChain::stageComp;
        case rackEq:      return VoiceChain::stageEq;
        case rackBus:     return VoiceChain::stageBus;
        case rackInput:   return VoiceChain::stageInput;
        case rackNoise:   return VoiceChain::stageNoise;
        case rackDeess:   return VoiceChain::stageDeess;
        case rackLevel:   return VoiceChain::stageLevel;
        case rackFx:      return VoiceChain::stageFx;
        case rackLimit:   return VoiceChain::stageLimit;
        default:          return 0;
    }
}

namespace
{
    // The placement as one list: the left rack, then the right.
    std::vector<AudioEngine::RackUnit> inOrder (const AudioEngine::RackPatch& p)
    {
        auto all = p.left;
        all.insert (all.end(), p.right.begin(), p.right.end());
        return all;
    }

    // The cables placement makes: the channel's input through every unit in order to the fader.
    std::vector<std::pair<AudioEngine::RackUnit, AudioEngine::RackUnit>> seriesCables (const AudioEngine::RackPatch& p)
    {
        std::vector<std::pair<AudioEngine::RackUnit, AudioEngine::RackUnit>> cables;
        AudioEngine::RackUnit from { AudioEngine::rackIn, nullptr };
        for (auto& u : inOrder (p))
        {
            cables.push_back ({ from, u });
            from = u;
        }
        cables.push_back ({ from, AudioEngine::RackUnit { AudioEngine::rackOut, nullptr } });
        return cables;
    }

    // The units in an order where each comes after everything feeding it (Kahn's), and whether
    // that was possible: false, the cables loop.
    bool sortUnits (const std::vector<AudioEngine::RackUnit>& units, const std::vector<std::pair<AudioEngine::RackUnit, AudioEngine::RackUnit>>& cables,
                    std::vector<AudioEngine::RackUnit>& sorted)
    {
        sorted.clear();
        std::vector<int> waiting (units.size(), 0);
        auto indexOf = [&units] (const AudioEngine::RackUnit& u)
        {
            for (size_t i = 0; i < units.size(); ++i)
                if (units[i] == u)
                    return (int) i;
            return -1;
        };
        for (auto& [from, to] : cables)
            if (const int t = indexOf (to); t >= 0 && indexOf (from) >= 0)
                ++waiting[(size_t) t];
        std::vector<bool> done (units.size(), false);
        for (size_t round = 0; round < units.size(); ++round)
        {
            int next = -1;
            for (size_t i = 0; i < units.size() && next < 0; ++i)
                if (! done[i] && waiting[i] == 0)
                    next = (int) i;
            if (next < 0)
                return false;
            done[(size_t) next] = true;
            sorted.push_back (units[(size_t) next]);
            for (auto& [from, to] : cables)
                if (from == units[(size_t) next])
                    if (const int t = indexOf (to); t >= 0)
                        --waiting[(size_t) t];
        }
        return true;
    }
}

bool AudioEngine::rackLoops (const RackPatch& p)
{
    if (! p.manual)
        return false;
    std::vector<RackUnit> sorted;
    return ! sortUnits (inOrder (p), p.cables, sorted);
}

std::vector<AudioEngine::RackUnit> AudioEngine::signalOrder (Strip& s)
{
    const auto& p = getRack (s);
    auto outside = [&p] (int b) { return ((p.inRack >> b) & 1u) == 0; };
    std::vector<RackUnit> order;
    for (int b : { (int) rackInput, (int) rackFilters, (int) rackNoise, (int) rackGate, (int) rackComp, (int) rackEq, (int) rackBus })   // (VoiceChain's own order)
        if (outside (b))
            order.push_back ({ b, nullptr });
    std::vector<RackUnit> sorted;
    if (! p.manual || ! sortUnits (inOrder (p), p.cables, sorted))
        sorted = inOrder (p);
    order.insert (order.end(), sorted.begin(), sorted.end());
    for (int b : { (int) rackDeess, (int) rackLevel, (int) rackFx, (int) rackLimit })
        if (outside (b))
            order.push_back ({ b, nullptr });
    return order;
}

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

// Placement holds every built-in unit and plugin, and nothing else: gone plugins leave (their
// cables too); new ones go at the bottom of the right rack. The plugins' slots in the placement
// take them in `inserts` order, so moving plugins in the chain (insert slots, the rack's keys)
// moves them in the rack.
void AudioEngine::syncRack (Strip& s)
{
    auto& p = s.rack;
    auto present = [&s, &p] (const RackUnit& u)
    {
        if (u.plugin == nullptr)   // (a built-in unit: only those moved into the rack)
            return juce::isPositiveAndBelow (u.builtin, (int) rackBuiltins) && ((p.inRack >> u.builtin) & 1u) != 0;
        return std::any_of (s.inserts.begin(), s.inserts.end(), [&u] (auto& i) { return i.get() == u.plugin; });
    };
    for (auto* side : { &p.left, &p.right })
        side->erase (std::remove_if (side->begin(), side->end(), [&] (const RackUnit& u) { return ! present (u); }), side->end());
    p.cables.erase (std::remove_if (p.cables.begin(), p.cables.end(), [&] (const auto& c)
    {
        auto ok = [&] (const RackUnit& u) { return u.builtin == rackIn || u.builtin == rackOut || present (u); };
        return ! ok (c.first) || ! ok (c.second);
    }), p.cables.end());

    const auto placed = inOrder (p);
    auto isPlaced = [&placed] (const RackUnit& u) { return std::find (placed.begin(), placed.end(), u) != placed.end(); };
    // Missing built-ins (a new channel, or a rack from before they were all units). The stages
    // that used to run outside the patch go where they ran: INPUT and NOISE REDUCTION at the top
    // of the left rack, straight after the channel's input; DE-ESSER, AUTO-LEVEL, VOICE FX and
    // LIMITER at the bottom of the right, straight before the fader. The others, the left rack in
    // their own order. A hand-patched rack has them cabled into those places.
    std::vector<int> atTop, atEnd;
    auto missing = [&] (int b) { return ((p.inRack >> b) & 1u) != 0 && ! isPlaced ({ b, nullptr }); };
    for (int b : { (int) rackInput, (int) rackNoise })
        if (missing (b)) atTop.push_back (b);
    for (int b : { (int) rackDeess, (int) rackLevel, (int) rackFx, (int) rackLimit })
        if (missing (b)) atEnd.push_back (b);
    std::vector<int> others;
    for (int b = 0; b < rackInput; ++b)
        if (missing (b))
        {
            others.push_back (b);
            p.left.insert (p.left.begin() + juce::jmin (b, (int) p.left.size()), RackUnit { b, nullptr });
        }
    for (size_t i = 0; i < atTop.size(); ++i)
        p.left.insert (p.left.begin() + (int) i, RackUnit { atTop[i], nullptr });
    for (int b : atEnd)
        p.right.push_back ({ b, nullptr });
    if (p.manual)
    {
        auto splice = [&p] (const std::vector<int>& units, bool afterInput)
        {
            if (units.empty())
                return;
            const RackUnit jack { afterInput ? rackIn : rackOut, nullptr };
            const RackUnit first { units.front(), nullptr }, last { units.back(), nullptr };
            for (auto& c : p.cables)   // what the input fed (or what fed the fader) now goes through them
            {
                if (afterInput && c.first == jack)    c.first = last;
                if (! afterInput && c.second == jack) c.second = first;
            }
            p.cables.push_back (afterInput ? std::pair (jack, first) : std::pair (last, jack));
            for (size_t i = 0; i + 1 < units.size(); ++i)
                p.cables.push_back ({ RackUnit { units[i], nullptr }, RackUnit { units[i + 1], nullptr } });
        };
        // The others go in after the input's (they'd otherwise sit in the rack with nothing patched
        // into them, and silently stop running).
        auto afterInput = atTop;
        afterInput.insert (afterInput.end(), others.begin(), others.end());
        splice (afterInput, true);
        splice (atEnd, false);
    }
    // New plugins: the right rack, above the output stages at its bottom.
    for (auto& plugin : s.inserts)
        if (! isPlaced ({ -1, plugin.get() }))
        {
            auto at = p.right.end();
            while (at != p.right.begin() && std::prev (at)->plugin == nullptr
                   && (std::prev (at)->builtin == rackDeess || std::prev (at)->builtin == rackLevel
                       || std::prev (at)->builtin == rackFx || std::prev (at)->builtin == rackLimit))
                --at;
            p.right.insert (at, { -1, plugin.get() });
        }

    // The plugins' slots, refilled in chain order.
    size_t next = 0;
    for (auto* side : { &p.left, &p.right })
        for (auto& u : *side)
            if (u.plugin != nullptr && next < s.inserts.size())
                u.plugin = s.inserts[next++].get();
}

const AudioEngine::RackPatch& AudioEngine::getRack (Strip& s)
{
    syncRack (s);
    return s.rack;
}

void AudioEngine::moveBuiltin (Strip& s, int builtin, bool intoRack)
{
    if (! juce::isPositiveAndBelow (builtin, (int) rackBuiltins) || (((s.rack.inRack >> builtin) & 1u) != 0) == intoRack)
        return;
    auto p = getRack (s);
    const RackUnit unit { builtin, nullptr };
    if (intoRack)
    {
        p.inRack |= 1u << builtin;
        p.left.push_back (unit);
        if (p.manual)   // just before the fader: what fed it goes through this first
        {
            const RackUnit fader { rackOut, nullptr };
            for (auto& c : p.cables)
                if (c.second == fader)
                    c.second = unit;
            p.cables.push_back ({ unit, fader });
        }
    }
    else
    {
        p.inRack &= ~(1u << builtin);
        p.left.erase (std::remove (p.left.begin(), p.left.end(), unit), p.left.end());
        p.right.erase (std::remove (p.right.begin(), p.right.end(), unit), p.right.end());
        if (p.manual)   // what fed it now feeds what it fed
        {
            std::vector<std::pair<RackUnit, RackUnit>> joined;
            for (auto& in : p.cables)
                if (in.second == unit)
                    for (auto& out : p.cables)
                        if (out.first == unit && std::find (p.cables.begin(), p.cables.end(), std::pair (in.first, out.second)) == p.cables.end())
                            joined.push_back ({ in.first, out.second });
            p.cables.erase (std::remove_if (p.cables.begin(), p.cables.end(), [&unit] (auto& c) { return c.first == unit || c.second == unit; }), p.cables.end());
            p.cables.insert (p.cables.end(), joined.begin(), joined.end());
        }
    }
    setRack (s, p);
}

std::vector<std::pair<AudioEngine::RackUnit, AudioEngine::RackUnit>> AudioEngine::rackCables (Strip& s)
{
    syncRack (s);
    return s.rack.manual ? s.rack.cables : seriesCables (s.rack);
}

bool AudioEngine::setRack (Strip& s, RackPatch p)
{
    if (rackLoops (p))
        return false;

    // A cable patched twice is one cable (two would sum its sound twice, +6 dB).
    std::vector<std::pair<RackUnit, RackUnit>> unique;
    for (auto& c : p.cables)
        if (std::find (unique.begin(), unique.end(), c) == unique.end())
            unique.push_back (c);
    p.cables = std::move (unique);

    // The chain follows the placement: the plugins in `inserts` in the order they're placed.
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> chain;
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        for (auto& u : inOrder (p))
            for (auto& plugin : s.inserts)
                if (plugin != nullptr && plugin.get() == u.plugin)
                    chain.push_back (std::move (plugin));
        for (auto& plugin : s.inserts)   // any the placement missed keep their place at the end
            if (plugin != nullptr)
                chain.push_back (std::move (plugin));
        s.inserts = std::move (chain);
    }
    const auto before = s.rack;
    s.rack = std::move (p);
    ++s.rackRevision;
    refreshLatency();   // (compiles the rack)
    if (onRackChanged)
        onRackChanged (s, before);
    return true;
}

// Compiles a channel's patch for the audio thread, and swaps it in. Message thread.
void AudioEngine::compileRack (Strip& s)
{
    syncRack (s);
    const auto cables = s.rack.manual ? s.rack.cables : seriesCables (s.rack);

    // Only when something changed: the units, the cables, a plugin's latency or the block size.
    // (A new plan starts its delay lines empty, so compiling for nothing would drop the delayed
    // branches of a parallel patch for a moment, and take the graph lock.)
    juce::String key;
    // A unit's latency: a plugin's own; NOISE REDUCTION's frame (the channel's VoiceChain reports
    // it); the other built-in units have none.
    auto latencyOf = [&s] (const RackUnit& u)
    {
        if (u.plugin != nullptr)       return u.plugin->getLatencySamples();
        if (u.builtin == rackNoise)    return s.channel != nullptr ? s.channel->getLatencySamples() : 0;
        return 0;
    };
    auto unitKey = [&latencyOf] (const RackUnit& u) { return juce::String (u.builtin) + ":" + juce::String::toHexString ((juce::pointer_sized_int) u.plugin)
                                                             + ":" + juce::String (latencyOf (u)); };
    for (auto& u : inOrder (s.rack))
        key << unitKey (u) << ",";
    key << "|";
    for (auto& [from, to] : cables)
        key << unitKey (from) << ">" << unitKey (to) << ",";
    key << "|" << maxBlock << "|" << interfaceRate << "|" << (int) s.rack.inRack;
    if (s.rackPlan != nullptr && key == s.rackCompiled)
        return;

    std::vector<RackUnit> sorted;
    if (! sortUnits (inOrder (s.rack), cables, sorted))
        return;   // (setRack refuses loops; keep what runs)
    s.rackCompiled = key;

    auto plan = std::make_shared<RackPlan>();
    std::vector<int> latency;   // each node's, from the channel's input to its output
    auto nodeOf = [&plan] (const RackUnit& u)
    {
        for (size_t i = 0; i < plan->nodes.size(); ++i)
            if (plan->nodes[i].builtin == (u.plugin != nullptr ? -1 : u.builtin) && plan->nodes[i].plugin == u.plugin)
                return (int) i;
        return -1;
    };
    // The feeds into `to`, each delayed to the slowest of them. Returns their latency.
    auto feedsInto = [&] (const RackUnit& to, std::vector<RackPlan::Feed>& feeds)
    {
        int slowest = 0;
        for (auto& [from, dest] : cables)
            if (dest == to)
            {
                RackPlan::Feed f;
                f.from = from.builtin == rackIn ? -1 : nodeOf (from);
                if (f.from == -1 && from.builtin != rackIn)
                    continue;   // from a unit nothing reaches: silent
                feeds.push_back (f);
                slowest = juce::jmax (slowest, f.from < 0 ? 0 : latency[(size_t) f.from]);
            }
        for (auto& f : feeds)
        {
            f.delay = juce::jmin (maxCompensation, slowest - (f.from < 0 ? 0 : latency[(size_t) f.from]));
            if (f.delay > 0)
            {
                f.line = (int) plan->lines.size();
                plan->lines.emplace_back();
                plan->lines.back().ring.setSize (2, f.delay + maxBlock);
                plan->lines.back().ring.clear();
            }
        }
        return slowest;
    };

    for (auto& u : sorted)
    {
        RackPlan::Node node;
        node.builtin = u.plugin != nullptr ? -1 : u.builtin;
        node.plugin = u.plugin;
        const int in = feedsInto (u, node.feeds);
        if (node.feeds.empty())
            continue;   // nothing patched into it: it isn't run
        plan->nodes.push_back (std::move (node));
        latency.push_back (in + latencyOf (u));
    }
    plan->latency = feedsInto ({ rackOut, nullptr }, plan->out);
    plan->buffers.resize (plan->nodes.size());
    for (auto& b : plan->buffers)
        b.setSize (2, maxBlock);
    plan->scratch.setSize (2, maxBlock);
    plan->result.setSize (2, maxBlock);
    for (int b = 0; b < rackBuiltins; ++b)
        if (((s.rack.inRack >> b) & 1u) == 0)
            (b == rackDeess || b == rackLevel || b == rackFx || b == rackLimit ? plan->after : plan->before) |= rackStages (b);

    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        std::swap (s.rackPlan, plan);
    }
    // (the old plan goes here, off the audio thread)
}

// Audio thread: what a channel's dynamics hear this block besides itself. KEY: the keying
// channel's sound (after its processing), this block's, or its last if that channel runs after
// this one. A keying channel switched off keys nothing (its tap would be stale).
void AudioEngine::keyChain (Strip& s, VoiceChain& chain, int numSamples)
{
    if (const int k = s.keySource.load(); juce::isPositiveAndBelow (k, (int) strips.size()) && strips[(size_t) k].get() != &s
                                               && strips[(size_t) k]->enabled.load()
                                               && strips[(size_t) k]->keyTap.getNumSamples() >= numSamples)
        chain.setKey (strips[(size_t) k]->keyTap.getReadPointer (0), strips[(size_t) k]->keyTap.getReadPointer (1), numSamples);
    else
        chain.setKey (nullptr, nullptr, 0);
}

// Audio thread: a channel's processing, its rack in the middle (see RackPatch).
void AudioEngine::runRack (Strip& s, juce::AudioBuffer<float>& buf, int numSamples)
{
    // AUDITION: the outputs get the committed copy; the edited chain runs on the side (below).
    auto* edited = s.channel.get();
    auto* committed = s.committed.get();
    auto* chain = committed != nullptr ? committed : edited;
    const bool auditioning = committed != nullptr && edited != nullptr && s.auditionBuf.getNumSamples() >= numSamples;
    if (auditioning)
        for (int ch = 0; ch < 2; ++ch)
            s.auditionBuf.copyFrom (ch, 0, buf, ch, 0, numSamples);
    juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, numSamples);
    auto* plan = s.rackPlan.get();
    if (chain != nullptr)
    {
        keyChain (s, *chain, numSamples);
        chain->beginBlock (numSamples);
        chain->inputTap = s.tapInput ? &s.rawBuffer : nullptr;   // (RAW sends: after the input stage, wherever it runs)
        chain->sourceStereo = s.source.stereo;
        // The strip's own stages (those not moved into the rack) ahead of it; with no rack yet, all of them.
        if (const unsigned before = plan != nullptr ? plan->before : (unsigned) VoiceChain::allStages; before != 0)
            chain->runStages (view, before);
    }

    if (plan != nullptr)
    {
        auto gather = [&] (const std::vector<RackPlan::Feed>& feeds, juce::AudioBuffer<float>& into)
        {
            for (int ch = 0; ch < 2; ++ch)
                into.clear (ch, 0, numSamples);
            for (auto& f : feeds)
            {
                const auto& from = f.from < 0 ? buf : plan->buffers[(size_t) f.from];
                if (f.line < 0)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        into.addFrom (ch, 0, from, ch, 0, numSamples);
                    continue;
                }
                for (int ch = 0; ch < 2; ++ch)
                    plan->scratch.copyFrom (ch, 0, from, ch, 0, numSamples);
                auto& line = plan->lines[(size_t) f.line];
                delayBlock (line.ring, line.pos, f.delay, plan->scratch, numSamples);
                for (int ch = 0; ch < 2; ++ch)
                    into.addFrom (ch, 0, plan->scratch, ch, 0, numSamples);
            }
        };

        const bool pluginsOn = s.insertsOn.load();
        for (size_t i = 0; i < plan->nodes.size(); ++i)
        {
            auto& node = plan->nodes[i];
            auto& b = plan->buffers[i];
            gather (node.feeds, b);
            juce::AudioBuffer<float> nodeView (b.getArrayOfWritePointers(), 2, numSamples);
            if (node.plugin != nullptr)
                processInsert (node.plugin, optionsTable, s.pluginBuffer, s.midi, nodeView, numSamples, pluginsOn);
            else if (chain != nullptr)
                chain->runStages (nodeView, rackStages (node.builtin));
        }
        gather (plan->out, plan->result);
        for (int ch = 0; ch < 2; ++ch)
            buf.copyFrom (ch, 0, plan->result, ch, 0, numSamples);
    }

    if (chain != nullptr)
    {
        if (plan != nullptr && plan->after != 0)   // the strip's own output stages, after the rack
            chain->runStages (view, plan->after);
        chain->endBlock (view);
        chain->inputTap = nullptr;
    }

    if (auditioning)   // the edited settings, every stage, for the control room alone
    {
        juce::AudioBuffer<float> side (s.auditionBuf.getArrayOfWritePointers(), 2, numSamples);
        keyChain (s, *edited, numSamples);
        edited->sourceStereo = s.source.stereo;
        edited->beginBlock (numSamples);
        edited->runStages (side, VoiceChain::allStages);
        edited->endBlock (side);
    }
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

struct AudioEngine::Secondary : public juce::AudioIODeviceCallback
{
    juce::String name;
    bool isOutput = true;
    std::unique_ptr<juce::AudioIODevice> device;
    DriftFifo fifo;   // output: master pushes, device pulls. input: device pushes, master pulls.
    int channels = 2;  // all the device has (a 16-channel cable: 16), each output or input using a pair of them

    // Output, audio thread: every output bus playing here is summed into `mix` on its own
    // channels, which is pushed to the fifo once per block (several outputs can share one device).
    juce::AudioBuffer<float> mix { 2, maxBlock };
    bool mixPending = false;

    // Input, audio thread: the block read from the fifo once (`pulledAt`: which block), for
    // every strip taking its own channels from it.
    juce::AudioBuffer<float> inBlock { 2, maxBlock };
    std::uint64_t pulledAt = 0;

    ~Secondary() override
    {
        if (device != nullptr)
            device->close();
    }

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override
    {
        if (isOutput)
            fifo.pull (outputs, numOutputs, numSamples);
        else
            fifo.push (inputs, numInputs, numSamples);
    }

    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
};

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
            bus->target.clear();
            for (auto& d : bus->alsoOn)
            {
                d.output = nullptr;
                d.toInterface = false;
                d.target.clear();
            }
        }
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
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        strips.push_back (std::move (strip));
    }
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
        refreshDucking();   // (ducks, mix-minus and the cue output are kept by position: in the same hold)
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
        refreshDucking();   // (ducks, mix-minus and the cue output are kept by position: in the same hold)
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
        {
            const juce::SpinLock::ScopedLockType sl (graphLock);
            strip->source = chosen;
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

std::pair<float, float> AudioEngine::stepFade (float& level, bool on, int numSamples) const
{
    const float start = level;
    const float step = (float) numSamples / (float) (interfaceRate * (on ? fadeInSeconds : fadeOutSeconds));
    level = on ? juce::jmin (1.0f, level + step) : juce::jmax (0.0f, level - step);
    return { start, level };
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

// Picking an output as a source means "listen to it", so switch its loopback on. The output's
// LOOPBACK button can still turn it off, which silences every strip fed from it.
void AudioEngine::enableLoopbackFor (const Source& source)
{
    if (source.kind == Source::loopback && juce::isPositiveAndBelow (source.masterIndex, (int) masters.size()))
        masters[(size_t) source.masterIndex]->loopback = true;
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
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        masters.push_back (std::move (bus));
    }
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
        refreshDucking();   // (mix-minus and the cue output are kept by position: in the same hold)
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
        auto& strip = addStrip (juce::File (rule.exe).getFileNameWithoutExtension(), { Source::app, 0, rule.exe, true });
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

void AudioEngine::setStripEnabled (Strip& strip, bool enabled)
{
    strip.enabled = enabled;
    strip.peakL = strip.peakR = 0.0f;
    strip.levels.meterL = strip.levels.meterR = 0.0f;   // not processed while off, so its meters would freeze
    log ((strip.source.kind == Source::bus ? "bus " : "input ") + strip.name + (enabled ? ": enabled" : ": disabled"));
    refreshDevices();   // opens or releases its device or app capture
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
        {
            const juce::SpinLock::ScopedLockType sl (graphLock);
            bus->candidates = candidates;
            bus->firstChannel = firstChannel;
            bus->stereo = stereo;
            bus->output = nullptr;
            bus->toInterface = false;

            // Its other devices follow mono/stereo (master channels in buses), and drop any that is
            // now its own device. No device at all: silent everywhere.
            if (candidates.isEmpty())
                bus->alsoOn.clear();
            for (auto& d : bus->alsoOn)
                if (stereo && d.candidate == interfaceOutput)
                    d.firstChannel -= d.firstChannel % 2;
            if (! candidates.isEmpty())
                bus->alsoOn.erase (std::remove_if (bus->alsoOn.begin(), bus->alsoOn.end(), [&] (const Master::Device& d)
                                   { return samePlace (d.candidate, d.firstChannel, candidates[0], firstChannel, stereo); }), bus->alsoOn.end());
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

    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        bus.alsoOn.push_back ({ candidate, bus.stereo && candidate == interfaceOutput ? firstChannel - firstChannel % 2 : firstChannel });
    }
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

    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        bus.alsoOn.erase (std::remove_if (bus.alsoOn.begin(), bus.alsoOn.end(), [&] (const Master::Device& d)
                          { return samePlace (d.candidate, d.firstChannel, candidate, firstChannel, bus.stereo); }), bus.alsoOn.end());
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
        const juce::SpinLock::ScopedLockType sl (graphLock);
        optionsTable[made.get()] = std::move (options);
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
        error = "no VST3 plugin found in " + pluginPath;
        return nullptr;
    }

    auto plugin = vst3.createInstanceFromDescription (*types[0], interfaceRate, interfaceBlock, error);

    if (plugin == nullptr)
    {
        if (error.isEmpty())
            error = "plugin failed to load";
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
        error = plugin->getName() + " needs more than " + juce::String (pluginMaxChannels) + " channels";
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
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        chain.push_back (std::move (plugin));
    }

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
    removed->releaseResources();
    forgetOptions (removed.get());
}

// A plugin gone for good: its IN/OUT/MIX settings and their delay lines (about half a megabyte)
// go too, taken out under the lock and freed after it.
void AudioEngine::forgetOptions (const juce::AudioPluginInstance* plugin)
{
    std::unique_ptr<InsertOptions> gone;
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        if (const auto it = optionsTable.find (plugin); it != optionsTable.end())
        {
            gone = std::move (it->second);
            optionsTable.erase (it);
        }
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

juce::String AudioEngine::addInsert (int i, const juce::String& path)
{
    if (! juce::isPositiveAndBelow (i, (int) strips.size()))
        return "no such input";
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
        return "no such bus";
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
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        buses.push_back (std::move (pair));
        updateBusOrder();
    }
    structureAdded ("bus", (int) buses.size() - 1, -1);
    return raw;
}

AudioEngine::Strip* AudioEngine::addCombinator (const juce::String& name, int paths)
{
    auto* pair = addBus (name);
    if (pair == nullptr)
        return nullptr;

    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        pair->pathSum.setSize (2, maxBlock);
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

    const juce::SpinLock::ScopedLockType sl (graphLock);
    buses[(size_t) busIndex]->paths.push_back (std::move (path));
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
        return "no such path";

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
        updateBusOrder();
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

    const juce::SpinLock::ScopedLockType sl (graphLock);
    buses[(size_t) fromBus]->busSends[(size_t) toBus] = on;
    updateBusOrder();
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

// Topological order (call with graphLock held): each pair comes after every pair that feeds it.
void AudioEngine::updateBusOrder()
{
    const int count = (int) buses.size();
    std::vector<int> incoming ((size_t) count, 0);

    for (int a = 0; a < count; ++a)
        for (int b = 0; b < count; ++b)
            if (a != b && buses[(size_t) a]->busSends[(size_t) b])
                ++incoming[(size_t) b];

    busOrder.clear();
    std::vector<int> ready;
    for (int p = 0; p < count; ++p)
        if (incoming[(size_t) p] == 0)
            ready.push_back (p);

    while (! ready.empty())
    {
        const int p = ready.front();
        ready.erase (ready.begin());
        busOrder.push_back (p);

        for (int b = 0; b < count; ++b)
            if (b != p && buses[(size_t) p]->busSends[(size_t) b] && --incoming[(size_t) b] == 0)
                ready.push_back (b);
    }
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

        // Already capturing a process that's still running: skip the lookup (a scan of every
        // process and audio session, ~10 ms on the message thread).
        if (it != appCaptures.end() && processIsRunning (it->second->getProcessId()))
        {
            if (strip->app != it->second.get())
            {
                const juce::SpinLock::ScopedLockType sl (graphLock);
                strip->app = it->second.get();
            }
            continue;
        }

        const auto pid = WindowDiscovery::isIsolated (strip->source.deviceName) ? WindowDiscovery::findIsolatedProcess (strip->source.deviceName)
                                                                               : findAppProcess (strip->source.deviceName);

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

            if (opened != nullptr)
            {
                log ("engine: capturing " + strip->source.deviceName + " (process " + juce::String (pid) + ")");
                capture = opened.get();

                {
                    const juce::SpinLock::ScopedLockType sl (graphLock);
                    for (auto& s : strips)
                        if (s->source.kind == Source::app && s->enabled && s->source.deviceName.toLowerCase() == key)
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

//==============================================================================
// `soloedOut`: another input is soloed and this one isn't, so it's silenced like a mute.
void AudioEngine::processStrip (Strip& s, const float* const* inputs, int numInputs, int numSamples, bool soloedOut)
{
    auto& buf = s.buffer;
    buf.clear (0, numSamples);

    switch (s.source.kind)
    {
        case Source::interfaceInputs:
        {
            const int left = s.source.firstChannel;
            const bool hasRight = s.source.stereo && left + 1 < numInputs && inputs[left + 1] != nullptr;

            if (juce::isPositiveAndBelow (left, numInputs) && inputs[left] != nullptr)
            {
                buf.copyFrom (0, 0, inputs[left], numSamples);
                buf.copyFrom (1, 0, inputs[hasRight ? left + 1 : left], numSamples);
            }
            break;
        }

        case Source::device:
            if (s.input != nullptr)
            {
                // The device read once this block, whichever strip asks first; each strip takes
                // its own channel (pair) from it. A pair past the device's end: its first pair.
                auto& in = *s.input;
                if (in.pulledAt != blockSerial)
                {
                    in.fifo.pull (in.inBlock.getArrayOfWritePointers(), in.channels, numSamples);
                    in.pulledAt = blockSerial;
                }
                const bool pair = s.source.stereo && in.channels > 1;
                const int left = s.source.firstChannel >= 0 && s.source.firstChannel + (pair ? 1 : 0) < in.channels ? s.source.firstChannel : 0;
                buf.copyFrom (0, 0, in.inBlock, left, 0, numSamples);
                buf.copyFrom (1, 0, in.inBlock, pair ? left + 1 : left, 0, numSamples);
            }
            break;

        case Source::loopback:
            if (juce::isPositiveAndBelow (s.source.masterIndex, (int) masters.size()))
            {
                auto& bus = *masters[(size_t) s.source.masterIndex];

                if (bus.loopback && bus.enabled)   // disabled: lastBlock is stale, so stay silent
                {
                    const int n = juce::jmin (numSamples, bus.lastBlockSamples);
                    buf.copyFrom (0, 0, bus.lastBlock, 0, 0, n);
                    buf.copyFrom (1, 0, bus.lastBlock, 1, 0, n);

                    if (! s.source.stereo)
                    {
                        buf.addFrom (0, 0, buf, 1, 0, n);
                        buf.applyGain (0, 0, n, 0.5f);
                        buf.copyFrom (1, 0, buf, 0, 0, n);
                    }
                }
            }
            break;

        case Source::app:
            if (s.app != nullptr)
            {
                auto& app = *s.app;   // read once this block, whichever strip asks first (as devices)
                if (app.pulledAt != blockSerial)
                {
                    app.fifo.pull (app.inBlock.getArrayOfWritePointers(), 2, numSamples);
                    app.pulledAt = blockSerial;
                }
                buf.copyFrom (0, 0, app.inBlock, 0, 0, numSamples);
                buf.copyFrom (1, 0, app.inBlock, 1, 0, numSamples);

                if (! s.source.stereo)
                {
                    buf.addFrom (0, 0, buf, 1, 0, numSamples);
                    buf.applyGain (0, 0, numSamples, 0.5f);
                    buf.copyFrom (1, 0, buf, 0, 0, numSamples);
                }
            }
            break;

        case Source::bus:
            buf.copyFrom (0, 0, s.busInput, 0, 0, numSamples);
            buf.copyFrom (1, 0, s.busInput, 1, 0, numSamples);

            if (! s.source.stereo)
            {
                buf.addFrom (0, 0, buf, 1, 0, numSamples);
                buf.applyGain (0, 0, numSamples, 0.5f);
                buf.copyFrom (1, 0, buf, 0, 0, numSamples);
            }

            // Combinator: each path runs what was sent to it; the paths' sum goes on.
            if (s.combinator && ! s.paths.empty())
            {
                s.pathSum.clear (0, numSamples);

                for (auto& path : s.paths)
                {
                    auto& p = *path;
                    for (int ch = 0; ch < 2; ++ch)
                        p.buffer.copyFrom (ch, 0, p.input, ch, 0, numSamples);
                    if (! s.source.stereo)
                    {
                        p.buffer.addFrom (0, 0, p.buffer, 1, 0, numSamples);
                        p.buffer.applyGain (0, 0, numSamples, 0.5f);
                        p.buffer.copyFrom (1, 0, p.buffer, 0, 0, numSamples);
                    }

                    processInserts (p.inserts, optionsTable, p.pluginBuffer, p.midi, p.buffer, numSamples);
                    delayBlock (p.delayLine, p.delayPos, p.compensation, p.buffer, numSamples);

                    p.buffer.applyGain (0, numSamples, p.mute ? 0.0f : p.gain.load());
                    updatePeak (p.peakL, p.buffer.getMagnitude (0, 0, numSamples), meterRelease);
                    updatePeak (p.peakR, p.buffer.getMagnitude (1, 0, numSamples), meterRelease);
                    p.levels.process (p.buffer, numSamples, s.source.stereo, interfaceRate);
                    for (int ch = 0; ch < 2; ++ch)
                        s.pathSum.addFrom (ch, 0, p.buffer, ch, 0, numSamples);
                }

                for (int ch = 0; ch < 2; ++ch)
                    buf.copyFrom (ch, 0, s.pathSum, ch, 0, numSamples);
            }
            break;

        case Source::none:
            break;

        case Source::tape:   // rendered once, however many strips play it
            if (! tapeRendered)
            {
                tape.render (tapeMix, numSamples, interfaceRate);
                tapeRendered = true;
            }
            for (int ch = 0; ch < 2; ++ch)
                buf.copyFrom (ch, 0, tapeMix, ch, 0, numSamples);
            break;

        case Source::pads:   // the PADS strip, unless the pads play into another input
            if (! padsElsewhere)
            {
                renderPads (numSamples);
                for (int ch = 0; ch < 2; ++ch)
                    buf.copyFrom (ch, 0, padsMix, ch, 0, numSamples);
            }
            break;

        case Source::tone:
        {
            const double delta = juce::MathConstants<double>::twoPi * 440.0 / interfaceRate;

            for (int i = 0; i < numSamples; ++i)
            {
                const auto v = (float) (0.125 * std::sin (s.tonePhase));   // -18 dBFS
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
                s.tonePhase = std::fmod (s.tonePhase + delta, juce::MathConstants<double>::twoPi);
            }
            break;
        }
    }

    // The input the pads play into: mixed in ahead of its plugins, so they go through its chain and sends.
    if (s.padsInto.load())
    {
        renderPads (numSamples);
        for (int ch = 0; ch < 2; ++ch)
            buf.addFrom (ch, 0, padsMix, ch, 0, numSamples);
    }
    keepFinite (buf, numSamples);   // (a device or app can hand over a NaN too: kept out of the meters and rack)

    // The source as read, before any processing: its meter (MASTER's record devices), and what
    // RAW sends carry. Off air the same way the fader is: mute and solo silence it.
    updatePeak (s.sourcePeakL, buf.getMagnitude (0, 0, numSamples), meterRelease);
    updatePeak (s.sourcePeakR, buf.getMagnitude (1, 0, numSamples), meterRelease);
    s.sourceLevels.process (buf, numSamples, s.source.stereo, interfaceRate);
    const int point = meterPoint.load();
    if (point == 0)   // IN: the channel's meter reads the source as it comes in
    {
        updatePeak (s.peakL, buf.getMagnitude (0, 0, numSamples), meterRelease);
        updatePeak (s.peakR, buf.getMagnitude (1, 0, numSamples), meterRelease);
        s.levels.process (buf, numSamples, s.source.stereo, interfaceRate);
    }
    if (s.mute || soloedOut)
        s.rawBuffer.clear (0, numSamples);
    else
        for (int ch = 0; ch < 2; ++ch)
            s.rawBuffer.copyFrom (ch, 0, buf, ch, 0, numSamples);

    // A captured program (browser, game, player) has assign, mute, cue and DUCK UNDER only: its
    // EQ, dynamics, inserts, pan and fader are switched out (kept, but not run).
    const bool program = s.source.kind == Source::app;

    // The channel's processing: its rack (built-in units and plugins, as patched).
    if (! program)
    {
        s.tapInput = ! (s.mute || soloedOut);
        runRack (s, buf, numSamples);
        keepFinite (buf, numSamples);   // (the built-in stages too, not only plugins)
    }

    // Delay this strip so every strip lines up with the one whose plugins add the most latency.
    delayBlock (s.delayLine, s.delayPos, s.compensation, buf, numSamples);

    // Auto-duck, keyed by its input's level after that input's fader (last block's, if that
    // strip comes later in the order). Down in about 20 ms, held 150 ms, back over the release.
    // An app ducks too (its key level is read the same way: no wait on its capture).
    {
        float target = 1.0f;
        const int key = s.duckSource.load();
        if (juce::isPositiveAndBelow (key, (int) strips.size()))
        {
            const auto& k = *strips[(size_t) key];
            const float level = k.keyLevel.load();
            if (k.enabled && level > juce::Decibels::decibelsToGain (s.duckThreshold.load()))
                s.duckHold = (int) (0.15 * interfaceRate);
            else
                s.duckHold = juce::jmax (0, s.duckHold - numSamples);
            if (s.duckHold > 0)
                target = juce::Decibels::decibelsToGain (s.duckDepth.load());
        }

        const float from = s.duckGain.load();
        const double ms = target < from ? 20.0 : juce::jmax (10.0f, s.duckRelease.load());
        const float to = from + (target - from) * (float) (1.0 - std::exp (-numSamples / (ms * 0.001 * interfaceRate)));
        s.duckGain = std::abs (to - target) < 0.0005f ? target : to;
        if (from < 1.0f || to < 1.0f)
            for (int ch = 0; ch < 2; ++ch)
                buf.applyGainRamp (ch, 0, numSamples, from, s.duckGain.load());
    }

    // Its source being changed: down to silence before the switch, back up after (setStripSource).
    if (s.sourceFadeRestart.exchange (false))
        s.sourceFade = 0.0f;
    if (const auto [from, to] = stepFade (s.sourceFade, ! s.sourceFadeOut.load(), numSamples); from < 1.0f || to < 1.0f)
    {
        buf.applyGainRamp (0, numSamples, from, to);
        s.rawBuffer.applyGainRamp (0, numSamples, from, to);
    }

    // (Its sound here, after the processing and before the fader, for any strip keyed by it.)
    if (s.keyTap.getNumSamples() >= numSamples)
        for (int ch = 0; ch < 2; ++ch)
            s.keyTap.copyFrom (ch, 0, buf, ch, 0, numSamples);

    // AUDITION: the edited sound (or with B the committed one) for the control room, alone.
    if (s.committed != nullptr && s.auditionBuf.getNumSamples() >= numSamples && auditionMix.getNumSamples() >= numSamples)
    {
        for (int ch = 0; ch < 2; ++ch)
            auditionMix.addFrom (ch, 0, s.auditionB.load() ? buf : s.auditionBuf, ch, 0, numSamples);
        auditionThisBlock = true;
    }

    if (point == 1)   // PRE FADER: after the processing, before the fader, pan and ON
    {
        updatePeak (s.peakL, buf.getMagnitude (0, 0, numSamples), meterRelease);
        updatePeak (s.peakR, buf.getMagnitude (1, 0, numSamples), meterRelease);
        s.levels.process (buf, numSamples, s.source.stereo, interfaceRate);
    }

    // PRE sends' tap: after the processing, before the fader and pan (CUT mutes it too).
    if (std::any_of (s.preSends.begin(), s.preSends.begin() + (std::ptrdiff_t) masters.size(), [] (auto& x) { return x.load(); }))
    {
        for (int ch = 0; ch < 2; ++ch)
            s.preBuffer.copyFrom (ch, 0, buf, ch, 0, numSamples);
        if (s.mute.load())
            s.preBuffer.clear (0, numSamples);
    }

    const float gain = s.mute || soloedOut ? 0.0f : program ? 1.0f : s.gain.load();
    const float pan = program ? 0.0f : s.pan.load();

    // WIDTH (stereo): the sides narrowed (to mono at 0) or widened (to twice at 2), before the balance.
    if (const float w = s.width.load(); s.source.stereo && ! program && std::abs (w - 1.0f) > 0.001f)
    {
        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            const float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]) * w;
            l[i] = mid + side;
            r[i] = mid - side;
        }
    }

    if (s.source.stereo)
    {
        // Balance: turning right attenuates left, and vice versa.
        buf.applyGain (0, 0, numSamples, gain * juce::jmin (1.0f, 1.0f - pan));
        buf.applyGain (1, 0, numSamples, gain * juce::jmin (1.0f, 1.0f + pan));
    }
    else
    {
        // Mono: inserts may have widened it, so fold to one channel, then constant-power pan.
        buf.addFrom (0, 0, buf, 1, 0, numSamples);
        buf.applyGain (0, 0, numSamples, 0.5f);
        if (s.binaural.load())
        {
            // BINAURAL: placed as a head hears it: the far ear up to 0.66 ms later (ITD), quieter by
            // up to 6 dB and duller (the head's shadow, a low-pass falling from 20 kHz to 1.5 kHz);
            // the near ear as it is. Centre: both ears alike.
            const float p = juce::jlimit (-1.0f, 1.0f, pan), side = std::abs (p);
            const int ring = (int) s.binauralRing.size();
            if (! s.binauralWas)   // (just switched on: no old sound in its line)
            {
                s.binauralRing.fill (0.0f);
                s.binauralShadow = 0.0f;
                s.binauralItd = (float) (side * 0.00066 * interfaceRate);
            }
            s.binauralWas = true;
            // (the far ear's delay glides across the block, read between samples: no clicks as it pans)
            const float itdFrom = s.binauralItd, itdTo = juce::jlimit (0.0f, (float) ring - 2.0f, (float) (side * 0.00066 * interfaceRate));
            s.binauralItd = itdTo;
            const float nearGain = gain * (0.7071f + 0.2929f * side);   // (centre -3 dB a side, as the constant-power pan)
            const float farGain = nearGain * juce::Decibels::decibelsToGain (-6.0f * side);
            const float cutoff = 20000.0f * std::pow (1500.0f / 20000.0f, side);
            const float a = std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (cutoff, 0.45f * (float) interfaceRate) / (float) interfaceRate);
            auto* l = buf.getWritePointer (0);
            auto* r = buf.getWritePointer (1);
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = l[i];
                s.binauralRing[(size_t) s.binauralPos] = x;
                const float d = itdFrom + (itdTo - itdFrom) * (float) i / (float) numSamples;
                const int whole = (int) d;
                const float frac = d - (float) whole;
                const float a0 = s.binauralRing[(size_t) ((s.binauralPos - whole + ring) % ring)], a1 = s.binauralRing[(size_t) ((s.binauralPos - whole - 1 + ring) % ring)];
                const float late = a0 + (a1 - a0) * frac;
                s.binauralPos = (s.binauralPos + 1) % ring;
                s.binauralShadow = late + a * (s.binauralShadow - late);
                const float nearEar = x * nearGain, farEar = s.binauralShadow * farGain;
                l[i] = p > 0.0f ? farEar : nearEar;
                r[i] = p > 0.0f ? nearEar : farEar;
            }
        }
        else
        {
            s.binauralWas = false;
            const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            buf.copyFrom (1, 0, buf, 0, 0, numSamples);
            buf.applyGain (1, 0, numSamples, gain * std::sin (angle));
            buf.applyGain (0, 0, numSamples, gain * std::cos (angle));
        }
    }

    if (point == 2)   // POST
    {
        updatePeak (s.peakL, buf.getMagnitude (0, 0, numSamples), meterRelease);
        updatePeak (s.peakR, buf.getMagnitude (1, 0, numSamples), meterRelease);
        s.levels.process (buf, numSamples, s.source.stereo, interfaceRate);
    }

    // What ducks other strips keyed by this one: after its fader and ON, wherever the meters read
    // (so a muted mic never ducks anything).
    updatePeak (s.keyLevel, buf.getMagnitude (0, numSamples), meterRelease);
}

// This block's pads, rendered once however many strips use them (audio thread).
void AudioEngine::renderPads (int numSamples)
{
    if (! padsRendered)
    {
        pads.render (padsMix, numSamples, interfaceRate);
        padsRendered = true;
    }
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

// Adds a processed strip or pair into the masters and buses it sends to.
// Each send fades in when it's made and out when it's broken (stepFade).
void AudioEngine::sendFrom (Strip& s, int selfBus, int numSamples)
{
    // A send's level (SENDS ON FADER), ramped from where it was last block so moving it doesn't click.
    auto level = [] (std::atomic<float>& db, float& dbNow)
    {
        const auto gain = [] (float d) { return d <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (d); };
        const float target = db.load(), was = dbNow;
        dbNow = target;
        return std::pair<float, float> { gain (was), gain (target) };
    };

    // RAW: after its input stage, before its processing and fader; PRE: before its fader; else after it.
    auto tap = [&] (bool raw, bool pre) -> const juce::AudioBuffer<float>& { return raw ? s.rawBuffer : (pre && s.preBuffer.getNumSamples() >= numSamples ? s.preBuffer : s.buffer); };

    auto add = [&] (juce::AudioBuffer<float>& into, float& fade, bool on, const juce::AudioBuffer<float>& source, std::pair<float, float> lvl, float left = 1.0f, float right = 1.0f)
    {
        if (! on && fade <= 0.0f)
            return;
        const auto [from, to] = stepFade (fade, on, numSamples);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float side = ch == 0 ? left : right;
            const float a = from * lvl.first * side, b = to * lvl.second * side;
            if (a == 1.0f && b == 1.0f)
                into.addFrom (ch, 0, source, ch, 0, numSamples);
            else
                into.addFromWithRamp (ch, 0, source.getReadPointer (ch), numSamples, a, b);
        }
    };

    for (size_t b = 0; b < masters.size(); ++b)   // no self-feedback into the output it listens to
    {
        auto& bus = *masters[b];
        // Mix-minus: an input reaching the main output comes here directly too, unless it's the
        // one left out. Buses don't send here then: they'd bring the left-out input back.
        const int main = bus.minusMain.load(), minus = bus.minusSource.load();
        const bool minusFeed = selfBus < 0 && main >= 0 && ! (juce::isPositiveAndBelow (minus, (int) strips.size()) && strips[(size_t) minus].get() == &s)
                               && reachesOutput (s, main, 0);
        const bool sent = s.sends[b] && ! (selfBus >= 0 && main >= 0);
        auto lvl = level (s.sendDb[b], s.sendDbNow[b]);
        const bool pre = sent && selfBus < 0 && s.preSends[b].load();
        const juce::AudioBuffer<float>* source = &tap (sent && s.rawSends[b].load(), pre);
        add (bus.buffer, s.sendFade[b], (sent || minusFeed) && bus.enabled && ! (s.source.kind == Source::loopback && s.source.masterIndex == (int) b),
             sent ? *source : s.buffer, sent ? lvl : std::pair<float, float> { 1.0f, 1.0f }, 1.0f, 1.0f);
    }

    for (size_t p = 0; p < buses.size(); ++p)
    {
        auto& to = *buses[p];
        const bool on = s.busSends[p] && (int) p != selfBus;
        auto lvl = level (s.busSendDb[p], s.busSendDbNow[p]);
        const auto& feed = s.buffer;
        if (! to.combinator || to.paths.empty())
        {
            add (to.busInput, s.busSendFade[p], on, feed, lvl);
            continue;
        }

        // A combinator: into the paths it's sent to (none chosen: the first), one fade for all.
        if (! on && s.busSendFade[p] <= 0.0f)
            continue;
        const auto [from, toLevel] = stepFade (s.busSendFade[p], on, numSamples);
        const float a = from * lvl.first, b = toLevel * lvl.second;
        unsigned mask = s.pathSends[p].load();
        if (mask == 0)
            mask = 1;
        for (size_t k = 0; k < to.paths.size(); ++k)
            if ((mask >> k) & 1u)
                for (int ch = 0; ch < 2; ++ch)
                {
                    if (a == 1.0f && b == 1.0f)
                        to.paths[k]->input.addFrom (ch, 0, feed, ch, 0, numSamples);
                    else
                        to.paths[k]->input.addFromWithRamp (ch, 0, feed.getReadPointer (ch), numSamples, a, b);
                }
    }
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                    float* const* outputs, int numOutputs,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    const juce::ScopedNoDenormals noDenormals;   // (filters and meters decaying into silence stay cheap)

    // Times the whole callback, however it returns (see noteCallbackTime).
    struct Timing
    {
        AudioEngine& engine;
        std::int64_t start;
        int samples;
        ~Timing() { engine.noteCallbackTime (start, samples); }
    } timing { *this, juce::Time::getHighResolutionTicks(), numSamples };

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    const juce::SpinLock::ScopedTryLockType tl (graphLock);

    if (! tl.isLocked() || numSamples > maxBlock || holdAudio.load())
    {
        ++skippedCount;   // this block goes out silent (or the driver's new size waits for reprepareAll)
        missedSamples = juce::jmin (maxBlock, missedSamples + numSamples);   // (and is made up next block)
        return;
    }

    ++blockSerial;   // input devices are read once each in this block (processStrip)

    // A block skipped (the graph was being changed): the Windows outputs and the recordings get
    // its length in silence now, so the outputs don't run dry and the recordings keep time.
    const int missed = std::exchange (missedSamples, 0);
    if (missed > 0)
    {
        recorder.beginBlock();
        recorder.endBlock (missed);
    }
    meterRelease = juce::Decibels::decibelsToGain (-26.0f * (float) (numSamples / interfaceRate));   // sample peaks: fast fall

    for (auto& bus : masters)
        bus->buffer.clear (0, numSamples);

    for (auto& pair : buses)
    {
        pair->busInput.clear (0, numSamples);
        for (auto& path : pair->paths)
            path->input.clear (0, numSamples);
    }

    // Solo in place: while any input is soloed, the others are silenced. Pairs and outputs
    // carry whatever soloed inputs reach them.
    auditionThisBlock = false;
    if (auditionMix.getNumSamples() >= numSamples)
        auditionMix.clear (0, numSamples);

    const bool anySolo = std::any_of (strips.begin(), strips.end(), [] (auto& s) { return s->solo.load(); });
    padsRendered = tapeRendered = false;
    recorder.beginBlock();
    padsElsewhere = std::any_of (strips.begin(), strips.end(), [] (auto& s) { return s->padsInto.load(); });

    for (auto& strip : strips)
    {
        if (! strip->enabled)
            continue;   // off: silent, sends nothing
        processStrip (*strip, inputs, numInputs, numSamples, anySolo && ! strip->solo && ! strip->soloIsolate);
        recorder.offer (Recorder::Kind::input, (int) (&strip - strips.data()), strip->buffer, numSamples);
        sendFrom (*strip, -1, numSamples);
    }
    if (! padsRendered)
        pads.drain (interfaceRate);   // no PADS channel this block: its commands are taken anyway
    if (! tapeRendered)
        tape.drain();              // nor a TAPE one

    // Each pair runs after everything feeding it, so chains of buses add no delay.
    for (int p : busOrder)
    {
        auto& pair = *buses[(size_t) p];
        if (! pair.enabled)
            continue;
        processStrip (pair, inputs, numInputs, numSamples);
        recorder.offer (Recorder::Kind::bus, p, pair.buffer, numSamples);
        sendFrom (pair, p, numSamples);
    }

    monitorMix.clear (0, numSamples);
    bool monitoring = false;
    for (auto& bus : masters)
    {
        if (! bus->enabled)
            continue;

        auto& out = bus->buffer;
        keepFinite (out, numSamples);   // (whatever fed it: the last stop before meters and devices)

        if (! bus->stereo)
        {
            out.addFrom (0, 0, out, 1, 0, numSamples);
            out.applyGain (0, 0, numSamples, 0.5f);
            out.copyFrom (1, 0, out, 0, 0, numSamples);
        }

        // MIX BUS: its shelving EQ and BUS COMP (a fast path when nothing's in), then WIDTH, all
        // before its fader: as the console's compressor shares the main fader's VCAs, riding or
        // fading the master doesn't change how hard it compresses.
        if (auto* mix = bus->masterChain.get())
        {
            juce::AudioBuffer<float> block (out.getArrayOfWritePointers(), 2, numSamples);
            mix->beginBlock (numSamples);
            mix->runStages (block, VoiceChain::stageEq | VoiceChain::stageBus);
            mix->endBlock (block);
        }
        if (const float width = bus->width.load(); bus->stereo && std::abs (width - 1.0f) > 0.001f)
        {
            auto* l = out.getWritePointer (0);
            auto* r = out.getWritePointer (1);
            for (int i = 0; i < numSamples; ++i)
            {
                const float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]) * width;
                l[i] = mid + side;
                r[i] = mid - side;
            }
        }
        out.applyGain (0, numSamples, bus->mute ? 0.0f : bus->gain.load());   // its fader

        // Headphone correction (before LIMIT, which catches what its boosts push over).
        if (auto* eq = bus->headphoneEq.get())
            eq->process (out, numSamples);

        // LIMIT: instant attack, 80 ms release, -1 dBFS ceiling.
        if (bus->safeOn.load())
        {
            constexpr float ceiling = 0.891f;
            const float release = 1.0f - std::exp (-1.0f / (0.08f * (float) interfaceRate));
            float most = 1.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                const float level = juce::jmax (std::abs (out.getSample (0, i)), std::abs (out.getSample (1, i)));
                bus->safeGain += (1.0f - bus->safeGain) * release;
                if (level * bus->safeGain > ceiling)
                    bus->safeGain = ceiling / level;
                most = juce::jmin (most, bus->safeGain);
                out.setSample (0, i, out.getSample (0, i) * bus->safeGain);
                out.setSample (1, i, out.getSample (1, i) * bus->safeGain);
            }
            bus->safeReduction = juce::Decibels::gainToDecibels (most, -60.0f);
        }
        else
        {
            bus->safeGain = 1.0f;
            bus->safeReduction = 0.0f;
        }

        // Its device being changed: down to silence before the switch, back up after (setMasterOutput).
        if (bus->deviceFadeRestart.exchange (false))
            bus->deviceFade = 0.0f;
        if (const auto [from, to] = stepFade (bus->deviceFade, ! bus->deviceFadeOut.load(), numSamples); from < 1.0f || to < 1.0f)
            out.applyGainRamp (0, numSamples, from, to);

        bus->loudness.process (out, numSamples, bus->stereo);

        const float magnitudeL = out.getMagnitude (0, 0, numSamples), magnitudeR = out.getMagnitude (1, 0, numSamples);
        updatePeak (bus->peakL, magnitudeL, meterRelease);
        updatePeak (bus->peakR, magnitudeR, meterRelease);
        if (juce::jmax (magnitudeL, magnitudeR) >= 1.0f)
            bus->clipped = true;   // for ALERTS
        bus->levels.process (out, numSamples, bus->stereo, interfaceRate);
        recorder.offer (Recorder::Kind::output, (int) (&bus - masters.data()), out, numSamples);
        bus->retro.push (out, numSamples);

        if (bus->loopback)
        {
            bus->lastBlock.copyFrom (0, 0, out, 0, 0, numSamples);
            bus->lastBlock.copyFrom (1, 0, out, 1, 0, numSamples);
            bus->lastBlockSamples = numSamples;
        }
        else
            bus->lastBlockSamples = 0;   // (turned on again, it doesn't replay an old block)

        auto playOn = [&] (Secondary* device, bool toInterface, int firstChannel)
        {
            if (device != nullptr)
            {
                // Its own channels on the device: a pair from firstChannel (a mono output on a
                // multichannel device: just the one); past the device's end, its first pair.
                const int count = bus->stereo || device->channels <= 2 ? juce::jmin (2, device->channels) : 1;
                const int first = firstChannel >= 0 && firstChannel + count <= device->channels ? firstChannel : 0;
                if (! device->mixPending)
                    device->mix.clear (0, numSamples);
                for (int ch = 0; ch < count; ++ch)
                    device->mix.addFrom (first + ch, 0, out, ch, 0, numSamples);
                device->mixPending = true;
            }
            else if (toInterface)
            {
                // Mono masters use one master output; stereo masters use a pair.
                for (int ch = 0; ch < (bus->stereo ? 2 : 1); ++ch)
                {
                    const int dest = firstChannel + ch;

                    if (juce::isPositiveAndBelow (dest, numOutputs) && outputs[dest] != nullptr)
                        juce::FloatVectorOperations::add (outputs[dest], out.getReadPointer (ch), numSamples);   // outputs sharing channels add up
                }
            }
        };

        playOn (bus->output, bus->toInterface, bus->firstChannel);
        for (auto& d : bus->alsoOn)
            playOn (d.output, d.toInterface, d.firstChannel);

        // MONITOR: what this output sends (say, into the mic) heard on the master interface's
        // first pair too, where the headphones are, to check it by ear.
        if (bus->monitor.load())
        {
            for (int ch = 0; ch < 2; ++ch)
                monitorMix.addFrom (ch, 0, out, bus->stereo ? ch : 0, 0, numSamples);
            monitoring = true;
        }
    }

    // AUDITION: while a channel is auditioned, the control room hears it alone.
    if (auditionThisBlock)
    {
        for (int ch = 0; ch < 2; ++ch)
            monitorMix.copyFrom (ch, 0, auditionMix, ch, 0, numSamples);
        monitoring = true;
    }

    // The control room: the MONITOR feed onto the master interface's first pair.
    if (monitoring)
    {
        for (int ch = 0; ch < juce::jmin (2, numOutputs); ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::add (outputs[ch], monitorMix.getReadPointer (ch), numSamples);
    }

    // A preview clip (HEAR WHAT THEY HEAR) on the master interface's first pair, once.
    if (auto* clip = preview.get(); clip != nullptr && previewPos < clip->getNumSamples())
    {
        const int n = juce::jmin (numSamples, clip->getNumSamples() - previewPos);
        for (int ch = 0; ch < juce::jmin (2, numOutputs); ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::add (outputs[ch], clip->getReadPointer (juce::jmin (ch, clip->getNumChannels() - 1), previewPos), n);
        previewPos += n;
        if (previewPos >= clip->getNumSamples())
            previewPlaying = false;
    }

    // One push per device, with every output playing there mixed together.
    auto push = [this, numSamples, missed] (Secondary* device)
    {
        if (device != nullptr && device->mixPending)
        {
            if (missed > 0 && device->channels <= silentBlock.getNumChannels())
                device->fifo.push (silentBlock.getArrayOfReadPointers(), device->channels, missed);
            device->fifo.push (device->mix.getArrayOfReadPointers(), device->channels, numSamples);
            device->mixPending = false;
        }
    };
    for (auto& bus : masters)
    {
        push (bus->output);
        for (auto& d : bus->alsoOn)
            push (d.output);
    }

    recorder.endBlock (numSamples);   // silence, if what's being recorded is switched off
}

//==============================================================================
// Callback load: time spent as a share of the time the block lasts. Over 100% the device ran
// dry while we were still working: an audible glitch.
void AudioEngine::noteCallbackTime (std::int64_t startTicks, int numSamples)
{
    const double spent = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
    const double budget = numSamples / juce::jmax (1.0, interfaceRate);
    const float load = (float) (100.0 * spent / budget);

    loadAverage = loadAverage.load() * 0.98f + load * 0.02f;
    if (load > loadPeak.load())
        loadPeak = load;
    if (load > 100.0f)
        ++overloadCount;
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

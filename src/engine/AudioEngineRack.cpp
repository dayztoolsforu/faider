// AudioEngine's racks (message thread): a channel's units and patch (getRack, setRack), compiled
// for the audio thread (compileRack; AudioEngineProcess.cpp runs it).
#include "AudioEngine.h"
#include "AudioEngineInternal.h"
#include "VoiceChain.h"

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

    // (a unit placed twice, as a garbled file can have it: once)
    {
        std::vector<RackUnit> seen;
        for (auto* side : { &p.left, &p.right })
            side->erase (std::remove_if (side->begin(), side->end(), [&seen] (const RackUnit& u)
            {
                if (std::find (seen.begin(), seen.end(), u) != seen.end())
                    return true;
                seen.push_back (u);
                return false;
            }), side->end());
    }

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
        // (One the cables already name was only missing from the placement: it keeps its cables.
        // Spliced in as well, it would feed itself.)
        auto uncabled = [&p] (std::vector<int> units)
        {
            units.erase (std::remove_if (units.begin(), units.end(), [&p] (int b)
            {
                const RackUnit u { b, nullptr };
                return std::any_of (p.cables.begin(), p.cables.end(), [&u] (const auto& c) { return c.first == u || c.second == u; });
            }), units.end());
            return units;
        };
        const auto top = uncabled (afterInput), end = uncabled (atEnd);   // (both worked out before either goes in)
        splice (top, true);
        splice (end, false);
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

    // Cables that loop (setRack refuses them, but it sees a patch before its missing units are
    // in): the units run in the order they're placed instead, so the rack always compiles.
    if (std::vector<RackUnit> sorted; p.manual && ! sortUnits (inOrder (p), p.cables, sorted))
    {
        p.manual = false;
        p.cables.clear();
        ++s.rackRevision;
        logEvent ("rack: " + s.name + ": its cables looped, so its units run in the order they're placed");
    }
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

    // The chain follows the placement: the plugins in `inserts` in the order they're placed
    // (worked out here; under the lock they only move across).
    std::vector<size_t> order;
    order.reserve (s.inserts.size());
    std::vector<bool> taken (s.inserts.size(), false);
    for (auto& u : inOrder (p))
        for (size_t i = 0; i < s.inserts.size(); ++i)
            if (! taken[i] && s.inserts[i] != nullptr && s.inserts[i].get() == u.plugin)
            {
                taken[i] = true;
                order.push_back (i);
            }
    for (size_t i = 0; i < s.inserts.size(); ++i)   // any the placement missed keep their place at the end
        if (! taken[i] && s.inserts[i] != nullptr)
            order.push_back (i);

    std::vector<std::unique_ptr<juce::AudioPluginInstance>> chain;
    chain.reserve (order.size());
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        for (size_t i : order)
            chain.push_back (std::move (s.inserts[i]));
        s.inserts.swap (chain);
    }
    // (the old list's storage goes when this returns, outside the lock)
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
    auto cables = s.rack.manual ? s.rack.cables : seriesCables (s.rack);

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

    // (syncRack leaves no loop. Should one get here, the placement's order runs: never the plan
    // from before, which may name a plugin that has gone.)
    std::vector<RackUnit> sorted;
    if (! sortUnits (inOrder (s.rack), cables, sorted))
    {
        cables = seriesCables (s.rack);
        sorted = inOrder (s.rack);
    }
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

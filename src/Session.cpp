#include "Session.h"

namespace
{
    const juce::Identifier sessionTag { "FaiderSession" };

    // Sessions saved before the rename (as AudioHub) load too.
    bool isSession (const juce::XmlElement& xml) { return xml.hasTagName (sessionTag) || xml.hasTagName ("PatchbaySession") || xml.hasTagName ("AudioHubSession"); }   // (and under the earlier names)

    // Colours as "ff3d8fd1"; empty for "use the default".
    juce::String colourToString (juce::Colour c) { return c.isTransparent() ? juce::String() : c.toString(); }
    juce::Colour stringToColour (const juce::String& s) { return s.isEmpty() ? juce::Colour() : juce::Colour::fromString (s); }

    // Send flags as "0,2,5" (the indexes that are on).
    template <size_t N>
    juce::String flagsToString (const std::array<std::atomic<bool>, N>& flags, size_t count)
    {
        juce::StringArray on;
        for (size_t i = 0; i < juce::jmin (count, N); ++i)
            if (flags[i])
                on.add (juce::String ((int) i));
        return on.joinIntoString (",");
    }

    template <size_t N>
    void stringToFlags (const juce::String& text, std::array<std::atomic<bool>, N>& flags)
    {
        juce::StringArray on;
        on.addTokens (text, ",", "");
        for (auto& i : on)
            if (i.isNotEmpty() && juce::isPositiveAndBelow (i.getIntValue(), (int) N))
                flags[(size_t) i.getIntValue()] = true;
    }

    // A strip's channel EQ and dynamics (its state, as a plugin's).
    void saveChannel (juce::XmlElement& e, const AudioEngine::Strip& s)
    {
        if (s.channel == nullptr)
            return;
        juce::MemoryBlock state;
        s.channel->getStateInformation (state);
        e.setAttribute ("channel", state.toBase64Encoding());
    }

    void restoreChannel (const juce::XmlElement& e, AudioEngine::Strip& s)
    {
        juce::MemoryBlock state;
        if (s.channel != nullptr && state.fromBase64Encoding (e.getStringAttribute ("channel")) && state.getSize() > 0)
            s.channel->setStateInformation (state.getData(), (int) state.getSize());
    }

    // A strip's auto-duck (only written when it has one).
    void saveDuck (juce::XmlElement& e, const AudioEngine::Strip& s)
    {
        if (s.keyFrom.isNotEmpty())   // (its sidechain key, with the duck: both name another input)
            e.setAttribute ("keyFrom", s.keyFrom);
        if (s.duckBy.isEmpty())
            return;
        e.setAttribute ("duckBy", s.duckBy);
        e.setAttribute ("duckDepth", (double) s.duckDepth.load());
        e.setAttribute ("duckThreshold", (double) s.duckThreshold.load());
        e.setAttribute ("duckRelease", (double) s.duckRelease.load());
    }

    void restoreDuck (const juce::XmlElement& e, AudioEngine::Strip& s)
    {
        s.duckBy = e.getStringAttribute ("duckBy");
        s.keyFrom = e.getStringAttribute ("keyFrom");
        s.duckDepth = (float) e.getDoubleAttribute ("duckDepth", -12.0);
        s.duckThreshold = (float) e.getDoubleAttribute ("duckThreshold", -40.0);
        s.duckRelease = (float) e.getDoubleAttribute ("duckRelease", 600.0);
    }

    void saveChain (const AudioEngine& engine, juce::XmlElement& parent, const std::vector<std::unique_ptr<juce::AudioPluginInstance>>& chain)
    {
        for (auto& plugin : chain)
        {
            juce::MemoryBlock state;
            plugin->getStateInformation (state);

            auto* p = parent.createNewChildElement ("Insert");
            p->setAttribute ("name", plugin->getName());
            p->setAttribute ("path", plugin->getPluginDescription().fileOrIdentifier);
            p->setAttribute ("state", state.toBase64Encoding());
            if (auto* o = engine.insertOptions (plugin.get()))   // only what differs from a fresh insert
            {
                if (o->bypassed.load())         p->setAttribute ("bypassed", true);
                if (o->mix.load() != 1.0f)      p->setAttribute ("mix", (double) o->mix.load());
                if (o->inGain.load() != 1.0f)   p->setAttribute ("inGain", (double) o->inGain.load());
                if (o->outGain.load() != 1.0f)  p->setAttribute ("outGain", (double) o->outGain.load());
                if (o->locked)                  p->setAttribute ("locked", true);
            }
        }
    }

    // add: loads the plugin at a path onto the chain, returning an error or empty. Returns where
    // each saved plugin ended up in the chain (-1: it didn't load), so a saved rack's "pN" tokens
    // still find their own plugins when one before them is missing.
    std::vector<int> restoreChain (const AudioEngine& engine, const juce::XmlElement& parent, const juce::String& owner,
                                   std::vector<std::unique_ptr<juce::AudioPluginInstance>>& chain,
                                   const std::function<juce::String (const juce::String&)>& add,
                                   const std::function<void (const juce::String&)>& log)
    {
        std::vector<int> placed;
        for (auto* p : parent.getChildWithTagNameIterator ("Insert"))
        {
            const auto error = add (p->getStringAttribute ("path"));

            if (error.isNotEmpty())
            {
                log ("session: " + owner + ": couldn't restore " + p->getStringAttribute ("name") + ": " + error);
                placed.push_back (-1);
                continue;
            }
            placed.push_back ((int) chain.size() - 1);

            juce::MemoryBlock state;
            if (state.fromBase64Encoding (p->getStringAttribute ("state")) && state.getSize() > 0)
                chain.back()->setStateInformation (state.getData(), (int) state.getSize());
            if (auto* o = engine.insertOptions (chain.back().get()))
            {
                o->bypassed = p->getBoolAttribute ("bypassed");
                o->mix = (float) p->getDoubleAttribute ("mix", 1.0);
                o->inGain = (float) p->getDoubleAttribute ("inGain", 1.0);
                o->outGain = (float) p->getDoubleAttribute ("outGain", 1.0);
                o->locked = p->getBoolAttribute ("locked");
            }
        }
        return placed;
    }

    using Log = std::function<void (const juce::String&)>;

    // A channel's rack (AudioEngine::RackPatch): placement and cables as unit names: the built-in
    // units by name, plugins by their place in the chain ("p0"), the channel's jacks "in"/"out".
    // The built-in units' names, in RackBuiltin order.
    const char* const rackNames[] = { "filters", "gate", "comp", "eq", "bus", "input", "noise", "deess", "level", "fx", "limit" };

    juce::String rackToken (const AudioEngine::Strip& s, const AudioEngine::RackUnit& u)
    {
        if (u.plugin == nullptr)
            return u.builtin == AudioEngine::rackIn ? "in" : u.builtin == AudioEngine::rackOut ? "out"
                 : juce::isPositiveAndBelow (u.builtin, (int) AudioEngine::rackBuiltins) ? rackNames[u.builtin] : "";
        for (size_t i = 0; i < s.inserts.size(); ++i)
            if (s.inserts[i].get() == u.plugin)
                return "p" + juce::String ((int) i);
        return {};
    }

    // `placed` (restoreChain): saved plugin numbers to their places now; null: the same.
    std::optional<AudioEngine::RackUnit> rackUnit (const AudioEngine::Strip& s, const juce::String& token, const std::vector<int>* placed = nullptr)
    {
        if (token == "in")  return AudioEngine::RackUnit { AudioEngine::rackIn, nullptr };
        if (token == "out") return AudioEngine::RackUnit { AudioEngine::rackOut, nullptr };
        for (int b = 0; b < AudioEngine::rackBuiltins; ++b)
            if (token == rackNames[b])
                return AudioEngine::RackUnit { b, nullptr };
        if (token.startsWith ("p"))
        {
            int i = token.substring (1).getIntValue();
            if (placed != nullptr)
                i = juce::isPositiveAndBelow (i, (int) placed->size()) ? (*placed)[(size_t) i] : -1;
            if (juce::isPositiveAndBelow (i, (int) s.inserts.size()))
                return AudioEngine::RackUnit { -1, s.inserts[(size_t) i].get() };
        }
        return std::nullopt;
    }

    void saveRack (juce::XmlElement& e, const AudioEngine::Strip& s)
    {
        auto list = [&s] (const std::vector<AudioEngine::RackUnit>& units)
        {
            juce::StringArray tokens;
            for (auto& u : units)
                if (const auto t = rackToken (s, u); t.isNotEmpty())
                    tokens.add (t);
            return tokens.joinIntoString (",");
        };
        e.setAttribute ("rackLeft", list (s.rack.left));
        e.setAttribute ("rackRight", list (s.rack.right));
        e.setAttribute ("rackDeleted", (int) (AudioEngine::allBuiltins & ~s.rack.inRack));   // the built-in units deleted from the rack
        if (s.rack.manual)
        {
            juce::StringArray cables;
            for (auto& [from, to] : s.rack.cables)
                cables.add (rackToken (s, from) + ">" + rackToken (s, to));
            e.setAttribute ("rackCables", cables.joinIntoString (","));
        }
    }

    // After its plugins are back. Sessions from before the rack keep the default placement.
    void restoreRack (AudioEngine& engine, const juce::XmlElement& e, AudioEngine::Strip& s, const Log& log, const std::vector<int>& placed)
    {
        if (! e.hasAttribute ("rackLeft"))
            return;
        AudioEngine::RackPatch patch;
        auto list = [&] (const char* attribute, std::vector<AudioEngine::RackUnit>& into)
        {
            for (auto& t : juce::StringArray::fromTokens (e.getStringAttribute (attribute), ",", {}))
                if (const auto u = rackUnit (s, t, &placed))
                    into.push_back (*u);
        };
        patch.manual = e.hasAttribute ("rackCables");
        // (From before built-in units stayed on the strip: a hand-patched rack keeps them all in it.)
        patch.inRack = AudioEngine::allBuiltins & ~(unsigned) e.getIntAttribute ("rackDeleted", 0);   // (every built-in unit is in the rack unless deleted)
        list ("rackLeft", patch.left);
        list ("rackRight", patch.right);
        for (auto& c : juce::StringArray::fromTokens (e.getStringAttribute ("rackCables"), ",", {}))
        {
            const auto from = rackUnit (s, c.upToFirstOccurrenceOf (">", false, false), &placed);
            const auto to = rackUnit (s, c.fromFirstOccurrenceOf (">", false, false), &placed);
            if (from && to)
                patch.cables.push_back ({ *from, *to });
        }
        if (! engine.setRack (s, patch))
            log ("session: " + s.name + ": its rack's cables looped, so they follow the placement");
    }

    // One output, input or bus each (the whole session, or one strip for undo).
    void saveMaster (juce::XmlElement& b, const AudioEngine&, const AudioEngine::Master& bus)
    {
        b.setAttribute ("name", bus.name);
        b.setAttribute ("candidates", bus.candidates.joinIntoString ("\n"));
        b.setAttribute ("firstChannel", bus.firstChannel);
        b.setAttribute ("stereo", bus.stereo);
        b.setAttribute ("gain", (double) bus.gain.load());
        b.setAttribute ("mute", bus.mute.load());
        b.setAttribute ("loopback", bus.loopback.load());
        b.setAttribute ("enabled", bus.enabled.load());
        b.setAttribute ("safe", bus.safeOn.load());
        if (bus.masterChain != nullptr)   // the MIX BUS's EQ and BUS COMP
        {
            juce::MemoryBlock state;
            bus.masterChain->getStateInformation (state);
            b.setAttribute ("mixBus", state.toBase64Encoding());
        }
        if (std::abs (bus.width.load() - 1.0f) > 0.001f)
            b.setAttribute ("width", (double) bus.width.load());
        if (bus.headphoneProfile.isNotEmpty())
            b.setAttribute ("headphoneEq", bus.headphoneProfile);
        if (bus.minusOf.isNotEmpty())
        {
            b.setAttribute ("minusOf", bus.minusOf);
            b.setAttribute ("minusInput", bus.minusInput);
        }
        if (bus.silenceAlert > 0)
            b.setAttribute ("silenceAlert", bus.silenceAlert);
        if (bus.clipAlert)
            b.setAttribute ("clipAlert", true);
        b.setAttribute ("colour", colourToString (bus.colour));
        if (bus.locked)
            b.setAttribute ("locked", true);
        for (auto& d : bus.alsoOn)   // the other devices it plays on
        {
            auto* also = b.createNewChildElement ("AlsoOn");
            also->setAttribute ("device", d.candidate);
            also->setAttribute ("firstChannel", d.firstChannel);
        }
    }

    // Which combinator paths each send feeds: "bus:paths" buses (the paths as bits), only where chosen.
    void savePathSends (juce::XmlElement& e, const AudioEngine& engine, const AudioEngine::Strip& strip)
    {
        juce::StringArray items;
        for (size_t p = 0; p < engine.getBuses().size(); ++p)
            if (const auto mask = strip.pathSends[p].load(); mask != 0)
                items.add (juce::String ((int) p) + ":" + juce::String ((int) mask));
        if (! items.isEmpty())
            e.setAttribute ("pathSends", items.joinIntoString (","));
    }

    // Send levels that aren't 0 dB: "index:dB" buses (SENDS ON FADER).
    template <size_t N>
    void saveLevels (juce::XmlElement& e, const char* attribute, const std::array<std::atomic<float>, N>& levels, size_t count)
    {
        juce::StringArray items;
        for (size_t i = 0; i < juce::jmin (count, N); ++i)
            if (const float db = levels[i].load(); db != 0.0f)
                items.add (juce::String ((int) i) + ":" + juce::String (db, 1));
        if (! items.isEmpty())
            e.setAttribute (attribute, items.joinIntoString (","));
    }

    template <size_t N>
    void restoreLevels (const juce::XmlElement& e, const char* attribute, std::array<std::atomic<float>, N>& levels)
    {
        for (auto& item : juce::StringArray::fromTokens (e.getStringAttribute (attribute), ",", {}))
            if (const int i = item.upToFirstOccurrenceOf (":", false, false).getIntValue(); juce::isPositiveAndBelow (i, (int) N))
                levels[(size_t) i] = juce::jlimit (-60.0f, 12.0f, item.fromFirstOccurrenceOf (":", false, false).getFloatValue());
    }

    void restorePathSends (const juce::XmlElement& e, AudioEngine::Strip& strip)
    {
        for (auto& item : juce::StringArray::fromTokens (e.getStringAttribute ("pathSends"), ",", {}))
            if (const int p = item.upToFirstOccurrenceOf (":", false, false).getIntValue(); juce::isPositiveAndBelow (p, AudioEngine::maxBuses))
                strip.pathSends[(size_t) p] = (unsigned) item.fromFirstOccurrenceOf (":", false, false).getIntValue();
    }

    void saveInput (juce::XmlElement& s, const AudioEngine& engine, const AudioEngine::Strip& strip)
    {
        s.setAttribute ("name", strip.name);
        s.setAttribute ("kind", (int) strip.source.kind);
        s.setAttribute ("firstChannel", strip.source.firstChannel);
        s.setAttribute ("device", strip.source.deviceName);
        s.setAttribute ("stereo", strip.source.stereo);
        s.setAttribute ("busIndex", strip.source.masterIndex);
        s.setAttribute ("gain", (double) strip.gain.load());
        s.setAttribute ("pan", (double) strip.pan.load());
        s.setAttribute ("width", (double) strip.width.load());
        s.setAttribute ("mute", strip.mute.load());
        s.setAttribute ("enabled", strip.enabled.load());
        s.setAttribute ("insertsOn", strip.insertsOn.load());
        saveDuck (s, strip);
        saveChannel (s, strip);
        s.setAttribute ("locked", strip.locked);
        if (strip.padsInto.load())
            s.setAttribute ("padsInto", true);
        s.setAttribute ("colour", colourToString (strip.colour));

        s.setAttribute ("sends", flagsToString (strip.sends, engine.getMasters().size()));
        s.setAttribute ("rawSends", flagsToString (strip.rawSends, engine.getMasters().size()));
        s.setAttribute ("preSends", flagsToString (strip.preSends, engine.getMasters().size()));
        if (strip.altSource.has_value())   // LINE B
        {
            s.setAttribute ("altKind", (int) strip.altSource->kind);
            s.setAttribute ("altFirstChannel", strip.altSource->firstChannel);
            s.setAttribute ("altDevice", strip.altSource->deviceName);
            s.setAttribute ("altStereo", strip.altSource->stereo);
            s.setAttribute ("altBusIndex", strip.altSource->masterIndex);
            s.setAttribute ("onLineB", strip.onLineB);
        }
        s.setAttribute ("soloIsolate", strip.soloIsolate.load());
        s.setAttribute ("binaural", strip.binaural.load());
        s.setAttribute ("pairSends", flagsToString (strip.busSends, engine.getBuses().size()));
        savePathSends (s, engine, strip);
        saveLevels (s, "sendLevels", strip.sendDb, engine.getMasters().size());
        saveLevels (s, "pairSendLevels", strip.busSendDb, engine.getBuses().size());
        saveChain (engine, s, strip.inserts);
        saveRack (s, strip);
    }

    void saveBus (juce::XmlElement& p, const AudioEngine& engine, const AudioEngine::Strip& pair)
    {
        p.setAttribute ("name", pair.name);
        p.setAttribute ("stereo", pair.source.stereo);
        p.setAttribute ("gain", (double) pair.gain.load());
        p.setAttribute ("pan", (double) pair.pan.load());
        p.setAttribute ("width", (double) pair.width.load());
        p.setAttribute ("mute", pair.mute.load());
        p.setAttribute ("enabled", pair.enabled.load());
        p.setAttribute ("insertsOn", pair.insertsOn.load());
        saveDuck (p, pair);
        saveChannel (p, pair);
        p.setAttribute ("colour", colourToString (pair.colour));
        if (pair.locked)
            p.setAttribute ("locked", true);
        p.setAttribute ("sends", flagsToString (pair.sends, engine.getMasters().size()));
        p.setAttribute ("pairSends", flagsToString (pair.busSends, engine.getBuses().size()));
        savePathSends (p, engine, pair);
        saveLevels (p, "sendLevels", pair.sendDb, engine.getMasters().size());
        saveLevels (p, "pairSendLevels", pair.busSendDb, engine.getBuses().size());
        saveChain (engine, p, pair.inserts);
        saveRack (p, pair);

        if (pair.combinator)
        {
            p.setAttribute ("combinator", true);
            for (auto& path : pair.paths)
            {
                auto* el = p.createNewChildElement ("Path");
                el->setAttribute ("name", path->name);
                el->setAttribute ("gain", (double) path->gain.load());
                el->setAttribute ("mute", path->mute.load());
                saveChain (engine, *el, path->inserts);
            }
        }
    }

    // Each adds its strip at the end and returns its index, or -1.
    int restoreMaster (AudioEngine& engine, const juce::XmlElement& b, const Log& log)
    {
        juce::StringArray candidates;
        candidates.addLines (b.getStringAttribute ("candidates"));
        candidates.removeEmptyStrings();

        // Older sessions saved a fallback to the master outputs after the wanted device. Outputs
        // no longer fall back, so keep only the device that was actually chosen.
        if (candidates.size() > 1)
            candidates.removeRange (1, candidates.size() - 1);

        auto* bus = engine.addMaster (b.getStringAttribute ("name"), candidates, juce::jmax (0, b.getIntAttribute ("firstChannel")), b.getBoolAttribute ("stereo", true));
        if (bus == nullptr)
            return -1;
        bus->gain = (float) b.getDoubleAttribute ("gain", 1.0);
        bus->mute = b.getBoolAttribute ("mute");
        bus->loopback = b.getBoolAttribute ("loopback");
        bus->enabled = b.getBoolAttribute ("enabled", true);
        bus->safeOn = b.getBoolAttribute ("safe");
        bus->width = (float) b.getDoubleAttribute ("width", 1.0);
        if (juce::MemoryBlock state; bus->masterChain != nullptr && b.hasAttribute ("mixBus") && state.fromBase64Encoding (b.getStringAttribute ("mixBus")))
            bus->masterChain->setStateInformation (state.getData(), (int) state.getSize());
        bus->minusOf = b.getStringAttribute ("minusOf");
        bus->minusInput = b.getStringAttribute ("minusInput");
        bus->silenceAlert = b.getIntAttribute ("silenceAlert");
        bus->clipAlert = b.getBoolAttribute ("clipAlert");
        bus->colour = stringToColour (b.getStringAttribute ("colour"));
        bus->locked = b.getBoolAttribute ("locked");

        const int masterIndex = (int) engine.getMasters().size() - 1;
        if (b.hasAttribute ("headphoneEq"))
            engine.setHeadphoneEq (masterIndex, b.getStringAttribute ("headphoneEq"));
        for (auto* also : b.getChildWithTagNameIterator ("AlsoOn"))
            engine.addMasterDevice (masterIndex, also->getStringAttribute ("device"), juce::jmax (0, also->getIntAttribute ("firstChannel")));
        if (b.getChildByName ("Insert") != nullptr)   // older sessions
            log ("session: master " + bus->name + ": its plugins weren't loaded (masters have no plugins; put them on a bus that feeds it)");
        return masterIndex;
    }

    int restoreInput (AudioEngine& engine, const juce::XmlElement& s, const Log& log)
    {
        AudioEngine::Source source;
        source.kind = (AudioEngine::Source::Kind) juce::jlimit (0, (int) AudioEngine::Source::tape, s.getIntAttribute ("kind"));
        source.firstChannel = juce::jmax (0, s.getIntAttribute ("firstChannel"));   // (a garbled file can't index before the buffer)
        source.deviceName = s.getStringAttribute ("device");
        source.stereo = s.getBoolAttribute ("stereo", true);
        source.masterIndex = s.getIntAttribute ("busIndex", -1);

        auto& strip = engine.addStrip (s.getStringAttribute ("name"), source);
        strip.gain = (float) s.getDoubleAttribute ("gain", 1.0);
        strip.pan = (float) s.getDoubleAttribute ("pan", 0.0);
        strip.width = (float) s.getDoubleAttribute ("width", 1.0);
        strip.mute = s.getBoolAttribute ("mute");
        strip.enabled = s.getBoolAttribute ("enabled", true);
        strip.insertsOn = s.getBoolAttribute ("insertsOn", true);
        restoreDuck (s, strip);
        restoreChannel (s, strip);
        strip.locked = s.getBoolAttribute ("locked");
        strip.padsInto = s.getBoolAttribute ("padsInto");
        strip.colour = stringToColour (s.getStringAttribute ("colour"));

        stringToFlags (s.getStringAttribute ("sends"), strip.sends);
        stringToFlags (s.getStringAttribute ("rawSends"), strip.rawSends);
        stringToFlags (s.getStringAttribute ("preSends"), strip.preSends);
        if (s.hasAttribute ("altKind"))
        {
            AudioEngine::Source alt;
            alt.kind = (AudioEngine::Source::Kind) juce::jlimit (0, (int) AudioEngine::Source::tape, s.getIntAttribute ("altKind"));
            alt.firstChannel = juce::jmax (0, s.getIntAttribute ("altFirstChannel"));
            alt.deviceName = s.getStringAttribute ("altDevice");
            alt.stereo = s.getBoolAttribute ("altStereo", true);
            alt.masterIndex = s.getIntAttribute ("altBusIndex", -1);
            strip.altSource = alt;
            strip.onLineB = s.getBoolAttribute ("onLineB");
        }
        strip.soloIsolate = s.getBoolAttribute ("soloIsolate");
        strip.binaural = s.getBoolAttribute ("binaural");
        // (cuesOn, cuePan, direct and linkWith in an earlier mix: the cue, aux, DIRECT and LINK
        // features they set are gone, so they're read past)
        stringToFlags (s.getStringAttribute ("pairSends"), strip.busSends);
        restorePathSends (s, strip);
        restoreLevels (s, "sendLevels", strip.sendDb);
        restoreLevels (s, "pairSendLevels", strip.busSendDb);

        const int stripIndex = (int) engine.getStrips().size() - 1;
        const auto placed = restoreChain (engine, s, strip.name, strip.inserts,
                                          [&engine, stripIndex] (const juce::String& path) { return engine.addInsert (stripIndex, path); }, log);
        restoreRack (engine, s, strip, log, placed);
        return stripIndex;
    }

    // Its pair-to-pair sends are left to restoreBusSends (once every pair exists).
    int restoreBus (AudioEngine& engine, const juce::XmlElement& p, const Log& log)
    {
        const bool combinator = p.getBoolAttribute ("combinator");
        auto* pair = combinator ? engine.addCombinator (p.getStringAttribute ("name"), 0) : engine.addBus (p.getStringAttribute ("name"));
        if (pair == nullptr)
            return -1;

        const int busIndex = (int) engine.getBuses().size() - 1;
        engine.setBusStereo (busIndex, p.getBoolAttribute ("stereo", true));
        pair->gain = (float) p.getDoubleAttribute ("gain", 1.0);
        pair->pan = (float) p.getDoubleAttribute ("pan", 0.0);
        pair->width = (float) p.getDoubleAttribute ("width", 1.0);
        pair->mute = p.getBoolAttribute ("mute");
        pair->enabled = p.getBoolAttribute ("enabled", true);
        pair->insertsOn = p.getBoolAttribute ("insertsOn", true);
        restoreDuck (p, *pair);
        restoreChannel (p, *pair);
        pair->colour = stringToColour (p.getStringAttribute ("colour"));
        pair->locked = p.getBoolAttribute ("locked");
        stringToFlags (p.getStringAttribute ("sends"), pair->sends);
        restorePathSends (p, *pair);
        restoreLevels (p, "sendLevels", pair->sendDb);
        restoreLevels (p, "pairSendLevels", pair->busSendDb);

        const auto placed = restoreChain (engine, p, pair->name, pair->inserts,
                                          [&engine, busIndex] (const juce::String& path) { return engine.addBusInsert (busIndex, path); }, log);
        restoreRack (engine, p, *pair, log, placed);

        for (auto* el : p.getChildWithTagNameIterator ("Path"))
        {
            engine.addPath (busIndex, el->getStringAttribute ("name"));
            const int pathIndex = (int) pair->paths.size() - 1;
            if (pathIndex < 0)
                break;
            auto& path = *pair->paths[(size_t) pathIndex];
            path.gain = (float) el->getDoubleAttribute ("gain", 1.0);
            path.mute = el->getBoolAttribute ("mute");
            restoreChain (engine, *el, pair->name + " / " + path.name, path.inserts,
                          [&engine, busIndex, pathIndex] (const juce::String& file) { return engine.addPathInsert (busIndex, pathIndex, file); }, log);
        }
        return busIndex;
    }

    void restoreBusSends (AudioEngine& engine, int from, const juce::String& links)
    {
        juce::StringArray targets;
        targets.addTokens (links, ",", "");
        for (auto& t : targets)
            if (t.isNotEmpty())
                engine.setBusSend (from, t.getIntValue(), true);
    }
}

std::unique_ptr<juce::XmlElement> Session::save (const AudioEngine& engine, const juce::String& interfaceName)
{
    auto root = std::make_unique<juce::XmlElement> (sessionTag);
    root->setAttribute ("version", 1);
    root->setAttribute ("master", interfaceName);
    root->setAttribute ("hiddenDevices", engine.getHiddenDevices().joinIntoString ("\n"));
    root->setAttribute ("disabledDevices", engine.getDisabledDevices().joinIntoString ("\n"));

    // The button box: each pad that has a sound (the sound itself stays in its file).
    for (int i = 0; i < Pads::count; ++i)
    {
        const auto& pad = engine.getPads().pad (i);
        if (pad.file.isEmpty() && pad.macro.isEmpty())   // (a macro-only pad has no sound, but is kept)
            continue;
        auto* p = root->createNewChildElement ("Pad");
        p->setAttribute ("index", i);
        p->setAttribute ("name", pad.name);
        p->setAttribute ("file", pad.file);
        p->setAttribute ("colour", colourToString (pad.colour));
        p->setAttribute ("gain", (double) pad.gain);
        p->setAttribute ("mode", (int) pad.mode);
        p->setAttribute ("choke", pad.choke);
        p->setAttribute ("hotkey", pad.hotkey);
        p->setAttribute ("macro", pad.macro);
        if (pad.trimStart > 0.0) p->setAttribute ("trimStart", pad.trimStart);
        if (pad.trimEnd > 0.0)   p->setAttribute ("trimEnd", pad.trimEnd);
        if (pad.fadeIn > 0.0)    p->setAttribute ("fadeIn", pad.fadeIn);
        if (pad.fadeOut > 0.0)   p->setAttribute ("fadeOut", pad.fadeOut);
    }

    // Macros: their steps (targets by name).
    for (auto& macro : engine.getMacros().list)
    {
        auto* m = root->createNewChildElement ("Macro");
        m->setAttribute ("name", macro.name);
        m->setAttribute ("hotkey", macro.hotkey);
        m->setAttribute ("undoOnRelease", macro.undoOnRelease);
        for (auto& t : macro.triggers)
        {
            auto* e = m->createNewChildElement ("Trigger");
            e->setAttribute ("when", (int) t.when);
            e->setAttribute ("what", t.what);
            e->setAttribute ("seconds", t.seconds);
        }
        for (auto& step : macro.steps)
        {
            auto* s = m->createNewChildElement ("Step");
            s->setAttribute ("action", (int) step.action);
            s->setAttribute ("target", step.target);
            s->setAttribute ("target2", step.target2);
            s->setAttribute ("value", (double) step.value);
            s->setAttribute ("ms", step.ms);
        }
    }

    root->setAttribute ("remote", engine.getRemote().saveState());
    root->setAttribute ("mixBusOutput", engine.mainMixMaster);
    for (auto& key : engine.trayStrips)
        root->createNewChildElement ("Tray")->setAttribute ("strip", key);

    // App rules.
    for (auto& rule : engine.appRules)
    {
        auto* r = root->createNewChildElement ("AppRule");
        r->setAttribute ("exe", rule.exe);
        r->setAttribute ("to", rule.to);
    }

    // Scenes: each strip's settings, by name.
    const auto& scenes = engine.getScenes();
    root->setAttribute ("activeScene", scenes.active);
    for (auto& scene : scenes.list)
    {
        auto* sc = root->createNewChildElement ("Scene");
        sc->setAttribute ("name", scene.name);
        sc->setAttribute ("hotkey", scene.hotkey);
        sc->setAttribute ("device", scene.device);
        if (scene.withPlugins)
            sc->setAttribute ("withPlugins", true);
        if (scene.goBack)
            sc->setAttribute ("goBack", true);
        for (auto& t : scene.triggers)
        {
            auto* e = sc->createNewChildElement ("Trigger");
            e->setAttribute ("when", (int) t.when);
            e->setAttribute ("what", t.what);
            e->setAttribute ("seconds", t.seconds);
        }
        for (auto& s : scene.settings)
        {
            auto* st = sc->createNewChildElement ("Set");
            st->setAttribute ("target", s.target);
            st->setAttribute ("gain", (double) s.gain);
            st->setAttribute ("pan", (double) s.pan);
            st->setAttribute ("mute", s.mute);
            st->setAttribute ("solo", s.solo);
            st->setAttribute ("on", s.on);
            st->setAttribute ("insertsOn", s.insertsOn);
            for (auto& [to, on] : s.sends)
            {
                auto* send = st->createNewChildElement ("Send");
                send->setAttribute ("to", to);
                send->setAttribute ("on", on);
            }
            for (auto& [name, state] : s.plugins)
            {
                auto* plugin = st->createNewChildElement ("Plugin");
                plugin->setAttribute ("name", name);
                plugin->setAttribute ("state", state.toBase64Encoding());
            }
        }
    }

    // The tape deck: its playlist and settings (not the position).
    const auto& tape = engine.getTape();
    auto* deck = root->createNewChildElement ("Tape");
    deck->setAttribute ("current", tape.current());
    deck->setAttribute ("level", (double) tape.getLevel());
    deck->setAttribute ("shuffle", tape.getShuffle());
    deck->setAttribute ("repeat", (int) tape.repeat);
    deck->setAttribute ("recordFolder", engine.getRecorder().folder.getFullPathName());
    deck->setAttribute ("recordSource", engine.getRecorder().chosen);
    deck->setAttribute ("replay", engine.getRecorder().replayLength);
    deck->setAttribute ("recordFormat", engine.getRecorder().format);
    for (int i = 0; i < tape.size(); ++i)
        deck->createNewChildElement ("Track")->setAttribute ("file", tape.file (i).getFullPathName());

    // The user's names for devices (Devices tab).
    const auto aliases = deviceAliases();
    for (auto& device : aliases.getAllKeys())
    {
        auto* d = root->createNewChildElement ("DeviceName");
        d->setAttribute ("device", device);
        d->setAttribute ("name", aliases[device]);
    }

    for (auto& bus : engine.getMasters())
        saveMaster (*root->createNewChildElement ("Bus"), engine, *bus);
    for (auto& strip : engine.getStrips())
        saveInput (*root->createNewChildElement ("Strip"), engine, *strip);
    for (auto& pair : engine.getBuses())
        saveBus (*root->createNewChildElement ("Pair"), engine, *pair);
    return root;
}

juce::String Session::getInterfaceName (const juce::XmlElement& xml)
{
    return isSession (xml) ? xml.getStringAttribute ("master") : juce::String();
}

bool Session::restore (AudioEngine& engine, const juce::XmlElement& xml, const std::function<void (const juce::String&)>& log,
                       const juce::Array<EndpointInfo>& endpoints)
{
    if (! isSession (xml))
        return false;

    juce::StringArray hidden;
    hidden.addLines (xml.getStringAttribute ("hiddenDevices"));
    hidden.removeEmptyStrings();
    for (auto& d : hidden)
        engine.setDeviceHidden (d, true);

    juce::StringArray disabled;
    disabled.addLines (xml.getStringAttribute ("disabledDevices"));
    disabled.removeEmptyStrings();
    if (! disabled.isEmpty())
    {
        // The devices that are on the machine and working are switched off, and said so. The
        // others keep their setting (so they are still off when they come back), but nothing
        // is said about them: a device that is not there, or is not working, is not worth a line
        // in the log, and the list of them would otherwise grow with every device ever taken
        // off the machine.
        juce::StringArray here, elsewhere;
        for (auto& name : disabled)
        {
            const bool present = std::any_of (endpoints.begin(), endpoints.end(),
                                              [&] (const EndpointInfo& e) { return e.name == name && e.isActive(); });
            (present ? here : elsewhere).add (name);
        }
        engine.setDeviceDisabled (here, true);
        engine.setDeviceDisabled (elsewhere, true, false);
    }

    for (auto* d : xml.getChildWithTagNameIterator ("DeviceName"))
        setDeviceAlias (d->getStringAttribute ("device"), d->getStringAttribute ("name"));

    // Pads: their settings, and their sounds loaded again from the files.
    for (auto* p : xml.getChildWithTagNameIterator ("Pad"))
    {
        const int index = p->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (index, Pads::count))
            continue;
        auto& pad = engine.getPads().pad (index);
        pad.name = p->getStringAttribute ("name");
        pad.file = p->getStringAttribute ("file");
        pad.colour = stringToColour (p->getStringAttribute ("colour"));
        pad.gain = (float) p->getDoubleAttribute ("gain", 1.0);
        pad.mode = (Pads::Mode) juce::jlimit (0, (int) Pads::Mode::loop, p->getIntAttribute ("mode"));
        pad.choke = p->getIntAttribute ("choke");
        pad.hotkey = p->getStringAttribute ("hotkey");
        pad.macro = p->getStringAttribute ("macro");

        if (pad.file.isEmpty())
            continue;   // a macro-only pad: no sound to load
        juce::String error;
        if (! engine.getPads().load (index, juce::File (pad.file), engine.getSampleRate(), error))
            log ("session: pad " + pad.name + ": " + error);   // the pad keeps its file, greyed, until it's found again
        pad.trimStart = p->getDoubleAttribute ("trimStart");   // after load, which starts them at 0
        pad.trimEnd = p->getDoubleAttribute ("trimEnd");
        pad.fadeIn = p->getDoubleAttribute ("fadeIn");
        pad.fadeOut = p->getDoubleAttribute ("fadeOut");
        engine.getPads().applyEdits (index);
    }

    auto& macros = engine.getMacros();
    macros.cancelAll();
    macros.list.clear();
    for (auto* m : xml.getChildWithTagNameIterator ("Macro"))
    {
        Macros::Macro macro;
        macro.name = m->getStringAttribute ("name");
        macro.hotkey = m->getStringAttribute ("hotkey");
        macro.undoOnRelease = m->getBoolAttribute ("undoOnRelease");
        for (auto* s : m->getChildWithTagNameIterator ("Step"))
            macro.steps.push_back ({ (Macros::Action) juce::jlimit (0, (int) Macros::Action::pluginSet, s->getIntAttribute ("action")),
                                     s->getStringAttribute ("target"), s->getStringAttribute ("target2"),
                                     (float) s->getDoubleAttribute ("value"), s->getIntAttribute ("ms") });
        for (auto* t : m->getChildWithTagNameIterator ("Trigger"))
            macro.triggers.push_back ({ (Macros::Trigger::When) juce::jlimit (0, (int) Macros::Trigger::When::time, t->getIntAttribute ("when")),
                                        t->getStringAttribute ("what"), t->getIntAttribute ("seconds", 10) });
        macros.list.push_back (std::move (macro));
    }

    engine.getRemote().restoreState (xml.getStringAttribute ("remote"));
    // (pfl, cueOutput, monitorLevel and auxOutputs in an earlier mix: PFL, the cue and aux outputs
    // and the monitor level are gone, so they're read past)
    engine.mainMixMaster = xml.getStringAttribute ("mixBusOutput");
    engine.trayStrips.clear();
    for (auto* t : xml.getChildWithTagNameIterator ("Tray"))
        engine.trayStrips.add (t->getStringAttribute ("strip"));

    engine.appRules.clear();
    for (auto* r : xml.getChildWithTagNameIterator ("AppRule"))
        engine.appRules.push_back ({ r->getStringAttribute ("exe"), r->getStringAttribute ("to") });
    if (xml.getChildByName ("AppTakeover") != nullptr)   // app takeover (a sink for captured apps) is gone
        log ("session: app takeover is no longer part of Faider; apps play on their own devices");

    auto& scenes = engine.getScenes();
    scenes.list.clear();
    for (auto* sc : xml.getChildWithTagNameIterator ("Scene"))
    {
        Scenes::Scene scene;
        scene.name = sc->getStringAttribute ("name");
        scene.hotkey = sc->getStringAttribute ("hotkey");
        scene.device = sc->getStringAttribute ("device");
        scene.withPlugins = sc->getBoolAttribute ("withPlugins");
        scene.goBack = sc->getBoolAttribute ("goBack");
        for (auto* t : sc->getChildWithTagNameIterator ("Trigger"))
            scene.triggers.push_back ({ (MacroTrigger::When) juce::jlimit (0, (int) MacroTrigger::When::time, t->getIntAttribute ("when")),
                                        t->getStringAttribute ("what"), t->getIntAttribute ("seconds", 10) });
        for (auto* st : sc->getChildWithTagNameIterator ("Set"))
        {
            Scenes::Setting s;
            s.target = st->getStringAttribute ("target");
            s.gain = (float) st->getDoubleAttribute ("gain", 1.0);
            s.pan = (float) st->getDoubleAttribute ("pan");
            s.mute = st->getBoolAttribute ("mute");
            s.solo = st->getBoolAttribute ("solo");
            s.on = st->getBoolAttribute ("on", true);
            s.insertsOn = st->getBoolAttribute ("insertsOn", true);
            for (auto* send : st->getChildWithTagNameIterator ("Send"))
                s.sends.push_back ({ send->getStringAttribute ("to"), send->getBoolAttribute ("on") });
            for (auto* plugin : st->getChildWithTagNameIterator ("Plugin"))
            {
                juce::MemoryBlock state;
                state.fromBase64Encoding (plugin->getStringAttribute ("state"));
                s.plugins.push_back ({ plugin->getStringAttribute ("name"), std::move (state) });
            }
            scene.settings.push_back (std::move (s));
        }
        scenes.list.push_back (std::move (scene));
    }
    scenes.active = juce::jlimit (-1, (int) scenes.list.size() - 1, xml.getIntAttribute ("activeScene", -1));

    if (auto* deck = xml.getChildByName ("Tape"))
    {
        auto& tape = engine.getTape();
        tape.clearPlaylist();
        for (auto* t : deck->getChildWithTagNameIterator ("Track"))
            tape.add (juce::File (t->getStringAttribute ("file")));
        tape.setLevel ((float) deck->getDoubleAttribute ("level", 1.0));
        tape.repeat = (Tape::Repeat) juce::jlimit (0, (int) Tape::Repeat::all, deck->getIntAttribute ("repeat", (int) Tape::Repeat::all));
        tape.cue (deck->getIntAttribute ("current", -1));
        tape.setShuffle (deck->getBoolAttribute ("shuffle"));

        auto& recorder = engine.getRecorder();
        if (const auto folder = deck->getStringAttribute ("recordFolder"); juce::File::isAbsolutePath (folder))
            recorder.folder = juce::File (folder);
        recorder.chosen = deck->getStringAttribute ("recordSource");
        recorder.replayLength = deck->getDoubleAttribute ("replay");
        recorder.format = deck->getStringAttribute ("recordFormat", "wav24");
    }

    for (auto* b : xml.getChildWithTagNameIterator ("Bus"))
        restoreMaster (engine, *b, log);
    for (auto* s : xml.getChildWithTagNameIterator ("Strip"))
    {
        // Loopbacks (an output fed back in as an input) aren't used any more: outputs reach
        // devices through the Mixer's MASTER section instead.
        if (s->getIntAttribute ("kind") == (int) AudioEngine::Source::loopback)
        {
            log ("session: loopback input " + s->getStringAttribute ("name") + " dropped (devices are set in MASTER now)");
            continue;
        }
        restoreInput (engine, *s, log);
    }
    for (auto* m : xml.getChildWithTagNameIterator ("Master"))   // from a build that had raw MASTER taps
        log ("session: MASTER tap " + m->getStringAttribute ("name") + " dropped (MASTER now lists devices)");
    for (auto& bus : engine.getMasters())
        bus->loopback = false;   // nothing listens to it

    // Pairs: create them all first, then wire pair-to-pair sends through the engine so its loop
    // check and processing order apply.
    std::vector<std::pair<int, juce::String>> pairLinks;
    for (auto* p : xml.getChildWithTagNameIterator ("Pair"))
    {
        const int busIndex = restoreBus (engine, *p, log);
        if (busIndex < 0)
            break;
        pairLinks.push_back ({ busIndex, p->getStringAttribute ("pairSends") });
    }
    for (auto& [from, links] : pairLinks)
        restoreBusSends (engine, from, links);

    engine.refreshLatency();
    return true;
}

//==============================================================================
std::unique_ptr<juce::XmlElement> Session::saveStrip (const AudioEngine& engine, const juce::String& kind, int index)
{
    const auto& strips = engine.getStrips();
    const auto& buses = engine.getBuses();
    const auto& masters = engine.getMasters();
    std::unique_ptr<juce::XmlElement> e;

    // Who sends to it, by name, with each send's level (and RAW, or the combinator paths it
    // feeds): the sends are the senders' own.
    auto sentFrom = [&] (auto sends, auto details)
    {
        for (auto& s : strips) if (sends (*s)) { auto* f = e->createNewChildElement ("SentFrom"); f->setAttribute ("input", s->name); details (*f, *s); }
        for (auto& p : buses)  if (sends (*p)) { auto* f = e->createNewChildElement ("SentFrom"); f->setAttribute ("bus", p->name);  details (*f, *p); }
    };

    if (kind == "output" && juce::isPositiveAndBelow (index, (int) masters.size()))
    {
        e = std::make_unique<juce::XmlElement> ("Bus");
        saveMaster (*e, engine, *masters[(size_t) index]);
        sentFrom ([index] (const AudioEngine::Strip& s) { return s.sends[(size_t) index].load(); },
                  [index] (juce::XmlElement& f, const AudioEngine::Strip& s)
                  {
                      f.setAttribute ("level", (double) s.sendDb[(size_t) index].load());
                      f.setAttribute ("raw", s.rawSends[(size_t) index].load());
                  });
    }
    else if (kind == "input" && juce::isPositiveAndBelow (index, (int) strips.size()))
    {
        e = std::make_unique<juce::XmlElement> ("Strip");
        saveInput (*e, engine, *strips[(size_t) index]);
    }
    else if (kind == "bus" && juce::isPositiveAndBelow (index, (int) buses.size()))
    {
        e = std::make_unique<juce::XmlElement> ("Pair");
        saveBus (*e, engine, *buses[(size_t) index]);
        e->setAttribute ("removedAt", index);   // its own bus sends: the masters after it move down meanwhile
        sentFrom ([index] (const AudioEngine::Strip& s) { return s.busSends[(size_t) index].load(); },
                  [index] (juce::XmlElement& f, const AudioEngine::Strip& s)
                  {
                      f.setAttribute ("level", (double) s.busSendDb[(size_t) index].load());
                      f.setAttribute ("paths", (int) s.pathSends[(size_t) index].load());
                  });
    }
    return e;
}

int Session::restoreStrip (AudioEngine& engine, const juce::XmlElement& xml, const std::function<void (const juce::String&)>& log)
{
    int index = -1;
    if (xml.hasTagName ("Bus"))        index = restoreMaster (engine, xml, log);
    else if (xml.hasTagName ("Strip")) index = restoreInput (engine, xml, log);
    else if (xml.hasTagName ("Pair"))
    {
        // Saved when it sat at removedAt: the masters after it have since moved down one, so its
        // own bus-indexed settings move with them. (It comes back at the end.)
        juce::XmlElement pair (xml);
        if (const int removedAt = xml.getIntAttribute ("removedAt", -1); removedAt >= 0)
            for (auto* attribute : { "pairSends", "pathSends", "pairSendLevels" })
            {
                auto items = juce::StringArray::fromTokens (xml.getStringAttribute (attribute), ",", {});
                for (auto& item : items)
                    if (const int i = item.upToFirstOccurrenceOf (":", false, false).getIntValue(); item.isNotEmpty() && i > removedAt)
                        item = juce::String (i - 1) + item.fromFirstOccurrenceOf (":", true, false);
                if (xml.hasAttribute (attribute))
                    pair.setAttribute (attribute, items.joinIntoString (","));
            }
        index = restoreBus (engine, pair, log);
        if (index >= 0)
            restoreBusSends (engine, index, pair.getStringAttribute ("pairSends"));
    }
    if (index < 0)
        return -1;

    for (auto* from : xml.getChildWithTagNameIterator ("SentFrom"))
    {
        const bool fromInput = from->hasAttribute ("input");
        const auto name = from->getStringAttribute (fromInput ? "input" : "bus");
        const auto& senders = fromInput ? engine.getStrips() : engine.getBuses();
        for (int i = 0; i < (int) senders.size(); ++i)
        {
            if (senders[(size_t) i]->name != name)
                continue;
            auto& sender = *senders[(size_t) i];
            const float level = juce::jlimit (-60.0f, 12.0f, (float) from->getDoubleAttribute ("level", 0.0));
            if (xml.hasTagName ("Bus"))
            {
                sender.sendDb[(size_t) index] = level;
                sender.rawSends[(size_t) index] = from->getBoolAttribute ("raw");
                sender.sends[(size_t) index] = true;
            }
            else
            {
                sender.busSendDb[(size_t) index] = level;
                sender.pathSends[(size_t) index] = (unsigned) from->getIntAttribute ("paths");
                if (fromInput)
                    sender.busSends[(size_t) index] = true;
                else
                    engine.setBusSend (i, index, true);
            }
            break;
        }
    }
    engine.refreshLatency();
    return index;
}

namespace
{
    std::vector<std::unique_ptr<juce::AudioPluginInstance>>* chainOf (const AudioEngine& engine, const juce::String& kind, int index)
    {
        if (kind == "bus")    return juce::isPositiveAndBelow (index, (int) engine.getBuses().size()) ? &engine.getBuses()[(size_t) index]->inserts : nullptr;
        return juce::isPositiveAndBelow (index, (int) engine.getStrips().size()) ? &engine.getStrips()[(size_t) index]->inserts : nullptr;
    }
}

std::unique_ptr<juce::XmlElement> Session::saveInsert (const AudioEngine& engine, const juce::String& kind, int index, int slot)
{
    auto* chain = chainOf (engine, kind, index);
    if (chain == nullptr || ! juce::isPositiveAndBelow (slot, (int) chain->size()))
        return nullptr;
    juce::XmlElement holder ("Inserts");
    saveChain (engine, holder, *chain);
    return std::make_unique<juce::XmlElement> (*holder.getChildElement (slot));
}

bool Session::restoreInsert (AudioEngine& engine, const juce::String& kind, int index, int slot, const juce::XmlElement& xml,
                             const std::function<void (const juce::String&)>& log)
{
    auto* chain = chainOf (engine, kind, index);
    if (chain == nullptr)
        return false;
    juce::XmlElement holder ("Inserts");
    holder.addChildElement (new juce::XmlElement (xml));
    const auto before = chain->size();
    restoreChain (engine, holder, kind, *chain, [&] (const juce::String& path)
    {
        return kind == "bus" ? engine.addBusInsert (index, path) : engine.addInsert (index, path);
    }, log);
    if (chain->size() == before)
        return false;
    for (int i = (int) chain->size() - 1; i > juce::jlimit (0, (int) chain->size() - 1, slot); --i)   // back into its slot
        engine.swapInserts (*chain, i, i - 1);
    return true;
}

std::unique_ptr<juce::XmlElement> Session::saveRackPreset (const AudioEngine& engine, const juce::String& kind, int index)
{
    auto* chain = chainOf (engine, kind, index);
    if (chain == nullptr)
        return nullptr;
    const auto& strip = kind == "bus" ? *engine.getBuses()[(size_t) index] : *engine.getStrips()[(size_t) index];
    auto xml = std::make_unique<juce::XmlElement> ("Rack");
    xml->setAttribute ("from", strip.name);
    xml->setAttribute ("insertsOn", strip.insertsOn.load());
    saveChannel (*xml, strip);
    saveChain (engine, *xml, *chain);
    saveRack (*xml, strip);
    return xml;
}

bool Session::loadRackPreset (AudioEngine& engine, const juce::String& kind, int index, const juce::XmlElement& xml,
                              const std::function<void (const juce::String&)>& log)
{
    auto* chain = chainOf (engine, kind, index);
    if (chain == nullptr || ! xml.hasTagName ("Rack"))
        return false;
    auto& strip = kind == "bus" ? *engine.getBuses()[(size_t) index] : *engine.getStrips()[(size_t) index];
    while (! chain->empty())
    {
        if (kind == "bus") engine.removeBusInsert (index, (int) chain->size() - 1);
        else               engine.removeInsert (index, (int) chain->size() - 1);
    }
    restoreChannel (xml, strip);
    strip.insertsOn = xml.getBoolAttribute ("insertsOn", true);
    const auto placed = restoreChain (engine, xml, strip.name, *chain, [&] (const juce::String& path)
    {
        return kind == "bus" ? engine.addBusInsert (index, path) : engine.addInsert (index, path);
    }, log);
    restoreRack (engine, xml, strip, log, placed);
    return true;
}

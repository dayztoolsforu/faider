// Checks that saved mixes load (src/Session.h), headless: no audio device is opened, and
// nothing is written to Faider's own folder in %APPDATA% (this target is built with
// FAIDER_DATA_FOLDER="FaiderSessionTest"; the test's own files go to the temp folder).
//
//   1. A fixture session, written in the format Session::save writes, with every kind of
//      thing a session holds: inputs of several sources (a Windows device that isn't on this
//      machine, a test tone, nothing, the tape deck, interface inputs), buses (one a combinator),
//      outputs, sends between them, levels, colours, channel processing (VoiceChain), scenes,
//      macros, pads, the tape deck, MIDI mappings, app rules, device names; and the features
//      about to go (PFL / cue output, MONITOR level, AUX 1-4, DIRECT, INDEPENDENT, LINK, KEY).
//   2. Round trip: restore it, save, restore that into a fresh engine, save again: the two
//      saves must be the same.
//   3. Particular values survive the round trip.
//   4. Sessions under the earlier names (PatchbaySession, AudioHubSession) load.
//   5. Garbage in a session doesn't crash restore.
//   Also: one strip saved and restored on its own (undo) comes back the same.
//
// Prints PASS/FAIL per check (NOTE lines are information only); exits non-zero on failure.
//
// Checks marked "(deleted-feature check: remove with the feature)" are the only ones that need
// to go when those features are removed from the engine: the round trip and the rest don't
// depend on them.
#include "../src/Session.h"
#include "../src/ProductName.h"

// The editor isn't part of this check (it would bring the whole UI in).
juce::AudioProcessorEditor* VoiceChain::createEditor() { return nullptr; }

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    }

    void note (const juce::String& what) { std::cout << "NOTE  " << what << "\n"; }

    bool near (double a, double b, double tolerance = 1.0e-4) { return std::abs (a - b) <= tolerance; }

    juce::StringArray logLines;
    auto logger() { return [] (const juce::String& line) { logLines.add (line); }; }

    void set (VoiceChain& chain, const char* id, float value)
    {
        auto* p = chain.params.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    float param (const VoiceChain* chain, const char* id)
    {
        if (chain == nullptr)
            return std::numeric_limits<float>::quiet_NaN();
        auto* v = chain->params.getRawParameterValue (id);
        return v != nullptr ? v->load() : std::numeric_limits<float>::quiet_NaN();
    }

    // A VoiceChain's state, as a session keeps it (base64): every stage out, then these set.
    juce::String chainState (std::initializer_list<std::pair<const char*, float>> values)
    {
        VoiceChain chain;
        chain.setAllStages (false);
        for (auto& [id, value] : values)
            set (chain, id, value);
        juce::MemoryBlock state;
        chain.getStateInformation (state);
        return state.toBase64Encoding();
    }

    //==========================================================================
    // Differences between two saved sessions, for a failure's hint (or a NOTE).
    bool sameValue (const juce::String& a, const juce::String& b)
    {
        if (a == b)
            return true;
        auto numeric = [] (const juce::String& s) { return s.trim().isNotEmpty() && s.trim().containsOnly ("0123456789.-+eE"); };
        return numeric (a) && numeric (b) && near (a.getDoubleValue(), b.getDoubleValue());
    }

    juce::String label (const juce::XmlElement& e, int index)
    {
        auto l = e.getTagName() + "[" + juce::String (index) + "]";
        if (e.hasAttribute ("name"))
            l << "(" << e.getStringAttribute ("name") << ")";
        return l;
    }

    juce::String shorten (const juce::String& s) { return s.length() > 60 ? s.substring (0, 57) + "..." : s; }

    // aOnly: only what `a` has (a fixture against what was saved from it; attributes `a` leaves
    // out, `b` may add).
    void diff (const juce::XmlElement& a, const juce::XmlElement& b, const juce::String& path, juce::StringArray& out, bool aOnly)
    {
        for (int i = 0; i < a.getNumAttributes(); ++i)
        {
            const auto name = a.getAttributeName (i);
            const auto value = a.getAttributeValue (i);
            if (! b.hasAttribute (name))
                out.add (path + " @" + name + ": \"" + shorten (value) + "\" -> (not saved)");
            else if (! sameValue (value, b.getStringAttribute (name)))
                out.add (path + " @" + name + ": \"" + shorten (value) + "\" -> \"" + shorten (b.getStringAttribute (name)) + "\"");
        }
        if (! aOnly)
            for (int i = 0; i < b.getNumAttributes(); ++i)
                if (! a.hasAttribute (b.getAttributeName (i)))
                    out.add (path + " @" + b.getAttributeName (i) + ": (none) -> \"" + shorten (b.getAttributeValue (i)) + "\"");

        // Children by tag, in order.
        juce::StringArray tags;
        for (auto* c : a.getChildIterator()) tags.addIfNotAlreadyThere (c->getTagName());
        for (auto* c : b.getChildIterator()) tags.addIfNotAlreadyThere (c->getTagName());
        for (auto& tag : tags)
        {
            juce::Array<juce::XmlElement*> as, bs;
            for (auto* c : a.getChildWithTagNameIterator (tag)) as.add (c);
            for (auto* c : b.getChildWithTagNameIterator (tag)) bs.add (c);
            if (as.size() != bs.size() && (! aOnly || as.size() > bs.size()))
                out.add (path + " <" + tag + ">: " + juce::String (as.size()) + " -> " + juce::String (bs.size()));
            for (int i = 0; i < juce::jmin (as.size(), bs.size()); ++i)
                diff (*as[i], *bs[i], path + "/" + label (*as[i], i), out, aOnly);
        }
    }

    juce::String diffHint (const juce::XmlElement& a, const juce::XmlElement& b, bool aOnly = false, int max = 15)
    {
        juce::StringArray out;
        diff (a, b, a.getTagName(), out, aOnly);
        juce::String text;
        for (int i = 0; i < juce::jmin (max, out.size()); ++i)
            text << "        " << out[i] << "\n";
        if (out.size() > max)
            text << "        ... and " << (out.size() - max) << " more\n";
        return text;
    }

    juce::StringArray differences (const juce::XmlElement& a, const juce::XmlElement& b, bool aOnly)
    {
        juce::StringArray out;
        diff (a, b, a.getTagName(), out, aOnly);
        return out;
    }

    //==========================================================================
    // The fixture. Written as Session::save writes a session (bools 0/1, doubles as text, lists
    // with "\n" as &#10;); $TEMP$ and $...$ are filled in at run time. No rack attributes: the
    // engine's own default placement is saved, and must then round-trip.
    const char* const fixtureText = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<FaiderSession version="1" master="&lt;clock&gt;" hiddenDevices="Speakers (Hidden Test Device)"
               disabledDevices="Ghost Mic (Not On This Machine)"
               remote="&lt;Remote osc=&quot;0&quot; oscPreferred=&quot;9123&quot;&gt;&lt;Map control=&quot;cc:1:7&quot; target=&quot;fader:input:MIC&quot;/&gt;&lt;/Remote&gt;"
               pfl="1" cueOutput="PHONES" monitorLevel="0.5" mixBusOutput="MAIN"
               auxOutputs="PHONES&#10;STREAM&#10;&#10;MAIN" activeScene="1">
  <Pad index="3" name="AIRHORN" file="$TEMP$\missing-airhorn.wav" colour="ff00ff00" gain="0.5" mode="2"
       choke="1" hotkey="F5" macro="" trimStart="0.25" fadeIn="0.125"/>
  <Pad index="5" name="GO LIVE" file="" colour="" gain="1.0" mode="0" choke="0" hotkey="" macro="GO LIVE"/>
  <Macro name="GO LIVE" hotkey="ctrl + F1" undoOnRelease="1">
    <Trigger when="0" what="obs64.exe" seconds="10"/>
    <Step action="0" target="input:MIC" target2="" value="0.5" ms="200"/>
    <Step action="1" target="output:STREAM" target2="" value="1.0" ms="0"/>
  </Macro>
  <Tray strip="input:MIC"/>
  <Tray strip="output:MAIN"/>
  <AppRule exe="Discord.exe" to="output:STREAM"/>
  <Scene name="TALK" hotkey="ctrl + F2" device="">
    <Set target="input:MIC" gain="1.0" pan="0.0" mute="0" solo="1" on="1" insertsOn="1">
      <Send to="output:MAIN" on="1"/>
      <Send to="bus:VOX BUS" on="0"/>
    </Set>
    <Set target="output:MAIN" gain="0.5" pan="0.0" mute="0" solo="0" on="1" insertsOn="1"/>
  </Scene>
  <Scene name="GAME" hotkey="" device="Headphones (Test USB Audio)" withPlugins="1" goBack="1">
    <Trigger when="0" what="game.exe" seconds="10"/>
    <Set target="input:MUSIC" gain="0.25" pan="0.5" mute="1" solo="0" on="1" insertsOn="0"/>
  </Scene>
  <Tape current="-1" level="0.5" shuffle="0" repeat="1" recordFolder="$TEMP$" recordSource="output:MAIN"
        replay="30.0" recordFormat="flac">
    <Track file="$TEMP$\song.wav"/>
  </Tape>
  <DeviceName device="Microphone (Test USB Mic)" name="DESK MIC"/>
  <Bus name="MAIN" candidates="&lt;master&gt;" firstChannel="0" stereo="1" gain="0.5" mute="0" loopback="0"
       enabled="1" safe="1" mixBus="$MIXBUS$" independent="1" width="1.5" colour="ff3d8fd1" locked="1"/>
  <Bus name="PHONES" candidates="Headphones (Test USB Audio)" firstChannel="0" stereo="1" gain="1.0" mute="0"
       loopback="0" enabled="1" safe="0" headphoneEq="Sennheiser HD 650" colour="">
    <AlsoOn device="Speakers (Test Realtek)" firstChannel="0"/>
  </Bus>
  <Bus name="STREAM" candidates="" firstChannel="0" stereo="0" gain="0.75" mute="1" loopback="0" enabled="0"
       safe="0" minusOf="MAIN" minusInput="MIC" silenceAlert="30" clipAlert="1" colour="ffaa0000"/>
  <Strip name="MIC" kind="1" firstChannel="0" device="Microphone (Test USB Mic)" stereo="0" busIndex="-1"
         gain="1.5" pan="-0.25" width="1.0" mute="0" enabled="1" insertsOn="1" keyFrom="MUSIC"
         channel="$CHANNEL_MIC$" locked="1" colour="ff112233" sends="0,1" rawSends="1" preSends="2"
         altKind="2" altFirstChannel="0" altDevice="" altStereo="1" altBusIndex="-1" onLineB="0"
         soloIsolate="1" cuesOn="0" direct="1" binaural="1" cuePan="-0.5" linkWith="GUEST" pairSends="0"
         sendLevels="0:-6.0,1:3.5" pairSendLevels="0:-3.0"/>
  <Strip name="MUSIC" kind="2" firstChannel="0" device="" stereo="1" busIndex="-1" gain="0.25" pan="0.5"
         width="0.5" mute="1" enabled="1" insertsOn="1" duckBy="MIC" duckDepth="-18.0" duckThreshold="-35.0"
         duckRelease="400.0" locked="0" colour="" sends="0" rawSends="" preSends="" soloIsolate="0" cuesOn="1"
         direct="0" binaural="0" cuePan="0.0" linkWith="" pairSends="1" pathSends="1:2"/>
  <Strip name="GUEST" kind="4" firstChannel="0" device="" stereo="1" busIndex="-1" gain="1.0" pan="0.0"
         width="1.0" mute="0" enabled="0" insertsOn="1" locked="0" padsInto="1" colour="" sends="2" rawSends=""
         preSends="" soloIsolate="0" cuesOn="1" direct="1" binaural="0" cuePan="0.25" linkWith="MIC" pairSends=""/>
  <Strip name="TAPE IN" kind="8" firstChannel="0" device="" stereo="1" busIndex="-1" gain="1.0" pan="0.0"
         width="1.0" mute="0" enabled="1" insertsOn="1" locked="0" colour="" sends="0" rawSends="" preSends=""
         soloIsolate="0" cuesOn="1" direct="0" binaural="0" cuePan="0.0" linkWith="" pairSends=""/>
  <Strip name="LINE 3" kind="0" firstChannel="2" device="" stereo="0" busIndex="-1" gain="1.0" pan="0.0"
         width="1.0" mute="0" enabled="1" insertsOn="1" locked="0" colour="" sends="1" rawSends="" preSends=""
         soloIsolate="0" cuesOn="1" direct="0" binaural="0" cuePan="0.0" linkWith="" pairSends=""/>
  <Pair name="VOX BUS" stereo="1" gain="0.75" pan="0.25" width="1.25" mute="0" enabled="1" insertsOn="0"
        channel="$CHANNEL_BUS$" colour="ff445566" locked="1" sends="0" pairSends="1" sendLevels="0:-2.0"/>
  <Pair name="FX COMBO" stereo="1" gain="1.0" pan="0.0" width="1.0" mute="0" enabled="1" insertsOn="1"
        colour="" sends="0,1" pairSends="" combinator="1">
    <Path name="DRY" gain="1.0" mute="0"/>
    <Path name="WET" gain="0.5" mute="1"/>
  </Pair>
</FaiderSession>
)XML";

    std::unique_ptr<juce::XmlElement> parse (juce::String text, const juce::File& temp)
    {
        text = text.replace ("$TEMP$", temp.getFullPathName())
                   .replace ("$CHANNEL_MIC$", chainState ({ { "inputOn", 1.0f }, { "trim", 6.0f }, { "drive", 4.5f } }))
                   .replace ("$CHANNEL_BUS$", chainState ({ { "trim", -3.0f } }))
                   .replace ("$MIXBUS$", chainState ({ { "drive", 2.0f } }));
        return juce::XmlDocument::parse (text);
    }

    const AudioEngine::Strip* findStrip (const std::vector<std::unique_ptr<AudioEngine::Strip>>& list, const juce::String& name)
    {
        for (auto& s : list)
            if (s->name == name)
                return s.get();
        return nullptr;
    }

    const AudioEngine::Master* findMaster (const AudioEngine& engine, const juce::String& name)
    {
        for (auto& m : engine.getMasters())
            if (m->name == name)
                return m.get();
        return nullptr;
    }

    juce::StringArray names (const std::vector<std::unique_ptr<AudioEngine::Strip>>& list)
    {
        juce::StringArray n;
        for (auto& s : list) n.add (s->name);
        return n;
    }

    juce::StringArray names (const std::vector<std::unique_ptr<AudioEngine::Master>>& list)
    {
        juce::StringArray n;
        for (auto& s : list) n.add (s->name);
        return n;
    }

    // One strip saved, removed, restored (it comes back at the end) and saved again: the same.
    void checkStripUndo (AudioEngine& engine, const juce::String& kind, int index, const juce::String& what)
    {
        auto before = Session::saveStrip (engine, kind, index);
        if (before == nullptr)
        {
            check (false, "undo: " + what + ": saveStrip gives nothing");
            return;
        }
        if (kind == "input")       engine.removeStrip (index);
        else if (kind == "bus")    engine.removeBus (index);
        else                       engine.removeMaster (index);
        const int back = Session::restoreStrip (engine, *before, logger());
        auto after = back >= 0 ? Session::saveStrip (engine, kind, back) : nullptr;
        // (A bus's removedAt is where it was; it comes back at the end. Compared without it.)
        if (after != nullptr && before->hasAttribute ("removedAt"))
        {
            before->removeAttribute ("removedAt");
            after->removeAttribute ("removedAt");
        }
        const bool same = after != nullptr && before->isEquivalentTo (after.get(), false);
        check (same, "undo: " + what + " saved, removed and restored comes back the same");
        if (! same && after != nullptr)
            std::cout << diffHint (*before, *after).toStdString();
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;   // (timers and parameters expect a message manager)

    // Never the real user folder.
    const auto data = Product::dataFolder();
    check (data.getFileName() == "FaiderSessionTest", "built with its own data folder (" + data.getFullPathName() + ")");
    if (data.getFileName() != "FaiderSessionTest")
        return 1;

    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("faider-session-test");
    temp.deleteRecursively();
    temp.createDirectory();

    auto fixture = parse (fixtureText, temp);
    check (fixture != nullptr, "the fixture parses");
    if (fixture == nullptr)
        return 1;

    //==========================================================================
    // 1-2: restore, save, restore into a fresh engine, save again.
    std::unique_ptr<juce::XmlElement> first, second;
    {
        AudioEngine a (logger());
        check (Session::restore (a, *fixture, logger(), {}), "restore: the fixture is a session");
        check (Session::getInterfaceName (*fixture) == AudioEngine::builtInClock, "getInterfaceName: the master it was saved with");
        first = Session::save (a, Session::getInterfaceName (*fixture));
    }
    {
        AudioEngine b (logger());
        check (Session::restore (b, *first, logger(), {}), "restore: what was saved is a session");
        second = Session::save (b, Session::getInterfaceName (*first));

        const bool same = first->isEquivalentTo (second.get(), false);
        check (same, "round trip: saved, restored into a fresh engine and saved again, the same");
        if (! same)
            std::cout << "      differences (first save -> second save):\n" << diffHint (*first, *second).toStdString();
        const auto textOf = [] (const juce::XmlElement& x) { return x.toString (juce::XmlElement::TextFormat().withoutHeader()); };
        check (textOf (*first) == textOf (*second), "round trip: the same text, byte for byte");

        {
            juce::XmlElement changed (*second);   // (the comparison itself can see a change)
            if (auto* strip = changed.getChildByName ("Strip"))
                strip->setAttribute ("gain", 0.123);
            check (! first->isEquivalentTo (&changed, false) && differences (*first, changed, false).size() == 1,
                   "round trip: a changed gain would be caught (and named)");
        }

        // What the first restore didn't keep of the fixture (information: values Session reads
        // but writes differently, or doesn't write).
        const auto lost = differences (*fixture, *first, true);
        for (auto& line : lost)
            note ("fixture -> first save: " + line);
        if (lost.isEmpty())
            note ("fixture -> first save: every attribute the fixture sets was saved back with its value");

        //======================================================================
        // 3: particular values, in the engine the round trip restored into.
        check (names (b.getStrips()) == juce::StringArray ({ "MIC", "MUSIC", "GUEST", "TAPE IN", "LINE 3" }),
               "inputs: all five, in order (" + names (b.getStrips()).joinIntoString (", ") + ")");
        check (names (b.getBuses()) == juce::StringArray ({ "VOX BUS", "FX COMBO" }), "buses: both, in order");
        check (names (b.getMasters()) == juce::StringArray ({ "MAIN", "PHONES", "STREAM" }), "outputs: all three, in order");

        if (auto* mic = findStrip (b.getStrips(), "MIC"))
        {
            check (mic->source.kind == AudioEngine::Source::device && mic->source.deviceName == "Microphone (Test USB Mic)",
                   "MIC: a Windows device not on this machine, remembered by name");
            check (! mic->source.stereo, "MIC: mono");
            check (near (mic->gain.load(), 1.5) && near (mic->pan.load(), -0.25), "MIC: gain and pan");
            check (mic->sends[0].load() && mic->sends[1].load() && ! mic->sends[2].load(), "MIC: sent to MAIN and PHONES, not STREAM");
            check (mic->rawSends[1].load() && ! mic->rawSends[0].load(), "MIC: RAW to PHONES only");
            check (mic->preSends[2].load(), "MIC: PRE to STREAM");
            check (near (mic->sendDb[0].load(), -6.0) && near (mic->sendDb[1].load(), 3.5), "MIC: send levels");
            check (mic->busSends[0].load() && near (mic->busSendDb[0].load(), -3.0), "MIC: sent to VOX BUS at -3 dB");
            check (mic->colour == juce::Colour (0xff112233) && mic->locked, "MIC: colour and lock");
            check (mic->altSource.has_value() && mic->altSource->kind == AudioEngine::Source::tone, "MIC: LINE B (a test tone)");
            check (mic->soloIsolate.load() && mic->binaural.load(), "MIC: SOLO ISOLATE and BINAURAL");
            check (near (param (mic->channel.get(), "drive"), 4.5) && near (param (mic->channel.get(), "trim"), 6.0)
                       && near (param (mic->channel.get(), "inputOn"), 1.0),
                   "MIC: channel processing (input stage on, trim +6 dB, drive 4.5 dB)");
            check (mic->keyFrom == "MUSIC", "MIC: KEY from MUSIC");     // (kept: EXT's key, until it gets a control or goes)
        }
        else
            check (false, "MIC is there");

        if (auto* music = findStrip (b.getStrips(), "MUSIC"))
        {
            check (music->source.kind == AudioEngine::Source::tone, "MUSIC: a test tone");
            check (music->mute.load() && near (music->width.load(), 0.5), "MUSIC: muted, width 0.5");
            check (music->duckBy == "MIC" && near (music->duckDepth.load(), -18.0) && near (music->duckThreshold.load(), -35.0)
                       && near (music->duckRelease.load(), 400.0),
                   "MUSIC: ducked by MIC (depth, threshold, release)");
            check (music->busSends[1].load() && music->pathSends[1].load() == 2u, "MUSIC: sent into FX COMBO's second path");
        }
        else
            check (false, "MUSIC is there");

        if (auto* guest = findStrip (b.getStrips(), "GUEST"))
        {
            check (guest->source.kind == AudioEngine::Source::none, "GUEST: no source");
            check (! guest->enabled.load() && guest->padsInto.load() && guest->sends[2].load(), "GUEST: off, pads into it, sent to STREAM");
        }
        else
            check (false, "GUEST is there");

        if (auto* line = findStrip (b.getStrips(), "LINE 3"))
            check (line->source.kind == AudioEngine::Source::interfaceInputs && line->source.firstChannel == 2, "LINE 3: interface input 3");
        if (auto* tape = findStrip (b.getStrips(), "TAPE IN"))
            check (tape->source.kind == AudioEngine::Source::tape, "TAPE IN: the tape deck");

        if (auto* vox = findStrip (b.getBuses(), "VOX BUS"))
        {
            check (near (vox->gain.load(), 0.75) && near (vox->pan.load(), 0.25) && near (vox->width.load(), 1.25), "VOX BUS: gain, pan, width");
            check (! vox->insertsOn.load() && vox->locked && vox->colour == juce::Colour (0xff445566), "VOX BUS: INSERT off, locked, colour");
            check (vox->sends[0].load() && near (vox->sendDb[0].load(), -2.0), "VOX BUS: to MAIN at -2 dB");
            check (vox->busSends[1].load(), "VOX BUS: into FX COMBO (a bus-to-bus route)");
            check (near (param (vox->channel.get(), "trim"), -3.0), "VOX BUS: channel trim -3 dB");
        }
        else
            check (false, "VOX BUS is there");

        if (auto* combo = findStrip (b.getBuses(), "FX COMBO"))
            check (combo->combinator && combo->paths.size() == 2 && combo->paths[0]->name == "DRY" && combo->paths[1]->name == "WET"
                       && near (combo->paths[1]->gain.load(), 0.5) && combo->paths[1]->mute.load(),
                   "FX COMBO: a combinator with its two paths (DRY, WET at 0.5, muted)");
        else
            check (false, "FX COMBO is there");

        if (auto* main = findMaster (b, "MAIN"))
        {
            check (main->candidates == juce::StringArray (AudioEngine::interfaceOutput), "MAIN: on the master interface");
            check (near (main->gain.load(), 0.5) && main->safeOn.load() && near (main->width.load(), 1.5), "MAIN: gain, SAFE, width");
            check (main->colour == juce::Colour (0xff3d8fd1) && main->locked, "MAIN: colour and lock");
            check (near (param (main->masterChain.get(), "drive"), 2.0), "MAIN: MIX BUS settings");
        }
        else
            check (false, "MAIN is there");

        if (auto* phones = findMaster (b, "PHONES"))
        {
            check (phones->candidates == juce::StringArray (juce::String ("Headphones (Test USB Audio)")), "PHONES: its device, by name");
            check (phones->alsoOn.size() == 1 && phones->alsoOn[0].candidate == "Speakers (Test Realtek)", "PHONES: also on a second device");
            check (phones->headphoneProfile == "Sennheiser HD 650", "PHONES: headphone correction");
        }
        else
            check (false, "PHONES is there");

        if (auto* stream = findMaster (b, "STREAM"))
            check (! stream->stereo && stream->mute.load() && ! stream->enabled.load() && stream->minusOf == "MAIN"
                       && stream->minusInput == "MIC" && stream->silenceAlert == 30 && stream->clipAlert,
                   "STREAM: mono, muted, off, mix-minus of MAIN without MIC, alerts");
        else
            check (false, "STREAM is there");

        check (b.getScenes().list.size() == 2 && b.getScenes().active == 1 && b.getScenes().list[0].name == "TALK"
                   && b.getScenes().list[0].settings.size() == 2 && b.getScenes().list[1].withPlugins && b.getScenes().list[1].goBack,
               "scenes: both, the second active");
        check (b.getMacros().list.size() == 1 && b.getMacros().list[0].steps.size() == 2 && b.getMacros().list[0].triggers.size() == 1,
               "macros: GO LIVE with its steps and trigger");
        check (b.getPads().pad (3).name == "AIRHORN" && b.getPads().pad (3).file.endsWith ("missing-airhorn.wav")
                   && b.getPads().pad (5).macro == "GO LIVE",
               "pads: a sound pad (its file missing, kept) and a macro pad");
        check (b.mainMixMaster == "MAIN", "the MIX bus output");
        check (b.trayStrips == juce::StringArray ({ "input:MIC", "output:MAIN" }), "the tray mixer's strips");
        check (b.appRules.size() == 1 && b.appRules[0].exe == "Discord.exe" && b.appRules[0].to == "output:STREAM", "app rules");
        check (b.getHiddenDevices().contains ("Speakers (Hidden Test Device)"), "hidden devices");
        check (b.getDisabledDevices().contains ("Ghost Mic (Not On This Machine)"), "devices off in Faider (one not on this machine)");
        check (b.getRecorder().format == "flac" && b.getRecorder().chosen == "output:MAIN"
                   && b.getRecorder().folder == temp,
               "the recorder's format, source and folder");
        check (first->getChildByName ("DeviceName") != nullptr
                   && first->getChildByName ("DeviceName")->getStringAttribute ("name") == "DESK MIC",
               "device names");
        check (b.getRemote().saveState().contains ("fader:input:MIC") && b.getRemote().saveState().contains ("9123"),
               "MIDI mappings and the OSC port");


        //======================================================================
        // One strip on its own (undo): an input, a bus with what sends to it, an output.
        checkStripUndo (b, "input", 0, "input MIC");
        checkStripUndo (b, "bus", (int) b.getBuses().size() - 1, "bus FX COMBO (with what sent to it)");
        checkStripUndo (b, "output", (int) b.getMasters().size() - 1, "output STREAM (with what sent to it)");
        if (auto* mic = findStrip (b.getStrips(), "MIC"))   // (saveStrip keeps an output's senders' sends, levels and RAW, not PRE)
            note (juce::String ("undo of output STREAM: MIC's PRE send to it ") + (mic->preSends[2].load() ? "came back" : "did not come back"));
        check (Session::saveStrip (b, "input", 99) == nullptr && Session::saveStrip (b, "nonsense", 0) == nullptr,
               "saveStrip: nothing for a strip that isn't there");
    }

    //==========================================================================
    // 4: sessions saved under the earlier names.
    for (auto* tag : { "PatchbaySession", "AudioHubSession" })
    {
        auto old = std::make_unique<juce::XmlElement> (*first);
        old->setTagName (tag);
        AudioEngine e (logger());
        const bool ok = Session::restore (e, *old, logger(), {});
        check (ok && e.getStrips().size() == 5 && e.getBuses().size() == 2 && e.getMasters().size() == 3,
               juce::String (tag) + ": loads, with its inputs, buses and outputs");
        auto resaved = Session::save (e, Session::getInterfaceName (*old));
        check (resaved->hasTagName ("FaiderSession") && resaved->isEquivalentTo (first.get(), false),
               juce::String (tag) + ": saved again as a FaiderSession, the same mix");
    }
    {
        // A small one written by hand, as the oldest sessions are: few attributes, the rest default.
        auto old = juce::XmlDocument::parse (R"XML(<PatchbaySession version="1" master="Focusrite USB ASIO">
              <Bus name="Speakers" candidates="Speakers (Realtek)&#10;&lt;master&gt;" firstChannel="0" stereo="1" gain="1.0"/>
              <Strip name="mic" kind="0" firstChannel="0" stereo="0" gain="0.5" sends="0"/>
              <Strip name="old loopback" kind="3" busIndex="0" sends="0"/>
              <Master name="raw tap"/>
              <Bus name="Old master with plugins" candidates="&lt;master&gt;"><Insert name="x" path="C:\nowhere\x.vst3" state=""/></Bus>
            </PatchbaySession>)XML");
        AudioEngine e (logger());
        logLines.clear();
        const bool ok = old != nullptr && Session::restore (e, *old, logger(), {});
        check (ok && Session::getInterfaceName (*old) == "Focusrite USB ASIO", "oldest format: loads, its master named");
        check (e.getStrips().size() == 1 && e.getStrips()[0]->name == "MIC" && near (e.getStrips()[0]->gain.load(), 0.5)
                   && e.getStrips()[0]->sends[0].load(),
               "oldest format: the input (name in capitals), its gain and send; the loopback input dropped");
        check (e.getMasters().size() == 2 && e.getMasters()[0]->candidates == juce::StringArray (juce::String ("Speakers (Realtek)")),
               "oldest format: outputs keep only the device chosen (no fallback)");
        check (logLines.joinIntoString ("\n").contains ("loopback input") && logLines.joinIntoString ("\n").contains ("MASTER tap"),
               "oldest format: what's dropped is said in the log");
    }

    //==========================================================================
    // 5: garbage doesn't crash restore (and what comes of it still saves and loads).
    {
        auto broken = juce::XmlDocument::parse (R"XML(<FaiderSession version="999" master="" hiddenDevices="&#10;&#10;"
              remote="&lt;not xml" pfl="maybe" monitorLevel="nan" auxOutputs="a&#10;b&#10;c&#10;d&#10;e&#10;f" activeScene="99">
              <Pad index="-4" name="x"/><Pad index="1000" name="y"/><Pad index="2" file="::bad::path::" mode="77" gain="abc"/>
              <Macro name=""><Step action="999" ms="-5"/><Trigger when="-3"/></Macro>
              <Scene name="S"><Trigger when="999"/><Set target="" gain="inf"><Send/><Plugin state="!!!not base64!!!"/></Set></Scene>
              <Tape current="50" repeat="77" level="-9" recordFolder="relative\folder" replay="-1"><Track file=""/><Track/></Tape>
              <Bus name="" candidates="&#10;&#10;x&#10;y" firstChannel="-3" stereo="perhaps" gain="1e40" width="-7" colour="zzzz"
                   headphoneEq="No Such Headphones" mixBus="@@@" silenceAlert="-1"><AlsoOn firstChannel="-9"/><AlsoOn device="&lt;master&gt;" firstChannel="1000"/></Bus>
              <Strip name="" kind="999" firstChannel="-50" gain="abc" pan="7" width="-1" sends="x,,-3,99,1" rawSends="," preSends="8"
                     sendLevels="garbage:::,5:1e40,-1:3,:" pairSendLevels="0:" pathSends="-1:7,100:3,0:-1" busIndex="77"
                     channel="AAAA" cuePan="12" altKind="-4" altFirstChannel="-2" rackLeft="p9,nonsense,in,out,p-1"
                     rackRight="," rackDeleted="-1" rackCables="in>p7,>,out>in,eq>eq,filters>out">
                <Insert name="missing" path="C:\nowhere\missing.vst3" state="xyz" mix="nan"/>
              </Strip>
              <Strip name="A" kind="1" device="" stereo="1"/><Strip name="A" kind="1"/>
              <Pair name="P" pairSends="0,0,42,-1" pathSends="0:99" combinator="1"><Path/><Path name="x" gain="nan"/></Pair>
              <Pair name="Q" pairSends="0" rackLeft="p0" rackCables="p0>p0"/>
              <Pair name="R" combinator="0"><Path name="not a combinator"/></Pair>
              <Unknown thing="1"/>
            </FaiderSession>)XML");
        check (broken != nullptr, "broken: the garbage parses as XML");
        if (broken != nullptr)
        {
            AudioEngine e (logger());
            const bool ok = Session::restore (e, *broken, logger(), {});
            check (ok, "broken: restore gets through garbage attributes without crashing");
            check (e.getStrips().size() == 3 && e.getStrips()[1]->name != e.getStrips()[2]->name,
                   "broken: every input made, two of the same name made unique ("
                       + names (e.getStrips()).joinIntoString (", ") + ")");
            check (e.getStrips()[0]->source.firstChannel >= 0, "broken: a negative channel is not used");
            auto resaved = Session::save (e, {});
            AudioEngine again (logger());
            check (resaved != nullptr && Session::restore (again, *resaved, logger(), {}), "broken: what it made saves and loads again");

            // One strip of garbage on its own (undo).
            juce::XmlElement junk ("Strip");
            junk.setAttribute ("kind", "-12");
            junk.setAttribute ("sends", ",,,999");
            check (Session::restoreStrip (e, junk, logger()) >= 0, "broken: restoreStrip takes a garbage strip");
            check (Session::restoreStrip (e, juce::XmlElement ("Nonsense"), logger()) == -1, "broken: restoreStrip refuses what isn't a strip");
        }

        AudioEngine e (logger());
        check (! Session::restore (e, juce::XmlElement ("NotASession"), logger(), {}) && e.getStrips().empty(),
               "not a session: refused, nothing changed");
        check (Session::getInterfaceName (juce::XmlElement ("NotASession")).isEmpty(), "not a session: no master named");
    }

    temp.deleteRecursively();
    check (! data.exists(), "nothing written to a data folder (" + data.getFullPathName() + ")");

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (failures)) << "\n";
    return failures;
}

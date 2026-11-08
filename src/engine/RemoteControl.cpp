#include "RemoteControl.h"
#include "AudioEngine.h"

namespace
{
    // A strip, pair or output by "kind:NAME", any case. The exact name first; then, as OSC
    // addresses can't hold spaces, with _ read as a space (a name may hold a real _).
    struct Found { AudioEngine::Strip* strip = nullptr; AudioEngine::Master* bus = nullptr; };

    Found find (AudioEngine& engine, const juce::String& kind, const juce::String& name)
    {
        auto look = [&] (const juce::String& n)
        {
            Found f;
            if (kind == "input")
                for (auto& s : engine.getStrips()) { if (s->name.equalsIgnoreCase (n)) f.strip = s.get(); }
            else if (kind == "bus")
                for (auto& s : engine.getBuses())  { if (s->name.equalsIgnoreCase (n)) f.strip = s.get(); }
            else if (kind == "output")
                for (auto& b : engine.getMasters())  { if (b->name.equalsIgnoreCase (n)) f.bus = b.get(); }
            return f;
        };
        auto f = look (name);
        if (f.strip == nullptr && f.bus == nullptr && name.containsChar ('_'))
            f = look (name.replaceCharacter ('_', ' '));
        return f;
    }

    // A scene's or macro's place in `list` by name, the same way (exact first, then _ as space); -1 if none.
    template <typename List>
    int findNamed (const List& list, const juce::String& name)
    {
        for (int pass = 0; pass < 2; ++pass)
        {
            const auto n = pass == 0 ? name : name.replaceCharacter ('_', ' ');
            for (int i = 0; i < (int) list.size(); ++i)
                if (list[(size_t) i].name.equalsIgnoreCase (n))
                    return i;
        }
        return -1;
    }

    // A controller's 0..1 to fader gain: -inf at the bottom, then -60 to +12 dB, even in dB.
    float faderGain (float value) { return value <= 0.002f ? 0.0f : juce::Decibels::decibelsToGain (-60.0f + 72.0f * value); }
}

juce::String RemoteControl::describe (const juce::String& key)
{
    juce::StringArray parts;
    parts.addTokens (key, ":", "");
    return (parts[0] == "cc" ? "CC " : "NOTE ") + parts[2] + " CH " + parts[1];
}

RemoteControl::RemoteControl (AudioEngine& e) : engine (e)
{
    osc.addListener (this);
}

RemoteControl::~RemoteControl()
{
    for (auto& in : inputs)
        in->stop();
    osc.disconnect();
}

void RemoteControl::setInputOn (const juce::String& identifier, bool on)
{
    if (on) enabledInputs.addIfNotAlreadyThere (identifier);
    else    enabledInputs.removeString (identifier);
    openInputs();
}

void RemoteControl::openInputs()
{
    for (auto& in : inputs)
        in->stop();
    inputs.clear();
    for (auto& device : juce::MidiInput::getAvailableDevices())
        if (enabledInputs.contains (device.identifier))
            if (auto in = juce::MidiInput::openDevice (device.identifier, this))
            {
                in->start();
                inputs.push_back (std::move (in));
            }
}

void RemoteControl::learn (const juce::String& target)
{
    learnTarget = target;
    if (log) log ("midi: learning " + target + " (move a control)");
}

void RemoteControl::clearTarget (const juce::String& target)
{
    for (auto it = mapping.begin(); it != mapping.end();)
        it = it->second == target ? mapping.erase (it) : std::next (it);
}

// Every "kind:NAME" pair in a target is renamed: "fader:input:OLD", "send:input:A:output:OLD".
// Matched as text (a name may hold ':'): ":kind:OLD" followed by the end or another field.
void RemoteControl::renameTarget (const juce::String& oldKey, const juce::String& newKey)
{
    const auto from = ":" + oldKey, to = ":" + newKey;
    auto rename = [&] (juce::String& target)
    {
        for (int pos = 0;;)
        {
            const int at = target.indexOf (pos, from);
            if (at < 0)
                break;
            const int end = at + from.length();
            if (end == target.length() || target[end] == ':')
            {
                target = target.substring (0, at) + to + target.substring (end);
                pos = at + to.length();
            }
            else
                pos = end;
        }
    };
    for (auto& [control, target] : mapping)
        rename (target);
    rename (learnTarget);
}

juce::String RemoteControl::controlFor (const juce::String& target) const
{
    for (auto& [key, t] : mapping)
        if (t == target)
            return describe (key);
    return {};
}

// MIDI thread: hand the control over to the message thread.
void RemoteControl::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    juce::String key;
    float value = 0.0f;
    bool pressed = false, released = false;
    if (m.isController())
    {
        key = "cc:" + juce::String (m.getChannel()) + ":" + juce::String (m.getControllerNumber());
        value = (float) m.getControllerValue() / 127.0f;
    }
    else if (m.isNoteOn() || m.isNoteOff())
    {
        key = "note:" + juce::String (m.getChannel()) + ":" + juce::String (m.getNoteNumber());
        value = m.isNoteOn() ? (float) m.getVelocity() / 127.0f : 0.0f;
        pressed = m.isNoteOn();
        released = m.isNoteOff();
    }
    else
        return;

    juce::MessageManager::callAsync ([this, alive = std::weak_ptr<bool> (alive), key, value, pressed, released]
    {
        if (alive.lock() != nullptr)   // not if this has gone in the meantime (quitting)
            control (key, value, pressed, released);
    });
}

void RemoteControl::control (const juce::String& key, float value, bool pressed, bool released)
{
    // A CC used as a key: pressed when it crosses the middle going up, released going down.
    if (key.startsWith ("cc:"))
    {
        const float before = lastValue.count (key) > 0 ? lastValue[key] : 0.0f;
        lastValue[key] = value;
        pressed = before < 0.5f && value >= 0.5f;
        released = before >= 0.5f && value < 0.5f;
    }

    if (learnTarget.isNotEmpty())
    {
        lastControl = key;
        lastControlValue = value;
        if (key.startsWith ("note:") && ! pressed)
            return;   // learn on the press
        clearTarget (learnTarget);
        mapping[key] = learnTarget;
        if (log) log ("midi: " + describe (key) + " -> " + learnTarget);
        learnTarget.clear();
        return;
    }

    lastControl = key;
    lastControlValue = value;
    if (auto it = mapping.find (key); it != mapping.end())
        apply (it->second, value, pressed, released, key.startsWith ("note:"));
}

void RemoteControl::apply (const juce::String& target, float value, bool pressed, bool released, bool fromKey)
{
    const auto what = target.upToFirstOccurrenceOf (":", false, false);
    const auto rest = target.fromFirstOccurrenceOf (":", false, false);
    juce::StringArray t;
    t.addTokens (target, ":", "");

    if (what == "scene")
    {
        if (pressed)
            if (const int i = engine.getScenes().find (rest); i >= 0)
                engine.getScenes().recall (i);
        return;
    }
    if (what == "macro")
    {
        if (const int i = engine.getMacros().find (rest); i >= 0)
        {
            if (pressed)  engine.getMacros().press (i);
            if (released) engine.getMacros().release (i);
        }
        return;
    }

    // The fields again with names that hold ':' put back together: NAME is the longest run of
    // tokens that names a channel of that KIND; a send's TO is the rest; a plugin's name runs up
    // to the setting's number at the end.
    {
        int afterName = juce::jmin (3, t.size());
        for (int end = t.size(); end > 3; --end)
        {
            const auto f = find (engine, t[1], t.joinIntoString (":", 2, end - 2));
            if (f.strip != nullptr || f.bus != nullptr) { afterName = end; break; }
        }
        juce::StringArray n;
        for (int i = 0; i < 2 && i < t.size(); ++i) n.add (t[i]);
        if (t.size() > 2) n.add (t.joinIntoString (":", 2, afterName - 2));
        for (int i = afterName; i < t.size(); ++i) n.add (t[i]);
        if (what == "send" && n.size() > 5)
            n = juce::StringArray { n[0], n[1], n[2], n[3], n.joinIntoString (":", 4, n.size() - 4) };
        if (what == "plug" && n.size() > 5)
            n = juce::StringArray { n[0], n[1], n[2], n.joinIntoString (":", 3, n.size() - 4), n[n.size() - 1] };
        t = n;
    }

    const auto f = find (engine, t[1], t[2]);   // what:KIND:NAME[:...]
    std::atomic<float>* gain = f.strip != nullptr ? &f.strip->gain : f.bus != nullptr ? &f.bus->gain : nullptr;
    std::atomic<bool>* mute = f.strip != nullptr ? &f.strip->mute : f.bus != nullptr ? &f.bus->mute : nullptr;

    if (what == "fader" && gain != nullptr && ! fromKey)
        gain->store (faderGain (value));
    else if (what == "pan" && f.strip != nullptr && ! fromKey)
        f.strip->pan = value * 2.0f - 1.0f;
    else if (what == "mute" && mute != nullptr && pressed)
        mute->store (! mute->load());
    else if (what == "solo" && f.strip != nullptr && pressed)
        f.strip->solo = ! f.strip->solo.load();
    else if (what == "on" && pressed)
    {
        if (f.strip != nullptr)
            engine.setStripEnabled (*f.strip, ! f.strip->enabled.load());
        for (int i = 0; f.bus != nullptr && i < (int) engine.getMasters().size(); ++i)
            if (engine.getMasters()[(size_t) i].get() == f.bus)
                engine.setMasterEnabled (i, ! f.bus->enabled.load());
    }
    else if (what == "inserts" && pressed && f.strip != nullptr)
        f.strip->insertsOn = ! f.strip->insertsOn.load();
    else if (what == "safe" && pressed && f.bus != nullptr)
        f.bus->safeOn = ! f.bus->safeOn.load();
    else if (what == "send" && pressed && f.strip != nullptr && t.size() >= 5)
    {
        // send:FROMKIND:FROM:TOKIND:TO, to an output (sends) or a bus (busSends).
        const auto to = t[3] == "output" ? engine.getMasters().size() : engine.getBuses().size();
        for (int i = 0; i < (int) to; ++i)
        {
            const auto& name = t[3] == "output" ? engine.getMasters()[(size_t) i]->name : engine.getBuses()[(size_t) i]->name;
            if (! name.equalsIgnoreCase (t[4]))
                continue;
            if (t[3] == "output")
                f.strip->sends[(size_t) i] = ! f.strip->sends[(size_t) i].load();
            else if (t[1] == "input")
                f.strip->busSends[(size_t) i] = ! f.strip->busSends[(size_t) i].load();
            else
                for (int p = 0; p < (int) engine.getBuses().size(); ++p)
                    if (engine.getBuses()[(size_t) p].get() == f.strip)
                        engine.setBusSend (p, i, ! f.strip->busSends[(size_t) i].load());
            break;
        }
    }
    else if (what == "param" && f.strip != nullptr && f.strip->channel != nullptr && t.size() >= 4)
    {
        // param:KIND:NAME:ID, one of the channel's EQ / dynamics / FX settings: a knob follows
        // the control; an on/off switch (or a choice) flips on a key press.
        if (auto* p = f.strip->channel->params.getParameter (t[3]))
        {
            const int steps = p->getNumSteps();
            if (steps <= 3 && steps > 0)
            {
                if (pressed)
                    p->setValueNotifyingHost (p->getValue() >= 0.5f ? 0.0f : 1.0f);
            }
            else if (! fromKey)
                p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value));
        }
    }
    else if (what == "plug" && t.size() >= 5)
    {
        // plug:KIND:NAME:PLUGIN:N, setting N of the first insert named PLUGIN on that strip
        // (by name, so it follows the plugin when inserts are moved).
        auto* chain = f.strip != nullptr ? &f.strip->inserts : nullptr;
        for (size_t i = 0; chain != nullptr && i < chain->size(); ++i)
            if ((*chain)[i]->getName() == t[3])
            {
                const auto& params = (*chain)[i]->getParameters();
                if (auto* p = params[t[4].getIntValue()])
                {
                    const int steps = p->getNumSteps();
                    if (p->isBoolean() || (steps <= 3 && steps > 0))
                    {
                        if (pressed)
                            p->setValueNotifyingHost (p->getValue() >= 0.5f ? 0.0f : 1.0f);
                    }
                    else if (! fromKey)
                        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value));
                }
                break;
            }
    }
    else if (appTarget)
        appTarget (target, value, pressed, released);   // the app's own keys: MIC, pads, tape, recorder
}

bool RemoteControl::setOscPort (int port)
{
    osc.disconnect();
    oscSocket.reset();
    const bool wasOn = oscPort > 0;
    oscPort = 0;
    if (port <= 0)
    {
        if (wasOn && log) log ("osc: off");
        return true;
    }
    oscPreferredPort = port;   // what ENABLE comes back to
    oscSocket = std::make_unique<juce::DatagramSocket> (false);
    if (! oscSocket->bindToPort (port, "127.0.0.1") || ! osc.connectToSocket (*oscSocket))
    {
        osc.disconnect();
        oscSocket.reset();
        if (log) log ("osc: couldn't listen on port " + juce::String (port));
        return false;
    }
    oscPort = port;
    if (log) log ("osc: listening on port " + juce::String (port) + " (this PC only)");
    return true;
}

bool RemoteControl::setOscPreferredPort (int port)
{
    if (port < 1024 || port > 65535)
        return false;
    oscPreferredPort = port;
    if (log) log ("osc: port " + juce::String (port));
    return oscPort > 0 && oscPort != port ? setOscPort (port) : true;   // on now: it moves there
}

// /faider/<kind>/<NAME>/<fader|pan|mute|solo> <value>, /faider/scene <NAME>, /faider/macro/<NAME> <1|0>
void RemoteControl::oscMessageReceived (const juce::OSCMessage& m)
{
    juce::StringArray parts;
    parts.addTokens (m.getAddressPattern().toString(), "/", "");
    parts.removeEmptyStrings();
    if (parts.size() < 2 || ! (parts[0].equalsIgnoreCase ("faider") || parts[0].equalsIgnoreCase ("patchbay")))   // (/patchbay/...: the earlier name, still taken)
        return;

    auto number = [&m] (int i) -> float
    {
        if (m.size() <= i) return 0.0f;
        if (m[i].isFloat32()) return m[i].getFloat32();   // (NaN or inf: the message is dropped below)
        if (m[i].isInt32())   return (float) m[i].getInt32();
        return 0.0f;
    };
    auto text = [&m] (int i) { return m.size() > i && m[i].isString() ? m[i].getString() : juce::String(); };

    const float value = number (0);
    if (! std::isfinite (value))   // not a fader at 0 dB, not a toggle: nothing
        return;
    parts.remove (0);
    if (parts[0].equalsIgnoreCase ("scene") && parts.size() == 1)   // /faider/scene "NAME"
        parts.add (text (0));
    // An OSC message always carries its value (a macro without one is pressed).
    perform (parts, m.size() > 0 && ! m[0].isString(), value);
}

bool RemoteControl::perform (const juce::StringArray& parts, bool hasValue, float v)
{
    if (! std::isfinite (v))   // "?value=nan": refused (not +12 dB, not a NaN pan, not a toggle)
    {
        if (hasValue)
            return false;
        v = 0.0f;
    }
    const auto kind = parts[0].toLowerCase();
    if (kind == "scene" && parts.size() > 1)
    {
        const int i = findNamed (engine.getScenes().list, parts[1]);
        if (i >= 0)
            engine.getScenes().recall (i);
        return i >= 0;
    }
    if (kind == "pad" && parts.size() > 1 && appTarget)   // pad N (1-64), as its key on a controller works it
    {
        const int pad = parts[1].getIntValue() - 1;
        if (! juce::isPositiveAndBelow (pad, Pads::count))
            return false;
        appTarget ("pad:" + juce::String (pad), 1.0f, ! hasValue || v > 0.5f, ! hasValue || v <= 0.5f);
        return true;
    }
    if (kind == "macro" && parts.size() > 1)
    {
        const int i = findNamed (engine.getMacros().list, parts[1]);
        if (i < 0)
            return false;
        if (! hasValue)               engine.getMacros().run (i);       // a tap (the phone's key, OSC without a value): a click
        else if (v > 0.5f)            engine.getMacros().press (i);
        else                          engine.getMacros().release (i);
        return true;
    }
    if (parts.size() < 3)
        return false;

    const auto f = find (engine, kind, parts[1]);
    const auto what = parts[2].toLowerCase();
    std::atomic<bool>* mute = f.strip != nullptr ? &f.strip->mute : f.bus != nullptr ? &f.bus->mute : nullptr;
    if (what == "fader" && hasValue && (f.strip != nullptr || f.bus != nullptr))
    {
        // Below -60 dB is OFF; -60 itself is the fader's lowest step (a deck's dial stepping up
        // from OFF lands there, so it comes back up).
        const float gain = v < -60.0f ? 0.0f : juce::Decibels::decibelsToGain (juce::jmin (12.0f, v));
        (f.strip != nullptr ? f.strip->gain : f.bus->gain) = gain;
    }
    else if (what == "pan" && hasValue && f.strip != nullptr)
        f.strip->pan = juce::jlimit (-1.0f, 1.0f, v);
    else if (what == "mute" && mute != nullptr)
        mute->store (hasValue ? v > 0.5f : ! mute->load());
    else if (what == "solo" && f.strip != nullptr)
        f.strip->solo = hasValue ? v > 0.5f : ! f.strip->solo.load();
    else
        return false;
    return true;
}

juce::String RemoteControl::saveState() const
{
    juce::XmlElement xml ("Remote");
    xml.setAttribute ("osc", oscPort);
    xml.setAttribute ("oscPreferred", oscPreferredPort);
    for (auto& id : enabledInputs)
        xml.createNewChildElement ("Input")->setAttribute ("id", id);
    for (auto& [key, target] : mapping)
    {
        auto* m = xml.createNewChildElement ("Map");
        m->setAttribute ("control", key);
        m->setAttribute ("target", target);
    }
    return xml.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
}

void RemoteControl::restoreState (const juce::String& text)
{
    auto xml = juce::XmlDocument::parse (text);
    if (xml == nullptr || ! xml->hasTagName ("Remote"))
        return;
    enabledInputs.clear();
    mapping.clear();
    for (auto* in : xml->getChildWithTagNameIterator ("Input"))
        enabledInputs.add (in->getStringAttribute ("id"));
    for (auto* m : xml->getChildWithTagNameIterator ("Map"))
        mapping[m->getStringAttribute ("control")] = m->getStringAttribute ("target");
    openInputs();
    oscPreferredPort = xml->getIntAttribute ("oscPreferred", 9000);
    setOscPort (xml->getIntAttribute ("osc"));
}

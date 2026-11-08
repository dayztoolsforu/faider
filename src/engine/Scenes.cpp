#include <algorithm>
#include <map>
#include "Scenes.h"
#include "AudioEngine.h"

namespace
{
    // Every part of the mix a scene covers, by name: inputs, masters (buses) and outputs.
    struct Part
    {
        juce::String key;
        AudioEngine::Strip* strip = nullptr;   // an input or a bus
        int pair = -1;                         // a bus: its index in the buses
        int output = -1;                       // an output: its index in the engine's masters
    };

    std::vector<Part> partsOf (const AudioEngine& engine)
    {
        std::vector<Part> parts;
        for (auto& s : engine.getStrips())
            parts.push_back ({ "input:" + s->name, s.get() });
        for (int i = 0; i < (int) engine.getBuses().size(); ++i)
            parts.push_back ({ "bus:" + engine.getBuses()[(size_t) i]->name, engine.getBuses()[(size_t) i].get(), i });
        for (int i = 0; i < (int) engine.getMasters().size(); ++i)
            parts.push_back ({ "output:" + engine.getMasters()[(size_t) i]->name, nullptr, -1, i });
        return parts;
    }

    Part partFor (const AudioEngine& engine, const juce::String& key)
    {
        for (auto& p : partsOf (engine))
            if (p.key == key)
                return p;
        return {};
    }

    std::atomic<float>* gainOf (const AudioEngine& engine, const Part& p)
    {
        if (p.strip != nullptr) return &p.strip->gain;
        if (p.output >= 0)      return &engine.getMasters()[(size_t) p.output]->gain;
        return nullptr;
    }

    std::vector<std::unique_ptr<juce::AudioPluginInstance>>& insertsOf (const AudioEngine&, const Part& p)
    {
        static std::vector<std::unique_ptr<juce::AudioPluginInstance>> none;   // outputs have no plugins
        return p.strip != nullptr ? p.strip->inserts : none;
    }

    // Faders glide in dB, so the move sounds even all the way down.
    float toDb (float gain)  { return gain <= 0.001f ? -60.0f : juce::Decibels::gainToDecibels (gain); }
    float toGain (float db)  { return db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (db); }
}

Scenes::Scenes (AudioEngine& e) : engine (e) {}
Scenes::~Scenes() = default;

int Scenes::find (const juce::String& name) const
{
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].name == name)
            return i;
    return -1;
}

Scenes::Scene Scenes::capture (const juce::String& name, bool withPlugins) const
{
    Scene scene;
    scene.name = name;
    scene.withPlugins = withPlugins;
    const auto& buses = engine.getBuses();
    const auto& masters = engine.getMasters();

    for (auto& p : partsOf (engine))
    {
        Setting s;
        s.target = p.key;
        if (p.strip != nullptr)
        {
            s.gain = p.strip->gain.load();
            s.pan = p.strip->pan.load();
            s.mute = p.strip->mute.load();
            s.solo = p.strip->solo.load();
            s.on = p.strip->enabled.load();
            s.insertsOn = p.strip->insertsOn.load();
            for (int i = 0; i < (int) buses.size(); ++i)
                if (i != p.pair)
                    s.sends.push_back ({ "bus:" + buses[(size_t) i]->name, p.strip->busSends[(size_t) i].load() });
            for (int i = 0; i < (int) masters.size(); ++i)
                s.sends.push_back ({ "output:" + masters[(size_t) i]->name, p.strip->sends[(size_t) i].load() });
        }
        else
        {
            auto& bus = *masters[(size_t) p.output];
            s.gain = bus.gain.load();
            s.mute = bus.mute.load();
            s.on = bus.enabled.load();
        }
        if (withPlugins)
            for (auto& plugin : insertsOf (engine, p))
            {
                juce::MemoryBlock state;
                plugin->getStateInformation (state);
                s.plugins.push_back ({ plugin->getName(), std::move (state) });
            }
        scene.settings.push_back (std::move (s));
    }
    return scene;
}

void Scenes::recall (int index, Part part)
{
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;
    apply (list[(size_t) index], part);
    setActive (index);
    if (onRecalled)
        onRecalled (index);
}

void Scenes::apply (const Scene& scene, Part part)
{
    bool switched = false;
    const bool levels = part != Part::mutes, mutes = part != Part::levels, rest = part == Part::all;
    if (levels)
        glides.clear();   // (a mutes-only recall leaves a level glide running)
    const auto parts = partsOf (engine);

    auto settingFor = [&scene] (const juce::String& key) -> const Setting*
    {
        for (auto& s : scene.settings)
            if (s.target == key)
                return &s;
        return nullptr;
    };

    for (auto& p : parts)
    {
        const auto* s = settingFor (p.key);
        if (s == nullptr)
            continue;

        if (auto* gain = gainOf (engine, p); levels && gain != nullptr && ! juce::approximatelyEqual (gain->load(), s->gain))
            glides.push_back ({ p.key, false, toDb (gain->load()), toDb (s->gain), s->gain });

        if (p.strip != nullptr)
        {
            if (levels && ! juce::approximatelyEqual (p.strip->pan.load(), s->pan))
                glides.push_back ({ p.key, true, p.strip->pan.load(), s->pan, s->pan });
            if (mutes)
            {
                p.strip->mute = s->mute;
                p.strip->solo = s->solo;
            }
            if (rest)
                p.strip->insertsOn = s->insertsOn;
            if (rest && p.strip->enabled.load() != s->on)
            {
                engine.setStripEnabled (*p.strip, s->on);
                switched = true;
            }
        }
        else
        {
            auto& bus = *engine.getMasters()[(size_t) p.output];
            if (mutes)
                bus.mute = s->mute;
            if (rest && bus.enabled.load() != s->on)
            {
                engine.setMasterEnabled (p.output, s->on);
                switched = true;
            }
        }
    }

    // Sends: every one switched off first, then the ones that go on, so a bus-to-bus send isn't
    // refused as a loop because of one the scene is about to take away.
    for (const bool pass : { false, true })
        for (auto& p : parts)
        {
            const auto* s = settingFor (p.key);
            if (! rest || s == nullptr || p.strip == nullptr)   // (a partial recall leaves the routing)
                continue;
            for (auto& [to, on] : s->sends)
            {
                if (on != pass)
                    continue;
                const auto d = partFor (engine, to);
                if (d.output >= 0)
                    p.strip->sends[(size_t) d.output] = on;
                else if (d.pair >= 0 && p.pair >= 0)
                    engine.setBusSend (p.pair, d.pair, on);   // refused (and logged) if it would loop
                else if (d.pair >= 0)
                    p.strip->busSends[(size_t) d.pair] = on;
            }
        }

    // Plugins' settings (a scene that keeps them): each insert in its place, if it's still the
    // same plugin there, and only where they differ (setting a plugin's state can glitch it).
    for (auto& p : parts)
        if (const auto* s = settingFor (p.key); rest && s != nullptr && ! s->plugins.empty())
        {
            auto& chain = insertsOf (engine, p);
            for (size_t i = 0; i < chain.size() && i < s->plugins.size(); ++i)
            {
                auto& plugin = *chain[i];
                const auto& [name, state] = s->plugins[i];
                juce::MemoryBlock now;
                plugin.getStateInformation (now);
                if (plugin.getName() == name && now != state && state.getSize() > 0)
                    plugin.setStateInformation (state.getData(), (int) state.getSize());
            }
        }

    glideStart = juce::Time::getMillisecondCounterHiRes();
    if (! glides.empty())
        startTimerHz (60);

    if (switched && onStructureChanged)
        onStructureChanged();
}

juce::String Scenes::devicesChanged (const juce::StringArray& activeNow)
{
    // The first look only learns what's there: a headset already on at start doesn't switch scenes.
    const auto before = activeDevices;
    activeDevices = activeNow;
    if (! std::exchange (devicesSeen, true))
        return {};

    juce::String happened;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        const auto& device = list[(size_t) i].device;
        if (device.isEmpty())
            continue;

        if (activeNow.contains (device) && ! before.contains (device) && active != i)
        {
            sceneBeforeDevice = juce::isPositiveAndBelow (active, (int) list.size()) ? list[(size_t) active].name : juce::String();
            recall (i);
            happened = "scene: " + list[(size_t) i].name + " (" + device + " connected)";
        }
        else if (! activeNow.contains (device) && before.contains (device) && active == i)
        {
            if (const int previous = find (sceneBeforeDevice); previous >= 0)
            {
                recall (previous);
                happened = "scene: " + list[(size_t) previous].name + " (" + device + " gone)";
            }
            sceneBeforeDevice.clear();
        }
    }
    return happened;
}

void Scenes::remove (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;
    list.erase (list.begin() + index);
    setActive (active == index ? -1 : active > index ? active - 1 : active);
}

void Scenes::setActive (int index)
{
    active = index;
    if (onActiveChanged)
        onActiveChanged();
}

void Scenes::timerCallback()
{
    const float t = (float) juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - glideStart) / glideMs);
    const float eased = t * t * (3.0f - 2.0f * t);

    for (auto& g : glides)
    {
        const auto p = partFor (engine, g.target);
        const float v = g.from + (g.to - g.from) * eased;
        if (g.isPan && p.strip != nullptr)
            p.strip->pan = t >= 1.0f ? g.end : v;
        else if (auto* gain = gainOf (engine, p); ! g.isPan && gain != nullptr)
            gain->store (t >= 1.0f ? g.end : toGain (v));
    }

    if (t >= 1.0f)
    {
        glides.clear();
        stopTimer();
    }
}

bool Scenes::hasTriggers() const
{
    return back.has_value() || std::any_of (list.begin(), list.end(), [] (const Scene& s) { return ! s.triggers.empty(); });
}

juce::StringArray Scenes::checkTriggers (const juce::StringArray& apps, const juce::StringArray& devices, juce::Time now)
{
    std::map<juce::String, bool> quiet;   // each output: under -60 dBFS (or off)
    for (auto& bus : engine.getMasters())
        quiet[bus->name] = ! bus->enabled || juce::jmax (bus->peakL.load(), bus->peakR.load()) < juce::Decibels::decibelsToGain (-60.0f);

    std::vector<TriggerWatch::Macro> watched;
    for (auto& s : list)
        watched.push_back ({ s.name, &s.triggers });

    juce::StringArray happened;
    if (back && ! apps.contains (back->app, true))   // the app that recalled a scene has stopped: the one before comes back
    {
        if (const int i = find (back->scene); i >= 0)
        {
            recall (i);
            happened.add (back->app.toUpperCase() + " STOPPED -> back to " + back->scene);
        }
        back.reset();
    }
    for (auto& f : triggerWatch.check (watched, apps, devices, quiet, now))
    {
        auto& scene = list[(size_t) f.macro];
        if (f.macro == active)
            continue;   // already there
        if (scene.goBack)
            for (auto& t : scene.triggers)
                if (t.when == MacroTrigger::When::appStarts && apps.contains (t.what.trim(), true) && f.what.startsWithIgnoreCase (t.what.trim()))
                    back = Back { t.what.trim(), juce::isPositiveAndBelow (active, (int) list.size()) ? list[(size_t) active].name : juce::String() };
        if (back && back->scene.isEmpty())
            back.reset();
        happened.add (f.what + " -> scene " + scene.name);
        recall (f.macro);
    }
    return happened;
}

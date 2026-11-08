#include "Macros.h"
#include "AudioEngine.h"
#include "../devices/WindowsDefault.h"

namespace
{
    // What a step's target names: "input:MIC FOCUS", "bus:SUMMING", "output:MISC", "pad:3".
    struct Target
    {
        AudioEngine::Strip* strip = nullptr;   // an input, or a bus (pair)
        int output = -1;                       // AudioEngine's masters
        int pair = -1;
        int pad = -1;
    };

    Target resolve (const AudioEngine& engine, const juce::String& target)
    {
        Target t;
        const auto kind = target.upToFirstOccurrenceOf (":", false, false);
        const auto name = target.fromFirstOccurrenceOf (":", false, false);
        if (kind == "input")
        {
            for (auto& s : engine.getStrips())
                if (s->name == name)
                    t.strip = s.get();
        }
        else if (kind == "bus")
        {
            for (int i = 0; i < (int) engine.getBuses().size(); ++i)
                if (engine.getBuses()[(size_t) i]->name == name)
                {
                    t.strip = engine.getBuses()[(size_t) i].get();
                    t.pair = i;
                }
        }
        else if (kind == "output")
        {
            for (int i = 0; i < (int) engine.getMasters().size(); ++i)
                if (engine.getMasters()[(size_t) i]->name == name)
                    t.output = i;
        }
        else if (kind == "pad")
        {
            t.pad = juce::isPositiveAndBelow (name.getIntValue(), Pads::count) ? name.getIntValue() : -1;
        }
        return t;
    }

    float dbToGain (float db) { return db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (db); }
}

// One macro being run: where it's got to, fades in progress, and how to undo what it's done.
struct Macros::Run
{
    int macro = -1;
    bool held = false;
    size_t next = 0;
    double waitUntil = 0.0;

    struct Fade { juce::String target; float from, to; double start, end; };
    std::vector<Fade> fades;
    std::vector<std::function<void()>> undo;   // run backwards on release
};

Macros::Macros (AudioEngine& e) : engine (e) {}
Macros::~Macros() = default;

int Macros::find (const juce::String& name) const
{
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].name == name)
            return i;
    return -1;
}

void Macros::cancelAll()
{
    running.clear();
    stopTimer();
}

bool Macros::isRunning (int index) const
{
    return std::any_of (running.begin(), running.end(), [index] (auto& r) { return r->macro == index; });
}

void Macros::start (int index, bool held)
{
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;
    if (held && std::any_of (running.begin(), running.end(), [index] (auto& r) { return r->macro == index && r->held; }))
        return;   // already held down
    if (onStarted)
        onStarted (index);

    auto run = std::make_unique<Run>();
    run->macro = index;
    run->held = held;
    advance (*run);
    running.push_back (std::move (run));
    timerCallback();   // drops it at once if it's already done
    if (! running.empty() && ! isTimerRunning())
        startTimerHz (60);
}

void Macros::release (int index)
{
    for (auto it = running.begin(); it != running.end();)
    {
        auto& run = **it;
        if (run.macro != index || ! run.held)
        {
            ++it;
            continue;
        }

        run.held = false;
        if (juce::isPositiveAndBelow (index, (int) list.size()) && list[(size_t) index].undoOnRelease)
        {
            // Put back everything it changed, newest first; stop where it was.
            for (auto u = run.undo.rbegin(); u != run.undo.rend(); ++u)
                (*u)();
            it = running.erase (it);
            continue;
        }
        ++it;
    }
}

void Macros::advance (Run& run)
{
    const auto& steps = list[(size_t) run.macro].steps;
    while (run.next < steps.size())
    {
        const auto& step = steps[run.next++];
        if (step.action == Action::wait)
        {
            run.waitUntil = juce::Time::getMillisecondCounterHiRes() + step.ms;
            return;
        }
        apply (run, step);
    }
}

void Macros::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (auto it = running.begin(); it != running.end();)
    {
        auto& run = **it;

        // Level fades.
        for (auto f = run.fades.begin(); f != run.fades.end();)
        {
            const float t = (float) juce::jlimit (0.0, 1.0, (now - f->start) / juce::jmax (1.0, f->end - f->start));
            if (auto* gain = gainOf (f->target))
                gain->store (f->from + (f->to - f->from) * t);
            f = t >= 1.0f ? run.fades.erase (f) : f + 1;
        }

        if (run.waitUntil > 0.0 && now >= run.waitUntil && juce::isPositiveAndBelow (run.macro, (int) list.size()))
        {
            run.waitUntil = 0.0;
            advance (run);
        }

        // Done: every step run, nothing fading or waiting, and not held for an undo.
        const bool steps = juce::isPositiveAndBelow (run.macro, (int) list.size()) && run.next < list[(size_t) run.macro].steps.size();
        const bool keepForUndo = run.held && juce::isPositiveAndBelow (run.macro, (int) list.size()) && list[(size_t) run.macro].undoOnRelease;
        if (! steps && run.fades.empty() && run.waitUntil == 0.0 && ! keepForUndo)
            it = running.erase (it);
        else
            ++it;
    }

    if (running.empty())
        stopTimer();
}

//==============================================================================
std::atomic<float>* Macros::gainOf (const juce::String& target) const
{
    const auto t = resolve (engine, target);
    if (t.strip != nullptr) return &t.strip->gain;
    if (t.output >= 0)      return &engine.getMasters()[(size_t) t.output]->gain;
    return nullptr;
}

std::atomic<bool>* Macros::muteOf (const juce::String& target) const
{
    const auto t = resolve (engine, target);
    if (t.strip != nullptr) return &t.strip->mute;
    if (t.output >= 0)      return &engine.getMasters()[(size_t) t.output]->mute;
    return nullptr;
}

// A send from an input or bus to a bus or output.
bool Macros::sendIsOn (const juce::String& from, const juce::String& to) const
{
    const auto f = resolve (engine, from), d = resolve (engine, to);
    if (f.strip == nullptr) return false;
    if (d.output >= 0) return f.strip->sends[(size_t) d.output].load();
    if (d.pair >= 0)   return f.strip->busSends[(size_t) d.pair].load();
    return false;
}

void Macros::setSend (const juce::String& from, const juce::String& to, bool on)
{
    const auto f = resolve (engine, from), d = resolve (engine, to);
    if (f.strip == nullptr)
        return;
    if (d.output >= 0)
        f.strip->sends[(size_t) d.output] = on;
    else if (d.pair >= 0 && f.pair >= 0)
        engine.setBusSend (f.pair, d.pair, on);   // refused (and logged) if it would loop
    else if (d.pair >= 0)
        f.strip->busSends[(size_t) d.pair] = on;
}

bool Macros::isOn (const juce::String& target) const
{
    const auto t = resolve (engine, target);
    if (t.strip != nullptr) return t.strip->enabled.load();
    if (t.output >= 0)      return engine.getMasters()[(size_t) t.output]->enabled.load();
    return false;
}

void Macros::setOn (const juce::String& target, bool on)
{
    const auto t = resolve (engine, target);
    if (t.strip != nullptr && t.strip->enabled.load() != on)
        engine.setStripEnabled (*t.strip, on);
    else if (t.output >= 0 && engine.getMasters()[(size_t) t.output]->enabled.load() != on)
        engine.setMasterEnabled (t.output, on);
    else
        return;
    if (onStructureChanged)
        onStructureChanged();
}

// Does one step, and remembers how to undo it (by name, so it still works if things moved).
void Macros::apply (Run& run, const Step& step)
{
    switch (step.action)
    {
        case Action::muteOn:
        case Action::muteOff:
        case Action::muteToggle:
            if (auto* mute = muteOf (step.target))
            {
                const bool was = mute->load();
                mute->store (step.action == Action::muteToggle ? ! was : step.action == Action::muteOn);
                run.undo.push_back ([this, target = step.target, was] { if (auto* m = muteOf (target)) m->store (was); });
            }
            break;

        case Action::level:
            if (auto* gain = gainOf (step.target))
            {
                const float was = gain->load(), to = dbToGain (step.value);
                const double now = juce::Time::getMillisecondCounterHiRes();
                if (step.ms > 0)
                    run.fades.push_back ({ step.target, was, to, now, now + step.ms });
                else
                    gain->store (to);
                run.undo.push_back ([this, target = step.target, was] { if (auto* g = gainOf (target)) g->store (was); });
            }
            break;

        case Action::sendOn:
        case Action::sendOff:
        case Action::sendToggle:
        {
            const bool was = sendIsOn (step.target, step.target2);
            setSend (step.target, step.target2, step.action == Action::sendToggle ? ! was : step.action == Action::sendOn);
            run.undo.push_back ([this, from = step.target, to = step.target2, was] { setSend (from, to, was); });
            break;
        }

        case Action::powerOn:
        case Action::powerOff:
        {
            const bool was = isOn (step.target);
            setOn (step.target, step.action == Action::powerOn);
            run.undo.push_back ([this, target = step.target, was] { setOn (target, was); });
            break;
        }

        case Action::playPad:
            if (const int pad = resolve (engine, step.target).pad; pad >= 0)
            {
                engine.getPads().press (pad);
                run.undo.push_back ([this, pad] { engine.getPads().stop (pad); });
            }
            break;

        case Action::stopPad:
            if (const int pad = resolve (engine, step.target).pad; pad >= 0)
                engine.getPads().stop (pad);
            break;

        case Action::stopAllPads:
            engine.getPads().stopAll();
            break;

        case Action::tapePlayPause:
            engine.getTape().togglePlay();
            run.undo.push_back ([this] { engine.getTape().togglePlay(); });
            break;

        case Action::tapeNext:
            engine.getTape().next();
            break;

        case Action::recordStart:
            if (! engine.getRecorder().isRecording())
            {
                // The recorder's chosen source, as the TAPE tab shows it (the first output if none).
                juce::String error;
                engine.startChosenRecording (error);
                run.undo.push_back ([this] { engine.stopRecording(); });
            }
            break;

        case Action::recordStop:
            engine.stopRecording();
            break;

        case Action::scene:
            if (const int s = engine.getScenes().find (step.target.fromFirstOccurrenceOf (":", false, false)); s >= 0)
            {
                auto& scenes = engine.getScenes();
                const int wasActive = scenes.active;
                auto was = std::make_shared<Scenes::Scene> (scenes.capture ({}));
                scenes.recall (s);
                run.undo.push_back ([this, was, wasActive]
                {
                    auto& sc = engine.getScenes();
                    sc.apply (*was);
                    sc.setActive (juce::isPositiveAndBelow (wasActive, (int) sc.list.size()) ? wasActive : -1);
                });
            }
            break;

        case Action::fxOn:
        case Action::fxOff:
        case Action::fxToggle:
        {
            const auto t = resolve (engine, step.target);
            auto* param = t.strip != nullptr && t.strip->channel != nullptr
                        ? t.strip->channel->params.getParameter (VoiceChain::fxSwitch (step.target2.fromFirstOccurrenceOf (":", false, false)))
                        : nullptr;
            if (param == nullptr)
                break;
            const bool was = param->getValue() > 0.5f;
            const bool now = step.action == Action::fxToggle ? ! was : step.action == Action::fxOn;
            param->setValueNotifyingHost (now ? 1.0f : 0.0f);
            run.undo.push_back ([param, was, keep = t.strip->channel] { param->setValueNotifyingHost (was ? 1.0f : 0.0f); });
            break;
        }

        case Action::defaultOutput:
        case Action::defaultInput:
        {
            // Windows' default device; undone (a held macro let go) to the one before.
            const bool recording = step.action == Action::defaultInput;
            const auto was = WindowsDefault::current (recording);
            juce::String error;
            if (WindowsDefault::set (step.target.fromFirstOccurrenceOf (":", false, false), recording, error))
                run.undo.push_back ([was, recording] { juce::String ignored; WindowsDefault::set (was, recording, ignored); });
            break;
        }

        case Action::pluginSet:
        {
            // Found again when undone, by name: the plugin may have gone or moved since.
            auto paramOf = [this, target = step.target, which = step.target2]() -> juce::AudioProcessorParameter*
            {
                const auto t = resolve (engine, target);
                auto* chain = t.strip != nullptr ? &t.strip->inserts : nullptr;   // (outputs have no plugins)
                juce::StringArray w;
                w.addTokens (which, ":", "");   // plug:PLUGIN:N
                for (size_t i = 0; chain != nullptr && i < chain->size(); ++i)
                    if ((*chain)[i]->getName() == w[1])
                        return (*chain)[i]->getParameters()[w[2].getIntValue()];
                return nullptr;
            };
            if (auto* param = paramOf())
            {
                const float was = param->getValue();
                param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, step.value * 0.01f));
                run.undo.push_back ([paramOf, was] { if (auto* p = paramOf()) p->setValueNotifyingHost (was); });
            }
            break;
        }

        case Action::wait:
            break;
    }
}

//==============================================================================
juce::StringArray Macros::checkTriggers (const juce::StringArray& apps, const juce::StringArray& devices, juce::Time now)
{
    std::map<juce::String, bool> quiet;   // each output: under -60 dBFS (or off)
    for (auto& bus : engine.getMasters())
        quiet[bus->name] = ! bus->enabled || juce::jmax (bus->peakL.load(), bus->peakR.load()) < juce::Decibels::decibelsToGain (-60.0f);

    std::vector<TriggerWatch::Macro> watched;
    for (auto& m : list)
        watched.push_back ({ m.name, &m.triggers });

    juce::StringArray fired;
    for (auto& f : triggerWatch.check (watched, apps, devices, quiet, now))
    {
        fired.add (f.what + " -> " + list[(size_t) f.macro].name);
        run (f.macro);
    }
    return fired;
}

juce::String Macros::actionName (Action a)
{
    switch (a)
    {
        case Action::muteOn:        return "MUTE ON";
        case Action::muteOff:       return "MUTE OFF";
        case Action::muteToggle:    return "MUTE TOGGLE";
        case Action::level:         return "SET FADER";
        case Action::sendOn:        return "ASSIGN ON";
        case Action::sendOff:       return "ASSIGN OFF";
        case Action::sendToggle:    return "ASSIGN TOGGLE";
        case Action::powerOn:       return "CHANNEL ON";
        case Action::powerOff:      return "CHANNEL OFF";
        case Action::playPad:       return "TRIGGER PAD";
        case Action::stopPad:       return "STOP PAD";
        case Action::stopAllPads:   return "ALL NOTES OFF";
        case Action::tapePlayPause: return "TRANSPORT PLAY/PAUSE";
        case Action::tapeNext:      return "TRANSPORT NEXT";
        case Action::recordStart:   return "RECORD START";
        case Action::recordStop:    return "RECORD STOP";
        case Action::wait:          return "WAIT";
        case Action::scene:         return "SCENE RECALL";
        case Action::fxOn:          return "FX ON";
        case Action::fxOff:         return "FX OFF";
        case Action::fxToggle:      return "FX TOGGLE";
        case Action::defaultOutput: return "WINDOWS DEFAULT OUTPUT";
        case Action::defaultInput:  return "WINDOWS DEFAULT INPUT";
        case Action::pluginSet:     return "PLUGIN SETTING";
    }
    return {};
}

bool Macros::needsTarget (Action a)
{
    return ! (a == Action::stopAllPads || a == Action::tapePlayPause || a == Action::tapeNext
              || a == Action::recordStart || a == Action::recordStop || a == Action::wait);
}

bool Macros::needsTarget2 (Action a)
{
    return a == Action::sendOn || a == Action::sendOff || a == Action::sendToggle
        || a == Action::fxOn || a == Action::fxOff || a == Action::fxToggle || a == Action::pluginSet;
}

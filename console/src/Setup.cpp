#include "Setup.h"
#include "../../src/engine/AudioEngine.h"
#include "../../src/engine/AppCapture.h"
#include "../../src/devices/EndpointWatcher.h"
#include "../../src/ui/Theme.h"
#include "../../src/ui/LargeFormat.h"
#include "../../src/ui/Console.h"
#include "../../src/ui/DeskSettings.h"
#include "../../src/ui/WindowsDefaults.h"

namespace
{
    juce::String uniqueName (const juce::StringArray& taken, const juce::String& base)
    {
        auto name = base;
        for (int i = 2; taken.contains (name); ++i)
            name = base + " " + juce::String (i);
        return name;
    }

    // The line under a device's name: its adapter, by the name given it on DEVICES if any; a device
    // that has a name of its own but an adapter without one, just what it is.
    juce::String adapterText (const EndpointInfo& e, const juce::String& kind)
    {
        if (const auto alias = deviceAlias (e.adapter); alias.isNotEmpty())
            return alias;
        return deviceAlias (e.name).isNotEmpty() ? kind : e.adapter;
    }

    bool isCable (const EndpointInfo& e, const juce::Array<EndpointInfo>& all)
    {
        return WindowsDefaults::isVirtual (e, all);
    }

    //==============================================================================
    // SETUP: a question a page, the answers as cards (one or several), then the mix built.
    class Setup : public juce::Component
    {
    public:
        Setup (AudioEngine& e, std::function<void()> done) : engine (e), finished (std::move (done))
        {
            for (auto* b : { &back, &next })
            {
                addAndMakeVisible (b);
                b->setWantsKeyboardFocus (false);
            }
            back.onClick = [this] { if (step > 0) show (step - 1); };
            // On the cable's page, while Windows' default speaker or mic is the cable (as installing it leaves them).
            addChildComponent (defaults);
            defaults.setWantsKeyboardFocus (false);
            defaults.setTooltip (TRANS ("Windows plays or records on the cable: pick your real speakers and mic as its defaults, for calls too"));
            defaults.onClick = [this]
            {
                WindowsDefaults::showMenu (engine.getEndpoints(), juce::PopupMenu::Options().withTargetComponent (defaults),
                                           [this] (const juce::String& e) { engine.logEvent (e); });
            };
            next.onClick = [this]
            {
                if (step == apps)
                    build();
                if (step == ready)
                    return finished ? finished() : void();
                show (step + 1);
            };
        }

        void visibilityChanged() override
        {
            if (isVisible())
                show (0);   // (the questions start again with what's there now)
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (DeskSettings::desk());
            static const char* names[] = { NEEDS_TRANS ("WHERE YOU LISTEN"), NEEDS_TRANS ("MICROPHONES"), NEEDS_TRANS ("APPS HEAR YOU"), NEEDS_TRANS ("PROGRAMS"), NEEDS_TRANS ("DONE") };   // (translated where drawn)
            for (int i = 0; i <= ready; ++i)
            {
                const auto r = stepCard (i).toFloat();
                DeskSettings::card (g, r, i == step, false);
                g.setColour (i < step ? Theme::get().ledGreen : i == step ? juce::Colour (0xfff2c14e) : juce::Colour (0xff55585d));
                g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ r.getX() + 18.0f, r.getCentreY() }));
                g.setColour (i == step ? juce::Colours::white : DeskSettings::text());
                g.setFont (LargeFormat::print (12.0f));
                g.drawFittedText (juce::translate (names[i]), r.withTrimmedLeft (32.0f).withTrimmedRight (6.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.7f);
            }
            const auto pane = paneArea();
            DeskSettings::card (g, pane.toFloat());
            g.setColour (juce::Colours::white);
            g.setFont (LargeFormat::print (17.0f));
            g.drawFittedText (question(), pane.reduced (20).removeFromTop (30), juce::Justification::centredLeft, 1, 0.7f);
            if (step == ready)
                return paintSummary (g, pane.reduced (20).withTrimmedTop (44));
            for (int i = 0; i < (int) choices.size(); ++i)
            {
                const auto r = choiceArea (i).toFloat();
                const auto& c = choices[(size_t) i];
                const bool hover = r.contains (over) && ! c.fixed;
                g.setColour (c.on ? juce::Colour (0xff22303d) : hover ? juce::Colour (0xff2c2f33) : juce::Colour (0xff1d1f22));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (c.on ? Console::Colours::accent() : hover ? Console::Colours::accent().withAlpha (0.6f) : juce::Colour (0xff3a3d42));
                g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, c.on ? 1.6f : 1.0f);
                g.setColour (c.on ? juce::Colours::white : DeskSettings::text());
                g.setFont (LargeFormat::print (12.5f));
                g.drawFittedText (c.title, r.reduced (12.0f, 0.0f).withTrimmedTop (8.0f).withHeight (20.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
                g.setColour (DeskSettings::text().withAlpha (0.85f));
                g.setFont (juce::FontOptions (12.0f));
                g.drawFittedText (c.detail, r.reduced (12.0f, 0.0f).withTrimmedTop (30.0f).withHeight (18.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
            }
            if (choices.empty())
            {
                g.setColour (DeskSettings::text());
                g.setFont (LargeFormat::print (12.5f));
                g.drawFittedText (step == apps ? TRANS ("NO APPS PLAYING") : TRANS ("NONE FOUND"), pane.reduced (20).withTrimmedTop (50).removeFromTop (24), juce::Justification::centredLeft, 1, 0.7f);
            }
        }

        void resized() override
        {
            auto keys = paneArea().reduced (20).removeFromBottom (34);
            next.setBounds (keys.removeFromRight (180));
            keys.removeFromRight (8);
            back.setBounds (keys.removeFromRight (120));
            defaults.setBounds (keys.removeFromLeft (180));
        }

        void mouseMove (const juce::MouseEvent& e) override { over = e.position; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { over = { -1.0e6f, -1.0e6f }; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            for (int i = 0; i <= ready; ++i)
                if (stepCard (i).contains (e.getPosition()) && i < step)
                    return show (i);
            for (int i = 0; i < (int) choices.size(); ++i)
                if (choiceArea (i).contains (e.getPosition()) && ! choices[(size_t) i].fixed)
                {
                    auto& c = choices[(size_t) i];
                    if (multiple())
                        c.on = ! c.on;
                    else
                        for (auto& other : choices)
                            other.on = &other == &c;
                    remember();
                    repaint();
                    return;
                }
        }

    private:
        enum Step { listen, mics, cable, apps, ready };
        struct Choice { juce::String title, detail, key; bool on = false, fixed = false; AudioEngine::OutputChoice out; AudioEngine::Source source; };

        AudioEngine& engine;
        juce::SharedResourcePointer<WindowDiscovery::Scanner> windows;   // (the one the input menus use)
        std::function<void()> finished;
        juce::TextButton back { TRANS ("BACK") }, next { TRANS ("NEXT") }, defaults { TRANS ("SET MY DEFAULTS") };
        int step = 0;
        std::vector<Choice> choices;
        std::map<int, juce::StringArray> picked;   // each step's answers, by key
        std::map<int, bool> asked;                 // (a step's defaults are only chosen the first time)
        juce::StringArray built;                   // what the last build made, for the log
        juce::StringArray builtShown;              // (the same, translated, for DONE)
        juce::Point<float> over { -1.0e6f, -1.0e6f };

        bool multiple() const { return step != cable; }
        juce::String question() const
        {
            switch (step)
            {
                case listen: return TRANS ("Where do you listen?");
                case mics:   return TRANS ("Which microphones do you speak into?");
                case cable:  return TRANS ("How do Discord, games and OBS hear your mix?");
                case apps:   return TRANS ("Which programs get a channel of their own?");
                default:     return TRANS ("Your mix is ready");
            }
        }

        juce::Rectangle<int> stepCard (int i) const { return { 14, 14 + i * 40, 210, 34 }; }
        juce::Rectangle<int> paneArea() const { return getLocalBounds().withTrimmedLeft (238).reduced (14); }
        juce::Rectangle<int> choiceArea (int i) const
        {
            const auto area = paneArea().reduced (20).withTrimmedTop (50).withTrimmedBottom (50);
            const int columns = juce::jmax (1, area.getWidth() / 290), w = (area.getWidth() - (columns - 1) * 10) / columns;
            return { area.getX() + (i % columns) * (w + 10), area.getY() + (i / columns) * 66, w, 58 };
        }
        int choiceRoom() const   // how many cards the page holds
        {
            const auto area = paneArea().reduced (20).withTrimmedTop (50).withTrimmedBottom (50);
            return juce::jmax (1, area.getWidth() / 290) * juce::jmax (1, (area.getHeight() + 8) / 66);
        }

        void remember()
        {
            auto& list = picked[step];
            list.clear();
            for (auto& c : choices)
                if (c.on)
                    list.add (c.key);
        }

        void show (int s)
        {
            step = s;
            choices.clear();
            const auto& endpoints = engine.getEndpoints();
            if (step == listen)
            {
                juce::String def;
                for (auto& e : endpoints)
                    if (e.api == "WASAPI" && e.isOutput && e.isDefault)
                        def = e.name;
                for (auto& out : engine.outputChoices())
                {
                    const auto* ep = std::find_if (endpoints.begin(), endpoints.end(), [&out] (const EndpointInfo& e) { return e.name == out.candidate; });
                    if (ep != endpoints.end() && isCable (*ep, endpoints))
                        continue;
                    Choice c { out.label.upToFirstOccurrenceOf (" (", false, false).toUpperCase(), ep != endpoints.end() ? adapterText (*ep, TRANS ("Windows playback device")) : TRANS ("Interface outputs"),
                               out.candidate + "|" + juce::String (out.firstChannel) };
                    c.out = out;
                    c.on = ! asked[step] ? (out.candidate == def || (def.isEmpty() && choices.empty())) : picked[step].contains (c.key);
                    choices.push_back (c);
                }
                if (! asked[step] && ! choices.empty() && std::none_of (choices.begin(), choices.end(), [] (const Choice& c) { return c.on; }))
                    choices.front().on = true;   // (Windows' default is the interface itself: its first pair)
            }
            else if (step == mics)
            {
                const auto ins = engine.getInterfaceInputNames();
                for (int ch = 0; ch < ins.size(); ++ch)
                {
                    Choice c { "IN " + juce::String (ch + 1), (deviceAlias (engine.getInterfaceAdapter()).isNotEmpty() ? deviceAlias (engine.getInterfaceAdapter()) : engine.getInterfaceAdapter()) + ": " + ins[ch], "in|" + juce::String (ch) };
                    c.source = { AudioEngine::Source::interfaceInputs, ch, {}, false };
                    c.on = asked[step] && picked[step].contains (c.key);
                    choices.push_back (c);
                }
                for (auto& e : endpoints)
                {
                    if (e.api != "WASAPI" || e.isOutput || ! e.isActive() || engine.isDeviceDisabled (e.name) || e.adapter.containsIgnoreCase ("VB-Audio")
                        || e.name.containsIgnoreCase ("Voicemeeter") || e.adapter.containsIgnoreCase ("Virtual Cable"))
                        continue;
                    if (engine.getClock() == AudioEngine::Clock::asio && e.adapter == engine.getInterfaceAdapter())
                        continue;   // (the interface's inputs are listed as IN 1, IN 2 above)
                    Choice c { endpointDisplayName (e).toUpperCase(), adapterText (e, TRANS ("Windows recording device")), e.name };
                    c.source = { AudioEngine::Source::device, 0, e.name, false };
                    c.on = ! asked[step] ? e.isDefault : picked[step].contains (c.key);
                    choices.push_back (c);
                }
                if (! asked[step] && ! choices.empty() && std::none_of (choices.begin(), choices.end(), [] (const Choice& c) { return c.on; }))
                    choices.front().on = true;   // (Windows' default mic is the interface's: its first input)
                for (auto& c : choices)   // already a channel: shown ticked
                    if (std::any_of (engine.getStrips().begin(), engine.getStrips().end(), [this, &c] (auto& st) { return sameSource (st->source, c.source); }))
                        c.on = c.fixed = true;
            }
            else if (step == cable)
            {
                for (auto& e : endpoints)
                    if (e.api == "WASAPI" && e.isOutput && e.isActive() && virtualMicPartner (e, endpoints).isNotEmpty())
                    {
                        Choice c { endpointDisplayName (e).toUpperCase(), TRANS ("Apps pick \"%1\" as their mic").replace ("%1", virtualMicPartner (e, endpoints)), e.name };
                        c.out = { e.name, e.name, 0 };
                        choices.push_back (c);
                    }
                choices.push_back ({ TRANS ("NOT NOW"), TRANS ("Add it later from SETUP"), "none" });
                // First time: the plain stereo cable (VB-CABLE's 16-channel way in is the same cable,
                // and only one of the two plays at a time).
                const auto& was = picked[step];
                auto first = std::find_if (choices.begin(), choices.end(), [] (const Choice& c) { return c.key != "none" && ! c.key.containsIgnoreCase ("16ch"); });
                if (first == choices.end())
                    first = choices.begin();
                for (auto& c : choices)
                    c.on = asked[step] ? was.contains (c.key) : &c == &*first;
            }
            else if (step == apps)
            {
                auto programs = engine.appChoices (windows->snapshot());
                for (auto& st : engine.getStrips())   // (a channel whose program isn't running now: shown as on the desk)
                    if (st->source.kind == AudioEngine::Source::app
                        && std::none_of (programs.begin(), programs.end(), [&st] (auto& p) { return p.exe.equalsIgnoreCase (st->source.deviceName); }))
                        programs.push_back ({ st->source.deviceName, {}, false, true });
                // Those playing or on the desk by name, then those running that aren't playing yet.
                std::stable_sort (programs.begin(), programs.end(), [] (auto& a, auto& b)
                {
                    const bool first = a.playing || a.onDesk;
                    return first != (b.playing || b.onDesk) ? first : a.exe.compareNatural (b.exe) < 0;
                });
                for (auto& p : programs)
                {
                    if (! p.playing && ! p.onDesk && (int) choices.size() >= choiceRoom())
                        break;   // (the page doesn't scroll: the rest are in ADD APP)
                    Choice c { p.label().toUpperCase(), p.onDesk ? TRANS ("On the desk") : p.playing ? TRANS ("Playing now") : TRANS ("Not playing yet: captured when it plays"), p.exe };
                    c.on = p.onDesk || (asked[step] && picked[step].contains (p.exe));
                    c.fixed = p.onDesk;
                    choices.push_back (c);
                }
            }
            if (step != ready)
            {
                asked[step] = true;
                remember();
            }
            back.setVisible (step > 0 && step < ready);
            defaults.setVisible (step == cable && WindowsDefaults::onCable (endpoints).isNotEmpty());
            next.setButtonText (step == apps ? TRANS ("BUILD THE MIX") : step == ready ? TRANS ("OPEN THE MIXER") : TRANS ("NEXT"));
            resized();
            repaint();
            Console::riseIn (*this);
        }

        // The same thing captured: a channel on "Windows default" counts as the device that is the
        // default now, so SETUP doesn't add that microphone a second time.
        bool sameSource (const AudioEngine::Source& a, const AudioEngine::Source& b) const
        {
            if (a.kind != b.kind) return false;
            if (a.kind == AudioEngine::Source::interfaceInputs) return a.firstChannel == b.firstChannel;
            if (a.kind != AudioEngine::Source::device) return a.deviceName == b.deviceName;
            auto device = [this] (const juce::String& name)
            {
                if (name != AudioEngine::windowsDefault)
                    return name;
                for (auto& e : engine.getEndpoints())
                    if (e.api == "WASAPI" && ! e.isOutput && e.isActive() && e.isDefault)
                        return e.name;
                return name;
            };
            const auto x = device (a.deviceName), y = device (b.deviceName);
            return x.equalsIgnoreCase (y) || (x.isNotEmpty() && y.isNotEmpty() && (x.containsIgnoreCase (y) || y.containsIgnoreCase (x)));   // (a channel's device may be named by part of its name)
        }

        // What the answers make: an output per listening device (the first HEADPHONES), MIC FOR APPS
        // on the cable, a channel per microphone (to MIC FOR APPS; not in your own headphones), a
        // channel per program (to where you listen) with a rule so it comes back by itself. Only
        // what isn't there already.
        void build()
        {
            built.clear();
            builtShown.clear();
            juce::StringArray taken;
            for (auto& m : engine.getMasters()) taken.add (m->name);

            auto outputOn = [this, &taken] (const AudioEngine::OutputChoice& out, const juce::String& base) -> int
            {
                if (const int there = engine.masterPlayingOn (out.candidate, out.firstChannel); there >= 0)
                    return there;
                const auto name = uniqueName (taken, base);
                if (engine.addMaster (name, { out.candidate }, out.firstChannel) == nullptr)
                    return -1;   // (eight outputs already)
                taken.add (name);
                built.add ("OUTPUT " + name);
                builtShown.add (TRANS ("OUTPUT %1").replace ("%1", name));
                return (int) engine.getMasters().size() - 1;
            };

            std::vector<int> listening;
            for (auto& key : picked[listen])
            {
                const auto candidate = key.upToLastOccurrenceOf ("|", false, false);
                const int first = key.fromLastOccurrenceOf ("|", false, false).getIntValue();
                juce::String label;
                for (auto& out : engine.outputChoices())
                    if (out.candidate == candidate && out.firstChannel == first)
                        label = out.label.upToFirstOccurrenceOf (" (", false, false).toUpperCase();
                const int m = outputOn ({ label, candidate, first }, listening.empty() ? juce::String ("HEADPHONES") : label);
                if (m >= 0)
                    listening.push_back (m);
            }

            int forApps = -1;
            if (const auto key = picked[cable].isEmpty() ? juce::String ("none") : picked[cable][0]; key != "none")
                forApps = outputOn ({ key, key, 0 }, "MIC FOR APPS");

            juce::StringArray names;
            for (auto& st : engine.getStrips()) names.add (st->name);
            for (auto& key : picked[mics])
            {
                AudioEngine::Source source;
                if (key.startsWith ("in|")) source = { AudioEngine::Source::interfaceInputs, key.fromFirstOccurrenceOf ("|", false, false).getIntValue(), {}, false };
                else                        source = { AudioEngine::Source::device, 0, key, false };
                if (std::any_of (engine.getStrips().begin(), engine.getStrips().end(), [this, &source] (auto& st) { return sameSource (st->source, source); }))
                    continue;
                const auto name = uniqueName (names, "MICROPHONE");
                names.add (name);
                auto& strip = engine.addStrip (name, source);
                if (forApps >= 0)
                    strip.sends[(size_t) forApps] = true;   // (not in your headphones: no echo of your own voice)
                built.add ("CHANNEL " + name);
                builtShown.add (TRANS ("CHANNEL %1").replace ("%1", name));
            }

            for (auto& exe : picked[apps])
            {
                if (std::none_of (engine.getStrips().begin(), engine.getStrips().end(), [&exe] (auto& st)
                                  { return st->source.kind == AudioEngine::Source::app && st->source.deviceName.equalsIgnoreCase (exe); }))
                {
                    auto& strip = engine.addAppStrip (exe);
                    for (int m : listening)
                        strip.sends[(size_t) m] = true;
                    built.add ("CHANNEL " + strip.name);
                    builtShown.add (TRANS ("CHANNEL %1").replace ("%1", strip.name));
                    // (a rule only for a channel made here: one already on the desk is routed as the user left it)
                    if (! listening.empty() && std::none_of (engine.appRules.begin(), engine.appRules.end(), [&exe] (auto& r) { return r.exe.equalsIgnoreCase (exe); }))
                        engine.appRules.push_back ({ exe, "output:" + engine.getMasters()[(size_t) listening.front()]->name });
                }
            }
            engine.refreshDevices();
            engine.refreshDucking();
            engine.logEvent ("setup: " + (built.isEmpty() ? juce::String ("nothing new to add") : built.joinIntoString (", ")));
        }

        void paintSummary (juce::Graphics& g, juce::Rectangle<int> r)
        {
            g.setFont (LargeFormat::print (12.5f));
            if (builtShown.isEmpty())
            {
                g.setColour (DeskSettings::text());
                g.drawFittedText (TRANS ("EVERYTHING WAS ALREADY THERE"), r.removeFromTop (24), juce::Justification::centredLeft, 1, 0.7f);
                return;
            }
            for (auto& line : builtShown)
            {
                g.setColour (Theme::get().ledGreen);
                g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) r.getX() + 6.0f, (float) r.getY() + 12.0f }));
                g.setColour (juce::Colours::white);
                g.drawText (line, r.removeFromTop (24).withTrimmedLeft (20), juce::Justification::centredLeft, false);
            }
        }
    };
}

namespace MixSetup
{
    std::unique_ptr<juce::Component> make (AudioEngine& engine, std::function<void()> finished)
    {
        return std::make_unique<Setup> (engine, std::move (finished));
    }
}

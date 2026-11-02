#include "MixerComponent.h"
#include "SelectedChannel.h"

#include "ProStrip.h"
#include "PluginRack.h"
#include "Console.h"
#include "../engine/Metering.h"
#include "../engine/VoiceChain.h"
#include "../Session.h"
#include "EqCurve.h"
#include "StageGraphs.h"
#include "MidiMap.h"
#include "PluginPicker.h"
#include "../engine/PluginPresets.h"
#include "../engine/PluginScan.h"
#include <numeric>
#include <set>

// The services the console uses from what was Patchbay's mixer: the input and plugin menus, plugin
// windows, the colour menu, the selection, A / B, putting a channel back from a saved session, and
// the channel pages the SteamVR dashboard shows. It draws nothing itself.
namespace
{
    // Windows devices of one direction, grouped by the physical device (adapter) they belong to.
    // A-Z ignoring case, with numbers in number order ("Voicemeeter 2" before "Voicemeeter 10").
    bool alphabetical (const juce::String& a, const juce::String& b) { return a.compareNatural (b) < 0; }

    // Groups and the devices within them are both in alphabetical order.
    std::vector<std::pair<juce::String, juce::Array<EndpointInfo>>> byAdapter (const AudioEngine& engine, bool outputs, bool activeOnly)
    {
        std::map<juce::String, juce::Array<EndpointInfo>> groups;
        for (auto& e : engine.getEndpoints())
            if (e.api == "WASAPI" && e.isOutput == outputs && (e.isActive() || ! activeOnly) && ! engine.isDeviceDisabled (e.name))
                groups[e.adapter.isEmpty() ? juce::String ("Other") : e.adapter].add (e);

        std::vector<std::pair<juce::String, juce::Array<EndpointInfo>>> sorted (groups.begin(), groups.end());
        std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return alphabetical (a.first, b.first); });

        for (auto& [adapter, devices] : sorted)
            std::sort (devices.begin(), devices.end(), [] (auto& a, auto& b)
                       { return alphabetical (endpointDisplayName (a), endpointDisplayName (b)); });

        return sorted;
    }

    // True if another strip already uses this input (channel ranges count as overlapping, so a
    // mono In 1 blocks the stereo pair In 1-2). The test tone and loopbacks can be shared.
    bool sourceTaken (const AudioEngine& eng, const AudioEngine::Source& s, int exceptStrip)
    {
        using K = AudioEngine::Source;
        auto& strips = eng.getStrips();

        for (int i = 0; i < (int) strips.size(); ++i)
        {
            const auto& o = strips[(size_t) i]->source;

            if (i == exceptStrip || o.kind != s.kind)
                continue;

            if (s.kind == K::interfaceInputs
                && s.firstChannel <= o.firstChannel + (o.stereo ? 1 : 0)
                && o.firstChannel <= s.firstChannel + (s.stereo ? 1 : 0))
                return true;

            // A device's channels: taken where they overlap (one pair of a 16-channel cable
            // doesn't take the others).
            if (s.kind == K::device && s.deviceName.equalsIgnoreCase (o.deviceName)
                && s.firstChannel <= o.firstChannel + (o.stereo ? 1 : 0) && o.firstChannel <= s.firstChannel + (s.stereo ? 1 : 0))
                return true;

            if (s.kind == K::app && s.deviceName.equalsIgnoreCase (o.deviceName))
                return true;
        }

        return false;
    }

    // Every input source, grouped like the device tree: the master's own channels (mono) or
    // buses (stereo), the Windows default, each physical device's recording endpoints, apps,
    // then the test tone and players. Used by the strip's input selector
    // and the "+ Input" menu. item: (menu label, suggested strip name, source).
    void forEachSource (const AudioEngine& eng, const std::vector<WindowDiscovery::Window>& windows, bool masterStereo, bool otherStereo,
                        const std::function<void (const juce::String&)>& heading,
                        const std::function<void (const juce::String&, const juce::String&, AudioEngine::Source)>& item)
    {
        const auto inputs = eng.getInterfaceInputNames();
        if (! inputs.isEmpty())
            heading ((eng.getInterfaceAdapter().isNotEmpty() ? eng.getInterfaceAdapter() : juce::String ("Master")) + " (" + eng.getClockName() + ")");

        for (int ch = 0; ch < inputs.size(); ch += (masterStereo ? 2 : 1))
        {
            const auto channels = masterStereo && ch + 1 < inputs.size()
                ? juce::String (ch + 1) + "-" + juce::String (ch + 2)
                : juce::String (ch + 1);
            item ("In " + channels + "  " + inputs[ch], "In " + channels, { AudioEngine::Source::interfaceInputs, ch, {}, masterStereo });
        }

        heading ("Windows");
        item ("Windows default input", "Windows input", { AudioEngine::Source::device, 0, AudioEngine::windowsDefault, otherStereo });

        for (auto& [adapter, devices] : byAdapter (eng, false, true))
        {
            heading (adapter);
            for (auto& e : devices)
            {
                const auto label = endpointDisplayName (e) + (e.isDefault ? "  (default)" : "");
                if (e.channels <= 2)
                {
                    item (label, endpointDisplayName (e), { AudioEngine::Source::device, 0, e.name, otherStereo });
                    continue;
                }
                // More than two channels (a 16-channel cable): each pair (or channel) on its own.
                for (int ch = 0; ch + (otherStereo ? 1 : 0) < e.channels; ch += (otherStereo ? 2 : 1))
                {
                    const auto channels = juce::String (ch + 1) + (otherStereo ? "-" + juce::String (ch + 2) : juce::String());
                    item (label + "  ch " + channels, endpointDisplayName (e) + " " + channels, { AudioEngine::Source::device, ch, e.name, otherStereo });
                }
            }
        }

        // Apps playing sound right now, captured directly (no virtual device needed).
        heading ("Apps");
        auto apps = listAudioApps();
        std::sort (apps.begin(), apps.end(), [] (auto& a, auto& b) { return alphabetical (a.exe, b.exe); });
        if (apps.isEmpty())
            item ("(no apps playing audio)", {}, { AudioEngine::Source::none });

        for (auto& a : apps)
        {
            const auto label = juce::File (a.exe).getFileNameWithoutExtension();
            item (label, label, { AudioEngine::Source::app, 0, a.exe, otherStereo });
        }

        // Program windows and browser tabs (WindowDiscovery.h). Windows captures a program as a
        // whole, so a window or a tab captures its program; "Isolate" opens a tab on its own.
        constexpr auto app = AudioEngine::Source::app;
        auto shortTitle = [] (const juce::String& t) { return t.length() > 48 ? t.substring (0, 46).trimEnd() + "..." : t; };

        bool programsShown = false;
        for (auto& w : windows)
            if (! w.isBrowser())
            {
                if (! std::exchange (programsShown, true))
                    heading ("Program windows");
                item (w.product + ": " + shortTitle (w.title), w.product, { app, 0, w.exeName(), otherStereo });
            }

        for (auto& w : windows)
            if (w.isBrowser())
            {
                heading (w.product + " tabs (whole browser)");
                for (auto& t : w.tabs)
                    item ((t.playing ? "[playing]  " : "") + shortTitle (t.title), w.product, { app, 0, w.exeName(), otherStereo });

                heading ("Isolate a " + w.product + " tab");
                for (auto& t : w.tabs)
                    item ("Isolate: " + shortTitle (t.title), shortTitle (t.title),
                          { app, 0, "isolate?" + juce::String ((juce::int64) w.handle) + "|" + t.title, otherStereo });
            }

        // Isolated tabs in use (a closed one opens again when picked).
        juce::StringArray isolated;
        for (auto& s : eng.getStrips())
            if (s->source.kind == app && WindowDiscovery::isIsolated (s->source.deviceName))
                isolated.addIfNotAlreadyThere (s->source.deviceName);
        if (! isolated.isEmpty())
            heading ("Isolated tabs");
        for (auto& n : isolated)
            item (WindowDiscovery::appLabel (n) + (WindowDiscovery::findIsolatedProcess (n) == 0 ? "  (closed: reopens)" : ""),
                  WindowDiscovery::appLabel (n), { app, 0, n, otherStereo });

        // Outputs aren't offered: a loopback is a MASTER strip (an output's LOOPBACK key), which
        // goes to devices only, never back into the mix.
        heading ("Other");
        item ("Test tone", "Test tone", { AudioEngine::Source::tone, 0, {}, otherStereo });
        item ("Pads", "PADS", { AudioEngine::Source::pads, 0, {}, true });
        item ("Tape deck", "TAPE", { AudioEngine::Source::tape, 0, {}, true });
    }
}
//==============================================================================
class MixerComponent::PluginWindow : public juce::DocumentWindow
{
public:
    // `options`: the insert's own settings (null for a plugin that has none); `rate`: the
    // engine's, for the CPU reading.
    // `owner`: the strip or output it's on ("input:MIC"; empty: no MIDI learn).
    PluginWindow (juce::AudioProcessor& p, AudioEngine::InsertOptions* options, double rate, RemoteControl& remote, const juce::String& owner,
                  std::function<void()> onCloseFn)
        : DocumentWindow (p.getName(), juce::Colours::darkgrey, DocumentWindow::closeButton),
          onClose (std::move (onCloseFn))
    {
        setUsingNativeTitleBar (true);

        juce::AudioProcessorEditor* editor = p.hasEditor() ? p.createEditorIfNeeded() : nullptr;
        if (editor == nullptr)   // none, or it failed to make one: its parameters as sliders
            editor = new juce::GenericAudioProcessorEditor (p);
        auto* content = new WithPresets (p, editor, options, rate, remote, owner);
        setContentOwned (content, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { onClose(); }

private:
    // The plugin's own window under a bar of PRESET (save, recall, delete), A and B (two settings
    // to switch between while listening) and COPY (the one in use onto the other). The A/B
    // settings last while the window is open; what's in use is what the session keeps.
    // Then the insert's own settings: BYPASS, MIX, IN and OUT gain, and its CPU use.
    // MIDI (with an owner): press it, touch a control in the plugin, then move one on the MIDI
    // controller, and that controller plays the plugin's control from then on.
    class WithPresets : public juce::Component,
                        private juce::ComponentListener,
                        private juce::AudioProcessorListener,
                        private juce::Timer
    {
    public:
        WithPresets (juce::AudioProcessor& p, juce::AudioProcessorEditor* e, AudioEngine::InsertOptions* o, double sampleRate,
                     RemoteControl& r, const juce::String& ownerKey)   // takes the editor
            : plugin (p), editor (e), options (o), rate (sampleRate), remote (r), owner (ownerKey)
        {
            if (owner.isNotEmpty())
            {
                midiLearn.setClickingTogglesState (true);
                midiLearn.setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
                midiLearn.setTooltip ("MIDI learn: touch a plugin control, then a MIDI one");
                midiLearn.onClick = [this]
                {
                    if (! midiLearn.getToggleState())
                        stopLearning();
                    else
                    {
                        plugin.addListener (this);
                        midiLearn.setButtonText ("TOUCH ONE");
                    }
                };
                addAndMakeVisible (midiLearn);
                startTimerHz (4);
            }
            if (options != nullptr)
            {
                bypass.setClickingTogglesState (true);
                bypass.setColour (juce::TextButton::buttonOnColourId, Console::monoLight());   // amber: something's taken out
                bypass.setToggleState (options->bypassed.load(), juce::dontSendNotification);
                bypass.setTooltip ("Bypass this plugin");
                bypass.onClick = [this] { options->bypassed = bypass.getToggleState(); };
                addAndMakeVisible (bypass);

                auto box = [this] (juce::Slider& s, const juce::String& name, double lo, double hi, double value, double reset, bool percent,
                                   const juce::String& tip, std::function<void (double)> write)
                {
                    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                    s.setRange (lo, hi, percent ? 1.0 : 0.1);
                    s.setValue (value, juce::dontSendNotification);
                    s.setDoubleClickReturnValue (true, reset);
                    s.textFromValueFunction = [percent] (double v)
                    {
                        return percent ? juce::String ((int) v) + " %" : (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB";
                    };
                    Console::setValueBox (s, name);
                    s.setTooltip (tip);
                    s.onValueChange = [&s, write] { write (s.getValue()); };
                    addAndMakeVisible (s);
                };
                box (mix, "MIX", 0.0, 100.0, options->mix.load() * 100.0, 100.0, true,
                     "Wet/dry mix", [this] (double v) { options->mix = (float) (v * 0.01); });
                box (inGain, "IN", -24.0, 24.0, juce::Decibels::gainToDecibels (options->inGain.load()), 0.0, false,
                     "Level into the plugin", [this] (double v) { options->inGain = juce::Decibels::decibelsToGain ((float) v); });
                box (outGain, "OUT", -24.0, 24.0, juce::Decibels::gainToDecibels (options->outGain.load()), 0.0, false,
                     "Level out of the plugin", [this] (double v) { options->outGain = juce::Decibels::decibelsToGain ((float) v); });

                cpu.setTooltip ("CPU used by this plugin");
                cpu.setJustificationType (juce::Justification::centredRight);
                cpu.setFont (Console::legendFont (Console::Type::legend));
                addAndMakeVisible (cpu);
                startTimerHz (4);
            }

            preset.setTooltip ("Presets for " + p.getName());
            preset.onClick = [this] { showPresetMenu(); };
            for (auto* k : { &a, &b })
            {
                k->setClickingTogglesState (true);
                k->setRadioGroupId (1);
                k->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
            }
            a.setToggleState (true, juce::dontSendNotification);
            a.setTooltip ("Settings A");
            b.setTooltip ("Settings B");
            a.onClick = [this] { switchTo (0); };
            b.onClick = [this] { switchTo (1); };
            copy.onClick = [this]
            {
                plugin.getStateInformation (slots[(size_t) (1 - active)]);
                showCopy();
            };
            showCopy();
            for (auto* c : std::initializer_list<juce::Component*> { &preset, &a, &b, &copy })
                addAndMakeVisible (c);
            addAndMakeVisible (*editor);
            editor->addComponentListener (this);
            setSize (juce::jmax (editor->getWidth(), minWidth()), editor->getHeight() + barHeight);
        }

        ~WithPresets() override
        {
            stopLearning();
            editor->removeComponentListener (this);   // then the editor goes (it's owned here)
        }

        int minWidth() const { return (options != nullptr ? 720 : 270) + (owner.isNotEmpty() ? 96 : 0); }   // the bar's keys (and CPU)

        // MIDI learn: the first control touched in the plugin is the one the controller will play.
        void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<WithPresets> (this), index]
            {
                if (safe == nullptr || ! safe->midiLearn.getToggleState() || safe->learningTarget.isNotEmpty())
                    return;
                safe->plugin.removeListener (safe.getComponent());
                safe->learningTarget = "plug:" + safe->owner + ":" + safe->plugin.getName() + ":" + juce::String (index);
                safe->remote.learn (safe->learningTarget);
                safe->midiLearn.setButtonText ("MOVE MIDI");
                if (auto* p = safe->plugin.getParameters()[index])
                    safe->midiLearn.setTooltip ("Now move a MIDI control for " + p->getName (40));
            });
        }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}

        void stopLearning()
        {
            plugin.removeListener (this);
            if (learningTarget.isNotEmpty() && remote.learning() == learningTarget)
                remote.cancelLearn();
            learningTarget.clear();
            midiLearn.setToggleState (false, juce::dontSendNotification);
            midiLearn.setButtonText ("MIDI");
        }

        // The CPU reading: its share of real time (time to process a sample against a sample's
        // length); and MIDI learn ending (bound, or cancelled elsewhere).
        void timerCallback() override
        {
            if (learningTarget.isNotEmpty() && remote.learning() != learningTarget)
                stopLearning();
            if (options == nullptr)
                return;
            const float percent = options->secondsPerSample.load() * (float) rate * 100.0f;
            const auto text = options->bypassed.load() ? juce::String ("CPU -") : "CPU " + juce::String (percent, percent < 10.0f ? 1 : 0) + " %";
            if (cpu.getText() != text)
                cpu.setText (text, juce::dontSendNotification);
        }

        void paint (juce::Graphics& g) override { g.fillAll (Console::Colours::panelDark()); }

        void resized() override
        {
            auto r = getLocalBounds();
            // PRESET, A B (half a column each), COPY | BYPASS, MIX, IN, OUT | MIDI | CPU.
            Console::KeyRow bar (r.removeFromTop (barHeight).reduced (Console::Space::unit, 0));
            bar.left (preset).leftWidth (a, halfColumn).leftWidth (b, halfColumn).left (copy);
            if (options != nullptr)
            {
                bar.groupLeft().left (bypass);
                for (auto* s : { &mix, &inGain, &outGain })
                    bar.left (*s);
            }
            if (owner.isNotEmpty())
                bar.groupLeft().left (midiLearn);
            if (options != nullptr)
                cpu.setBounds (bar.rest().withTrimmedLeft (Console::Space::pad));
            if (! resizingFromEditor && editor->isResizable())
                editor->setBounds (r);
            else
                editor->setTopLeftPosition (r.getTopLeft());
        }

    private:
        static constexpr int barHeight = Console::Space::bar;
        static constexpr int halfColumn = (Console::Space::column - Console::Space::unit) / 2;   // A and B: one column together

        // The plugin resized its own window: the bar follows.
        void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
        {
            if (! wasResized)
                return;
            const juce::ScopedValueSetter<bool> guard (resizingFromEditor, true);
            setSize (juce::jmax (editor->getWidth(), minWidth()), editor->getHeight() + barHeight);
        }

        // A or B: what's in use is kept as the one left, the other comes in (B, the first time,
        // starts as a copy of A, so nothing changes until it's turned).
        void switchTo (int slot)
        {
            if (slot == active)
                return;
            plugin.getStateInformation (slots[(size_t) active]);
            if (slots[(size_t) slot].getSize() == 0)
                slots[(size_t) slot] = slots[(size_t) active];
            plugin.setStateInformation (slots[(size_t) slot].getData(), (int) slots[(size_t) slot].getSize());
            active = slot;
            showCopy();
        }

        void showCopy()
        {
            copy.setButtonText (active == 0 ? "A > B" : "B > A");
            copy.setTooltip ("Copy settings " + juce::String (active == 0 ? "A onto B" : "B onto A"));
        }

        void showPresetMenu()
        {
            juce::PopupMenu menu, remove;
            menu.addItem ("SAVE AS...", [this]
            {
                auto* w = new juce::AlertWindow ("SAVE PRESET", "Preset name:", juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("name", {});
                w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
                w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                w->enterModalState (true, juce::ModalCallbackFunction::create ([self = juce::Component::SafePointer<WithPresets> (this), w] (int result)
                {
                    if (self == nullptr)   // the plugin's window closed (the plugin removed) while this was open
                        return;
                    const auto name = w->getTextEditorContents ("name");
                    if (result == 1 && ! PluginPresets::save (self->plugin, name))
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "NOT SAVED", "Couldn't save \"" + name + "\".");
                }), true);
            });
            const auto names = PluginPresets::list (plugin);
            if (! names.isEmpty())
            {
                menu.addSeparator();
                for (auto& n : names)
                {
                    menu.addItem (n, [this, n] { PluginPresets::load (plugin, n); });
                    remove.addItem (n, [this, n] { PluginPresets::remove (plugin, n); });
                }
                menu.addSeparator();
                menu.addSubMenu ("DELETE", remove);
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (preset));
        }

        juce::AudioProcessor& plugin;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        juce::TextButton preset { "PRESET" }, a { "A" }, b { "B" }, copy;
        AudioEngine::InsertOptions* options;   // the engine keeps it while the plugin lives (longer than this window)
        const double rate;
        juce::TextButton bypass { "BYPASS" };
        juce::Slider mix, inGain, outGain;
        juce::Label cpu;
        RemoteControl& remote;
        const juce::String owner;
        juce::TextButton midiLearn { "MIDI" };
        juce::String learningTarget;   // MIDI learn: the plugin's control waiting for the controller
        std::array<juce::MemoryBlock, 2> slots;
        int active = 0;
        bool resizingFromEditor = false;
    };

    std::function<void()> onClose;
};
//==============================================================================
// The selected channel's page across the top of the Mixer, as a desk's screen. Two tabs: CHANNEL
// (its EQ, gate and compressor as graphs) and RACK: its processing as racks of units side by side
// (PluginRack: the built-ins' and the plugins', each running on into more when full): the
// built-in FILTERS, GATE, COMPRESSOR, EQ and BUS COMP, every plugin in its chain as a faceplate of
// its settings, and ADD PLUGIN. Drag units between slots; FLIP turns the racks round to patch
// them by hand (RE-PATCH IN ORDER: back to following the placement). Click a plugin to choose
// it for PRESETS and WINDOW (its own panel in a window). The rack follows plugins being added,
// removed or reordered elsewhere.
class MixerComponent::SelectedPage : public juce::Component,
                                     private juce::Timer
{
public:
    // onDesk: the Mixer's own page (made again on each SEL), not one made by createRackPage.
    SelectedPage (MixerComponent& m, AudioEngine::Strip& s, const juce::String& key, juce::Colour colour, bool onDesk = true)
        : mixer (m), strip (&s), channel (s.channel, s.name, colour, key), deskPage (onDesk), pageKey (key)
    {
        setOpaque (true);
        addAndMakeVisible (channel);
        for (int b = 0; b < AudioEngine::rackBuiltins; ++b)
            stageUnits.add (nullptr);   // (each made the first time it's in the rack: buildRack)

        window.setTooltip ("Open plugin in a window");
        window.onClick = [this]
        {
            if (auto* p = shownPlugin())
                mixer.openEditor (*p);
        };
        addChildComponent (window);

        // A plugin's presets (PluginPresets: shared by every copy of that plugin).
        presets.setTextWhenNothingSelected ("PRESETS");
        presets.setTooltip ("Load a preset");
        presets.onChange = [this]
        {
            if (auto* p = shownPlugin(); p != nullptr && presets.getSelectedId() > 0)
                PluginPresets::load (*p, presets.getText());
        };
        addChildComponent (presets);
        savePreset.setTooltip ("Save as a preset");
        savePreset.onClick = [this] { askPresetName(); };
        addChildComponent (savePreset);
        deletePreset.setTooltip ("Delete this preset");
        deletePreset.onClick = [this]
        {
            if (auto* p = shownPlugin(); p != nullptr && presets.getSelectedId() > 0)
                PluginPresets::remove (*p, presets.getText());
            listPresets();
        };
        addChildComponent (deletePreset);

        // FLIP: the racks' backs, to patch them by hand; RE-PATCH IN ORDER undoes the hand patching.
        flipKey.setClickingTogglesState (true);
        flipKey.setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
        flipKey.setTooltip ("Flip the racks to patch cables");
        flipKey.onClick = [this]
        {
            flipped = flipKey.getToggleState();
            if (rack != nullptr)
                rack->setFlipped (flipped);
            showRackKeys();
        };
        addChildComponent (flipKey);
        inOrderKey.setTooltip ("Patch the cables in order again, top to bottom (following where the units are)");
        inOrderKey.onClick = [this]
        {
            if (! stripExists())
                return;
            auto p = mixer.engine.getRack (*strip);
            p.manual = false;
            p.cables.clear();
            repatch (p);
            showRackKeys();
        };
        addChildComponent (inOrderKey);

        rackView.setScrollBarsShown (false, true);   // (sideways only, when there are more racks than fit)
        rackView.setSingleStepSizes (16, (int) PluginRack::rackU);
        rackView.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);   // dragging moves units and cables, not the view
        addChildComponent (rackView);
        addChildComponent (rackFrame);

        auto addTab = [this] (const juce::String& text, int index)
        {
            tabIndex.push_back (index);
            auto* tab = tabs.add (new juce::TextButton (text));
            tab->setClickingTogglesState (true);
            tab->setRadioGroupId (1);
            tab->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
            tab->onClick = [this, index] { show (index); };
            addAndMakeVisible (tab);
        };
        addTab ("CHANNEL", -1);
        addTab ("RACK", rackTab);       // the built-in units' racks and the plugins'

        // A / B: two settings of this channel to flip between while tuning (MixerComponent::compare).
        for (int slot = 0; slot < 2; ++slot)
        {
            auto& k = slot == 0 ? compareA : compareB;
            k.setClickingTogglesState (true);
            k.setRadioGroupId (2);
            k.setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
            k.setTooltip ("Compare: two settings of this channel (its built-in processing and its plugins'), flip between them");
            k.onClick = [this, slot] { mixer.compare (pageKey, slot); showCompare(); };
            addAndMakeVisible (k);
        }
        showCompare();

        shownList = plugins();
        resized();
        show (-1);
        startTimerHz (4);
    }

    ~SelectedPage() override { dropRack(); }

    // The page is one grey panel, edge to edge (nothing of the desk behind shows through it).
    void paint (juce::Graphics& g) override { g.fillAll (Console::Colours::panel()); }

    void openRack() { show (rackTab); }

    // A plugin about to go (removed, or its channel deleted): the rack lets go of it now, before
    // it's freed (its faceplate reads the plugin), and is made again once that's done.
    void release (juce::AudioProcessor* p)
    {
        if (p == nullptr || shown != rackTab || std::find (shownList.begin(), shownList.end(), p) == shownList.end())
            return;
        dropRack();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SelectedPage> (this)]
        {
            if (safe != nullptr && safe->shown == rackTab)
                safe->buildRack();
        });
    }

    void resized() override
    {
        auto r = getLocalBounds();
        Console::KeyRow bar (r.removeFromTop (Console::Space::key));
        bar.right (window).right (deletePreset).right (savePreset).right (presets, 2);   // (evenly, as one group)
        for (auto* tab : tabs)
            bar.left (*tab, Console::KeyRow::columnsFor (tab->getButtonText()));
        bar.group().leftWidth (compareA, Console::Space::key + 8).leftWidth (compareB, Console::Space::key + 8);   // A / B, small, beside the tabs
        bar.group().left (flipKey);   // (shown on RACK)
        bar.left (inOrderKey, Console::KeyRow::columnsFor (inOrderKey.getButtonText()));
        r.removeFromTop (Console::Space::unit);
        channel.setBounds (r);
        rackView.setBounds (r);
        rackFrame.setBounds (r);
        if (rack != nullptr)   // racks as tall as the page, from the left, more to the right as they fill (scrolling sideways only then)
        {
            auto fit = [this, &r] (int height)
            {
                rack->setRows (juce::jmax (2, (int) ((float) height / PluginRack::rackU)));
                return rack->widthNeeded();
            };
            int w = fit (r.getHeight());
            const int room = w > r.getWidth() ? r.getHeight() - rackView.getScrollBarThickness() : r.getHeight();
            if (room != r.getHeight())
                w = fit (room);
            rack->setSize (juce::jmax (r.getWidth(), w), room);   // (the cabinet fills the page)
        }
    }

private:
    // A frame round the racks' view: a dark rim with a light line inside it, so the racks sit in a
    // window with a clear edge where they're cut off. Never takes clicks.
    struct Frame : juce::Component
    {
        Frame() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (Console::Colours::rim());
            g.drawRect (r, 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawRect (r.reduced (2.0f), 1.0f);
        }
    };

    bool stripExists() const
    {
        for (auto* list : { &mixer.engine.getStrips(), &mixer.engine.getBuses() })
            for (auto& s : *list)
                if (s.get() == strip)
                    return true;
        return false;
    }

    std::vector<juce::AudioPluginInstance*> plugins() const
    {
        std::vector<juce::AudioPluginInstance*> list;
        if (stripExists())
            for (auto& p : strip->inserts)
                list.push_back (p.get());
        return list;
    }

    // The plugin PRESETS and WINDOW act on: the chosen one in the rack.
    juce::AudioProcessor* shownPlugin() const
    {
        return shown == rackTab && std::find (shownList.begin(), shownList.end(), chosen) != shownList.end() ? chosen : nullptr;
    }

    void showRackKeys()
    {
        const bool rackShown = shown == rackTab;
        for (auto* c : std::initializer_list<juce::Component*> { &window, &presets, &savePreset, &deletePreset })
            c->setVisible (rackShown && ! flipped && ! shownList.empty());
        flipKey.setVisible (rackShown);
        flipKey.setToggleState (flipped, juce::dontSendNotification);
        inOrderKey.setVisible (rackShown && flipped && stripExists() && mixer.engine.getRack (*strip).manual);   // (patched by hand)
    }

    // The channel's chain, as the mixer names it.
    std::optional<InsertTarget> insertTarget() const
    {
        for (auto [list, kind] : { std::pair (&mixer.engine.getStrips(), InsertTarget::strip), std::pair (&mixer.engine.getBuses(), InsertTarget::bus) })
            for (int i = 0; i < (int) list->size(); ++i)
                if ((*list)[(size_t) i].get() == strip)
                    return InsertTarget { kind, i };
        return std::nullopt;
    }

    // The racks as the engine has them. Keeps them scrolled where they were.
    void buildRack()
    {
        const auto scrolled = rackView.getViewPosition();
        dropRack();
        shownList = plugins();
        if (! stripExists())
            return;
        shownRevision = strip->rackRevision;   // (so the timer doesn't make it again after this page's own edit)
        if (std::find (shownList.begin(), shownList.end(), chosen) == shownList.end())
            chosen = shownList.empty() ? nullptr : shownList.front();

        auto& engine = mixer.engine;
        const auto patch = engine.getRack (*strip);
        std::vector<PluginRack::Mount> mounts;
        for (int b = 0; b < AudioEngine::rackBuiltins; ++b)   // (only the built-in tools moved into the rack: the rest are the strip's)
            if (((patch.inRack >> b) & 1u) != 0)
            {
                if (stageUnits[b] == nullptr)
                    stageUnits.set (b, new ProStrip::StageUnit (strip->channel, b));
                mounts.push_back ({ { b, nullptr }, nullptr, stageUnits[b], AudioEngine::rackUnitName ({ b, nullptr }), false });
            }
        for (auto* p : shownList)
            mounts.push_back ({ { -1, p }, p, nullptr, {}, mixer.pluginLocked (p) });
        // Whose swings and glides these are: the desk's (whichever page it has made, so selecting
        // the channel again carries on from where it was) or this page's own, and the channel.
        const auto view = deskPage ? juce::String ("desk") : "page:" + juce::String::toHexString ((juce::pointer_sized_int) this);
        const auto store = view + ":" + juce::String::toHexString ((juce::pointer_sized_int) strip);
        rack = std::make_unique<PluginRack> (patch, engine.rackCables (*strip), mounts, strip->name, store, [this] (juce::AudioProcessor* p)
        {
            const auto list = plugins();
            return std::find (list.begin(), list.end(), p) != list.end();
        });
        rack->setFlipped (flipped);
        rack->setSelected ({ -1, chosen });
        rack->onSelect = [this] (PluginRack::Unit u)
        {
            if (u.plugin == chosen)
                return;
            chosen = u.plugin;
            rack->setSelected (u);
            listPresets();
        };
        // Keys, drags and cables: done once the click is over (the rack is made again after).
        auto later = [this] (std::function<void (SelectedPage&)> f)
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SelectedPage> (this), f]
            {
                if (safe != nullptr)
                    f (*safe);
            });
        };
        rack->onAction = [later] (PluginRack::Unit u, PluginRack::Action action)
        {
            if (action == PluginRack::Action::toStrip)   // a built-in tool back to the channel strip
                later ([u] (SelectedPage& page) { page.moveBuiltin (u.builtin, false); });
            else if (u.plugin == nullptr && action == PluginRack::Action::remove)   // a built-in tool deleted
                later ([u] (SelectedPage& page) { page.deleteBuiltin (u.builtin); });
            else
                later ([u, action] (SelectedPage& page) { page.act (u.plugin, action); });
        };
        // BUILT-IN TOOL: the strip's built-in tools not in the rack yet, one moved in.
        rack->onAddBuiltin = [this, later] (juce::Component& near)
        {
            if (! stripExists())
                return;
            juce::PopupMenu menu;
            menu.addSectionHeader ("MOVE INTO THE RACK");
            const auto inRack = strip->rack.inRack;
            for (int b = 0; b < AudioEngine::rackBuiltins; ++b)
                menu.addItem (AudioEngine::rackUnitName ({ b, nullptr }), ((inRack >> b) & 1u) == 0, false,
                              [later, b] { later ([b] (SelectedPage& page) { page.moveBuiltin (b, true); }); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&near));
        };
        rack->onPatch = [later] (AudioEngine::RackPatch p)
        {
            later ([p] (SelectedPage& page) { page.repatch (p); });
        };
        rack->onAdd = [this] (juce::Component& near)
        {
            if (const auto target = insertTarget())
                mixer.showAddInsertMenu (*target, juce::PopupMenu::Options().withTargetComponent (&near));
        };
        rackView.setViewedComponent (rack.get(), false);
        resized();
        showRackKeys();
        listPresets();
        resized();
        rackView.setViewPosition (scrolled);
    }

    // A unit dragged, or a cable patched or pulled out.
    void repatch (const AudioEngine::RackPatch& p)
    {
        if (! stripExists())
            return;
        if (! mixer.engine.setRack (*strip, p))
            mixer.engine.logEvent ("rack: " + strip->name + ": that cable would loop the sound back on itself, so it isn't patched");
        shownList.clear();                    // (the plugins may have changed order: made again)
        buildRack();
        showRackKeys();                       // (RE-PATCH IN ORDER, once patched by hand)
    }

    // A built-in tool moved from the channel strip into the rack, or back.
    void moveBuiltin (int builtin, bool intoRack)
    {
        if (! stripExists())
            return;
        mixer.engine.moveBuiltin (*strip, builtin, intoRack);
        mixer.engine.logEvent ("rack: " + strip->name + ": " + AudioEngine::rackUnitName ({ builtin, nullptr })
                               + (intoRack ? " moved into the rack" : " back on the channel strip"));
        buildRack();
    }

    // A built-in tool deleted from the rack: out of it, and switched out (its IN keys off), so it
    // no longer does anything. Its settings stay; switch it in on the CHANNEL tab to have it back.
    void deleteBuiltin (int builtin)
    {
        if (! stripExists() || strip->channel == nullptr)
            return;
        static const std::vector<std::vector<const char*>> ins {
            { "hpfOn", "lpfOn" }, { "gateOn" }, { "compOn" }, { "eqOn" }, { "busOn" }, {}, { "nrOn" },
            { "deessOn" }, { "levelOn" }, { "robotOn", "echoOn", "radioOn", "pitchOn" }, { "limitOn" } };
        if (juce::isPositiveAndBelow (builtin, (int) ins.size()))
            for (auto* id : ins[(size_t) builtin])
                if (auto* p = strip->channel->params.getParameter (id))
                    p->setValueNotifyingHost (0.0f);
        mixer.engine.moveBuiltin (*strip, builtin, false);
        mixer.engine.logEvent ("rack: " + strip->name + ": " + AudioEngine::rackUnitName ({ builtin, nullptr }) + " deleted from the rack (switched out)");
        buildRack();
    }

    // Lock or remove a plugin.
    void act (juce::AudioPluginInstance* plugin, PluginRack::Action action)
    {
        const auto target = insertTarget();
        const auto list = plugins();
        const auto it = std::find (list.begin(), list.end(), plugin);
        if (! target || it == list.end())
            return;
        switch (action)
        {
            case PluginRack::Action::remove: mixer.removeInsert (*target, (int) (it - list.begin())); break;
            case PluginRack::Action::toStrip: break;   // (moveBuiltin)
            case PluginRack::Action::window:  mixer.openEditor (*plugin); return;   // its own interface, in a window
            case PluginRack::Action::lock:
                if (auto* o = mixer.engine.insertOptions (plugin))
                {
                    o->locked = ! o->locked;
                    if (stripExists())
                        ++strip->rackRevision;   // every other view of this rack (the headset's, the desk's) shows it too
                    mixer.engine.logEvent ("plugins: " + plugin->getName() + (o->locked ? " locked" : " unlocked"));
                }
                break;
        }
        if (shown == rackTab && rack != nullptr)   // (removeInsert has already let go of the rack: it comes back by itself)
            buildRack();
    }

    void dropRack()
    {
        rackView.setViewedComponent (nullptr, false);
        rack = nullptr;
    }

    void show (int index)   // -1: CHANNEL
    {
        if (index == shown)
            return;
        shown = index;
        if (shown == rackTab)
            buildRack();
        else
            dropRack();
        channel.setVisible (shown == -1);
        rackView.setVisible (shown == rackTab);
        rackFrame.setVisible (shown == rackTab);
        showRackKeys();
        listPresets();
        for (int i = 0; i < tabs.size(); ++i)
            tabs[i]->setToggleState (tabIndex[(size_t) i] == shown, juce::dontSendNotification);
        resized();
        Console::riseIn (shown == rackTab ? (juce::Component&) rackView : (juce::Component&) channel);   // the tab's page rises into place
    }

    void listPresets()
    {
        presets.clear (juce::dontSendNotification);
        if (auto* p = shownPlugin())
        {
            const auto names = PluginPresets::list (*p);
            for (int i = 0; i < names.size(); ++i)
                presets.addItem (names[i], i + 1);
        }
    }

    void askPresetName()
    {
        auto* p = shownPlugin();
        if (p == nullptr)
            return;
        auto* box = new juce::AlertWindow ("SAVE PRESET", "Preset name:", juce::MessageBoxIconType::NoIcon);
        box->addTextEditor ("name", "");
        box->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
        box->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        box->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<SelectedPage> (this), box, p] (int result)
        {
            if (result == 1 && safe != nullptr && safe->shownPlugin() == p)
            {
                PluginPresets::save (*p, box->getTextEditorContents ("name"));
                safe->listPresets();
            }
        }), true);
    }

    // Plugins added, removed or moved elsewhere, or the rack changed (undo): the rack (and RACK's count) follow.
    void timerCallback() override
    {
        const int revision = stripExists() ? strip->rackRevision : 0;
        if (plugins() == shownList && revision == shownRevision)
            return;
        shownRevision = revision;
        if (shown == rackTab)
            buildRack();
        else
        {
            shownList = plugins();
            resized();
        }
    }

    MixerComponent& mixer;
    AudioEngine::Strip* strip;   // checked against the engine's lists before use
    static constexpr int rackTab = -5;    // RACK's tab number (CHANNEL: -1)
    SelectedChannel channel;
    juce::TextButton compareA { "A" }, compareB { "B" };
    void showCompare() { (mixer.compareSlot (pageKey) == 0 ? compareA : compareB).setToggleState (true, juce::dontSendNotification); }
    const bool deskPage;
    const juce::String pageKey;   // its channel ("input:MIC")
    juce::OwnedArray<ProStrip::StageUnit> stageUnits;   // the built-in units' controls (RackBuiltin order)
    juce::OwnedArray<juce::TextButton> tabs;
    std::vector<int> tabIndex;   // what each tab shows
    juce::TextButton window { "WINDOW" }, savePreset { "SAVE" }, deletePreset { "DELETE" };
    juce::TextButton flipKey { "FLIP" }, inOrderKey { "RE-PATCH IN ORDER" };
    juce::ComboBox presets;
    juce::Viewport rackView;
    Frame rackFrame;
    std::unique_ptr<PluginRack> rack;   // (after stageUnits: it holds them)
    std::vector<juce::AudioPluginInstance*> shownList;
    juce::AudioPluginInstance* chosen = nullptr;   // the plugin PRESETS and WINDOW act on
    int shownRevision = -1;                        // the strip's rackRevision the rack was made from
    int shown = -2;
    bool flipped = false;
};

//==============================================================================
MixerComponent::MixerComponent (AudioEngine& e) : engine (e) {}

MixerComponent::~MixerComponent()
{
    editors.clear();   // (the plugins' windows closed before the engine lets the plugins go)
}

void MixerComponent::renamed (const juce::String& kind, const juce::String& oldName, const juce::String& newName)
{
    const auto oldKey = kind + ":" + oldName, newKey = kind + ":" + newName;
    if (selectedKey == oldKey)
        select (newKey);
}

//==============================================================================
void MixerComponent::addColourMenu (juce::PopupMenu& menu, juce::Colour current, std::function<void (juce::Colour)> onPick)
{
    static const std::pair<const char*, juce::uint32> palette[] = {
        { "Red", 0xffe0524f },    { "Orange", 0xffe08a3c }, { "Amber", 0xffe0b040 }, { "Lime", 0xff8fc74a },
        { "Green", 0xff3fb37a },  { "Teal", 0xff3fb3a8 },   { "Blue", 0xff3d8fd1 },  { "Indigo", 0xff5b6be0 },
        { "Purple", 0xff9a6ad1 }, { "Pink", 0xffd166a8 },   { "Grey", 0xff8a929b }
    };

    juce::PopupMenu colours;
    colours.addItem ("Default", true, current.isTransparent(), [onPick] { onPick ({}); });
    colours.addSeparator();

    for (auto& [name, argb] : palette)
    {
        const juce::Colour c (argb);

        // A vector swatch (a rounded square), sharp at any zoom.
        auto swatch = std::make_unique<juce::DrawablePath>();
        juce::Path square;
        square.addRoundedRectangle (0.0f, 0.0f, 14.0f, 14.0f, 2.5f);
        swatch->setPath (square);
        swatch->setFill (c);
        swatch->setStrokeFill (juce::Colours::black.withAlpha (0.35f));
        swatch->setStrokeThickness (1.0f);

        juce::PopupMenu::Item item (name);
        item.setImage (std::move (swatch));
        item.setTicked (c == current);
        item.setAction ([onPick, c] { onPick (c); });
        colours.addItem (std::move (item));
    }

    menu.addSubMenu ("Colour", colours);
}

namespace
{
    // Where "to the mic" sends an app's channel: a bus whose sound ends up only on outputs that
    // play into a cable apps use as their mic (preferring one other apps already go to, then one
    // no mic goes to), else such an output itself. index -1: there's none.
    struct MicRoute { bool toBus = false; int index = -1; juce::String name; };

    MicRoute findMicRoute (const AudioEngine& eng)
    {
        auto& buses = eng.getBuses();
        auto& masters = eng.getMasters();
        auto isMicOutput = [&] (int b)
        {
            const auto places = masters[(size_t) b]->places();
            return ! places.empty() && std::all_of (places.begin(), places.end(), [&] (auto& p)
            {
                const auto device = p.target.isNotEmpty() ? p.target : p.candidate;
                return std::any_of (eng.getEndpoints().begin(), eng.getEndpoints().end(), [&] (const EndpointInfo& e)
                                    { return e.api == "WASAPI" && e.isOutput && e.name == device && virtualMicPartner (e, eng.getEndpoints()).isNotEmpty(); });
            });
        };

        // The outputs a bus ends up on (through the masters it feeds).
        std::function<void (int, std::set<int>&, int)> reach = [&] (int p, std::set<int>& outs, int depth)
        {
            if (depth > AudioEngine::maxBuses)
                return;
            for (int b = 0; b < (int) masters.size(); ++b)
                if (buses[(size_t) p]->sends[(size_t) b])
                    outs.insert (b);
            for (int q = 0; q < (int) buses.size(); ++q)
                if (q != p && buses[(size_t) p]->busSends[(size_t) q])
                    reach (q, outs, depth + 1);
        };

        int best = -1, bestScore = -1;
        for (int p = 0; p < (int) buses.size(); ++p)
        {
            std::set<int> outs;
            reach (p, outs, 0);
            if (outs.empty() || ! std::all_of (outs.begin(), outs.end(), isMicOutput))
                continue;
            bool apps = false, mics = false;
            for (auto& s : eng.getStrips())
                if (s->busSends[(size_t) p])
                    (s->source.kind == AudioEngine::Source::app ? apps : mics) = true;
            const int score = (apps ? 2 : 0) + (mics ? 0 : 1);
            if (score > bestScore)
                best = p, bestScore = score;
        }
        if (best >= 0)
            return { true, best, buses[(size_t) best]->name };

        for (int b = 0; b < (int) masters.size(); ++b)
            if (isMicOutput (b))
                return { false, b, masters[(size_t) b]->name };
        return {};
    }
}

void MixerComponent::showAddInputMenu (juce::PopupMenu::Options where)
{
    juce::PopupMenu menu;
    juce::Array<AudioEngine::Source> sources;
    juce::StringArray names;

    // TO THE MIC: an app playing now, as a channel already sent to the mic only (the app still
    // plays on its own device, so it isn't sent to your ears as well).
    {
        const auto route = findMicRoute (engine);
        juce::PopupMenu toMic;
        auto apps = listAudioApps();
        std::sort (apps.begin(), apps.end(), [] (auto& a, auto& b) { return alphabetical (a.exe, b.exe); });
        for (auto& a : apps)
        {
            const AudioEngine::Source s { AudioEngine::Source::app, 0, a.exe, true };
            const auto label = juce::File (a.exe).getFileNameWithoutExtension();
            toMic.addItem (label, route.index >= 0 && ! sourceTaken (engine, s, -1), false, [this, s, label]
            {
                const auto r = findMicRoute (engine);
                if (r.index < 0)
                    return;
                auto name = label.toUpperCase();
                for (int i = 2; std::any_of (engine.getStrips().begin(), engine.getStrips().end(), [&name] (auto& st) { return st->name == name; }); ++i)
                    name = label.toUpperCase() + " " + juce::String (i);
                auto& strip = engine.addStrip (name, s);
                if (r.toBus) strip.busSends[(size_t) r.index] = true;
                else          strip.sends[(size_t) r.index] = true;
                engine.logEvent ("input " + name + ": sent to the mic (" + r.name + ")");
                checkSetupSoon (engine);
            });
        }
        if (apps.isEmpty())
            toMic.addItem ("(no apps playing audio)", false, false, [] {});
        menu.addSubMenu (route.index >= 0 ? "SEND AN APP TO THE MIC  (via " + route.name + ")"
                                          : juce::String ("SEND AN APP TO THE MIC  (no mic cable master yet)"),
                         toMic, route.index >= 0);
        menu.addSeparator();
    }

    // Hardware inputs start mono (one mic/instrument per channel); everything else stereo.
    forEachSource (engine, windows->snapshot(), false, true,
                   [&] (const juce::String& text) { menu.addSectionHeader (text); },
                   [&] (const juce::String& label, const juce::String& stripName, AudioEngine::Source s)
                   {
                       const bool taken = sourceTaken (engine, s, -1);
                       sources.add (s);
                       names.add (stripName);
                       menu.addItem (sources.size(), label + (taken ? "  (in use)" : ""), ! taken && s.kind != AudioEngine::Source::none);
                   });

    menu.showMenuAsync (where,
                        [this, sources, names] (int result)
                        {
                            if (result <= 0 || sources[result - 1].kind == AudioEngine::Source::none)
                                return;

                            auto source = sources[result - 1];
                            auto base = names[result - 1];
                            if (! resolveSource (source, base))
                                return;

                            // Number repeats: "VR Headset loop", "VR Headset loop 2", ...
                            auto name = base;
                            auto taken = [this] (const juce::String& n)
                            {
                                return std::any_of (engine.getStrips().begin(), engine.getStrips().end(), [&n] (auto& s) { return s->name == n; });
                            };

                            for (int i = 2; taken (name); ++i)
                                name = base + " " + juce::String (i);

                            engine.addStrip (name, source);
                            engine.refreshDevices();
                        });
}

// A picked "Isolate: <tab>" opens the tab in a browser instance of its own and becomes that
// instance; a closed isolated tab opens again. False (after saying why) if isolating failed.
bool MixerComponent::resolveSource (AudioEngine::Source& s, juce::String& stripName)
{
    if (s.kind != AudioEngine::Source::app)
        return true;

    // An app keeps playing on its own device too: check where its channel goes once it's set up.
    checkSetupSoon (engine);

    if (WindowDiscovery::isIsolated (s.deviceName))
    {
        WindowDiscovery::reopen (s.deviceName);   // does nothing while it's open
        return true;
    }

    if (! s.deviceName.startsWith ("isolate?"))
        return true;

    const auto handle = s.deviceName.fromFirstOccurrenceOf ("?", false, false).upToFirstOccurrenceOf ("|", false, false).getLargeIntValue();
    const auto title = s.deviceName.fromFirstOccurrenceOf ("|", false, false);
    juce::String error = "That browser window has closed.";

    for (auto& w : windows->snapshot())
        if ((juce::int64) w.handle == handle)
        {
            juce::StringArray inUse;
            for (auto& strip : engine.getStrips())
                inUse.add (strip->source.deviceName);

            s.deviceName = WindowDiscovery::isolateTab (w, title, inUse, error);
            if (s.deviceName.isNotEmpty())
            {
                stripName = title.length() > 24 ? title.substring (0, 23).trimEnd() + "..." : title;
                return true;
            }
            break;
        }

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't isolate the tab", error);
    return false;
}

void MixerComponent::showAddInsertMenu (InsertTarget target, juce::PopupMenu::Options where)
{
    auto plugins = engine.findInstalledPlugins();
    std::sort (plugins.begin(), plugins.end(), [] (const juce::String& a, const juce::String& b)
               { return alphabetical (juce::File (a).getFileNameWithoutExtension(), juce::File (b).getFileNameWithoutExtension()); });

    // The chain itself, found again when the choice comes (channels may have moved or gone while
    // the picker or the file dialog was open, so the index it was opened with can be someone else's).
    const auto* chain = chainOf (target);
    auto stillThere = [this, chain] { return findTarget (chain); };
    auto pick = [this, stillThere] (const juce::String& path)
    {
        PluginPicker::remember (path);
        if (const auto now = stillThere())
            insertPlugin (*now, path);
    };
    auto loadFile = [this, pick]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a VST3 plugin", juce::File(), "*.vst3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [pick] (const juce::FileChooser& fc)
                              {
                                  // A .vst3 is usually a bundle folder; climb to it from any file inside.
                                  auto bundle = fc.getResult();
                                  for (auto f = bundle; f.exists() && f != f.getParentDirectory(); f = f.getParentDirectory())
                                      if (f.hasFileExtension ("vst3"))
                                          bundle = f;
                                  if (bundle.exists())
                                      pick (bundle.getFullPathName());
                              });
    };

    // The picker (PluginPicker.h) in a call-out pointing at what opened it: on the desktop, or in
    // the page that opened it (the headset's, Console::Look::menuParent).
    auto* parent = Console::Look::menuParent != nullptr ? Console::Look::menuParent (where) : nullptr;
    const auto targetArea = where.getTargetScreenArea();
    auto area = parent != nullptr ? parent->getLocalArea (nullptr, targetArea) : targetArea;
    juce::CallOutBox::launchAsynchronously (std::make_unique<PluginPicker> (plugins, pick, loadFile), area, parent);
}

std::optional<MixerComponent::InsertTarget> MixerComponent::findTarget (const void* chain) const
{
    if (chain == nullptr)
        return std::nullopt;
    auto& strips = engine.getStrips();
    for (int i = 0; i < (int) strips.size(); ++i)
        if (&strips[(size_t) i]->inserts == chain)
            return InsertTarget { InsertTarget::strip, i, -1 };
    auto& buses = engine.getBuses();
    for (int i = 0; i < (int) buses.size(); ++i)
    {
        if (&buses[(size_t) i]->inserts == chain)
            return InsertTarget { InsertTarget::bus, i, -1 };
        for (int p = 0; p < (int) buses[(size_t) i]->paths.size(); ++p)
            if (&buses[(size_t) i]->paths[(size_t) p]->inserts == chain)
                return InsertTarget { InsertTarget::bus, i, p };
    }
    return std::nullopt;
}

std::vector<std::unique_ptr<juce::AudioPluginInstance>>* MixerComponent::chainOf (InsertTarget target) const
{
    auto pick = [&target] (auto& list) -> std::vector<std::unique_ptr<juce::AudioPluginInstance>>*
    {
        return juce::isPositiveAndBelow (target.index, (int) list.size()) ? &list[(size_t) target.index]->inserts : nullptr;
    };

    if (target.kind == InsertTarget::bus && target.path >= 0)
    {
        auto& buses = engine.getBuses();
        if (! juce::isPositiveAndBelow (target.index, (int) buses.size())
            || ! juce::isPositiveAndBelow (target.path, (int) buses[(size_t) target.index]->paths.size()))
            return nullptr;
        return &buses[(size_t) target.index]->paths[(size_t) target.path]->inserts;
    }

    switch (target.kind)
    {
        case InsertTarget::strip: return pick (engine.getStrips());
        case InsertTarget::bus:  return pick (engine.getBuses());
    }

    return nullptr;
}

void MixerComponent::insertPlugin (InsertTarget target, const juce::String& path)
{
    // A plugin new to Patchbay (or updated since) is tried in a separate copy first
    // (PluginScan.h); one that failed its test isn't loaded unless it's tested again.
    // Waiting on a test or a question, channels can move or go: find the same chain again (or give up).
    const void* const chain = chainOf (target);
    auto again = [chain] (MixerComponent& m, InsertTarget, const juce::String& file)
    {
        if (const auto now = m.findTarget (chain))
            m.insertPlugin (*now, file);
    };

    if (path != VoiceChain::identifier)
    {
        const auto known = PluginScan::find (path);
        const auto name = juce::File (path).getFileNameWithoutExtension();
        if (known && ! known->passed)
        {
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "PLUGIN FAILED ITS TEST",
                                                name + " wasn't loaded: " + known->reason + ".\n\nTest it again?", "TEST AGAIN", "CANCEL", nullptr,
                                                juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MixerComponent> (this), target, path, again] (int ok)
                                                {
                                                    if (ok != 0 && safe != nullptr)
                                                    {
                                                        PluginScan::forget (path);
                                                        again (*safe, target, path);
                                                    }
                                                }));
            return;
        }
        if (! known)
        {
            engine.logEvent ("plugins: testing " + name + " in a separate copy before it loads");
            juce::Thread::launch ([safe = juce::Component::SafePointer<MixerComponent> (this), target, path, name, again]
            {
                const auto failed = PluginScan::test (path);
                juce::MessageManager::callAsync ([safe, target, path, name, failed, again]
                {
                    if (safe == nullptr)
                        return;
                    if (failed.isEmpty())
                        return again (*safe, target, path);
                    safe->engine.logEvent ("plugins: " + name + " failed its test: " + failed);
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "PLUGIN FAILED ITS TEST",
                                                            name + " wasn't loaded: " + failed + ".");
                });
            });
            return;
        }
    }

    const auto error = target.kind == InsertTarget::bus ? (target.path >= 0 ? engine.addPathInsert (target.index, target.path, path)
                                                                             : engine.addBusInsert (target.index, path))
                                                         : engine.addInsert (target.index, path);

    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't load plugin", error);

}

void MixerComponent::checkSetupSoon (AudioEngine& engine)
{
    // After the source switch (it waits for a fade) has happened. The
    // engine outlives the window, so it's safe to use here.
    juce::Timer::callAfterDelay (1500, [&engine]
    {
        auto problems = engine.findDoubling();
        if (const auto loop = engine.findFeedbackLoop(); loop.isNotEmpty())
            problems.insert (0, "Feedback loop: " + loop);
        if (problems.isEmpty())
            return;
        for (auto& p : problems)
            engine.logEvent ("setup: " + p);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "HEARD TWICE",
                                                "Some sound would play twice or echo:\n\n- " + problems.joinIntoString ("\n- "));
    });
}

void MixerComponent::removeInsert (InsertTarget target, int slot)
{
    auto* chain = chainOf (target);

    if (chain == nullptr || ! juce::isPositiveAndBelow (slot, (int) chain->size()))
        return;
    if (pluginLocked ((*chain)[(size_t) slot].get()))
        return engine.logEvent ("plugins: " + (*chain)[(size_t) slot]->getName() + " is locked (unlock it in the rack to remove it)");

    // Before the engine frees it: every open rack (the desk's, the headset's) drops its faceplate.
    releaseFromPages ((*chain)[(size_t) slot].get());
    closeEditor ((*chain)[(size_t) slot].get());

    switch (target.kind)
    {
        case InsertTarget::strip: engine.removeInsert (target.index, slot); break;
        case InsertTarget::bus:
            if (target.path >= 0) engine.removePathInsert (target.index, target.path, slot);
            else                  engine.removeBusInsert (target.index, slot);
            break;
    }
}

bool MixerComponent::pluginLocked (const juce::AudioPluginInstance* plugin) const
{
    const auto* o = engine.insertOptions (plugin);
    return o != nullptr && o->locked;
}

void MixerComponent::swapInserts (InsertTarget target, int a, int b)
{
    if (auto* chain = chainOf (target))
    {
        const bool inRange = juce::isPositiveAndBelow (a, (int) chain->size()) && juce::isPositiveAndBelow (b, (int) chain->size());
        if (inRange && (pluginLocked ((*chain)[(size_t) a].get()) || pluginLocked ((*chain)[(size_t) b].get())))
            engine.logEvent ("plugins: a locked plugin stays in its slot");   // (the slots spring back)
        else
            engine.swapInserts (*chain, a, b);
    }
}

void MixerComponent::openEditor (juce::AudioProcessor& p)
{
    // (The racks' faceplates read the plugin's parameters, not its editor: opening its window
    // leaves them be.)
    if (auto it = editors.find (&p); it != editors.end())
        return it->second->toFront (true);

    auto* key = &p;
    auto* options = engine.insertOptions (dynamic_cast<juce::AudioPluginInstance*> (&p));

    // Which input or bus it's on ("input:MIC"), for its settings' MIDI targets (a combinator
    // path's plugin has none).
    juce::String owner;
    auto search = [&p, &owner] (const auto& list, const juce::String& kind)
    {
        for (auto& s : list)
            for (auto& plugin : s->inserts)
                if (plugin.get() == &p)
                    owner = kind + ":" + s->name;
    };
    search (engine.getStrips(), "input");
    search (engine.getBuses(), "bus");

    editors[key] = std::make_unique<PluginWindow> (p, options, engine.getSampleRate(), engine.getRemote(), owner, [this, key]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MixerComponent> (this), key]
        {
            if (safe != nullptr)
                safe->closeEditor (key);
        });
    });
}

void MixerComponent::closeEditor (juce::AudioProcessor* p)
{
    // Just its window: the pages keep their racks (remaking them would drop a cable being
    // carried). Whatever removes the plugin has the pages let go of it first (releaseFromPages).
    editors.erase (p);
}

void MixerComponent::releaseFromPages (juce::AudioProcessor* p)
{
    otherPages.erase (std::remove_if (otherPages.begin(), otherPages.end(), [] (auto& s) { return s == nullptr; }), otherPages.end());
    for (auto& page : otherPages)
        page->release (p);
}

std::unique_ptr<juce::Component> MixerComponent::createRackPage (const juce::String& key)
{
    const auto kind = key.upToFirstOccurrenceOf (":", false, false), name = key.fromFirstOccurrenceOf (":", false, false);
    for (auto& s : (kind == "bus" ? engine.getBuses() : engine.getStrips()))
        if (s->name == name && s->channel != nullptr)
        {
            auto page = std::make_unique<SelectedPage> (*this, *s, key, (s->colour.isTransparent() ? (kind == "bus" ? Console::Colours::kindBus() : Console::Colours::kindInput()) : s->colour), false);
            page->openRack();
            otherPages.push_back (page.get());
            return page;
        }
    return nullptr;
}

void MixerComponent::closeEditorsFor (InsertTarget target)
{
    if (auto* chain = chainOf (target))
        for (auto& plugin : *chain)
        {
            releaseFromPages (plugin.get());   // (every caller removes them next)
            closeEditor (plugin.get());
        }

    // A combinator's paths go with it.
    if (target.kind == InsertTarget::bus && target.path < 0 && juce::isPositiveAndBelow (target.index, (int) engine.getBuses().size()))
        for (int p = 0; p < (int) engine.getBuses()[(size_t) target.index]->paths.size(); ++p)
            closeEditorsFor ({ InsertTarget::bus, target.index, p });
}

// A / B: the channel's settings now kept in the slot it's on, and the other slot's put on (the
// first time, the other slot starts as a copy of these). Built-in processing and every plugin's
// state, without reloading anything: plugins added or removed since keep what they have.
void MixerComponent::compare (const juce::String& key, int slot)
{
    const auto kind = key.upToFirstOccurrenceOf (":", false, false), name = key.fromFirstOccurrenceOf (":", false, false);
    AudioEngine::Strip* strip = nullptr;
    for (auto& s : kind == "bus" ? engine.getBuses() : engine.getStrips())
        if (s->name == name)
            strip = s.get();
    auto& c = compares[key];
    if (strip == nullptr || strip->channel == nullptr || slot == c.active)
        return;

    auto keep = [strip] (CompareSlot& into)
    {
        into.channel.reset();
        strip->channel->getStateInformation (into.channel);
        into.plugins.clear();
        for (auto& p : strip->inserts)
        {
            into.plugins.emplace_back();
            p->getStateInformation (into.plugins.back());
        }
        into.kept = true;
    };
    keep (c.slots[c.active]);
    if (! c.slots[slot].kept)
        c.slots[slot] = c.slots[c.active];   // B starts as A
    else
    {
        const auto& from = c.slots[slot];
        strip->channel->setStateInformation (from.channel.getData(), (int) from.channel.getSize());
        for (size_t i = 0; i < strip->inserts.size() && i < from.plugins.size(); ++i)
            if (from.plugins[i].getSize() > 0)
                strip->inserts[i]->setStateInformation (from.plugins[i].getData(), (int) from.plugins[i].getSize());
    }
    c.active = slot;
    engine.logEvent (strip->name + ": settings " + (slot == 0 ? "A" : "B"));
}

int MixerComponent::compareSlot (const juce::String& key) const
{
    const auto it = compares.find (key);
    return it != compares.end() ? it->second.active : 0;
}

bool MixerComponent::revertChannel (const juce::String& key, const juce::XmlElement& saved)
{
    const auto kind = key.upToFirstOccurrenceOf (":", false, false), name = key.fromFirstOccurrenceOf (":", false, false);
    auto& list = kind == "bus" ? engine.getBuses() : engine.getStrips();
    int index = -1;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i]->name == name)
            index = i;
    if (index < 0)
        return false;
    const InsertTarget target { kind == "bus" ? InsertTarget::bus : InsertTarget::strip, index };
    auto& strip = *list[(size_t) index];
    for (auto& p : strip.inserts)
        if (pluginLocked (p.get()))
            return false;

    closeEditorsFor (target);   // (every page lets go of its plugins with them, before the rack is replaced)
    strip.gain = (float) saved.getDoubleAttribute ("gain", strip.gain.load());
    strip.pan = (float) saved.getDoubleAttribute ("pan", strip.pan.load());
    strip.mute = saved.getBoolAttribute ("mute", strip.mute.load());
    juce::XmlElement rack (saved);
    rack.setTagName ("Rack");
    Session::loadRackPreset (engine, kind, index, rack, [this] (const juce::String& m) { engine.logEvent (m); });
    return true;
}

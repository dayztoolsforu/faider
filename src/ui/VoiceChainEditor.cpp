#include "../engine/VoiceChain.h"
#include "Console.h"
#include "ConsoleControls.h"
#include "DotMatrix.h"

namespace
{
    // The voice chain's panel: presets across the top, then a column per stage (its IN key, its
    // knobs on screens, and what it's doing now), in signal order.
    class VoiceChainEditor : public juce::AudioProcessorEditor,
                             private juce::Timer
    {
    public:
        explicit VoiceChainEditor (VoiceChain& v) : AudioProcessorEditor (v), chain (v)
        {
            title.setFont (Console::legendFont (Console::Type::display));
            addAndMakeVisible (title);
            presets.onChange = [this] (int p) { chain.loadPreset (p); };
            presets.setTooltip ("Preset");
            addAndMakeVisible (presets);

            stage ("HPF",    "hpfOn",   { knob ("hpf", hzText) }, nullptr);
            stage ("GATE",   "gateOn",  { knob ("gateThreshold", dbText) }, &chain.gateOpen);
            stage ("COMP",   "compOn",  { knob ("compThreshold", dbText), knob ("compRatio", ratioText), knob ("makeup", dbText) }, &chain.compReduction);
            stage ("EQ",     "eqOn",    { knob ("lowGain", dbText), knob ("midGain", dbText), knob ("midFreq", hzText), knob ("highGain", dbText) }, nullptr);
            stage ("DE-ESS", "deessOn", { knob ("deessThreshold", dbText), knob ("deessFreq", hzText) }, &chain.deessReduction);
            stage ("LIMIT",  "limitOn", { knob ("ceiling", dbText) }, &chain.limitReduction);

            setSize (6 * columnWidth + 24, 360);
            startTimerHz (20);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Console::Colours::panel());
            for (int i = 1; i < (int) stages.size(); ++i)
            {
                g.setColour (Console::Colours::rim());
                g.fillRect (12 + i * columnWidth, 56, 1, getHeight() - 68);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (Console::Space::pad);
            auto top = r.removeFromTop (Console::Space::bar).reduced (Console::Space::gap, 0);   // in line with the stages' keys
            title.setBounds (top.removeFromLeft (160));
            Console::KeyRow keys (top);
            keys.right (presets, VoiceChain::presetNames().size());
            r.removeFromTop (Console::Space::pad);   // (paint's dividers start here)

            for (auto& s : stages)
            {
                auto col = r.removeFromLeft (columnWidth).reduced (Console::Space::gap, 0);
                s->key->setBounds (col.removeFromTop (Console::Space::key));
                col.removeFromTop (Console::Space::gap);
                for (auto* k : s->knobs)
                {
                    k->label.setBounds (col.removeFromTop (16));
                    k->control.setBounds (col.removeFromTop (40));
                    col.removeFromTop (8);
                }
                if (s->meter != nullptr)
                    s->screen.setBounds (col.removeFromBottom (28));
            }
        }

    private:
        static constexpr int columnWidth = 150;

        struct Knob
        {
            juce::Label label;
            ConsoleControls::ValueKnob control;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        };

        struct Stage
        {
            std::unique_ptr<juce::TextButton> key;
            std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
            juce::OwnedArray<Knob> knobs;
            std::atomic<float>* meter = nullptr;
            juce::Label screen;
        };

        static juce::String dbText (double v)    { return juce::String (v, 1) + " dB"; }
        static juce::String hzText (double v)    { return v >= 1000.0 ? juce::String (v / 1000.0, 1) + " kHz" : juce::String ((int) v) + " Hz"; }
        static juce::String ratioText (double v) { return juce::String (v, 1) + ":1"; }

        Knob* knob (const char* id, std::function<juce::String (double)> format)
        {
            auto* k = new Knob();
            auto* param = chain.params.getParameter (id);
            k->label.setText (param != nullptr ? param->getName (32).toUpperCase() : juce::String (id), juce::dontSendNotification);
            k->label.setFont (Console::legendFont (Console::Type::legend));
            k->control.format = std::move (format);
            k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (chain.params, id, k->control.knob);
            k->control.setValue (k->control.knob.getValue());   // its screen shows the value at once
            if (param != nullptr)
                k->control.knob.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
            addAndMakeVisible (k->label);
            addAndMakeVisible (k->control);
            return k;
        }

        void stage (const juce::String& name, const char* onId, std::initializer_list<Knob*> knobs, std::atomic<float>* meter)
        {
            auto s = std::make_unique<Stage>();
            s->key = std::make_unique<juce::TextButton> (name);
            s->key->setClickingTogglesState (true);
            s->key->setColour (juce::TextButton::buttonOnColourId, Console::Colours::on());   // a stage in
            s->key->setTooltip (name + " in/out");
            s->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (chain.params, onId, *s->key);
            addAndMakeVisible (*s->key);
            for (auto* k : knobs)
                s->knobs.add (k);
            s->meter = meter;
            if (meter != nullptr)
            {
                DotMatrix::markReadout (s->screen);
                s->screen.setTooltip (meter == &chain.gateOpen ? "Gate: open / closed" : "Gain reduction");
                addAndMakeVisible (s->screen);
            }
            stages.push_back (std::move (s));
        }

        void timerCallback() override
        {
            for (auto& s : stages)
            {
                if (s->meter == nullptr)
                    continue;
                const float v = s->meter->load();
                s->screen.setText (s->meter == &chain.gateOpen ? juce::String (v > 0.5f ? "OPEN" : "CLOSED")
                                                               : "GR " + juce::String (v, 1), juce::dontSendNotification);
            }
        }

        VoiceChain& chain;
        juce::Label title { {}, "VOICE CHAIN" };
        ConsoleControls::KeyRow presets { VoiceChain::presetNames() };
        std::vector<std::unique_ptr<Stage>> stages;
    };
}

juce::AudioProcessorEditor* VoiceChain::createEditor()
{
    return new VoiceChainEditor (*this);
}

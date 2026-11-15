#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/AudioEngine.h"
#include "ProStrip.h"
#include "Console.h"
#include "Theme.h"

// MIX-MINUS AND STEMS: as the console builds a mix-minus feed for each caller and music /
// dialogue / effects stems through its groups. Per channel: MINUS (an output that's the main mix
// without that channel: the feed for that guest's call, so they don't hear themselves) and a STEM
// (VOICE, MUSIC or GAME: an output each, fed by the channels picked, to record apart). BUILD makes
// the outputs (named MINUS <guest> and STEM <kind>, on no device yet: pick theirs in OUTPUTS).
class MixMinusBuilder : public juce::Component
{
public:
    // The builder in a dialog of its own, centred on `near`.
    static void showBuilder (AudioEngine& engine, std::function<void()> built, juce::Component* near)
    {
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (new MixMinusBuilder (engine, std::move (built)));
        o.dialogTitle = TRANS ("Mix-minus and stems");
        o.dialogBackgroundColour = Console::Colours::panel();
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.componentToCentreAround = near;
        o.launchAsync();
    }

    MixMinusBuilder (AudioEngine& e, std::function<void()> builtFn) : engine (e), built (std::move (builtFn))
    {
        for (auto& b : engine.getMasters())
            mainPick.addItem (b->name, mainPick.getNumItems() + 1);
        for (int i = 0; i < (int) engine.getMasters().size(); ++i)
            if (engine.getMasters()[(size_t) i]->name == engine.mainMixMaster)
                mainPick.setSelectedItemIndex (i, juce::dontSendNotification);
        if (mainPick.getSelectedItemIndex() < 0 && mainPick.getNumItems() > 0)
            mainPick.setSelectedItemIndex (0, juce::dontSendNotification);
        addAndMakeVisible (mainPick);

        for (auto& s : engine.getStrips())
        {
            auto* row = rows.add (new Row());
            row->name = s->name;
            ProStrip::styleKey (row->minus);
            row->minus.setButtonText (TRANS ("MINUS"));
            row->minus.setTooltip (TRANS ("Make %1 a mix-minus: the main mix without them, for their call").replace ("%1", s->name));
            ProStrip::styleKey (row->stem);
            row->stem.setClickingTogglesState (false);
            row->stem.onClick = [row] { row->stemKind = (row->stemKind + 1) % 4; row->stem.setButtonText (juce::translate (stemNames[row->stemKind])); };
            row->stem.setButtonText (juce::translate (stemNames[0]));
            row->stem.setTooltip (TRANS ("Which stem it goes to (an output each, to record apart)"));
            addAndMakeVisible (row->minus);
            addAndMakeVisible (row->stem);
        }
        ProStrip::styleKey (build);
        build.setClickingTogglesState (false);
        build.onClick = [this] { buildIt(); };
        addAndMakeVisible (build);
        setSize (460, 120 + rows.size() * 26);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = Theme::get();
        g.fillAll (t.panel);
        g.setColour (t.legend);
        g.setFont (Console::legendFont (Console::Type::display));
        g.drawFittedText (TRANS ("MIX-MINUS AND STEMS"), getLocalBounds().reduced (12).removeFromTop (26), juce::Justification::centredLeft, 1, 0.7f);
        g.setFont (Console::legendFont (Console::Type::tag));
        g.setColour (t.legendDim);
        g.drawFittedText (TRANS ("MAIN MIX"), 12, 44, 80, 22, juce::Justification::centredLeft, 1, 0.7f);
        for (auto* r : rows)
        {
            g.setColour (t.legend);
            g.drawText (r->name, r->area.withWidth (220), juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        mainPick.setBounds (96, 44, 220, 22);
        int y = 76;
        for (auto* r : rows)
        {
            r->area = { 12, y, getWidth() - 24, 22 };
            r->stem.setBounds (getWidth() - 12 - 90, y, 90, 22);
            r->minus.setBounds (getWidth() - 12 - 90 - 6 - 80, y, 80, 22);
            y += 26;
        }
        build.setBounds (getLocalBounds().removeFromBottom (40).reduced (12, 8).removeFromRight (120));
    }

private:
    struct Row
    {
        juce::String name;
        juce::TextButton minus, stem;
        int stemKind = 0;
        juce::Rectangle<int> area;
    };
    static inline const char* stemNames[] = { NEEDS_TRANS ("NO STEM"), NEEDS_TRANS ("VOICE"), NEEDS_TRANS ("MUSIC"), NEEDS_TRANS ("GAME") };   // (English: the outputs are named STEM <kind>; the keys show them translated)

    void buildIt()
    {
        const auto main = mainPick.getText();
        int made = 0;
        for (auto* r : rows)
            if (r->minus.getToggleState() && main.isNotEmpty())
                if (auto* m = engine.addMaster ("MINUS " + r->name, {}))
                {
                    engine.setMixMinus (*m, main, r->name);
                    ++made;
                }
        for (int kind = 1; kind < 4; ++kind)
        {
            juce::StringArray members;
            for (auto* r : rows)
                if (r->stemKind == kind)
                    members.add (r->name);
            if (members.isEmpty())
                continue;
            if (engine.addMaster ("STEM " + juce::String (stemNames[kind]), {}) == nullptr)
                continue;
            const auto index = engine.getMasters().size() - 1;
            for (auto& s : engine.getStrips())
                if (members.contains (s->name))
                    s->sends[index] = true;
            ++made;
        }
        engine.logEvent ("mix-minus and stems: " + juce::String (made) + " outputs made (put them on devices in OUTPUTS)");
        if (built)
            built();
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    }

    AudioEngine& engine;
    std::function<void()> built;
    juce::ComboBox mainPick;
    juce::OwnedArray<Row> rows;
    juce::TextButton build { TRANS ("BUILD") };
};

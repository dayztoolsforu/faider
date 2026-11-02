#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/VoiceChain.h"
#include "Console.h"
#include "EqCurve.h"
#include "MidiMap.h"
#include "ProStrip.h"
#include "StageGraphs.h"
#include "StageMeter.h"

// The selected channel's CHANNEL page (on the SELECTED page across the top of the Mixer), as on a
// touchscreen console, one page at a time: EQ (large, a handle per band and each band's values in
// boxes under it), then DYN (DYN 1, the gate, beside DYN 2, the compressor, as graphs with their
// values), then the other built-in units, small ones sharing a page (CLEAN-UP: input, noise,
// de-esser, auto-level; BUS + LIMIT; VOICE FX: robot, echo, radio, pitch, each effect apart),
// each in a panel of its own. The
// keys in the name bar (or its arrows) slide from page to page; the page stays chosen from one
// channel to the next. Every value box drags up or down (double-click: its default); the graphs'
// handles drag too. The same settings as the rack's built-in units.
class SelectedChannel : public juce::Component,
                        private juce::Timer
{
public:
    // `key`: the channel's MIDI target name ("input:MIC"), `colour` its strip colour.
    SelectedChannel (std::shared_ptr<VoiceChain> c, const juce::String& name, juce::Colour colour, const juce::String& key)
        : chain (std::move (c)), title (name.toUpperCase()), accent (colour), target (key),
          eqGraph (chain, true), gateGraph (chain, true), compGraph (chain, false),
          eqMeter (chain, StageMeter::inOut, VoiceChain::afterEq),
          gateMeter (chain, StageMeter::level, VoiceChain::afterNoise, nullptr, true),
          compMeter (chain, StageMeter::reduction, VoiceChain::afterComp, &chain->compReduction)
    {
        addAndMakeVisible (eqGraph);
        addAndMakeVisible (gateGraph);
        addAndMakeVisible (compGraph);
        for (auto* m : { &eqMeter, &gateMeter, &compMeter })
            addAndMakeVisible (m);

        stageKey (eqIn, "eqOn", "EQ in/out");
        stageKey (hpfIn, "hpfOn", "HPF in/out");
        stageKey (hpfSteep, "hpfSteep", "Steeper HPF slope (24 dB/oct)");
        stageKey (lpfIn, "lpfOn", "LPF in/out");
        stageKey (gateIn, "gateOn", "Gate in/out");
        stageKey (compIn, "compOn", "Compressor in/out");

        // The band row, in the bands' colours (as their handles on the graph).
        band (EqCurve::hpfBand, "HPF", { { "hpf", "FREQ" } });
        band (EqCurve::lowBand, "1  LOW", { { "lowFreq", "FREQ" }, { "lowGain", "GAIN" } });
        band (EqCurve::midBand, "2  MID", { { "midQ", "Q" }, { "midFreq", "FREQ" }, { "midGain", "GAIN" } });
        band (EqCurve::mid2Band, "3  MID", { { "mid2Q", "Q" }, { "mid2Freq", "FREQ" }, { "mid2Gain", "GAIN" } });
        band (EqCurve::highBand, "4  HIGH", { { "highFreq", "FREQ" }, { "highGain", "GAIN" } });
        band (EqCurve::lpfBand, "LPF", { { "lpf", "FREQ" } });

        box (gateBoxes, "gateThreshold", "THRESH");
        box (gateBoxes, "gateRange", "RANGE");
        box (gateBoxes, "gateAttack", "ATTACK");
        box (gateBoxes, "gateHold", "HOLD");
        box (gateBoxes, "gateRelease", "RELEASE");
        box (gateBoxes, "gateHysteresis", "HYST");
        box (compBoxes, "compThreshold", "THRESH");
        box (compBoxes, "compRatio", "RATIO");
        box (compBoxes, "makeup", "MAKEUP");
        box (compBoxes, "compAttack", "ATTACK");
        box (compBoxes, "compRelease", "RELEASE");
        box (compBoxes, "compKnee", "KNEE");
        box (compBoxes, "compMix", "MIX");

        // The other built-in units, a page each (AudioEngine::RackBuiltin numbers).
        for (auto& u : unitPages())
        {
            auto* unit = units.add (new ProStrip::StageUnit (chain, u.builtin));
            unit->setKnobWidth (88);
            addAndMakeVisible (unit);
        }

        // The page keys: one per page, and arrows either side.
        for (const auto& name : pageNames())
        {
            auto* k = pageKeys.add (new juce::TextButton (name));
            k->setClickingTogglesState (true);   // (lit while its page shows, as the tabs are)
            k->setRadioGroupId (1);
            k->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
            k->onClick = [this, i = pageKeys.size() - 1] { showPage (i); };
            addAndMakeVisible (k);
        }
        prevKey.setTooltip ("Previous page");
        prevKey.onClick = [this] { showPage (page - 1); };
        nextKey.setTooltip ("Next page");
        nextKey.onClick = [this] { showPage (page + 1); };
        addAndMakeVisible (prevKey);
        addAndMakeVisible (nextKey);

        page = juce::jlimit (0, pageKeys.size() - 1, lastPage);
        pageX = page;   // (no slide on choosing a channel: it opens on that page)
        showPage (page);
    }

    void paint (juce::Graphics& g) override
    {
        // The channel's name on its colour, then a panel per section.
        g.setColour (accent);
        g.fillRoundedRectangle (nameArea.toFloat(), 3.0f);
        g.setColour (accent.getPerceivedBrightness() > 0.7f ? juce::Colour (0xff141414) : juce::Colours::white);
        g.setFont (Console::legendFont (Console::Type::display));
        g.drawText (title, nameArea.reduced (10, 0), juce::Justification::centredLeft, true);

        for (auto& p : panels)
        {
            g.setColour (Console::Colours::panel());
            g.fillRoundedRectangle (p.area.toFloat(), 4.0f);
            g.setColour (Console::Colours::rim());
            g.drawRoundedRectangle (p.area.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setColour (Console::Colours::legend());
            g.setFont (Console::legendFont (Console::Type::body));
            g.drawText (p.name, p.head, juce::Justification::centredLeft, false);
            g.setColour (Console::Colours::legendDim());
            g.setFont (Console::legendFont (Console::Type::legend));
            g.drawText (p.detail, p.head, juce::Justification::centredRight, false);
        }

        for (auto* b : bands)
        {
            g.setColour (EqCurve::bandColour (b->index));
            g.fillRoundedRectangle (b->head.toFloat().withHeight (3.0f), 1.5f);
            g.setColour (Console::Colours::legend());
            g.setFont (Console::legendFont (Console::Type::legend));
            g.drawText (b->name, b->head.withTrimmedTop (3), juce::Justification::centredLeft, false);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        nameArea = r.removeFromTop (28);
        r.removeFromTop (8);
        panels.clear();

        // The page keys at the right of the name bar: ◀ EQ DYN ... LIMITER ▶.
        {
            auto bar = nameArea.reduced (3, 3);
            nextKey.setBounds (bar.removeFromRight (30));
            bar.removeFromRight (Console::Space::unit);
            for (int i = pageKeys.size(); --i >= 0;)
            {
                pageKeys[i]->setBounds (bar.removeFromRight (Console::Space::columns (Console::KeyRow::columnsFor (pageKeys[i]->getButtonText()))));
                bar.removeFromRight (Console::Space::unit);
            }
            prevKey.setBounds (bar.removeFromRight (30));
        }

        // The pages side by side, a gap apart, the shown one in view (pageX glides between them).
        const int pitch = r.getWidth() + 24;
        auto pageArea = [&r, pitch, this] (int i) { return r.translated (juce::roundToInt (((double) i - pageX) * pitch), 0); };

        auto eqArea = addPanel (pageArea (0), "EQ", "HPF  4 BANDS  LPF", { &hpfIn, &hpfSteep, &eqIn, &lpfIn });
        auto row = eqArea.removeFromBottom (64);
        eqArea.removeFromBottom (8);
        eqMeter.setBounds (eqArea.removeFromBottom (eqMeter.wanted()));
        eqArea.removeFromBottom (6);
        eqGraph.setBounds (eqArea);

        // The band row: each band as wide as its boxes.
        int boxes = 0;
        for (auto* b : bands) boxes += b->sliders.size();
        const int unit = (row.getWidth() - Console::Space::pad * (bands.size() - 1)) / juce::jmax (1, boxes);
        for (auto* b : bands)
        {
            auto cell = row.removeFromLeft (unit * b->sliders.size());
            row.removeFromLeft (Console::Space::pad);
            b->head = cell.removeFromTop (18);
            cell.removeFromTop (2);
            // The boxes a unit apart, flush with the band's colour bar at each end.
            const int n = juce::jmax (1, b->sliders.size());
            const int w = (cell.getWidth() - (n - 1) * Console::Space::unit) / n;
            for (auto* s : b->sliders)
            {
                s->setBounds (cell.removeFromLeft (s == b->sliders.getLast() ? cell.getWidth() : w));
                cell.removeFromLeft (Console::Space::unit);
            }
        }

        // DYN: the gate beside the compressor.
        auto dyn = pageArea (1);
        auto dyn1 = dyn.removeFromLeft ((dyn.getWidth() - 8) / 2);
        dyn.removeFromLeft (8);
        layoutDyn (addPanel (dyn1, "DYN 1", "GATE", { &gateIn }), gateGraph, gateMeter, gateBoxes);
        layoutDyn (addPanel (dyn, "DYN 2", "COMP", { &compIn }), compGraph, compMeter, compBoxes);

        // The other units, several to a page (small ones share), each in a panel of its own as
        // wide as its share of the page by the room it wants, its controls centred in it.
        int first = 0;
        for (int p = 0; p < (int) unitGroups().size(); ++p)
        {
            auto area = pageArea (2 + p);
            const int count = unitGroups()[(size_t) p].count;
            const int pad = 2 * Console::Space::pad;
            int wanted = 0;
            for (int i = first; i < first + count; ++i)
                wanted += units[i]->widthWanted() + pad;
            const int room = area.getWidth() - 8 * (count - 1);
            for (int i = first; i < first + count; ++i)
            {
                const int w = i == first + count - 1 ? area.getWidth() : (units[i]->widthWanted() + pad) * room / juce::jmax (1, wanted);
                auto inside = addPanel (area.removeFromLeft (w), unitPages()[(size_t) i].name, unitPages()[(size_t) i].detail, {});
                area.removeFromLeft (8);
                units[i]->setBounds (inside.withSizeKeepingCentre (juce::jmin (inside.getWidth(), units[i]->widthWanted()),
                                                                   juce::jmin (inside.getHeight(), 150)));
            }
            first += count;
        }
    }

    // A page by number (0 EQ, 1 DYN, then the units), sliding to it.
    void showPage (int index)
    {
        page = juce::jlimit (0, pageKeys.size() - 1, index);
        lastPage = page;
        for (int i = 0; i < pageKeys.size(); ++i)
            pageKeys[i]->setToggleState (i == page, juce::dontSendNotification);
        prevKey.setEnabled (page > 0);
        nextKey.setEnabled (page < pageKeys.size() - 1);
        if (pageX != (double) page)
            startTimerHz (60);
        resized();
        repaint();
    }

private:
    struct Panel { juce::Rectangle<int> area, head; juce::String name, detail; };

    struct UnitPage { int builtin; const char* name; const char* detail; };
    static const std::vector<UnitPage>& unitPages()
    {
        static const std::vector<UnitPage> list {   // (in unitGroups' order)
            { 5, "INPUT", "TRIM  POLARITY  DELAY" }, { 6, "NOISE", "NOISE REDUCTION" }, { 7, "DE-ESS", "DE-ESSER" }, { 8, "LEVEL", "AUTO-LEVEL" },
            { 4, "BUS COMP", "GLUE COMPRESSOR" }, { 10, "LIMITER", "CEILING" },
            { ProStrip::StageUnit::robotUnit, "ROBOT", "VOICE FX" }, { ProStrip::StageUnit::echoUnit, "ECHO", "VOICE FX" },
            { ProStrip::StageUnit::radioUnit, "RADIO", "VOICE FX" }, { ProStrip::StageUnit::pitchUnit, "PITCH", "VOICE FX" } };
        return list;
    }
    // The unit pages: how many of unitPages, in order, each holds.
    struct UnitGroup { const char* name; int count; };
    static const std::vector<UnitGroup>& unitGroups()
    {
        static const std::vector<UnitGroup> list { { "CLEAN-UP", 4 }, { "BUS + LIMIT", 2 }, { "VOICE FX", 4 } };
        return list;
    }
    static juce::StringArray pageNames()
    {
        juce::StringArray names { "EQ", "DYN" };
        for (auto& g : unitGroups())
            names.add (g.name);
        return names;
    }

    // The slide: eases to the page chosen.
    void timerCallback() override
    {
        pageX = std::abs (page - pageX) < 0.002 ? (double) page : pageX + (page - pageX) * 0.22;
        if (pageX == (double) page)
            stopTimer();
        resized();
        repaint();
    }

    struct Band
    {
        int index;
        juce::String name;
        juce::Rectangle<int> head;
        juce::OwnedArray<juce::Slider> sliders;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> attachments;
    };

    // A panel's frame and heading (its IN keys at the right of the heading); returns its inside.
    juce::Rectangle<int> addPanel (juce::Rectangle<int> area, const juce::String& name, const juce::String& detail,
                                   std::initializer_list<juce::Button*> keys)
    {
        Panel p { area, {}, name, detail };
        auto inside = area.reduced (Console::Space::pad, Console::Space::gap);
        Console::KeyRow head (inside.removeFromTop (Console::Space::key));
        for (auto* k : keys)
            head.left (*k);
        p.head = head.rest().withTrimmedLeft (keys.size() > 0 ? Console::Space::pad : 0);
        panels.push_back (p);
        inside.removeFromTop (Console::Space::gap);
        return inside;
    }

    // A dynamics panel: the graph square on the left with its meter under it, its value boxes
    // on the right in columns (as many as keep every box full height), filled down the first, then the next.
    void layoutDyn (juce::Rectangle<int> inside, DynGraph& graph, StageMeter& meter, juce::OwnedArray<juce::Slider>& boxes)
    {
        const int fit = juce::jmax (1, inside.getHeight() / 38);   // rows of boxes that fit at a readable height
        const int cols = juce::jmax (2, (boxes.size() + fit - 1) / fit);
        // The graph no wider than 8:5 (a page is wide), the boxes beside it.
        const int boxesWidth = Console::Space::columns (cols);
        const int graphWidth = juce::jmin (inside.getWidth() - boxesWidth - Console::Space::pad,
                                           (inside.getHeight() - meter.wanted() - Console::Space::gap) * 8 / 5);
        auto left = inside.removeFromLeft (graphWidth);
        inside.removeFromLeft (Console::Space::pad);
        auto columns = inside.removeFromLeft (boxesWidth);
        meter.setBounds (left.removeFromBottom (meter.wanted()));
        left.removeFromBottom (Console::Space::gap);
        graph.setBounds (left);
        const int rows = (boxes.size() + cols - 1) / cols;
        const int h = juce::jmin (44, columns.getHeight() / juce::jmax (1, rows));
        for (int i = 0; i < boxes.size(); ++i)
            boxes[i]->setBounds (columns.getX() + (i / rows) * (Console::Space::column + Console::Space::unit), columns.getY() + (i % rows) * h,
                                 Console::Space::column, h - Console::Space::unit);
    }

    void stageKey (juce::TextButton& b, const char* id, const juce::String& tip)
    {
        const juce::String key (id);
        b.setButtonText (key == "hpfOn" ? "HPF" : key == "hpfSteep" ? "24 DB" : key == "lpfOn" ? "LPF" : key == "eqOn" ? "EQ" : "IN");
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonOnColourId, key == "hpfSteep" ? Console::Colours::ledGreen()   // a mode
                                                                             : Console::Colours::on());       // a stage in: signal passes
        b.setTooltip (tip);
        keyAttachments.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (chain->params, id, b));
        MidiMap::tag (b, "param:" + target + ":" + id);
        addAndMakeVisible (b);
    }

    // A value box for a parameter: drag up or down; the value written short, by its unit.
    juce::Slider& makeBox (const char* id, const juce::String& name, juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment>& into,
                           juce::Slider* s)
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        Console::setValueBox (*s, name);
        auto* param = chain->params.getParameter (id);
        const auto unit = param != nullptr ? param->getLabel() : juce::String();
        const bool silentAtBottom = juce::String (id) == "gateRange";
        s->textFromValueFunction = [unit, silentAtBottom] (double v)
        {
            if (silentAtBottom && v <= VoiceChain::gateSilent) return juce::String ("-INF");
            if (unit == "Hz") return v >= 1000.0 ? juce::String (v / 1000.0, 2) + "k" : juce::String ((int) v);
            if (unit == "dB") return (v > 0.05 ? "+" : "") + juce::String (v, 1);
            if (unit == "Q")  return juce::String (v, 2);
            if (unit == "ms") return v < 10.0 ? juce::String (v, 1) : juce::String ((int) v);
            if (unit == "%")  return juce::String ((int) v);
            if (v >= VoiceChain::compRatioInf) return juce::String ("INF:1");
            return juce::String (v, 1) + ":1";
        };
        into.add (new juce::AudioProcessorValueTreeState::SliderAttachment (chain->params, id, *s));
        if (param != nullptr)
        {
            s->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
            s->setTooltip (param->getName (40) + (unit.isNotEmpty() && unit != "Q" ? " (" + unit + ")" : juce::String()));
        }
        MidiMap::tag (*s, "param:" + target + ":" + id);
        addAndMakeVisible (s);
        return *s;
    }

    struct Spec { const char* id; const char* name; };

    void band (int index, const juce::String& name, std::initializer_list<Spec> specs)
    {
        auto* b = bands.add (new Band());
        b->index = index;
        b->name = name;
        for (auto& spec : specs)
            makeBox (spec.id, spec.name, b->attachments, b->sliders.add (new juce::Slider()));
    }

    void box (juce::OwnedArray<juce::Slider>& list, const char* id, const juce::String& name)
    {
        makeBox (id, name, boxAttachments, list.add (new juce::Slider()));
    }

    std::shared_ptr<VoiceChain> chain;   // first: outlives the attachments
    const juce::String title;
    const juce::Colour accent;
    const juce::String target;
    EqCurve eqGraph;
    DynGraph gateGraph, compGraph;
    StageMeter eqMeter, gateMeter, compMeter;
    juce::TextButton eqIn, hpfIn, hpfSteep, lpfIn, gateIn, compIn;
    juce::OwnedArray<Band> bands;
    juce::OwnedArray<juce::Slider> gateBoxes, compBoxes;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> boxAttachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> keyAttachments;
    juce::Rectangle<int> nameArea;
    std::vector<Panel> panels;

    juce::OwnedArray<ProStrip::StageUnit> units;   // pages 2 on (unitPages, grouped by unitGroups)
    juce::OwnedArray<juce::TextButton> pageKeys;
    juce::TextButton prevKey { juce::String::fromUTF8 ("\xe2\x97\x80") }, nextKey { juce::String::fromUTF8 ("\xe2\x96\xb6") };
    int page = 0;
    double pageX = 0;               // the page in view, gliding toward `page`
    static inline int lastPage = 0;   // the page shown last, for the next channel chosen
};

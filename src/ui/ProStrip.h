#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/VoiceChain.h"
#include "Materials.h"
#include "Console.h"   // Console::Type

// The built-in processing as channel-strip units: a grey panel with
// darker sections, knobs with caps coloured by section (HF red, HMF green, LMF blue, LF brown,
// filters and dynamics grey), white legends, lit IN keys and LED gain-reduction meters. Its own
// look whatever the theme. ProStrip::StageUnit: one of the rack's 1U built-in
// units (FILTERS, GATE, COMPRESSOR, EQ, BUS COMP), a stage of the channel's VoiceChain.
// Every control is the VoiceChain's parameter, so the CHANNEL page follows.
namespace ProStrip
{
    // In the console's colours: its faceplate, printing, knob caps and lamps.
    namespace Colours
    {
        inline juce::Colour panel()     { return Theme::get().panel; }
        inline juce::Colour section()   { return Theme::get().panelDark; }
        inline juce::Colour rim()       { return Theme::get().rim; }
        inline juce::Colour legend()    { return Theme::get().legend; }
        inline juce::Colour legendDim() { return Theme::get().legendDim; }
        inline juce::Colour red()       { return Theme::get().knobRed; }
        inline juce::Colour green()     { return Theme::get().knobGreen; }
        inline juce::Colour blue()      { return Theme::get().knobBlue; }
        inline juce::Colour brown()     { return Theme::get().knobBrown; }
        inline juce::Colour grey()      { return Theme::get().knobGrey; }
        inline juce::Colour black()     { return Theme::get().knobGrey; }
        inline juce::Colour lit()       { return Theme::get().ledGreen; }
        inline juce::Colour amber()     { return Theme::get().ledYellow; }
        inline juce::Colour stripe()    { return Theme::get().ledBlue; }   // a built-in unit's stripe
    }

    // A rotary knob: a coloured cap with a white pointer on a silver skirt, ticks round it, its
    // name under it and its value shown while it's touched.
    class Knob : public juce::Slider
    {
    public:
        Knob (juce::Colour capColour, const juce::String& legendText) : cap (capColour), legend (legendText)
        {
            setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            setMouseDragSensitivity (180);
            setScrollWheelEnabled (false);   // (the wheel scrolls the view it sits in: knobs turn by dragging)
            setTooltip (legendText);
        }

        float fixedSize = 0.0f;   // LARGE FORMAT: the knob's size as the layout sets it (0: as its space allows)

        // A knob that reads the other way (the console's THRESHOLD, RANGE, Q): clockwise runs down
        // its range, drawn and dragged alike.
        bool reversed = false;
        double valueToProportionOfLength (double v) override { const double p = Slider::valueToProportionOfLength (v); return reversed ? 1.0 - p : p; }
        double proportionOfLengthToValue (double p) override { return Slider::proportionOfLengthToValue (reversed ? 1.0 - p : p); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            const auto label = r.removeFromBottom (14.0f);
            // The console's: a coloured cap in a white collar, a dot scale.
            LargeFormat::knob (g, r, (float) valueToProportionOfLength (getValue()), cap, isEnabled(), isMouseOverOrDragging() ? 1.0f : 0.0f, 11, fixedSize);
            // (its name small in capitals under it, in the panel's print; its value while touched)
            const bool touched = isMouseOverOrDragging();
            g.setColour (touched ? Colours::legend() : Theme::get().legendDim);
            g.setFont (juce::FontOptions (touched ? 10.0f : 9.0f, juce::Font::bold).withKerningFactor (touched ? 0.0f : 0.03f));
            g.drawFittedText (touched ? getTextFromValue (getValue()) : legend.toUpperCase(), label.toNearestInt().reduced (1, 0), juce::Justification::centred, 1, 0.8f);
        }
    private:
        const juce::Colour cap;
        const juce::String legend;
    };

    // A lit square key (IN, 24 dB...).
    inline void styleKey (juce::TextButton& b)
    {
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffc9cbce));        // grey square key
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xfffff0c8));      // lit cream
        b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff1d1e20));
        b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xffc32a24));        // red legend while lit
    }

    // A column of gain-reduction LEDs (0 to -20 dB), read from an atomic.
    class GrMeter : public juce::Component, private juce::Timer
    {
    public:
        // `green`: all green (an expander's activity), not yellow and red.
        explicit GrMeter (std::atomic<float>& source, bool green = false) : reduction (source), allGreen (green) { startTimerHz (30); }

        void paint (juce::Graphics& g) override
        {
            const float steps[] = { 20.0f, 14.0f, 10.0f, 6.0f, 3.0f };
            // Five round LEDs down the column: 3, 6, 10 (yellow), 14, 20 dB (red).
            const float pitch = (float) getHeight() / 5.0f, size = juce::jmin ((float) getWidth() - 4.0f, pitch * 0.8f), x = (float) getWidth() * 0.5f, top = 0.0f;
            for (int i = 0; i < 5; ++i)
            {
                const bool on = -shown >= steps[i];
                auto colour = allGreen ? Colours::lit() : (i < 2 ? Colours::red() : Colours::amber());
                if (liveShown == 0)   // (the section out: a preview, dimmed)
                    colour = colour.withMultipliedBrightness (0.5f).withMultipliedSaturation (0.55f);
                LargeFormat::led (g, { x, top + ((float) i + 0.5f) * pitch }, on, colour, size);
            }
        }
    private:
        void timerCallback() override
        {
            const float now = reduction.load();
            shown = now < shown ? now : shown + (now - shown) * 0.25f;
            // Drawn again only when an LED changes (it sits on the rack, which repaints under it).
            const int lit = (int) std::count_if (std::begin (steps), std::end (steps), [this] (float s) { return -shown >= s; });
            const int isLive = ! live || live() ? 1 : 0;
            const bool litChanged = std::exchange (litShown, lit) != lit, liveChanged = std::exchange (liveShown, isLive) != isLive;
            if ((litChanged || liveChanged) && isShowing())
                repaint();
        }

        static constexpr float steps[] = { 20.0f, 14.0f, 10.0f, 6.0f, 3.0f };
        int litShown = 0, liveShown = 1;
    public:
        std::function<bool()> live;   // whether its section is in (out: its LEDs a dimmed preview)
    private:

        std::atomic<float>& reduction;
        const bool allGreen;
        float shown = 0.0f;
    };

    // A VU-style needle for gain reduction (0 at the right, -20 dB at the left).
    class VuMeter : public juce::Component, private juce::Timer
    {
    public:
        explicit VuMeter (std::atomic<float>& source) : reduction (source) { startTimerHz (60); }

        // A backlit moving-coil meter, as on the classic bus compressors: a black bezel, a warm
        // cream face lit from behind by two lamps, the scale on an arc (red past 10 dB), the needle
        // swinging from a pivot hidden under the bottom cover, glass over it all, and a peak lamp
        // that lights while the reduction passes 10 dB.
        void paint (juce::Graphics& g) override
        {
            const auto outer = getLocalBounds().toFloat().reduced (1.0f);

            // Bezel: moulded black, lit from above.
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2e31), outer.getTopLeft(), juce::Colour (0xff0b0c0d), outer.getBottomLeft(), false));
            g.fillRoundedRectangle (outer, 5.0f);
            g.setColour (juce::Colours::black);
            g.drawRoundedRectangle (outer, 5.0f, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawHorizontalLine ((int) outer.getY() + 1, outer.getX() + 5.0f, outer.getRight() - 5.0f);

            const auto face = outer.reduced (5.0f, 4.0f);
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path window;
            window.addRoundedRectangle (face, 3.0f);
            g.reduceClipRegion (window);

            // The face, warm cream, brighter where the lamps behind it sit (the two lower corners).
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff7ecc9), face.getCentreX(), face.getBottom(),
                                                     juce::Colour (0xffd9c79a), face.getCentreX(), face.getY() - face.getHeight() * 0.3f, true));
            g.fillRect (face);
            for (auto lampX : { face.getX() + face.getWidth() * 0.18f, face.getRight() - face.getWidth() * 0.18f })
            {
                const juce::Point<float> c (lampX, face.getBottom() - 2.0f);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff3c2).withAlpha (0.85f), c, juce::Colour (0xfffff3c2).withAlpha (0.0f),
                                                         c.translated (face.getWidth() * 0.35f, 0.0f), true));
                g.fillRect (face);
            }

            // The scale: an arc from 20 dB (left) to 0 (right), red past 10 dB, sized by the face's
            // width (a wide, shallow arc, as a real meter's), its top just under the face's top.
            const float radius = face.getWidth() * 0.95f;
            const auto pivot = juce::Point<float> (face.getCentreX(), face.getY() + radius * 0.98f + 5.0f);
            const float sweep = std::asin (juce::jmin (0.95f, face.getWidth() * 0.4f / radius));
            auto angleFor = [sweep] (float db) { return juce::jmap (juce::jlimit (-20.0f, 0.0f, db), -20.0f, 0.0f, -sweep, sweep); };
            auto at = [&pivot] (float a, float rr) { return pivot + juce::Point<float> (std::sin (a), -std::cos (a)) * rr; };
            const auto ink = juce::Colour (0xff1c1a16), red = juce::Colour (0xffc0281e);

            juce::Path arc, redArc;
            arc.addCentredArc (pivot.x, pivot.y, radius * 0.86f, radius * 0.86f, 0.0f, angleFor (-10.0f), sweep, true);
            redArc.addCentredArc (pivot.x, pivot.y, radius * 0.86f, radius * 0.86f, 0.0f, -sweep, angleFor (-10.0f), true);
            g.setColour (ink);
            g.strokePath (arc, juce::PathStrokeType (1.2f));
            g.setColour (red);
            g.strokePath (redArc, juce::PathStrokeType (2.4f));

            g.setFont (juce::FontOptions (Console::Type::tag, juce::Font::bold));
            for (float db : { 0.0f, -1.0f, -2.0f, -3.0f, -4.0f, -6.0f, -8.0f, -10.0f, -14.0f, -20.0f })
            {
                const float a = angleFor (db);
                const bool heavy = db <= -10.0f, major = db == 0.0f || db == -3.0f || db == -6.0f || db == -10.0f || db == -20.0f;
                const auto p1 = at (a, radius * 0.86f), p2 = at (a, radius * (major ? 0.96f : 0.92f));
                g.setColour (heavy ? red : ink);
                g.drawLine (p1.x, p1.y, p2.x, p2.y, major ? 1.4f : 1.0f);
                if (major)
                    g.drawText (juce::String ((int) -db), juce::Rectangle<float> (18.0f, 10.0f).withCentre (at (a, radius * 0.76f)), juce::Justification::centred, false);
            }
            g.setColour (ink.withAlpha (0.75f));
            g.drawText ("GR", juce::Rectangle<float> (24.0f, 10.0f).withCentre (at (0.0f, radius * 0.6f)), juce::Justification::centred, false);

            // The needle, black with a fine tip, from the hidden pivot.
            const float a = angleFor (shown);
            const auto tip = at (a, radius * 0.95f);
            g.setColour (juce::Colours::black.withAlpha (0.18f));   // its shadow on the face
            g.drawLine (pivot.x + 1.5f, pivot.y, tip.x + 1.5f, tip.y + 1.0f, 1.6f);
            g.setColour (juce::Colour (0xff141414));
            g.drawLine (pivot.x, pivot.y, tip.x, tip.y, 1.5f);

            // The pivot cover: a dark band across the bottom of the window.
            const auto cover = face.withTop (face.getBottom() - juce::jmin (9.0f, face.getHeight() * 0.18f));
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2b2d), cover.getTopLeft(), juce::Colour (0xff101112), cover.getBottomLeft(), false));
            g.fillRect (cover);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawHorizontalLine ((int) cover.getY(), cover.getX(), cover.getRight());

            // Glass: a soft glare across the top.
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.28f), face.getTopLeft(), juce::Colours::white.withAlpha (0.0f),
                                                     face.getTopLeft().translated (0.0f, face.getHeight() * 0.55f), false));
            g.fillRect (face.withHeight (face.getHeight() * 0.55f));

            // The peak lamp, top right: lit while the reduction passes 10 dB.
            const auto lamp = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ face.getRight() - 7.0f, face.getY() + 7.0f });
            const bool lit = shown <= -10.0f;
            g.setColour (lit ? juce::Colour (0xffff3b2e) : juce::Colour (0xff4a1612));
            g.fillEllipse (lamp);
            if (lit)
            {
                g.setColour (juce::Colour (0xffff3b2e).withAlpha (0.3f));
                g.fillEllipse (lamp.expanded (4.0f));
            }
        }
    private:
        void timerCallback() override
        {
            const float now = reduction.load();
            // A needle's ballistics: about 300 ms either way.
            shown += (now - shown) * 0.09f;
            // Drawn again only when the needle visibly moves (it sits on the rack, which repaints under it).
            if (std::abs (shown - drawn) > 0.05f && isShowing())
            {
                drawn = shown;
                repaint();
            }
        }
        float drawn = 0.0f;

        std::atomic<float>& reduction;
        float shown = 0.0f;
    };

    // Shared by the pages: knobs and keys tied to the VoiceChain's parameters, drawn on the panel.
    class Page : public juce::Component
    {
    public:
        explicit Page (std::shared_ptr<VoiceChain> c) : chain (std::move (c)) {}

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Colours::panel());
            for (auto& s : sections)
            {
                // Each section a slightly darker plate in the same finish, its edge engraved (dark
                // line, light line under it).
                g.setColour (Colours::section());
                g.fillRoundedRectangle (s.area.toFloat(), 5.0f);
                g.setColour (juce::Colours::white.withAlpha (0.10f));
                g.drawRoundedRectangle (s.area.toFloat().translated (0.0f, 1.0f).reduced (0.5f), 5.0f, 1.0f);
                g.setColour (Colours::rim());
                g.drawRoundedRectangle (s.area.toFloat().reduced (0.5f), 5.0f, 1.0f);
                g.setColour (Colours::legend());
                g.setFont (juce::FontOptions (Console::Type::body, juce::Font::bold).withKerningFactor (0.08f));
                g.drawText (s.title, s.area.withHeight (24).reduced (10, 0), juce::Justification::centredLeft, false);
            }
        }

    protected:
        Knob& knob (const char* id, juce::Colour cap, const juce::String& legend)
        {
            auto* k = knobs.add (new Knob (cap, legend));
            sliderLinks.add (new juce::AudioProcessorValueTreeState::SliderAttachment (chain->params, id, *k));
            addAndMakeVisible (k);
            return *k;
        }

        juce::TextButton& key (const char* id, const juce::String& text)
        {
            auto* b = keys.add (new juce::TextButton (text));
            styleKey (*b);
            buttonLinks.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (chain->params, id, *b));
            addAndMakeVisible (b);
            return *b;
        }

        struct Section { juce::String title; juce::Rectangle<int> area; };
        std::vector<Section> sections;
        std::shared_ptr<VoiceChain> chain;
        juce::OwnedArray<Knob> knobs;   // before the attachments, which go first
        juce::OwnedArray<juce::TextButton> keys;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderLinks;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonLinks;
    };

    // One built-in rack unit (1U): a stage of the channel's VoiceChain (AudioEngine::RackBuiltin:
    // 0 FILTERS, 1 GATE, 2 COMPRESSOR, 3 EQ, 4 BUS COMP, 5 INPUT, 6 NOISE REDUCTION, 7 DE-ESSER,
    // 8 AUTO-LEVEL, 9 VOICE FX, 10 LIMITER). Its keys in a column, its knobs in a
    // row, and its reduction LEDs or needle at the right. The rack draws the faceplate and the
    // unit's name; this draws nothing of its own.
    class StageUnit : public Page
    {
    public:
        enum { robotUnit = 100, echoUnit, radioUnit, pitchUnit };   // VOICE FX's effects on their own (not rack units)

        StageUnit (std::shared_ptr<VoiceChain> c, int builtin) : Page (std::move (c))
        {
            using namespace Colours;
            switch (builtin)
            {
                case 0:
                    key ("hpfOn", "HPF");  key ("hpfSteep", "24 dB");  key ("lpfOn", "LPF");
                    knob ("hpf", grey(), "HPF");  knob ("lpf", grey(), "LPF");
                    break;
                case 1:
                    key ("gateOn", "IN");
                    knob ("gateThreshold", green(), "THRESH");  knob ("gateRange", green(), "RANGE");
                    knob ("gateRelease", green(), "RELEASE");   knob ("gateHold", green(), "HOLD");
                    meter = std::make_unique<GrMeter> (gateClosed);
                    gateTimer.callback = [this] { gateClosed = (chain->gateOpen.load() - 1.0f) * 20.0f; };   // closed: the LEDs fill
                    gateTimer.startTimerHz (30);
                    break;
                case 2:
                    key ("compOn", "IN");
                    knob ("compRatio", black(), "RATIO");     knob ("compThreshold", black(), "THRESH");
                    knob ("compRelease", black(), "RELEASE"); knob ("compAttack", black(), "ATTACK");
                    knob ("makeup", black(), "MAKEUP");
                    meter = std::make_unique<GrMeter> (chain->compReduction);
                    break;
                case 3:
                    key ("eqOn", "IN");
                    knob ("highGain", red(), "HF GAIN");    knob ("highFreq", red(), "HF FREQ");
                    knob ("mid2Gain", green(), "HMF GAIN"); knob ("mid2Freq", green(), "HMF FREQ"); knob ("mid2Q", green(), "HMF Q");
                    knob ("midGain", blue(), "LMF GAIN");   knob ("midFreq", blue(), "LMF FREQ");   knob ("midQ", blue(), "LMF Q");
                    knob ("lowGain", brown(), "LF GAIN");   knob ("lowFreq", brown(), "LF FREQ");
                    break;
                case 4:
                    key ("busOn", "IN");
                    vu = std::make_unique<VuMeter> (chain->busReduction);
                    knob ("busThreshold", black(), "THRESHOLD");  knob ("busRatio", grey(), "RATIO");
                    knob ("busMakeup", black(), "MAKE-UP");       knob ("busAttack", red(), "ATTACK");
                    knob ("busRelease", blue(), "RELEASE");       knob ("busMix", grey(), "MIX");
                    break;
                case 5:
                    key ("polarity", juce::String::fromUTF8 ("\xc3\x98"));
                    knob ("trim", grey(), "TRIM");  knob ("inputDelay", grey(), "DELAY");
                    break;
                case 6:
                    key ("nrOn", "IN");
                    knob ("nrDepth", green(), "DEPTH");  knob ("nrType", grey(), "TYPE");
                    break;
                case 7:
                    key ("deessOn", "IN");
                    knob ("deessThreshold", red(), "THRESH");  knob ("deessFreq", red(), "FREQ");
                    meter = std::make_unique<GrMeter> (chain->deessReduction);
                    break;
                case 8:
                    key ("levelOn", "IN");
                    knob ("levelTarget", blue(), "TARGET");  knob ("levelRange", blue(), "RANGE");
                    break;
                case 9:
                    key ("robotOn", "ROBOT");  key ("echoOn", "ECHO");  key ("radioOn", "RADIO");  key ("pitchOn", "PITCH");
                    knob ("robotFreq", grey(), "ROBOT");   knob ("echoTime", blue(), "TIME");  knob ("echoFeedback", blue(), "FEEDBACK");
                    knob ("echoMix", blue(), "MIX");       knob ("radioDrive", red(), "DRIVE");  knob ("pitch", green(), "PITCH");
                    break;
                // VOICE FX's effects one at a time (the CHANNEL tab's VOICE FX page, a panel each).
                case robotUnit: key ("robotOn", "IN"); knob ("robotFreq", grey(), "FREQ"); break;
                case echoUnit:
                    key ("echoOn", "IN");
                    knob ("echoTime", blue(), "TIME");  knob ("echoFeedback", blue(), "FEEDBACK");  knob ("echoMix", blue(), "MIX");
                    break;
                case radioUnit: key ("radioOn", "IN"); knob ("radioDrive", red(), "DRIVE"); break;
                case pitchUnit: key ("pitchOn", "IN"); knob ("pitch", green(), "PITCH"); break;
                case 10:
                    key ("limitOn", "IN");
                    knob ("ceiling", black(), "CEILING");  knob ("limitRelease", black(), "RELEASE");
                    meter = std::make_unique<GrMeter> (chain->limitReduction);
                    break;
                default: break;
            }
            if (meter != nullptr) addAndMakeVisible (*meter);
            if (vu != nullptr)    addAndMakeVisible (*vu);
        }

        void paint (juce::Graphics&) override {}   // (the rack's faceplate shows through)

        // On a page of its own (the CHANNEL tab's) rather than in 1U: bigger knobs, and the width
        // it wants laid out like that.
        void setKnobWidth (int w) { knobWidth = w; resized(); }
        int widthWanted() const
        {
            const int gap = Console::Space::gap;
            return ((keys.size() + 1) / 2) * (64 + gap) + (vu != nullptr ? 150 + gap : 0) + (meter != nullptr ? 30 + gap : 0)
                 + knobs.size() * knobWidth;
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (0, 4);
            // Keys: two to a column, each a key high, centred in the unit's height.
            const int keyH = Console::Space::keySmall, keyW = 64;
            for (int i = 0; i < keys.size(); i += 2)
            {
                auto column = r.removeFromLeft (keyW).withSizeKeepingCentre (keyW, i + 1 < keys.size() ? 2 * keyH + Console::Space::unit : keyH);
                keys[i]->setBounds (column.removeFromTop (keyH));
                if (i + 1 < keys.size())
                {
                    column.removeFromTop (Console::Space::unit);
                    keys[i + 1]->setBounds (column.removeFromTop (keyH));
                }
                r.removeFromLeft (Console::Space::gap);
            }
            if (meter != nullptr)
            {
                meter->setBounds (r.removeFromRight (30));
                r.removeFromRight (Console::Space::gap);
            }
            if (vu != nullptr)
            {
                vu->setBounds (r.removeFromLeft (juce::jmin (150, r.getHeight() * 5 / 2)).reduced (0, 1));
                r.removeFromLeft (Console::Space::gap);
            }
            if (knobs.isEmpty())
                return;
            const int w = juce::jmin (knobWidth, r.getWidth() / knobs.size());
            for (auto* k : knobs)
                k->setBounds (r.removeFromLeft (w));
        }

    private:
        struct Tick : juce::Timer { std::function<void()> callback; void timerCallback() override { callback(); } };
        std::atomic<float> gateClosed { 0.0f };
        int knobWidth = 64;
        std::unique_ptr<GrMeter> meter;
        std::unique_ptr<VuMeter> vu;
        Tick gateTimer;
    };
}

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/VoiceChain.h"
#include "Console.h"
#include "EqCurve.h"

// The modern theme's dynamics graphs: the gate's and the compressor's transfer curves (level in
// across, level out up), as on a touchscreen console, with handles to drag, on the
// selected-channel page (DynGraph).
namespace StageGraphs
{
    const juce::Colour gateColour { 0xffe8853a }, compColour { 0xff3a8fe0 };

    inline float value (VoiceChain& c, const char* id) { return c.params.getRawParameterValue (id)->load(); }
    inline bool on (VoiceChain& c, const char* id)     { return value (c, id) > 0.5f; }

    // The gate: level out for a level in (dB); below the threshold, turned down by its RANGE
    // (all the way: silent, -INF).
    inline float gateOut (VoiceChain& c, float in)
    {
        const float threshold = value (c, "gateThreshold"), range = value (c, "gateRange");
        if (in >= threshold)
            return in;
        return range <= VoiceChain::gateSilent ? -std::numeric_limits<float>::infinity() : in + range;
    }

    // The compressor: over the threshold the level rises 1/RATIO as fast (not at all at INF:1),
    // eased in across the KNEE; then the makeup. As VoiceChain::processBlock works it out.
    inline float compOut (VoiceChain& c, float in)
    {
        const float threshold = value (c, "compThreshold"), ratio = juce::jmax (1.0f, value (c, "compRatio")), knee = value (c, "compKnee");
        const float slope = ratio >= VoiceChain::compRatioInf ? -1.0f : 1.0f / ratio - 1.0f;
        const float over = in - threshold;
        float reduction = 0.0f;
        if (over >= knee * 0.5f)      reduction = slope * over;
        else if (over > -knee * 0.5f) reduction = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
        return in + reduction + value (c, "makeup");
    }

    // The bottom of each graph's scale (in and out): the gate's threshold goes down to -80 dB.
    inline float floorOf (bool isGate) { return isGate ? -80.0f : -60.0f; }

    // The transfer curve in `area` (floor to 0 dB both ways): filled below, the line on top; a
    // faint diagonal for "unchanged". `alpha`: dimmed when the stage is out.
    inline void drawTransfer (juce::Graphics& g, juce::Rectangle<float> area, VoiceChain& c, bool isGate, float alpha, float line)
    {
        const float floor = floorOf (isGate);
        const auto colour = isGate ? gateColour : compColour;
        auto xOf = [&] (float db) { return area.getX() + area.getWidth() * (db - floor) / -floor; };
        auto yOf = [&] (float db) { return area.getBottom() - area.getHeight() * (juce::jmax (db, floor - 1.0f) - floor) / -floor; };

        g.setColour (Console::Colours::screenInk().withAlpha (0.25f));
        g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getY(), 1.0f);

        juce::Path curve;
        const int steps = juce::jmax (16, (int) area.getWidth());
        for (int i = 0; i <= steps; ++i)
        {
            float in = floor - floor * (float) i / (float) steps;
            if (isGate && i > 0)   // the step at the threshold: straight up
                if (const float t = value (c, "gateThreshold"); in >= t && floor - floor * (float) (i - 1) / (float) steps < t)
                {
                    curve.lineTo (xOf (t), yOf (gateOut (c, t - 0.001f)));
                    curve.lineTo (xOf (t), yOf (t));
                }
            const float out = isGate ? gateOut (c, in) : compOut (c, in);
            const juce::Point<float> p (xOf (in), yOf (out));
            if (i == 0) curve.startNewSubPath (p); else curve.lineTo (p);
        }

        juce::Graphics::ScopedSaveState keep (g);
        g.reduceClipRegion (area.toNearestInt());
        juce::Path fill (curve);
        fill.lineTo (area.getRight(), area.getBottom());
        fill.lineTo (area.getX(), area.getBottom());
        fill.closeSubPath();
        g.setColour (colour.withAlpha (0.35f * alpha));
        g.fillPath (fill);
        g.setColour (colour.withAlpha (alpha));
        g.strokePath (curve, juce::PathStrokeType (line, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }
}

// A large gate or compressor graph (the selected-channel page): scales printed in and out, and
// handles to drag. Gate: T the threshold (across), R its range (up and down). Compressor: T the
// threshold (across), R the ratio (up and down at 0 dB in), G the makeup (up and down at the
// left). Double-click a handle: back to its default. Beside it, the stage at work: the gate OPEN
// or CLOSED, the compressor's gain reduction.
class DynGraph : public juce::Component,
                 private juce::Timer
{
public:
    DynGraph (std::shared_ptr<VoiceChain> c, bool isGate) : chain (std::move (c)), gate (isGate) { startTimerHz (20); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto meter = r.removeFromRight (14.0f);
        r.removeFromRight (4.0f);
        g.setColour (DotMatrix::glass());
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (DotMatrix::bezel().withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        const auto area = graphArea();
        const float floor = StageGraphs::floorOf (gate);

        // Grid every 20 dB, numbered along the bottom (in) and up the left (out).
        g.setFont (juce::FontOptions (Console::Type::tag));
        for (float db = floor; db <= 0.0f; db += 20.0f)
        {
            const float x = xOf (db), y = yOf (db);
            g.setColour (Console::Colours::screenInk().withAlpha (0.15f));
            g.fillRect (x, area.getY(), 1.0f, area.getHeight());
            g.fillRect (area.getX(), y, area.getWidth(), 1.0f);
            g.setColour (Console::Colours::screenInk());
            const auto text = juce::String ((int) std::abs (db));
            g.drawText (text, juce::Rectangle<float> (x - 12.0f, area.getBottom() + 2.0f, 24.0f, 11.0f), juce::Justification::centred, false);
            g.drawText (text, juce::Rectangle<float> (r.getX() + 1.0f, y - 6.0f, area.getX() - r.getX() - 4.0f, 12.0f), juce::Justification::centredRight, false);
        }

        auto& c = *chain;
        const bool in = StageGraphs::on (c, gate ? "gateOn" : "compOn");
        StageGraphs::drawTransfer (g, area, c, gate, in ? 1.0f : 0.4f, 2.0f);

        for (int h = 0; h < handleCount(); ++h)
        {
            const auto at = handleAt (h);
            if (! area.expanded (2.0f).contains (at))
                continue;
            const float size = h == dragging || h == hovered ? 20.0f : 16.0f;
            const auto circle = juce::Rectangle<float> (size, size).withCentre (at);
            g.setColour (juce::Colour (0xff6a6e75).withMultipliedAlpha (in ? 1.0f : 0.6f));
            g.fillEllipse (circle);
            g.setColour (juce::Colours::white.withAlpha (in ? 0.9f : 0.5f));
            g.drawEllipse (circle, 1.2f);
            g.setFont (juce::FontOptions (Console::Type::legend, juce::Font::bold));
            g.drawText (juce::String::charToString (handleLetter (h)), circle, juce::Justification::centred, false);
        }

        // The stage at work: the gate's lamp (green open, red closed), or the compressor's
        // reduction coming down from the top (0 to -24 dB).
        g.setColour (DotMatrix::glass());
        g.fillRoundedRectangle (meter, 2.0f);
        if (gate)
        {
            const bool open = c.gateOpen.load() > 0.5f;
            g.setColour ((open ? Console::Colours::ledGreen() : Console::Colours::ledRed()).withAlpha (in ? 1.0f : 0.25f));
            g.fillRoundedRectangle (meter.reduced (3.0f).withHeight (meter.getWidth() - 6.0f), 2.0f);
        }
        else
        {
            const float gr = juce::jlimit (0.0f, 1.0f, -c.compReduction.load() / 24.0f);
            const auto bar = meter.reduced (3.0f);
            g.setColour (StageGraphs::compColour.withAlpha (in ? 1.0f : 0.25f));
            g.fillRect (bar.withHeight (bar.getHeight() * gr));
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int near = handleNear (e.position);
        if (std::exchange (hovered, near) != near)
            repaint();
        setMouseCursor (near >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseExit (const juce::MouseEvent&) override { if (std::exchange (hovered, -1) != -1) repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = handleNear (e.position);
        if (auto* p = paramOf (dragging))
            p->beginChangeGesture();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0)
            return;
        const auto area = graphArea();
        const float floor = StageGraphs::floorOf (gate);
        const float inDb = floor - floor * juce::jlimit (0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth());
        const float outDb = floor - floor * juce::jlimit (0.0f, 1.0f, (area.getBottom() - e.position.y) / area.getHeight());
        auto& c = *chain;
        const char* id = idOf (dragging);
        if (gate)
            set (id, dragging == 0 ? inDb : outDb - StageGraphs::value (c, "gateThreshold"));
        else if (dragging == 0)
            set (id, inDb);
        else if (dragging == 1)   // the ratio from where 0 dB in comes out
        {
            const float threshold = StageGraphs::value (c, "compThreshold");
            const float rise = outDb - StageGraphs::value (c, "makeup") - threshold;
            set (id, rise <= 0.01f ? 10.0f : -threshold / rise);
        }
        else
            set (id, outDb - floor);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (auto* p = paramOf (dragging))
            p->endChangeGesture();
        dragging = -1;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (auto* p = paramOf (handleNear (e.position)))
            p->setValueNotifyingHost (p->getDefaultValue());
    }

private:
    juce::Rectangle<float> graphArea() const
    {
        auto r = getLocalBounds().toFloat().withTrimmedRight (18.0f);
        return r.withTrimmedLeft (24.0f).withTrimmedBottom (15.0f).withTrimmedTop (8.0f).withTrimmedRight (8.0f);
    }

    float xOf (float db) const { const auto a = graphArea(); const float f = StageGraphs::floorOf (gate); return a.getX() + a.getWidth() * (db - f) / -f; }
    float yOf (float db) const { const auto a = graphArea(); const float f = StageGraphs::floorOf (gate); return a.getBottom() - a.getHeight() * (juce::jmax (db, f) - f) / -f; }

    int handleCount() const { return gate ? 2 : 3; }
    juce::juce_wchar handleLetter (int h) const { return h == 0 ? 'T' : h == 1 ? 'R' : 'G'; }
    const char* idOf (int h) const
    {
        if (gate) return h == 0 ? "gateThreshold" : "gateRange";
        return h == 0 ? "compThreshold" : h == 1 ? "compRatio" : "makeup";
    }

    juce::Point<float> handleAt (int h) const
    {
        auto& c = *chain;
        const float floor = StageGraphs::floorOf (gate);
        if (gate)
        {
            const float t = StageGraphs::value (c, "gateThreshold");
            return h == 0 ? juce::Point<float> (xOf (t), yOf (t))
                          : juce::Point<float> (xOf (t), yOf (juce::jmax (floor, t + StageGraphs::value (c, "gateRange"))));
        }
        const float t = StageGraphs::value (c, "compThreshold");
        if (h == 0) return { xOf (t), yOf (StageGraphs::compOut (c, t)) };
        if (h == 1) return { xOf (0.0f), yOf (StageGraphs::compOut (c, 0.0f)) };
        return { xOf (floor), yOf (StageGraphs::compOut (c, floor)) };
    }

    int handleNear (juce::Point<float> p) const
    {
        int best = -1;
        float bestDistance = 14.0f;
        for (int h = 0; h < handleCount(); ++h)
            if (const float d = handleAt (h).getDistanceFrom (p); d < bestDistance)
            {
                best = h;
                bestDistance = d;
            }
        return best;
    }

    juce::RangedAudioParameter* paramOf (int h) const { return h < 0 ? nullptr : chain->params.getParameter (idOf (h)); }
    void set (const char* id, float v) { if (auto* p = chain->params.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); }

    // Redrawn when a setting moves or the stage's work changes. Not while it can't be seen (the
    // RACK tab, or the Mixer hidden): it catches up on its first tick back.
    void timerCallback() override
    {
        if (! isShowing())
            return;
        auto& c = *chain;
        const std::array<float, 9> now { StageGraphs::value (c, "gateThreshold"), StageGraphs::value (c, "gateRange"), StageGraphs::value (c, "compThreshold"),
                                         StageGraphs::value (c, "compRatio"), StageGraphs::value (c, "makeup"), StageGraphs::value (c, gate ? "gateOn" : "compOn"),
                                         c.gateOpen.load(), std::round (c.compReduction.load() * 4.0f), StageGraphs::value (c, "compKnee") };
        if (now != shown)
        {
            shown = now;
            repaint();
        }
    }

    std::shared_ptr<VoiceChain> chain;
    const bool gate;
    int dragging = -1, hovered = -1;
    std::array<float, 9> shown {};
};


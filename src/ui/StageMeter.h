#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/VoiceChain.h"
#include "Console.h"

// A live meter for one stage of the channel chain (VoiceChain), on the CHANNEL page: thin bars
// across, -60 to 0 dB, each with a peak line that holds a second. What it shows depends on the
// stage:
//   level      one bar at a point (INPUT: after the trim); a gate's shows its threshold, and its
//              bar is lit while the gate's open, dim while it's closed;
//   inOut      the stage's input and output, one bar each, so what it adds or takes is plain;
//   reduction  the output and, under it, how far the stage is turning down (0 to -24 dB, from
//              the left, amber);
//   gain       auto-level's gain, from the middle: up to the right, down to the left (+-24 dB).
class StageMeter : public juce::Component,
                   public juce::SettableTooltipClient,
                   private juce::Timer
{
public:
    enum Kind { level, inOut, reduction, gain };

    // `point`: the level shown (level), or the stage's output (inOut: its input is the point
    // before). `amount`: the dB a reduction or gain meter reads (the chain's own atomics).
    StageMeter (std::shared_ptr<VoiceChain> c, Kind k, VoiceChain::Point p, const std::atomic<float>* amountDb = nullptr, bool isGate = false)
        : chain (std::move (c)), kind (k), point (p), amount (amountDb), gate (isGate)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    // Height for its rows (the panel lays it out at this).
    int wanted() const { return rows() * rowHeight + (rows() - 1) * rowGap; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto row = [&r] { auto line = r.removeFromTop ((float) rowHeight); r.removeFromTop ((float) rowGap); return line; };
        switch (kind)
        {
            case level:     drawLevel (g, row(), gate ? "" : "IN", shown[0], held[0]); break;
            case inOut:     drawLevel (g, row(), "IN", shown[0], held[0]); drawLevel (g, row(), "OUT", shown[1], held[1]); break;
            case reduction: drawLevel (g, row(), "OUT", shown[1], held[1]); drawReduction (g, row()); break;
            case gain:      drawGain (g, row()); break;
        }
    }

private:
    static constexpr int rowHeight = 9, rowGap = 3, labelWidth = 24;
    static constexpr float floorDb = -60.0f;

    int rows() const { return kind == inOut || kind == reduction ? 2 : 1; }

    static float toDb (float gain) { return juce::Decibels::gainToDecibels (gain, floorDb); }
    float value (const char* id) const { return chain->params.getRawParameterValue (id)->load(); }

    juce::Rectangle<float> bar (juce::Rectangle<float> line, const juce::String& label, juce::Graphics& g) const
    {
        g.setColour (Console::Colours::legendDim());
        g.setFont (Console::legendFont (Console::Type::tag));
        g.drawText (label, line.removeFromLeft ((float) labelWidth), juce::Justification::centredLeft, false);
        g.setColour (Console::Colours::slot());
        g.fillRoundedRectangle (line, 2.0f);
        return line.reduced (1.0f);
    }

    float xOf (juce::Rectangle<float> b, float db) const
    {
        return b.getX() + b.getWidth() * juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb);
    }

    // A level bar: green, then yellow from -12 dB and red from -3 dB, as the desk's meters; a
    // gate's is dim while it's closed, with its threshold (and, with hysteresis, where it closes).
    void drawLevel (juce::Graphics& g, juce::Rectangle<float> line, const juce::String& label, float db, float heldDb) const
    {
        const auto b = bar (line, label, g);
        const bool closed = gate && chain->gateOpen.load() < 0.5f;
        const float to = xOf (b, db);
        const std::pair<float, juce::Colour> zones[] { { -12.0f, Console::Colours::ledGreen() }, { -3.0f, Console::Colours::ledYellow() },
                                                       { 0.0f, Console::Colours::ledRed() } };
        float from = b.getX();
        for (auto& [top, colour] : zones)
        {
            const float end = juce::jmin (to, xOf (b, top));
            if (end > from)
            {
                g.setColour (closed ? Console::Colours::legendDim().withAlpha (0.6f) : colour);
                g.fillRect (juce::Rectangle<float> (from, b.getY(), end - from, b.getHeight()));
            }
            from = juce::jmax (from, xOf (b, top));
        }
        if (heldDb > floorDb)
        {
            g.setColour (Console::Colours::legend());
            g.fillRect (xOf (b, heldDb) - 1.0f, b.getY(), 1.5f, b.getHeight());
        }
        if (gate)
        {
            const float threshold = value ("gateThreshold"), closesAt = threshold - value ("gateHysteresis");
            g.setColour (Console::Colours::ledRed());
            g.fillRect (xOf (b, threshold) - 0.5f, b.getY() - 1.0f, 2.0f, b.getHeight() + 2.0f);
            if (closesAt < threshold)
            {
                g.setColour (Console::Colours::ledRed().withAlpha (0.5f));
                g.fillRect (xOf (b, closesAt) - 0.5f, b.getY(), 1.0f, b.getHeight());
            }
        }
    }

    // How far it turns down: amber from the left, 0 to -24 dB.
    void drawReduction (juce::Graphics& g, juce::Rectangle<float> line) const
    {
        const auto b = bar (line, "GR", g);
        const float width = b.getWidth() * juce::jlimit (0.0f, 1.0f, -shownAmount / 24.0f);
        g.setColour (Console::monoLight());
        g.fillRect (b.withWidth (width));
        if (heldAmount < -0.1f)
        {
            g.setColour (Console::Colours::legend());
            g.fillRect (b.getX() + b.getWidth() * juce::jlimit (0.0f, 1.0f, -heldAmount / 24.0f) - 1.0f, b.getY(), 1.5f, b.getHeight());
        }
    }

    // Auto-level's gain from the middle: up (right, green) or down (left, amber), +-24 dB.
    void drawGain (juce::Graphics& g, juce::Rectangle<float> line) const
    {
        const auto b = bar (line, "GAIN", g);
        const float mid = b.getCentreX(), x = mid + b.getWidth() * 0.5f * juce::jlimit (-1.0f, 1.0f, shownAmount / 24.0f);
        g.setColour (shownAmount >= 0.0f ? Console::Colours::ledGreen() : Console::monoLight());
        g.fillRect (juce::Rectangle<float> (juce::jmin (mid, x), b.getY(), std::abs (x - mid), b.getHeight()));
        g.setColour (Console::Colours::legendDim());
        g.fillRect (mid - 0.5f, b.getY() - 1.0f, 1.0f, b.getHeight() + 2.0f);
    }

    // Each frame: the levels (and peaks: held a second, then they fall), redrawn when they've
    // moved from what was last drawn (a slow climb, a little each tick, still shows once it adds
    // up). Not while it can't be seen (the RACK tab, or the Mixer hidden).
    void timerCallback() override
    {
        if (! isShowing())
            return;
        const float in = toDb (chain->pointLevel[(size_t) juce::jmax (0, (int) point - (kind == inOut ? 1 : 0))].load());
        const float out = toDb (chain->pointLevel[(size_t) point].load());
        const float a = amount != nullptr ? amount->load() : 0.0f;
        const std::array<float, 2> now { in, out };
        const double t = juce::Time::getMillisecondCounterHiRes();

        for (size_t i = 0; i < 2; ++i)
        {
            shown[i] = now[i];
            if (now[i] >= held[i] || t - heldAt[i] > 1000.0)
            {
                held[i] = now[i];
                heldAt[i] = t;
            }
        }
        if (a <= heldAmount || t - heldAmountAt > 1000.0)
        {
            heldAmount = a;
            heldAmountAt = t;
        }
        shownAmount = a;
        shownOpen = chain->gateOpen.load() >= 0.5f;

        bool moved = std::abs (shownAmount - drawnAmount) > 0.1f || std::abs (heldAmount - drawnHeldAmount) > 0.1f || (gate && shownOpen != drawnOpen);
        for (size_t i = 0; i < 2; ++i)
            moved = moved || std::abs (shown[i] - drawn[i]) > 0.25f || std::abs (held[i] - drawnHeld[i]) > 0.25f;
        if (moved)
        {
            drawn = shown;
            drawnHeld = held;
            drawnAmount = shownAmount;
            drawnHeldAmount = heldAmount;
            drawnOpen = shownOpen;
            repaint();
        }
    }

    std::shared_ptr<VoiceChain> chain;
    const Kind kind;
    const VoiceChain::Point point;
    const std::atomic<float>* amount;
    const bool gate;

    std::array<float, 2> shown { floorDb, floorDb }, held { floorDb, floorDb };   // in, out (dB)
    std::array<double, 2> heldAt {};
    float shownAmount = 0.0f, heldAmount = 0.0f;
    double heldAmountAt = 0.0;
    bool shownOpen = true;

    // What the last repaint drew (the values above as they were then).
    std::array<float, 2> drawn { floorDb, floorDb }, drawnHeld { floorDb, floorDb };
    float drawnAmount = 0.0f, drawnHeldAmount = 0.0f;
    bool drawnOpen = true;
};

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/AudioEngine.h"
#include "Console.h"

// The top bar's health light: how the audio is doing at a glance. Green: fine. Amber: the callback
// is working hard (over 70% of its time), or a device's buffer ran short or over in the last 10 s.
// Red: a callback overran its buffer (an audible glitch) or blocks went out silent in the last
// 10 s, or the audio isn't running. Its load beside it; click it for the streams (DEVICES).
class HealthLight : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit HealthLight (AudioEngine& e) : engine (e)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        startTimerHz (4);
    }

    std::function<void()> onClick;

    void paint (juce::Graphics& g) override
    {
        using namespace Console;
        const auto colour = state == 2 ? Colours::danger() : state == 1 ? Colours::caution() : Colours::ledGreen();
        auto r = getLocalBounds().toFloat();
        const float radius = 4.5f;
        // A red light breathes, so it's noticed.
        const float level = state == 2 ? 0.6f + 0.4f * (float) std::sin (juce::Time::getMillisecondCounterHiRes() * 0.006) : 1.0f;
        drawLED (g, { r.getX() + radius + 3.0f, r.getCentreY() }, radius, colour, level);
        g.setColour (Colours::legend().withAlpha (isMouseOver() ? 1.0f : 0.8f));
        g.setFont (legendFont (Type::legend));
        g.drawText (text, r.withTrimmedLeft (radius * 2.0f + 9.0f), juce::Justification::centredLeft, false);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

private:
    void timerCallback() override
    {
        const auto stats = engine.getStats();
        const auto now = juce::Time::getMillisecondCounter();
        int shortOrOver = 0;
        for (auto& s : stats.streams)
            shortOrOver += s.underruns + s.overruns;
        if (stats.overloads > lastOverloads || stats.skippedBlocks > lastSkipped) badAt = now;
        if (shortOrOver > lastShortOrOver) roughAt = now;
        lastOverloads = stats.overloads;
        lastSkipped = stats.skippedBlocks;
        lastShortOrOver = shortOrOver;

        const bool running = engine.getSampleRate() > 0.0;
        const int next = ! running || (badAt != 0 && now - badAt < 10000) ? 2
                       : stats.loadPercent > 70.0f || (roughAt != 0 && now - roughAt < 10000) ? 1 : 0;
        const auto nextText = running ? juce::String (juce::roundToInt (stats.loadPercent)) + "%" : juce::String ("OFF");
        const char* tips[] { "Audio is fine", "Audio is working hard, or a device's buffer ran short or over just now",
                             "Audio glitched just now (or isn't running)" };
        if (next != state || nextText != text || next == 2)
        {
            state = next;
            text = nextText;
            setTooltip (juce::String (tips[state]) + ". Load " + juce::String (juce::roundToInt (stats.loadPercent)) + "% (peak "
                        + juce::String (juce::roundToInt (stats.peakLoadPercent)) + "%). Click for the streams.");
            repaint();
        }
    }

    AudioEngine& engine;
    int state = 0;   // 0 green, 1 amber, 2 red
    juce::String text { "--" };
    int lastOverloads = 0, lastSkipped = 0, lastShortOrOver = 0;
    juce::uint32 badAt = 0, roughAt = 0;
};

#pragma once

// Part of the console: Console.cpp includes it, and nothing else does.
// The top bar: the time display and the transport.

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../src/ui/LargeFormat.h"
#include "../../src/engine/AudioEngine.h"
#include "../../src/ui/Console.h"
#include "ConsoleBench.h"

namespace ConsoleDesk
{
    // THE TOP BAR: the time display and the transport, driving the Tape deck
    // (rewind, stop, play, wind on, record, loop).
    class TopBar : public juce::Component, private juce::Timer
    {
    public:
        explicit TopBar (AudioEngine& e) : engine (e) { startTimerHz (30); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff222326));
            g.setColour (Console::Colours::recess());
            g.fillRect (getLocalBounds().removeFromBottom (1));
            auto& tape = engine.getTape();
            const auto screen = screenArea();
            g.setColour (juce::Colour (0xff0d1110));
            g.fillRoundedRectangle (screen, 3.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawRoundedRectangle (screen, 3.0f, 1.0f);
            const auto green = Console::Colours::palette ("teal");
            auto text = screen.reduced (10.0f, 0.0f);
            g.setColour (green);
            g.setFont (LargeFormat::print (18.0f));
            g.drawText (clock (tape.isLoaded() ? tape.position() : 0.0), text.removeFromLeft (60.0f), juce::Justification::centredLeft, false);
            g.setColour (green.withAlpha (0.55f));
            g.setFont (LargeFormat::print (12.0f));
            g.drawText (clock (tape.isLoaded() ? tape.length() : 0.0), text.removeFromLeft (46.0f), juce::Justification::centredLeft, false);
            const int now = tape.current();
            g.setColour (green.withAlpha (0.85f));
            g.drawFittedText (juce::isPositiveAndBelow (now, tape.size()) ? tape.file (now).getFileNameWithoutExtension().toUpperCase() : juce::String ("NO TRACK"),
                              text.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
            for (int i = 0; i < keys().size(); ++i)
            {
                const auto& k = keys()[i];
                const auto key = keyArea (i);
                const auto lamp = k == "REC" ? Console::Colours::stopLamp() : k == "LOOP" ? Console::Colours::palette ("orange") : Console::Colours::runLamp();
                LargeFormat::key (g, key, {}, lit[i], lamp, true, held == i ? 1.5f : 0.0f, key.contains (over) ? 1.0f : 0.0f);
                const auto ink = lit[i] > 0.5f ? Console::Colours::litInk() : k == "REC" ? juce::Colour (0xffb8302a) : juce::Colour (0xff26282b);
                Bench::drawTransport (g, k, key.reduced (key.getWidth() * 0.3f, key.getHeight() * 0.28f), ink);
                if (k == "LOOP" && tape.repeat == Tape::Repeat::one)   // (one track over)
                {
                    g.setColour (ink);
                    g.setFont (LargeFormat::print (10.0f));
                    g.drawText ("1", key.withTrimmedLeft (key.getWidth() * 0.62f).withTrimmedTop (key.getHeight() * 0.5f), juce::Justification::centred, false);
                }
            }
        }

        void mouseMove (const juce::MouseEvent& e) override { over = e.position; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { over = { -1.0e6f, -1.0e6f }; repaint(); }
        void mouseUp (const juce::MouseEvent&) override { held = -1; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto& tape = engine.getTape();
            for (int i = 0; i < keys().size(); ++i)
                if (keyArea (i).contains (e.position))
                {
                    held = i;
                    const auto& k = keys()[i];
                    if (k == "REW")       tape.skip (-10.0);
                    else if (k == "STOP") tape.stop();
                    else if (k == "PLAY") tape.togglePlay();
                    else if (k == "FF")   tape.skip (10.0);
                    else if (k == "REC")  record();
                    else if (k == "LOOP") tape.repeat = tape.repeat == Tape::Repeat::all ? Tape::Repeat::one : tape.repeat == Tape::Repeat::one ? Tape::Repeat::off : Tape::Repeat::all;
                    repaint();
                }
        }

    private:
        static const juce::StringArray& keys()
        {
            static const juce::StringArray k { "REW", "STOP", "PLAY", "FF", "REC", "LOOP" };
            return k;
        }
        juce::Rectangle<float> screenArea() const { return { 8.0f, 6.0f, 300.0f, (float) getHeight() - 13.0f }; }
        juce::Rectangle<float> keyArea (int i) const { return { 318.0f + 46.0f * (float) i, 6.0f, 42.0f, (float) getHeight() - 13.0f }; }
        static juce::String clock (double seconds)
        {
            const int t = (int) seconds;
            return juce::String (t / 60).paddedLeft ('0', 2) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
        }

        // REC: the Tape page's recording (its source, format and folder); again, it stops.
        void record()
        {
            if (engine.getRecorder().isRecording())
                return engine.stopRecording();
            juce::String error;
            if (engine.startChosenRecording (error) == juce::File())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "RECORD FAILED", error);
        }

        void timerCallback() override
        {
            if (! isShowing())
                return;
            auto& tape = engine.getTape();
            const bool on[] = { false, false, tape.isPlaying(), false, engine.getRecorder().isRecording(), tape.repeat != Tape::Repeat::off };
            bool moved = false;
            for (int i = 0; i < 6; ++i)   // (the lamps easing on and off)
                if (const float target = on[i] ? 1.0f : 0.0f; lit[i] != target)
                {
                    lit[i] += (target - lit[i]) * 0.35f;
                    if (std::abs (target - lit[i]) < 0.02f)
                        lit[i] = target;
                    moved = true;
                }
            if (moved || ++ticks % 10 == 0)   // (the clock)
                repaint();
        }

        AudioEngine& engine;
        float lit[6] {};
        int ticks = 0, held = -1;
        juce::Point<float> over { -1.0e6f, -1.0e6f };
    };
}

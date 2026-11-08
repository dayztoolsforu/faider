#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Console.h"

// Notices: small cards in the window's bottom-right corner for things worth a glance (a device
// plugged in or gone, a channel clipping, a scene or macro, an undo). Each slides in from the right
// with a little overshoot, settles into the stack (newest at the bottom, the others gliding up to
// make room), stays about four seconds and fades away; click one to dismiss it. The full record
// stays in the event log. A kind can be switched off (SETTINGS > GENERAL: `enabled`).
class Toasts : public juce::Component, private juce::Timer
{
public:
    Toasts() { setInterceptsMouseClicks (false, true); }

    // Kinds: "device", "clip", "scene", "macro", "undo", "update", "tip" (a tip stays up longer, on two lines).
    std::function<bool (const juce::String& kind)> enabled;

    void show (const juce::String& kind, const juce::String& title, const juce::String& text = {})
    {
        if (enabled != nullptr && ! enabled (kind))
            return;
        // The same notice again while it's up (a channel clipping over and over): it stays up longer.
        for (auto* t : cards)
            if (t->title == title && t->text == text && ! t->leaving)
            {
                t->born = now() - appearMs;
                return;
            }
        auto* card = cards.add (new Card (*this, kind, title, text));
        addAndMakeVisible (card);
        int showing = 0;
        for (int i = cards.size(); --i >= 0;)   // too many up: the oldest go
            if (! cards[i]->leaving && ++showing > maxCards)
            {
                cards[i]->leaving = true;
                cards[i]->leftAt = now();
            }
        place (true);
        if (! isTimerRunning())
            startTimerHz (60);
    }

    // The area the cards stack in (the window's): they sit in its bottom-right corner.
    void resized() override { place (false); }

private:
    static constexpr int cardWidth = 300, cardHeight = 54, tipHeight = 76, gap = 8, margin = 14, maxCards = 4;
    static constexpr double appearMs = 380.0, lifeMs = 4200.0, tipLifeMs = 11000.0, leaveMs = 320.0;

    static double now() { return juce::Time::getMillisecondCounterHiRes(); }

    struct Card : juce::Component
    {
        Card (Toasts& o, const juce::String& k, const juce::String& t, const juce::String& x)
            : owner (o), kind (k), title (t), text (x), born (now())
        {
            setSize (cardWidth, k == "tip" ? tipHeight : cardHeight);
        }

        juce::Colour accent() const
        {
            using namespace Console::Colours;
            return kind == "clip" ? danger() : kind == "device" || kind == "tip" ? link() : kind == "scene" ? selected() : kind == "macro" ? on() : legendDim();
        }

        void paint (juce::Graphics& g) override
        {
            using namespace Console;
            const auto r = getLocalBounds().toFloat().reduced (4.0f);
            juce::Path shape;
            shape.addRoundedRectangle (r, 5.0f);
            juce::DropShadow (juce::Colours::black.withAlpha (0.4f), 8, { 0, 2 }).drawForPath (g, shape);
            g.setColour (Colours::capTop());
            g.fillPath (shape);
            g.setColour (Colours::rim());
            g.strokePath (shape, juce::PathStrokeType (1.0f));
            g.setColour (accent());
            g.fillRoundedRectangle (r.withWidth (4.0f), 2.0f);

            auto inside = r.reduced (14.0f, 6.0f).withTrimmedLeft (2.0f);
            g.setColour (Colours::capText());
            g.setFont (legendFont (Type::name));
            g.drawFittedText (title.toUpperCase(), inside.removeFromTop (text.isEmpty() ? inside.getHeight() : 20.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.8f);
            if (text.isNotEmpty())
            {
                g.setColour (Colours::capText().withAlpha (0.75f));
                g.setFont (juce::FontOptions (Type::body));
                g.drawFittedText (text, inside.toNearestInt(), juce::Justification::topLeft, kind == "tip" ? 2 : 1, 0.8f);
            }
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (! leaving)
            {
                leaving = true;
                leftAt = now();
            }
        }

        Toasts& owner;
        juce::String kind, title, text;
        double born, leftAt = 0.0;
        bool leaving = false;
        float y = -1.0f;   // where it's drawn (gliding to its place in the stack)
    };

    // Each card's place: from the bottom up, newest lowest.
    void place (bool)
    {
        float bottom = (float) (getHeight() - margin);
        for (int i = cards.size(); --i >= 0;)
        {
            auto* c = cards[i];
            if (c->leaving)
                continue;
            const float target = bottom - (float) c->getHeight();
            bottom = target - (float) gap;
            if (c->y < 0.0f)
                c->y = target;
            c->getProperties().set ("target", target);
        }
    }

    void timerCallback() override
    {
        const double t = now();
        for (int i = cards.size(); --i >= 0;)
        {
            auto* c = cards[i];
            if (! c->leaving && t - c->born > (c->kind == "tip" ? tipLifeMs : lifeMs))
            {
                c->leaving = true;
                c->leftAt = t;
            }
            if (c->leaving && t - c->leftAt > leaveMs)
            {
                cards.remove (i);
                place (false);
                continue;
            }
        }
        place (false);
        for (auto* c : cards)
        {
            // In: slides from the right with a little overshoot (an ease-out-back); out: fades and drifts right.
            const float in = (float) juce::jlimit (0.0, 1.0, (t - c->born) / appearMs);
            constexpr float s = 1.70158f;
            const float back = 1.0f + (s + 1.0f) * std::pow (in - 1.0f, 3.0f) + s * std::pow (in - 1.0f, 2.0f);
            const float out = c->leaving ? (float) juce::jlimit (0.0, 1.0, (t - c->leftAt) / leaveMs) : 0.0f;
            const float x = (float) (getWidth() - margin - cardWidth) + (1.0f - back) * (cardWidth + margin) + out * 40.0f;
            const float target = (float) c->getProperties()["target"];
            c->y += (target - c->y) * 0.22f;
            c->setBounds (juce::roundToInt (x), juce::roundToInt (c->y), cardWidth, c->getHeight());
            c->setAlpha (juce::jlimit (0.0f, 1.0f, in * 2.0f) * (1.0f - out));
        }
        if (cards.isEmpty())
            stopTimer();
    }

    juce::OwnedArray<Card> cards;   // oldest first
};

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

// Motion for the desk's controls (drawn by Console.h): every key, tab, selector, knob and fader
// moves the way good hardware and a good touchscreen do, quickly and without fuss:
//
//   hover   eases in (~90 ms) and out (~200 ms): the cap brightens and its edge catches the light
//   press   a spring: down at once, and back up with a slight overshoot as it's let go
//   ripple  a soft ring of light spreads from where it was clicked and fades (~0.45 s)
//   open    a selector's chevron turns over while its list is open
//
// One animator for the program (Console::motion()): a component is followed from the first time
// it's drawn; mouse activity anywhere wakes it, it repaints what's still moving at 60 fps, and it
// stops once everything has settled.
namespace Motion
{
    struct State
    {
        float hover = 0.0f;      // 0..1
        float press = 0.0f;      // 0 up .. 1 down (a little past either end while springing)
        float velocity = 0.0f;
        float open = 0.0f;       // 0..1: a selector's list open
        float ripple = 1.0f;     // 0..1 through the ripple; 1: none
        juce::Point<float> rippleAt;
    };

    class Animator : private juce::Timer, private juce::MouseListener
    {
    public:
        Animator() { juce::Desktop::getInstance().addGlobalMouseListener (this); }
        ~Animator() override { juce::Desktop::getInstance().removeGlobalMouseListener (this); }

        // The component's motion now (it's followed from here on).
        const State& of (juce::Component& c)
        {
            auto& e = entries[&c];
            if (e.component == nullptr)   // first sight (or a new component where an old one was): as it is, no motion
            {
                e.component = &c;
                e.state = {};
                e.state.hover = hovered (c) ? 1.0f : 0.0f;
                e.state.press = down (c) ? 1.0f : 0.0f;
                e.state.open = open (c) ? 1.0f : 0.0f;
            }
            return e.state;
        }

    private:
        struct Entry { juce::Component::SafePointer<juce::Component> component; State state; };
        std::map<juce::Component*, Entry> entries;
        double lastEvent = 0.0;

        static bool hovered (juce::Component& c) { return c.isEnabled() && c.isMouseOver (true); }
        static bool down (juce::Component& c)
        {
            if (auto* b = dynamic_cast<juce::Button*> (&c))
                return b->isDown();
            return c.isEnabled() && c.isMouseButtonDown (false);
        }
        static bool open (juce::Component& c)
        {
            auto* box = dynamic_cast<juce::ComboBox*> (&c);
            return box != nullptr && box->isPopupActive();
        }

        void wake()
        {
            lastEvent = juce::Time::getMillisecondCounterHiRes();
            if (! isTimerRunning())
                startTimerHz (60);
        }

        void mouseMove (const juce::MouseEvent&) override  { wake(); }
        void mouseEnter (const juce::MouseEvent&) override { wake(); }
        void mouseExit (const juce::MouseEvent&) override  { wake(); }
        void mouseDrag (const juce::MouseEvent&) override  { wake(); }
        void mouseUp (const juce::MouseEvent&) override    { wake(); }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override { wake(); }

        // A click: the ripple starts where it landed, on the control it landed on (or the one it's part of).
        void mouseDown (const juce::MouseEvent& e) override
        {
            wake();
            for (auto* c = e.eventComponent; c != nullptr; c = c->getParentComponent())
                if (auto it = entries.find (c); it != entries.end() && it->second.component != nullptr)
                {
                    it->second.state.ripple = 0.0f;
                    it->second.state.rippleAt = e.getEventRelativeTo (c).position;
                    break;
                }
        }

        void timerCallback() override
        {
            constexpr float dt = 1.0f / 60.0f;
            bool moving = false;
            for (auto it = entries.begin(); it != entries.end();)
            {
                auto* c = it->second.component.getComponent();
                if (c == nullptr) { it = entries.erase (it); continue; }
                auto& s = it->second.state;
                const State before = s;

                // Hover: quick in, gentler out.
                const float hoverTarget = hovered (*c) ? 1.0f : 0.0f;
                s.hover += (hoverTarget - s.hover) * (1.0f - std::exp (-dt / (hoverTarget > s.hover ? 0.03f : 0.07f)));

                // Press: a spring, stiff and damped going down (it lands at once), softer coming up
                // (it overshoots a touch and settles), in small steps so it stays steady.
                const bool pressed = down (*c);
                const float target = pressed ? 1.0f : 0.0f;
                const float stiffness = pressed ? 2600.0f : 900.0f, damping = pressed ? 90.0f : 26.0f;
                for (int i = 0; i < 4; ++i)
                {
                    const float h = dt / 4.0f;
                    s.velocity += (stiffness * (target - s.press) - damping * s.velocity) * h;
                    s.press += s.velocity * h;
                }
                if (std::abs (s.press - target) < 0.002f && std::abs (s.velocity) < 0.01f)
                {
                    s.press = target;
                    s.velocity = 0.0f;
                }

                const float openTarget = open (*c) ? 1.0f : 0.0f;
                s.open += (openTarget - s.open) * (1.0f - std::exp (-dt / 0.04f));
                if (s.ripple < 1.0f)
                    s.ripple = juce::jmin (1.0f, s.ripple + dt / 0.45f);

                for (auto* v : { &s.hover, &s.open })
                    if (std::abs (*v - std::round (*v)) < 0.003f)
                        *v = std::round (*v);

                if (s.hover != before.hover || s.press != before.press || s.open != before.open || s.ripple != before.ripple)
                {
                    c->repaint();
                    moving = true;
                }
                ++it;
            }
            if (! moving && juce::Time::getMillisecondCounterHiRes() - lastEvent > 250.0)
                stopTimer();
        }
    };
}

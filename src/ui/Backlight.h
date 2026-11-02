#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

// Lamps behind illuminated switches (drawn by Console.h), as on classic console desks:
//
//   - A lamp fades up and down over a few dozen milliseconds, like a bulb, rather than snapping.
//   - Keys marked to blink (Solo) pulse while on, the way desks warn that something is soloed.
namespace Backlight
{
    // The fade every light in the program shares: it comes up almost at once and dies away with a
    // short afterglow, like a filament lamp or a VFD's phosphor. `value` 0..1, stepped by `dt`
    // seconds; `fallSeconds` is how long the glow takes to die out.
    inline float afterglow (float value, bool on, float dt, float fallSeconds = 0.15f)
    {
        if (on)
            return juce::jmin (1.0f, value + dt / 0.035f);
        value *= std::exp (-dt * 3.0f / fallSeconds);
        return value < 0.02f ? 0.0f : value;
    }


    // Marks a button to pulse while it's on. The owner repaints it while it's on (the pulse needs frames).
    inline void setBlinks (juce::Button& b) { b.getProperties().set ("blink", true); }
    inline bool blinks (const juce::Button& b) { return b.getProperties()["blink"]; }

    // 0.45..1, about 1.2 times a second, for a blinking key that's on; 1 otherwise.
    inline float pulse (const juce::Button& b)
    {
        if (! blinks (b) || ! b.getToggleState())
            return 1.0f;
        const double t = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        return 0.45f + 0.55f * (float) (0.5 + 0.5 * std::sin (t * juce::MathConstants<double>::twoPi * 1.2));
    }

    // Lamp brightness per button, with the afterglow fade: up in about 35 ms, dying away over
    // about 200 ms. Repaints the buttons it's fading at 60 fps until they settle.
    // A key that rebuilds its view when pressed (an output's ON, STEREO, LOOPBACK...) is replaced
    // by a new button in the same place; that one carries on from the old lamp, so it still fades.
    // Held by each look-and-feel through a juce::SharedResourcePointer, so it goes before JUCE does.
    class Lamps : private juce::Timer
    {
    public:
        float level (juce::Button& b)
        {
            const float target = b.getToggleState() && b.isEnabled() ? 1.0f : 0.0f;
            const auto place = placeOf (b);
            auto it = lamps.find (&b);
            if (it == lamps.end() || it->second.button == nullptr)   // new (or a new button where an old one was)
            {
                const auto before = lastAt.find (place);
                const float start = before != lastAt.end() ? before->second : target;   // first sight: as the lamp here was
                lamps[&b] = { juce::Component::SafePointer<juce::Button> (&b), start };
                it = lamps.find (&b);
            }

            if (std::abs (it->second.value - target) > 0.001f && ! isTimerRunning())
                startTimerHz (60);
            remember (place, it->second.value);
            return it->second.value;
        }

    private:
        struct Lamp { juce::Component::SafePointer<juce::Button> button; float value = 0.0f; };

        // Where a button is on screen (4 px grid), to match a rebuilt button with the one it replaced.
        static juce::Point<int> placeOf (juce::Button& b) { return b.getScreenBounds().getCentre() / 4; }

        void remember (juce::Point<int> place, float value)
        {
            if (lastAt.size() > 1024)
                lastAt.clear();
            lastAt[place] = value;
        }

        struct PointOrder { bool operator() (juce::Point<int> a, juce::Point<int> b) const { return a.x != b.x ? a.x < b.x : a.y < b.y; } };
        std::map<juce::Point<int>, float, PointOrder> lastAt;

        void timerCallback() override
        {
            bool moving = false;
            for (auto it = lamps.begin(); it != lamps.end();)
            {
                auto& lamp = it->second;
                if (lamp.button == nullptr) { it = lamps.erase (it); continue; }

                const float target = lamp.button->getToggleState() && lamp.button->isEnabled() ? 1.0f : 0.0f;
                if (std::abs (lamp.value - target) > 0.001f)
                {
                    lamp.value = afterglow (lamp.value, target > 0.5f, 1.0f / 60.0f, 0.2f);
                    remember (placeOf (*lamp.button), lamp.value);
                    lamp.button->repaint();
                    moving = true;
                }
                ++it;
            }
            if (! moving)
                stopTimer();
        }

        std::map<juce::Button*, Lamp> lamps;
    };
}

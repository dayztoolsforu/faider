#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Console.h"

// PRE-FLIGHT: before going live, the setup at a glance. A row per check, a light (green: fine,
// red: not), what it found, and a FIX key where one click can put it right. Shown a few seconds
// after Faider starts if anything's wrong (SETTINGS > GENERAL can stop that), and from FIND.
// ZERO DESK is the same panel with its own checks (from PRE-FLIGHT's ZERO THE DESK, or FIND).
class PreflightPanel : public juce::Component
{
public:
    struct Check
    {
        juce::String title, detail;
        bool ok = true;
        juce::String fixLabel;          // empty: nothing to press
        std::function<void()> fix;
    };

    // `recheck` gives the checks as they are now (after a fix, the list follows).
    explicit PreflightPanel (std::function<std::vector<Check>()> recheckFn, juce::String headingText = "PRE-FLIGHT")
        : heading (std::move (headingText)), recheck (std::move (recheckFn))
    {
        again.onClick = [this] { refresh(); };
        addAndMakeVisible (again);
        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Console;
        g.fillAll (Colours::panel());
        g.setColour (Colours::legend());
        g.setFont (legendFont (Type::display));
        g.drawText (heading, getLocalBounds().reduced (Space::pad).removeFromTop (28), juce::Justification::centredLeft);
        for (size_t i = 0; i < checks.size(); ++i)
        {
            const auto& c = checks[i];
            auto r = rowArea ((int) i);
            drawLED (g, { (float) r.getX() + 7.0f, (float) r.getY() + 11.0f }, 5.0f, c.ok ? Colours::ledGreen() : Colours::danger(), 1.0f);
            r.removeFromLeft (22);
            if (fixes[(int) i] != nullptr)
                r.removeFromRight (fixes[(int) i]->getWidth() + Space::pad);
            g.setColour (Colours::legend());
            g.setFont (legendFont (Type::name));
            g.drawText (c.title.toUpperCase(), r.removeFromTop (20), juce::Justification::centredLeft, true);
            g.setColour (Colours::legendDim());
            g.setFont (juce::FontOptions (Type::body));
            g.drawFittedText (c.detail, r, juce::Justification::topLeft, 2, 0.9f);
        }
    }

    void resized() override
    {
        using namespace Console;
        for (size_t i = 0; i < checks.size(); ++i)
            if (fixes[(int) i] != nullptr)
            {
                const auto r = rowArea ((int) i);
                fixes[(int) i]->setBounds (r.getRight() - 130, r.getY() + 4, 130, Space::key);
            }
        auto bottom = getLocalBounds().reduced (Space::pad).removeFromBottom (Space::key);
        again.setBounds (bottom.removeFromRight (160));
        other.setBounds (bottom.removeFromLeft (160));
    }

    bool allClear() const { return std::all_of (checks.begin(), checks.end(), [] (const Check& c) { return c.ok; }); }

    // A key at the bottom left, for the other list (PRE-FLIGHT's ZERO THE DESK).
    void setOtherKey (const juce::String& text, const juce::String& tip, std::function<void()> onClick)
    {
        other.setButtonText (text);
        other.setTooltip (tip);
        other.onClick = std::move (onClick);
        addAndMakeVisible (other);
        resized();
    }

private:
    static constexpr int rowH = 58;

    juce::Rectangle<int> rowArea (int i) const
    {
        return getLocalBounds().reduced (Console::Space::pad).withTrimmedTop (36 + i * rowH).withHeight (rowH - 6);
    }

    void refresh()
    {
        checks = recheck();
        fixes.clear();
        for (auto& c : checks)
        {
            auto* key = fixes.add (! c.ok && c.fix != nullptr ? new juce::TextButton (c.fixLabel.isNotEmpty() ? c.fixLabel : juce::String ("FIX")) : nullptr);
            if (key != nullptr)
            {
                key->onClick = [this, fix = c.fix]
                {
                    fix();
                    juce::Timer::callAfterDelay (600, [safe = juce::Component::SafePointer<PreflightPanel> (this)] { if (safe != nullptr) safe->refresh(); });
                };
                addAndMakeVisible (key);
            }
        }
        setSize (560, 36 + (int) checks.size() * rowH + Console::Space::key + 2 * Console::Space::pad + 8);
        resized();
        repaint();
    }

    juce::String heading;   // (PRE-FLIGHT, or ZERO DESK)
    std::function<std::vector<Check>()> recheck;
    std::vector<Check> checks;
    juce::OwnedArray<juce::TextButton> fixes;   // per check (null where there's none)
    juce::TextButton again { "CHECK AGAIN" }, other;
};

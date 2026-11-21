#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Console.h"

// PRE-FLIGHT: before going live, the setup at a glance. A row per check, a light (green: fine,
// red: not, yellow: only a suggestion), what it found, and a FIX key where one click can put it right
// (the mic output's row, while it's fine, has HEAR WHAT THEY HEAR instead). Shown a few seconds
// after Faider starts if anything's wrong (SETTINGS > STARTUP & CHECKS can stop that), and from FIND.
// ZERO DESK is the same panel with its own checks (from PRE-FLIGHT's ZERO THE DESK, SETTINGS > STARTUP & CHECKS, or FIND).
// While it shows, the checks are looked at again every 1.5 s and the rows follow: a fix that ends
// later than its key (SET MY DEFAULTS' menu, BOOST once Windows has asked), ZERO THE DESK's own
// panel, or the mix changing behind it.
class PreflightPanel : public juce::Component,
                       private juce::Timer
{
public:
    struct Check
    {
        juce::String title, detail;
        bool ok = true;
        juce::String fixLabel;          // empty: nothing to press
        std::function<void()> fix;
        bool advisory = false;          // not ok, but only a suggestion: yellow, and not "something's wrong"
        juce::String tryLabel = {};     // a key shown while it's fine instead (the mic output's HEAR WHAT THEY HEAR)
        std::function<void()> tryIt = {};
    };

    // `recheck` gives the checks as they are now (after a fix, the list follows).
    explicit PreflightPanel (std::function<std::vector<Check>()> recheckFn, juce::String headingText = TRANS ("PRE-FLIGHT"))
        : heading (std::move (headingText)), recheck (std::move (recheckFn))
    {
        again.onClick = [this] { refresh(); };
        addAndMakeVisible (again);
        refresh();
        startTimer (1500);
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
            drawLED (g, { (float) r.getX() + 7.0f, (float) r.getY() + 11.0f }, 5.0f, c.ok ? Colours::ledGreen() : c.advisory ? Colours::ledYellow() : Colours::danger(), 1.0f);
            r.removeFromLeft (22);
            if (fixes[(int) i] != nullptr)
                r.removeFromRight (fixes[(int) i]->getWidth() + Space::pad);
            g.setColour (Colours::legend());
            g.setFont (legendFont (Type::name));
            g.drawFittedText (Language::upper (c.title), r.removeFromTop (20), juce::Justification::centredLeft, 1, 0.7f);   // (squeezed, not cut, when long)
            g.setColour (Colours::legendDim());
            g.setFont (juce::FontOptions (Type::body));
            g.drawFittedText (c.detail, r, juce::Justification::topLeft, 2, 0.8f);
        }
    }

    void resized() override
    {
        using namespace Console;
        for (size_t i = 0; i < checks.size(); ++i)
            if (fixes[(int) i] != nullptr)
            {
                const auto r = rowArea ((int) i);
                const int w = keyWidth (*fixes[(int) i], fixes[(int) i]->getButtonText().length() > 15 ? 190 : 130, 240);   // (a long name gets a wider key)
                fixes[(int) i]->setBounds (r.getRight() - w, r.getY() + 4, w, Space::key);
            }
        auto bottom = getLocalBounds().reduced (Space::pad).removeFromBottom (Space::key);
        const int half = bottom.getWidth() / 2 - Space::pad;
        again.setBounds (bottom.removeFromRight (keyWidth (again, 160, half)));
        other.setBounds (bottom.removeFromLeft (keyWidth (other, 160, half)));
    }

    bool allClear() const { return std::all_of (checks.begin(), checks.end(), [] (const Check& c) { return c.ok || c.advisory; }); }

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

    // A key's width: `usual`, or wider (up to `most`) when its (translated) text needs it.
    static int keyWidth (juce::TextButton& key, int usual, int most)
    {
        const int needed = juce::GlyphArrangement::getStringWidthInt (Console::legendFont (12.5f), key.getButtonText()) + 24;
        return juce::jmax (usual, juce::jmin (most, needed));
    }

    juce::Rectangle<int> rowArea (int i) const
    {
        return getLocalBounds().reduced (Console::Space::pad).withTrimmedTop (36 + i * rowH).withHeight (rowH - 6);
    }

    // (not while a key is held: the rows' keys are made again)
    void timerCallback() override
    {
        if (! isShowing() || juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
            return;
        if (auto now = recheck(); signature (now) != signature (checks))
            show (std::move (now));
    }

    static juce::String signature (const std::vector<Check>& list)
    {
        juce::String s;
        for (auto& c : list)
            s << c.title << "|" << c.detail << "|" << (int) c.ok << (int) c.advisory << (int) (c.fix != nullptr) << "|" << c.fixLabel << "|" << c.tryLabel << "\n";
        return s;
    }

    void refresh() { show (recheck()); }

    void show (std::vector<Check> now)
    {
        checks = std::move (now);
        fixes.clear();
        for (auto& c : checks)
        {
            const bool wrong = ! c.ok && c.fix != nullptr, canTry = c.ok && c.tryIt != nullptr;
            auto* key = fixes.add (wrong   ? new juce::TextButton (c.fixLabel.isNotEmpty() ? c.fixLabel : TRANS ("FIX"))
                                 : canTry ? new juce::TextButton (c.tryLabel) : nullptr);
            if (key != nullptr)
            {
                key->onClick = [this, fix = wrong ? c.fix : c.tryIt]
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
    juce::TextButton again { TRANS ("CHECK AGAIN") }, other;
};

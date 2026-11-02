#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Console.h"
#include "../ProductName.h"

// The command palette (Ctrl+K, or FIND): type a few letters of anything Patchbay can do, a
// channel, scene, macro, pad, page or setting, and Enter does it. Matches every word typed, in
// any order, a word's start counting most; letters in order (a short form, "mcph") match too.
// Up and down choose, Esc or clicking away closes. Commands come from the app (MainComponent),
// made fresh each time it opens so they're always the current ones. RECENT: the last few run come
// first while nothing's typed (and rank a little higher while something is); kept between runs.
class CommandPalette : public juce::Component,
                       private juce::ListBoxModel,
                       private juce::KeyListener
{
public:
    struct Command
    {
        juce::String name;      // what it does: "MUTE MICROPHONE"
        juce::String detail;    // what it is, dimmer: "Input"
        std::function<void()> run;
    };

    CommandPalette (std::vector<Command> commands, std::function<void()> closeIt)
        : all (std::move (commands)), close (std::move (closeIt))
    {
        query.setFont (juce::FontOptions (Console::Type::name));
        query.onTextChange = [this] { filter(); };
        query.addKeyListener (this);
        addAndMakeVisible (query);

        list.setModel (this);
        list.setRowHeight (Console::Space::row + 4);
        addAndMakeVisible (list);
        styleColours();
        filter();
        juce::Desktop::getInstance().addGlobalMouseListener (this);   // a click anywhere else closes it
    }

    ~CommandPalette() override
    {
        juce::Desktop::getInstance().removeGlobalMouseListener (this);
        query.removeKeyListener (this);
    }

    void grabFocus() { query.grabKeyboardFocus(); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.45f));   // a soft shadow under it
        g.fillRoundedRectangle (r.translated (0.0f, 4.0f).reduced (2.0f), 8.0f);
        g.setColour (Console::Colours::panel());
        g.fillRoundedRectangle (r.reduced (2.0f), 8.0f);
        g.setColour (Console::Colours::selected());
        g.drawRoundedRectangle (r.reduced (2.5f), 8.0f, 1.5f);
        g.setColour (Console::Colours::legendDim());
        g.setFont (Console::legendFont (Console::Type::tag));
        g.drawText ("ENTER: DO IT    UP / DOWN: CHOOSE    ESC: CLOSE", getLocalBounds().removeFromBottom (Console::Space::title).reduced (Console::Space::pad, 0),
                    juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (Console::Space::pad);
        query.setBounds (r.removeFromTop (Console::Space::bar));
        r.removeFromTop (Console::Space::gap);
        r.removeFromBottom (Console::Space::title - Console::Space::pad);   // the hint line (paint)
        list.setBounds (r);
    }

    // A click outside it closes it (the app removes it once that's done).
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != this && ! isParentOf (e.eventComponent) && ! getScreenBounds().contains (e.getScreenPosition()))
            close();
    }

    // A theme change while it's open: its colours in the new theme's.
    void lookAndFeelChanged() override
    {
        styleColours();
        repaint();
    }

private:
    // The search field's and list's colours, from the theme (at the start and on a theme change).
    void styleColours()
    {
        query.setTextToShowWhenEmpty ("Search channels, scenes, settings...", Console::Colours::legendDim());
        query.setColour (juce::TextEditor::backgroundColourId, Console::Colours::panelDark());
        query.setColour (juce::TextEditor::outlineColourId, Console::Colours::rim());
        query.setColour (juce::TextEditor::focusedOutlineColourId, Console::Colours::selected());
        query.setColour (juce::TextEditor::textColourId, Console::Colours::legend());
        query.applyColourToAllText (Console::Colours::legend(), true);   // (typed text keeps the colour it was typed in)
        list.setColour (juce::ListBox::backgroundColourId, Console::Colours::panelDark());
    }

    // The last commands run (by name), newest first, kept in %APPDATA%\Patchbay\find-recent.txt.
    static constexpr int recentCount = 8;
    static juce::File recentFile() { return Product::dataFolder().getChildFile ("find-recent.txt"); }
    static juce::StringArray loadRecent()
    {
        juce::StringArray names;
        names.addLines (recentFile().loadFileAsString());
        names.removeEmptyStrings();
        return names;
    }
    static void remember (const juce::String& name)
    {
        auto names = loadRecent();
        names.removeString (name);
        names.insert (0, name);
        names.removeRange (recentCount, names.size());
        recentFile().getParentDirectory().createDirectory();
        recentFile().replaceWithText (names.joinIntoString ("\n"));
    }

    // How well `c` matches the words typed (0: not at all). Every word must be in the name or detail;
    // a word at the start of a word scores more; letters in order (a short form) score least.
    static int score (const Command& c, const juce::StringArray& words)
    {
        if (words.isEmpty())
            return 1;
        const auto name = c.name.toLowerCase(), text = name + " " + c.detail.toLowerCase();
        int total = 0;
        for (auto& w : words)
        {
            const int at = text.indexOf (w);
            if (at >= 0)
            {
                total += 10 + (at == 0 || text[at - 1] == ' ' ? 20 : 0) + (name.contains (w) ? 5 : 0);
                continue;
            }
            int from = 0;   // letters in order
            for (auto ch : w)
            {
                from = text.indexOfChar (from, ch);
                if (from < 0)
                    return 0;
                ++from;
            }
            total += 3;
        }
        return total;
    }

    void filter()
    {
        const auto words = juce::StringArray::fromTokens (query.getText().toLowerCase().trim(), " ", {});
        std::vector<std::pair<int, const Command*>> ranked;
        for (auto& c : all)
            if (int s = score (c, words); s > 0)
            {
                if (const int at = recent.indexOf (c.name); at >= 0)   // recent: first with nothing typed, a nudge up with something
                    s += words.isEmpty() ? 1000 - at : 4;
                ranked.push_back ({ s, &c });
            }
        std::stable_sort (ranked.begin(), ranked.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        shown.clear();
        for (size_t i = 0; i < ranked.size() && i < 60; ++i)
            shown.push_back (ranked[i].second);
        list.updateContent();
        list.selectRow (0);
        list.repaint();
    }

    void runChosen()
    {
        const int row = list.getSelectedRow();
        if (! juce::isPositiveAndBelow (row, (int) shown.size()))
            return;
        auto run = shown[(size_t) row]->run;
        remember (shown[(size_t) row]->name);
        close();   // (this goes now; the command runs once it has)
        juce::MessageManager::callAsync ([run] { if (run) run(); });
    }

    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        if (key == juce::KeyPress::escapeKey)  { close(); return true; }
        if (key == juce::KeyPress::returnKey)  { runChosen(); return true; }
        const int row = list.getSelectedRow();
        if (key == juce::KeyPress::downKey)    { list.selectRow (juce::jmin (row + 1, (int) shown.size() - 1)); return true; }
        if (key == juce::KeyPress::upKey)      { list.selectRow (juce::jmax (row - 1, 0)); return true; }
        return false;
    }

    // ListBoxModel
    int getNumRows() override { return (int) shown.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) shown.size()))
            return;
        auto r = juce::Rectangle<int> (width, height).reduced (Console::Space::gap, 0);
        if (selected)
        {
            g.setColour (Console::Colours::selected().withAlpha (0.22f));
            g.fillRoundedRectangle (juce::Rectangle<int> (width, height).reduced (2, 1).toFloat(), 4.0f);
        }
        const auto& c = *shown[(size_t) row];
        g.setColour (Console::Colours::legendDim());
        g.setFont (Console::legendFont (Console::Type::legend));
        g.drawText (c.detail.toUpperCase(), r.removeFromRight (160), juce::Justification::centredRight, true);
        if (query.isEmpty() && recent.contains (c.name))
        {
            g.setColour (Console::Colours::selected());
            g.setFont (Console::legendFont (Console::Type::tag));
            g.drawText ("RECENT", r.removeFromRight (60), juce::Justification::centredRight, true);
        }
        g.setColour (Console::Colours::legend());
        g.setFont (Console::legendFont (Console::Type::body));
        g.drawText (c.name, r, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override { list.selectRow (row); query.grabKeyboardFocus(); }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { runChosen(); }
    void returnKeyPressed (int) override { runChosen(); }

    std::vector<Command> all;
    const juce::StringArray recent = loadRecent();
    std::vector<const Command*> shown;
    std::function<void()> close;
    juce::TextEditor query;
    juce::ListBox list;
};

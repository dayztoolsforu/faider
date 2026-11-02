#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Console.h"
#include "Hotkeys.h"
 #include "DeskSettings.h"

// A settings page of plain rows: headings, on/off keys, a choice of one key from several, and
// actions. The keys show what's set now (read again 4 times a second while shown, so a change made
// elsewhere, the desk's METER key or the tray, shows here too).
class OptionsPage : public juce::Component, private juce::Timer
                  , public DeskSettings::WidthFill
{
public:
    OptionsPage() { startTimerHz (4); }

    void heading (const juce::String& text)
    {
        auto* l = labels.add (new juce::Label ({}, text));
        l->setFont (Console::legendFont (Console::Type::name));   // section headings
        l->setColour (juce::Label::textColourId, Console::Colours::legendDim());
        l->setFont (LargeFormat::print (12.5f));   // (the console's print)
        l->setColour (juce::Label::textColourId, DeskSettings::print());
        addAndMakeVisible (l);
        rows.push_back ({ l, true });
    }

    void toggle (const juce::String& text, const juce::String& tip, std::function<bool()> get, std::function<void (bool)> set)
    {
        auto* sw = switches.add (new DeskSettings::Switch (text));
        sw->setTooltip (tip);
        addAndMakeVisible (sw);
        rows.push_back ({ sw, false });
        sw->onClick = [this, get, set] { set (! get()); refresh(); };
        refreshers.push_back ([sw, get] { sw->setToggleState (get(), juce::dontSendNotification); });
    }

    // One of several, a key each (lit: the one chosen): down the page, or side by side on one row.
    void choice (const juce::StringArray& names, std::function<int()> get, std::function<void (int)> set, bool oneRow = false)
    {
        for (int i = 0; i < names.size(); ++i)
        {
            auto& b = key (names[i], {}, oneRow && i > 0);
            b.onClick = [this, i, set] { set (i); refresh(); };
            refreshers.push_back ([&b, i, get] { b.setToggleState (get() == i, juce::dontSendNotification); });
        }
    }

    void action (const juce::String& text, const juce::String& tip, std::function<void()> run)
    {
        auto& b = key (text, tip);
        b.setClickingTogglesState (false);
        b.onClick = std::move (run);
    }

    // Done adding rows: sized to them.
    void finish()
    {
        refresh();
        setSize (juce::jmax (getWidth(), width), heightFor (juce::jmax (getWidth(), width)));
    }

    // Each heading and its rows a card; the cards fill the page in columns (each
    // into the shortest), as many columns as the width holds.
    int heightFor (int w) override { return arrange (w, false); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (DeskSettings::desk());
        for (auto& c : cards)
            DeskSettings::card (g, c.toFloat());
    }
    void resized() override { arrange (getWidth(), true); }

    int arrange (int w, bool apply)
    {
        constexpr int pad = 14, gap = 12, cardW = 300, head = 30, rowH = 28, rowGap = 4, inner = 12;
        const int columns = juce::jmax (1, (w - 2 * pad + gap) / (cardW + gap));
        const int colW = (w - 2 * pad - (columns - 1) * gap) / columns;
        std::vector<int> bottom ((size_t) columns, pad);
        if (apply)
            cards.clear();
        for (size_t i = 0; i < rows.size();)
        {
            size_t end = i + 1;   // (a heading and the rows after it)
            while (end < rows.size() && ! rows[end].heading)
                ++end;
            const int h = (rows[i].heading ? head : inner) + (int) (end - i - (rows[i].heading ? 1 : 0)) * (rowH + rowGap) + inner;
            const auto c = (size_t) (std::min_element (bottom.begin(), bottom.end()) - bottom.begin());
            const juce::Rectangle<int> box (pad + (int) c * (colW + gap), bottom[c], colW, h);
            bottom[c] += h + gap;
            if (apply)
            {
                cards.push_back (box);
                auto r = box.reduced (inner, 0);
                size_t k = i;
                if (rows[i].heading)
                {
                    rows[i].c->setBounds (r.withHeight (head).withTrimmedTop (4));
                    r.removeFromTop (head);
                    ++k;
                }
                else
                    r.removeFromTop (inner);
                for (; k < end; ++k)
                {
                    auto line = r.removeFromTop (rowH);
                    r.removeFromTop (rowGap);
                    const int n = (int) rows[k].more.size() + 1;
                    const int kw = (line.getWidth() - (n - 1) * 4) / n;
                    rows[k].c->setBounds (line.removeFromLeft (n == 1 ? line.getWidth() : kw));
                    for (auto* m : rows[k].more)
                    {
                        line.removeFromLeft (4);
                        m->setBounds (m == rows[k].more.back() ? line : line.removeFromLeft (kw));
                    }
                }
            }
            i = end;
        }
        return *std::max_element (bottom.begin(), bottom.end()) + pad;
    }

private:
    struct Row { juce::Component* c; bool heading; std::vector<juce::Component*> more = {}; };   // more: keys beside c

    juce::TextButton& key (const juce::String& text, const juce::String& tip, bool besideLast = false)
    {
        auto* b = keys.add (new juce::TextButton (text));
        b->setClickingTogglesState (true);   // (lights it; refresh then shows what it set really did)
        b->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
        b->setTooltip (tip);
        addAndMakeVisible (b);
        if (besideLast && ! rows.empty() && ! rows.back().heading)
            rows.back().more.push_back (b);
        else
            rows.push_back ({ b, false });
        return *b;
    }

    void refresh()
    {
        for (auto& r : refreshers)
            r();
    }

    void timerCallback() override { if (isShowing()) refresh(); }

    // A theme change: the colours set when the page was made, in the new theme's.
    void lookAndFeelChanged() override
    {
        for (auto* l : labels)
            l->setColour (juce::Label::textColourId, Console::Colours::legendDim());
        for (auto* b : keys)
            b->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
        repaint();
    }

    static constexpr int width = 520;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::TextButton> keys;
    juce::OwnedArray<DeskSettings::Switch> switches;
    std::vector<juce::Rectangle<int>> cards;
    std::vector<Row> rows;
    std::vector<std::function<void()>> refreshers;
};

// HOTKEYS: every global hotkey in one list (pads, macros, scenes, instant replay, the overlay),
// grouped; click a key, then press the new one (Esc: cancel, Delete: none). The keys that only
// work inside Patchbay are listed under it, fixed.
class HotkeysPage : public juce::Component, private juce::Timer
                  , public DeskSettings::WidthFill
{
public:
    struct Entry
    {
        juce::String group, name, key;                         // key: as juce::KeyPress describes it, or empty
        std::function<bool (const juce::KeyPress&)> set;       // false: refused (another app has it); null: fixed
    };

    explicit HotkeysPage (std::function<std::vector<Entry>()> listFn) : list (std::move (listFn))
    {
        note.setFont (juce::FontOptions (Console::Type::body));
        note.setColour (juce::Label::textColourId, Console::Colours::legendDim());
        DeskSettings::styleHeading (note);
        note.setText ("CLICK A KEY, THEN PRESS THE NEW ONE.  ESC: CANCEL.  DELETE: NONE.", juce::dontSendNotification);
        addAndMakeVisible (note);
        rebuild();
        startTimerHz (1);
    }

    // The note across the top, each group a card (a name, its key at the right),
    // the cards in columns.
    int heightFor (int w) override { return arrangeCards (w, false); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (DeskSettings::desk());
        for (auto& c : cards)
            DeskSettings::card (g, c.toFloat());
    }
    void resized() override { arrangeCards (getWidth(), true); }
    int arrangeCards (int w, bool apply)
    {
        if (apply)
            note.setBounds (14, 10, w - 28, 24);
        std::vector<DeskSettings::Group> groups;
        for (auto& row : rows)
        {
            if (row.heading != nullptr)
                groups.push_back ({ row.heading.get(), {} });
            else if (! groups.empty())
                groups.back().rows.push_back ({ { row.name.get(), row.keyButton.get() }, 160 });
        }
        return DeskSettings::arrange (groups, w, apply, cards, 400, 42);
    }
    std::vector<juce::Rectangle<int>> cards;

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (capturing < 0)
            return false;
        if (key == juce::KeyPress::escapeKey)
            stopCapturing ({});
        else if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
            stopCapturing (entries[(size_t) capturing].set ({}) ? juce::String() : "COULDN'T CLEAR IT");
        else if (! GlobalHotkeys::usable (key))
            note.setText ("NEEDS CTRL, ALT OR AN F KEY", juce::dontSendNotification);
        else
            stopCapturing (entries[(size_t) capturing].set (key) ? juce::String() : key.getTextDescription().toUpperCase() + " IS ALREADY IN USE");
        return true;
    }

    void focusLost (FocusChangeType) override
    {
        if (capturing >= 0)
            stopCapturing ({});
    }

private:
    struct Row
    {
        std::unique_ptr<juce::Label> heading, name;
        std::unique_ptr<juce::TextButton> keyButton;
    };

    static juce::String signature (const std::vector<Entry>& list)
    {
        juce::String s;
        for (auto& e : list)
            s << e.group << "|" << e.name << "|" << e.key << "\n";
        return s;
    }

    void rebuild()
    {
        entries = list();
        shown = signature (entries);
        rows.clear();
        juce::String group;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            auto& e = entries[i];
            if (e.group != group)
            {
                group = e.group;
                Row r;
                r.heading = std::make_unique<juce::Label> (juce::String(), group);
                r.heading->setFont (Console::legendFont (Console::Type::name));
                r.heading->setColour (juce::Label::textColourId, Console::Colours::legendDim());
                DeskSettings::styleHeading (*r.heading);
                addAndMakeVisible (*r.heading);
                rows.push_back (std::move (r));
            }
            Row r;
            r.name = std::make_unique<juce::Label> (juce::String(), e.name);
            r.name->setFont (juce::FontOptions (Console::Type::body));
            r.name->setColour (juce::Label::textColourId, Console::Colours::legend());
            DeskSettings::styleText (*r.name);
            addAndMakeVisible (*r.name);
            r.keyButton = std::make_unique<juce::TextButton> (e.key.isEmpty() ? juce::String ("NONE") : e.key.toUpperCase());
            r.keyButton->setEnabled (e.set != nullptr);
            r.keyButton->setTooltip (e.set != nullptr ? "Click, then press the new key" : "Fixed");
            r.keyButton->onClick = [this, i] { startCapturing ((int) i); };
            addAndMakeVisible (*r.keyButton);
            rows.push_back (std::move (r));
        }

        const int w = juce::jmax (getWidth(), 560);
        setSize (w, heightFor (w));
        resized();
    }

    void startCapturing (int index)
    {
        capturing = index;
        size_t n = 0;
        for (auto& r : rows)
            if (r.keyButton != nullptr && (int) n++ == index)
                r.keyButton->setButtonText ("PRESS A KEY...");
        setWantsKeyboardFocus (true);
        grabKeyboardFocus();
    }

    void stopCapturing (const juce::String& problem)
    {
        capturing = -1;
        setWantsKeyboardFocus (false);
        note.setText (problem.isNotEmpty() ? problem : "CLICK A KEY, THEN PRESS THE NEW ONE.  ESC: CANCEL.  DELETE: NONE.", juce::dontSendNotification);
        rebuild();
    }

    // Pads, macros and scenes come and go (or get keys elsewhere): the list follows while shown.
    void timerCallback() override
    {
        if (isShowing() && capturing < 0 && signature (list()) != shown)
            rebuild();
    }

    std::function<std::vector<Entry>()> list;
    std::vector<Entry> entries;
    juce::String shown;
    std::vector<Row> rows;
    juce::Label note;
    int capturing = -1;
};

// The SETTINGS tab: its sections as keys down the left (GENERAL, LOOK, METERS, ...), the chosen
// one beside them. The pages belong to MainComponent; this only shows them.
class SettingsTab : public juce::Component
{
public:
    SettingsTab()
    {
        view.setScrollBarsShown (true, false);
        addAndMakeVisible (view);
    }

    ~SettingsTab() override { view.setViewedComponent (nullptr, false); }

    // fill: the page takes all the room beside the keys (MACROS); otherwise it keeps its own size,
    // scrolling down when it's taller.
    void addSection (const juce::String& name, juce::Component& page, bool fill)
    {
        auto* b = keys.add (new juce::TextButton (name.toUpperCase()));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
        b->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
        b->onClick = [this, i = keys.size() - 1] { show (i); };
        addAndMakeVisible (b);
        b->setVisible (false);   // (drawn as cards instead: paint)
        sections.push_back ({ name, &page, fill });
        if (fill)
            addChildComponent (page);
        if (sections.size() == 1)
            show (0);
    }

    void show (const juce::String& name)
    {
        for (size_t i = 0; i < sections.size(); ++i)
            if (sections[i].name.equalsIgnoreCase (name))
                show ((int) i);
    }

    // The sections as cards down the left (the one showing lit, a blue edge), the
    // section beside them on the dark desk, its option cards as wide as the room.
    void paint (juce::Graphics& g) override
    {
        g.fillAll (DeskSettings::desk());
        for (int i = 0; i < (int) sections.size(); ++i)
        {
            const auto r = sectionCard (i).toFloat();
            DeskSettings::card (g, r, i == shown, i == hovered);
            g.setColour (i == shown ? juce::Colours::white : DeskSettings::text());
            g.setFont (LargeFormat::print (12.0f));
            g.drawText (sections[(size_t) i].name.toUpperCase(), r.withTrimmedLeft (16.0f), juce::Justification::centredLeft, false);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { const int h = sectionAt (e.getPosition()); if (h != hovered) { hovered = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { if (const int i = sectionAt (e.getPosition()); i >= 0) show (i); }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (sectionCard (0).getRight() + 6);
        view.setBounds (r);
        const int w = r.getWidth() - view.getScrollBarThickness();
        for (auto& s : sections)
        {
            if (s.fill)
                s.page->setBounds (r);
            else if (auto* fills = dynamic_cast<DeskSettings::WidthFill*> (s.page))
                s.page->setSize (w, fills->heightFor (w));   // (its cards fill the width)
        }
    }

private:
    juce::Rectangle<int> sectionCard (int i) const { return { 14, 14 + i * 40, 150, 34 }; }
    int sectionAt (juce::Point<int> p) const
    {
        for (int i = 0; i < (int) sections.size(); ++i)
            if (sectionCard (i).contains (p))
                return i;
        return -1;
    }
    int hovered = -1;

    struct Section { juce::String name; juce::Component* page; bool fill; };

    void show (int index)
    {
        shown = index;
        for (int i = 0; i < keys.size(); ++i)
            keys[i]->setToggleState (i == index, juce::dontSendNotification);
        auto& s = sections[(size_t) index];
        for (auto& other : sections)
            if (other.fill)
                other.page->setVisible (other.page == s.page);
        view.setViewedComponent (s.fill ? nullptr : s.page, false);
        view.setVisible (! s.fill);
        resized();
        repaint();
        Console::riseIn (s.fill ? *s.page : (juce::Component&) view);   // the section rises into place
    }

    juce::OwnedArray<juce::TextButton> keys;
    std::vector<Section> sections;
    juce::Viewport view;
    int shown = -1;
};

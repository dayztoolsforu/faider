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

    void heading (const juce::String& text);

    void toggle (const juce::String& text, const juce::String& tip, std::function<bool()> get, std::function<void (bool)> set);

    // One of several, a key each (lit: the one chosen): down the page, or side by side on one row.
    void choice (const juce::StringArray& names, std::function<int()> get, std::function<void (int)> set, bool oneRow = false);

    void action (const juce::String& text, const juce::String& tip, std::function<void()> run);

    // An action whose key reads what `text` says now (read again with the rest).
    void actionShowing (std::function<juce::String()> text, const juce::String& tip, std::function<void()> run);

    // Done adding rows: sized to them.
    void finish();

    // Each heading and its rows a card; the cards fill the page in columns (each
    // into the shortest), as many columns as the width holds.
    int heightFor (int w) override { return arrange (w, false); }
    void paint (juce::Graphics& g) override;
    void resized() override { arrange (getWidth(), true); }

    int arrange (int w, bool apply);

private:
    struct Row { juce::Component* c; bool heading; std::vector<juce::Component*> more = {}; };   // more: keys beside c

    juce::TextButton& key (const juce::String& text, const juce::String& tip, bool besideLast = false);

    void refresh();

    void timerCallback() override { if (isShowing()) refresh(); }

    // A theme change: the colours set when the page was made, in the new theme's.
    void lookAndFeelChanged() override;

    static constexpr int width = 520;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::TextButton> keys;
    juce::OwnedArray<DeskSettings::Switch> switches;
    std::vector<juce::Rectangle<int>> cards;
    std::vector<Row> rows;
    std::vector<std::function<void()>> refreshers;
};

// HOTKEYS: every global hotkey in one list (pads, macros, scenes, instant replay, the overlay),
// grouped, each group with what its keys do and where else they're set; click a key, then press
// the new one (Esc: cancel, Delete: none). The keys that only work inside Faider are listed under
// it, fixed. A key given twice, or one Windows refused, says so in red under its row.
class HotkeysPage : public juce::Component, private juce::Timer
                  , public DeskSettings::WidthFill
{
public:
    struct Entry
    {
        juce::String group, name, key;                         // key: as juce::KeyPress describes it, or empty
        std::function<bool (const juce::KeyPress&)> set;       // false: refused (the key is taken); null: fixed
        juce::String where = {};                               // under the group's heading: what its keys do, where else they're set
        bool working = true;                                   // false: Windows refused the key
        juce::StringArray keys = {};                           // a fixed entry's keys, for the same-key check (none: it isn't checked)
    };

    explicit HotkeysPage (std::function<std::vector<Entry>()> listFn);

    // The note across the top, each group a card (a name, its key at the right),
    // the cards in columns.
    int heightFor (int w) override { return arrangeCards (w, false); }
    void paint (juce::Graphics& g) override;
    void resized() override { arrangeCards (getWidth(), true); }
    int arrangeCards (int w, bool apply);
    std::vector<juce::Rectangle<int>> cards;

    bool keyPressed (const juce::KeyPress& key) override;

    void focusLost (FocusChangeType) override;

private:
    struct Row
    {
        std::unique_ptr<juce::Label> heading, name;
        std::unique_ptr<juce::TextButton> keyButton;
    };

    static juce::String signature (const std::vector<Entry>& list);

    static bool sameKey (const juce::String& a, const juce::String& b);

    // The first other entry with this key (a fixed one: any of the keys it names), or -1.
    int holderOf (const juce::String& key, int except) const;

    // A line of its own in a group's card.
    void addLine (const juce::String& text, juce::Colour colour);

    void rebuild();

    // The note across the top: how to set a key, and how many are given twice.
    juce::String usualNote() const;

    void startCapturing (int index);

    void stopCapturing (const juce::String& whyNot);

    // Pads, macros and scenes come and go (or get keys elsewhere): the list follows while shown.
    void timerCallback() override;

    std::function<std::vector<Entry>()> list;
    std::vector<Entry> entries;
    juce::String shown;
    std::vector<Row> rows;
    juce::Label note;
    juce::String problem;   // why the last key wasn't taken, if it wasn't
    int capturing = -1, clashes = 0;
};

// The SETTINGS tab: its sections as keys down the left (GENERAL, LOOK, METERS, ...), the chosen
// one beside them. The pages belong to MainComponent; this only shows them.
class SettingsTab : public juce::Component
{
public:
    SettingsTab();

    ~SettingsTab() override { view.setViewedComponent (nullptr, false); }

    // fill: the page takes all the room beside the keys (MACROS); otherwise it keeps its own size,
    // scrolling down when it's taller.
    void addSection (const juce::String& name, juce::Component& page, bool fill);

    void show (const juce::String& name);

    // The sections as cards down the left (the one showing lit, a blue edge), the
    // section beside them on the dark desk, its option cards as wide as the room.
    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override { const int h = sectionAt (e.getPosition()); if (h != hovered) { hovered = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { if (const int i = sectionAt (e.getPosition()); i >= 0) show (i); }

    void resized() override;

private:
    juce::Rectangle<int> sectionCard (int i) const { return { 14, 14 + i * 40, 150, 34 }; }
    int sectionAt (juce::Point<int> p) const;
    int hovered = -1;

    struct Section { juce::String name; juce::Component* page; bool fill; };

    void show (int index);

    juce::OwnedArray<juce::TextButton> keys;
    std::vector<Section> sections;
    juce::Viewport view;
    int shown = -1;
};

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SettingsRegistry.h"

// The SETTINGS tab's parts: a row (a setting's name, what it does under it, its control at the
// right) and the controls a row can have: a switch, a choice of a few, a menu, a file path, a
// status chip, buttons, a number field and a table. Every colour comes from the look in force
// (Theme::get()) as it paints, and the type is the look's typeface, so a new look shows at once.
namespace SettingsUi
{
    juce::Font font (float height, bool strong = false);

    // The look's colours, as the settings use them.
    namespace Ink
    {
        juce::Colour page();       // the column the rows sit on
        juce::Colour sidebar();    // the darker panel down the left
        juce::Colour box();        // a row, a table
        juce::Colour field();      // a recessed field: search, a path, a number, a menu's box
        juce::Colour edge();       // a box's outline, a table's lines
        juce::Colour name();       // a setting's name, a page's title
        juce::Colour text();       // ordinary text: a control's, a table's
        juce::Colour dim();        // descriptions, section headings
        juce::Colour warning();    // amber: something to know first
        juce::Colour danger();     // red: a hotkey that isn't working
    }

    // Wrapped text: its height at `width`, and drawn in `area`.
    int textHeight (const juce::String& text, const juce::Font& f, int width);
    void drawText (juce::Graphics& g, const juce::String& text, const juce::Font& f, juce::Colour colour, juce::Rectangle<int> area);
    int textWidth (const juce::String& text, const juce::Font& f);

    // Something on a page that reads its values again while shown (SettingsTab, 4 times a second).
    struct Refreshing
    {
        virtual ~Refreshing() = default;
        virtual void refresh() = 0;
        virtual int preferredWidth() const { return 240; }
        virtual int heightFor (int width) const { (void) width; return 28; }
    };

    // ON / OFF: a pill, cream with a soft glow when on (the look's power lamp), grey when off.
    class Switch : public juce::Button
    {
    public:
        Switch() : juce::Button ({}) { setClickingTogglesState (false); }
        void paintButton (juce::Graphics& g, bool over, bool down) override;
        static constexpr int width = 42, height = 24;
    };

    // One of a few, side by side: the one chosen filled cream.
    class Segmented : public juce::Component
    {
    public:
        explicit Segmented (juce::StringArray names) : options (std::move (names)) {}
        std::function<void (int)> onChoose;
        void setChosen (int index) { if (index != chosen) { chosen = index; repaint(); } }
        int preferredWidth() const;
        void paint (juce::Graphics& g) override;
        void mouseMove (const juce::MouseEvent& e) override { setHovered (at (e.x)); }
        void mouseExit (const juce::MouseEvent&) override { setHovered (-1); }
        void mouseUp (const juce::MouseEvent& e) override;

    private:
        int at (int x) const;
        void setHovered (int i) { if (i != hovered) { hovered = i; repaint(); } }
        std::vector<int> widths() const;
        juce::StringArray options;
        int chosen = -1, hovered = -1;
    };

    // A plain button, or the primary one (the accent blue). Sentence case, as written.
    class Button : public juce::Button
    {
    public:
        explicit Button (const juce::String& text, bool isPrimary = false) : juce::Button (text), primary (isPrimary) {}
        void paintButton (juce::Graphics& g, bool over, bool down) override;
        int preferredWidth() const { return textWidth (getButtonText(), font (13.0f, true)) + 28; }
        bool primary;
    };

    // A value that opens a menu (the look, the clock, the meter standard): its box, a chevron.
    class Dropdown : public juce::Button
    {
    public:
        Dropdown() : juce::Button ({}) {}
        void paintButton (juce::Graphics& g, bool over, bool down) override;
        int preferredWidth() const { return juce::jlimit (180, 320, textWidth (getButtonText(), font (13.0f)) + 48); }
    };

    // A status, read again while shown (WINDOWS AUDIO's priority, the version).
    class Chip : public juce::Component
    {
    public:
        void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
        int preferredWidth() const { return textWidth (text, font (12.5f, true)) + 34; }
        void paint (juce::Graphics& g) override;
    private:
        juce::String text;
    };

    // A file's path (or NOT FOUND), its start dropped when it doesn't fit.
    class PathField : public juce::Component
    {
    public:
        void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
        void paint (juce::Graphics& g) override;
    private:
        juce::String text;
    };

    // A number typed in, then APPLY (the OSC port). What's typed stays until it's applied (Apply,
    // Enter) or dropped (Escape): reading the value again never writes over it.
    class NumberField : public juce::Component, public Refreshing
    {
    public:
        NumberField (std::function<int()> get, std::function<void (int)> apply, int digits);
        void refresh() override;
        int preferredWidth() const override { return 96 + 8 + applyKey.preferredWidth(); }
        void resized() override;
    private:
        void style();
        void applyTyped();
        std::function<int()> value;
        std::function<void (int)> onApply;
        juce::TextEditor editor;
        Button applyKey { TRANS ("Apply") };   // (made with the field, after the language is loaded)
        int lookShown = -1;
        bool typed = false;   // (an edit not applied yet)
    };

    // A table: a header row, then lines, each line's buttons in the last column; a line can have a
    // note under it (in red: a hotkey that isn't working). A button runs once its click is over
    // (what it does may change the table).
    class Table : public juce::Component, public Refreshing
    {
    public:
        struct Column { juce::String title; float weight; };
        struct LineAction
        {
            juce::String text;
            std::function<void (juce::Component& button)> run;
            bool primary = false;
            bool enabled = true;
        };
        struct Line
        {
            juce::StringArray cells;
            std::vector<LineAction> actions = {};
            juce::String note = {};
            bool dim = false;    // a fixed line: its text dimmer
        };

        Table (std::vector<Column> columns, juce::String emptyText, std::function<std::vector<Line>()> source = {});

        // The lines now (the buttons are made again only when something in them changed).
        void setLines (std::vector<Line> lines);
        void refresh() override { if (source) setLines (source()); }
        int heightFor (int width) const override;
        void paint (juce::Graphics& g) override;
        void resized() override;

        static constexpr int headerHeight = 32, lineHeight = 38, noteHeight = 20;

    private:
        static juce::String signature (const std::vector<Line>& lines);
        std::vector<juce::Rectangle<int>> cellAreas (juce::Rectangle<int> line) const;
        std::vector<Column> columns;
        juce::String empty, shown;
        std::function<std::vector<Line>()> source;
        std::vector<Line> lines;
        juce::OwnedArray<Button> buttons;
        std::vector<std::pair<int, int>> buttonOf;   // each button's line and action
    };

    // A setting's row: a rounded box, its name and what it does at the left (a note and an amber
    // warning under them while they say something), its control at the right, or under the text
    // when it's wide (a table) or there's no room beside it.
    class Row : public juce::Component
    {
    public:
        explicit Row (const SettingsRegistry::Setting& s, const juce::String& pageAbove = {});

        const SettingsRegistry::Setting& setting;

        int heightFor (int width);
        // Reads the setting again; true if the row's height may have changed (a note or warning came or went).
        bool refresh();
        void flash();   // FIND brought it here: lit for a moment
        bool passesThrough (juce::Point<int> screen) const;

        void paint (juce::Graphics& g) override;
        void resized() override { layout (getWidth(), true); }

    private:
        int layout (int width, bool apply);
        int controlsWidth() const;

        juce::String above;   // search results: the page it's on, over the box
        std::unique_ptr<Switch> toggle;
        std::unique_ptr<Segmented> segmented;
        std::unique_ptr<Dropdown> dropdown;
        std::unique_ptr<Chip> chip;
        std::unique_ptr<PathField> pathBox;
        juce::OwnedArray<Button> buttons;
        std::unique_ptr<juce::Component> custom;
        juce::String noteText, warningText;
        juce::Rectangle<int> box, nameArea, descArea, noteArea, warnArea, aboveArea;
        float lit = 0.0f;
        int customHeight = 0;
        struct Fade : juce::Timer { std::function<void()> tick; void timerCallback() override { tick(); } } fade;
    };

    // A section's heading between rows.
    class Heading : public juce::Component
    {
    public:
        explicit Heading (const juce::String& t) : text (t) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override;
        static constexpr int height = 40;
    private:
        juce::String text;
    };
}

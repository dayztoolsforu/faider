#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Console.h"
#include "Hotkeys.h"
#include "settings/SettingsRegistry.h"
#include "settings/SettingsWidgets.h"

// HOTKEYS: every global hotkey in two tables. ASSIGNED: the pads', macros' and scenes' keys and
// instant replay's, each with where else it's set; click its key, then press the new one (Esc:
// cancel, Delete: none). FIXED: the keys that are always the same (the overlay's, the window's,
// the mixer's). A key given twice, or one Windows refused, says so in red under its line.
class HotkeysPage : public juce::Component, private juce::Timer
{
public:
    struct Entry
    {
        juce::String group, name, key;                         // key: as juce::KeyPress describes it, or empty
        std::function<bool (const juce::KeyPress&)> set;       // false: refused (the key is taken); null: fixed
        juce::String where = {};                               // ASSIGNED: where else it's set; FIXED: where it works
        bool working = true;                                   // false: Windows refused the key
        juce::StringArray keys = {};                           // a fixed entry's keys, for the same-key check (none: it isn't checked)
    };

    explicit HotkeysPage (std::function<std::vector<Entry>()> listFn);

    std::function<void()> onHeightChanged;   // (the page it's on lays itself out again)

    int heightFor (int width) const;
    void paint (juce::Graphics& g) override;
    void resized() override;

    bool keyPressed (const juce::KeyPress& key) override;
    void focusLost (FocusChangeType) override;

    // "ctrl + shift + O": "Ctrl + Shift + O".
    static juce::String keyText (const juce::String& description);

private:
    static juce::String signature (const std::vector<Entry>& list);
    static bool sameKey (const juce::String& a, const juce::String& b);

    // The first other entry with this key (a fixed one: any of the keys it names), or -1.
    int holderOf (const juce::String& key, int except) const;

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
    SettingsUi::Table assigned { { { "What", 0.42f }, { "Set also in", 0.36f }, { "Key", 0.22f } }, "Nothing to give a key yet: add pads, macros or scenes" };
    SettingsUi::Table fixed { { { "What", 0.42f }, { "Where", 0.30f }, { "Key", 0.28f } }, {} };
    juce::String note;
    juce::String problem;   // why the last key wasn't taken, if it wasn't
    int capturing = -1, clashes = 0;
    static constexpr int headingHeight = 40;
};

// The SETTINGS tab: a sidebar down the left (a search box, then the pages under their groups), and
// the page beside it: its title, a line on what it's for, then its settings as rows in one column
// that scrolls. Typing in the search box shows the rows that match from every page instead, each
// under its page's name. The rows and FIND's settings come from one list (SettingsRegistry.h); the
// values are read again 4 times a second while it shows, so a change made elsewhere shows here too.
class SettingsTab : public juce::Component, private juce::Timer
{
public:
    SettingsTab (std::vector<SettingsRegistry::Page> pages, std::vector<SettingsRegistry::Setting> settings);
    ~SettingsTab() override;

    const std::vector<SettingsRegistry::Page>& getPages() const { return pages; }
    const std::vector<SettingsRegistry::Setting>& getSettings() const { return settings; }

    void show (const juce::String& pageName);
    // FIND: the setting's page, scrolled to it, its row lit for a moment.
    void showSetting (const juce::String& pageName, const juce::String& settingName);

    // Whether a point on the screen is on a control that stays usable while MIDI MAP is on.
    bool passesThrough (juce::Point<int> screen) const;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent& e) override;

    static constexpr int sidebarWidth = 232, columnWidth = 760;

private:
    // The column the viewport scrolls: the title and intro (painted), then headings and rows.
    struct Column : juce::Component
    {
        juce::String title, intro;
        juce::Rectangle<int> titleArea, introArea;
        void paint (juce::Graphics& g) override;
    };

    void showPage (int index);
    void build();          // the rows for the page, or for the search
    void layoutColumn();
    int linkAt (juce::Point<int> p) const;
    juce::Rectangle<int> linkArea (int page) const;
    juce::Rectangle<int> groupArea (const juce::String& group) const;
    juce::Rectangle<int> pageArea() const { return getLocalBounds().withTrimmedLeft (sidebarWidth); }
    void styleSearch();
    void timerCallback() override;

    std::vector<SettingsRegistry::Page> pages;
    std::vector<SettingsRegistry::Setting> settings;
    int current = 0, hovered = -1, lookShown = -1;
    juce::TextEditor search;
    juce::Viewport view;
    Column column;
    juce::OwnedArray<SettingsUi::Row> rows;
    juce::OwnedArray<SettingsUi::Heading> headings;
    juce::Component* ownShown = nullptr;   // HOTKEYS' page in the column, MACROS' beside it
};

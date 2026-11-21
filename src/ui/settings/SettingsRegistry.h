#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "../../ProductName.h"

// THE SETTINGS, one list: every row the SETTINGS tab shows and FIND (Ctrl+K) finds by name. Each
// says where it lives (its page and section), what it's called and what it does, the control it
// has and how that reads and sets it. The tab builds its pages from it and FIND its "settings"
// commands, so a setting is added in one place (MainComponentSettings.cpp).
namespace SettingsRegistry
{
    // A page's group, a page's, a section's or a setting's name is English, the identifier that
    // openSettings, showSetting and FIND find it by (NEEDS_TRANS where it's written); %1 in it is
    // the product's name ("About %1"). Shown: translated. English: as the log and the code have it.
    inline juce::String shown (const juce::String& name)   { return juce::translate (name).replace ("%1", Product::name); }
    inline juce::String english (const juce::String& name) { return name.replace ("%1", Product::name); }

    enum class Kind
    {
        toggle,      // a switch: get / set
        segmented,   // one of a few: options, chosen / choose
        dropdown,    // a value that opens a menu: value / menu
        actions,     // buttons only (with a status chip or a file path beside them, if given)
        custom       // its own control (a table, a number field): make
    };

    // A button on a row: its text read again with the rest (it can change: CHECK NOW, GET 0.2.0...).
    struct Action
    {
        std::function<juce::String()> text;
        std::function<void (juce::Component& button)> run;   // (the button: a menu shown under it)
        bool primary = false;                                // the accent blue: the thing to do
        std::function<bool()> enabled = {};                  // (none: always)
    };

    struct Setting
    {
        juce::String page, section, name;   // English identifiers, shown translated (shown())
        juce::String description;           // translated where it's made (TRANS)
        juce::String keywords = {};    // more words FIND and the search box match (not shown, English)
        Kind kind = Kind::actions;

        std::function<bool()> get = {};                  // toggle
        std::function<void (bool)> set = {};

        juce::StringArray options = {};                  // segmented
        std::function<int()> chosen = {};
        std::function<void (int)> choose = {};

        std::function<juce::String()> value = {};       // dropdown: what it shows
        std::function<void (juce::Component&)> menu = {};   // (shows its menu under the control)

        std::function<juce::String()> status = {};      // actions: a chip, or a path box (`path`)
        bool path = false;
        std::vector<Action> actions = {};               // beside any control (TIPS: SHOW AGAIN)

        std::function<std::unique_ptr<juce::Component>()> make = {};   // custom
        bool wide = false;                              // the control goes under the text, the row's width

        std::function<juce::String()> note = {};        // a dim line under the description, read again (NOW: ...)
        std::function<juce::String()> warning = {};     // an amber line, shown while it says something
        bool passThrough = false;                       // its controls stay usable while MIDI MAP is on

        // Whether every word of `query` is in its name, description, keywords, section or page, as
        // shown or in English (so a search finds it in either).
        bool matches (const juce::String& query) const
        {
            const auto text = (shown (name) + " " + description + " " + keywords + " " + shown (section) + " " + shown (page) + " "
                               + english (name) + " " + english (section) + " " + english (page)).toLowerCase();
            const auto words = juce::StringArray::fromTokens (query.toLowerCase().trim(), " ", {});
            for (auto& w : words)
                if (! text.contains (w))
                    return false;
            return ! words.isEmpty();
        }
    };

    // A page in the sidebar, under its group (APP, AUDIO, MIX, CONTROL). A page whose rows are its own
    // (HOTKEYS) or that fills the room (MACROS) gives its component.
    struct Page
    {
        juce::String group, name;    // English identifiers, shown translated (shown())
        juce::String intro;          // translated where it's made (TRANS)
        juce::Component* own = nullptr;   // shown under the intro instead of rows
        bool fill = false;                // `own` takes all the room below the intro (no scrolling)
        juce::String keywords = {};       // more words FIND and the search box match for the page

        // Whether every word of `query` is in its name, group or keywords, as shown or in English
        // (the search box shows the page among the rows: "hotkey", "shortcut" find HOTKEYS).
        bool matches (const juce::String& query) const
        {
            const auto text = (shown (name) + " " + shown (group) + " " + english (name) + " " + english (group) + " " + keywords).toLowerCase();
            const auto words = juce::StringArray::fromTokens (query.toLowerCase().trim(), " ", {});
            for (auto& w : words)
                if (! text.contains (w))
                    return false;
            return ! words.isEmpty();
        }
    };
}

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Console.h"
#include "../engine/PluginScan.h"
#include "../ProductName.h"

// ADD PLUGIN's picker: a search box over the installed plugins, with FAVOURITES (click a row's
// star) and RECENT (the last ones added) at the top, then everything; LOAD VST3 FILE... at the
// bottom. Type to narrow it, Enter adds the top match (or the one picked with the arrows).
// Favourites and recent are kept in %APPDATA%\Faider\plugin-picker.xml.
class PluginPicker : public juce::Component, private juce::ListBoxModel, private juce::KeyListener
{
public:
    PluginPicker (juce::StringArray installed, std::function<void (const juce::String& path)> pick, std::function<void()> loadFile)
        : plugins (std::move (installed)), onPick (std::move (pick)), onLoadFile (std::move (loadFile))
    {
        search.setTextToShowWhenEmpty ("SEARCH PLUGINS", Console::Colours::legendDim());
        search.setFont (juce::FontOptions (Console::Type::body));
        search.onTextChange = [this] { refill(); };
        search.onReturnKey = [this] { pickRow (juce::jmax (0, list.getSelectedRow())); };
        search.setWantsKeyboardFocus (true);
        search.addKeyListener (this);   // (the arrows reach the list before the search box takes them)
        addAndMakeVisible (search);

        list.setModel (this);
        list.setRowHeight (26);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (list);

        loadFileKey.onClick = [this] { close(); if (onLoadFile) onLoadFile(); };
        addAndMakeVisible (loadFileKey);

        refill();
        setSize (440, 480);
    }

    // A plugin added (from here or anywhere): it goes to the top of RECENT.
    static void remember (const juce::String& path)
    {
        auto recent = load ("recent");
        recent.removeString (path);
        recent.insert (0, path);
        recent.removeRange (8, recent.size());
        save ("recent", recent);
    }

    void visibilityChanged() override
    {
        if (isShowing())
            search.grabKeyboardFocus();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (Console::Space::pad);
        search.setBounds (r.removeFromTop (Console::Space::key + 4));
        r.removeFromTop (Console::Space::unit);
        loadFileKey.setBounds (r.removeFromBottom (Console::Space::key));
        r.removeFromBottom (Console::Space::unit);
        list.setBounds (r);
    }

    void paint (juce::Graphics& g) override { g.fillAll (Console::Colours::panelDark()); }

    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        // The arrows move through the list while typing in the search box.
        if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey)
        {
            int row = list.getSelectedRow() + (key == juce::KeyPress::downKey ? 1 : -1);
            while (juce::isPositiveAndBelow (row, (int) rows.size()) && rows[(size_t) row].header)
                row += key == juce::KeyPress::downKey ? 1 : -1;
            if (juce::isPositiveAndBelow (row, (int) rows.size()))
            {
                list.selectRow (row);
                list.scrollToEnsureRowIsOnscreen (row);
            }
            return true;
        }
        return false;
    }

private:
    struct Row { bool header = false; juce::String text, path, maker; };

    static juce::File storeFile()
    {
        return Product::dataFolder().getChildFile ("plugin-picker.xml");
    }
    static juce::StringArray load (const juce::String& what)
    {
        juce::StringArray list;
        if (auto xml = juce::XmlDocument::parse (storeFile()))
            if (auto* e = xml->getChildByName (what))
                list.addLines (e->getAllSubText());
        list.removeEmptyStrings();
        return list;
    }
    static void save (const juce::String& what, const juce::StringArray& list)
    {
        auto xml = juce::XmlDocument::parse (storeFile());
        if (xml == nullptr)
            xml = std::make_unique<juce::XmlElement> ("PluginPicker");
        if (auto* old = xml->getChildByName (what))
            xml->removeChildElement (old, true);
        xml->createNewChildElement (what)->addTextElement (list.joinIntoString ("\n"));
        storeFile().getParentDirectory().createDirectory();
        xml->writeTo (storeFile());
    }

    static juce::String nameOf (const juce::String& path) { return juce::File (path).getFileNameWithoutExtension(); }

    void refill()
    {
        const auto query = search.getText().trim();
        auto matches = [&query] (const juce::String& path, const juce::String& maker)
        {
            return query.isEmpty() || nameOf (path).containsIgnoreCase (query) || maker.containsIgnoreCase (query);
        };
        auto makerOf = [] (const juce::String& path)
        {
            const auto known = PluginScan::find (path);
            return known ? (known->passed ? known->maker : juce::String ("FAILED TEST")) : juce::String();
        };
        rows.clear();
        favourites = load ("favourites");
        auto section = [&] (const juce::String& title, const juce::StringArray& paths)
        {
            bool any = false;
            for (auto& p : paths)
                if (plugins.contains (p) && matches (p, makerOf (p)))
                {
                    if (! any)
                        rows.push_back ({ true, title, {}, {} });
                    any = true;
                    rows.push_back ({ false, nameOf (p), p, makerOf (p) });
                }
        };
        section (juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85 FAVOURITES")), favourites);
        if (query.isEmpty())
            section ("RECENT", load ("recent"));
        section (query.isEmpty() ? "ALL PLUGINS" : "MATCHES", plugins);
        if (plugins.isEmpty())
            rows.push_back ({ true, "NO VST3 PLUGINS FOUND", {}, {} });
        list.updateContent();
        list.repaint();
        for (size_t i = 0; i < rows.size(); ++i)   // the top match picked, ready for Enter
            if (! rows[i].header)
            {
                list.selectRow ((int) i);
                break;
            }
    }

    int getNumRows() override { return (int) rows.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()))
            return;
        using namespace Console;
        const auto& r = rows[(size_t) row];
        auto area = juce::Rectangle<int> (width, height).reduced (2, 1);
        if (r.header)
        {
            g.setColour (Colours::legendDim());
            g.setFont (legendFont (Type::legend));
            g.drawText (r.text, area.withTrimmedTop (6), juce::Justification::bottomLeft);
            return;
        }
        if (selected)
        {
            g.setColour (Colours::selected().withAlpha (0.22f));
            g.fillRoundedRectangle (area.toFloat(), 3.0f);
            g.setColour (Colours::selected());
            g.fillRect (area.withWidth (3));
        }
        const bool fav = favourites.contains (r.path);
        auto star = area.removeFromRight (26);
        g.setColour (fav ? Colours::caution() : Colours::legendDim().withAlpha (0.6f));
        g.setFont (juce::FontOptions (16.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 (fav ? "\xe2\x98\x85" : "\xe2\x98\x86")), star, juce::Justification::centred);
        area.removeFromLeft (8);
        g.setColour (Colours::legend());
        g.setFont (juce::FontOptions (Type::body, juce::Font::bold));
        g.drawFittedText (r.text, area.removeFromLeft (area.getWidth() * 3 / 5), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (r.maker == "FAILED TEST" ? Colours::danger() : Colours::legendDim());
        g.setFont (juce::FontOptions (Type::legend));
        g.drawFittedText (r.maker.toUpperCase(), area, juce::Justification::centredRight, 1, 0.8f);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].header)
            return;
        if (e.x > list.getWidth() - 34)   // the star: a favourite, or not
        {
            const auto path = rows[(size_t) row].path;
            if (favourites.contains (path)) favourites.removeString (path); else favourites.add (path);
            save ("favourites", favourites);
            refill();
            return;
        }
        pickRow (row);
    }

    void pickRow (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].header)
            return;
        const auto path = rows[(size_t) row].path;
        close();
        if (onPick)
            onPick (path);
    }

    void close()
    {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }

    juce::StringArray plugins, favourites;
    std::vector<Row> rows;
    std::function<void (const juce::String&)> onPick;
    std::function<void()> onLoadFile;
    juce::TextEditor search;
    juce::ListBox list;
    juce::TextButton loadFileKey { "LOAD VST3 FILE..." };
};

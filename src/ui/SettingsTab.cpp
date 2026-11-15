#include "SettingsTab.h"

using namespace SettingsUi;

//==============================================================================
HotkeysPage::HotkeysPage (std::function<std::vector<Entry>()> listFn) : list (std::move (listFn))
{
    addAndMakeVisible (assigned);
    addAndMakeVisible (fixed);
    rebuild();
    startTimerHz (1);
}

juce::String HotkeysPage::keyText (const juce::String& description)
{
    auto words = juce::StringArray::fromTokens (description, " ", {});
    for (auto& w : words)
        w = w.substring (0, 1).toUpperCase() + w.substring (1);
    return words.joinIntoString (" ");
}

int HotkeysPage::heightFor (int width) const
{
    return textHeight (note, font (13.0f), width) + 4 + headingHeight + assigned.heightFor (width) + headingHeight + fixed.heightFor (width);
}

void HotkeysPage::resized()
{
    int y = textHeight (note, font (13.0f), getWidth()) + 4 + headingHeight;
    assigned.setBounds (0, y, getWidth(), assigned.heightFor (getWidth()));
    y = assigned.getBottom() + headingHeight;
    fixed.setBounds (0, y, getWidth(), fixed.heightFor (getWidth()));
}

void HotkeysPage::paint (juce::Graphics& g)
{
    const int noteH = textHeight (note, font (13.0f), getWidth());
    drawText (g, note, font (13.0f, problem.isNotEmpty()), problem.isNotEmpty() ? Ink::warning() : Ink::dim(), { 0, 0, getWidth(), noteH });
    g.setColour (Ink::dim());
    g.setFont (font (13.0f, true));
    g.drawText ("Assigned", juce::Rectangle<int> (2, noteH + 4, getWidth(), headingHeight - 8), juce::Justification::bottomLeft, true);
    g.drawText ("Fixed", juce::Rectangle<int> (2, assigned.getBottom(), getWidth(), headingHeight - 8), juce::Justification::bottomLeft, true);
}

bool HotkeysPage::keyPressed (const juce::KeyPress& key)
{
    if (capturing < 0)
        return false;
    if (key == juce::KeyPress::escapeKey)
        stopCapturing ({});
    else if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        stopCapturing (entries[(size_t) capturing].set ({}) ? juce::String() : "Couldn't clear it");
    else if (! GlobalHotkeys::usable (key))
    {
        note = "Needs Ctrl, Alt or an F key";
        repaint();
    }
    else
    {
        // Refused: what has the key here keeps it (it isn't even tried); or Windows won't give it, so another app has it.
        const auto text = keyText (key.getTextDescription());
        if (const int holder = holderOf (key.getTextDescription(), capturing); holder >= 0)
            stopCapturing (text + " is already the key for " + entries[(size_t) holder].name);
        else
            stopCapturing (entries[(size_t) capturing].set (key) ? juce::String() : text + " is taken by another app");
    }
    return true;
}

void HotkeysPage::focusLost (FocusChangeType)
{
    if (capturing >= 0)
        stopCapturing ({});
}

juce::String HotkeysPage::signature (const std::vector<Entry>& list)
{
    juce::String s;
    for (auto& e : list)
        s << e.group << "|" << e.name << "|" << e.key << "|" << e.where << "|" << (e.working ? "" : "refused") << "\n";
    return s;
}

bool HotkeysPage::sameKey (const juce::String& a, const juce::String& b)
{
    const auto x = juce::KeyPress::createFromDescription (a), y = juce::KeyPress::createFromDescription (b);
    return a.isNotEmpty() && b.isNotEmpty() && x.isValid() && x == y;
}

int HotkeysPage::holderOf (const juce::String& key, int except) const
{
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        if (i == except)
            continue;
        if (e.set != nullptr ? sameKey (e.key, key)
                             : std::any_of (e.keys.begin(), e.keys.end(), [&key] (const juce::String& k) { return sameKey (k, key); }))
            return i;
    }
    return -1;
}

void HotkeysPage::rebuild()
{
    entries = list();
    shown = signature (entries);
    clashes = 0;
    std::vector<Table::Line> settable, always;
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        auto& e = entries[(size_t) i];

        // The same key twice (in a mix from before a second one was refused: only one of them gets
        // it), or a key Windows wouldn't give. A fixed key that isn't working: who has it here.
        juce::String red;
        int other = -1;
        if (e.key.isNotEmpty() && e.set != nullptr)
            other = holderOf (e.key, i);
        else if (e.key.isNotEmpty() && ! e.working)
            for (auto& k : e.keys)
                if (other < 0)
                    other = holderOf (k, i);
        if (other >= 0 && (e.working || entries[(size_t) other].working))
        {
            ++clashes;
            red = e.working ? "Same key as " + entries[(size_t) other].name + ": only this one works"
                            : "Not working: " + entries[(size_t) other].name + " has this key";
        }
        else if (e.key.isNotEmpty() && ! e.working)
            red = "Not working: Windows refused the key (another app may have it)";

        if (e.set != nullptr)
        {
            const auto shownKey = capturing == i ? juce::String (juce::CharPointer_UTF8 ("Press a key\xe2\x80\xa6"))
                                                 : e.key.isEmpty() ? juce::String ("None") : keyText (e.key);
            settable.push_back ({ { e.name, e.where, juce::String() }, { { shownKey, [this, i] (juce::Component&) { startCapturing (i); }, capturing == i } }, red });
        }
        else
            always.push_back ({ { e.name, e.where, keyText (e.key) }, {}, red, true });
    }
    assigned.setLines (std::move (settable));
    fixed.setLines (std::move (always));
    note = problem.isNotEmpty() ? problem : usualNote();
    resized();
    repaint();
    if (onHeightChanged)
        onHeightChanged();
}

juce::String HotkeysPage::usualNote() const
{
    return "Click a key, then press the new one. Esc: cancel. Delete: none. A hotkey needs Ctrl, Alt or an F key, and works while a game has focus."
         + (clashes > 0 ? "  " + juce::String (clashes) + (clashes == 1 ? " shares a key (in red)." : " share a key (in red).") : juce::String());
}

void HotkeysPage::startCapturing (int index)
{
    capturing = index;
    problem.clear();
    rebuild();
    setWantsKeyboardFocus (true);
    grabKeyboardFocus();
}

void HotkeysPage::stopCapturing (const juce::String& whyNot)
{
    capturing = -1;
    setWantsKeyboardFocus (false);
    problem = whyNot;   // (shown until the next key is clicked)
    rebuild();
}

void HotkeysPage::timerCallback()
{
    if (isShowing() && capturing < 0 && signature (list()) != shown)
        rebuild();
}

//==============================================================================
void SettingsTab::Column::paint (juce::Graphics& g)
{
    g.fillAll (Ink::page());
    g.setColour (Ink::name());
    g.setFont (font (22.0f, true));
    g.drawText (title, titleArea, juce::Justification::centredLeft, true);
    drawText (g, intro, font (13.0f), Ink::dim(), introArea);
}

SettingsTab::SettingsTab (std::vector<SettingsRegistry::Page> p, std::vector<SettingsRegistry::Setting> s)
    : pages (std::move (p)), settings (std::move (s))
{
    search.setTextToShowWhenEmpty ("Search settings", Ink::dim());
    search.setIndents (12, 0);
    search.setJustification (juce::Justification::centredLeft);
    search.onTextChange = [this] { build(); };
    search.onEscapeKey = [this] { search.clear(); build(); };
    addAndMakeVisible (search);
    styleSearch();

    view.setScrollBarsShown (true, false);
    view.setViewedComponent (&column, false);
    addAndMakeVisible (view);
    showPage (0);
    startTimerHz (4);
}

SettingsTab::~SettingsTab()
{
    if (ownShown != nullptr && ownShown->getParentComponent() != nullptr)
        ownShown->getParentComponent()->removeChildComponent (ownShown);   // (its owner keeps it)
    view.setViewedComponent (nullptr, false);
}

void SettingsTab::styleSearch()
{
    lookShown = Theme::generation();
    search.setFont (font (13.5f));
    search.setColour (juce::TextEditor::backgroundColourId, Ink::field());
    search.setColour (juce::TextEditor::outlineColourId, Ink::edge());
    search.setColour (juce::TextEditor::focusedOutlineColourId, Theme::get().accent);
    search.setColour (juce::TextEditor::textColourId, Ink::name());
    search.setTextToShowWhenEmpty ("Search settings", Ink::dim());
    search.applyFontToAllText (font (13.5f));
    search.applyColourToAllText (Ink::name(), true);
    view.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, Theme::get().chromeHover);
}

void SettingsTab::show (const juce::String& pageName)
{
    for (int i = 0; i < (int) pages.size(); ++i)
        if (pages[(size_t) i].name.equalsIgnoreCase (pageName))
        {
            if (search.getText().isNotEmpty())
                search.clear();
            showPage (i);
            return;
        }
}

void SettingsTab::showSetting (const juce::String& pageName, const juce::String& settingName)
{
    show (pageName);
    for (auto* r : rows)
        if (r->setting.name == settingName)
        {
            view.setViewPosition (0, juce::jmax (0, r->getY() - 24));
            r->flash();
        }
}

bool SettingsTab::passesThrough (juce::Point<int> screen) const
{
    for (auto* r : rows)
        if (r->passesThrough (screen))
            return true;
    return false;
}

void SettingsTab::showPage (int index)
{
    current = juce::jlimit (0, (int) pages.size() - 1, index);
    build();
    repaint();
    Console::riseIn (pages[(size_t) current].fill && ownShown != nullptr ? *ownShown : (juce::Component&) view);   // the page rises into place
}

void SettingsTab::build()
{
    rows.clear();
    headings.clear();
    if (ownShown != nullptr && ownShown->getParentComponent() != nullptr)
        ownShown->getParentComponent()->removeChildComponent (ownShown);
    ownShown = nullptr;

    const auto query = search.getText().trim();
    if (query.isNotEmpty())
    {
        // Every page's rows that match, each under its page's name.
        int found = 0;
        for (auto& s : settings)
            if (s.matches (query))
            {
                column.addAndMakeVisible (rows.add (new Row (s, s.page)));
                ++found;
            }
        column.title = "Search";
        column.intro = found > 0 ? juce::String (found) + (found == 1 ? " setting matches." : " settings match.")
                                 : juce::String ("Nothing matches \"") + query + "\". Try another word.";
    }
    else
    {
        const auto& page = pages[(size_t) current];
        column.title = page.name;
        column.intro = page.intro;
        juce::String section;
        for (auto& s : settings)
        {
            if (s.page != page.name)
                continue;
            if (s.section != section)
            {
                section = s.section;
                if (section.isNotEmpty())
                    column.addAndMakeVisible (headings.add (new Heading (section)));
            }
            column.addAndMakeVisible (rows.add (new Row (s)));
        }
        if (page.own != nullptr)
        {
            ownShown = page.own;
            if (page.fill)
                addAndMakeVisible (ownShown);
            else
            {
                column.addAndMakeVisible (ownShown);
                if (auto* hotkeys = dynamic_cast<HotkeysPage*> (ownShown))
                    hotkeys->onHeightChanged = [this] { layoutColumn(); };
            }
        }
    }
    view.setViewPosition (0, 0);
    resized();
    repaint();
}

void SettingsTab::layoutColumn()
{
    const bool fill = search.getText().trim().isEmpty() && pages[(size_t) current].fill;
    const int viewW = view.getWidth() - (fill ? 0 : view.getScrollBarThickness());
    const int w = juce::jmin (columnWidth, viewW - 2 * 32);
    const int x = 32;
    int y = 28;
    column.titleArea = { x, y, w, 32 };
    y += 36;
    const int introH = textHeight (column.intro, font (13.0f), w);
    column.introArea = { x, y, w, introH };
    y += introH + 16;

    // The headings and rows in the settings' order (a heading before its section's first row).
    int h = 0;
    juce::String section;
    const bool searching = search.getText().trim().isNotEmpty();
    for (auto* r : rows)
    {
        if (! searching && r->setting.section != section)
        {
            section = r->setting.section;
            if (section.isNotEmpty() && h < headings.size())
            {
                headings[h++]->setBounds (x, y, w, Heading::height);
                y += Heading::height;
            }
        }
        const int rh = r->heightFor (w);
        r->setBounds (x, y, w, rh);
        y += rh + (searching ? 14 : 8);
    }
    if (ownShown != nullptr && ! fill)
    {
        y += 8;
        int ownH = 200;
        if (auto* hotkeys = dynamic_cast<HotkeysPage*> (ownShown))
            ownH = hotkeys->heightFor (w);
        ownShown->setBounds (x, y, w, ownH);
        y += ownH;
    }
    column.setSize (juce::jmax (0, viewW), fill ? y : y + 32);
    column.repaint();
}

void SettingsTab::resized()
{
    search.setBounds (16, 16, sidebarWidth - 32, 34);
    const auto area = pageArea();
    const bool fill = search.getText().trim().isEmpty() && pages[(size_t) current].fill && ownShown != nullptr;
    if (fill)
    {
        // The title and intro in the view's place, the page under them, filling the rest.
        view.setBounds (area);
        layoutColumn();
        view.setBounds (area.withHeight (column.getHeight()));
        ownShown->setBounds (area.withTrimmedTop (column.getHeight()));
    }
    else
    {
        view.setBounds (area);
        layoutColumn();
    }
}

juce::Rectangle<int> SettingsTab::linkArea (int page) const
{
    int y = 66;
    juce::String group;
    for (int i = 0; i < (int) pages.size(); ++i)
    {
        if (pages[(size_t) i].group != group)
        {
            group = pages[(size_t) i].group;
            y += (i > 0 ? 10 : 0) + 28;
        }
        if (i == page)
            return { 10, y, sidebarWidth - 20, 34 };
        y += 36;
    }
    return {};
}

juce::Rectangle<int> SettingsTab::groupArea (const juce::String& group) const
{
    for (int i = 0; i < (int) pages.size(); ++i)
        if (pages[(size_t) i].group == group)
            return linkArea (i).withHeight (28).translated (8, -28);
    return {};
}

int SettingsTab::linkAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) pages.size(); ++i)
        if (linkArea (i).contains (p))
            return i;
    return -1;
}

void SettingsTab::paint (juce::Graphics& g)
{
    g.fillAll (Ink::page());
    auto side = getLocalBounds().removeFromLeft (sidebarWidth);
    g.setColour (Ink::sidebar());
    g.fillRect (side);
    g.setColour (Ink::edge());
    g.fillRect (side.removeFromRight (1));

    const bool searching = search.getText().trim().isNotEmpty();
    juce::String group;
    for (int i = 0; i < (int) pages.size(); ++i)
    {
        const auto& p = pages[(size_t) i];
        if (p.group != group)
        {
            group = p.group;
            g.setColour (Ink::dim());
            g.setFont (font (11.5f, true));
            g.drawText (group.toUpperCase(), groupArea (group), juce::Justification::centredLeft, false);
        }
        const auto r = linkArea (i).toFloat();
        const bool on = i == current && ! searching;
        if (on || i == hovered)
        {
            g.setColour (on ? Theme::get().chromeOn : Theme::get().chromeHover);
            g.fillRoundedRectangle (r, 6.0f);
        }
        if (on)
        {
            g.setColour (Theme::get().accent);
            g.fillRoundedRectangle (r.withWidth (3.0f).reduced (0.0f, 7.0f), 1.5f);
        }
        g.setColour (on ? Ink::name() : Ink::text());
        g.setFont (font (13.5f, on));
        g.drawText (p.name, r.withTrimmedLeft (14.0f), juce::Justification::centredLeft, true);
    }
}

void SettingsTab::mouseMove (const juce::MouseEvent& e)
{
    if (const int i = linkAt (e.getPosition()); i != hovered)
    {
        hovered = i;
        repaint (0, 0, sidebarWidth, getHeight());
    }
}

void SettingsTab::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint (0, 0, sidebarWidth, getHeight());
}

void SettingsTab::mouseDown (const juce::MouseEvent& e)
{
    if (const int i = linkAt (e.getPosition()); i >= 0)
    {
        if (search.getText().isNotEmpty())
            search.setText ({}, false);
        showPage (i);
    }
}

void SettingsTab::timerCallback()
{
    if (! isShowing())
        return;
    if (lookShown != Theme::generation())
    {
        styleSearch();
        repaint();
    }
    bool moved = false;
    for (auto* r : rows)
        moved = r->refresh() || moved;
    if (moved)
        layoutColumn();
}

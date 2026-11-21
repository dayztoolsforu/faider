#include "SettingsTab.h"
#include "../LanguageCase.h"

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
    g.drawFittedText (TRANS ("Assigned"), juce::Rectangle<int> (2, noteH + 4, getWidth(), headingHeight - 8), juce::Justification::bottomLeft, 1, 0.7f);
    g.drawFittedText (TRANS ("Fixed"), juce::Rectangle<int> (2, assigned.getBottom(), getWidth(), headingHeight - 8), juce::Justification::bottomLeft, 1, 0.7f);
}

bool HotkeysPage::keyPressed (const juce::KeyPress& key)
{
    if (capturing.isEmpty())
        return false;
    const int index = capturingIndex();
    if (index < 0)   // (gone from the list meanwhile)
        stopCapturing ({});
    else if (key == juce::KeyPress::escapeKey)
        stopCapturing ({});
    else if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        stopCapturing (entries[(size_t) index].set ({}) ? juce::String() : TRANS ("Couldn't clear it"));
    else if (! GlobalHotkeys::usable (key))
    {
        note = TRANS ("Needs Ctrl, Alt or an F key");
        repaint();
    }
    else
    {
        // Refused: what has the key here keeps it (it isn't even tried); or Windows won't give it, so another app has it.
        const auto text = keyText (key.getTextDescription());
        if (const int holder = holderOf (key.getTextDescription(), index); holder >= 0)
            stopCapturing (TRANS ("%1 is already the key for %2").replace ("%1", text).replace ("%2", entries[(size_t) holder].name));
        else
            stopCapturing (entries[(size_t) index].set (key) ? juce::String() : TRANS ("%1 is taken by another app").replace ("%1", text));
    }
    return true;
}

void HotkeysPage::focusLost (FocusChangeType)
{
    if (capturing.isNotEmpty())
        stopCapturing ({});
}

juce::String HotkeysPage::idOf (int index) const
{
    const auto& e = entries[(size_t) index];
    int same = 0;
    for (int i = 0; i < index; ++i)
        same += entries[(size_t) i].group == e.group && entries[(size_t) i].name == e.name ? 1 : 0;
    return e.group + "\n" + e.name + "\n" + juce::String (same);
}

int HotkeysPage::capturingIndex() const
{
    for (int i = 0; capturing.isNotEmpty() && i < (int) entries.size(); ++i)
        if (entries[(size_t) i].set != nullptr && idOf (i) == capturing)
            return i;
    return -1;
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
    const int capturingAt = capturingIndex();
    if (capturing.isNotEmpty() && capturingAt < 0)   // (the one waiting for a key is gone: no longer waiting)
    {
        capturing.clear();
        setWantsKeyboardFocus (false);
    }
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
            red = (e.working ? TRANS ("Same key as %1: only this one works") : TRANS ("Not working: %1 has this key")).replace ("%1", entries[(size_t) other].name);
        }
        else if (e.key.isNotEmpty() && ! e.working)
            red = TRANS ("Not working: Windows refused the key (another app may have it)");

        if (e.set != nullptr)
        {
            const auto shownKey = capturingAt == i ? TRANS (juce::CharPointer_UTF8 ("Press a key\xe2\x80\xa6"))
                                                   : e.key.isEmpty() ? TRANS ("None") : keyText (e.key);
            settable.push_back ({ { e.name, e.where, juce::String() }, { { shownKey, [this, id = idOf (i)] (juce::Component&) { startCapturing (id); }, capturingAt == i } }, red });
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
    const auto usual = TRANS ("Click a key, then press the new one. Esc: cancel. Delete: none. A hotkey needs Ctrl, Alt or an F key, and works while a game has focus.");
    if (clashes == 0)
        return usual;
    return usual + "  " + (clashes == 1 ? TRANS ("%1 shares a key (in red).") : TRANS ("%1 share a key (in red).")).replace ("%1", juce::String (clashes));
}

void HotkeysPage::startCapturing (const juce::String& id)
{
    capturing = id;
    problem.clear();
    rebuild();
    if (capturing.isEmpty())   // (gone from the list as it was made again)
        return;
    setWantsKeyboardFocus (true);
    grabKeyboardFocus();
}

void HotkeysPage::stopCapturing (const juce::String& whyNot)
{
    capturing.clear();
    setWantsKeyboardFocus (false);
    problem = whyNot;   // (shown until the next key is clicked)
    rebuild();
}

void HotkeysPage::timerCallback()
{
    if (isShowing() && capturing.isEmpty() && signature (list()) != shown)
        rebuild();
}

//==============================================================================
void SettingsTab::Column::paint (juce::Graphics& g)
{
    g.fillAll (Ink::page());
    g.setColour (Ink::name());
    g.setFont (font (22.0f, true));
    g.drawFittedText (title, titleArea, juce::Justification::centredLeft, 1, 0.7f);
    drawText (g, intro, font (13.0f), Ink::dim(), introArea);
}

SettingsTab::SettingsTab (std::vector<SettingsRegistry::Page> p, std::vector<SettingsRegistry::Setting> s)
    : pages (std::move (p)), settings (std::move (s))
{
    search.setTextToShowWhenEmpty (TRANS ("Search settings"), Ink::dim());
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
    search.setTextToShowWhenEmpty (TRANS ("Search settings"), Ink::dim());
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

    pageResults.clear();   // (after the rows that showed them)

    const auto query = search.getText().trim();
    if (query.isNotEmpty())
    {
        // The pages that match by name or keywords, each a row that opens it (under its group), then
        // every page's rows that match, each under its page's name.
        int found = 0;
        for (auto& p : pages)
            if (p.matches (query))
            {
                auto page = std::make_unique<SettingsRegistry::Setting>();
                page->page = p.group;
                page->name = p.name;
                page->description = p.intro;
                page->actions = { { [] { return TRANS ("Open"); },
                                    [self = juce::Component::SafePointer<SettingsTab> (this), name = p.name] (juce::Component&)
                                    {   // (once the click is over: opening it makes the rows again, this one's with them)
                                        juce::MessageManager::callAsync ([self, name] { if (self != nullptr) self->show (name); });
                                    } } };
                column.addAndMakeVisible (rows.add (new Row (*page, p.group)));
                pageResults.push_back (std::move (page));
                ++found;
            }
        for (auto& s : settings)
            if (s.matches (query))
            {
                column.addAndMakeVisible (rows.add (new Row (s, s.page)));
                ++found;
            }
        column.title = TRANS ("Search");
        column.intro = found > 0 ? (found == 1 ? TRANS ("%1 setting matches.") : TRANS ("%1 settings match.")).replace ("%1", juce::String (found))
                                 : TRANS ("Nothing matches \"%1\". Try another word.").replace ("%1", query);
    }
    else
    {
        const auto& page = pages[(size_t) current];
        column.title = SettingsRegistry::shown (page.name);   // (an identifier: translated to show)
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
            g.drawFittedText (Language::upper (SettingsRegistry::shown (group)), groupArea (group), juce::Justification::centredLeft, 1, 0.7f);
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
        g.drawFittedText (SettingsRegistry::shown (p.name), r.withTrimmedLeft (14.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);   // (an identifier: translated to show)
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

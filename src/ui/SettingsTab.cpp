#include "SettingsTab.h"

//==============================================================================
void OptionsPage::heading (const juce::String& text)
{
    auto* l = labels.add (new juce::Label ({}, text));
    l->setFont (Console::legendFont (Console::Type::name));   // section headings
    l->setColour (juce::Label::textColourId, Console::Colours::legendDim());
    l->setFont (LargeFormat::print (12.5f));   // (the console's print)
    l->setColour (juce::Label::textColourId, DeskSettings::print());
    addAndMakeVisible (l);
    rows.push_back ({ l, true });
}

void OptionsPage::toggle (const juce::String& text, const juce::String& tip, std::function<bool()> get, std::function<void (bool)> set)
{
    auto* sw = switches.add (new DeskSettings::Switch (text));
    sw->setTooltip (tip);
    addAndMakeVisible (sw);
    rows.push_back ({ sw, false });
    sw->onClick = [this, get, set] { set (! get()); refresh(); };
    refreshers.push_back ([sw, get] { sw->setToggleState (get(), juce::dontSendNotification); });
}

void OptionsPage::choice (const juce::StringArray& names, std::function<int()> get, std::function<void (int)> set, bool oneRow)
{
    for (int i = 0; i < names.size(); ++i)
    {
        auto& b = key (names[i], {}, oneRow && i > 0);
        b.onClick = [this, i, set] { set (i); refresh(); };
        refreshers.push_back ([&b, i, get] { b.setToggleState (get() == i, juce::dontSendNotification); });
    }
}

void OptionsPage::action (const juce::String& text, const juce::String& tip, std::function<void()> run)
{
    auto& b = key (text, tip);
    b.setClickingTogglesState (false);
    b.onClick = std::move (run);
}

void OptionsPage::actionShowing (std::function<juce::String()> text, const juce::String& tip, std::function<void()> run)
{
    auto& b = key (text(), tip);
    b.setClickingTogglesState (false);
    b.onClick = std::move (run);
    refreshers.push_back ([&b, text] { if (const auto now = text(); now != b.getButtonText()) b.setButtonText (now); });
}

void OptionsPage::finish()
{
    refresh();
    setSize (juce::jmax (getWidth(), width), heightFor (juce::jmax (getWidth(), width)));
}

void OptionsPage::paint (juce::Graphics& g)
{
    g.fillAll (DeskSettings::desk());
    for (auto& c : cards)
        DeskSettings::card (g, c.toFloat());
}

int OptionsPage::arrange (int w, bool apply)
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

juce::TextButton& OptionsPage::key (const juce::String& text, const juce::String& tip, bool besideLast)
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

void OptionsPage::refresh()
{
    for (auto& r : refreshers)
        r();
}

void OptionsPage::lookAndFeelChanged()
{
    for (auto* l : labels)
        l->setColour (juce::Label::textColourId, Console::Colours::legendDim());
    for (auto* b : keys)
        b->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
    repaint();
}

//==============================================================================
HotkeysPage::HotkeysPage (std::function<std::vector<Entry>()> listFn) : list (std::move (listFn))
{
    note.setFont (juce::FontOptions (Console::Type::body));
    note.setColour (juce::Label::textColourId, Console::Colours::legendDim());
    DeskSettings::styleHeading (note);
    addAndMakeVisible (note);
    rebuild();
    startTimerHz (1);
}

void HotkeysPage::paint (juce::Graphics& g)
{
    g.fillAll (DeskSettings::desk());
    for (auto& c : cards)
        DeskSettings::card (g, c.toFloat());
}

int HotkeysPage::arrangeCards (int w, bool apply)
{
    if (apply)
        note.setBounds (14, 10, w - 28, 24);
    std::vector<DeskSettings::Group> groups;
    for (auto& row : rows)
    {
        if (row.heading != nullptr)
            groups.push_back ({ row.heading.get(), {} });
        else if (groups.empty())
            continue;
        else if (row.keyButton == nullptr)   // (a line of its own: the group's note, a warning)
            groups.back().rows.push_back ({ { row.name.get() }, 0 });
        else
            groups.back().rows.push_back ({ { row.name.get(), row.keyButton.get() }, 160 });
    }
    return DeskSettings::arrange (groups, w, apply, cards, 400, 42);
}

bool HotkeysPage::keyPressed (const juce::KeyPress& key)
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
    {
        // Refused: by what has the key here, if anything does; otherwise another app has it.
        const int holder = holderOf (key.getTextDescription(), capturing);
        const auto taken = key.getTextDescription().toUpperCase()
                         + (holder >= 0 ? " IS ALREADY THE KEY FOR " + entries[(size_t) holder].name : juce::String (" IS TAKEN BY ANOTHER APP"));
        stopCapturing (entries[(size_t) capturing].set (key) ? juce::String() : taken);
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

void HotkeysPage::addLine (const juce::String& text, juce::Colour colour)
{
    Row r;
    r.name = std::make_unique<juce::Label> (juce::String(), text);
    DeskSettings::styleText (*r.name);
    r.name->setColour (juce::Label::textColourId, colour);
    r.name->setMinimumHorizontalScale (0.7f);
    r.name->setTooltip (text);
    addAndMakeVisible (*r.name);
    rows.push_back (std::move (r));
}

void HotkeysPage::rebuild()
{
    entries = list();
    shown = signature (entries);
    rows.clear();
    clashes = 0;
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
            if (e.where.isNotEmpty())
                addLine (e.where, Console::Colours::legendDim());
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

        // The same key twice (only one of them gets it), or a key Windows wouldn't give.
        const int other = e.set != nullptr && e.key.isNotEmpty() ? holderOf (e.key, (int) i) : -1;
        if (other >= 0)
        {
            ++clashes;
            addLine ("SAME KEY AS " + entries[(size_t) other].name + ": ONLY ONE WORKS", Console::Colours::danger());
        }
        else if (e.key.isNotEmpty() && ! e.working)
            addLine ("NOT WORKING: WINDOWS REFUSED THE KEY (ANOTHER APP MAY HAVE IT)", Console::Colours::danger());
    }
    note.setText (problem.isNotEmpty() ? problem : usualNote(), juce::dontSendNotification);

    const int w = juce::jmax (getWidth(), 560);
    setSize (w, heightFor (w));
    resized();
}

juce::String HotkeysPage::usualNote() const
{
    return "CLICK A KEY, THEN PRESS THE NEW ONE.  ESC: CANCEL.  DELETE: NONE."
         + (clashes > 0 ? "   " + juce::String (clashes) + " SHARE A KEY (IN RED)." : juce::String());
}

void HotkeysPage::startCapturing (int index)
{
    capturing = index;
    problem.clear();
    note.setText (usualNote(), juce::dontSendNotification);
    size_t n = 0;
    for (auto& r : rows)
        if (r.keyButton != nullptr && (int) n++ == index)
            r.keyButton->setButtonText ("PRESS A KEY...");
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
SettingsTab::SettingsTab()
{
    view.setScrollBarsShown (true, false);
    addAndMakeVisible (view);
}

void SettingsTab::addSection (const juce::String& name, juce::Component& page, bool fill)
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

void SettingsTab::show (const juce::String& name)
{
    for (size_t i = 0; i < sections.size(); ++i)
        if (sections[i].name.equalsIgnoreCase (name))
            show ((int) i);
}

void SettingsTab::paint (juce::Graphics& g)
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

void SettingsTab::resized()
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

int SettingsTab::sectionAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) sections.size(); ++i)
        if (sectionCard (i).contains (p))
            return i;
    return -1;
}

void SettingsTab::show (int index)
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

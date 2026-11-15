#include "SettingsWidgets.h"
#include "../Theme.h"
#include "../LargeFormat.h"

namespace SettingsUi
{
    // (bold for the names: JUCE has no semibold that every look's typeface is sure to have)
    juce::Font font (float height, bool strong) { return juce::FontOptions (height, strong ? juce::Font::bold : juce::Font::plain); }

    namespace Ink
    {
        juce::Colour page()    { return Theme::get().panel; }
        juce::Colour sidebar() { return Theme::get().panelDark; }
        juce::Colour box()     { return Theme::get().bay; }
        juce::Colour field()   { return Theme::get().slot; }
        juce::Colour edge()    { return Theme::get().rim; }
        juce::Colour name()    { return Theme::get().legend; }
        juce::Colour text()    { return Theme::get().ink; }
        juce::Colour dim()     { return Theme::get().legendDim; }
        juce::Colour warning() { return Theme::get().palAmber; }
        juce::Colour danger()  { return Theme::get().ledRed; }
    }

    int textHeight (const juce::String& text, const juce::Font& f, int width)
    {
        if (text.isEmpty() || width <= 0)
            return 0;
        juce::AttributedString s;
        s.append (text, f);
        s.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout (s, (float) width);
        return (int) std::ceil (layout.getHeight());
    }

    void drawText (juce::Graphics& g, const juce::String& text, const juce::Font& f, juce::Colour colour, juce::Rectangle<int> area)
    {
        if (text.isEmpty())
            return;
        juce::AttributedString s;
        s.append (text, f, colour);
        s.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout (s, (float) area.getWidth());
        layout.draw (g, area.toFloat());
    }

    int textWidth (const juce::String& text, const juce::Font& f)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (f, text));
    }

    //==========================================================================
    void Switch::paintButton (juce::Graphics& g, bool over, bool down)
    {
        const auto track = juce::Rectangle<float> ((float) width, (float) height).withCentre (getLocalBounds().toFloat().getCentre());
        const bool on = getToggleState();
        const auto lamp = Theme::get().powerLamp;
        if (on)
            LargeFormat::bloom (g, track, lamp, over ? 0.9f : 0.7f, track.getHeight() * 0.5f, 4.0f);   // (its soft glow)
        g.setColour (on ? lamp.withMultipliedBrightness (down ? 0.85f : 1.0f) : Theme::get().chromeOff.brighter (over ? 0.15f : 0.0f));
        g.fillRoundedRectangle (track, track.getHeight() * 0.5f);
        g.setColour (over ? Theme::get().accent.withAlpha (0.8f) : Ink::edge());
        g.drawRoundedRectangle (track.reduced (0.5f), track.getHeight() * 0.5f, 1.0f);
        const float d = track.getHeight() - 6.0f;
        const auto knob = juce::Rectangle<float> (d, d).withCentre ({ on ? track.getRight() - 3.0f - d * 0.5f : track.getX() + 3.0f + d * 0.5f, track.getCentreY() });
        g.setColour (on ? Theme::get().litInk : Theme::get().knobTop);
        g.fillEllipse (knob);
    }

    //==========================================================================
    std::vector<int> Segmented::widths() const
    {
        std::vector<int> w;
        for (auto& o : options)
            w.push_back (textWidth (o, font (13.0f, true)) + 28);
        return w;
    }

    int Segmented::preferredWidth() const
    {
        int total = 4;
        for (int w : widths())
            total += w;
        return total;
    }

    int Segmented::at (int x) const
    {
        const auto w = widths();
        const float scale = (float) (getWidth() - 4) / (float) juce::jmax (1, preferredWidth() - 4);
        float left = 2.0f;
        for (int i = 0; i < (int) w.size(); ++i)
        {
            left += (float) w[(size_t) i] * scale;
            if ((float) x < left)
                return i;
        }
        return -1;
    }

    void Segmented::mouseUp (const juce::MouseEvent& e)
    {
        if (const int i = at (e.x); i >= 0 && getLocalBounds().contains (e.getPosition()) && onChoose)
            onChoose (i);
    }

    void Segmented::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Ink::field());
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Ink::edge());
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        const auto w = widths();
        const float scale = (r.getWidth() - 4.0f) / (float) juce::jmax (1, preferredWidth() - 4);
        float x = r.getX() + 2.0f;
        for (int i = 0; i < options.size(); ++i)
        {
            const auto seg = juce::Rectangle<float> (x, r.getY() + 2.0f, (float) w[(size_t) i] * scale, r.getHeight() - 4.0f);
            x += seg.getWidth();
            if (i == chosen)
            {
                g.setColour (Theme::get().powerLamp);
                g.fillRoundedRectangle (seg, 4.5f);
            }
            else if (i == hovered)
            {
                g.setColour (Theme::get().chromeHover);
                g.fillRoundedRectangle (seg, 4.5f);
            }
            g.setColour (i == chosen ? Theme::get().litInk : Ink::text());
            g.setFont (font (13.0f, true));
            g.drawFittedText (options[i], seg.toNearestInt(), juce::Justification::centred, 1, 0.7f);   // (squeezed: shrinks, not cut)
        }
    }

    //==========================================================================
    void Button::paintButton (juce::Graphics& g, bool over, bool down)
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        auto fill = primary ? Theme::get().accent : over ? Theme::get().chromeHover : Theme::get().chromeOff;
        if (primary && over)
            fill = fill.brighter (0.12f);
        if (down)
            fill = fill.darker (0.25f);
        const float alpha = isEnabled() ? 1.0f : 0.45f;
        g.setColour (fill.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour ((over && ! primary ? Theme::get().accent.withAlpha (0.7f) : Ink::edge()).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        g.setColour ((primary ? Theme::get().litInk : Ink::name()).withMultipliedAlpha (alpha));
        g.setFont (font (13.0f, true));
        g.drawFittedText (getButtonText(), getLocalBounds().reduced (8, 0), juce::Justification::centred, 1, 0.7f);
    }

    //==========================================================================
    void Dropdown::paintButton (juce::Graphics& g, bool over, bool down)
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (down ? Ink::field().darker (0.2f) : Ink::field());
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (over ? Theme::get().accent.withAlpha (0.8f) : Ink::edge());
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        auto inner = getLocalBounds().reduced (12, 0);
        const auto arrow = inner.removeFromRight (12).toFloat().withSizeKeepingCentre (9.0f, 5.0f);
        juce::Path chevron;
        chevron.startNewSubPath (arrow.getX(), arrow.getY());
        chevron.lineTo (arrow.getCentreX(), arrow.getBottom());
        chevron.lineTo (arrow.getRight(), arrow.getY());
        g.setColour (Ink::dim());
        g.strokePath (chevron, juce::PathStrokeType (1.5f));
        g.setColour (Ink::name());
        g.setFont (font (13.0f));
        g.drawFittedText (getButtonText(), inner.withTrimmedRight (8), juce::Justification::centredLeft, 1, 0.7f);
    }

    //==========================================================================
    void Chip::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Ink::field());
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (Ink::edge());
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        g.setColour (Theme::get().powerLamp.withAlpha (0.8f));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ r.getX() + 13.0f, r.getCentreY() }));
        g.setColour (Ink::name());
        g.setFont (font (12.5f, true));
        g.drawFittedText (text, getLocalBounds().withTrimmedLeft (22).withTrimmedRight (10), juce::Justification::centredLeft, 1, 0.7f);
    }

    //==========================================================================
    void PathField::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Ink::field());
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Ink::edge());
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        const auto area = getLocalBounds().reduced (10, 0);
        const auto f = font (13.0f);
        auto shown = text;   // (the end of a path says most: its start goes first)
        if (textWidth (shown, f) > area.getWidth())
        {
            const juce::String dots (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
            while (shown.isNotEmpty() && textWidth (dots + shown, f) > area.getWidth())
                shown = shown.substring (1);
            shown = dots + shown;
        }
        g.setColour (Ink::name());
        g.setFont (f);
        g.drawText (shown, area, juce::Justification::centredLeft, false);
    }

    //==========================================================================
    NumberField::NumberField (std::function<int()> get, std::function<void (int)> apply, int digits)
        : value (std::move (get)), onApply (std::move (apply))
    {
        editor.setInputRestrictions (digits, "0123456789");
        editor.setJustification (juce::Justification::centredLeft);
        editor.setIndents (10, 0);
        editor.onReturnKey = [this] { onApply (editor.getText().getIntValue()); };
        applyKey.onClick = [this] { onApply (editor.getText().getIntValue()); };
        addAndMakeVisible (editor);
        addAndMakeVisible (applyKey);
        refresh();
    }

    void NumberField::style()
    {
        editor.setFont (font (13.0f));
        editor.setColour (juce::TextEditor::backgroundColourId, Ink::field());
        editor.setColour (juce::TextEditor::outlineColourId, Ink::edge());
        editor.setColour (juce::TextEditor::focusedOutlineColourId, Theme::get().accent);
        editor.setColour (juce::TextEditor::textColourId, Ink::name());
        editor.applyFontToAllText (font (13.0f));
        editor.applyColourToAllText (Ink::name(), true);
    }

    void NumberField::refresh()
    {
        if (std::exchange (lookShown, Theme::generation()) != Theme::generation())
            style();
        if (! editor.hasKeyboardFocus (true))   // (not while it's being typed in)
            if (const auto now = juce::String (value()); now != editor.getText())
                editor.setText (now, false);
    }

    void NumberField::resized()
    {
        auto r = getLocalBounds();
        applyKey.setBounds (r.removeFromRight (applyKey.preferredWidth()));
        r.removeFromRight (8);
        editor.setBounds (r);
    }

    //==========================================================================
    Table::Table (std::vector<Column> c, juce::String emptyText, std::function<std::vector<Line>()> s)
        : columns (std::move (c)), empty (std::move (emptyText)), source (std::move (s))
    {
        refresh();
    }

    juce::String Table::signature (const std::vector<Line>& list)
    {
        juce::String sig;
        for (auto& l : list)
        {
            sig << l.cells.joinIntoString ("\t") << "|" << l.note << "|" << (l.dim ? "d" : "");
            for (auto& a : l.actions)
                sig << "|" << a.text << (a.enabled ? "" : "-") << (a.primary ? "!" : "");
            sig << "\n";
        }
        return sig;
    }

    void Table::setLines (std::vector<Line> now)
    {
        const auto sig = signature (now);
        lines = std::move (now);   // (the newest closures, whether or not anything shown changed)
        if (sig == shown)
            return;
        shown = sig;
        buttons.clear();
        buttonOf.clear();
        for (int i = 0; i < (int) lines.size(); ++i)
            for (int k = 0; k < (int) lines[(size_t) i].actions.size(); ++k)
            {
                auto& a = lines[(size_t) i].actions[(size_t) k];
                auto* b = buttons.add (new Button (a.text, a.primary));
                b->setEnabled (a.enabled);
                // (once the click is over: what it does may make the table again, this button with it)
                b->onClick = [this, i, k, b]
                {
                    juce::MessageManager::callAsync ([self = juce::Component::SafePointer<Table> (this), button = juce::Component::SafePointer<Button> (b), i, k]
                    {
                        if (self == nullptr || button == nullptr || ! juce::isPositiveAndBelow (i, (int) self->lines.size()))
                            return;
                        auto& actions = self->lines[(size_t) i].actions;
                        if (juce::isPositiveAndBelow (k, (int) actions.size()) && actions[(size_t) k].run)
                            actions[(size_t) k].run (*button);
                    });
                };
                addAndMakeVisible (b);
                buttonOf.push_back ({ i, k });
            }
        resized();
        repaint();
    }

    int Table::heightFor (int) const
    {
        int h = headerHeight;
        for (auto& l : lines)
            h += lineHeight + (l.note.isNotEmpty() ? noteHeight : 0);
        return h + (lines.empty() ? lineHeight : 0) + 2;
    }

    std::vector<juce::Rectangle<int>> Table::cellAreas (juce::Rectangle<int> line) const
    {
        float total = 0.0f;
        for (auto& c : columns)
            total += c.weight;
        std::vector<juce::Rectangle<int>> areas;
        auto r = line.reduced (14, 0);
        const int full = r.getWidth();
        for (size_t i = 0; i < columns.size(); ++i)
            areas.push_back (i + 1 == columns.size() ? r : r.removeFromLeft (juce::roundToInt ((float) full * columns[i].weight / juce::jmax (0.01f, total))));
        return areas;
    }

    void Table::resized()
    {
        int y = headerHeight;
        size_t b = 0;
        for (int i = 0; i < (int) lines.size(); ++i)
        {
            const auto line = juce::Rectangle<int> (0, y, getWidth(), lineHeight);
            auto last = cellAreas (line).back().reduced (0, 5);
            for (int k = (int) lines[(size_t) i].actions.size(); --k >= 0;)   // right to left
                if (b + (size_t) k < (size_t) buttons.size())
                {
                    auto* button = buttons[(int) b + k];
                    button->setBounds (last.removeFromRight (button->preferredWidth()));
                    last.removeFromRight (6);
                }
            b += lines[(size_t) i].actions.size();
            y += lineHeight + (lines[(size_t) i].note.isNotEmpty() ? noteHeight : 0);
        }
    }

    void Table::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Ink::box());
        g.fillRoundedRectangle (r, 8.0f);
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (getLocalBounds().removeFromTop (headerHeight));
            g.setColour (Theme::get().bayDark);
            g.fillRoundedRectangle (r, 8.0f);
        }
        g.setColour (Ink::edge());
        g.drawRoundedRectangle (r, 8.0f, 1.0f);

        const auto header = cellAreas ({ 0, 0, getWidth(), headerHeight });
        g.setFont (font (12.0f, true));
        g.setColour (Ink::dim());
        for (size_t i = 0; i < columns.size(); ++i)
            g.drawFittedText (columns[i].title, header[i], i + 1 == columns.size() && columns.size() > 1 ? juce::Justification::centredRight : juce::Justification::centredLeft, 1, 0.7f);

        int y = headerHeight;
        if (lines.empty())
        {
            g.setColour (Ink::edge());
            g.fillRect (1, y, getWidth() - 2, 1);
            g.setColour (Ink::dim());
            g.setFont (font (13.0f));
            g.drawFittedText (empty, juce::Rectangle<int> (14, y, getWidth() - 28, lineHeight), juce::Justification::centredLeft, 2, 0.8f);
            return;
        }
        for (auto& l : lines)
        {
            g.setColour (Ink::edge());
            g.fillRect (1, y, getWidth() - 2, 1);
            const auto areas = cellAreas ({ 0, y, getWidth(), lineHeight });
            const int textColumns = (int) columns.size() - (l.actions.empty() ? 0 : 1);
            for (int i = 0; i < textColumns && i < l.cells.size(); ++i)
            {
                const bool keyColumn = i + 1 == (int) columns.size() && columns.size() > 1;
                g.setColour (i == 0 && ! l.dim ? Ink::name() : Ink::dim());
                g.setFont (font (13.0f, i == 0));
                g.drawFittedText (l.cells[i], areas[(size_t) i].withTrimmedRight (8), keyColumn ? juce::Justification::centredRight : juce::Justification::centredLeft, 1, 0.7f);
            }
            y += lineHeight;
            if (l.note.isNotEmpty())
            {
                g.setColour (Ink::danger());
                g.setFont (font (12.0f, true));
                g.drawFittedText (l.note, juce::Rectangle<int> (14, y - 6, getWidth() - 28, noteHeight), juce::Justification::centredLeft, 1, 0.7f);
                y += noteHeight;
            }
        }
    }

    //==========================================================================
    Row::Row (const SettingsRegistry::Setting& s, const juce::String& pageAbove) : setting (s), above (pageAbove)
    {
        using SettingsRegistry::Kind;
        if (s.kind == Kind::toggle)
        {
            toggle = std::make_unique<Switch>();
            toggle->setTitle (SettingsRegistry::shown (s.name));
            toggle->onClick = [this] { if (setting.set && setting.get) setting.set (! setting.get()); refresh(); };
            addAndMakeVisible (*toggle);
        }
        else if (s.kind == Kind::segmented)
        {
            segmented = std::make_unique<Segmented> (s.options);
            segmented->onChoose = [this] (int i) { if (setting.choose) setting.choose (i); refresh(); };
            addAndMakeVisible (*segmented);
        }
        else if (s.kind == Kind::dropdown)
        {
            dropdown = std::make_unique<Dropdown>();
            dropdown->onClick = [this] { if (setting.menu) setting.menu (*dropdown); };
            addAndMakeVisible (*dropdown);
        }
        else if (s.kind == Kind::custom && s.make)
        {
            custom = s.make();
            if (custom != nullptr)
                addAndMakeVisible (*custom);
        }
        if (s.status && s.path)
        {
            pathBox = std::make_unique<PathField>();
            addAndMakeVisible (*pathBox);
        }
        else if (s.status)
        {
            chip = std::make_unique<Chip>();
            addAndMakeVisible (*chip);
        }
        for (size_t i = 0; i < s.actions.size(); ++i)
        {
            auto* b = buttons.add (new Button (s.actions[i].text ? s.actions[i].text() : juce::String(), s.actions[i].primary));
            b->onClick = [this, i, b] { if (setting.actions[i].run) setting.actions[i].run (*b); refresh(); };
            addAndMakeVisible (b);
        }
        fade.tick = [this]
        {
            lit = juce::jmax (0.0f, lit - 0.04f);
            repaint();
            if (lit <= 0.0f)
                fade.stopTimer();
        };
        refresh();
    }

    void Row::flash()
    {
        lit = 1.0f;
        fade.startTimerHz (30);
    }

    bool Row::passesThrough (juce::Point<int> screen) const
    {
        if (! setting.passThrough || ! isShowing())
            return false;
        for (auto* b : buttons)
            if (b->getScreenBounds().contains (screen))
                return true;
        return false;
    }

    bool Row::refresh()
    {
        if (toggle != nullptr && setting.get)
            toggle->setToggleState (setting.get(), juce::dontSendNotification);
        if (segmented != nullptr && setting.chosen)
            segmented->setChosen (setting.chosen());
        if (dropdown != nullptr && setting.value)
            if (const auto v = setting.value(); v != dropdown->getButtonText())
                dropdown->setButtonText (v);
        if (chip != nullptr)
            chip->setText (setting.status());
        if (pathBox != nullptr)
            pathBox->setText (setting.status());
        bool resize = false;
        for (int i = 0; i < buttons.size(); ++i)
        {
            const auto& a = setting.actions[(size_t) i];
            if (a.text)
                if (const auto t = a.text(); t != buttons[i]->getButtonText())
                {
                    buttons[i]->setButtonText (t);
                    resize = true;   // (wider or narrower)
                }
            buttons[i]->setEnabled (! a.enabled || a.enabled());
        }
        if (auto* r = dynamic_cast<Refreshing*> (custom.get()))
        {
            r->refresh();
            if (const int h = r->heightFor (getWidth()); std::exchange (customHeight, h) != h)
                resize = true;   // (a table's lines came or went)
        }
        const auto note = setting.note ? setting.note() : juce::String();
        const auto warn = setting.warning ? setting.warning() : juce::String();
        if (note != noteText || warn != warningText)
        {
            noteText = note;
            warningText = warn;
            resize = true;
        }
        if (resize)
        {
            resized();
            repaint();
        }
        return resize;
    }

    int Row::controlsWidth() const
    {
        int w = 0;
        auto add = [&w] (int width) { w += (w > 0 ? 8 : 0) + width; };
        if (pathBox != nullptr)    add (240);
        if (chip != nullptr)       add (chip->preferredWidth());
        for (auto* b : buttons)    add (b->preferredWidth());
        if (toggle != nullptr)     add (Switch::width);
        if (segmented != nullptr)  add (segmented->preferredWidth());
        if (dropdown != nullptr)   add (dropdown->preferredWidth());
        if (auto* r = dynamic_cast<Refreshing*> (custom.get()); r != nullptr && ! setting.wide)
            add (r->preferredWidth());
        return w;
    }

    int Row::heightFor (int width) { return layout (width, false); }

    int Row::layout (int width, bool apply)
    {
        constexpr int padX = 18, padY = 14, gap = 16, controlH = 30;
        int y = 0;
        if (above.isNotEmpty())
        {
            if (apply)
                aboveArea = { 2, 0, width - 4, 22 };
            y += 24;
        }
        const auto boxTop = y;
        auto inner = juce::Rectangle<int> (padX, boxTop + padY, width - 2 * padX, 0);
        const int wantControls = controlsWidth();
        const bool beside = wantControls > 0 && inner.getWidth() - wantControls - gap >= 220;
        const int textW = beside ? inner.getWidth() - wantControls - gap : inner.getWidth();

        const auto nameFont = font (14.0f, true), descFont = font (13.0f);
        const int nameH = 20;
        const int descH = textHeight (setting.description, descFont, textW);
        const int noteH = textHeight (noteText, descFont, textW);
        const int warnH = textHeight (warningText, font (13.0f, true), textW);
        int textH = nameH + (descH > 0 ? 3 + descH : 0) + (noteH > 0 ? 4 + noteH : 0) + (warnH > 0 ? 4 + warnH : 0);
        if (apply)
        {
            int ty = inner.getY();
            nameArea = { inner.getX(), ty, textW, nameH };
            ty += nameH + 3;
            descArea = { inner.getX(), ty, textW, descH };
            ty += descH + (descH > 0 ? 4 : 0);
            noteArea = { inner.getX(), ty, textW, noteH };
            ty += noteH + (noteH > 0 ? 4 : 0);
            warnArea = { inner.getX(), ty, textW, warnH };
        }

        int contentH = textH;
        // The controls: beside the text (centred on it, a single line's worth at the least), else under it.
        int controlsTop = beside ? inner.getY() + juce::jmax (0, (juce::jmin (textH, 46) - controlH) / 2) : inner.getY() + textH + 12;
        if (wantControls > 0 && ! beside)
            contentH = textH + 12 + controlH;
        if (beside)
            contentH = juce::jmax (textH, controlH);
        if (apply && wantControls > 0)
        {
            auto line = beside ? juce::Rectangle<int> (inner.getRight() - wantControls, controlsTop, wantControls, controlH)
                               : juce::Rectangle<int> (inner.getX(), controlsTop, inner.getWidth(), controlH);
            auto place = [&line] (juce::Component& c, int w, int h)
            {
                auto r = line.removeFromLeft (juce::jmin (w, line.getWidth()));
                c.setBounds (r.withSizeKeepingCentre (r.getWidth(), h));
                line.removeFromLeft (8);
            };
            // (under the text, a path box takes what the buttons leave)
            int fixed = 0;
            for (auto* b : buttons)
                fixed += b->preferredWidth() + 8;
            if (pathBox != nullptr)    place (*pathBox, beside ? 240 : juce::jmax (120, line.getWidth() - fixed), controlH);
            if (chip != nullptr)       place (*chip, chip->preferredWidth(), 26);
            for (auto* b : buttons)    place (*b, b->preferredWidth(), controlH);
            if (toggle != nullptr)     place (*toggle, Switch::width, Switch::height);
            if (segmented != nullptr)  place (*segmented, segmented->preferredWidth(), controlH);
            if (dropdown != nullptr)   place (*dropdown, dropdown->preferredWidth(), controlH);
            if (auto* r = dynamic_cast<Refreshing*> (custom.get()); r != nullptr && ! setting.wide)
                place (*custom, r->preferredWidth(), controlH);
        }
        if (setting.wide && custom != nullptr)
        {
            const int h = dynamic_cast<Refreshing*> (custom.get()) != nullptr ? dynamic_cast<Refreshing*> (custom.get())->heightFor (inner.getWidth()) : 120;
            if (apply)
                custom->setBounds (inner.getX(), inner.getY() + contentH + 14, inner.getWidth(), h);
            contentH += 14 + h;
        }
        const int boxH = contentH + 2 * padY;
        if (apply)
            box = { 0, boxTop, width, boxH };
        return boxTop + boxH;
    }

    void Row::paint (juce::Graphics& g)
    {
        if (above.isNotEmpty())
        {
            g.setColour (Ink::dim());
            g.setFont (font (12.0f, true));
            g.drawFittedText (SettingsRegistry::shown (above), aboveArea, juce::Justification::bottomLeft, 1, 0.7f);   // (the page's name, translated)
        }
        const auto r = box.toFloat().reduced (0.5f);
        g.setColour (Ink::box());
        g.fillRoundedRectangle (r, 8.0f);
        if (lit > 0.0f)   // (FIND brought it here)
        {
            g.setColour (Theme::get().accent.withAlpha (0.18f * lit));
            g.fillRoundedRectangle (r, 8.0f);
        }
        g.setColour (lit > 0.0f ? Theme::get().accent.withAlpha (juce::jmax (0.25f, lit)) : Ink::edge());
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));   // (its top edge catching the light)
        g.fillRect (r.reduced (8.0f, 0.0f).withHeight (1.0f).translated (0.0f, 1.0f));

        g.setColour (Ink::name());
        g.setFont (font (14.0f, true));
        g.drawFittedText (SettingsRegistry::shown (setting.name), nameArea, juce::Justification::centredLeft, 1, 0.7f);   // (its name is English, an identifier: translated here)
        drawText (g, setting.description, font (13.0f), Ink::dim(), descArea);
        drawText (g, noteText, font (13.0f), Ink::text(), noteArea);
        drawText (g, warningText, font (13.0f, true), Ink::warning(), warnArea);
    }

    //==========================================================================
    void Heading::paint (juce::Graphics& g)
    {
        g.setColour (Ink::dim());
        g.setFont (font (13.0f, true));
        g.drawFittedText (SettingsRegistry::shown (text), getLocalBounds().withTrimmedLeft (2).withTrimmedBottom (8), juce::Justification::bottomLeft, 1, 0.7f);   // (a section's name: translated here)
    }
}

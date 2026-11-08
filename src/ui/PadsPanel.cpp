#include "PadsPanel.h"
#include "MixerComponent.h"
#include "Console.h"
#include "ConsoleControls.h"
#include "MidiMap.h"
#include "../engine/AppCapture.h"
#include "../devices/WindowsDefault.h"

namespace
{
    constexpr const char* soundFiles = "*.wav;*.flac;*.aif;*.aiff;*.ogg;*.mp3";

    juce::String padLabel (int index)   // "A1" .. "D16"
    {
        return juce::String::charToString ((juce::juce_wchar) ('A' + index / Pads::perBank)) + juce::String (index % Pads::perBank + 1);
    }

    juce::Colour padColour (const Pads::Pad& p)
    {
        return p.colour.isTransparent() ? Console::Colours::ledBlue() : p.colour;
    }

    // Loads `file` into a pad, saying why not if it can't.
    bool loadSound (AudioEngine& engine, int index, const juce::File& file)
    {
        juce::String error;
        if (engine.getPads().load (index, file, engine.getSampleRate(), error))
            return true;
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "SAMPLE NOT LOADED", error);
        return false;
    }

    // Peer keys across a row, all one width and a unit apart, each Space::key high and centred.
    void evenKeys (juce::Rectangle<int> row, std::initializer_list<juce::Component*> keys)
    {
        const int n = juce::jmax (1, (int) keys.size());
        const int span = row.getWidth() + Console::Space::unit;   // each key and the gap after it
        const int h = juce::jmin (Console::Space::key, row.getHeight());
        int i = 0;
        for (auto* k : keys)
        {
            const int x0 = row.getX() + span * i / n, x1 = row.getX() + span * (i + 1) / n - Console::Space::unit;
            k->setBounds (juce::Rectangle<int> (x0, row.getY(), x1 - x0, row.getHeight()).withSizeKeepingCentre (x1 - x0, h));
            ++i;
        }
    }
}

//==============================================================================
// One pad: a big key that lights in its colour while it plays (with the lamps' fade), its name,
// its mode and choke group, and a bar that runs along the bottom as the sound plays.
class PadsPanel::PadButton : public juce::Component,
                             public juce::FileDragAndDropTarget,
                             public juce::DragAndDropTarget
{
public:
    PadButton (PadsPanel& p, AudioEngine& e) : owner (p), engine (e) {}

    void setIndex (int i) { index = i; glow = 0.0f; repaint(); }
    int getIndex() const { return index; }
    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }

    // From the panel's timer: the lamp's fade and the progress bar. True if it needs redrawing.
    bool step (float dt)
    {
        const bool playing = engine.getPads().isPlaying (index);
        const float g = Backlight::afterglow (glow, playing || held, dt, 0.15f);
        const float pr = playing ? engine.getPads().progress (index) : 0.0f;
        const bool changed = g != glow || std::abs (pr - shownProgress) > 0.002f;
        glow = g;
        shownProgress = pr;
        return changed;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = engine.getPads().pad (index);
        const bool has = p.sound != nullptr;
        const auto lit = padColour (p);
        const auto r = getLocalBounds().toFloat().reduced (4.0f);

        auto face = Console::drawCap (g, r, true, lit, glow, *Console::motionOf (*this));

        if (selected)
        {
            g.setColour (Console::Colours::legend().withAlpha (0.9f));
            g.drawRoundedRectangle (r.expanded (2.0f), 4.0f, 1.5f);
        }

        // Its colour as a strip across the top while dark, so pads are told apart at a glance.
        g.setColour (lit.withAlpha (has ? 0.85f * (1.0f - glow) : 0.2f));
        g.fillRect (face.reduced (6.0f, 0.0f).withHeight (3.0f).translated (0.0f, 4.0f));

        const auto ink = glow > 0.5f ? (lit.getPerceivedBrightness() > 0.7f ? juce::Colour (0xff141414) : juce::Colours::white)
                                     : Console::Colours::capText();
        auto inner = face.reduced (8.0f, 6.0f);

        // Top: its place, then how it plays, its choke group and its length.
        g.setColour (ink.withMultipliedAlpha (0.7f));
        g.setFont (Console::legendFont (Console::Type::legend));
        auto top = inner.removeFromTop (14.0f);
        g.drawText (padLabel (index), top, juce::Justification::centredLeft, false);
        if (has)
        {
            juce::StringArray tags;
            if (p.mode == Pads::Mode::hold) tags.add ("HOLD");
            if (p.mode == Pads::Mode::loop) tags.add ("LOOP");
            if (p.choke > 0) tags.add ("C" + juce::String (p.choke));
            tags.add ((p.seconds() < 10.0 ? juce::String (p.seconds(), 1) : juce::String (juce::roundToInt (p.seconds()))) + "s");
            g.drawText (tags.joinIntoString ("  "), top, juce::Justification::centredRight, false);
        }

        // Bottom: its hotkey.
        auto bottom = inner.removeFromBottom (16.0f);
        if (p.hotkey.isNotEmpty())
        {
            g.setFont (Console::legendFont (Console::Type::legend));
            g.drawFittedText (p.hotkey.toUpperCase(), bottom.withTrimmedBottom (4.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        }

        // Its name on a screen, like every readout: shortened to what stays readable there.
        if (has)
        {
            const auto screen = inner.withSizeKeepingCentre (inner.getWidth(), juce::jmin (34.0f, inner.getHeight()));
            DotMatrix::draw (g, DotMatrix::shorten (p.name, DotMatrix::charactersFor (screen)), screen, this);
        }
        else
        {
            g.setColour (ink.withMultipliedAlpha (0.75f));   // (empty: quieter than a loaded pad, still readable)
            g.setFont (juce::FontOptions (Console::Type::body));
            g.drawFittedText (p.file.isNotEmpty() ? "MISSING: " + juce::File (p.file).getFileName().toUpperCase() : juce::String ("EMPTY"),
                              inner.toNearestInt(), juce::Justification::centred, 2, 0.8f);
        }

        if (shownProgress > 0.0f)   // the sound running down: a bar along the bottom, left to right
        {
            const auto bar = face.reduced (6.0f, 0.0f).removeFromBottom (7.0f).withTrimmedBottom (3.0f);
            g.setColour (ink.withAlpha (0.2f));
            g.fillRect (bar);
            g.setColour (ink.withAlpha (0.85f));
            g.fillRect (bar.withWidth (bar.getWidth() * shownProgress));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.select (index);
        if (e.mods.isPopupMenu() || e.mods.isCtrlDown())
            return;   // right-click: just edit it; Ctrl: drag it onto another pad to swap them

        const auto& p = engine.getPads().pad (index);
        if (p.sound == nullptr && p.macro.isEmpty())
        {
            owner.chooseSound (index);
            return;
        }
        held = true;
        owner.pressPad (index);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! held)
            return;
        held = false;
        owner.releasePad (index);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isCtrlDown() && e.getDistanceFromDragStart() > 6)
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this); container != nullptr && ! container->isDragAndDropActive())
                container->startDragging ("pad:" + juce::String (index), this);
    }

    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("pad:"); }

    void itemDropped (const SourceDetails& d) override
    {
        owner.swapPads (d.description.toString().fromFirstOccurrenceOf ("pad:", false, false).getIntValue(), index);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files)
            if (juce::File (f).hasFileExtension (soundFiles))
                return true;
        return false;
    }

    // Dropped sounds fill this pad and the ones after it in the bank.
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        int target = index;
        const int bankEnd = (index / Pads::perBank + 1) * Pads::perBank;
        for (auto& f : files)
            if (juce::File (f).hasFileExtension (soundFiles) && target < bankEnd)
                if (loadSound (engine, target, juce::File (f)))
                    ++target;
        owner.select (index);
        owner.edited();
    }

private:
    PadsPanel& owner;
    AudioEngine& engine;
    int index = 0;
    bool selected = false, held = false;
    float glow = 0.0f, shownProgress = 0.0f;
};

//==============================================================================
// A pad's whole file on a screen, the part that plays bright and the trimmed-off ends dim, its
// fades drawn over it. Drag the start or end line to trim (double-click: the whole file).
class PadWave : public juce::Component,
                public juce::SettableTooltipClient
{
public:
    std::function<Pads::Pad*()> pad;
    std::function<void()> onTrimmed;   // let go after dragging a line

    PadWave() { setTooltip ("Drag the ends to trim. Double-click: whole file"); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (DotMatrix::bezel());
        g.fillRoundedRectangle (r, 2.5f);
        const auto screen = r.reduced (1.5f);
        g.setColour (DotMatrix::glass());
        g.fillRoundedRectangle (screen, 1.5f);

        auto* p = pad ? pad() : nullptr;
        if (p == nullptr || p->full == nullptr)
            return;
        refreshPeaks (*p, (int) screen.getWidth());

        const float mid = screen.getCentreY(), half = screen.getHeight() * 0.45f;
        const auto lit = DotMatrix::lit();
        const float x0 = xOf (p->trimStart), x1 = xOf (p->trimEnd > 0.0 ? p->trimEnd : p->fullSeconds());
        for (int i = 0; i < (int) peaks.size(); ++i)
        {
            const float x = screen.getX() + (float) i;
            g.setColour (lit.withAlpha (x >= x0 && x <= x1 ? 0.85f : 0.22f));
            g.drawVerticalLine ((int) x, mid - peaks[(size_t) i] * half, mid + peaks[(size_t) i] * half + 1.0f);
        }

        // The fades: up from silence at the start, down to it at the end.
        const float top = screen.getY() + 3.0f, bottom = screen.getBottom() - 3.0f;
        juce::Path fades;
        fades.startNewSubPath (x0, bottom);
        fades.lineTo (xOf (p->trimStart + p->fadeIn), top);
        fades.lineTo (xOf ((p->trimEnd > 0.0 ? p->trimEnd : p->fullSeconds()) - p->fadeOut), top);
        fades.lineTo (x1, bottom);
        g.setColour (lit.withAlpha (0.6f));
        g.strokePath (fades, juce::PathStrokeType (1.2f));

        g.setColour (lit);
        g.fillRect (x0 - 1.0f, screen.getY(), 2.0f, screen.getHeight());
        g.fillRect (x1 - 1.0f, screen.getY(), 2.0f, screen.getHeight());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* p = pad ? pad() : nullptr;
        dragging = 0;
        if (p == nullptr || p->full == nullptr)
            return;
        const float x0 = xOf (p->trimStart), x1 = xOf (p->trimEnd > 0.0 ? p->trimEnd : p->fullSeconds());
        dragging = std::abs (e.position.x - x0) <= std::abs (e.position.x - x1) ? 1 : 2;   // the nearer line
        mouseDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* p = pad ? pad() : nullptr;
        if (p == nullptr || dragging == 0)
            return;
        const double t = secondsAt (e.position.x), end = p->trimEnd > 0.0 ? p->trimEnd : p->fullSeconds();
        if (dragging == 1) p->trimStart = juce::jlimit (0.0, end - 0.01, t);
        else               p->trimEnd = juce::jlimit (p->trimStart + 0.01, p->fullSeconds(), t);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging != 0 && onTrimmed)
            onTrimmed();
        dragging = 0;
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (auto* p = pad ? pad() : nullptr)
        {
            p->trimStart = p->trimEnd = 0.0;
            if (onTrimmed)
                onTrimmed();
        }
    }

private:
    float xOf (double seconds) const
    {
        auto* p = pad ? pad() : nullptr;
        const auto screen = getLocalBounds().toFloat().reduced (1.5f);
        const double total = p != nullptr ? juce::jmax (0.001, p->fullSeconds()) : 1.0;
        return screen.getX() + (float) (juce::jlimit (0.0, total, seconds) / total) * screen.getWidth();
    }

    double secondsAt (float x) const
    {
        auto* p = pad ? pad() : nullptr;
        const auto screen = getLocalBounds().toFloat().reduced (1.5f);
        return p != nullptr ? juce::jlimit (0.0, 1.0, (double) ((x - screen.getX()) / screen.getWidth())) * p->fullSeconds() : 0.0;
    }

    // The loudest sample in each column, for the file and width shown (worked out again when either changes).
    void refreshPeaks (const Pads::Pad& p, int width)
    {
        if (p.full.get() == peaksOf && (int) peaks.size() == width)
            return;
        peaksOf = p.full.get();
        peaks.assign ((size_t) juce::jmax (0, width), 0.0f);
        const auto& audio = p.full->audio;
        const int n = audio.getNumSamples();
        for (int i = 0; i < width; ++i)
        {
            const int from = (int) ((juce::int64) n * i / width), to = juce::jmax (from + 1, (int) ((juce::int64) n * (i + 1) / width));
            peaks[(size_t) i] = juce::jmin (1.0f, juce::jmax (audio.getMagnitude (0, from, juce::jmin (to, n) - from),
                                                              audio.getMagnitude (1, from, juce::jmin (to, n) - from)));
        }
    }

    std::vector<float> peaks;
    const Pads::Sound* peaksOf = nullptr;
    int dragging = 0;   // 1: the start, 2: the end
};

//==============================================================================
// The selected pad's settings.
class PadsPanel::Editor : public juce::Component,
                          private juce::Timer
{
public:
    Editor (PadsPanel& p, AudioEngine& e) : owner (p), engine (e)
    {
        title.setFont (Console::legendFont (Console::Type::display));
        addAndMakeVisible (title);

        name.setFont (juce::FontOptions (Console::Type::display, juce::Font::bold));
        name.setEditable (false, true);
        name.setTooltip ("Double-click: rename");
        name.setColour (juce::Label::backgroundColourId, Console::Colours::slot());
        Console::capitalsOnly (name);
        name.onTextChange = [this] { pad().name = name.getText(); owner.edited(); };
        addAndMakeVisible (name);

        file.setFont (juce::FontOptions (Console::Type::body));
        file.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (file);

        load.onClick = [this] { owner.chooseSound (index); };
        clear.onClick = [this] { engine.getPads().clear (index); owner.edited(); };

        // REPLAY: what instant replay holds now becomes this pad's sound (a WAV beside the
        // recordings), to trim to the moment.
        replay.setTooltip ("Load the instant replay");
        replay.onClick = [this]
        {
            juce::String error;
            const auto saved = engine.getRecorder().replayArmed() ? engine.getRecorder().saveReplayNow ("REPLAY PAD " + padLabel (index), error) : juce::File();
            if (saved == juce::File())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "NO REPLAY",
                                                        engine.getRecorder().replayArmed() ? error : "Arm instant replay in TOOLS > TAPE first.");
                return;
            }
            pad().name = "REPLAY " + juce::Time::getCurrentTime().formatted ("%H:%M:%S");
            if (! engine.getPads().load (index, saved, engine.getSampleRate(), error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "NO REPLAY", error);
            owner.edited();
        };

        wave.pad = [this] { return &pad(); };
        wave.onTrimmed = [this] { engine.getPads().applyEdits (index); owner.edited(); };
        addAndMakeVisible (wave);

        // Fades: the knob moves the drawing at once; the sound is made again a moment after it stops.
        for (auto* k : { &fadeIn, &fadeOut })
        {
            k->knob.setRange (0.0, 5000.0, 1.0);
            k->knob.setSkewFactorFromMidPoint (500.0);
            k->knob.setDoubleClickReturnValue (true, 0.0);
            k->format = [] (double ms) { return ms >= 1000.0 ? juce::String (ms / 1000.0, 2) + " s" : juce::String ((int) ms) + " ms"; };
            addAndMakeVisible (*k);
        }
        fadeIn.onChange = [this] (double ms) { pad().fadeIn = ms / 1000.0; wave.repaint(); pending = index; startTimer (250); };
        fadeOut.onChange = [this] (double ms) { pad().fadeOut = ms / 1000.0; wave.repaint(); pending = index; startTimer (250); };
        colour.onClick = [this]
        {
            juce::PopupMenu menu;
            MixerComponent::addColourMenu (menu, pad().colour, [this] (juce::Colour c) { pad().colour = c; owner.edited(); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (colour));
        };
        for (auto* b : { &load, &replay, &clear, &colour })
            addAndMakeVisible (b);

        mode.setTooltip ("One-shot: plays out. Hold: while held. Loop: toggles");
        mode.onChange = [this] (int m) { pad().mode = (Pads::Mode) m; owner.edited(); };
        addAndMakeVisible (mode);

        choke.setTooltip ("Pads in one group stop each other");
        choke.onChange = [this] (int c) { pad().choke = c; owner.edited(); };
        addAndMakeVisible (choke);

        level.knob.setRange (-60.0, 12.0, 0.1);
        level.knob.setSkewFactorFromMidPoint (-12.0);
        level.knob.setDoubleClickReturnValue (true, 0.0);
        level.format = [] (double db) { return db <= -60.0 ? juce::String ("-INF dB") : juce::String (db, 1) + " dB"; };
        level.onChange = [this] (double db) { pad().gain = db <= -60.0 ? 0.0f : juce::Decibels::decibelsToGain ((float) db); };
        addAndMakeVisible (level);

        // Hotkey: click, then press the keys (Esc: cancel, Delete: none).
        hotkey.setTooltip ("Global hotkey");
        hotkey.onClick = [this] { capturing = true; hotkey.setButtonText ("LEARN..."); setWantsKeyboardFocus (true); grabKeyboardFocus(); };
        addAndMakeVisible (hotkey);
        hotkeyNote.setFont (juce::FontOptions (Console::Type::body));
        addAndMakeVisible (hotkeyNote);

        // A macro the pad runs as well (or on its own, with no sound).
        macro.setTooltip ("Macro to run with this pad");
        macro.onChange = [this]
        {
            const int i = macro.getSelectedId() - 2;
            const auto& list = engine.getMacros().list;
            pad().macro = juce::isPositiveAndBelow (i, (int) list.size()) ? list[(size_t) i].name : juce::String();
            owner.edited();
        };
        addAndMakeVisible (macro);

        for (auto* l : { &modeLabel, &chokeLabel, &levelLabel, &hotkeyLabel, &macroLabel, &trimLabel, &fadeInLabel, &fadeOutLabel })
        {
            l->setFont (Console::legendFont (Console::Type::body));
            addAndMakeVisible (l);
        }
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (! capturing)
            return false;

        if (key == juce::KeyPress::escapeKey)
            stopCapturing ({});
        else if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            owner.setHotkey (index, {});
            stopCapturing ({});
        }
        else if (! GlobalHotkeys::usable (key))
            { hotkeyNote.setText ("Needs Ctrl, Alt or an F key", juce::dontSendNotification); resized(); }
        else
            stopCapturing (owner.setHotkey (index, key) ? juce::String() : key.getTextDescription() + " is already in use");
        return true;
    }

    void focusLost (FocusChangeType) override
    {
        if (capturing)
            stopCapturing ({});
    }

    void show (int i)
    {
        index = i;
        const auto& p = pad();
        title.setText ("PAD " + padLabel (i), juce::dontSendNotification);
        name.setText (p.name, juce::dontSendNotification);
        file.setText (p.file.isEmpty() ? juce::String ("NO SAMPLE") : juce::File (p.file).getFileName() + (p.sound == nullptr ? "  (offline)" : ""),
                      juce::dontSendNotification);
        mode.setSelected ((int) p.mode);
        choke.setIndex (p.choke);
        level.setValue (juce::Decibels::gainToDecibels (p.gain, -60.0f));
        fadeIn.setValue (p.fadeIn * 1000.0);
        fadeOut.setValue (p.fadeOut * 1000.0);
        wave.repaint();
        if (! capturing)
            hotkey.setButtonText (p.hotkey.isEmpty() ? juce::String ("NONE") : p.hotkey.toUpperCase());
        for (auto* c : std::initializer_list<juce::Component*> { &mode, &choke, &level, &clear, &fadeIn, &fadeOut, &wave })
            c->setEnabled (p.file.isNotEmpty());
        const bool used = p.file.isNotEmpty() || p.macro.isNotEmpty();   // a macro-only pad needs no sound
        for (auto* c : std::initializer_list<juce::Component*> { &name, &colour })
            c->setEnabled (used);
        hotkey.setEnabled (used);

        macro.clear (juce::dontSendNotification);
        macro.addItem ("NONE", 1);
        const auto& list = engine.getMacros().list;
        for (int m = 0; m < (int) list.size(); ++m)
            macro.addItem (list[(size_t) m].name, m + 2);
        macro.setSelectedId (engine.getMacros().find (p.macro) + 2, juce::dontSendNotification);
    }

    void stopCapturing (const juce::String& note)
    {
        capturing = false;
        setWantsKeyboardFocus (false);
        hotkeyNote.setText (note, juce::dontSendNotification);
        resized();   // (the note takes a line only when there is one)
        show (index);
    }

    void lookAndFeelChanged() override
    {
        file.setColour (juce::Label::textColourId, Console::Colours::legendDim());
        hotkeyNote.setColour (juce::Label::textColourId, Console::Colours::legendDim());
        name.setColour (juce::Label::backgroundColourId, Console::Colours::slot());
    }

    void resized() override
    {
        namespace Space = Console::Space;
        auto r = getLocalBounds().reduced (Space::pad);
        title.setBounds (r.removeFromTop (Space::title));
        r.removeFromTop (Space::gap);
        name.setBounds (r.removeFromTop (30));
        r.removeFromTop (Space::unit);
        file.setBounds (r.removeFromTop (22));
        r.removeFromTop (Space::unit);
        evenKeys (r.removeFromTop (Space::key), { &load, &replay, &clear, &colour });
        r.removeFromTop (Space::pad);
        modeLabel.setBounds (r.removeFromTop (18));
        mode.setBounds (r.removeFromTop (Space::key));
        r.removeFromTop (Space::gap);
        chokeLabel.setBounds (r.removeFromTop (18));
        choke.setBounds (r.removeFromTop (Space::key));
        r.removeFromTop (Space::gap);
        levelLabel.setBounds (r.removeFromTop (18));
        level.setBounds (r.removeFromTop (44));
        r.removeFromTop (Space::gap);
        hotkeyLabel.setBounds (r.removeFromTop (18));
        hotkey.setBounds (r.removeFromTop (Space::key));
        if (hotkeyNote.getText().isNotEmpty())   // (only when there's something to say: the sections stay evenly spaced)
            hotkeyNote.setBounds (r.removeFromTop (22));
        else
            hotkeyNote.setBounds ({});
        r.removeFromTop (Space::gap);
        macroLabel.setBounds (r.removeFromTop (18));
        macro.setBounds (r.removeFromTop (Space::key));
        r.removeFromTop (Space::gap);
        trimLabel.setBounds (r.removeFromTop (18));
        wave.setBounds (r.removeFromTop (64));
        r.removeFromTop (Space::gap);
        auto labels = r.removeFromTop (18);
        auto knobs = r.removeFromTop (44);
        const int half = (knobs.getWidth() - Space::gap) / 2;
        fadeInLabel.setBounds (labels.removeFromLeft (half));
        fadeIn.setBounds (knobs.removeFromLeft (half));
        labels.removeFromLeft (Space::gap);
        knobs.removeFromLeft (Space::gap);
        fadeOutLabel.setBounds (labels);
        fadeOut.setBounds (knobs);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Console::Colours::panel());
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        g.setColour (Console::Colours::rim());
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
    }

private:
    Pads::Pad& pad() { return engine.getPads().pad (index); }

    // A fade knob stopped: the sound is made again with it.
    void timerCallback() override
    {
        stopTimer();
        engine.getPads().applyEdits (pending);   // the pad it was turned on, even if another is shown now
        owner.edited();
    }

    int pending = 0;

    PadsPanel& owner;
    AudioEngine& engine;
    int index = 0;
    bool capturing = false;
    juce::Label title, name, file, modeLabel { {}, "MODE" }, chokeLabel { {}, "CHOKE" }, levelLabel { {}, "LEVEL" },
                hotkeyLabel { {}, "HOTKEY" }, hotkeyNote, macroLabel { {}, "MACRO" },
                trimLabel { {}, "TRIM" }, fadeInLabel { {}, "FADE IN" }, fadeOutLabel { {}, "FADE OUT" };
    juce::TextButton load { "LOAD" }, replay { "REPLAY" }, clear { "CLEAR" }, colour { "COLOUR" }, hotkey { "NONE" };
    ConsoleControls::KeyRow mode { { "ONE-SHOT", "HOLD", "LOOP" } };
    ConsoleControls::Stepper choke { { "OFF", "1", "2", "3", "4", "5", "6", "7", "8" } };
    ConsoleControls::ValueKnob level, fadeIn, fadeOut;
    PadWave wave;
    juce::ComboBox macro;
};

//==============================================================================
// The macros: pick one (or make one), name it, run it, give it a hotkey, and edit its steps.
// Each step is an action and what it acts on, chosen from menus of the current mix.
class PadsPanel::MacroEditor : public juce::Component
{
public:
    MacroEditor (PadsPanel& p, AudioEngine& e) : owner (p), engine (e)
    {
        title.setFont (Console::legendFont (Console::Type::display));
        addAndMakeVisible (title);

        name.setFont (juce::FontOptions (Console::Type::display, juce::Font::bold));
        name.setEditable (false, true);
        name.setTooltip ("Double-click: rename");
        Console::capitalsOnly (name);
        name.onTextChange = [this]
        {
            auto* m = macro();
            const auto newName = name.getText().trim();
            if (m == nullptr || newName.isEmpty() || (newName != m->name && macros().find (newName) >= 0))
            {
                refresh();   // empty or taken: keep the old name
                return;
            }
            engine.macroRenamed (m->name, newName);   // pads and MIDI mappings follow
            m->name = newName;
            owner.macrosChanged();
        };

        run.setTooltip ("Fire");
        Console::setIcon (run, Console::Icon::play);
        run.onClick = [this] { if (macro() != nullptr) macros().run (current); };

        undo.setClickingTogglesState (true);
        undo.setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
        undo.setTooltip ("On: undo the actions when released");
        undo.onClick = [this] { if (auto* m = macro()) m->undoOnRelease = undo.getToggleState(); };

        hotkey.setTooltip ("Global hotkey");
        hotkey.onClick = [this] { capturing = true; hotkey.setButtonText ("LEARN..."); setWantsKeyboardFocus (true); grabKeyboardFocus(); };
        hotkeyNote.setFont (juce::FontOptions (Console::Type::body));

        addStep.onClick = [this]
        {
            if (auto* m = macro())
            {
                m->steps.push_back ({});
                refresh();
                steps.setViewPosition (0, holder.getHeight());
            }
        };

        addTrigger.setTooltip ("Run this macro automatically");
        addTrigger.onClick = [this]
        {
            if (auto* m = macro())
            {
                m->triggers.push_back ({});
                refresh();
            }
        };

        for (auto* c : std::initializer_list<juce::Component*> { &name, &run, &undo, &hotkey, &hotkeyNote, &addStep, &steps, &addTrigger })
            addAndMakeVisible (c);
        for (auto* l : { &hotkeyLabel, &stepsLabel, &triggersLabel })
        {
            l->setFont (Console::legendFont (Console::Type::body));
            addAndMakeVisible (l);
        }
        steps.setViewedComponent (&holder, false);
        steps.setScrollBarsShown (true, false);
        refresh();
    }

    ~MacroEditor() override { steps.setViewedComponent (nullptr, false); }

    // A new macro ("MACRO 3"), shown.
    void newMacro()
    {
        auto& list = macros().list;
        juce::String newName;
        for (int n = (int) list.size() + 1; newName.isEmpty() || macros().find (newName) >= 0; ++n)
            newName = "MACRO " + juce::String (n);
        list.push_back ({ newName, {}, false, {} });
        current = (int) list.size() - 1;
        owner.macrosChanged();
    }

    // The shown macro deleted (pads that ran it run nothing now).
    void deleteMacro()
    {
        auto& list = macros().list;
        if (! juce::isPositiveAndBelow (current, (int) list.size()))
            return;
        macros().cancelAll();
        for (int i = 0; i < Pads::count; ++i)
            if (engine.getPads().pad (i).macro == list[(size_t) current].name)
                engine.getPads().pad (i).macro.clear();
        list.erase (list.begin() + current);
        current = juce::jmin (current, (int) list.size() - 1);
        owner.macrosChanged();
    }

    int shown() const { return current; }
    std::function<void()> onRefreshed;   // the list beside it follows

    // The shown macro, after anything changed.
    void refresh()
    {
        auto& list = macros().list;
        if (current >= (int) list.size()) current = (int) list.size() - 1;
        if (current < 0 && ! list.empty()) current = 0;

        auto* m = macro();
        name.setText (m != nullptr ? m->name : juce::String(), juce::dontSendNotification);
        undo.setToggleState (m != nullptr && m->undoOnRelease, juce::dontSendNotification);
        if (! capturing)
            hotkey.setButtonText (m == nullptr || m->hotkey.isEmpty() ? juce::String ("NONE") : m->hotkey.toUpperCase());
        for (auto* c : std::initializer_list<juce::Component*> { &name, &run, &undo, &addStep, &addTrigger })
            c->setEnabled (m != nullptr);
        hotkey.setEnabled (m != nullptr);
        title.setText (m != nullptr ? juce::String ("MACRO") : juce::String ("NO MACROS: CLICK + NEW"), juce::dontSendNotification);

        rows.clear();
        if (m != nullptr)
            for (int i = 0; i < (int) m->steps.size(); ++i)
                holder.addAndMakeVisible (rows.add (new StepRow (*this, i)));
        triggerRows.clear();
        if (m != nullptr)
            for (int i = 0; i < (int) m->triggers.size(); ++i)
                addAndMakeVisible (triggerRows.add (new TriggerRow (*this, i)));
        resized();
        if (onRefreshed)
            onRefreshed();
    }

    void show (int index) { current = index; refresh(); }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (! capturing)
            return false;
        if (key == juce::KeyPress::escapeKey)
            stopCapturing ({});
        else if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            owner.setMacroHotkey (current, {});
            stopCapturing ({});
        }
        else if (! GlobalHotkeys::usable (key))
            hotkeyNote.setText ("Needs Ctrl, Alt or an F key", juce::dontSendNotification);
        else
            stopCapturing (owner.setMacroHotkey (current, key) ? juce::String() : key.getTextDescription() + " is already in use");
        return true;
    }

    void focusLost (FocusChangeType) override { if (capturing) stopCapturing ({}); }

    void lookAndFeelChanged() override
    {
        hotkeyNote.setColour (juce::Label::textColourId, Console::Colours::legendDim());
        name.setColour (juce::Label::backgroundColourId, Console::Colours::slot());
    }

    void resized() override
    {
        namespace Space = Console::Space;
        auto r = getLocalBounds().reduced (Space::pad);
        title.setBounds (r.removeFromTop (Space::title));
        r.removeFromTop (Space::gap);
        name.setBounds (r.removeFromTop (30));
        r.removeFromTop (Space::gap);
        evenKeys (r.removeFromTop (Space::key), { &run, &undo });
        r.removeFromTop (Space::gap);
        hotkeyLabel.setBounds (r.removeFromTop (18));
        hotkey.setBounds (r.removeFromTop (Space::key));
        hotkeyNote.setBounds (r.removeFromTop (20));
        r.removeFromTop (Space::unit);
        Console::KeyRow triggersHead (r.removeFromTop (Space::title));
        triggersHead.right (addTrigger, Console::KeyRow::columnsFor (addTrigger.getButtonText()));
        triggersLabel.setBounds (triggersHead.rest().withTrimmedRight (Space::pad));
        r.removeFromTop (Space::unit);
        for (auto* row : triggerRows)
        {
            row->setBounds (r.removeFromTop (triggerHeight));
            r.removeFromTop (Space::unit);
        }
        r.removeFromTop (Space::unit);
        stepsLabel.setBounds (r.removeFromTop (18));
        addStep.setBounds (r.removeFromBottom (Space::key));
        r.removeFromBottom (Space::gap);
        steps.setBounds (r);
        layoutRows();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Console::Colours::panel());
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        g.setColour (Console::Colours::rim());
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
    }

private:
    // One step: what it does (and its remove key), then what it does it to.
    class StepRow : public juce::Component
    {
    public:
        StepRow (MacroEditor& e, int i) : editor (e), index (i)
        {
            using A = Macros::Action;
            for (int a = 0; a <= (int) A::pluginSet; ++a)
                action.addItem (Macros::actionName ((A) a), a + 1);
            action.setSelectedId ((int) step().action + 1, juce::dontSendNotification);
            action.onChange = [this]
            {
                step().action = (Macros::Action) (action.getSelectedId() - 1);
                juce::MessageManager::callAsync ([e = juce::Component::SafePointer<MacroEditor> (&editor)] { if (e != nullptr) e->refresh(); });   // rows rebuild (this one too)
            };

            fillTargets (target, step().target, isPadAction(), false);
            target.onChange = [this]
            {
                pick (target, targetKeys, step().target);
                if (editorStep().action == A::pluginSet)   // its plugins' controls, in the second list
                    fillTargets (target2, step().target2, false, true);
            };
            fillTargets (target2, step().target2, false, true);
            target2.onChange = [this] { pick (target2, target2Keys, step().target2); };

            if (step().action == A::pluginSet)   // the control's setting, 0 to 100 % of its travel
            {
                value.knob.setRange (0.0, 100.0, 0.1);
                value.knob.setDoubleClickReturnValue (true, 50.0);
                value.format = [] (double v) { return juce::String (v, 1) + " %"; };
                value.setTooltip ("Control position (0-100 %)");
            }
            else
            {
                value.knob.setRange (-60.0, 12.0, 0.1);
                value.knob.setSkewFactorFromMidPoint (-12.0);
                value.knob.setDoubleClickReturnValue (true, 0.0);
                value.format = [] (double db) { return db <= -60.0 ? juce::String ("-INF dB") : juce::String (db, 1) + " dB"; };
                value.setTooltip ("Fader level");
            }
            value.setValue (step().value);
            value.onChange = [this] (double v) { step().value = (float) v; };

            ms.knob.setRange (0.0, 10000.0, 10.0);
            ms.knob.setSkewFactorFromMidPoint (1000.0);
            ms.knob.setDoubleClickReturnValue (true, 0.0);
            ms.format = [] (double t) { return t >= 1000.0 ? juce::String (t / 1000.0, 2) + " s" : juce::String ((int) t) + " ms"; };
            ms.setValue (step().ms);
            ms.onChange = [this] (double t) { step().ms = (int) t; };
            ms.setTooltip (step().action == Macros::Action::wait ? "Wait" : "Fade");

            remove.setTooltip ("Delete action");
            remove.onClick = [this]
            {
                auto& s = editor.macro()->steps;
                s.erase (s.begin() + index);
                juce::MessageManager::callAsync ([e = juce::Component::SafePointer<MacroEditor> (&editor)] { if (e != nullptr) e->refresh(); });   // this row goes
            };

            const auto a = step().action;
            for (auto* c : std::initializer_list<juce::Component*> { &action, &remove })
                addAndMakeVisible (c);
            target.setVisible (Macros::needsTarget (a));
            target2.setVisible (Macros::needsTarget2 (a));
            value.setVisible (a == A::level || a == A::pluginSet);
            ms.setVisible (a == A::level || a == A::wait);
            for (auto* c : std::initializer_list<juce::Component*> { &target, &target2, &value, &ms })
                addChildComponent (c);
        }

        // Lines: the action, then its target(s), then (a level) the level; or the wait time.
        int preferredHeight() const
        {
            const auto a = editorStep().action;
            const int lines = 1 + (Macros::needsTarget (a) ? 1 : 0);
            const bool knobs = a == Macros::Action::level || a == Macros::Action::wait || a == Macros::Action::pluginSet;
            namespace Space = Console::Space;
            return 2 * Space::unit + lines * (Space::key + Space::unit) + (knobs ? knobHeight + Space::unit : 0);
        }

        void resized() override
        {
            namespace Space = Console::Space;
            auto r = getLocalBounds().reduced (Space::gap, Space::unit);
            auto line = [&r] { auto l = r.removeFromTop (Space::key); r.removeFromTop (Space::unit); return l; };
            auto first = line();
            remove.setBounds (first.removeFromRight (Space::key));   // a square key
            first.removeFromRight (Space::unit);
            action.setBounds (first);
            const auto a = editorStep().action;
            if (Macros::needsTarget (a))
            {
                auto l = line();
                if (Macros::needsTarget2 (a))
                {
                    target.setBounds (l.removeFromLeft ((l.getWidth() - Space::unit) / 2));
                    l.removeFromLeft (Space::unit);
                    target2.setBounds (l);
                }
                else
                    target.setBounds (l);
            }
            // Level and fade knobs side by side; a wait has just its time.
            auto knobs = r.removeFromTop (knobHeight);
            const int half = (knobs.getWidth() - Space::gap) / 2;
            if (a == Macros::Action::level)
            {
                value.setBounds (knobs.removeFromLeft (half));
                knobs.removeFromLeft (Space::gap);
                ms.setBounds (knobs);
            }
            if (a == Macros::Action::wait)
                ms.setBounds (knobs.removeFromLeft (half));
            if (a == Macros::Action::pluginSet)
                value.setBounds (knobs.removeFromLeft (half));
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (Console::Colours::panelDark());
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 3.0f);
        }

    private:
        Macros::Step& step() { return editor.macro()->steps[(size_t) index]; }
        const Macros::Step& editorStep() const { return editor.macro()->steps[(size_t) index]; }
        bool isPadAction() const { const auto a = editorStep().action; return a == Macros::Action::playPad || a == Macros::Action::stopPad; }

        // Menus of what's in the mix now (or the pads), the step's choice selected (kept even if
        // it's gone, so a step for a strip that's away doesn't lose it).
        void fillTargets (juce::ComboBox& box, const juce::String& chosen, bool pads, bool destinations)
        {
            auto& keys = &box == &target ? targetKeys : target2Keys;
            keys.clear();
            box.clear (juce::dontSendNotification);
            auto& eng = editor.engine;
            auto item = [&] (const juce::String& key, const juce::String& label)
            {
                keys.add (key);
                box.addItem (label, keys.size());
            };
            const auto a = editorStep().action;
            if (&box == &target && a == Macros::Action::scene)
            {
                for (auto& s : eng.getScenes().list)
                    item ("scene:" + s.name, s.name);
            }
            else if (&box == &target2 && (a == Macros::Action::fxOn || a == Macros::Action::fxOff || a == Macros::Action::fxToggle))
            {
                for (auto& fx : VoiceChain::fxNames())
                    item ("fx:" + fx, fx);
            }
            else if (&box == &target2 && a == Macros::Action::pluginSet)
            {
                // Each plugin on the chosen input or bus, its controls under its name.
                const auto chosenTarget = editorStep().target;
                const auto kind = chosenTarget.upToFirstOccurrenceOf (":", false, false), targetName = chosenTarget.fromFirstOccurrenceOf (":", false, false);
                const std::vector<std::unique_ptr<juce::AudioPluginInstance>>* chain = nullptr;
                for (auto& s : kind == "input" ? eng.getStrips() : eng.getBuses())
                    if (kind != "output" && s->name == targetName)
                        chain = &s->inserts;
                for (size_t i = 0; chain != nullptr && i < chain->size(); ++i)
                {
                    auto& plugin = *(*chain)[i];
                    box.addSectionHeading (plugin.getName());
                    const auto& params = plugin.getParameters();
                    for (int p = 0; p < params.size(); ++p)
                        item ("plug:" + plugin.getName() + ":" + juce::String (p), params[p]->getName (40));
                }
            }
            else if (&box == &target && (a == Macros::Action::defaultOutput || a == Macros::Action::defaultInput))
            {
                for (auto& deviceName : WindowsDefault::devices (a == Macros::Action::defaultInput))   // connected now (a missing one stays listed)
                    item ("device:" + deviceName, deviceName.toUpperCase());
            }
            else if (pads)
            {
                for (int i = 0; i < Pads::count; ++i)
                {
                    const auto& p = eng.getPads().pad (i);
                    if (p.sound != nullptr || p.name.isNotEmpty())
                        item ("pad:" + juce::String (i), padLabel (i) + "  " + p.name);
                }
            }
            else
            {
                if (! destinations)
                {
                    box.addSectionHeading ("Channels");
                    for (auto& s : eng.getStrips()) item ("input:" + s->name, s->name);
                }
                box.addSectionHeading ("Buses");
                for (auto& s : eng.getBuses()) item ("bus:" + s->name, s->name);
                box.addSectionHeading ("Outputs");
                for (auto& b : eng.getMasters()) item ("output:" + b->name, b->name);
            }
            if (chosen.isNotEmpty() && ! keys.contains (chosen))
                item (chosen, chosen.fromFirstOccurrenceOf (":", false, false) + "  (missing)");
            box.setSelectedId (keys.indexOf (chosen) + 1, juce::dontSendNotification);
            box.setTextWhenNothingSelected (&box == &target2 && (a == Macros::Action::fxOn || a == Macros::Action::fxOff || a == Macros::Action::fxToggle)
                                                ? "FX..." : &box == &target2 && a == Macros::Action::pluginSet ? "PLUGIN CONTROL..." : destinations ? "TO..." : pads ? "PAD..." : &box == &target && editorStep().action == Macros::Action::scene ? "SCENE..."
                                                : a == Macros::Action::defaultOutput || a == Macros::Action::defaultInput ? "DEVICE..." : "TARGET...");
        }

        void pick (juce::ComboBox& box, const juce::StringArray& keys, juce::String& into)
        {
            const int i = box.getSelectedId() - 1;
            if (juce::isPositiveAndBelow (i, keys.size()))
                into = keys[i];
        }

        MacroEditor& editor;
        const int index;
        juce::ComboBox action, target, target2;
        juce::StringArray targetKeys, target2Keys;
        ConsoleControls::ValueKnob value, ms;
        static constexpr int knobHeight = 38;
        juce::TextButton remove { "x" };
    };

    // One trigger: when (an app starts, a device connects, an output goes silent, a time...),
    // what (the app, device, output or "HH:MM": picked from what's here now, or typed), how long
    // (an output's silence), and its remove key.
    class TriggerRow : public juce::Component
    {
    public:
        TriggerRow (MacroEditor& e, int i) : editor (e), index (i)
        {
            using W = Macros::Trigger::When;
            const char* names[] = { "APP STARTS", "APP STOPS", "DEVICE CONNECTED", "DEVICE DISCONNECTED", "OUTPUT SILENT", "OUTPUT HAS SOUND", "TIME" };
            for (int w = 0; w <= (int) W::time; ++w)
                when.addItem (names[w], w + 1);
            when.setSelectedId ((int) trigger().when + 1, juce::dontSendNotification);
            when.onChange = [this] { trigger().when = (Macros::Trigger::When) (when.getSelectedId() - 1); fill(); resized(); };

            what.setEditableText (true);
            what.setTextWhenNothingSelected ("...");
            what.onChange = [this] { trigger().what = what.getText().trim(); };

            seconds.setInputRestrictions (4, "0123456789");
            seconds.setJustification (juce::Justification::centred);
            seconds.setTooltip ("Seconds silent");
            seconds.setText (juce::String (trigger().seconds), false);
            seconds.onTextChange = [this] { trigger().seconds = juce::jmax (1, seconds.getText().getIntValue()); };

            remove.setTooltip ("Delete trigger");
            remove.onClick = [this]   // after this click returns: the refresh deletes this row
            {
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MacroEditor> (&editor), i = index]
                {
                    if (safe != nullptr && safe->macro() != nullptr && juce::isPositiveAndBelow (i, (int) safe->macro()->triggers.size()))
                    {
                        safe->macro()->triggers.erase (safe->macro()->triggers.begin() + i);
                        safe->refresh();
                    }
                });
            };
            for (auto* c : std::initializer_list<juce::Component*> { &when, &what, &seconds, &remove })
                addAndMakeVisible (c);
            fill();
        }

        void resized() override
        {
            namespace Space = Console::Space;
            auto r = getLocalBounds().withSizeKeepingCentre (getWidth(), juce::jmin (Space::key, getHeight()));
            remove.setBounds (r.removeFromRight (r.getHeight()));   // a square key
            r.removeFromRight (Space::unit);
            when.setBounds (r.removeFromLeft (r.getWidth() * 9 / 20));
            r.removeFromLeft (Space::unit);
            seconds.setVisible (trigger().when == Macros::Trigger::When::outputSilent);
            if (seconds.isVisible())
            {
                seconds.setBounds (r.removeFromRight (40));
                r.removeFromRight (Space::unit);
            }
            what.setBounds (r);
        }

    private:
        Macros::Trigger& trigger() { return editor.macro()->triggers[(size_t) index]; }

        // The choices for its kind: apps playing (and those with a channel), connected devices,
        // outputs, or times; whatever was typed stays.
        void fill()
        {
            using W = Macros::Trigger::When;
            auto& audioEngine = editor.engine;
            juce::StringArray choices;
            switch (trigger().when)
            {
                case W::appStarts: case W::appStops:
                    for (auto& a : listAudioApps()) choices.addIfNotAlreadyThere (a.exe, true);
                    for (auto& s : audioEngine.getStrips())
                        if (s->source.kind == AudioEngine::Source::app) choices.addIfNotAlreadyThere (s->source.deviceName, true);
                    break;
                case W::deviceOn: case W::deviceOff:
                    for (auto& ep : audioEngine.getEndpoints())
                        if (ep.api == "WASAPI") choices.addIfNotAlreadyThere (ep.name, true);
                    break;
                case W::outputSilent: case W::outputSound:
                    for (auto& b : audioEngine.getMasters()) choices.add (b->name);
                    break;
                case W::time:
                    for (int h = 0; h < 24; ++h) choices.add (juce::String (h).paddedLeft ('0', 2) + ":00");
                    break;
            }
            choices.sortNatural();
            what.clear (juce::dontSendNotification);
            what.addItemList (choices, 1);
            what.setText (trigger().what, juce::dontSendNotification);
            what.setTooltip (trigger().when == W::time ? "Time, HH:MM (24-hour)" : trigger().when == W::deviceOn || trigger().when == W::deviceOff
                             ? "A device, or part of its name" : "Pick or type");
        }

        MacroEditor& editor;
        const int index;
        juce::ComboBox when, what;
        juce::TextEditor seconds;
        juce::TextButton remove { "x" };
    };

    Macros& macros() { return engine.getMacros(); }
    Macros::Macro* macro() { return juce::isPositiveAndBelow (current, (int) macros().list.size()) ? &macros().list[(size_t) current] : nullptr; }

    void layoutRows()
    {
        int y = 0;
        const int width = steps.getWidth() - steps.getScrollBarThickness() - 2;
        for (auto* row : rows)
        {
            const int h = row->preferredHeight();
            row->setBounds (0, y, juce::jmax (0, width), h);
            y += h + Console::Space::unit;
        }
        holder.setSize (juce::jmax (0, width), juce::jmax (y, 1));
    }

    void stopCapturing (const juce::String& note)
    {
        capturing = false;
        setWantsKeyboardFocus (false);
        hotkeyNote.setText (note, juce::dontSendNotification);
        refresh();
    }

    PadsPanel& owner;
    AudioEngine& engine;
    int current = 0;
    bool capturing = false;
    juce::Label title { {}, "MACRO" }, name, hotkeyLabel { {}, "HOTKEY" }, hotkeyNote, stepsLabel { {}, "ACTIONS" }, triggersLabel { {}, "TRIGGERS" };
    juce::TextButton run { "FIRE" }, undo { "MOMENTARY" }, hotkey { "NONE" }, addStep { "+ ACTION" }, addTrigger { "+ TRIGGER" };
    juce::Viewport steps;
    juce::Component holder;
    juce::OwnedArray<StepRow> rows;
    juce::OwnedArray<TriggerRow> triggerRows;
    static constexpr int triggerHeight = Console::Space::row;
};

//==============================================================================
// The MACROS tab: the shown macro's editor, and at the right the list of macros to pick from,
// each with what it has (hotkey, triggers, actions) and a lamp while it runs; NEW and DELETE.
class PadsPanel::MacrosPage : public juce::Component,
                              private juce::ListBoxModel,
                              private juce::Timer
{
public:
    MacrosPage (MacroEditor& e, AudioEngine& eng) : editor (e), engine (eng)
    {
        addAndMakeVisible (editor);

        listTitle.setFont (Console::legendFont (Console::Type::display));
        count.setFont (Console::legendFont (Console::Type::body));
        count.setJustificationType (juce::Justification::centredRight);
        for (auto* l : { &listTitle, &count })
            addAndMakeVisible (l);

        list.setModel (this);
        list.setRowHeight (48);
        list.setOutlineThickness (0);
        addAndMakeVisible (list);

        newKey.setTooltip ("New macro");
        newKey.onClick = [this] { editor.newMacro(); };
        deleteKey.setTooltip ("Delete macro");
        deleteKey.onClick = [this]
        {
            const int i = editor.shown();
            if (! juce::isPositiveAndBelow (i, (int) engine.getMacros().list.size()))
                return;
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "DELETE MACRO",
                                                "Delete " + engine.getMacros().list[(size_t) i].name + "?", "DELETE", "CANCEL", this,
                                                juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MacrosPage> (this)] (int ok)
                                                {
                                                    if (ok != 0 && safe != nullptr)
                                                        safe->editor.deleteMacro();
                                                }));
        };
        for (auto* b : { &newKey, &deleteKey })
            addAndMakeVisible (b);

        editor.onRefreshed = [this] { listChanged(); };
        listChanged();
        startTimerHz (8);   // the running lamps
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Console::Colours::background());
        g.setColour (Console::Colours::panel());
        g.fillRoundedRectangle (listPanel.toFloat(), 4.0f);
        g.setColour (Console::Colours::rim());
        g.drawRoundedRectangle (listPanel.toFloat().reduced (0.5f), 4.0f, 1.0f);
    }

    void resized() override
    {
        namespace Space = Console::Space;
        auto r = getLocalBounds().reduced (Space::pad);
        listPanel = r.removeFromRight (juce::jmin (320, r.getWidth() / 3));
        r.removeFromRight (Space::pad);
        editor.setBounds (r);

        auto inside = listPanel.reduced (Space::pad);
        auto head = inside.removeFromTop (Space::title);
        count.setBounds (head.removeFromRight (Space::columns (1)));
        listTitle.setBounds (head);
        inside.removeFromTop (Space::gap);
        evenKeys (inside.removeFromBottom (Space::key), { &newKey, &deleteKey });
        inside.removeFromBottom (Space::gap);
        list.setBounds (inside);
    }

    void visibilityChanged() override
    {
        if (isShowing())
            editor.refresh();   // the mix may have changed while another tab was up
    }

    void lookAndFeelChanged() override
    {
        list.setColour (juce::ListBox::backgroundColourId, Console::Colours::panelDark());
        count.setColour (juce::Label::textColourId, Console::Colours::legendDim());
    }

private:
    int getNumRows() override { return (int) engine.getMacros().list.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        auto& macros = engine.getMacros();
        if (! juce::isPositiveAndBelow (row, (int) macros.list.size()))
            return;
        const auto& m = macros.list[(size_t) row];
        auto r = juce::Rectangle<int> (width, height);

        if (selected)
        {
            g.setColour (Console::Colours::ledGreen().withAlpha (0.16f));
            g.fillRect (r);
            g.setColour (Console::Colours::ledGreen());
            g.fillRect (r.removeFromLeft (3));
        }
        else
            r.removeFromLeft (3);
        g.setColour (Console::Colours::rim().withAlpha (0.5f));
        g.fillRect (0, height - 1, width, 1);

        // The lamp: lit while it runs.
        auto lamp = r.removeFromRight (26).toFloat().withSizeKeepingCentre (9.0f, 9.0f);
        Console::drawLED (g, lamp.getCentre(), 4.0f, Console::Colours::ledGreen(), macros.isRunning (row) ? 1.0f : 0.0f);

        r.reduce (10, 6);
        g.setColour (Console::Colours::legend());
        g.setFont (Console::legendFont (Console::Type::name));
        g.drawText (m.name, r.removeFromTop (r.getHeight() / 2), juce::Justification::bottomLeft, true);

        juce::StringArray detail;
        detail.add (juce::String ((int) m.steps.size()) + ((int) m.steps.size() == 1 ? " ACTION" : " ACTIONS"));
        if (! m.triggers.empty())
            detail.add (juce::String ((int) m.triggers.size()) + ((int) m.triggers.size() == 1 ? " TRIGGER" : " TRIGGERS"));
        if (m.hotkey.isNotEmpty())
            detail.add (m.hotkey.toUpperCase());
        if (m.undoOnRelease)
            detail.add ("MOMENTARY");
        g.setColour (Console::Colours::legendDim());
        g.setFont (Console::legendFont (Console::Type::legend));
        g.drawText (detail.joinIntoString ("  /  "), r, juce::Justification::topLeft, true);
    }

    void selectedRowsChanged (int row) override
    {
        if (row >= 0 && row != editor.shown())
            editor.show (row);
    }

    void listChanged()
    {
        const int n = getNumRows();
        list.updateContent();
        list.selectRow (editor.shown(), true, true);
        list.repaint();
        deleteKey.setEnabled (editor.shown() >= 0);
        count.setText (juce::String (n) + (n == 1 ? " MACRO" : " MACROS"), juce::dontSendNotification);
    }

    void timerCallback() override
    {
        juce::String running;
        for (int i = 0; i < getNumRows(); ++i)
            running << (engine.getMacros().isRunning (i) ? '1' : '0');
        if (running != shownRunning)
        {
            shownRunning = running;
            list.repaint();
        }
    }

    MacroEditor& editor;
    AudioEngine& engine;
    juce::Label listTitle { {}, "MACROS" }, count;
    juce::ListBox list { "Macros" };
    juce::TextButton newKey { "+ NEW" }, deleteKey { "DELETE" };
    juce::Rectangle<int> listPanel;
    juce::String shownRunning;
};

juce::Component& PadsPanel::getMacrosPage() { return *macrosPage; }

//==============================================================================
PadsPanel::PadsPanel (AudioEngine& e, MixerComponent& m) : engine (e), mixer (m)
{
    for (int b = 0; b < Pads::banks; ++b)
    {
        auto* button = bankButtons.add (new juce::TextButton ("BANK " + juce::String::charToString ((juce::juce_wchar) ('A' + b))));
        button->setClickingTogglesState (true);
        button->setRadioGroupId (1);
        button->setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
        button->onClick = [this, b] { showBank (b); };
        addAndMakeVisible (button);
    }

    stopAll.setTooltip ("Fade out all pads");
    Console::setIcon (stopAll, Console::Icon::stop);
    stopAll.onClick = [this] { engine.getPads().stopAll(); };
    addAndMakeVisible (stopAll);

    addStrip.setTooltip ("Create the PADS channel");
    addStrip.onClick = [this]
    {
        engine.addStrip ("PADS", { AudioEngine::Source::pads, 0, {}, true });
    };
    addAndMakeVisible (addStrip);

    playIntoLabel.setFont (Console::legendFont (Console::Type::body));
    playIntoLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (playIntoLabel);
    playInto.setTooltip ("Where the pads play");
    playInto.onChange = [this] { engine.setPadsTarget (playInto.getSelectedId() - 2); };   // 1: the PADS strip
    addAndMakeVisible (playInto);
    fillPlayInto();

    for (int i = 0; i < Pads::perBank; ++i)
        addAndMakeVisible (padButtons.add (new PadButton (*this, engine)));

    editor = std::make_unique<Editor> (*this, engine);
    addAndMakeVisible (*editor);
    macroEditor = std::make_unique<MacroEditor> (*this, engine);
    macrosPage = std::make_unique<MacrosPage> (*macroEditor, engine);


    for (auto* c : getChildren())
        c->setWantsKeyboardFocus (false);

    bankButtons[0]->setToggleState (true, juce::dontSendNotification);
    showBank (0);
    registerHotkeys();   // as saved with the session
    startTimerHz (30);
}

PadsPanel::~PadsPanel()
{
    engine.getMacros().onStructureChanged = nullptr;   // (it calls into the mixer, by way of this)
}

void PadsPanel::showBank (int b)
{
    bank = b;
    for (int i = 0; i < Pads::perBank; ++i)
    {
        padButtons[i]->setIndex (b * Pads::perBank + i);
        MidiMap::tag (*padButtons[i], "pad:" + juce::String (b * Pads::perBank + i));
    }
    select (b * Pads::perBank + selected % Pads::perBank);
}

void PadsPanel::select (int index)
{
    selected = index;
    for (auto* p : padButtons)
        p->setSelected (p->getIndex() == index);
    editor->show (index);
}

void PadsPanel::chooseSound (int index)
{
    chooser = std::make_unique<juce::FileChooser> ("Load sample: pad " + padLabel (index), juce::File(), soundFiles);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, index] (const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile() && loadSound (engine, index, fc.getResult()))
            select (index);
        edited();
    });
}

void PadsPanel::swapPads (int a, int b)
{
    if (! juce::isPositiveAndBelow (a, Pads::count) || a == b)
        return;
    engine.getPads().swap (a, b);
    registerHotkeys();   // hotkeys travel with their pads
    select (b);
    edited();
}

bool PadsPanel::setHotkey (int index, const juce::KeyPress& key)
{
    auto& pad = engine.getPads().pad (index);
    const auto before = pad.hotkey;
    hotkeys.remove (index + 1);
    pad.hotkey = key.isValid() ? key.getTextDescription() : juce::String();

    // One pad per key: another pad with the same key gives it up.
    if (pad.hotkey.isNotEmpty())
        for (int i = 0; i < Pads::count; ++i)
            if (i != index && engine.getPads().pad (i).hotkey == pad.hotkey)
            {
                engine.getPads().pad (i).hotkey.clear();
                hotkeys.remove (i + 1);
            }

    registerHotkeys();
    const bool ok = pad.hotkey.isEmpty() || hotkeyWorks.count (index) > 0;
    if (! ok)
        pad.hotkey = before;   // Windows refused it: keep what it had
    registerHotkeys();
    edited();
    return ok;
}

void PadsPanel::pressPad (int index)
{
    engine.getPads().press (index);
    auto& macros = engine.getMacros();
    if (const int m = macros.find (engine.getPads().pad (index).macro); m >= 0)
        macros.press (m);
}

void PadsPanel::releasePad (int index)
{
    engine.getPads().release (index);
    auto& macros = engine.getMacros();
    if (const int m = macros.find (engine.getPads().pad (index).macro); m >= 0)
        macros.release (m);
}

void PadsPanel::macrosChanged()
{
    registerHotkeys();
    macroEditor->refresh();
    editor->show (selected);
    for (auto* p : padButtons)
        p->repaint();
}

bool PadsPanel::setMacroHotkey (int macro, const juce::KeyPress& key)
{
    auto& list = engine.getMacros().list;
    if (! juce::isPositiveAndBelow (macro, (int) list.size()))
        return false;
    const auto before = list[(size_t) macro].hotkey;
    list[(size_t) macro].hotkey = key.isValid() ? key.getTextDescription() : juce::String();
    registerHotkeys();
    const bool ok = list[(size_t) macro].hotkey.isEmpty() || hotkeyWorks.count (macroHotkeyBase + macro) > 0;
    if (! ok)
    {
        list[(size_t) macro].hotkey = before;   // Windows refused it (or a pad has it): keep what it had
        registerHotkeys();
    }
    return ok;
}

void PadsPanel::registerHotkeys()
{
    hotkeys.clear();
    hotkeyWorks.clear();
    for (int i = 0; i < Pads::count; ++i)
    {
        const auto& text = engine.getPads().pad (i).hotkey;
        if (text.isEmpty())
            continue;
        if (hotkeys.add (i + 1, juce::KeyPress::createFromDescription (text),
                         [this, i] { pressPad (i); },
                         [this, i] { releasePad (i); }))
            hotkeyWorks.insert (i);
    }

    // Macros' own hotkeys (a key already a pad's is refused by Windows, so it stays the pad's).
    auto& macros = engine.getMacros();
    for (int m = 0; m < (int) macros.list.size(); ++m)
    {
        const auto& text = macros.list[(size_t) m].hotkey;
        if (text.isNotEmpty() && hotkeys.add (macroHotkeyBase + m, juce::KeyPress::createFromDescription (text),
                                              [this, m] { engine.getMacros().press (m); },
                                              [this, m] { engine.getMacros().release (m); }))
            hotkeyWorks.insert (macroHotkeyBase + m);
    }

    // Scenes' hotkeys: a press recalls the scene.
    auto& scenes = engine.getScenes();
    for (int s = 0; s < (int) scenes.list.size(); ++s)
    {
        const auto& text = scenes.list[(size_t) s].hotkey;
        if (text.isNotEmpty() && hotkeys.add (sceneHotkeyBase + s, juce::KeyPress::createFromDescription (text),
                                              [this, s] { engine.getScenes().recall (s); }, {}))
            hotkeyWorks.insert (sceneHotkeyBase + s);
    }

    for (auto& [id, extra] : extras)
        if (hotkeys.add (id, juce::KeyPress::createFromDescription (extra.key), extra.onPress, extra.onRelease))
            hotkeyWorks.insert (id);
}

bool PadsPanel::setExtraHotkey (int id, const juce::String& key, std::function<void()> onPress, std::function<void()> onRelease)
{
    const auto before = extras.count (id) > 0 ? std::optional<Extra> (extras[id]) : std::nullopt;
    if (key.isEmpty())
        extras.erase (id);
    else
        extras[id] = { key, std::move (onPress), std::move (onRelease) };
    registerHotkeys();

    const bool ok = key.isEmpty() || hotkeyWorks.count (id) > 0;
    if (! ok)
    {
        if (before) extras[id] = *before; else extras.erase (id);   // refused: keep what it had
        registerHotkeys();
    }
    return ok;
}

bool PadsPanel::setSceneHotkey (int scene, const juce::KeyPress& key)
{
    auto& list = engine.getScenes().list;
    if (! juce::isPositiveAndBelow (scene, (int) list.size()))
        return false;
    const auto before = list[(size_t) scene].hotkey;
    list[(size_t) scene].hotkey = key.isValid() ? key.getTextDescription() : juce::String();
    registerHotkeys();
    const bool ok = list[(size_t) scene].hotkey.isEmpty() || hotkeyWorks.count (sceneHotkeyBase + scene) > 0;
    if (! ok)
    {
        list[(size_t) scene].hotkey = before;   // Windows refused it (or a pad or macro has it): keep what it had
        registerHotkeys();
    }
    return ok;
}

void PadsPanel::scenesChanged()
{
    registerHotkeys();
    if (macroEditor->isVisible())
        macroEditor->refresh();   // scene steps list the scenes
}

void PadsPanel::edited()
{
    for (auto* p : padButtons)
        p->repaint();
    editor->show (selected);
}

// The PADS strip, then every other input by name; the current choice selected.
void PadsPanel::fillPlayInto()
{
    juce::String shown;
    for (auto& s : engine.getStrips())
        shown << s->name << '|' << (int) s->source.kind << ';';
    shown << engine.getPadsTarget();
    if (shown == playIntoShown)
        return;
    playIntoShown = shown;

    playInto.clear (juce::dontSendNotification);
    playInto.addItem ("PADS CHANNEL", 1);
    auto& strips = engine.getStrips();
    for (int i = 0; i < (int) strips.size(); ++i)
        if (strips[(size_t) i]->source.kind != AudioEngine::Source::pads)
            playInto.addItem (strips[(size_t) i]->name, i + 2);
    playInto.setSelectedId (engine.getPadsTarget() + 2, juce::dontSendNotification);
}

bool PadsPanel::hasPadsStrip() const
{
    return std::any_of (engine.getStrips().begin(), engine.getStrips().end(),
                        [] (auto& s) { return s->source.kind == AudioEngine::Source::pads; });
}

void PadsPanel::timerCallback()
{
    for (auto* p : padButtons)
        if (p->step (1.0f / 30.0f))
            p->repaint();

    fillPlayInto();   // inputs added, renamed or removed elsewhere

    if (addStrip.isEnabled() == hasPadsStrip())   // says when it's there
    {
        addStrip.setEnabled (! hasPadsStrip());
        addStrip.setButtonText (hasPadsStrip() ? "PADS CHANNEL ACTIVE" : "CREATE PADS CHANNEL");
    }
}

void PadsPanel::resized()
{
    using namespace Console;
    auto r = getLocalBounds().reduced (Space::pad);
    {
        KeyRow bar (r.removeFromTop (Space::key));
        for (auto* b : bankButtons)
            bar.left (*b, KeyRow::columnsFor (b->getButtonText()));
        bar.group().left (stopAll, KeyRow::columnsFor (stopAll.getButtonText()));
        bar.right (addStrip, juce::jmax (KeyRow::columnsFor ("CREATE PADS CHANNEL"), KeyRow::columnsFor ("PADS CHANNEL ACTIVE"))).groupRight()
           .right (playInto, 3).rightWidth (playIntoLabel, Space::column);
    }
    r.removeFromTop (Space::pad);

    editor->setBounds (r.removeFromRight (320));
    r.removeFromRight (Space::pad);

    // A square 4x4 grid, as big as fits, from the top left. Each pad draws its key a unit in from
    // its edges, so the cells reach a unit past the content edge: the keys line up with the keys
    // above and the editor, a gap apart.
    const auto area = r.expanded (Space::unit);
    const int cell = juce::jmin (area.getWidth() / 4, area.getHeight() / 4);
    gridArea = area.withSize (cell * 4, cell * 4);
    for (int i = 0; i < Pads::perBank; ++i)
        padButtons[i]->setBounds (gridArea.getX() + (i % 4) * cell, gridArea.getY() + (i / 4) * cell, cell, cell);
}

void PadsPanel::paint (juce::Graphics& g)
{
    g.fillAll (Console::Colours::background());
}

#include "TapePanel.h"
#include "MixerComponent.h"
#include "Console.h"
#include "DotMatrix.h"
#include "Hotkeys.h"
#include "MidiMap.h"

namespace
{
    constexpr const char* soundFiles = "*.wav;*.flac;*.aif;*.aiff;*.ogg;*.mp3";
    constexpr int fps = 30;

    juce::String clock (double seconds)   // "3:07", "1:02:45"
    {
        const int s = (int) seconds;
        const auto mmss = juce::String (s / 60 % 60).paddedLeft ('0', s >= 3600 ? 2 : 1) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
        return s >= 3600 ? juce::String (s / 3600) + ":" + mmss : mmss;
    }

    // A rough time for the space estimates: "~41 H 20 M", "~35 M", "<1 M".
    juce::String roughTime (double seconds)
    {
        const auto m = (juce::int64) (seconds / 60.0);
        if (m < 1)
            return "<1 M";
        return "~" + (m >= 60 ? juce::String (m / 60) + " H " : juce::String()) + juce::String (m % 60) + " M";
    }

    juce::String trackName (const juce::File& f) { return f.getFileNameWithoutExtension().toUpperCase(); }

    constexpr float holdToWind = 0.35f;   // seconds REW/FF must be held before it winds
    constexpr double windSpeed = 12.0;    // times normal speed
}

TapePanel::TapePanel (AudioEngine& e, MixerComponent& m) : engine (e), tape (e.getTape()), mixer (m)
{
    // Transport keys. PLAY and PAUSE light while the deck is in that state (latching keys, so they
    // get the lamps' fade; the timer sets their state from the deck).
    for (auto* b : { &play, &pause })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (b);
    }
    play.setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
    pause.setColour (juce::TextButton::buttonOnColourId, Console::Colours::ledYellow());

    prev.onClick  = [this] { tape.previous(); };
    rew.onClick   = [this] { if (winding != 0) finishWinding(); else tape.skip (-10.0); };
    play.onClick  = [this] { if (! tape.isPlaying()) tape.togglePlay(); };
    pause.onClick = [this] { if (tape.isPlaying()) tape.togglePlay(); };
    stop.onClick  = [this] { tape.stop(); };
    ff.onClick    = [this] { if (winding != 0) finishWinding(); else tape.skip (10.0); };
    next.onClick  = [this] { tape.next(); };
    for (auto [key, target] : std::initializer_list<std::pair<juce::Button*, const char*>> {
             { &prev, "tape:prev" }, { &play, "tape:play" }, { &pause, "tape:pause" }, { &stop, "tape:stop" }, { &next, "tape:next" } })
        MidiMap::tag (*key, target);
    rew.setTooltip ("Back 10 s. Hold: rewind");
    ff.setTooltip ("Forward 10 s. Hold: fast-forward");
    prev.setTooltip ("Previous / start of track");
    next.setTooltip ("Next track");
    play.setTooltip ("Play");
    pause.setTooltip ("Pause");
    stop.setTooltip ("Stop");
    using Console::Icon;
    for (auto [b, icon] : { std::pair<juce::Button*, Icon> { &prev, Icon::prev }, { &rew, Icon::rewind }, { &play, Icon::play }, { &pause, Icon::pause },
                            { &stop, Icon::stop }, { &ff, Icon::forward }, { &next, Icon::next }, { &shuffle, Icon::shuffle },
                            { &addFilesButton, Icon::addFile }, { &addFolderButton, Icon::addFolder }, { &eject, Icon::eject },
                            { &rec, Icon::record }, { &recordFolder, Icon::folder }, { &openFolder, Icon::open } })
        Console::setIcon (*b, icon);
    shuffle.setTooltip ("Shuffle");
    addFilesButton.setTooltip ("Add files");
    addFolderButton.setTooltip ("Add folder");
    for (auto* b : { &prev, &rew, &stop, &ff, &next })
        addAndMakeVisible (b);

    shuffle.setClickingTogglesState (true);
    shuffle.setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
    shuffle.setToggleState (tape.getShuffle(), juce::dontSendNotification);
    shuffle.onClick = [this] { tape.setShuffle (shuffle.getToggleState()); };
    addAndMakeVisible (shuffle);

    repeat.setClickingTogglesState (true);   // a latching key, so it gets the lamps; showRepeat() sets it
    repeat.setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
    repeat.onClick = [this]
    {
        tape.repeat = tape.repeat == Tape::Repeat::all ? Tape::Repeat::one
                    : tape.repeat == Tape::Repeat::one ? Tape::Repeat::off : Tape::Repeat::all;
        tape.setShuffle (tape.getShuffle());   // the track after this one may have changed
        showRepeat();
    };
    addAndMakeVisible (repeat);
    showRepeat();

    position.setSliderStyle (juce::Slider::LinearBar);
    position.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    position.setRange (0.0, 1.0);
    position.setColour (juce::Slider::trackColourId, Console::Colours::ledBlue().withAlpha (0.7f));
    position.onDragEnd = [this] { tape.seek (position.getValue()); };
    addAndMakeVisible (position);

    levelLabel.setFont (Console::legendFont (Console::Type::body));
    levelLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (levelLabel);
    level.setSliderStyle (juce::Slider::LinearHorizontal);
    level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 26);
    level.setRange (-60.0, 12.0, 0.1);
    level.setSkewFactorFromMidPoint (-12.0);
    level.setDoubleClickReturnValue (true, 0.0);
    level.setTextValueSuffix (" dB");
    level.setValue (juce::Decibels::gainToDecibels (tape.getLevel(), -60.0f), juce::dontSendNotification);
    level.onValueChange = [this] { tape.setLevel (level.getValue() <= -60.0 ? 0.0f : juce::Decibels::decibelsToGain ((float) level.getValue())); };
    addAndMakeVisible (level);

    // The playlist.
    addFilesButton.onClick = [this] { choose (false); };
    addFolderButton.onClick = [this] { choose (true); };
    eject.setTooltip ("Clear playlist");
    eject.onClick = [this] { tape.clearPlaylist(); list.updateContent(); list.deselectAllRows(); };
    addStrip.setTooltip ("Create the TAPE channel");
    addStrip.onClick = [this]
    {
        engine.addStrip ("TAPE", { AudioEngine::Source::tape, 0, {}, true });
    };
    for (auto* b : { &addFilesButton, &addFolderButton, &eject, &addStrip })
        addAndMakeVisible (b);

    playlistLabel.setFont (Console::legendFont (Console::Type::body));
    addAndMakeVisible (playlistLabel);
    list.setMultipleSelectionEnabled (true);
    list.setRowHeight (Console::Space::row);
    list.setColour (juce::ListBox::backgroundColourId, Console::Colours::panelDark());
    addAndMakeVisible (list);


    // The recorder.
    recorderLabel.setFont (Console::legendFont (Console::Type::body));
    addAndMakeVisible (recorderLabel);
    recordSource.setTooltip ("Record source");
    recordSource.onChange = [this]
    {
        const int i = recordSource.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (i, (int) recordSources.size()))
            engine.getRecorder().chosen = recordSources[(size_t) i].key;
    };
    addAndMakeVisible (recordSource);
    fillRecordSources();

    rec.setClickingTogglesState (true);
    rec.setColour (juce::TextButton::buttonOnColourId, Console::Colours::ledRed());
    Backlight::setBlinks (rec);   // pulses while recording
    rec.setTooltip ("Record");
    rec.onClick = [this] { toggleRecording(); };
    MidiMap::tag (rec, "rec");
    addAndMakeVisible (rec);

    recordFolder.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Recording path", engine.getRecorder().folder);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().isDirectory())
            {
                engine.getRecorder().folder = fc.getResult();
                recordFolder.setTooltip (fc.getResult().getFullPathName());
            }
        });
    };
    recordFolder.setTooltip (engine.getRecorder().folder.getFullPathName());
    openFolder.setTooltip ("Reveal in Explorer");
    openFolder.onClick = [this]
    {
        auto folder = engine.getRecorder().folder;
        folder.createDirectory();
        folder.startAsProcess();
    };
    // File format, for recordings and replays.
    {
        const auto formats = Recorder::formats();
        for (int i = 0; i < (int) formats.size(); ++i)
        {
            recordFormat.addItem (formats[(size_t) i].name, i + 1);
            if (formats[(size_t) i].id == engine.getRecorder().format)
                recordFormat.setSelectedId (i + 1, juce::dontSendNotification);
        }
        if (recordFormat.getSelectedId() == 0)
            recordFormat.setSelectedId (1, juce::dontSendNotification);
        recordFormat.setTooltip ("File format, and how much time fits");
        recordFormat.onChange = [this]
        {
            const auto formats = Recorder::formats();
            if (const int i = recordFormat.getSelectedId() - 1; juce::isPositiveAndBelow (i, (int) formats.size()))
                engine.getRecorder().format = formats[(size_t) i].id;
        };
        addAndMakeVisible (recordFormat);
    }

    // Instant replay.
    replayLabel.setFont (Console::legendFont (Console::Type::body));
    addAndMakeVisible (replayLabel);
    for (int i = 0; i < (int) std::size (replayLengths); ++i)
        if (juce::approximatelyEqual (engine.getRecorder().replayLength, replayLengths[i]))
            replayLengthIndex = i;
    replayLength.setIndex (replayLengthIndex);
    replayLength.setTooltip ("Replay length");
    replayLength.onChange = [this] (int i)
    {
        replayLengthIndex = i;
        if (replayArm.getToggleState())
            engine.getRecorder().replayLength = replayLengths[i];
        keepReplayArmed();
    };
    addAndMakeVisible (replayLength);
    replayArm.setClickingTogglesState (true);
    replayArm.setColour (juce::TextButton::buttonOnColourId, Console::Colours::selected());
    replayArm.setToggleState (engine.getRecorder().replayLength > 0.0, juce::dontSendNotification);
    replayArm.setTooltip ("Instant replay on/off");
    replayArm.onClick = [this]
    {
        engine.getRecorder().replayLength = replayArm.getToggleState() ? replayLengths[replayLengthIndex] : 0.0;
        keepReplayArmed();
    };
    addAndMakeVisible (replayArm);
    replaySave.onPress = [this] (bool menu)
    {
        if (! menu)
            return saveReplay();
        juce::PopupMenu m;
        m.addSectionHeader ("REPLAY");
        m.addItem (replayHotkey.isEmpty() ? juce::String ("Assign hotkey...") : "Hotkey: " + replayHotkey + "...",
                   true, false, [this]
        {
            learningReplayKey = true;
            replaySave.setButtonText ("LEARN...");
            setWantsKeyboardFocus (true);
            grabKeyboardFocus();
        });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (replaySave));
    };
    MidiMap::tag (replaySave, "replay");
    addAndMakeVisible (replaySave);

    for (auto* b : { &recordFolder, &openFolder })
        addAndMakeVisible (b);

    for (auto* c : getChildren())
        if (c != &list)
            c->setWantsKeyboardFocus (false);

    startTimerHz (fps);
}

TapePanel::~TapePanel() = default;

// REPEAT lights while it's on; a 1 in its arrows for repeating one track.
void TapePanel::showRepeat()
{
    repeat.setButtonText (tape.repeat == Tape::Repeat::all ? "REPEAT ALL" : tape.repeat == Tape::Repeat::one ? "REPEAT ONE" : "REPEAT OFF");
    repeat.setTooltip (repeat.getButtonText().substring (0, 1) + repeat.getButtonText().substring (1).toLowerCase());
    Console::setIcon (repeat, tape.repeat == Tape::Repeat::one ? Console::Icon::repeatOne : Console::Icon::repeat);
    repeat.setToggleState (tape.repeat != Tape::Repeat::off, juce::dontSendNotification);
}

bool TapePanel::hasTapeStrip() const
{
    return std::any_of (engine.getStrips().begin(), engine.getStrips().end(),
                        [] (auto& s) { return s->source.kind == AudioEngine::Source::tape; });
}

//==============================================================================
void TapePanel::addFiles (const juce::Array<juce::File>& files)
{
    tape.update (engine.getSampleRate());
    for (auto& f : files)
        tape.add (f);
    list.updateContent();
    list.repaint();
}

void TapePanel::choose (bool folders)
{
    chooser = std::make_unique<juce::FileChooser> (folders ? "Add folder" : "Add files", juce::File(), folders ? juce::String() : juce::String (soundFiles));
    const int mode = juce::FileBrowserComponent::openMode
                   | (folders ? juce::FileBrowserComponent::canSelectDirectories
                              : juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems);
    chooser->launchAsync (mode,[this] (const juce::FileChooser& fc) { addFiles (fc.getResults()); });
}

void TapePanel::removeSelected()
{
    const auto rows = list.getSelectedRows();
    for (int i = rows.size(); --i >= 0;)   // from the end, so the indices stay put
        tape.remove (rows[i]);
    list.deselectAllRows();
    list.updateContent();
    list.repaint();
}

bool TapePanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    return std::any_of (files.begin(), files.end(), [] (const juce::String& f)
                        { return juce::File (f).isDirectory() || Tape::isSoundFile (juce::File (f)); });
}

void TapePanel::filesDropped (const juce::StringArray& files, int, int)
{
    juce::Array<juce::File> dropped;
    for (auto& f : files)
        dropped.add (juce::File (f));
    addFiles (dropped);
}

//==============================================================================
int TapePanel::getNumRows() { return tape.size(); }

void TapePanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, tape.size()))
        return;

    if (selected)
        g.fillAll (Console::Colours::panel().brighter (0.25f));

    // The current track: a lamp and its name lit.
    const bool current = row == tape.current();
    auto r = juce::Rectangle<int> (width, height).reduced (8, 0);
    if (current)
        Console::drawLED (g, r.removeFromLeft (14).toFloat().getCentre(), 4.0f,
                          tape.isPlaying() ? Console::Colours::ledGreen() : Console::Colours::ledYellow(), 1.0f);
    else
        r.removeFromLeft (14);
    r.removeFromLeft (6);

    g.setFont (Console::legendFont (Console::Type::body));
    g.setColour (Console::Colours::legendDim());
    g.drawText (juce::String (row + 1), r.removeFromLeft (32), juce::Justification::centredLeft);
    g.setColour (current ? Console::Colours::legend() : Console::Colours::legend().withAlpha (0.8f));
    g.drawText (trackName (tape.file (row)).toUpperCase(), r, juce::Justification::centredLeft, true);
}

void TapePanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    tape.update (engine.getSampleRate());
    tape.play (row);
}

void TapePanel::deleteKeyPressed (int) { removeSelected(); }

// Clicked away while learning the replay key: it stops learning (a later key isn't taken for it).
void TapePanel::focusLost (FocusChangeType)
{
    if (learningReplayKey)
    {
        learningReplayKey = false;   // (the timer puts SAVE's label back)
        setWantsKeyboardFocus (false);
    }
}

// One track at a time can be dragged to a new place in the playlist.
juce::var TapePanel::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    return rows.size() == 1 ? juce::var ("tape-row:" + juce::String (rows[0])) : juce::var();
}

bool TapePanel::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.toString().startsWith ("tape-row:");
}

int TapePanel::dropRowAt (juce::Point<int> panelPosition) const
{
    const auto p = list.getLocalPoint (this, panelPosition);
    return juce::jlimit (0, tape.size(), list.getInsertionIndexForPosition (p.x, p.y));
}

void TapePanel::itemDragMove (const SourceDetails& details)
{
    if (const int row = dropRowAt (details.localPosition); row != dropRow)
    {
        dropRow = row;
        repaint (list.getBounds().expanded (0, 2));
    }
}

void TapePanel::itemDragExit (const SourceDetails&)
{
    dropRow = -1;
    repaint (list.getBounds().expanded (0, 2));
}

void TapePanel::itemDropped (const SourceDetails& details)
{
    const int from = details.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
    const int before = dropRowAt (details.localPosition);   // the gap it was dropped in
    dropRow = -1;
    const int to = before > from ? before - 1 : before;
    if (juce::isPositiveAndBelow (from, tape.size()) && to != from)
    {
        tape.move (from, to);
        list.selectRow (to);
        list.updateContent();
    }
    repaint();
}

// The line where a dragged track would land.
void TapePanel::paintOverChildren (juce::Graphics& g)
{
    if (dropRow < 0 || tape.size() == 0)
        return;
    const auto row = list.getRowPosition (juce::jmin (dropRow, tape.size() - 1), true);
    const int y = list.getY() + (dropRow < tape.size() ? row.getY() : row.getBottom());
    g.setColour (Console::Colours::ledBlue());
    g.fillRect (list.getX() + 4, y - 1, list.getWidth() - 8, 2);
}

//==============================================================================
// Every output, bus and input, refilled when they change; the saved choice selected.
void TapePanel::fillRecordSources()
{
    juce::String shown;
    for (auto& b : engine.getMasters())  shown << b->name << '|';
    shown << '#';
    for (auto& p : engine.getBuses())  shown << p->name << '|';
    shown << '#';
    for (auto& s : engine.getStrips()) shown << s->name << '|';
    if (shown == recordSourcesShown)
        return;
    recordSourcesShown = shown;

    recordSources.clear();
    recordSource.clear (juce::dontSendNotification);
    auto add = [this] (Recorder::Kind kind, int index, const juce::String& name, const juce::String& prefix, const juce::String& heading)
    {
        if (index == 0)
            recordSource.addSectionHeading (heading);
        recordSources.push_back ({ kind, index, name, prefix + ":" + name });
        recordSource.addItem (name, (int) recordSources.size());
    };
    // First: every input and output at once, each to its own file.
    recordSource.addSectionHeading ("Multitrack");
    recordSources.push_back ({ Recorder::Kind::output, -1, "ALL CHANNELS", multitrackKey });
    recordSource.addItem ("ALL CHANNELS, ONE FILE EACH", 1);
    for (int i = 0; i < (int) engine.getMasters().size(); ++i)  add (Recorder::Kind::output, i, engine.getMasters()[(size_t) i]->name, "output", "Outputs");
    for (int i = 0; i < (int) engine.getBuses().size(); ++i)  add (Recorder::Kind::bus, i, engine.getBuses()[(size_t) i]->name, "bus", "Buses");
    for (int i = 0; i < (int) engine.getStrips().size(); ++i) add (Recorder::Kind::input, i, engine.getStrips()[(size_t) i]->name, "input", "Inputs");

    int selected = recordSources.size() > 1 ? 2 : 1;   // the first output, unless one was chosen
    for (size_t i = 0; i < recordSources.size(); ++i)
        if (recordSources[i].key == engine.getRecorder().chosen)
            selected = (int) i + 1;
    recordSource.setSelectedId (selected, juce::dontSendNotification);
    // (A recording follows its source when strips move; the engine ends it if the source goes.)
}

void TapePanel::toggleRecording()
{
    // From the recorder, not the key: a click has toggled the key already, a MIDI key hasn't.
    if (engine.getRecorder().isRecording())
    {
        engine.stopRecording();
        rec.setToggleState (false, juce::dontSendNotification);
        return;
    }
    rec.setToggleState (true, juce::dontSendNotification);

    const int i = recordSource.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow (i, (int) recordSources.size()))
    {
        rec.setToggleState (false, juce::dontSendNotification);
        return;
    }

    const auto& s = recordSources[(size_t) i];
    juce::String error;
    const auto started = s.key == multitrackKey ? engine.startMultitrack (engine.getRecorder().folder, error)
                                                : engine.startRecording (s.kind, s.index, s.name, engine.getRecorder().folder, error);
    if (started == juce::File())
    {
        rec.setToggleState (false, juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "RECORD FAILED", error);
    }
    recordBytes = 0;
}

void TapePanel::keepReplayArmed()
{
    int i = recordSource.getSelectedId() - 1;
    if (i == 0 && recordSources.size() > 1)
        i = 1;   // multitrack chosen: the replay keeps the first output
    const double seconds = engine.getRecorder().replayLength;
    if (seconds > 0.0 && juce::isPositiveAndBelow (i, (int) recordSources.size()))
        engine.armReplay (recordSources[(size_t) i].kind, recordSources[(size_t) i].index, seconds);
    else
        engine.armReplay (Recorder::Kind::output, -1, 0.0);
}

void TapePanel::saveReplay()
{
    int i = recordSource.getSelectedId() - 1;
    if (i == 0 && recordSources.size() > 1)
        i = 1;   // as keepReplayArmed
    if (! engine.getRecorder().replayArmed() || ! juce::isPositiveAndBelow (i, (int) recordSources.size()))
        return;
    juce::String error;
    if (engine.getRecorder().saveReplay (recordSources[(size_t) i].name, error) == juce::File())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "REPLAY NOT SAVED", error);
    else
        replaySavedAt = juce::Time::getMillisecondCounterHiRes();
}

bool TapePanel::keyPressed (const juce::KeyPress& key)
{
    if (! learningReplayKey)
        return false;
    juce::String text;
    if (key == juce::KeyPress::escapeKey)
        text = replayHotkey;
    else if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        text = {};
    else if (GlobalHotkeys::usable (key))
        text = key.getTextDescription();
    else
        return true;   // needs Ctrl, Alt or an F key: keep listening

    if (text != replayHotkey)
    {
        if (setReplayHotkey && setReplayHotkey (text))
            replayHotkey = text;
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "HOTKEY IN USE", text + " is already in use.");
    }
    learningReplayKey = false;
    setWantsKeyboardFocus (false);
    return true;
}

void TapePanel::finishWinding()
{
    tape.seek (windPosition);
    if (playAfterWinding)
        tape.togglePlay();   // carries on from there
    winding = 0;
    playAfterWinding = false;
}

void TapePanel::timerCallback()
{
    const float dt = 1.0f / fps;
    tape.update (engine.getSampleRate());

    // REW/FF held: wind. The tape pauses (it fades out), the position runs along fast, and
    // letting go puts the tape there.
    rewHeld = rew.isDown() ? rewHeld + dt : 0.0f;
    ffHeld = ff.isDown() ? ffHeld + dt : 0.0f;
    const int held = ffHeld > holdToWind ? 1 : rewHeld > holdToWind ? -1 : 0;
    if (held != 0 && winding == 0 && tape.isLoaded())
    {
        winding = held;
        windPosition = tape.position();
        playAfterWinding = tape.isPlaying();
        if (playAfterWinding)
            tape.togglePlay();
    }
    if (winding != 0)
    {
        if (! rew.isDown() && ! ff.isDown())
            finishWinding();   // let go somewhere off the key (so no click to do it)
        else
            windPosition = juce::jlimit (0.0, tape.length(), windPosition + winding * windSpeed * dt);
    }

    play.setToggleState (tape.isPlaying(), juce::dontSendNotification);
    pause.setToggleState (tape.isLoaded() && ! tape.isPlaying() && tape.position() > 0.0 && winding == 0, juce::dontSendNotification);

    // The position bar follows the tape (or the winding), except while it's being dragged.
    const double length = tape.length();
    const double at = winding != 0 ? windPosition : position.isMouseButtonDown() ? position.getValue() : tape.position();
    if (! position.isMouseButtonDown())
    {
        position.setRange (0.0, juce::jmax (0.001, length), 0.0);
        position.setValue (at, juce::dontSendNotification);
    }

    // The screens: track name, elapsed and remaining time.
    const int current = tape.current();
    const bool hasTrack = juce::isPositiveAndBelow (current, tape.size());
    const auto name = hasTrack ? trackName (tape.file (current))   // a long name scrolls
                    : tape.size() > 0 ? juce::String (tape.size()) + " TRACKS" : juce::String ("NO MEDIA");

    const auto elapsed = tape.isLoaded() ? clock (at) : juce::String ("-:--");
    const auto remaining = tape.isLoaded() ? "-" + clock (juce::jmax (0.0, length - at)) : juce::String ("-:--");
    if (name != shownName || elapsed != shownElapsed || remaining != shownRemaining)
    {
        shownName = name;
        shownElapsed = elapsed;
        shownRemaining = remaining;
        repaint (nameScreen.getUnion (elapsedScreen).getUnion (remainingScreen).getSmallestIntegerContainer());
    }

    if (current != shownCurrent || tape.size() != shownSize)
    {
        shownCurrent = current;
        shownSize = tape.size();
        list.updateContent();
        list.repaint();
    }

    // The recorder: REC lit while recording; the source and folder stay put during a recording.
    const auto& recorder = engine.getRecorder();
    const bool recording = recorder.isRecording();
    fillRecordSources();
    rec.setToggleState (recording, juce::dontSendNotification);
    recordSource.setEnabled (! recording);
    recordFormat.setEnabled (! recording);

    recordFolder.setEnabled (! recording);
    if (frame++ % fps == 0)   // once a second: the file's size, and how long the drive has room for
    {
        if (recording)
            recordBytes = recorder.currentFile().getSize();
        const int tracks = recording ? (recorder.isMultitracking() ? recorder.trackCount() : 1)
                         : recordSource.getSelectedId() == 1 ? (int) (engine.getStrips().size() + engine.getMasters().size()) : 1;
        const double rate = engine.getSampleRate();
        recordRoom = roughTime (recorder.secondsThatFit (recorder.format, rate, tracks));

        // Each format in the list with how long fits, so the choice can be made on it.
        if (! recording)
        {
            const auto formats = Recorder::formats();
            for (int i = 0; i < (int) formats.size(); ++i)
            {
                const auto text = formats[(size_t) i].name + "   " + roughTime (recorder.secondsThatFit (formats[(size_t) i].id, rate, tracks));
                if (recordFormat.getItemText (i) != text)
                    recordFormat.changeItemText (i + 1, text);
            }
        }
    }
    const auto record = ! recording ? "READY  " + recordRoom
                      : recorder.isMultitracking() ? clock (recorder.seconds()) + "  " + juce::String (recorder.trackCount()) + " TRACKS  " + recordRoom + " LEFT"
                      : clock (recorder.seconds()) + "  " + juce::String ((double) recordBytes / (1024.0 * 1024.0), 1) + " MB  " + recordRoom + " LEFT"
                          + (recorder.droppedBlocks() > 0 ? "  DROPS " + juce::String (recorder.droppedBlocks()) : juce::String());
    if (record != shownRecord)
    {
        shownRecord = record;
        repaint (recordScreen.getSmallestIntegerContainer());
    }

    // Replay: armed on the record source; SAVE shows how much is buffered.
    keepReplayArmed();
    if (! learningReplayKey)
    {
        const auto& r = engine.getRecorder();
        const bool justSaved = juce::Time::getMillisecondCounterHiRes() - replaySavedAt < 1500.0;
        const auto text = justSaved ? juce::String ("SAVED") : r.replayArmed() ? "SAVE " + clock (r.replayBuffered()) : juce::String ("SAVE");
        if (replaySave.getButtonText() != text)
            replaySave.setButtonText (text);
        replaySave.setEnabled (r.replayArmed());
        replaySave.setTooltip ("Save replay" + (replayHotkey.isEmpty() ? juce::String() : "  (" + replayHotkey + ")") + ". Right-click: hotkey");
    }

    if (addStrip.isEnabled() == hasTapeStrip())   // says when it's there
    {
        addStrip.setEnabled (! hasTapeStrip());
        addStrip.setButtonText (hasTapeStrip() ? "TAPE CHANNEL ACTIVE" : "CREATE TAPE CHANNEL");
    }
}

void TapePanel::resized()
{
    using namespace Console;
    auto r = getLocalBounds().reduced (Space::pad);

    // The file keys along the top, all one width; the tape channel key at the right.
    {
        int columns = 1;
        for (auto* b : { &addFilesButton, &addFolderButton, &eject })
            columns = juce::jmax (columns, KeyRow::columnsFor (b->getButtonText()));
        KeyRow bar (r.removeFromTop (Space::key));
        bar.left (addFilesButton, columns).left (addFolderButton, columns).left (eject, columns);
        bar.right (addStrip, juce::jmax (KeyRow::columnsFor ("CREATE TAPE CHANNEL"), KeyRow::columnsFor ("TAPE CHANNEL ACTIVE")));
    }
    r.removeFromTop (Space::pad);

    // The deck: a fixed width on the left; the playlist takes the rest.
    auto deck = r.removeFromLeft (juce::jmin (560, r.getWidth() / 2));
    r.removeFromLeft (Space::pad);

    nameScreen = deck.removeFromTop (44).toFloat();
    deck.removeFromTop (Space::gap);
    auto times = deck.removeFromTop (36);
    elapsedScreen = times.removeFromLeft ((times.getWidth() - Space::gap) / 2).toFloat();
    times.removeFromLeft (Space::gap);
    remainingScreen = times.toFloat();
    deck.removeFromTop (Space::gap);
    position.setBounds (deck.removeFromTop (20));
    deck.removeFromTop (Space::pad);

    // Every key on the deck sits on one grid of seven equal columns, a unit apart, so the rows
    // line up: `span (row, first, n)` is n columns of a row from column `first`.
    const int columnWidth = (deck.getWidth() - 6 * Space::unit) / 7;
    auto span = [&] (juce::Rectangle<int> row, int first, int n)
    {
        return juce::Rectangle<int> (deck.getX() + first * (columnWidth + Space::unit), row.getY(),
                                     n * columnWidth + (n - 1) * Space::unit, row.getHeight());
    };
    auto keyRow = [&deck] { return deck.removeFromTop (Space::key); };

    auto keys = keyRow();
    int column = 0;
    for (auto* b : { &prev, &rew, &play, &pause, &stop, &ff, &next })
        b->setBounds (span (keys, column++, 1));
    deck.removeFromTop (Space::gap);

    auto modes = keyRow();
    shuffle.setBounds (span (modes, 0, 1));
    repeat.setBounds (span (modes, 1, 1));
    deck.removeFromTop (Space::gap);

    auto lvl = deck.removeFromTop (Space::bar);
    levelLabel.setBounds (span (lvl, 0, 1).withSizeKeepingCentre (columnWidth, Space::key));
    level.setBounds (lvl.withLeft (span (lvl, 1, 1).getX()));
    deck.removeFromTop (Space::pad);

    recorderLabel.setBounds (deck.removeFromTop (Space::title));
    deck.removeFromTop (Space::unit);
    auto recordRow = keyRow();
    recordSource.setBounds (span (recordRow, 0, 4));
    recordFormat.setBounds (span (recordRow, 4, 2));
    rec.setBounds (span (recordRow, 6, 1));
    deck.removeFromTop (Space::gap);
    auto recordInfo = deck.removeFromTop (36);
    recordScreen = span (recordInfo, 0, 5).toFloat();
    recordFolder.setBounds (span (recordInfo, 5, 1).withSizeKeepingCentre (columnWidth, Space::key));
    openFolder.setBounds (span (recordInfo, 6, 1).withSizeKeepingCentre (columnWidth, Space::key));
    deck.removeFromTop (Space::gap);
    auto replayRow = keyRow();
    replayLabel.setBounds (span (replayRow, 0, 1));
    replayArm.setBounds (span (replayRow, 1, 1));
    replayLength.setBounds (span (replayRow, 2, 3));
    replaySave.setBounds (span (replayRow, 5, 2));

    playlistLabel.setBounds (r.removeFromTop (Space::title));
    r.removeFromTop (Space::unit);
    list.setBounds (r);
}

void TapePanel::paint (juce::Graphics& g)
{
    g.fillAll (Console::Colours::background());

    auto drawScreen = [&] (const juce::String& text, juce::Rectangle<float> area)
    {
        if (g.clipRegionIntersects (area.getSmallestIntegerContainer()))
            DotMatrix::draw (g, text, area, this);
    };
    drawScreen (shownName, nameScreen);
    drawScreen (shownElapsed, elapsedScreen);
    drawScreen (shownRemaining, remainingScreen);
    drawScreen (shownRecord, recordScreen);
}

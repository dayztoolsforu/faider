#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/AudioEngine.h"
#include "ConsoleControls.h"

class MixerComponent;

// The TAPE tab: the tape deck's player (Tape.h) and recorder (Recorder.h). The deck on the left
// (track name and time on screens, a position bar to scrub, transport keys, shuffle, repeat,
// level, the recorder), the playlist on the right. Drop files or folders anywhere on the tab; drag
// tracks to reorder; double-click one to play it; Delete removes the selected ones. Hold REW or
// FF to wind. Everything plays through the TAPE input strip.
class TapePanel : public juce::Component,
                  public juce::FileDragAndDropTarget,
                  public juce::DragAndDropContainer,
                  public juce::DragAndDropTarget,
                  private juce::ListBoxModel,
                  private juce::Timer
{
public:
    TapePanel (AudioEngine&, MixerComponent&);
    ~TapePanel() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    // Dragging tracks within the playlist.
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;   // learning the replay hotkey
    void focusLost (FocusChangeType) override;

    // Instant replay: writes what's buffered to a WAV (SAVE, or its global hotkey).
    void saveReplay();
    void toggleRecording();   // REC (also from a MIDI key)
    juce::String replayHotkey;                                    // KeyPress text, or empty
    std::function<bool (const juce::String& key)> setReplayHotkey;   // registers it with the app's other keys

private:
    void timerCallback() override;
    int dropRowAt (juce::Point<int> panelPosition) const;   // the playlist row a dragged track would go to
    void finishWinding();
    void addFiles (const juce::Array<juce::File>&);
    void choose (bool folders);
    void removeSelected();
    void showRepeat();
    bool hasTapeStrip() const;

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void deleteKeyPressed (int) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;

    AudioEngine& engine;
    Tape& tape;
    MixerComponent& mixer;

    juce::TextButton prev { "PREV" }, rew { "REW" }, play { "PLAY" }, pause { "PAUSE" }, stop { "STOP" }, ff { "FF" }, next { "NEXT" };
    juce::TextButton shuffle { "SHUFFLE" }, repeat { "REPEAT ALL" };
    juce::TextButton addFilesButton { "ADD FILES" }, addFolderButton { "ADD FOLDER" }, eject { "EJECT" }, addStrip { "CREATE TAPE CHANNEL" };
    juce::Label levelLabel { {}, "LEVEL" }, playlistLabel { {}, "PLAYLIST" };
    juce::Slider position, level;
    juce::ListBox list { "Playlist", this };
    std::unique_ptr<juce::FileChooser> chooser;

    // The deck's screens, drawn in paint().
    juce::Rectangle<float> nameScreen, elapsedScreen, remainingScreen;
    juce::String shownName, shownElapsed, shownRemaining;
    int shownCurrent = -2, shownSize = -1;

    // Winding: holding REW or FF pauses the tape and runs the position along fast; letting go
    // puts the tape there (and plays on if it was playing). A quick click skips 10 seconds.
    float rewHeld = 0.0f, ffHeld = 0.0f;   // seconds each key has been held
    int winding = 0;                       // -1 rewinding, +1 fast-forwarding
    double windPosition = 0.0;
    bool playAfterWinding = false;

    int dropRow = -1;   // where a dragged track would land (a line drawn there), or -1

    // The recorder: what to record (any output, bus or input), REC, a screen with the time and
    // file size, and where the files go.
    struct RecordSource { Recorder::Kind kind; int index; juce::String name, key; };
    void fillRecordSources();
    juce::Label recorderLabel { {}, "RECORDER" };
    juce::ComboBox recordSource, recordFormat;
    std::vector<RecordSource> recordSources;   // the first: multitrack (key multitrackKey)
    static inline const juce::String multitrackKey { "multitrack" };
    juce::String recordSourcesShown;
    juce::TextButton rec { "REC" }, recordFolder { "PATH" }, openFolder { "REVEAL" };
    juce::Rectangle<float> recordScreen;
    juce::String shownRecord;
    juce::int64 recordBytes = 0;
    juce::String recordRoom;   // how long the drive has room for in the chosen format ("~41 H 20 M")
    int frame = 0;

    // Instant replay of the record source: ARM keeps the last 30 s to 5 min in memory; SAVE
    // (right-click: hotkey) writes it out.
    struct SaveKey : juce::TextButton
    {
        SaveKey() : TextButton ("SAVE") {}
        std::function<void (bool menu)> onPress;
        void clicked() override { if (onPress) onPress (false); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu())
                return TextButton::mouseDown (e);
            if (onPress)
                onPress (true);
        }
    };
    void keepReplayArmed();   // follows the record source and length (re-arms if they change)
    static constexpr double replayLengths[] = { 30.0, 60.0, 120.0, 300.0 };
    juce::Label replayLabel { {}, "REPLAY" };
    juce::TextButton replayArm { "ARM" };
    ConsoleControls::Stepper replayLength { { "30 S", "1 MIN", "2 MIN", "5 MIN" } };
    SaveKey replaySave;
    int replayLengthIndex = 1;
    bool learningReplayKey = false;
    double replaySavedAt = 0.0;   // when SAVE last wrote a file (it says SAVED for a moment)
};

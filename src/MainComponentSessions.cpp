// MainComponent's sessions: saving the mix and its history, the SESSION TIMELINE, saved sessions opened,
// exported and imported.
#include "MainComponent.h"
#include "ProductName.h"
#include "Session.h"

// Writes only when something changed, so this is cheap to call every second.
void MainComponent::saveSession()
{
    // Only a mix that was restored or deliberately made at start is written: anything else (no
    // master, or the master wouldn't open) would save an empty mix over the real one.
    if (interfaceName.isEmpty() || ! sessionLoaded)
        return;

    const auto xml = Session::save (engine, masterToSave())->toString();

    if (xml != lastSavedSession)
    {
        // Written whole to a file beside it and flushed to the disk before it takes the session's
        // place, so a power cut leaves the old session or the new one, never an empty file.
        const auto temp = sessionFile.getSiblingFile ("session.xml.tmp");
        {
            juce::FileOutputStream out (temp);
            if (! out.openedOk())
                return;
            out.setPosition (0);
            out.truncate();
            out.writeText (xml, false, false, nullptr);
            out.flush();   // (FlushFileBuffers on Windows)
            if (! out.getStatus().wasOk())
                return;
        }
        if (! temp.replaceFileIn (sessionFile))
            return;
        lastSavedSession = xml;
        keepHistory (xml);
    }
}

// Session safety: earlier versions in %APPDATA%\Faider\history. The first save of a run is
// kept too, so the mix as it was at start can always come back.
void MainComponent::keepHistory (const juce::String& xml)
{
    const auto now = juce::Time::getCurrentTime();
    if (lastHistory != juce::Time() && now - lastHistory < juce::RelativeTime::minutes (5))
        return;
    lastHistory = now;

    const auto dir = dataDir.getChildFile ("history");
    dir.createDirectory();
    dir.getChildFile ("session-" + now.formatted ("%Y-%m-%d_%H-%M-%S") + ".xml").replaceWithText (xml);

    const auto files = historyFiles();
    for (int i = 60; i < files.size(); ++i)
        files[i].deleteFile();
}

juce::Array<juce::File> MainComponent::historyFiles() const
{
    auto files = dataDir.getChildFile ("history").findChildFiles (juce::File::findFiles, false, "session-*.xml");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
    return files;
}

// Mixes set aside at start (the master missing or wouldn't open): beside session.xml, outside
// history/, so the history's pruning never takes them; listed after the history as "kept: ...".
juce::Array<juce::File> MainComponent::setAsideFiles() const
{
    auto files = dataDir.findChildFiles (juce::File::findFiles, false, "session-unloaded-*.xml");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
    return files;
}

void MainComponent::addSessionItems (juce::PopupMenu& menu)
{
    menu.addItem ("Session timeline...", [this] { showTimeline (juce::PopupMenu::Options().withMousePosition()); });
}

// SESSION TIMELINE: every kept copy of the session, newest first. Each brings back the whole mix
// (Faider restarts), or one channel just as it was then (its level, pan, mute and rack). From
// SETTINGS > GENERAL, FIND, or the tray menu (where the window may be hidden: at the mouse).
void MainComponent::showTimeline (juce::PopupMenu::Options where)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("SESSION TIMELINE");
    const auto now = juce::Time::getCurrentTime();
    auto files = historyFiles();
    const int historyCount = files.size();
    files.addArray (setAsideFiles());   // the set-aside mixes after the history, as "kept: ..."
    for (int i = 0; i < files.size(); ++i)
    {
        const auto& f = files.getReference (i);
        const auto when = f.getLastModificationTime();
        const auto ago = now - when;
        const auto label = juce::String (i >= historyCount ? "kept: " : "")
                         + when.formatted ("%H:%M  %a %d %b") + "   (" + (ago.inMinutes() < 60 ? juce::String ((int) ago.inMinutes()) + " min ago"
                                                                                                   : juce::String (ago.inHours(), 1) + " h ago") + ")";
        juce::PopupMenu version;
        version.addItem ("RESTORE EVERYTHING (RESTARTS)", [this, f] { restoreSession (f); });
        version.addSeparator();
        version.addSectionHeader ("PUT ONE CHANNEL BACK");
        if (const auto xml = juce::XmlDocument::parse (f))
        {
            for (auto* tag : { "Strip", "Pair" })
                for (auto* e : xml->getChildWithTagNameIterator (tag))
                {
                    const auto key = juce::String (juce::String (tag) == "Pair" ? "bus:" : "input:") + e->getStringAttribute ("name");
                    version.addItem (e->getStringAttribute ("name"), [this, key, saved = std::make_shared<juce::XmlElement> (*e), at = when.formatted ("%H:%M")]
                    {
                        if (mixer->revertChannel (key, *saved))
                            logEvent ("timeline: " + key.fromFirstOccurrenceOf (":", false, false) + " put back as it was at " + at);
                        else
                            logEvent ("timeline: couldn't put " + key.fromFirstOccurrenceOf (":", false, false) + " back (gone, or a plugin is locked)");
                    });
                }
        }
        menu.addSubMenu (label, version);
    }
    if (menu.getNumItems() <= 1)
        menu.addItem ("NOTHING KEPT YET", false, false, [] {});
    menu.showMenuAsync (where);
}

//==============================================================================
// Named sessions (%APPDATA%\Faider\sessions\<name>.xml): the mix saved under a name to come back
// to, and sessions as files to share or move (EXPORT, IMPORT). Opening one restarts Faider with it.
juce::File MainComponent::sessionsDir() const { return dataDir.getChildFile ("sessions"); }

void MainComponent::saveSessionAs()
{
    if (interfaceName.isEmpty() || ! sessionLoaded)
        return logEvent ("sessions: nothing to save yet (no mix loaded)");
    auto* box = new juce::AlertWindow ("SAVE SESSION AS", "A name for this mix:", juce::MessageBoxIconType::NoIcon);
    box->addTextEditor ("name", "");
    box->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    box->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    box->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this), box] (int result)
    {
        const auto name = juce::File::createLegalFileName (box->getTextEditorContents ("name").trim());
        if (result != 1 || safe == nullptr || name.isEmpty())
            return;
        safe->sessionsDir().createDirectory();
        const auto file = safe->sessionsDir().getChildFile (name + ".xml");
        if (file.replaceWithText (Session::save (safe->engine, safe->masterToSave())->toString()))
        {
            safe->logEvent ("sessions: saved as " + name);
            safe->toasts.show ("scene", "Session saved", name);
        }
    }), true);
}

void MainComponent::showSessions (juce::PopupMenu::Options where)
{
    juce::PopupMenu menu;
    menu.addItem ("SAVE AS...", [this] { saveSessionAs(); });
    menu.addSectionHeader (juce::String ("OPEN (") + Product::nameUpper + " RESTARTS)");
    auto files = sessionsDir().findChildFiles (juce::File::findFiles, false, "*.xml");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    for (auto& f : files)
        menu.addItem (f.getFileNameWithoutExtension(), [this, f] { restoreSession (f, f.getFileNameWithoutExtension()); });
    if (files.isEmpty())
        menu.addItem ("NO SAVED SESSIONS YET", false, false, [] {});
    menu.addSeparator();
    menu.addItem ("EXPORT TO A FILE...", [this] { exportSession(); });
    menu.addItem ("IMPORT FROM A FILE...", [this] { importSession(); });
    menu.showMenuAsync (where);
}

void MainComponent::exportSession()
{
    if (interfaceName.isEmpty() || ! sessionLoaded)
        return;
    sessionChooser = std::make_unique<juce::FileChooser> ("Export the session", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                                    .getChildFile ("Faider session.xml"), "*.xml");
    sessionChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult().withFileExtension ("xml");
        if (fc.getResult() != juce::File() && file.replaceWithText (Session::save (engine, masterToSave())->toString()))
            logEvent ("sessions: exported to " + file.getFullPathName());
    });
}

void MainComponent::importSession()
{
    sessionChooser = std::make_unique<juce::FileChooser> ("Import a session", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.xml");
    sessionChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        const auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || xml->getChildByName ("Strip") == nullptr && xml->getChildByName ("Bus") == nullptr && xml->getChildByName ("Pair") == nullptr)
        {
            if (file != juce::File())
                logEvent ("sessions: " + file.getFileName() + " isn't a Faider session");
            return;
        }
        restoreSession (file, file.getFileNameWithoutExtension());
    });
}

// Puts an earlier session back: it becomes session.xml and Faider restarts with it.
void MainComponent::restoreSession (const juce::File& version, const juce::String& named)
{
    const auto when = version.getLastModificationTime().formatted ("%a %d %b %H:%M");
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::NoIcon, named.isNotEmpty() ? "OPEN SESSION" : "RESTORE SESSION",
                                        (named.isNotEmpty() ? "Open the session \"" + named + "\"?" : "Restore the mix from " + when + "?") + "\nFaider restarts.",
                                        named.isNotEmpty() ? "OPEN" : "RESTORE", "CANCEL", nullptr,
                                        juce::ModalCallbackFunction::create ([this, version] (int result)
    {
        if (result != 1 || ! version.existsAsFile())
            return;

        interfaceName.clear();   // nothing saves over it on the way out
        version.copyFileTo (sessionFile);

        // Start again once this copy has gone (only one Faider runs at a time): the new copy
        // waits for this one's process to end (--after), however long closing takes.
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
        juce::ChildProcess relaunch;
        relaunch.start ("\"" + exe + "\" --after " + juce::String (thisProcessId()), 0);
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }));
}

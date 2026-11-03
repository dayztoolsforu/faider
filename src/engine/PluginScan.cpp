#include "PluginScan.h"
#ifndef FAIDER_DATA_FOLDER
 #define FAIDER_DATA_FOLDER "Faider"
#endif

namespace PluginScan
{
namespace
{
    juce::File storeFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (FAIDER_DATA_FOLDER).getChildFile ("plugins.xml");   // (each app its own)
    }

    // The plugin's file as it is now: its binary's time and size (a .vst3 is usually a bundle
    // folder, whose own time doesn't change when the plugin inside is updated).
    juce::String stampOf (const juce::String& path)
    {
        auto file = juce::File (path);
        if (file.isDirectory())
        {
            const auto inside = file.getChildFile ("Contents").getChildFile ("x86_64-win").findChildFiles (juce::File::findFiles, false, "*.vst3");
            if (! inside.isEmpty())
                file = inside.getFirst();
        }
        return juce::String (file.getLastModificationTime().toMilliseconds()) + ":" + juce::String (file.getSize());
    }

    struct Store
    {
        juce::CriticalSection lock;
        bool loaded = false;
        std::map<juce::String, std::pair<juce::String, Entry>> entries;   // path: its stamp when tested, what was found

        void load()
        {
            if (loaded)
                return;
            loaded = true;
            if (auto xml = juce::parseXML (storeFile()))
                for (auto* e : xml->getChildWithTagNameIterator ("Plugin"))
                    entries[e->getStringAttribute ("path")] = { e->getStringAttribute ("stamp"),
                                                                { e->getBoolAttribute ("passed"), e->getStringAttribute ("name"),
                                                                  e->getStringAttribute ("maker"), e->getStringAttribute ("reason") } };
        }

        void save() const
        {
            juce::XmlElement root ("Plugins");
            for (auto& [path, item] : entries)
            {
                auto* e = root.createNewChildElement ("Plugin");
                e->setAttribute ("path", path);
                e->setAttribute ("stamp", item.first);
                e->setAttribute ("passed", item.second.passed);
                e->setAttribute ("name", item.second.name);
                e->setAttribute ("maker", item.second.maker);
                if (item.second.reason.isNotEmpty())
                    e->setAttribute ("reason", item.second.reason);
            }
            storeFile().getParentDirectory().createDirectory();
            root.writeTo (storeFile());
        }
    };

    Store& store()
    {
        static Store s;
        return s;
    }

    void record (const juce::String& path, const juce::String& stamp, const Entry& entry)
    {
        auto& s = store();
        const juce::ScopedLock sl (s.lock);
        s.load();
        s.entries[path] = { stamp, entry };
        s.save();
    }
}

int runChild (const juce::String& pluginPath, const juce::File& out)
{
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> types;
    vst3.findAllTypesForFile (types, pluginPath);
    if (types.isEmpty())
        return 2;

    // The first plugin in the file: the one the desk loads from it (AudioEngine::createPlugin).
    juce::String error;
    auto plugin = vst3.createInstanceFromDescription (*types[0], 48000.0, 512, error);
    if (plugin == nullptr)
    {
        out.replaceWithText ("error\t" + (error.isNotEmpty() ? error : juce::String ("it wouldn't load")));
        return 3;
    }

    // As the desk would use it: stereo, prepared, a block of silence through it, then closed.
    auto layout = plugin->getBusesLayout();
    for (auto* busList : { &layout.inputBuses, &layout.outputBuses })
        for (int i = 0; i < busList->size(); ++i)
            busList->getReference (i) = (i == 0) ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    plugin->setBusesLayout (layout);
    plugin->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> block (juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), 512);
    block.clear();
    juce::MidiBuffer midi;
    plugin->processBlock (block, midi);
    plugin->releaseResources();

    // "ok" only once the instance is gone: a plugin that crashes as it's destroyed would crash
    // the desk when its insert is removed, so it fails here.
    const auto found = "ok\t" + types[0]->name + "\t" + types[0]->manufacturerName;
    plugin = nullptr;
    out.replaceWithText (found);
    return 0;
}

std::optional<Entry> find (const juce::String& path)
{
    auto& s = store();
    const juce::ScopedLock sl (s.lock);
    s.load();
    const auto it = s.entries.find (path);
    if (it == s.entries.end() || it->second.first != stampOf (path))
        return std::nullopt;
    return it->second.second;
}

juce::String test (const juce::String& path, int timeoutMs)
{
    const auto stamp = stampOf (path);
    const auto out = juce::File::createTempFile (".scan");
    const juce::StringArray args { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
                                   "--scan", path, "--out", out.getFullPathName() };
    juce::ChildProcess child;
    Entry entry;
    if (! child.start (args, 0))
        return "Couldn't start the test.";   // not the plugin's fault: nothing recorded

    const bool finished = child.waitForProcessToFinish (timeoutMs);
    if (! finished)
        child.kill();
    const auto result = juce::StringArray::fromTokens (out.loadFileAsString(), "\t", {});
    const auto code = finished ? child.getExitCode() : 0;
    // "ok" is only written once the plugin has loaded, run and been released, so a child that
    // hangs or crashes after that (on its way out) still means the plugin passed.
    if (result[0] == "ok")
    {
        entry.passed = true;
        entry.name = result[1];
        entry.maker = result[2];
        if (! finished || code != 0)
            entry.reason = "it passed, but the test copy didn't close cleanly";
    }
    else if (! finished)
        entry.reason = "it didn't finish loading in " + juce::String (timeoutMs / 1000) + " s";
    else
        entry.reason = result[0] == "error" ? result[1]
                     : code == 2        ? juce::String ("no VST3 plugin was found in it")
                                        : "it crashed while being tested (code " + juce::String::toHexString ((int) code) + ")";
    out.deleteFile();
    record (path, stamp, entry);
    return entry.passed ? juce::String() : entry.reason;
}

void forget (const juce::String& path)
{
    auto& s = store();
    const juce::ScopedLock sl (s.lock);
    s.load();
    s.entries.erase (path);
    s.save();
}
}

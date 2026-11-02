#pragma once

#include "../../src/engine/AudioEngine.h"
#include "../../src/ProductName.h"

// THE ORDER OF THE BUSES AND OUTPUTS ON THE DESK, as last arranged by dragging them: kept by name in
// desk-order.txt in the program's folder. Only what the desk shows moves: the engine's own order,
// and every route, scene and binding that counts on it, stay as they are. Buses or outputs not in
// the file (new ones) follow in the engine's order. Message thread.
class DeskOrder
{
public:
    // Brought in line with the engine (one added, removed or renamed) and the file (saved by the
    // other view). Cheap: call it often.
    void sync (const AudioEngine& engine)
    {
        if (const auto t = file().getLastModificationTime(); t != loadedAt)
        {
            loadedAt = t;
            load();
        }
        busAt = arrange (engine.getBuses(), busNames);
        outAt = arrange (engine.getMasters(), outNames);
    }

    // Engine index at a place on the desk, and the place of an engine index (both: as is, if unknown).
    int bus (int place) const    { return pick (busAt, place); }
    int output (int place) const { return pick (outAt, place); }
    int busPlace (int index) const    { return find (busAt, index); }
    int outputPlace (int index) const { return find (outAt, index); }

    // A bus or output moved from one place to another; saved.
    void moveBus (const AudioEngine& engine, int from, int to)    { move (busAt, from, to); busNames = namesOf (engine.getBuses(), busAt); save(); }
    void moveOutput (const AudioEngine& engine, int from, int to) { move (outAt, from, to); outNames = namesOf (engine.getMasters(), outAt); save(); }

    // A bus or output renamed (kind "bus" or "output"): it keeps its place (every view picks the
    // file up on its next sync).
    static void renamed (const juce::String& kind, const juce::String& oldName, const juce::String& newName)
    {
        if (kind != "bus" && kind != "output")
            return;
        juce::StringArray lines;
        lines.addLines (file().loadFileAsString());
        const int at = lines.indexOf (kind + ":" + oldName);
        if (at < 0)
            return;
        lines.set (at, kind + ":" + newName);
        file().replaceWithText (lines.joinIntoString ("\n"));
    }

private:
    static juce::File file() { return Product::dataFolder().getChildFile ("desk-order.txt"); }

    template <typename List>
    static std::vector<int> arrange (const List& list, const juce::StringArray& names)
    {
        std::vector<int> order;
        std::vector<bool> used (list.size(), false);
        for (auto& name : names)
            for (int i = 0; i < (int) list.size(); ++i)
                if (! used[(size_t) i] && list[(size_t) i]->name == name)
                {
                    order.push_back (i);
                    used[(size_t) i] = true;
                    break;
                }
        for (int i = 0; i < (int) list.size(); ++i)
            if (! used[(size_t) i])
                order.push_back (i);
        return order;
    }
    template <typename List>
    static juce::StringArray namesOf (const List& list, const std::vector<int>& order)
    {
        juce::StringArray names;
        for (int i : order)
            if (juce::isPositiveAndBelow (i, (int) list.size()))
                names.add (list[(size_t) i]->name);
        return names;
    }
    static int pick (const std::vector<int>& order, int place) { return juce::isPositiveAndBelow (place, (int) order.size()) ? order[(size_t) place] : place; }
    static int find (const std::vector<int>& order, int index)
    {
        const auto it = std::find (order.begin(), order.end(), index);
        return it != order.end() ? (int) (it - order.begin()) : index;
    }
    static void move (std::vector<int>& order, int from, int to)
    {
        if (! juce::isPositiveAndBelow (from, (int) order.size()) || ! juce::isPositiveAndBelow (to, (int) order.size()) || from == to)
            return;
        const int item = order[(size_t) from];
        order.erase (order.begin() + from);
        order.insert (order.begin() + to, item);
    }

    void load()
    {
        busNames.clear();
        outNames.clear();
        juce::StringArray lines;
        lines.addLines (file().loadFileAsString());
        for (auto& line : lines)
        {
            if (line.startsWith ("bus:"))    busNames.add (line.fromFirstOccurrenceOf (":", false, false));
            if (line.startsWith ("output:")) outNames.add (line.fromFirstOccurrenceOf (":", false, false));
        }
    }
    void save()
    {
        juce::StringArray lines;
        for (auto& n : busNames) lines.add ("bus:" + n);
        for (auto& n : outNames) lines.add ("output:" + n);
        file().replaceWithText (lines.joinIntoString ("\n"));
        loadedAt = file().getLastModificationTime();
    }

    juce::StringArray busNames, outNames;
    std::vector<int> busAt, outAt;
    juce::Time loadedAt;
};

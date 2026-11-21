// Checks the console's layouts (console/layout/*.json) and what their parts drive (console/src/
// ConsoleBinding.h), headless: no window, no audio device, nothing written to Faider's own folder
// (this target is built with FAIDER_DATA_FOLDER="FaiderDeskTest").
//   1. Every layout has mixer.json's sections, in its order, of its heights, COMPACT alike (or the
//      desk falls back on mixer.json's: ConsoleBench.h, reload).
//   2. Every part of the channel strip lies inside its section.
//   3. The channel strip's DYNAMICS holds the DE-ESS and AUTO-LEVEL keys, knobs and LEDs; the raw
//      strips (apps, tools, combinators) don't.
//   4. On a channel: every knob reads a value in its own units (it drives a real setting), and the
//      DE-ESS and AUTO-LEVEL parts drive the channel's de-esser and auto-level.
// Prints PASS/FAIL per check; exits non-zero on failure.
#include "../console/src/ConsoleBinding.h"

// The editor isn't part of this check.
juce::AudioProcessorEditor* VoiceChain::createEditor() { return nullptr; }

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    }

    juce::File layoutFolder() { return juce::File (FAIDER_LAYOUT_DIR); }

    // A section's parts, by name (none: an empty list).
    const juce::Array<juce::var>* items (const juce::var& layout, const juce::String& section)
    {
        if (auto* sections = layout["sections"].getArray())
            for (auto& s : *sections)
                if (s["name"].toString() == section)
                    return s["items"].getArray();
        return nullptr;
    }

    const juce::var* part (const juce::var& layout, const juce::String& section, const juce::String& kind, const juce::String& id)
    {
        if (auto* list = items (layout, section))
            for (auto& v : *list)
                if (v["kind"].toString() == kind && v["id"].toString() == id)
                    return &v;
        return nullptr;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;   // (timers and parameters expect a message manager)

    //==========================================================================
    // 1: the layouts agree.
    const auto mixer = juce::JSON::parse (layoutFolder().getChildFile ("mixer.json"));
    check (mixer.isObject() && mixer["sections"].isArray(), "mixer.json reads");
    if (! mixer.isObject() || ! mixer["sections"].isArray())
        return failures;
    const auto& reference = *mixer["sections"].getArray();
    const auto files = layoutFolder().findChildFiles (juce::File::findFiles, false, "*.json");
    check (files.size() == 6, "six layouts (" + juce::String (files.size()) + ")");
    for (auto& f : files)
    {
        const auto layout = juce::JSON::parse (f);
        auto* sections = layout["sections"].getArray();
        bool same = sections != nullptr && sections->size() == reference.size();
        for (int i = 0; same && i < reference.size(); ++i)
        {
            const auto& a = (*sections)[i];
            const auto& b = reference[i];
            same = a["name"].toString() == b["name"].toString() && (bool) a.getProperty ("compact", false) == (bool) b.getProperty ("compact", false)
                   && std::abs ((double) a["height"] - (double) b["height"]) <= 0.5;
        }
        check (same, f.getFileName() + ": mixer.json's sections, order, heights and COMPACT");
    }

    //==========================================================================
    // 2: the channel strip's parts inside their sections.
    {
        const float width = (float) (double) mixer.getProperty ("width", 214.0);
        juce::StringArray outside;
        for (auto& s : reference)
            for (auto& v : *s["items"].getArray())
            {
                if (v["kind"].toString() == "line")
                    continue;
                const bool knob = v["kind"].toString() == "knob";
                const float x = (float) (double) v["x"], y = (float) (double) v["y"];
                const float w = (float) (double) (knob ? v["size"] : v["w"]), h = (float) (double) (knob ? v["size"] : v["h"]);
                if (x < 0.0f || y < 0.0f || x + w > width + 0.5f || y + h > (float) (double) s["height"] + 0.5f)
                    outside.add (s["name"].toString() + " " + v["kind"].toString() + " " + v["id"].toString() + v["text"].toString());
            }
        check (outside.isEmpty(), "mixer.json: every part inside its section" + (outside.isEmpty() ? juce::String() : " (not: " + outside.joinIntoString (", ") + ")"));
    }

    //==========================================================================
    // 3: DE-ESS and AUTO-LEVEL on the channel strip, and only there.
    const std::pair<const char*, const char*> parts[] = { { "switch", "deessIn" }, { "knob", "deessThresh" }, { "knob", "deessFreq" }, { "leds", "deessGr" },
                                                          { "switch", "levelIn" }, { "knob", "levelTarget" }, { "knob", "levelRange" } };
    for (auto& [kind, id] : parts)
        check (part (mixer, "DYNAMICS", kind, id) != nullptr, juce::String ("mixer.json DYNAMICS: ") + kind + " " + id);
    for (auto& f : files)
        if (f.getFileName() != "mixer.json")
        {
            const auto layout = juce::JSON::parse (f);
            bool none = true;
            for (auto& s : *layout["sections"].getArray())
                for (auto& v : *s["items"].getArray())
                    none = none && ! v["id"].toString().startsWith ("deess") && ! v["id"].toString().startsWith ("levelIn")
                                && v["id"].toString() != "levelTarget" && v["id"].toString() != "levelRange";
            check (none, f.getFileName() + ": no DE-ESS or AUTO-LEVEL parts (a raw strip or an output)");
        }

    //==========================================================================
    // 4: on a channel, what they drive.
    {
        AudioEngine engine ([] (const juce::String&) {});
        engine.addStrip ("MIC", AudioEngine::Source {});   // (a test tone)
        Binding bind (&engine);
        auto* chain = engine.getStrips()[0]->channel.get();
        check (chain != nullptr, "a channel has its processing");
        if (chain == nullptr)
            return failures;

        juce::StringArray unbound;
        for (auto& s : reference)
            for (auto& v : *s["items"].getArray())
                if (v["kind"].toString() == "knob" && bind.valueText (0, v["id"].toString()).isEmpty())
                    unbound.add (v["id"].toString());
        check (unbound.isEmpty(), "every knob on the channel strip reads a value" + (unbound.isEmpty() ? juce::String() : " (not: " + unbound.joinIntoString (", ") + ")"));

        const std::pair<const char*, const char*> knobs[] = { { "deessThresh", "deessThreshold" }, { "deessFreq", "deessFreq" },
                                                              { "levelTarget", "levelTarget" }, { "levelRange", "levelRange" } };
        for (auto& [id, param] : knobs)
        {
            bind.set (0, id, 0.25f);
            const float moved = chain->params.getParameter (param)->getValue();
            bind.reset (0, id);
            check (std::abs (moved - 0.25f) < 0.01f && std::abs (bind.get (0, id, -1.0f) - chain->params.getParameter (param)->getDefaultValue()) < 1.0e-4f,
                   juce::String (id) + " turns the channel's " + param + ", and double-click puts it back");
        }
        const auto thresh = bind.valueText (0, "deessThresh"), freq = bind.valueText (0, "deessFreq");
        const auto target = bind.valueText (0, "levelTarget"), range = bind.valueText (0, "levelRange");
        check (thresh == "-30.0 dB" && freq == "6.50 kHz" && target == "-18.0 dB" && range == juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + "12.0 dB",
               "their values in dB and kHz (" + thresh + ", " + freq + ", " + target + ", " + range + ")");

        const std::pair<const char*, const char*> keys[] = { { "deessIn", "deessOn" }, { "levelIn", "levelOn" } };
        for (auto& [id, flag] : keys)
        {
            auto on = [chain, flag = flag] { return chain->params.getRawParameterValue (flag)->load() > 0.5f; };
            const bool before = bind.isOn (0, id, true) || on();
            bind.toggle (0, id, false);
            const bool in = bind.isOn (0, id, false) && on();
            bind.toggle (0, id, false);
            check (! before && in && ! bind.isOn (0, id, true) && ! on(), juce::String (id) + " switches the channel's " + flag + " in and out (out to start)");
        }

        chain->deessReduction = -12.0f;
        const int lit = bind.reductionLit (0, "deessGr", -1);
        chain->deessReduction = 0.0f;
        check (lit == 3 && bind.reductionLit (0, "deessGr", -1) == 0, "deessGr: the de-esser's reduction (12 dB: 3, 6 and 10 lit)");
    }

    std::cout << (failures == 0 ? "All desk checks passed.\n" : "Some desk checks FAILED.\n");
    return failures;
}

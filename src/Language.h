#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>   // (PropertiesFile: settings.xml)
#include <vector>
#include "FaiderAssets.h"

// THE INTERFACE'S LANGUAGE (SETTINGS > General > Language): English, the text in the code, or one
// of the translations in assets/i18n (built into the program: FaiderAssets). JUCE's TRANS looks
// each English text up in the table set here, so it's set at start, before any window is made, and
// a change takes effect at the next start. The desk's keys, the event log and the elevated helper
// stay English (CONTRIBUTING.md: Translations).
namespace Language
{
    struct Entry
    {
        const char* code;   // settings.xml's "language", and assets/i18n/<code>.txt
        const char* name;   // in its own language, as the menu shows it (UTF-8)
    };

    inline const std::vector<Entry>& all()
    {
        static const std::vector<Entry> list {
            { "en", "English" },
            { "de", "Deutsch" },
            { "es", "Espa\xc3\xb1ol" },
            { "fr", "Fran\xc3\xa7" "ais" },
            { "it", "Italiano" },
            { "ja", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e" },
            { "ko", "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4" },
            { "nl", "Nederlands" },
            { "pl", "Polski" },
            { "pt-BR", "Portugu\xc3\xaas (Brasil)" },
            { "sv", "Svenska" },
            { "tr", "T\xc3\xbcrk\xc3\xa7" "e" },
            { "zh-Hans", "\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87" },
        };
        return list;
    }

    inline bool isKnown (const juce::String& code)
    {
        for (auto& e : all())
            if (code == e.code)
                return true;
        return false;
    }

    // Its name in its own language ("Deutsch"), or English for a code that isn't one.
    inline juce::String nameOf (const juce::String& code)
    {
        for (auto& e : all())
            if (code == e.code)
                return juce::String (juce::CharPointer_UTF8 (e.name));
        return "English";
    }

    // The built-in table for `code` (assets/i18n/<code>.txt), or empty (English, or none built in).
    inline juce::String table (const juce::String& code)
    {
        const auto file = code + ".txt";
        for (int i = 0; i < FaiderAssets::namedResourceListSize; ++i)
            if (file == FaiderAssets::originalFilenames[i])
            {
                int size = 0;
                if (const auto* data = FaiderAssets::getNamedResource (FaiderAssets::namedResourceList[i], size))
                    return juce::String::fromUTF8 (data, size);
            }
        return {};
    }

    // Windows' display language as one of ours: "de-DE" is "de", "pt-PT" too is "pt-BR" (the one
    // Portuguese there is), "zh-CN" and "zh-SG" are "zh-Hans" (Traditional Chinese isn't: English).
    // Empty if there's no translation for it.
    inline juce::String forDisplayLanguage (const juce::String& windows)
    {
        const auto tag = windows.toLowerCase();
        const auto first = tag.upToFirstOccurrenceOf ("-", false, false);
        if (first == "zh")
            return tag.contains ("hans") || tag.endsWith ("-cn") || tag.endsWith ("-sg") ? "zh-Hans" : "";
        if (first == "pt")
            return "pt-BR";
        return first != "en" && isKnown (first) ? first : juce::String();
    }

    // TRANS's table for `code` set (English, or a code with no table: none, so TRANS gives the
    // English back). True if it's the language asked for.
    inline bool use (const juce::String& code)
    {
        const auto text = code == "en" ? juce::String() : table (code);
        juce::LocalisedStrings::setCurrentMappings (text.isNotEmpty() ? new juce::LocalisedStrings (text, false) : nullptr);
        return code == "en" || text.isNotEmpty();
    }

    // At start, before any window: settings.xml's "language". The first start has none: Windows'
    // display language if there's a translation for it, else English, kept there so later starts
    // keep it (whatever Windows' becomes).
    inline juce::String start (juce::PropertiesFile& settings)
    {
        auto code = settings.getValue ("language");
        if (! isKnown (code))
        {
            code = forDisplayLanguage (juce::SystemStats::getDisplayLanguage());
            if (code.isEmpty())
                code = "en";
            settings.setValue ("language", code);
            settings.saveIfNeeded();
        }
        if (! use (code))
            use ("en");
        return code;
    }
}

// Checks the interface's translations (src/Language.h, assets/i18n) as the program loads them: each
// built-in table is there, reads as JUCE reads it, and translates known texts with their placeholders
// kept; English sets no table; Windows' display language picks the right one. (Every key against the
// code: tools/translations.py.) Prints PASS/FAIL per check; exits non-zero on failure.
#include "../src/Language.h"
#include <iostream>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    }

    // The %1, %2... in a text, in order.
    juce::StringArray placeholders (const juce::String& text)
    {
        juce::StringArray found;
        for (int i = 0; i + 1 < text.length(); ++i)
            if (text[i] == '%' && juce::CharacterFunctions::isDigit (text[i + 1]))
                found.add (text.substring (i, i + 2));
        found.sort (false);
        return found;
    }
}

int main()
{
    // Texts every table has, and none says as the English does (the LANGUAGE setting's own: they
    // stay while the code does). (Not LATER: Dutch says LATER too.)
    const char* known[] = { "LANGUAGE", "RESTART NOW" };
    const juce::String withValue ("The new language takes effect when %1 starts again.");

    check (Language::all().size() == 13 && juce::String (Language::all()[0].code) == "en", "English and 12 translations are offered, English first");
    for (auto& e : Language::all())
    {
        const juce::String code (e.code);
        if (code == "en")
            continue;
        const auto text = Language::table (code);
        check (text.isNotEmpty(), code + ": its table is built in");
        const juce::LocalisedStrings table (text, false);
        check (table.getLanguageName() == Language::nameOf (code), code + ": its language line is its own name (" + Language::nameOf (code) + ")");
        check (table.getMappings().size() > 100, code + ": " + juce::String (table.getMappings().size()) + " texts");

        bool allTranslated = true, placesKept = true;
        for (auto& key : table.getMappings().getAllKeys())
        {
            const auto value = table.getMappings()[key];
            allTranslated = allTranslated && value.trim().isNotEmpty();
            placesKept = placesKept && placeholders (key) == placeholders (value);
        }
        check (allTranslated, code + ": every text has a translation");
        check (placesKept, code + ": every translation keeps its placeholders");

        for (auto* k : known)
        {
            const auto t = table.translate (k, {});
            check (t.isNotEmpty() && t != k, code + ": \"" + k + "\" is translated");
        }
        const auto t = table.translate (withValue, {});
        check (t.isNotEmpty() && t != withValue && t.contains ("%1") && t.replace ("%1", "Faider").contains ("Faider"),
               code + ": a text with a value is translated and keeps %1");

        check (Language::use (code) && TRANS ("RESTART NOW") == table.translate ("RESTART NOW"), code + ": once in use, TRANS gives its text");
        check (TRANS ("not a key in any table") == "not a key in any table", code + ": a text it doesn't have stays English");
    }

    check (Language::use ("en") && TRANS ("RESTART NOW") == "RESTART NOW", "English: TRANS gives the code's text");
    check (! Language::use ("xx") && TRANS ("RESTART NOW") == "RESTART NOW", "a language with no table: English, and it says so");

    check (Language::forDisplayLanguage ("de-DE") == "de" && Language::forDisplayLanguage ("fr-CA") == "fr" && Language::forDisplayLanguage ("sv-SE") == "sv",
           "Windows' display language: de-DE is de, fr-CA fr, sv-SE sv");
    check (Language::forDisplayLanguage ("pt-BR") == "pt-BR" && Language::forDisplayLanguage ("pt-PT") == "pt-BR", "pt-BR and pt-PT are pt-BR");
    check (Language::forDisplayLanguage ("zh-CN") == "zh-Hans" && Language::forDisplayLanguage ("zh-Hans-SG") == "zh-Hans" && Language::forDisplayLanguage ("zh-TW").isEmpty(),
           "zh-CN is zh-Hans; zh-TW (Traditional) has none");
    check (Language::forDisplayLanguage ("en-US").isEmpty() && Language::forDisplayLanguage ("ru-RU").isEmpty() && Language::forDisplayLanguage ("").isEmpty(),
           "English, a language with no translation, or none: empty (English)");

    return failures;
}

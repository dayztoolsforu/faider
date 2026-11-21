// Checks the interface's translations (src/Language.h, assets/i18n) as the program loads them: each
// built-in table is there, reads as JUCE reads it, and translates known texts with their placeholders
// kept; English sets no table; Windows' display language picks the right one. (Every key against the
// code: tools/translations.py.) Also the language a start picks, and capitals as each language
// writes them (Language::upper / lower). Prints PASS/FAIL per check; exits non-zero on failure.
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

    // The language to start in: Windows' only on the very first start; an existing user without
    // the setting (from before languages) stays English.
    check (Language::startCode ({}, true, "de-DE") == "de" && Language::startCode ({}, true, "ru-RU") == "en",
           "first start: Windows' display language, English if there's no translation for it");
    check (Language::startCode ({}, false, "de-DE") == "en", "settings from before languages, no language kept: English, not Windows'");
    check (Language::startCode ("fr", false, "de-DE") == "fr" && Language::startCode ("fr", true, "de-DE") == "fr", "a language kept: that one");
    check (Language::startCode ("xx", false, "de-DE") == "en", "a language kept that isn't one: English");

    // Capitals and small letters as the language writes them (Language::upper / lower).
    auto u = [] (const char* utf8) { return juce::String (juce::CharPointer_UTF8 (utf8)); };
    Language::use ("tr");
    check (Language::upper ("mikser") == u ("M\xc4\xb0KSER"), "Turkish: mikser in capitals has a dotted capital I");
    check (Language::lower ("IRMAK") == u ("\xc4\xb1rmak"), "Turkish: IRMAK in small letters has a dotless i");
    Language::use ("de");
    check (Language::upper (u ("ger\xc3\xa4te")) == u ("GER\xc3\x84TE"), "German: geraete (a umlaut) in capitals has a capital A umlaut");
    Language::use ("fr");
    check (Language::upper (u ("r\xc3\xa9glages")) == u ("R\xc3\x89GLAGES"), "French: reglages (e acute) in capitals has a capital E acute");
    check (Language::lower (u ("R\xc3\x89GLAGES")) == u ("r\xc3\xa9glages"), "French: and back in small letters");
    Language::use ("pl");
    check (Language::upper (u ("narz\xc4\x99" "dzia")) == u ("NARZ\xc4\x98" "DZIA"), "Polish: narzedzia (e ogonek) in capitals has a capital E ogonek");
    Language::use ("en");
    check (Language::upper ("mixer") == "MIXER" && Language::upper (u ("caf\xc3\xa9")) == u ("CAF\xc3\x89"), "English: plain capitals (an accented letter too)");
    std::cout << "(JUCE's own toUpperCase of German geraete, a umlaut: " << u ("ger\xc3\xa4te").toUpperCase().toStdString() << ")\n";

    return failures;
}

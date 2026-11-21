#include "LanguageCase.h"
#include <windows.h>
#include <vector>

namespace
{
    juce::String& chosen()   // (message thread: set at start, before any window)
    {
        static juce::String code ("en");
        return code;
    }

    // The language's Windows locale (Language::all's codes); English or one not known: invariant ("").
    const wchar_t* localeFor (const juce::String& code)
    {
        static const std::pair<const char*, const wchar_t*> locales[] = {
            { "de", L"de-DE" }, { "es", L"es-ES" }, { "fr", L"fr-FR" }, { "it", L"it-IT" }, { "ja", L"ja-JP" }, { "ko", L"ko-KR" },
            { "nl", L"nl-NL" }, { "pl", L"pl-PL" }, { "pt-BR", L"pt-BR" }, { "sv", L"sv-SE" }, { "tr", L"tr-TR" }, { "zh-Hans", L"zh-CN" } };
        for (auto& [c, locale] : locales)
            if (code == c)
                return locale;
        return LOCALE_NAME_INVARIANT;
    }

    bool plainAscii (const juce::String& text)
    {
        for (auto t = text.getCharPointer(); ! t.isEmpty();)
            if (t.getAndAdvance() >= 128)
                return false;
        return true;
    }

    juce::String mapped (const juce::String& text, DWORD flags)
    {
        const auto* locale = localeFor (chosen());
        const wchar_t* source = text.toWideCharPointer();
        const int size = LCMapStringEx (locale, flags | LCMAP_LINGUISTIC_CASING, source, -1, nullptr, 0, nullptr, nullptr, 0);
        if (size <= 0)
            return {};
        std::vector<wchar_t> out ((size_t) size, L'\0');
        if (LCMapStringEx (locale, flags | LCMAP_LINGUISTIC_CASING, source, -1, out.data(), size, nullptr, nullptr, 0) <= 0)
            return {};
        return juce::String (out.data());
    }
}

namespace Language
{
    void setCaseLanguage (const juce::String& code) { chosen() = code; }
    juce::String caseLanguage() { return chosen(); }

    juce::String upper (const juce::String& text)
    {
        if (text.isEmpty() || (chosen() != "tr" && plainAscii (text)))
            return text.toUpperCase();
        const auto s = mapped (text, LCMAP_UPPERCASE);
        return s.isNotEmpty() ? s : text.toUpperCase();   // (Windows refused it: JUCE's, as before)
    }

    juce::String lower (const juce::String& text)
    {
        if (text.isEmpty() || (chosen() != "tr" && plainAscii (text)))
            return text.toLowerCase();
        const auto s = mapped (text, LCMAP_LOWERCASE);
        return s.isNotEmpty() ? s : text.toLowerCase();
    }
}

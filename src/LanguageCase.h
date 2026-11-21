#pragma once

#include <juce_core/juce_core.h>

// A translated text in capitals, or in small letters, as its language writes them: Turkish i is
// İ, German ä is Ä, French é is É. (JUCE's toUpperCase uses the C library's, which knows neither.)
// Windows' own mapping for the interface's language (LCMapStringEx, its linguistic casing), set
// with the language at start (Language::use); English, or before any is set: the invariant one.
// A text all in plain ASCII, in a language other than Turkish, takes JUCE's (the same, and quicker).
// For what's shown only: a search compares with JUCE's own, so English names still match in Turkish.
namespace Language
{
    void setCaseLanguage (const juce::String& code);   // (Language::use)
    juce::String caseLanguage();                         // the code set, "en" before any
    juce::String upper (const juce::String& text);
    juce::String lower (const juce::String& text);
}

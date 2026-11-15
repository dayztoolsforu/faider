#pragma once

#include <juce_core/juce_core.h>
#include <initializer_list>

// Messages the engine makes in English with values in them ("Can't read %1 ..."): kept English
// where they're logged or saved, and put into the interface's language where they're shown.
// `text` is matched against each English template (marked NEEDS_TRANS where it's defined), and
// that template's translation gets the same values back. A text no template matches is
// translated whole (juce::translate: unchanged if it isn't a key).
namespace EngineText
{
    inline juce::String translate (const juce::String& text, std::initializer_list<const char*> templates)
    {
        for (auto* t : templates)
        {
            const juce::String pattern (t);

            // The template's fixed parts, and its placeholders ("%1", "%2") in the order they come.
            juce::StringArray fixed, names;
            int cursor = 0;
            for (;;)
            {
                const int a = pattern.indexOf (cursor, "%1"), b = pattern.indexOf (cursor, "%2");
                const int next = a < 0 ? b : b < 0 ? a : juce::jmin (a, b);
                if (next < 0)
                    break;
                fixed.add (pattern.substring (cursor, next));
                names.add (pattern.substring (next, next + 2));
                cursor = next + 2;
            }
            if (names.isEmpty())
            {
                if (text == pattern)
                    return juce::translate (pattern);
                continue;
            }
            fixed.add (pattern.substring (cursor));   // (one more fixed part than placeholders)

            const auto& last = fixed[fixed.size() - 1];
            if (! text.startsWith (fixed[0]) || ! text.endsWith (last))
                continue;
            const int end = text.length() - last.length();
            int pos = fixed[0].length();
            bool ok = pos <= end;
            juce::StringArray values;
            for (int i = 1; ok && i < fixed.size(); ++i)
            {
                const bool isLast = i == fixed.size() - 1;
                const int found = isLast ? end : text.indexOf (pos, fixed[i]);
                ok = found >= pos && (isLast || (fixed[i].isNotEmpty() && found + fixed[i].length() <= end));
                if (ok)
                {
                    values.add (text.substring (pos, found));
                    pos = found + fixed[i].length();
                }
            }
            if (! ok)
                continue;

            // The values back in, in one pass (so a value holding "%2" stays as it is).
            const auto translated = juce::translate (pattern);
            juce::String result;
            for (int i = 0; i < translated.length(); ++i)
            {
                if (translated[i] == '%' && i + 1 < translated.length())
                    if (const int n = names.indexOf (translated.substring (i, i + 2)); n >= 0)
                    {
                        result << values[n];
                        ++i;
                        continue;
                    }
                result << juce::String::charToString (translated[i]);
            }
            return result;
        }
        return juce::translate (text);
    }
}

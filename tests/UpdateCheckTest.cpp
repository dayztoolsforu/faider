// Checks CHECK FOR UPDATES' reading of GitHub's answer (src/UpdateCheck.h) without the network:
// which tags are newer, and what an answer gives. Prints PASS/FAIL per check; exits non-zero on failure.
#include "../src/UpdateCheck.h"
#include <iostream>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    }
}

int main()
{
    using UpdateCheck::isNewer;
    check (isNewer ("v0.1.6", "0.1.5"), "v0.1.6 is newer than 0.1.5");
    check (isNewer ("0.2.0", "0.1.15"), "0.2.0 is newer than 0.1.15");
    check (isNewer ("v0.1.10", "0.1.9"), "numbers, not letters: 0.1.10 is newer than 0.1.9");
    check (isNewer ("1.0", "0.9.9"), "1.0 is newer than 0.9.9");
    check (isNewer ("0.1.5.1", "0.1.5"), "0.1.5.1 is newer than 0.1.5");
    check (! isNewer ("v0.1.5", "0.1.5"), "the same version isn't newer");
    check (! isNewer ("0.1.5.0", "0.1.5"), "0.1.5.0 is the same as 0.1.5");
    check (! isNewer ("v0.1.4", "0.1.5"), "an older one isn't newer");
    check (! isNewer ("0.1.5-beta", "0.1.5"), "0.1.5-beta isn't newer than 0.1.5");
    check (! isNewer ("", "0.1.5"), "no tag isn't newer");

    const auto good = UpdateCheck::read (200, R"({"tag_name":"v0.1.6","html_url":"https://github.com/dayztoolsforu/faider/releases/tag/v0.1.6","draft":false})");
    check (good.ok && good.version == "0.1.6" && good.url == "https://github.com/dayztoolsforu/faider/releases/tag/v0.1.6", "an answer gives the version and its page");
    check (! UpdateCheck::read (404, "{}").ok, "no release (404) isn't one");
    check (! UpdateCheck::read (200, "not json").ok, "a garbled answer isn't one");
    check (! UpdateCheck::read (200, R"({"tag_name":"v9","html_url":"https://example.com/x"})").ok, "a page that isn't on github.com is never offered");

    return failures;
}

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
    check (isNewer ("faider-0.1.6", "0.1.5") && isNewer ("release-0.2.0", "0.1.9") && isNewer ("V1.0", "0.9"), "a tag's prefix is skipped: faider-0.1.6, release-0.2.0, V1.0");
    check (! isNewer ("faider-0.1.5", "0.1.5") && ! isNewer ("release-0.1.4", "0.1.5"), "with a prefix, the same or an older one isn't newer");
    check (isNewer ("faider-v0.1.6-rc1", "0.1.5") && ! isNewer ("faider-0.1.5-beta", "0.1.5"), "a prefix and a suffix together");
    check (! isNewer ("latest", "0.1.5") && ! isNewer ("v", "0.1.5"), "a tag with no number isn't newer");
    check (UpdateCheck::versionOf ("faider-0.1.6") == "0.1.6" && UpdateCheck::versionOf (" v0.1.6 ") == "0.1.6" && UpdateCheck::versionOf ("nightly").isEmpty(),
           "a tag's version starts at its first digit");

    // When the daily check is due (times in ms).
    using UpdateCheck::isDue;
    const juce::int64 hour = 60LL * 60 * 1000, day = 24 * hour, now = 1000 * day;
    check (isDue (now, 0, 0), "never answered: due");
    check (! isDue (now, now - hour, 0) && ! isDue (now, now - day + 1, 0), "answered less than a day ago: not due");
    check (isDue (now, now - day, 0) && isDue (now, now - 3 * day, 0), "a day or more since the answer: due (Faider left running too)");
    check (isDue (now, now + hour, 0), "the PC's clock set back past the last answer: due");
    check (! isDue (now, now - 3 * day, now - 60 * 1000) && ! isDue (now, 0, now - hour + 1), "tried less than an hour ago (no answer came): not yet");
    check (isDue (now, now - 3 * day, now - hour) && isDue (now, 0, now - 5 * hour), "an hour after a try that failed: again, the same day");
    check (! isDue (now, now - 2 * hour, now - 2 * hour), "answered two hours ago: not due, though an hour has passed since the try");

    const auto good = UpdateCheck::read (200, R"({"tag_name":"v0.1.6","html_url":"https://github.com/dayztoolsforu/faider/releases/tag/v0.1.6","draft":false})");
    check (good.ok && good.version == "0.1.6" && good.url == "https://github.com/dayztoolsforu/faider/releases/tag/v0.1.6", "an answer gives the version and its page");
    check (UpdateCheck::read (200, R"({"tag_name":"faider-0.1.7","html_url":"https://github.com/dayztoolsforu/faider/releases/tag/faider-0.1.7"})").version == "0.1.7",
           "a tag with a prefix gives its version");
    check (! UpdateCheck::read (200, R"({"tag_name":"nightly","html_url":"https://github.com/dayztoolsforu/faider/releases/tag/nightly"})").ok, "a tag with no number isn't a release to offer");
    check (! UpdateCheck::read (404, "{}").ok, "no release (404) isn't one");
    check (! UpdateCheck::read (200, "not json").ok, "a garbled answer isn't one");
    check (! UpdateCheck::read (200, R"({"tag_name":"v9","html_url":"https://example.com/x"})").ok, "a page that isn't on github.com is never offered");

    return failures;
}

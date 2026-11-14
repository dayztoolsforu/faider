#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

// CHECK FOR UPDATES: one request to GitHub for the newest release, with the app's name and version
// as its User-Agent and nothing else of ours. Nothing is downloaded or installed: a newer version
// is only said, with its release page to open. Drafts and pre-releases never count (/latest skips them).
namespace UpdateCheck
{
    inline constexpr const char* address = "https://api.github.com/repos/dayztoolsforu/faider/releases/latest";

    // A tag's version: from its first digit on ("v0.1.6", "faider-0.1.6", "release-0.1.6": "0.1.6"; no digit: empty).
    inline juce::String versionOf (const juce::String& tag)
    {
        const auto v = tag.trim();
        const int first = v.indexOfAnyOf ("0123456789");
        return first < 0 ? juce::String() : v.substring (first);
    }

    // "v0.1.6" newer than "0.1.5", number by number (a missing number is 0; anything after a '-' or '+' ignored).
    inline bool isNewer (const juce::String& tag, const juce::String& current)
    {
        auto numbers = [] (juce::String v)
        {
            v = versionOf (v).upToFirstOccurrenceOf ("-", false, false).upToFirstOccurrenceOf ("+", false, false);
            juce::Array<int> n;
            for (auto& part : juce::StringArray::fromTokens (v, ".", {}))
                n.add (part.getIntValue());
            return n;
        };
        const auto a = numbers (tag), b = numbers (current);
        for (int i = 0; i < juce::jmax (a.size(), b.size()); ++i)
            if (a[i] != b[i])   // (juce::Array gives 0 past its end)
                return a[i] > b[i];
        return false;
    }

    // Whether the daily check is due. `lastAnswered`: when GitHub last answered (kept in the
    // settings; 0: never); `lastTried`: the last try of this run (0: none). A day since the answer
    // (or the PC's clock set back), and an hour since a try: one that failed is tried again the
    // same day, not at once. Times in milliseconds.
    inline bool isDue (juce::int64 now, juce::int64 lastAnswered, juce::int64 lastTried)
    {
        constexpr juce::int64 hour = 60LL * 60 * 1000, day = 24 * hour;
        if (lastTried != 0 && now >= lastTried && now - lastTried < hour)
            return false;
        return now - lastAnswered >= day || now < lastAnswered;
    }

    struct Result
    {
        bool ok = false;            // GitHub answered with a release
        juce::String version, url;  // its version ("0.1.6") and release page
        juce::String error;         // why not, when not ok
    };

    // GitHub's answer read: the tag and the page (only a github.com page is ever opened).
    inline Result read (int status, const juce::String& body)
    {
        Result r;
        if (status != 200)
        {
            r.error = "GitHub answered " + juce::String (status);
            return r;
        }
        const auto json = juce::JSON::parse (body);
        r.version = versionOf (json["tag_name"].toString());
        r.url = json["html_url"].toString().trim();
        if (r.version.isEmpty() || ! r.url.startsWith ("https://github.com/"))
        {
            r.error = "no release in GitHub's answer";
            return r;
        }
        r.ok = true;
        return r;
    }

    // The request, on a thread of its own (10 s at most to connect); `done` hears the result on the
    // message thread. Gone while it waits (Faider quitting): the request is cancelled, nothing heard.
    class Checker : private juce::Thread
    {
    public:
        Checker() : juce::Thread ("Update check") {}
        ~Checker() override { cancel(); }

        bool isChecking() const { return isThreadRunning(); }

        void start (const juce::String& appName, const juce::String& version, std::function<void (Result)> done)
        {
            if (isThreadRunning())
                return;
            agent = appName + "/" + version;
            onDone = std::move (done);
            startThread();
        }

    private:
        void cancel()
        {
            signalThreadShouldExit();
            {
                const juce::ScopedLock sl (lock);
                if (stream != nullptr)
                    stream->cancel();
            }
            stopThread (4000);
        }

        void run() override
        {
            juce::WebInputStream web (juce::URL (address), false);
            web.withExtraHeaders ("User-Agent: " + agent).withConnectionTimeout (10000).withNumRedirectsToFollow (2);
            {
                const juce::ScopedLock sl (lock);
                if (threadShouldExit())
                    return;
                stream = &web;
            }
            Result result;
            if (! web.connect (nullptr))
                result.error = "couldn't reach GitHub";
            else
                result = read (web.getStatusCode(), web.readEntireStreamAsString());
            {
                const juce::ScopedLock sl (lock);
                stream = nullptr;
            }
            if (! threadShouldExit())
                juce::MessageManager::callAsync ([done = onDone, result] { if (done) done (result); });
        }

        juce::String agent;
        std::function<void (Result)> onDone;
        juce::CriticalSection lock;
        juce::WebInputStream* stream = nullptr;   // while it's open (cancel() reaches it)
    };
}

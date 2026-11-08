#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

// CHECK FOR UPDATES: one request to GitHub for the newest release, with the app's name and version
// as its User-Agent and nothing else of ours. Nothing is downloaded or installed: a newer version
// is only said, with its release page to open. Drafts and pre-releases never count (/latest skips them).
namespace UpdateCheck
{
    inline constexpr const char* address = "https://api.github.com/repos/dayztoolsforu/faider/releases/latest";

    // "v0.1.6" newer than "0.1.5", number by number (a missing number is 0; anything after a '-' or '+' ignored).
    inline bool isNewer (const juce::String& tag, const juce::String& current)
    {
        auto numbers = [] (juce::String v)
        {
            v = v.trim().trimCharactersAtStart ("vV").upToFirstOccurrenceOf ("-", false, false).upToFirstOccurrenceOf ("+", false, false);
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
        r.version = json["tag_name"].toString().trim().trimCharactersAtStart ("vV");
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

#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>

// Work on threads of its own that never outlives its owner (a member of it): every job is
// counted, and the owner going (or finish()) sets `leaving`, which the jobs look at to stop
// early, and waits until the last one has returned.
class BackgroundJobs
{
public:
    ~BackgroundJobs() { finish(); }

    std::atomic<bool> leaving { false };   // set once: jobs return as soon as they can

    // Any thread (a job may launch another).
    void launch (std::function<void()> work)
    {
        {
            const std::lock_guard<std::mutex> sl (lock);
            ++running;
        }
        const bool started = juce::Thread::launch ([this, work = std::move (work)]() mutable
        {
            work();
            work = nullptr;   // (what it held goes before the owner may)
            done();
        });
        if (! started)
            done();   // (no thread to be had: nothing ran)
    }

    void finish()
    {
        leaving = true;
        std::unique_lock<std::mutex> ul (lock);
        changed.wait (ul, [this] { return running == 0; });
    }

private:
    void done()
    {
        const std::lock_guard<std::mutex> sl (lock);
        --running;
        changed.notify_all();
    }

    std::mutex lock;
    std::condition_variable changed;
    int running = 0;
};

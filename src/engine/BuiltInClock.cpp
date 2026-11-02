#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <avrt.h>
#pragma comment (lib, "avrt.lib")
#pragma comment (lib, "winmm.lib")

#include "BuiltInClock.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
 #define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

BuiltInClock::BuiltInClock()
    : juce::AudioIODevice (deviceName, "Built-in"),
      juce::Thread ("Built-in clock")
{
}

BuiltInClock::~BuiltInClock()
{
    close();
}

juce::String BuiltInClock::open (const juce::BigInteger&, const juce::BigInteger&, double sampleRate, int bufferSizeSamples)
{
    close();
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    block = bufferSizeSamples > 0 ? bufferSizeSamples : getDefaultBufferSize();
    opened = true;
    return {};
}

void BuiltInClock::close()
{
    stop();
    opened = false;
}

void BuiltInClock::start (juce::AudioIODeviceCallback* newCallback)
{
    if (! opened || newCallback == nullptr || isThreadRunning())
        return;

    newCallback->audioDeviceAboutToStart (this);
    {
        const juce::ScopedLock sl (callbackLock);
        callback = newCallback;
    }
    startThread (juce::Thread::Priority::highest);
}

void BuiltInClock::stop()
{
    stopThread (2000);

    juce::AudioIODeviceCallback* old = nullptr;
    {
        const juce::ScopedLock sl (callbackLock);
        old = std::exchange (callback, nullptr);
    }
    if (old != nullptr)
        old->audioDeviceStopped();
}

void BuiltInClock::run()
{
    // As Windows' own audio threads: the Pro Audio scheduling class.
    DWORD task = 0;
    const HANDLE mmcss = AvSetMmThreadCharacteristicsW (L"Pro Audio", &task);

    // A high-resolution timer (Windows 10 1803 on); before that the ordinary one at 1 ms.
    HANDLE timer = CreateWaitableTimerExW (nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    const bool coarse = timer == nullptr;
    if (coarse)
    {
        timeBeginPeriod (1);
        timer = CreateWaitableTimerW (nullptr, FALSE, nullptr);
    }

    const auto ticksPerSecond = (double) juce::Time::getHighResolutionTicksPerSecond();
    const auto blockTicks = ticksPerSecond * block / rate;
    double next = (double) juce::Time::getHighResolutionTicks();

    while (! threadShouldExit())
    {
        next += blockTicks;
        const auto now = (double) juce::Time::getHighResolutionTicks();

        if (now - next > blockTicks * 4)   // far behind (the PC slept, or was starved): start again from now
        {
            next = now;
            ++lateBlocks;
        }
        else if (next > now && timer != nullptr)
        {
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG) ((next - now) * 1.0e7 / ticksPerSecond);   // relative, in 100 ns
            if (SetWaitableTimer (timer, &due, 0, nullptr, nullptr, FALSE))
                WaitForSingleObject (timer, 1000);
        }

        const juce::ScopedLock sl (callbackLock);
        if (callback != nullptr)
            callback->audioDeviceIOCallbackWithContext (nullptr, 0, nullptr, 0, block, {});
    }

    if (timer != nullptr)
        CloseHandle (timer);
    if (coarse)
        timeEndPeriod (1);
    if (mmcss != nullptr)
        AvRevertMmThreadCharacteristics (mmcss);
}

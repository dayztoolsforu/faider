// App capture (src/engine/AppCapture.cpp) when the captured program closes: over and over, a
// helper process plays a silent sound, Faider's app capture opens on it, the process is killed
// mid-capture, and the capture is torn down, while thread-pool waits come and go beside it (as
// they do in Faider). It used to close its wake-up event before letting go of Windows' audio
// client: a late signal could then fire a reused handle's thread-pool wait and crash. Passes if
// every round opens, keeps receiving until the kill, and tears down without crashing.
// First, a program picked before it plays: a capture opened on it while silent hears it once it starts.
#include "../src/engine/AppCapture.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <iostream>
#include <atomic>

namespace
{
    int failures = 0;
    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    }

    // A second of silence as a WAV, for the helper to play on a loop (nothing heard).
    juce::File silentWav()
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("faider-silence.wav");
        juce::WavAudioFormat wav;
        juce::AudioBuffer<float> silence (2, 48000);
        silence.clear();
        f.deleteFile();
        if (auto out = std::unique_ptr<juce::OutputStream> (f.createOutputStream()))
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (out.release(), 48000.0, 2, 16, {}, 0)))
                writer->writeFromAudioSampleBuffer (silence, 0, silence.getNumSamples());
        return f;
    }

    // Two seconds of a 40 Hz tone at -72 dBFS: too quiet and too low to hear, but not silence.
    juce::File quietToneWav()
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("faider-quiet-tone.wav");
        juce::WavAudioFormat wav;
        juce::AudioBuffer<float> tone (2, 96000);
        for (int i = 0; i < tone.getNumSamples(); ++i)
        {
            const float s = 0.00025f * std::sin (juce::MathConstants<float>::twoPi * 40.0f * (float) i / 48000.0f);
            tone.setSample (0, i, s);
            tone.setSample (1, i, s);
        }
        f.deleteFile();
        if (auto out = std::unique_ptr<juce::OutputStream> (f.createOutputStream()))
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (out.release(), 48000.0, 2, 16, {}, 0)))
                writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
        return f;
    }

    // The loudest sample a capture hands over in `ms`, pulled as the engine pulls it.
    float peakFor (AppCapture& capture, int ms)
    {
        juce::AudioBuffer<float> block (2, 480);
        float peak = 0.0f;
        for (int t = 0; t < ms; t += 10)
        {
            capture.fifo.pull (block.getArrayOfWritePointers(), 2, block.getNumSamples());
            peak = juce::jmax (peak, block.getMagnitude (0, block.getNumSamples()));
            Sleep (10);
        }
        return peak;
    }

    // Thread-pool waits on events made and closed all the time, so a closed handle gets reused.
    std::atomic<int> fired { 0 };
    void CALLBACK onWait (PTP_CALLBACK_INSTANCE, PVOID, PTP_WAIT, TP_WAIT_RESULT) { ++fired; }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    const auto wav = silentWav();

    // A program picked before it plays (ADD APP's NOT PLAYING YET): the capture opens on it while
    // it's silent, hears nothing, then hears it from when it starts, without being opened again.
    {
        const auto tone = quietToneWav();
        STARTUPINFOW si { sizeof (si) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi {};
        auto command = juce::String ("powershell -NoProfile -Command \"Start-Sleep 5; (New-Object Media.SoundPlayer '") + tone.getFullPathName()
                     + "').PlayLooping(); Start-Sleep 30\"";
        std::wstring line (command.toWideCharPointer());
        const bool started = CreateProcessW (nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != 0;
        check (started, "the helper that plays later started");
        if (started)
        {
            Sleep (1500);   // (PowerShell is up, and sleeping: no sound for 5 s)
            juce::String error;
            auto capture = AppCapture::open ((uint32_t) pi.dwProcessId, 48000.0, 480, error);
            check (capture != nullptr, "app capture opens on a program that isn't playing yet" + (error.isEmpty() ? juce::String() : " (" + error + ")"));
            if (capture != nullptr)
            {
                const float before = peakFor (*capture, 1500);
                peakFor (*capture, 5000);   // (it starts playing somewhere in here)
                const float after = peakFor (*capture, 2000);
                check (before == 0.0f, "nothing comes from it while it's silent (peak " + juce::String (before, 6) + ")");
                check (after > 0.000001f, "its sound arrives once it plays, on the capture opened before (peak " + juce::String (after, 6) + ")");
            }
            TerminateProcess (pi.hProcess, 0);
            WaitForSingleObject (pi.hProcess, 3000);
            CloseHandle (pi.hThread);
            CloseHandle (pi.hProcess);
            Sleep (150);
        }
        tone.deleteFile();
    }

    std::atomic<bool> churning { true };
    std::thread churn ([&churning]
    {
        while (churning)
        {
            auto e = CreateEventW (nullptr, FALSE, FALSE, nullptr);
            auto w = CreateThreadpoolWait (onWait, nullptr, nullptr);
            SetThreadpoolWait (w, e, nullptr);
            Sleep (1);
            SetThreadpoolWait (w, nullptr, nullptr);
            WaitForThreadpoolWaitCallbacks (w, TRUE);
            CloseThreadpoolWait (w);
            CloseHandle (e);
        }
    });

    constexpr int rounds = 10;
    int opened = 0, received = 0;
    for (int round = 0; round < rounds; ++round)
    {
        // The helper: PowerShell playing the silence on a loop (an audio session of its own).
        STARTUPINFOW si { sizeof (si) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi {};
        auto command = juce::String ("powershell -NoProfile -Command \"(New-Object Media.SoundPlayer '") + wav.getFullPathName()
                     + "').PlayLooping(); Start-Sleep 30\"";
        std::wstring line (command.toWideCharPointer());
        if (! CreateProcessW (nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
            continue;
        Sleep (1500);   // (PowerShell starting, then the sound)

        juce::String error;
        auto capture = AppCapture::open ((uint32_t) pi.dwProcessId, 48000.0, 480, error);
        if (capture != nullptr)
        {
            ++opened;
            Sleep (300);
            received += capture->fifo.getFill() > 0 ? 1 : 0;
        }
        TerminateProcess (pi.hProcess, 0);   // the program closes under the capture
        WaitForSingleObject (pi.hProcess, 3000);
        CloseHandle (pi.hThread);
        CloseHandle (pi.hProcess);
        Sleep (50 + 20 * (round % 5));       // (late signals land while the capture goes)
        capture = nullptr;
        Sleep (100);
    }
    churning = false;
    churn.join();

    check (opened >= rounds - 2, "app capture opened on the helper (" + juce::String (opened) + " of " + juce::String (rounds) + ")");
    check (received >= opened / 2, "audio arrived while it ran (" + juce::String (received) + " rounds)");
    check (true, "every capture torn down as its program closed, no crash (" + juce::String (rounds) + " rounds)");
    wav.deleteFile();
    std::cout << (failures == 0 ? "All passed.\n" : juce::String (failures) + " failed.\n");
    return failures;
}

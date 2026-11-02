#include "Pads.h"

namespace
{
    constexpr double releaseFade = 0.03;    // seconds: a hold pad let go, a loop stopped, Stop all
    constexpr double restartFade = 0.005;   // seconds: a pad pressed again, or choked
    constexpr int maxSeconds = 600;         // longest sound loaded (10 minutes)
}

Pads::Pads()
{
    formats.registerBasicFormats();   // WAV, AIFF, FLAC, OGG, MP3
}

bool Pads::load (int index, const juce::File& file, double rate, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Can't read " + file.getFileName() + " (WAV, FLAC, AIFF, OGG or MP3).";
        return false;
    }

    const auto length = (int) juce::jmin ((juce::int64) (reader->sampleRate * maxSeconds), reader->lengthInSamples);
    juce::AudioBuffer<float> source (2, juce::jmax (1, length));
    reader->read (&source, 0, length, 0, true, reader->numChannels > 1);
    if (reader->numChannels == 1)
        source.copyFrom (1, 0, source, 0, 0, length);   // mono: both sides

    // To the engine's rate once, here, so playback is a straight copy.
    auto sound = std::make_shared<Sound>();
    sound->rate = rate;
    const double ratio = reader->sampleRate / rate;
    const int out = juce::jmax (1, (int) std::ceil (length / ratio));
    sound->audio.setSize (2, out);
    for (int ch = 0; ch < 2; ++ch)
    {
        juce::LagrangeInterpolator resampler;
        resampler.process (ratio, source.getReadPointer (ch), sound->audio.getWritePointer (ch), out, length, 0);
    }

    retire (index);
    auto& p = pads[(size_t) index];
    p.full = p.sound = sound;
    p.trimStart = p.trimEnd = p.fadeIn = p.fadeOut = 0.0;
    p.file = file.getFullPathName();
    if (p.name.isEmpty())
        p.name = file.getFileNameWithoutExtension().toUpperCase();   // names are in capitals
    return true;
}

void Pads::applyEdits (int index)
{
    auto& p = pads[(size_t) index];
    if (p.full == nullptr)
        return;

    const auto& source = p.full->audio;
    const double rate = p.full->rate;
    const int total = source.getNumSamples();
    p.trimStart = juce::jlimit (0.0, p.fullSeconds(), p.trimStart);
    p.trimEnd = p.trimEnd > 0.0 ? juce::jlimit (p.trimStart, p.fullSeconds(), p.trimEnd) : 0.0;
    const int from = juce::jlimit (0, juce::jmax (0, total - 1), (int) (p.trimStart * rate));
    const int to = p.trimEnd > 0.0 ? juce::jlimit (from + 1, total, (int) (p.trimEnd * rate)) : total;
    const int length = to - from;
    p.fadeIn = juce::jmax (0.0, p.fadeIn);
    p.fadeOut = juce::jmax (0.0, p.fadeOut);
    const int in = juce::jmin (length, (int) (p.fadeIn * rate)), out = juce::jmin (length - in, (int) (p.fadeOut * rate));

    retire (index);
    if (from == 0 && to == total && in == 0 && out == 0)
    {
        p.sound = p.full;   // nothing trimmed or faded: the file as it is
        return;
    }
    auto sound = std::make_shared<Sound>();
    sound->rate = rate;
    sound->audio.setSize (2, length);
    for (int ch = 0; ch < 2; ++ch)
    {
        sound->audio.copyFrom (ch, 0, source, ch, from, length);
        if (in > 0)  sound->audio.applyGainRamp (ch, 0, in, 0.0f, 1.0f);
        if (out > 0) sound->audio.applyGainRamp (ch, length - out, out, 1.0f, 0.0f);
    }
    p.sound = sound;
}

void Pads::clear (int index)
{
    retire (index);
    pads[(size_t) index] = Pad();
}

void Pads::swap (int a, int b)
{
    if (a == b)
        return;
    send ({ Command::stop, a });   // what's playing belongs to the old places
    send ({ Command::stop, b });
    std::swap (pads[(size_t) a], pads[(size_t) b]);
}

// Stops the pad's voices now and keeps its old sound alive until the audio thread has taken the
// stop (then nothing can be playing it). Sounds retired earlier whose stops it has taken go.
void Pads::retire (int index)
{
    auto& p = pads[(size_t) index];
    if (p.sound == nullptr)
        return;

    Message stop;
    stop.command = Command::stop;
    stop.pad = index;
    const auto number = send (stop);
    const auto done = taken.load();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [done] (auto& r) { return r.first <= done; }), retired.end());
    retired.push_back ({ number > 0 ? number : std::numeric_limits<std::uint64_t>::max(), std::move (p.sound) });
}

void Pads::press (int index)
{
    const auto& p = pads[(size_t) index];
    if (p.sound != nullptr)
        send ({ Command::press, index, p.sound.get(), p.gain, p.mode, p.choke, 0 });
}

void Pads::release (int index)
{
    const auto& p = pads[(size_t) index];
    if (p.sound != nullptr && p.mode == Mode::hold)
        send ({ Command::release, index });
}

void Pads::stop (int index)
{
    send ({ Command::release, index });
}

void Pads::stopAll()
{
    send ({ Command::stopAll });
}

std::uint64_t Pads::send (Message m)
{
    int start1, size1, start2, size2;
    fifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 + size2 == 0)
        return 0;   // full (the audio isn't running): dropped
    m.number = ++sent;
    queue[(size_t) (size1 > 0 ? start1 : start2)] = m;
    fifo.finishedWrite (1);
    return m.number;
}

void Pads::take (double rate, bool presses)
{
    int start1, size1, start2, size2;
    fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);
    auto one = [&] (const Message& m)
    {
        if (presses || m.command != Command::press)
            handle (m, rate);
        taken.store (m.number);
    };
    for (int i = 0; i < size1; ++i) one (queue[(size_t) (start1 + i)]);
    for (int i = 0; i < size2; ++i) one (queue[(size_t) (start2 + i)]);
    fifo.finishedRead (size1 + size2);
}

void Pads::drain (double rate)
{
    take (rate, false);
}

//==============================================================================
void Pads::fadeOut (Voice& v, double rate, double seconds)
{
    const int length = juce::jmax (1, (int) (rate * seconds));
    if (v.fade < 0 || v.fade > length)
    {
        v.fade = length;
        v.fadeLength = length;
    }
}

void Pads::handle (const Message& m, double rate)
{
    auto voicesOf = [this] (int pad, auto&& fn) { for (auto& v : voice) if (v.sound != nullptr && v.pad == pad) fn (v); };

    switch (m.command)
    {
        case Command::stop:
            voicesOf (m.pad, [] (Voice& v) { v.sound = nullptr; });
            return;

        case Command::stopAll:
            for (auto& v : voice)
                if (v.sound != nullptr)
                    fadeOut (v, rate, releaseFade);
            return;

        case Command::release:
            voicesOf (m.pad, [rate] (Voice& v) { fadeOut (v, rate, releaseFade); });
            return;

        case Command::press:
            break;
    }

    // A loop pressed while it plays: stop it.
    if (m.mode == Mode::loop)
    {
        bool wasPlaying = false;
        voicesOf (m.pad, [&] (Voice& v) { if (v.fade < 0) { wasPlaying = true; fadeOut (v, rate, releaseFade); } });
        if (wasPlaying)
            return;
    }
    else
    {
        voicesOf (m.pad, [rate] (Voice& v) { fadeOut (v, rate, restartFade); });   // pressed again: restart
    }

    if (m.choke > 0)
        for (auto& v : voice)
            if (v.sound != nullptr && v.choke == m.choke && v.pad != m.pad)
                fadeOut (v, rate, restartFade);

    // A free voice, or else the oldest one.
    Voice* chosen = nullptr;
    for (auto& v : voice)
        if (v.sound == nullptr) { chosen = &v; break; }
    if (chosen == nullptr)
        for (auto& v : voice)
            if (chosen == nullptr || v.started < chosen->started)
                chosen = &v;

    *chosen = { m.sound, m.pad, m.choke, 0.0, m.gain, m.mode == Mode::loop, -1, 1, ++starts };
}

void Pads::render (juce::AudioBuffer<float>& out, int numSamples, double rate)
{
    take (rate, true);

    out.clear (0, numSamples);
    std::array<int, count> active {};
    std::array<std::uint64_t, count> newest {};

    auto* left = out.getWritePointer (0);
    auto* right = out.getWritePointer (1);

    for (auto& v : voice)
    {
        if (v.sound == nullptr)
            continue;

        const auto& audio = v.sound->audio;
        const int length = audio.getNumSamples();
        const double step = v.sound->rate / rate;   // 1 unless the engine's rate changed since loading
        const auto* srcL = audio.getReadPointer (0);
        const auto* srcR = audio.getReadPointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            if (v.pos >= length)
            {
                if (! v.loop) { v.sound = nullptr; break; }
                v.pos -= length;
            }

            const int a = (int) v.pos;
            const int b = a + 1 < length ? a + 1 : (v.loop ? 0 : a);
            const float frac = (float) (v.pos - a);
            float g = v.gain;
            if (v.fade >= 0)
            {
                g *= (float) v.fade / (float) v.fadeLength;
                if (--v.fade < 0) { v.sound = nullptr; break; }
            }

            left[i] += g * (srcL[a] + frac * (srcL[b] - srcL[a]));
            right[i] += g * (srcR[a] + frac * (srcR[b] - srcR[a]));
            v.pos += step;
        }

        if (v.sound != nullptr && juce::isPositiveAndBelow (v.pad, count))
        {
            ++active[(size_t) v.pad];
            if (v.started >= newest[(size_t) v.pad])
            {
                newest[(size_t) v.pad] = v.started;
                position[(size_t) v.pad].store ((float) juce::jlimit (0.0, 1.0, v.pos / juce::jmax (1, length)));
            }
        }
    }

    for (int p = 0; p < count; ++p)
        playing[(size_t) p].store (active[(size_t) p]);
}

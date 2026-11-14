// AudioEngine on the audio thread: the master callback, processStrip, the rack as it runs, the sends.
#include "AudioEngine.h"
#include "AudioEngineInternal.h"
#include "VoiceChain.h"

namespace
{
    // Delays the first two channels of `buf` by `delay` samples, through the ring `line`
    // (latency compensation).
    void delayBlock (juce::AudioBuffer<float>& line, int& pos, int delay, juce::AudioBuffer<float>& buf, int numSamples)
    {
        if (delay <= 0)
            return;

        const int size = line.getNumSamples();
        for (int i = 0; i < numSamples; ++i)
        {
            const int readPos = (pos - delay + size) % size;
            for (int ch = 0; ch < 2; ++ch)
            {
                line.setSample (ch, pos, buf.getSample (ch, i));
                buf.setSample (ch, i, line.getSample (ch, readPos));
            }
            pos = (pos + 1) % size;
        }
    }

    // Peak meter (see Metering.h): instant attack, `release` per block of fall-off.
    void updatePeak (std::atomic<float>& peak, float level, float release)
    {
        peak = juce::jmax (level, peak.load() * release);
    }

    // A NaN or infinity (a misbehaving plugin) becomes silence, so it can't stick in the meters,
    // filters and loudness that follow or reach a device.
    void keepFinite (juce::AudioBuffer<float>& buf, int numSamples) noexcept
    {
        for (int ch = 0; ch < juce::jmin (2, buf.getNumChannels()); ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                if (! std::isfinite (d[i]))
                    d[i] = 0.0f;
        }
    }

    // Runs a stereo buffer through one plugin. Plugins with other channel counts get a mono fold
    // or the stereo pair padded with silence, via a scratch buffer. False if it couldn't run.
    bool runPlugin (juce::AudioPluginInstance* plugin, juce::AudioBuffer<float>& scratch, juce::MidiBuffer& midi,
                    juce::AudioBuffer<float>& buf, int numSamples)
    {
        {
            const int ins = plugin->getTotalNumInputChannels();
            const int outs = plugin->getTotalNumOutputChannels();
            const int channels = juce::jmax (ins, outs);

            if (channels == 0 || channels > scratch.getNumChannels() || plugin->isSuspended())
                return false;

            scratch.clear (0, numSamples);

            if (ins == 1)
            {
                scratch.addFrom (0, 0, buf, 0, 0, numSamples, 0.5f);
                scratch.addFrom (0, 0, buf, 1, 0, numSamples, 0.5f);
            }
            else if (ins >= 2)
            {
                scratch.copyFrom (0, 0, buf, 0, 0, numSamples);
                scratch.copyFrom (1, 0, buf, 1, 0, numSamples);
            }

            juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), channels, numSamples);
            midi.clear();

            {
                const juce::ScopedLock pluginLock (plugin->getCallbackLock());
                plugin->processBlock (view, midi);
            }

            if (outs >= 2)
            {
                buf.copyFrom (0, 0, view, 0, 0, numSamples);
                buf.copyFrom (1, 0, view, 1, 0, numSamples);
            }
            else if (outs == 1)
            {
                buf.copyFrom (0, 0, view, 0, 0, numSamples);
                buf.copyFrom (1, 0, view, 0, 0, numSamples);
            }
            keepFinite (buf, numSamples);
        }
        return true;
    }

    // Runs a stereo buffer through one plugin with its insert options (AudioEngine::InsertOptions):
    // IN gain, the plugin, OUT gain, then MIX against the sound as it came in (delayed by the
    // plugin's latency to stay in time). A bypassed plugin (or every plugin, `chainOn` false: the
    // INSERT key out) isn't run; the delayed sound goes on in its place. Every change ramps
    // across the block.
    void processInsert (juce::AudioPluginInstance* plugin, const AudioEngine::InsertOptionsTable& table,
                        juce::AudioBuffer<float>& scratch, juce::MidiBuffer& midi, juce::AudioBuffer<float>& buf, int numSamples,
                        bool chainOn = true)
    {
        {
            const auto it = table.find (plugin);
            if (it == table.end())
            {
                runPlugin (plugin, scratch, midi, buf, numSamples);
                return;
            }
            auto& o = *it->second;

            for (int ch = 0; ch < 2; ++ch)
                o.dry.copyFrom (ch, 0, buf, ch, 0, numSamples);
            delayBlock (o.line, o.pos, juce::jmin (plugin->getLatencySamples(), maxCompensation), o.dry, numSamples);

            const float blendTo = o.bypassed.load() || ! chainOn ? 0.0f : o.mix.load();
            if (blendTo == 0.0f && o.blend == 0.0f)
            {
                for (int ch = 0; ch < 2; ++ch)
                    buf.copyFrom (ch, 0, o.dry, ch, 0, numSamples);
                o.secondsPerSample = 0.0f;
                return;
            }

            const float inTo = o.inGain.load(), outTo = o.outGain.load();
            if (o.in != 1.0f || inTo != 1.0f)
                for (int ch = 0; ch < 2; ++ch)
                    buf.applyGainRamp (ch, 0, numSamples, o.in, inTo);
            o.in = inTo;

            const auto started = juce::Time::getHighResolutionTicks();
            const bool ran = runPlugin (plugin, scratch, midi, buf, numSamples);
            const auto took = (float) juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - started);
            o.secondsPerSample = o.secondsPerSample.load() * 0.95f + (ran ? took / (float) numSamples : 0.0f) * 0.05f;

            if (o.out != 1.0f || outTo != 1.0f)
                for (int ch = 0; ch < 2; ++ch)
                    buf.applyGainRamp (ch, 0, numSamples, o.out, outTo);
            o.out = outTo;

            if (o.blend != 1.0f || blendTo != 1.0f)
                for (int ch = 0; ch < 2; ++ch)
                {
                    buf.applyGainRamp (ch, 0, numSamples, o.blend, blendTo);
                    buf.addFromWithRamp (ch, 0, o.dry.getReadPointer (ch), numSamples, 1.0f - o.blend, 1.0f - blendTo);
                }
            o.blend = blendTo;
        }
    }

    // A plugin chain in order (a combinator path's).
    void processInserts (std::vector<std::unique_ptr<juce::AudioPluginInstance>>& chain, const AudioEngine::InsertOptionsTable& table,
                         juce::AudioBuffer<float>& scratch, juce::MidiBuffer& midi, juce::AudioBuffer<float>& buf, int numSamples)
    {
        for (auto& plugin : chain)
            processInsert (plugin.get(), table, scratch, midi, buf, numSamples);
    }
}

// Audio thread: what a channel's dynamics hear this block besides itself. KEY: the keying
// channel's sound (after its processing), this block's, or its last if that channel runs after
// this one. A keying channel switched off keys nothing (its tap would be stale).
void AudioEngine::keyChain (Strip& s, VoiceChain& chain, int numSamples)
{
    if (const int k = s.keySource.load(); juce::isPositiveAndBelow (k, (int) strips.size()) && strips[(size_t) k].get() != &s
                                               && strips[(size_t) k]->enabled.load()
                                               && strips[(size_t) k]->keyTap.getNumSamples() >= numSamples)
        chain.setKey (strips[(size_t) k]->keyTap.getReadPointer (0), strips[(size_t) k]->keyTap.getReadPointer (1), numSamples);
    else
        chain.setKey (nullptr, nullptr, 0);
}

// Audio thread: a channel's processing, its rack in the middle (see RackPatch).
void AudioEngine::runRack (Strip& s, juce::AudioBuffer<float>& buf, int numSamples)
{
    // AUDITION: the outputs get the committed copy; the edited chain runs on the side (below).
    auto* edited = s.channel.get();
    auto* committed = s.committed.get();
    auto* chain = committed != nullptr ? committed : edited;
    const bool auditioning = committed != nullptr && edited != nullptr && s.auditionBuf.getNumSamples() >= numSamples;
    if (auditioning)
        for (int ch = 0; ch < 2; ++ch)
            s.auditionBuf.copyFrom (ch, 0, buf, ch, 0, numSamples);
    juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, numSamples);
    auto* plan = s.rackPlan.get();
    if (chain != nullptr)
    {
        keyChain (s, *chain, numSamples);
        chain->beginBlock (numSamples);
        chain->inputTap = s.tapInput ? &s.rawBuffer : nullptr;   // (RAW sends: after the input stage, wherever it runs)
        chain->sourceStereo = s.source.stereo;
        // The strip's own stages (those not moved into the rack) ahead of it; with no rack yet, all of them.
        if (const unsigned before = plan != nullptr ? plan->before : (unsigned) VoiceChain::allStages; before != 0)
            chain->runStages (view, before);
    }

    if (plan != nullptr)
    {
        auto gather = [&] (const std::vector<RackPlan::Feed>& feeds, juce::AudioBuffer<float>& into)
        {
            for (int ch = 0; ch < 2; ++ch)
                into.clear (ch, 0, numSamples);
            for (auto& f : feeds)
            {
                const auto& from = f.from < 0 ? buf : plan->buffers[(size_t) f.from];
                if (f.line < 0)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        into.addFrom (ch, 0, from, ch, 0, numSamples);
                    continue;
                }
                for (int ch = 0; ch < 2; ++ch)
                    plan->scratch.copyFrom (ch, 0, from, ch, 0, numSamples);
                auto& line = plan->lines[(size_t) f.line];
                delayBlock (line.ring, line.pos, f.delay, plan->scratch, numSamples);
                for (int ch = 0; ch < 2; ++ch)
                    into.addFrom (ch, 0, plan->scratch, ch, 0, numSamples);
            }
        };

        const bool pluginsOn = s.insertsOn.load();
        for (size_t i = 0; i < plan->nodes.size(); ++i)
        {
            auto& node = plan->nodes[i];
            auto& b = plan->buffers[i];
            gather (node.feeds, b);
            juce::AudioBuffer<float> nodeView (b.getArrayOfWritePointers(), 2, numSamples);
            if (node.plugin != nullptr)
                processInsert (node.plugin, optionsTable, s.pluginBuffer, s.midi, nodeView, numSamples, pluginsOn);
            else if (chain != nullptr)
                chain->runStages (nodeView, rackStages (node.builtin));
        }
        gather (plan->out, plan->result);
        for (int ch = 0; ch < 2; ++ch)
            buf.copyFrom (ch, 0, plan->result, ch, 0, numSamples);
    }

    if (chain != nullptr)
    {
        if (plan != nullptr && plan->after != 0)   // the strip's own output stages, after the rack
            chain->runStages (view, plan->after);
        chain->endBlock (view);
        chain->inputTap = nullptr;
    }

    if (auditioning)   // the edited settings, every stage, for the control room alone
    {
        juce::AudioBuffer<float> side (s.auditionBuf.getArrayOfWritePointers(), 2, numSamples);
        keyChain (s, *edited, numSamples);
        edited->sourceStereo = s.source.stereo;
        edited->beginBlock (numSamples);
        edited->runStages (side, VoiceChain::allStages);
        edited->endBlock (side);
    }
}

std::pair<float, float> AudioEngine::stepFade (float& level, bool on, int numSamples) const
{
    const float start = level;
    const float step = (float) numSamples / (float) (interfaceRate * (on ? fadeInSeconds : fadeOutSeconds));
    level = on ? juce::jmin (1.0f, level + step) : juce::jmax (0.0f, level - step);
    return { start, level };
}

//==============================================================================
// `soloedOut`: another input is soloed and this one isn't, so it's silenced like a mute.
void AudioEngine::processStrip (Strip& s, const float* const* inputs, int numInputs, int numSamples, bool soloedOut)
{
    auto& buf = s.buffer;
    buf.clear (0, numSamples);

    switch (s.source.kind)
    {
        case Source::interfaceInputs:
        {
            const int left = s.source.firstChannel;
            const bool hasRight = s.source.stereo && left + 1 < numInputs && inputs[left + 1] != nullptr;

            if (juce::isPositiveAndBelow (left, numInputs) && inputs[left] != nullptr)
            {
                buf.copyFrom (0, 0, inputs[left], numSamples);
                buf.copyFrom (1, 0, inputs[hasRight ? left + 1 : left], numSamples);
            }
            break;
        }

        case Source::device:
            if (s.input != nullptr)
            {
                // The device read once this block, whichever strip asks first; each strip takes
                // its own channel (pair) from it. A pair past the device's end: its first pair.
                auto& in = *s.input;
                if (in.pulledAt != blockSerial)
                {
                    in.fifo.pull (in.inBlock.getArrayOfWritePointers(), in.channels, numSamples);
                    in.pulledAt = blockSerial;
                }
                const bool pair = s.source.stereo && in.channels > 1;
                const int left = s.source.firstChannel >= 0 && s.source.firstChannel + (pair ? 1 : 0) < in.channels ? s.source.firstChannel : 0;
                buf.copyFrom (0, 0, in.inBlock, left, 0, numSamples);
                buf.copyFrom (1, 0, in.inBlock, pair ? left + 1 : left, 0, numSamples);
            }
            break;

        case Source::loopback:
            if (juce::isPositiveAndBelow (s.source.masterIndex, (int) masters.size()))
            {
                auto& bus = *masters[(size_t) s.source.masterIndex];

                if (bus.loopback && bus.enabled)   // disabled: lastBlock is stale, so stay silent
                {
                    const int n = juce::jmin (numSamples, bus.lastBlockSamples);
                    buf.copyFrom (0, 0, bus.lastBlock, 0, 0, n);
                    buf.copyFrom (1, 0, bus.lastBlock, 1, 0, n);

                    if (! s.source.stereo)
                    {
                        buf.addFrom (0, 0, buf, 1, 0, n);
                        buf.applyGain (0, 0, n, 0.5f);
                        buf.copyFrom (1, 0, buf, 0, 0, n);
                    }
                }
            }
            break;

        case Source::app:
            if (s.app != nullptr)
            {
                auto& app = *s.app;   // read once this block, whichever strip asks first (as devices)
                if (app.pulledAt != blockSerial)
                {
                    app.fifo.pull (app.inBlock.getArrayOfWritePointers(), 2, numSamples);
                    app.pulledAt = blockSerial;
                }
                buf.copyFrom (0, 0, app.inBlock, 0, 0, numSamples);
                buf.copyFrom (1, 0, app.inBlock, 1, 0, numSamples);

                if (! s.source.stereo)
                {
                    buf.addFrom (0, 0, buf, 1, 0, numSamples);
                    buf.applyGain (0, 0, numSamples, 0.5f);
                    buf.copyFrom (1, 0, buf, 0, 0, numSamples);
                }
            }
            break;

        case Source::bus:
            buf.copyFrom (0, 0, s.busInput, 0, 0, numSamples);
            buf.copyFrom (1, 0, s.busInput, 1, 0, numSamples);

            if (! s.source.stereo)
            {
                buf.addFrom (0, 0, buf, 1, 0, numSamples);
                buf.applyGain (0, 0, numSamples, 0.5f);
                buf.copyFrom (1, 0, buf, 0, 0, numSamples);
            }

            // Combinator: each path runs what was sent to it; the paths' sum goes on.
            if (s.combinator && ! s.paths.empty())
            {
                s.pathSum.clear (0, numSamples);

                for (auto& path : s.paths)
                {
                    auto& p = *path;
                    for (int ch = 0; ch < 2; ++ch)
                        p.buffer.copyFrom (ch, 0, p.input, ch, 0, numSamples);
                    if (! s.source.stereo)
                    {
                        p.buffer.addFrom (0, 0, p.buffer, 1, 0, numSamples);
                        p.buffer.applyGain (0, 0, numSamples, 0.5f);
                        p.buffer.copyFrom (1, 0, p.buffer, 0, 0, numSamples);
                    }

                    processInserts (p.inserts, optionsTable, p.pluginBuffer, p.midi, p.buffer, numSamples);
                    delayBlock (p.delayLine, p.delayPos, p.compensation, p.buffer, numSamples);

                    p.buffer.applyGain (0, numSamples, p.mute ? 0.0f : p.gain.load());
                    updatePeak (p.peakL, p.buffer.getMagnitude (0, 0, numSamples), meterRelease);
                    updatePeak (p.peakR, p.buffer.getMagnitude (1, 0, numSamples), meterRelease);
                    p.levels.process (p.buffer, numSamples, s.source.stereo, interfaceRate);
                    for (int ch = 0; ch < 2; ++ch)
                        s.pathSum.addFrom (ch, 0, p.buffer, ch, 0, numSamples);
                }

                for (int ch = 0; ch < 2; ++ch)
                    buf.copyFrom (ch, 0, s.pathSum, ch, 0, numSamples);
            }
            break;

        case Source::none:
            break;

        case Source::tape:   // rendered once, however many strips play it
            if (! tapeRendered)
            {
                tape.render (tapeMix, numSamples, interfaceRate);
                tapeRendered = true;
            }
            for (int ch = 0; ch < 2; ++ch)
                buf.copyFrom (ch, 0, tapeMix, ch, 0, numSamples);
            break;

        case Source::pads:   // the PADS strip, unless the pads play into another input
            if (! padsElsewhere)
            {
                renderPads (numSamples);
                for (int ch = 0; ch < 2; ++ch)
                    buf.copyFrom (ch, 0, padsMix, ch, 0, numSamples);
            }
            break;

        case Source::tone:
        {
            const double delta = juce::MathConstants<double>::twoPi * 440.0 / interfaceRate;

            for (int i = 0; i < numSamples; ++i)
            {
                const auto v = (float) (0.125 * std::sin (s.tonePhase));   // -18 dBFS
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
                s.tonePhase = std::fmod (s.tonePhase + delta, juce::MathConstants<double>::twoPi);
            }
            break;
        }
    }

    // The input the pads play into: mixed in ahead of its plugins, so they go through its chain and sends.
    if (s.padsInto.load())
    {
        renderPads (numSamples);
        for (int ch = 0; ch < 2; ++ch)
            buf.addFrom (ch, 0, padsMix, ch, 0, numSamples);
    }
    keepFinite (buf, numSamples);   // (a device or app can hand over a NaN too: kept out of the meters and rack)

    // The source as read, before any processing: its meter (MASTER's record devices), and what
    // RAW sends carry. Off air the same way the fader is: mute and solo silence it.
    updatePeak (s.sourcePeakL, buf.getMagnitude (0, 0, numSamples), meterRelease);
    updatePeak (s.sourcePeakR, buf.getMagnitude (1, 0, numSamples), meterRelease);
    s.sourceLevels.process (buf, numSamples, s.source.stereo, interfaceRate);
    const int point = meterPoint.load();
    if (point == 0)   // IN: the channel's meter reads the source as it comes in
    {
        updatePeak (s.peakL, buf.getMagnitude (0, 0, numSamples), meterRelease);
        updatePeak (s.peakR, buf.getMagnitude (1, 0, numSamples), meterRelease);
        s.levels.process (buf, numSamples, s.source.stereo, interfaceRate);
    }
    if (s.mute || soloedOut)
        s.rawBuffer.clear (0, numSamples);
    else
        for (int ch = 0; ch < 2; ++ch)
            s.rawBuffer.copyFrom (ch, 0, buf, ch, 0, numSamples);

    // A captured program (browser, game, player) has assign, mute, cue and DUCK UNDER only: its
    // EQ, dynamics, inserts, pan and fader are switched out (kept, but not run).
    const bool program = s.source.kind == Source::app;

    // The channel's processing: its rack (built-in units and plugins, as patched).
    if (! program)
    {
        s.tapInput = ! (s.mute || soloedOut);
        runRack (s, buf, numSamples);
        keepFinite (buf, numSamples);   // (the built-in stages too, not only plugins)
    }

    // Delay this strip so every strip lines up with the one whose plugins add the most latency.
    delayBlock (s.delayLine, s.delayPos, s.compensation, buf, numSamples);

    // Auto-duck, keyed by its input's level after that input's fader (last block's, if that
    // strip comes later in the order). Down in about 20 ms, held 150 ms, back over the release.
    // An app ducks too (its key level is read the same way: no wait on its capture).
    {
        float target = 1.0f;
        const int key = s.duckSource.load();
        if (juce::isPositiveAndBelow (key, (int) strips.size()))
        {
            const auto& k = *strips[(size_t) key];
            const float level = k.keyLevel.load();
            if (k.enabled && level > juce::Decibels::decibelsToGain (s.duckThreshold.load()))
                s.duckHold = (int) (0.15 * interfaceRate);
            else
                s.duckHold = juce::jmax (0, s.duckHold - numSamples);
            if (s.duckHold > 0)
                target = juce::Decibels::decibelsToGain (s.duckDepth.load());
        }

        const float from = s.duckGain.load();
        const double ms = target < from ? 20.0 : juce::jmax (10.0f, s.duckRelease.load());
        const float to = from + (target - from) * (float) (1.0 - std::exp (-numSamples / (ms * 0.001 * interfaceRate)));
        s.duckGain = std::abs (to - target) < 0.0005f ? target : to;
        if (from < 1.0f || to < 1.0f)
            for (int ch = 0; ch < 2; ++ch)
                buf.applyGainRamp (ch, 0, numSamples, from, s.duckGain.load());
    }

    // Its source being changed: down to silence before the switch, back up after (setStripSource).
    if (s.sourceFadeRestart.exchange (false))
        s.sourceFade = 0.0f;
    if (const auto [from, to] = stepFade (s.sourceFade, ! s.sourceFadeOut.load(), numSamples); from < 1.0f || to < 1.0f)
    {
        buf.applyGainRamp (0, numSamples, from, to);
        s.rawBuffer.applyGainRamp (0, numSamples, from, to);
    }

    // (Its sound here, after the processing and before the fader, for any strip keyed by it.)
    if (s.keyTap.getNumSamples() >= numSamples)
        for (int ch = 0; ch < 2; ++ch)
            s.keyTap.copyFrom (ch, 0, buf, ch, 0, numSamples);

    // AUDITION: the edited sound (or with B the committed one) for the control room, alone.
    if (s.committed != nullptr && s.auditionBuf.getNumSamples() >= numSamples && auditionMix.getNumSamples() >= numSamples)
    {
        for (int ch = 0; ch < 2; ++ch)
            auditionMix.addFrom (ch, 0, s.auditionB.load() ? buf : s.auditionBuf, ch, 0, numSamples);
        auditionThisBlock = true;
    }

    if (point == 1)   // PRE FADER: after the processing, before the fader, pan and ON
    {
        updatePeak (s.peakL, buf.getMagnitude (0, 0, numSamples), meterRelease);
        updatePeak (s.peakR, buf.getMagnitude (1, 0, numSamples), meterRelease);
        s.levels.process (buf, numSamples, s.source.stereo, interfaceRate);
    }

    // PRE sends' tap: after the processing, before the fader and pan (CUT mutes it too).
    if (std::any_of (s.preSends.begin(), s.preSends.begin() + (std::ptrdiff_t) masters.size(), [] (auto& x) { return x.load(); }))
    {
        for (int ch = 0; ch < 2; ++ch)
            s.preBuffer.copyFrom (ch, 0, buf, ch, 0, numSamples);
        if (s.mute.load())
            s.preBuffer.clear (0, numSamples);
    }

    const float gain = s.mute || soloedOut ? 0.0f : program ? 1.0f : s.gain.load();
    const float pan = program ? 0.0f : s.pan.load();

    // WIDTH (stereo): the sides narrowed (to mono at 0) or widened (to twice at 2), before the balance.
    if (const float w = s.width.load(); s.source.stereo && ! program && std::abs (w - 1.0f) > 0.001f)
    {
        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            const float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]) * w;
            l[i] = mid + side;
            r[i] = mid - side;
        }
    }

    if (s.source.stereo)
    {
        // Balance: turning right attenuates left, and vice versa.
        buf.applyGain (0, 0, numSamples, gain * juce::jmin (1.0f, 1.0f - pan));
        buf.applyGain (1, 0, numSamples, gain * juce::jmin (1.0f, 1.0f + pan));
    }
    else
    {
        // Mono: inserts may have widened it, so fold to one channel, then constant-power pan.
        buf.addFrom (0, 0, buf, 1, 0, numSamples);
        buf.applyGain (0, 0, numSamples, 0.5f);
        if (s.binaural.load())
        {
            // BINAURAL: placed as a head hears it: the far ear up to 0.66 ms later (ITD), quieter by
            // up to 6 dB and duller (the head's shadow, a low-pass falling from 20 kHz to 1.5 kHz);
            // the near ear as it is. Centre: both ears alike.
            const float p = juce::jlimit (-1.0f, 1.0f, pan), side = std::abs (p);
            const int ring = (int) s.binauralRing.size();
            if (! s.binauralWas)   // (just switched on: no old sound in its line)
            {
                s.binauralRing.fill (0.0f);
                s.binauralShadow = 0.0f;
                s.binauralItd = (float) (side * 0.00066 * interfaceRate);
            }
            s.binauralWas = true;
            // (the far ear's delay glides across the block, read between samples: no clicks as it pans)
            const float itdFrom = s.binauralItd, itdTo = juce::jlimit (0.0f, (float) ring - 2.0f, (float) (side * 0.00066 * interfaceRate));
            s.binauralItd = itdTo;
            const float nearGain = gain * (0.7071f + 0.2929f * side);   // (centre -3 dB a side, as the constant-power pan)
            const float farGain = nearGain * juce::Decibels::decibelsToGain (-6.0f * side);
            const float cutoff = 20000.0f * std::pow (1500.0f / 20000.0f, side);
            const float a = std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (cutoff, 0.45f * (float) interfaceRate) / (float) interfaceRate);
            auto* l = buf.getWritePointer (0);
            auto* r = buf.getWritePointer (1);
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = l[i];
                s.binauralRing[(size_t) s.binauralPos] = x;
                const float d = itdFrom + (itdTo - itdFrom) * (float) i / (float) numSamples;
                const int whole = (int) d;
                const float frac = d - (float) whole;
                const float a0 = s.binauralRing[(size_t) ((s.binauralPos - whole + ring) % ring)], a1 = s.binauralRing[(size_t) ((s.binauralPos - whole - 1 + ring) % ring)];
                const float late = a0 + (a1 - a0) * frac;
                s.binauralPos = (s.binauralPos + 1) % ring;
                s.binauralShadow = late + a * (s.binauralShadow - late);
                const float nearEar = x * nearGain, farEar = s.binauralShadow * farGain;
                l[i] = p > 0.0f ? farEar : nearEar;
                r[i] = p > 0.0f ? nearEar : farEar;
            }
        }
        else
        {
            s.binauralWas = false;
            const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            buf.copyFrom (1, 0, buf, 0, 0, numSamples);
            buf.applyGain (1, 0, numSamples, gain * std::sin (angle));
            buf.applyGain (0, 0, numSamples, gain * std::cos (angle));
        }
    }

    if (point == 2)   // POST
    {
        updatePeak (s.peakL, buf.getMagnitude (0, 0, numSamples), meterRelease);
        updatePeak (s.peakR, buf.getMagnitude (1, 0, numSamples), meterRelease);
        s.levels.process (buf, numSamples, s.source.stereo, interfaceRate);
    }

    // What ducks other strips keyed by this one: after its fader and ON, wherever the meters read
    // (so a muted mic never ducks anything).
    updatePeak (s.keyLevel, buf.getMagnitude (0, numSamples), meterRelease);
}

// This block's pads, rendered once however many strips use them (audio thread).
void AudioEngine::renderPads (int numSamples)
{
    if (! padsRendered)
    {
        pads.render (padsMix, numSamples, interfaceRate);
        padsRendered = true;
    }
}

// Adds a processed strip or pair into the masters and buses it sends to.
// Each send fades in when it's made and out when it's broken (stepFade).
void AudioEngine::sendFrom (Strip& s, int selfBus, int numSamples)
{
    // A send's level (SENDS ON FADER), ramped from where it was last block so moving it doesn't click.
    auto level = [] (std::atomic<float>& db, float& dbNow)
    {
        const auto gain = [] (float d) { return d <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (d); };
        const float target = db.load(), was = dbNow;
        dbNow = target;
        return std::pair<float, float> { gain (was), gain (target) };
    };

    // RAW: after its input stage, before its processing and fader; PRE: before its fader; else after it.
    auto tap = [&] (bool raw, bool pre) -> const juce::AudioBuffer<float>& { return raw ? s.rawBuffer : (pre && s.preBuffer.getNumSamples() >= numSamples ? s.preBuffer : s.buffer); };

    auto add = [&] (juce::AudioBuffer<float>& into, float& fade, bool on, const juce::AudioBuffer<float>& source, std::pair<float, float> lvl, float left = 1.0f, float right = 1.0f)
    {
        if (! on && fade <= 0.0f)
            return;
        const auto [from, to] = stepFade (fade, on, numSamples);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float side = ch == 0 ? left : right;
            const float a = from * lvl.first * side, b = to * lvl.second * side;
            if (a == 1.0f && b == 1.0f)
                into.addFrom (ch, 0, source, ch, 0, numSamples);
            else
                into.addFromWithRamp (ch, 0, source.getReadPointer (ch), numSamples, a, b);
        }
    };

    for (size_t b = 0; b < masters.size(); ++b)   // no self-feedback into the output it listens to
    {
        auto& bus = *masters[b];
        // Mix-minus: an input reaching the main output comes here directly too, unless it's the
        // one left out. Buses don't send here then: they'd bring the left-out input back.
        const int main = bus.minusMain.load(), minus = bus.minusSource.load();
        const bool minusFeed = selfBus < 0 && main >= 0 && ! (juce::isPositiveAndBelow (minus, (int) strips.size()) && strips[(size_t) minus].get() == &s)
                               && reachesOutput (s, main, 0);
        const bool sent = s.sends[b] && ! (selfBus >= 0 && main >= 0);
        auto lvl = level (s.sendDb[b], s.sendDbNow[b]);
        const bool pre = sent && selfBus < 0 && s.preSends[b].load();
        const juce::AudioBuffer<float>* source = &tap (sent && s.rawSends[b].load(), pre);
        add (bus.buffer, s.sendFade[b], (sent || minusFeed) && bus.enabled && ! (s.source.kind == Source::loopback && s.source.masterIndex == (int) b),
             sent ? *source : s.buffer, sent ? lvl : std::pair<float, float> { 1.0f, 1.0f }, 1.0f, 1.0f);
    }

    for (size_t p = 0; p < buses.size(); ++p)
    {
        auto& to = *buses[p];
        const bool on = s.busSends[p] && (int) p != selfBus;
        auto lvl = level (s.busSendDb[p], s.busSendDbNow[p]);
        const auto& feed = s.buffer;
        if (! to.combinator || to.paths.empty())
        {
            add (to.busInput, s.busSendFade[p], on, feed, lvl);
            continue;
        }

        // A combinator: into the paths it's sent to (none chosen: the first), one fade for all.
        if (! on && s.busSendFade[p] <= 0.0f)
            continue;
        const auto [from, toLevel] = stepFade (s.busSendFade[p], on, numSamples);
        const float a = from * lvl.first, b = toLevel * lvl.second;
        unsigned mask = s.pathSends[p].load();
        if (mask == 0)
            mask = 1;
        for (size_t k = 0; k < to.paths.size(); ++k)
            if ((mask >> k) & 1u)
                for (int ch = 0; ch < 2; ++ch)
                {
                    if (a == 1.0f && b == 1.0f)
                        to.paths[k]->input.addFrom (ch, 0, feed, ch, 0, numSamples);
                    else
                        to.paths[k]->input.addFromWithRamp (ch, 0, feed.getReadPointer (ch), numSamples, a, b);
                }
    }
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                    float* const* outputs, int numOutputs,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    const juce::ScopedNoDenormals noDenormals;   // (filters and meters decaying into silence stay cheap)

    // Times the whole callback, however it returns (see noteCallbackTime).
    struct Timing
    {
        AudioEngine& engine;
        std::int64_t start;
        int samples;
        ~Timing() { engine.noteCallbackTime (start, samples); }
    } timing { *this, juce::Time::getHighResolutionTicks(), numSamples };

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    const juce::SpinLock::ScopedTryLockType tl (graphLock);

    if (! tl.isLocked() || numSamples > maxBlock || holdAudio.load())
    {
        ++skippedCount;   // this block goes out silent (or the driver's new size waits for reprepareAll)
        missedSamples = juce::jmin (maxBlock, missedSamples + numSamples);   // (and is made up next block)
        return;
    }

    ++blockSerial;   // input devices are read once each in this block (processStrip)

    // A block skipped (the graph was being changed): the Windows outputs and the recordings get
    // its length in silence now, so the outputs don't run dry and the recordings keep time.
    const int missed = std::exchange (missedSamples, 0);
    if (missed > 0)
    {
        recorder.beginBlock();
        recorder.endBlock (missed);
    }
    meterRelease = juce::Decibels::decibelsToGain (-26.0f * (float) (numSamples / interfaceRate));   // sample peaks: fast fall

    for (auto& bus : masters)
        bus->buffer.clear (0, numSamples);

    for (auto& pair : buses)
    {
        pair->busInput.clear (0, numSamples);
        for (auto& path : pair->paths)
            path->input.clear (0, numSamples);
    }

    // Solo in place: while any input is soloed, the others are silenced. Pairs and outputs
    // carry whatever soloed inputs reach them.
    auditionThisBlock = false;
    if (auditionMix.getNumSamples() >= numSamples)
        auditionMix.clear (0, numSamples);

    const bool anySolo = std::any_of (strips.begin(), strips.end(), [] (auto& s) { return s->solo.load(); });
    padsRendered = tapeRendered = false;
    recorder.beginBlock();
    padsElsewhere = std::any_of (strips.begin(), strips.end(), [] (auto& s) { return s->padsInto.load(); });

    for (auto& strip : strips)
    {
        if (! strip->enabled)
            continue;   // off: silent, sends nothing
        processStrip (*strip, inputs, numInputs, numSamples, anySolo && ! strip->solo && ! strip->soloIsolate);
        recorder.offer (Recorder::Kind::input, (int) (&strip - strips.data()), strip->buffer, numSamples);
        sendFrom (*strip, -1, numSamples);
    }
    if (! padsRendered)
        pads.drain (interfaceRate);   // no PADS channel this block: its commands are taken anyway
    if (! tapeRendered)
        tape.drain();              // nor a TAPE one

    // Each pair runs after everything feeding it, so chains of buses add no delay.
    for (int p : busOrder)
    {
        auto& pair = *buses[(size_t) p];
        if (! pair.enabled)
            continue;
        processStrip (pair, inputs, numInputs, numSamples);
        recorder.offer (Recorder::Kind::bus, p, pair.buffer, numSamples);
        sendFrom (pair, p, numSamples);
    }

    monitorMix.clear (0, numSamples);
    bool monitoring = false;
    for (auto& bus : masters)
    {
        if (! bus->enabled)
            continue;

        auto& out = bus->buffer;
        keepFinite (out, numSamples);   // (whatever fed it: the last stop before meters and devices)

        if (! bus->stereo)
        {
            out.addFrom (0, 0, out, 1, 0, numSamples);
            out.applyGain (0, 0, numSamples, 0.5f);
            out.copyFrom (1, 0, out, 0, 0, numSamples);
        }

        // MIX BUS: its shelving EQ and BUS COMP (a fast path when nothing's in), then WIDTH, all
        // before its fader: as the console's compressor shares the main fader's VCAs, riding or
        // fading the master doesn't change how hard it compresses.
        if (auto* mix = bus->masterChain.get())
        {
            juce::AudioBuffer<float> block (out.getArrayOfWritePointers(), 2, numSamples);
            mix->beginBlock (numSamples);
            mix->runStages (block, VoiceChain::stageEq | VoiceChain::stageBus);
            mix->endBlock (block);
        }
        if (const float width = bus->width.load(); bus->stereo && std::abs (width - 1.0f) > 0.001f)
        {
            auto* l = out.getWritePointer (0);
            auto* r = out.getWritePointer (1);
            for (int i = 0; i < numSamples; ++i)
            {
                const float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]) * width;
                l[i] = mid + side;
                r[i] = mid - side;
            }
        }
        out.applyGain (0, numSamples, bus->mute ? 0.0f : bus->gain.load());   // its fader

        // Headphone correction (before LIMIT, which catches what its boosts push over).
        if (auto* eq = bus->headphoneEq.get())
            eq->process (out, numSamples);

        // LIMIT: instant attack, 80 ms release, -1 dBFS ceiling.
        if (bus->safeOn.load())
        {
            constexpr float ceiling = 0.891f;
            const float release = 1.0f - std::exp (-1.0f / (0.08f * (float) interfaceRate));
            float most = 1.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                const float level = juce::jmax (std::abs (out.getSample (0, i)), std::abs (out.getSample (1, i)));
                bus->safeGain += (1.0f - bus->safeGain) * release;
                if (level * bus->safeGain > ceiling)
                    bus->safeGain = ceiling / level;
                most = juce::jmin (most, bus->safeGain);
                out.setSample (0, i, out.getSample (0, i) * bus->safeGain);
                out.setSample (1, i, out.getSample (1, i) * bus->safeGain);
            }
            bus->safeReduction = juce::Decibels::gainToDecibels (most, -60.0f);
        }
        else
        {
            bus->safeGain = 1.0f;
            bus->safeReduction = 0.0f;
        }

        // Its device being changed: down to silence before the switch, back up after (setMasterOutput).
        if (bus->deviceFadeRestart.exchange (false))
            bus->deviceFade = 0.0f;
        if (const auto [from, to] = stepFade (bus->deviceFade, ! bus->deviceFadeOut.load(), numSamples); from < 1.0f || to < 1.0f)
            out.applyGainRamp (0, numSamples, from, to);

        bus->loudness.process (out, numSamples, bus->stereo);

        const float magnitudeL = out.getMagnitude (0, 0, numSamples), magnitudeR = out.getMagnitude (1, 0, numSamples);
        updatePeak (bus->peakL, magnitudeL, meterRelease);
        updatePeak (bus->peakR, magnitudeR, meterRelease);
        if (juce::jmax (magnitudeL, magnitudeR) >= 1.0f)
            bus->clipped = true;   // for ALERTS
        bus->levels.process (out, numSamples, bus->stereo, interfaceRate);
        recorder.offer (Recorder::Kind::output, (int) (&bus - masters.data()), out, numSamples);
        bus->retro.push (out, numSamples);

        if (bus->loopback)
        {
            bus->lastBlock.copyFrom (0, 0, out, 0, 0, numSamples);
            bus->lastBlock.copyFrom (1, 0, out, 1, 0, numSamples);
            bus->lastBlockSamples = numSamples;
        }
        else
            bus->lastBlockSamples = 0;   // (turned on again, it doesn't replay an old block)

        auto playOn = [&] (Secondary* device, bool toInterface, int firstChannel)
        {
            if (device != nullptr)
            {
                // Its own channels on the device: a pair from firstChannel (a mono output on a
                // multichannel device: just the one); past the device's end, its first pair.
                const int count = bus->stereo || device->channels <= 2 ? juce::jmin (2, device->channels) : 1;
                const int first = firstChannel >= 0 && firstChannel + count <= device->channels ? firstChannel : 0;
                if (! device->mixPending)
                    device->mix.clear (0, numSamples);
                for (int ch = 0; ch < count; ++ch)
                    device->mix.addFrom (first + ch, 0, out, ch, 0, numSamples);
                device->mixPending = true;
            }
            else if (toInterface)
            {
                // Mono masters use one master output; stereo masters use a pair.
                for (int ch = 0; ch < (bus->stereo ? 2 : 1); ++ch)
                {
                    const int dest = firstChannel + ch;

                    if (juce::isPositiveAndBelow (dest, numOutputs) && outputs[dest] != nullptr)
                        juce::FloatVectorOperations::add (outputs[dest], out.getReadPointer (ch), numSamples);   // outputs sharing channels add up
                }
            }
        };

        playOn (bus->output, bus->toInterface, bus->firstChannel);
        for (auto& d : bus->alsoOn)
            playOn (d.output, d.toInterface, d.firstChannel);

        // MONITOR: what this output sends (say, into the mic) heard on the master interface's
        // first pair too, where the headphones are, to check it by ear.
        if (bus->monitor.load())
        {
            for (int ch = 0; ch < 2; ++ch)
                monitorMix.addFrom (ch, 0, out, bus->stereo ? ch : 0, 0, numSamples);
            monitoring = true;
        }
    }

    // AUDITION: while a channel is auditioned, the control room hears it alone.
    if (auditionThisBlock)
    {
        for (int ch = 0; ch < 2; ++ch)
            monitorMix.copyFrom (ch, 0, auditionMix, ch, 0, numSamples);
        monitoring = true;
    }

    // The control room: the MONITOR feed onto the master interface's first pair.
    if (monitoring)
    {
        for (int ch = 0; ch < juce::jmin (2, numOutputs); ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::add (outputs[ch], monitorMix.getReadPointer (ch), numSamples);
    }

    // A preview clip (HEAR WHAT THEY HEAR) on the master interface's first pair, once.
    if (auto* clip = preview.get(); clip != nullptr && previewPos < clip->getNumSamples())
    {
        const int n = juce::jmin (numSamples, clip->getNumSamples() - previewPos);
        for (int ch = 0; ch < juce::jmin (2, numOutputs); ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::add (outputs[ch], clip->getReadPointer (juce::jmin (ch, clip->getNumChannels() - 1), previewPos), n);
        previewPos += n;
        if (previewPos >= clip->getNumSamples())
            previewPlaying = false;
    }

    // One push per device, with every output playing there mixed together.
    auto push = [this, numSamples, missed] (Secondary* device)
    {
        if (device != nullptr && device->mixPending)
        {
            if (missed > 0 && device->channels <= silentBlock.getNumChannels())
                device->fifo.push (silentBlock.getArrayOfReadPointers(), device->channels, missed);
            device->fifo.push (device->mix.getArrayOfReadPointers(), device->channels, numSamples);
            device->mixPending = false;
        }
    };
    for (auto& bus : masters)
    {
        push (bus->output);
        for (auto& d : bus->alsoOn)
            push (d.output);
    }

    recorder.endBlock (numSamples);   // silence, if what's being recorded is switched off
}

//==============================================================================
// Callback load: time spent as a share of the time the block lasts. Over 100% the device ran
// dry while we were still working: an audible glitch.
void AudioEngine::noteCallbackTime (std::int64_t startTicks, int numSamples)
{
    const double spent = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
    const double budget = numSamples / juce::jmax (1.0, interfaceRate);
    const float load = (float) (100.0 * spent / budget);

    loadAverage = loadAverage.load() * 0.98f + load * 0.02f;
    if (load > loadPeak.load())
        loadPeak = load;
    if (load > 100.0f)
        ++overloadCount;
}

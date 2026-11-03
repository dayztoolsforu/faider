#include "VoiceChain.h"

namespace
{
    using Params = juce::AudioProcessorValueTreeState;

    float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -120.0f); }
    float gainToDb (float g)  { return juce::Decibels::gainToDecibels (g, -120.0f); }

    // One-pole smoothing coefficient for a time constant in ms.
    float coeff (double rate, double ms) { return (float) (1.0 - std::exp (-1.0 / (juce::jmax (0.01, ms) * 0.001 * rate))); }

    struct Preset { float hpf, gate, compThreshold, ratio, makeup, low, mid, midFreq, high, deess, ceiling; bool gateOn; };
    const Preset presets[] = {
        { 80.0f,  -50.0f, -20.0f, 4.0f, 6.0f,  1.0f, 3.0f, 3500.0f, 2.0f, -30.0f, -1.0f, true  },   // BROADCAST: close, dense, bright
        { 100.0f, -45.0f, -18.0f, 3.0f, 4.0f, -2.0f, 2.0f, 3000.0f, 0.0f, -28.0f, -1.0f, true  },   // CALLS: clear speech, less rumble
        { 70.0f,  -80.0f, -24.0f, 2.0f, 2.0f,  0.0f, 0.0f, 1000.0f, 0.0f, -35.0f, -1.0f, false },   // CLEAN: light touch
    };
}

VoiceChain::VoiceChain()
    : AudioPluginInstance (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo())
                                            .withOutput ("Output", juce::AudioChannelSet::stereo())),
      params (*this, nullptr, "VoiceChain", layout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout VoiceChain::layout()
{
    using namespace juce;
    auto db = [] (const String& id, const String& name, float lo, float hi, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.1f), def,
                                                      AudioParameterFloatAttributes().withLabel ("dB"));
    };
    auto hz = [] (const String& id, const String& name, float lo, float hi, float def)
    {
        NormalisableRange<float> range (lo, hi, 1.0f);
        range.setSkewForCentre (std::sqrt (lo * hi));
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def, AudioParameterFloatAttributes().withLabel ("Hz"));
    };
    auto on = [] (const String& id, const String& name) { return std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, true); };
    // Times: skewed so the short end, where most settings are, gets most of the travel.
    auto ms = [] (const String& id, const String& name, float lo, float hi, float def)
    {
        NormalisableRange<float> range (lo, hi, 0.1f);
        range.setSkewForCentre (std::sqrt (juce::jmax (0.1f, lo) * hi));
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def, AudioParameterFloatAttributes().withLabel ("ms"));
    };

    // The dynamics' times and knee default to what they were when they were fixed, so sessions
    // from before sound the same.
    Params::ParameterLayout l;
    // The input stage, ahead of everything (and of ALL OUT: it isn't a stage that goes in and out).
    l.add (on ("inputOn", "Input stage"),   // (out: trim, polarity, drive and delay bypassed)
           db ("trim", "Trim", -24.0f, 24.0f, 0.0f),   // (PRO prints the console's +/-20 on it; sessions keep their trims)
           std::make_unique<AudioParameterBool> (ParameterID { "polarity", 1 }, "Polarity invert", false),
           ms ("inputDelay", "Input delay", 0.0f, maxInputDelayMs, 0.0f),
           // The stereo module's input (stereo channels): each side's polarity, M/S decoding, and a
           // mono source (STEREO, L to both, R to both, L+R to both).
           std::make_unique<AudioParameterBool> (ParameterID { "polarityL", 1 }, "Polarity left", false),
           std::make_unique<AudioParameterBool> (ParameterID { "polarityR", 1 }, "Polarity right", false),
           std::make_unique<AudioParameterBool> (ParameterID { "msDecode", 1 }, "M/S decode", false),
           std::make_unique<AudioParameterChoice> (ParameterID { "stereoSource", 1 }, "Stereo source", StringArray { "STEREO", "L", "R", "L+R" }, 0),
           // DRIVE: a transformer-style preamp saturation, level-matched (0: clean, the default).
           std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Preamp drive", NormalisableRange<float> (0.0f, 24.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (on ("hpfOn", "HPF"),     hz ("hpf", "HPF frequency", 20.0f, 350.0f, 80.0f),
           std::make_unique<AudioParameterBool> (ParameterID { "hpfSteep", 1 }, "HPF 24 dB/oct", false));   // off: 12 dB/oct
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "lpfOn", 1 }, "LPF", false),
           hz ("lpf", "LPF frequency", 1000.0f, 20000.0f, 20000.0f));
    // SPLIT (the console's): the filters on their own, straight after the input (as they always
    // were here, so it starts on). Off, they belong to the EQ: after it, in and out with its CH key.
    l.add (on ("filterSplit", "Filters split from the EQ"));
    // FLT DYN S-C (the console's): the filters taken out of the sound and put in the dynamics'
    // sidechain instead (de-essing, cymbals filtered out of what keys a drum's gate).
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "filterToKey", 1 }, "Filters to the dynamics sidechain", false));
    l.add (on ("gateOn", "Gate"),   db ("gateThreshold", "Gate threshold", -80.0f, -8.0f, -50.0f),   // (to the console's +10 dBu)
           db ("gateRange", "Gate range", gateSilent, 0.0f, gateSilent),   // how far it closes; all the way down: silent
           ms ("gateAttack", "Gate attack", 0.1f, 50.0f, 1.0f), ms ("gateHold", "Gate hold", 0.0f, 500.0f, 0.0f),
           ms ("gateRelease", "Gate release", 10.0f, 4000.0f, 150.0f),   // (to the console's 4 s)
           db ("gateHysteresis", "Gate hysteresis", 0.0f, 20.0f, 0.0f),   // closes this far under the threshold it opened at
           // EXPANDER: below the threshold a 2:1 downward expander (as the classic console's, whose
           // GATE key makes it the gate) instead of shutting; RANGE still limits how far.
           std::make_unique<AudioParameterBool> (ParameterID { "gateExpand", 1 }, "Gate as expander", false));
    // SIDECHAIN: what the gate's and compressor's detectors hear: another channel (KEY, set by the
    // engine) or their own sound, through a high- and low-pass (all the way out: off), and LISTEN to
    // hear it in place of the channel.
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "keyExternal", 1 }, "Key from another channel", false),
           hz ("keyHpf", "Key high-pass", 20.0f, 2000.0f, 20.0f), hz ("keyLpf", "Key low-pass", 1000.0f, 20000.0f, 20000.0f),
           std::make_unique<AudioParameterBool> (ParameterID { "keyListen", 1 }, "Listen to the key", false));
    l.add (on ("compOn", "Comp"),   db ("compThreshold", "Comp threshold", -40.0f, 0.0f, -20.0f),
           std::make_unique<AudioParameterFloat> (ParameterID { "compRatio", 1 }, "Comp ratio", NormalisableRange<float> (1.0f, compRatioInf, 0.1f, 0.5f), 3.0f),
           db ("makeup", "Makeup", 0.0f, 18.0f, 4.0f),
           ms ("compAttack", "Comp attack", 0.1f, 100.0f, 5.0f), ms ("compRelease", "Comp release", 10.0f, 4000.0f, 120.0f),
           db ("compKnee", "Comp knee", 0.0f, 24.0f, 6.0f),
           std::make_unique<AudioParameterFloat> (ParameterID { "compMix", 1 }, "Comp mix", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
                                                  AudioParameterFloatAttributes().withLabel ("%")),   // under 100: parallel compression
           // AUTO MAKE-UP (the console's channel compressor): the gain lost at the line-up level
           // (-18 dBFS) given back, so threshold and ratio don't change the level there.
           std::make_unique<AudioParameterBool> (ParameterID { "compAutoMakeup", 1 }, "Comp automatic make-up", false),
           // PK: the detector on the peaks (as before, so old sessions sound the same) or, off, on
           // the average (RMS, 25 ms) level, as a console's channel compressor without PK.
           std::make_unique<AudioParameterBool> (ParameterID { "compPeak", 1 }, "Comp peak detection", true));
    // The EQ: a low shelf, two bells and a high shelf. The shelves' corners and the second bell
    // default to how the EQ was before they could be set, so old sessions sound the same.
    auto q = [] (const String& id, const String& name)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.3f, 4.0f, 0.01f, 0.5f), 1.0f,
                                                      AudioParameterFloatAttributes().withLabel ("Q"));
    };
    l.add (on ("eqOn", "EQ"),       db ("lowGain", "Low", -15.0f, 15.0f, 0.0f), hz ("lowFreq", "Low frequency", 30.0f, 500.0f, (float) lowShelfHz),
           db ("midGain", "Mid", -15.0f, 15.0f, 0.0f), hz ("midFreq", "Mid frequency", 60.0f, 8000.0f, 1000.0f), q ("midQ", "Mid Q"),
           db ("mid2Gain", "Mid 2", -15.0f, 15.0f, 0.0f), hz ("mid2Freq", "Mid 2 frequency", 300.0f, 21000.0f, 3000.0f), q ("mid2Q", "Mid 2 Q"),
           db ("highGain", "High", -15.0f, 15.0f, 0.0f), hz ("highFreq", "High frequency", 1500.0f, 16000.0f, (float) highShelfHz),
           // CLASSIC: the EQ and high-pass with a classic British console's curves (off: as before).
           std::make_unique<AudioParameterBool> (ParameterID { "eqClassic", 1 }, "Classic EQ", false),
           // BELL: the low or high band a bell at its frequency instead of a shelf (as the console's).
           std::make_unique<AudioParameterBool> (ParameterID { "lowBell", 1 }, "Low bell", false),
           std::make_unique<AudioParameterBool> (ParameterID { "highBell", 1 }, "High bell", false));
    l.add (on ("deessOn", "De-ess"), db ("deessThreshold", "De-ess threshold", -50.0f, 0.0f, -30.0f),
           hz ("deessFreq", "De-ess frequency", 3000.0f, 10000.0f, 6500.0f));
    l.add (on ("limitOn", "Limiter"), db ("ceiling", "Ceiling", -12.0f, 0.0f, -1.0f),
           ms ("limitRelease", "Limiter release", 5.0f, 1000.0f, 80.0f));

    // Auto-level (starts out): a slow gain riding the voice toward a target level, by at most its
    // range either way, holding still through pauses so it never pumps up the room noise.
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "levelOn", 1 }, "Auto-level", false),
           db ("levelTarget", "Auto-level target", -30.0f, -10.0f, -18.0f), db ("levelRange", "Auto-level range", 0.0f, 24.0f, 12.0f));

    // Noise suppression and voice FX (start out, unlike the stages above in the plugin; a channel
    // strip starts with everything out).
    auto off = [] (const String& id, const String& name) { return std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, false); };
    auto plain = [] (const String& id, const String& name, float lo, float hi, float def, const String& unit)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.01f), def,
                                                      AudioParameterFloatAttributes().withLabel (unit));
    };
    l.add (off ("nrOn", "Noise reduction"), db ("nrDepth", "Noise reduction depth", -40.0f, -3.0f, -18.0f),
           std::make_unique<AudioParameterChoice> (ParameterID { "nrType", 1 }, "Noise reduction type", StringArray { "NEURAL", "SPECTRAL" }, 0));
    l.add (off ("robotOn", "Robot"), hz ("robotFreq", "Robot frequency", 20.0f, 200.0f, 60.0f));

    // BUS COMP: a glue compressor set up as the classic console bus compressor: stepped ratio,
    // attack and release (AUTO: program-dependent), threshold, makeup, mix. After the voice FX.
    l.add (off ("busOn", "Bus comp"), db ("busThreshold", "Bus comp threshold", -40.0f, 0.0f, -10.0f),
           std::make_unique<AudioParameterChoice> (ParameterID { "busRatio", 1 }, "Bus comp ratio", StringArray { "2:1", "4:1", "10:1" }, 1),
           std::make_unique<AudioParameterChoice> (ParameterID { "busAttack", 1 }, "Bus comp attack", StringArray { "0.1", "0.3", "1", "3", "10", "30" }, 4),
           std::make_unique<AudioParameterChoice> (ParameterID { "busRelease", 1 }, "Bus comp release", StringArray { "0.1", "0.3", "0.6", "1.2", "AUTO" }, 4),
           db ("busMakeup", "Bus comp makeup", 0.0f, 20.0f, 0.0f),
           plain ("busMix", "Bus comp mix", 0.0f, 100.0f, 100.0f, "%"));
    l.add (off ("echoOn", "Echo"), plain ("echoTime", "Echo time", 50.0f, 1000.0f, 300.0f, "ms"),
           plain ("echoFeedback", "Echo feedback", 0.0f, 90.0f, 40.0f, "%"), plain ("echoMix", "Echo mix", 0.0f, 100.0f, 35.0f, "%"));
    l.add (off ("radioOn", "Radio"), db ("radioDrive", "Radio drive", 0.0f, 24.0f, 8.0f));
    l.add (off ("pitchOn", "Pitch"), plain ("pitch", "Pitch", -12.0f, 12.0f, -4.0f, "st"));
    return l;
}

void VoiceChain::loadPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) std::size (presets)))
        return;
    const auto& p = presets[index];
    auto set = [this] (const char* id, float value)
    {
        if (auto* param = params.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    set ("hpf", p.hpf);              set ("gateThreshold", p.gate);   set ("gateOn", p.gateOn ? 1.0f : 0.0f);
    set ("compThreshold", p.compThreshold); set ("compRatio", p.ratio); set ("makeup", p.makeup);
    set ("lowGain", p.low);          set ("midGain", p.mid);          set ("midFreq", p.midFreq);
    set ("midQ", 1.0f);              set ("highGain", p.high);        set ("deessThreshold", p.deess);
    set ("ceiling", p.ceiling);
    for (auto* id : { "hpfOn", "compOn", "eqOn", "deessOn", "limitOn" })
        set (id, 1.0f);
}

void VoiceChain::setAllStages (bool in)
{
    for (auto* id : { "hpfOn", "lpfOn", "nrOn", "gateOn", "compOn", "eqOn", "deessOn", "levelOn", "limitOn", "robotOn", "echoOn", "radioOn", "pitchOn", "busOn" })
        if (auto* param = params.getParameter (id))
            param->setValueNotifyingHost (in ? 1.0f : 0.0f);
}

void VoiceChain::fillInPluginDescription (juce::PluginDescription& d) const
{
    d.name = d.descriptiveName = "VOICE CHAIN";
    d.pluginFormatName = "Built-in";
    d.category = "Dynamics";
    d.manufacturerName = "Faider";
    d.version = "1.0";
    d.fileOrIdentifier = identifier;
    d.uniqueId = d.deprecatedUid = 0x50425643;   // "PBVC"
    d.isInstrument = false;
    d.numInputChannels = d.numOutputChannels = 2;
}

bool VoiceChain::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void VoiceChain::prepareToPlay (double sampleRate, int maxBlock)
{
    rate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlock);
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    oversampler->initProcessing ((size_t) preparedBlock);
    driving = false;
    dcIn.fill (0.0f);
    dcOut.fill (0.0f);
    dcCoeff = (float) std::exp (-juce::MathConstants<double>::twoPi * 5.0 / sampleRate);   // 5 Hz
    filtersSet = false;
    inputLine.setSize (2, (int) (sampleRate * maxInputDelayMs * 0.001) + 1);
    inputLine.clear();
    inputPos = 0;
    trimGain = dbToGain (params.getRawParameterValue ("trim")->load());
    lastKeyHp = lastKeyLp = -1.0f;   // (the sidechain's filters set again for this rate)
    for (auto* bank : { &keyHpGate, &keyLpGate, &keyHpComp, &keyLpComp })
        for (auto& f : *bank)
            f.reset();
    for (auto* bank : { &highPass, &highPass2, &lowPass, &low, &mid, &mid2, &high, &sibilance, &splitHigh })
        for (auto& f : *bank)
            f.reset();
    gateEnv = compEnv = deessEnv = compRmsSq = 0.0f;
    gateGain = limitGain = 1.0f;
    busFast = busSlow = 0.0f;
    updateFilters();

    // Voice FX: echo up to a second, pitch shifting over an 80 ms window, radio's band 300 Hz-3 kHz.
    echoLine.setSize (2, (int) (sampleRate * 1.05) + 1);
    echoLine.clear();
    pitchLine.setSize (2, (int) (sampleRate * 0.2));
    pitchLine.clear();
    echoPos = pitchPos = 0;
    robotPhase = pitchTap = 0.0;
    noiseReducer.prepare (sampleRate);
    neuralNoise.prepare();
    nrRunning = -1;
    for (int c = 0; c < 2; ++c)
    {
        radioLow[(size_t) c].setCoefficients (juce::IIRCoefficients::makeHighPass (sampleRate, 300.0, 0.9));
        radioHigh[(size_t) c].setCoefficients (juce::IIRCoefficients::makeLowPass (sampleRate, 3000.0, 0.9));
        radioLow[(size_t) c].reset();
        radioHigh[(size_t) c].reset();
    }
}

void VoiceChain::updateFilters()
{
    auto value = [this] (const char* id) { return params.getRawParameterValue (id)->load(); };
    const std::array<float, 17> now { value ("hpf"), value ("hpfSteep"), value ("lowGain"), value ("lowFreq"),
                                      value ("midGain"), value ("midFreq"), value ("midQ"),
                                      value ("mid2Gain"), value ("mid2Freq"), value ("mid2Q"),
                                      value ("highGain"), value ("highFreq"), value ("lpf"), value ("deessFreq"), value ("eqClassic"),
                                      value ("lowBell"), value ("highBell") };
    auto changed = [&] (std::initializer_list<int> which)
    {
        for (int i : which)
            if (! filtersSet || now[(size_t) i] != lastFilterSettings[(size_t) i])
                return true;
        return false;
    };
    // The top of each frequency kept under Nyquist, so a 44.1 kHz rate can't make a filter unstable.
    const double top = rate * 0.45;
    auto hz = [top] (float f) { return juce::jmin ((double) f, top); };

    using C = juce::IIRCoefficients;
    auto setBoth = [] (std::array<juce::IIRFilter, 2>& f, const C& c) { for (auto& x : f) x.setCoefficients (c); };
    // CLASSIC: the high-pass at 18 dB/oct (a 3rd-order Butterworth: a 2nd-order section with Q 1
    // and a 1st-order one); the mid bell broad when gentle, narrowing as it's boosted (as an
    // inductor EQ's); the shelves with a little overshoot past their corner.
    const bool classic = now[14] > 0.5f;
    if (changed ({ 0, 1, 14 }))
    {
        const bool steep = now[1] > 0.5f;
        if (classic)
        {
            setBoth (highPass, C::makeHighPass (rate, now[0], 1.0));
            const double k = std::tan (juce::MathConstants<double>::pi * now[0] / rate);
            const double b0 = 1.0 / (1.0 + k);
            setBoth (highPass2, C (b0, -b0, 0.0, 1.0, (k - 1.0) / (k + 1.0), 0.0));
        }
        else
        {
            setBoth (highPass, C::makeHighPass (rate, now[0], steep ? hpfSteepQ1 : hpfQ));
            setBoth (highPass2, C::makeHighPass (rate, now[0], hpfSteepQ2));
            if (! steep)
                for (auto& f : highPass2) f.reset();
        }
    }
    const double shelf = classic ? classicShelfQ : shelfQ;
    if (changed ({ 2, 3, 14, 15 }))
        setBoth (low, now[15] > 0.5f ? C::makePeakFilter (rate, hz (now[3]), bellQ, dbToGain (now[2])) : C::makeLowShelf (rate, hz (now[3]), shelf, dbToGain (now[2])));
    if (changed ({ 4, 5, 6, 14 })) setBoth (mid, C::makePeakFilter (rate, hz (now[5]), classic ? classicMidQ (now[4]) : now[6], dbToGain (now[4])));
    if (changed ({ 7, 8, 9 }))     setBoth (mid2, C::makePeakFilter (rate, hz (now[8]), now[9], dbToGain (now[7])));
    if (changed ({ 10, 11, 14, 16 }))
        setBoth (high, now[16] > 0.5f ? C::makePeakFilter (rate, hz (now[11]), bellQ, dbToGain (now[10])) : C::makeHighShelf (rate, hz (now[11]), shelf, dbToGain (now[10])));
    if (changed ({ 12 }))      setBoth (lowPass, C::makeLowPass (rate, hz (now[12]), hpfQ));
    if (changed ({ 13 }))
    {
        setBoth (sibilance, C::makeBandPass (rate, hz (now[13]), 2.0));
        setBoth (splitHigh, C::makeHighPass (rate, hz (now[13]) * 0.7, 0.707));
    }
    // The sidechain's filters (each detector its own, so each runs once a block).
    // (FLT DYN S-C: the channel's own filters are the sidechain's)
    const bool filtersToKey = value ("filterToKey") > 0.5f;
    if (const float khp = filtersToKey ? value ("hpf") : value ("keyHpf"), klp = filtersToKey ? value ("lpf") : value ("keyLpf"); khp != lastKeyHp || klp != lastKeyLp)
    {
        for (auto* f : { &keyHpGate, &keyHpComp }) setBoth (*f, C::makeHighPass (rate, hz (khp), 0.7071));
        for (auto* f : { &keyLpGate, &keyLpComp }) setBoth (*f, C::makeLowPass (rate, hz (klp), 0.7071));
        lastKeyHp = khp;
        lastKeyLp = klp;
    }
    lastFilterSettings = now;
    filtersSet = true;
}

void VoiceChain::processInput (juce::AudioBuffer<float>& buffer) noexcept
{
    const int channels = juce::jmin (2, buffer.getNumChannels()), n = buffer.getNumSamples();

    // TRIM, ramped across the block so turning it doesn't click; POLARITY flips the sign.
    // INPUT out: the stage passes the sound untouched (trim ramped to unity, no drive, no delay),
    // its delay line still kept filled so switching it back in never plays old sound.
    const bool bypassed = params.getRawParameterValue ("inputOn")->load() < 0.5f;
    const float to = bypassed ? 1.0f : dbToGain (params.getRawParameterValue ("trim")->load()) * (params.getRawParameterValue ("polarity")->load() > 0.5f ? -1.0f : 1.0f);
    if (to != trimGain || to != 1.0f)
    {
        for (int c = 0; c < channels; ++c)
            buffer.applyGainRamp (c, 0, n, trimGain, to);
        trimGain = to;
    }

    // THE STEREO MODULE'S INPUT (two channels): ØL / ØR, M/S (mid on the left, side on the right,
    // decoded to left and right), then the mono source.
    if (channels == 2 && ! bypassed && sourceStereo.load())
    {
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        const bool flipL = params.getRawParameterValue ("polarityL")->load() > 0.5f, flipR = params.getRawParameterValue ("polarityR")->load() > 0.5f;
        const bool ms = params.getRawParameterValue ("msDecode")->load() > 0.5f;
        const int source = juce::roundToInt (params.getRawParameterValue ("stereoSource")->load());
        if (flipL || flipR || ms || source != 0)
            for (int i = 0; i < n; ++i)
            {
                float a = flipL ? -l[i] : l[i], b = flipR ? -r[i] : r[i];
                if (ms)
                {
                    const float m = a, s = b;
                    a = (m + s) * 0.7071f;
                    b = (m - s) * 0.7071f;
                }
                if (source == 1)      b = a;
                else if (source == 2) a = b;
                else if (source == 3) a = b = 0.5f * (a + b);
                l[i] = a;
                r[i] = b;
            }
    }

    // DRIVE: the saturation at twice the rate, then the DC blocker. Out (0) the sound passes untouched.
    const float drive = bypassed ? 0.0f : params.getRawParameterValue ("drive")->load();
    const bool drivingNow = drive > 0.05f && oversampler != nullptr && n <= preparedBlock;
    if (drivingNow != driving)
    {
        if (oversampler != nullptr) oversampler->reset();
        dcIn.fill (0.0f);
        dcOut.fill (0.0f);
        driving = drivingNow;
    }
    if (driving)
    {
        const float g = dbToGain (drive);
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) channels, (size_t) n);
        auto up = oversampler->processSamplesUp (block);
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* d = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                d[i] = saturate (d[i], g);
        }
        oversampler->processSamplesDown (block);
        for (int c = 0; c < channels; ++c)
        {
            auto* d = buffer.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                const float y = d[i] - dcIn[(size_t) c] + dcCoeff * dcOut[(size_t) c];
                dcIn[(size_t) c] = d[i];
                dcOut[(size_t) c] = y;
                d[i] = y;
            }
        }
    }

    // DELAY: each sample goes into the line and comes out `delay` samples later. The line is
    // kept filled at 0 too, so turning DELAY up never plays old sound.
    const int length = inputLine.getNumSamples();
    const int delay = bypassed ? 0 : juce::jlimit (0, length - 1, (int) (params.getRawParameterValue ("inputDelay")->load() * 0.001 * rate));
    if (length < 2)
        return;
    for (int i = 0; i < n; ++i)
    {
        const int read = (inputPos - delay + length) % length;
        for (int c = 0; c < channels; ++c)
        {
            auto* d = buffer.getWritePointer (c);
            inputLine.setSample (c, inputPos, d[i]);
            d[i] = inputLine.getSample (c, read);
        }
        inputPos = (inputPos + 1) % length;
    }
}

void VoiceChain::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    beginBlock (buffer.getNumSamples());
    runStages (buffer, allStages);
    endBlock (buffer);
}

void VoiceChain::beginBlock (int numSamples)
{
    updateFilters();
    auto value = [this] (const char* id) { return params.getRawParameterValue (id)->load(); };
    auto flag = [this] (const char* id) { return params.getRawParameterValue (id)->load() > 0.5f; };

    const bool nrOn = flag ("nrOn");
    nrNow = ! nrOn ? -1 : (value ("nrType") < 0.5f && std::abs (rate - 48000.0) < 1.0 ? 0 : 1);
    if (nrNow != nrRunning)
    {
        if (nrNow == 0) neuralNoise.prepare();
        if (nrNow == 1) noiseReducer.prepare (rate);
        nrRunning = nrNow;
        nrVoice = nrNow == 0 ? 0.0f : nrNow == 1 ? -1.0f : -2.0f;
    }
    // The engine reads it on the message thread (applyLatency) and lines the strip up with the
    // others: setLatencySamples tells the processor's listeners, which isn't for the audio thread.
    wantedLatency = nrNow == 0 ? NeuralNoise::latencySamples() : nrNow == 1 ? NoiseReducer::latencySamples() : 0;
    if (! flag ("busOn"))
        busFast = busSlow = 0.0f;      // switched back on, it starts from no reduction

    // The stages' meters and screens collect over the block's runs (endBlock publishes them).
    blockPeaks.fill (0.0f);
    blockFall = dbToGain (-26.0f * (float) (numSamples / rate));
    compMost = deessMost = limitMost = busMost = previewMost = 0.0f;
    blockSamples = juce::jmax (1, numSamples);

    // Auto-level: this block's gain, from the voice's level (a 1.5 s average taken only while it's
    // there: within 25 dB of the target over the last 50 ms): toward the target while it's
    // there, held through pauses, gliding over about 3 s; ramped across the block.
    levelFrom = dbToGain (levelDb);
    const float levelTarget = value ("levelTarget"), levelPresent = dbToGain (levelTarget - 25.0f) * dbToGain (levelTarget - 25.0f);
    if (flag ("levelOn"))
    {
        const float range = value ("levelRange");
        if (levelFast > levelPresent)
            levelDb += (juce::jlimit (-range, range, levelTarget - gainToDb (std::sqrt (levelEnv))) - levelDb) * (float) (1.0 - std::exp (-numSamples / (3.0 * rate)));
        levelDb = juce::jlimit (-range, range, levelDb);
    }
    else
        levelDb = 0.0f;
    levelTo = dbToGain (levelDb);
}

void VoiceChain::endBlock (const juce::AudioBuffer<float>& out)
{
    for (int p = 0; p < pointCount; ++p)
        pointLevel[(size_t) p] = juce::jmax (blockPeaks[(size_t) p], pointLevel[(size_t) p].load() * blockFall);
    pushScope (out);
    const bool gateIn = params.getRawParameterValue ("gateOn")->load() > 0.5f;
    gateOpen = gateIn ? gateGain : 1.0f;
    gatePreviewOpen = gateGain;   // (in or out: what the gate does, or would do)
    compPreview = params.getRawParameterValue ("compOn")->load() > 0.5f ? compMost : previewMost;
    if (nrNow == 0)
        nrVoice = neuralNoise.voice();
    compReduction = compMost;
    deessReduction = deessMost;
    limitReduction = limitMost;
    busReduction = busMost;
    levelGain = levelDb;
}

void VoiceChain::runStages (juce::AudioBuffer<float>& buffer, unsigned stages)
{
    juce::ScopedNoDenormals noDenormals;
    if ((stages & stageInput) != 0)
    {
        processInput (buffer);
        if (inputTap != nullptr && inputTap->getNumSamples() >= buffer.getNumSamples())
            for (int c = 0; c < juce::jmin (2, buffer.getNumChannels(), inputTap->getNumChannels()); ++c)
                inputTap->copyFrom (c, 0, buffer, c, 0, buffer.getNumSamples());
    }

    auto value = [this] (const char* id) { return params.getRawParameterValue (id)->load(); };
    auto flag = [this] (const char* id) { return params.getRawParameterValue (id)->load() > 0.5f; };
    auto in = [stages] (unsigned stage) { return (stages & stage) != 0; };
    const bool filtersToKey = flag ("filterToKey");   // FLT DYN S-C: the filters in the sidechain, out of the sound
    const bool filtersIn = (flag ("filterSplit") || flag ("eqOn")) && ! filtersToKey;   // (not split: in and out with the EQ)
    const bool hpfOn = in (stageFilters) && filtersIn && flag ("hpfOn"), lpfOn = in (stageFilters) && filtersIn && flag ("lpfOn"), hpfSteep = flag ("hpfSteep") || flag ("eqClassic");   // (classic: its 1st-order section)
    const bool gateOn = in (stageGate) && flag ("gateOn"), compOn = in (stageComp) && flag ("compOn"), eqOn = in (stageEq) && flag ("eqOn");
    const bool previewing = previewDetectors.load();
    const bool compPreview = previewing && in (stageComp) && ! flag ("compOn");
    const bool gatePreview = previewing && in (stageGate) && ! flag ("gateOn");
    const bool deessOn = in (stageDeess) && flag ("deessOn"), limitOn = in (stageLimit) && flag ("limitOn");
    const bool robotOn = flag ("robotOn"), echoOn = flag ("echoOn"), radioOn = flag ("radioOn"), pitchOn = flag ("pitchOn");
    const bool fxOn = in (stageFx) && (robotOn || echoOn || radioOn || pitchOn) && echoLine.getNumSamples() > 0;
    const bool levelOn = in (stageLevel) && flag ("levelOn"), busOn = in (stageBus) && flag ("busOn");
    const int nr = in (stageNoise) ? nrNow : -1;
    const float nrDepth = value ("nrDepth");

    // Which meter points this run marks: those after its stages.
    const bool markIn = in (stageInput), markFilters = in (stageFilters), markNoise = in (stageNoise), markGate = in (stageGate),
               markComp = in (stageComp), markEq = in (stageEq), markDeess = in (stageDeess), markLevel = in (stageLevel),
               markFx = in (stageFx), markOut = in (stageLimit);

    if (! (hpfOn || lpfOn || nr >= 0 || gateOn || compOn || eqOn || deessOn || levelOn || limitOn || fxOn || busOn))
    {
        // All out: the audio passes untouched; every point this run covers reads the same.
        const float level = buffer.getMagnitude (0, buffer.getNumSamples());
        const bool marks[] = { markIn, markFilters, markNoise, markGate, markComp, markEq, markDeess, markLevel, markFx, markOut };
        for (int p = 0; p < pointCount; ++p)
            if (marks[p])
                blockPeaks[(size_t) p] = juce::jmax (blockPeaks[(size_t) p], level);
        return;
    }
    const float gateRange = value ("gateRange");
    const float gateThreshold = dbToGain (value ("gateThreshold")), gateFloor = gateRange <= gateSilent ? 0.0f : dbToGain (gateRange);
    const float gateClose = dbToGain (value ("gateThreshold") - value ("gateHysteresis"));
    const int gateHoldSamples = (int) (value ("gateHold") * 0.001 * rate);
    const bool gateExpand = flag ("gateExpand");
    const bool keyLink = keyLinked && keyL != nullptr && keyR != nullptr;
    const bool keyExt = flag ("keyExternal") && keyL != nullptr && keyR != nullptr && ! keyLink, keyHpOn = filtersToKey ? flag ("hpfOn") : value ("keyHpf") > 21.0f,
               keyLpOn = filtersToKey ? flag ("lpfOn") : value ("keyLpf") < 19900.0f;
    const bool keyListen = flag ("keyListen");
    const bool useKey = keyExt || keyLink || keyHpOn || keyLpOn || keyListen;
    const float compThreshold = value ("compThreshold"), knee = value ("compKnee");
    const float slope = value ("compRatio") >= compRatioInf ? -1.0f : 1.0f / value ("compRatio") - 1.0f;   // the top of the range: a limit (inf:1)
    const float makeup = flag ("compAutoMakeup") ? juce::jmin (24.0f, -slope * juce::jmax (0.0f, -18.0f - compThreshold)) : value ("makeup");
    const float compMix = value ("compMix") * 0.01f;
    const float deessThreshold = value ("deessThreshold"), ceiling = dbToGain (value ("ceiling"));

    const float gateFall = coeff (rate, 60.0), gateOpenC = coeff (rate, value ("gateAttack")), gateCloseC = coeff (rate, value ("gateRelease"));
    const float compAttack = coeff (rate, value ("compAttack")), compRelease = coeff (rate, value ("compRelease"));
    const bool compPeak = value ("compPeak") > 0.5f;
    const float compRmsC = coeff (rate, 25.0);
    auto compLevel = [this, compPeak, compRmsC] (float p)   // (PK off: the average level)
    {
        if (compPeak)
            return p;
        compRmsSq += (p * p - compRmsSq) * compRmsC;
        return std::sqrt (compRmsSq);
    };
    const float deessAttack = coeff (rate, 1.0), deessRelease = coeff (rate, 60.0), limitRelease = coeff (rate, value ("limitRelease"));

    const double pitchSemis = value ("pitch"), robotFreq = value ("robotFreq");
    const float radioDrive = dbToGain (value ("radioDrive"));
    const int echoDelay = juce::jlimit (1, juce::jmax (1, echoLine.getNumSamples() - 1), (int) (value ("echoTime") * 0.001 * rate));
    const float echoFeedback = value ("echoFeedback") * 0.01f, echoMix = value ("echoMix") * 0.01f;

    // BUS COMP's settings (stepped, as the classic one).
    const float busThreshold = value ("busThreshold"), busMakeup = value ("busMakeup"), busMix = value ("busMix") * 0.01f;
    const float busRatios[] = { 2.0f, 4.0f, 10.0f }, busAttacks[] = { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f }, busReleases[] = { 100.0f, 300.0f, 600.0f, 1200.0f };
    const float busSlope = 1.0f / busRatios[juce::jlimit (0, 2, (int) value ("busRatio"))] - 1.0f;
    const float busAttack = coeff (rate, busAttacks[juce::jlimit (0, 5, (int) value ("busAttack"))]);
    const int busReleaseIndex = juce::jlimit (0, 4, (int) value ("busRelease"));
    const bool busAuto = busReleaseIndex == 4;
    const float busRelease = coeff (rate, busAuto ? 100.0f : busReleases[busReleaseIndex]);
    const float busSlowAttack = coeff (rate, 300.0), busSlowRelease = coeff (rate, 1200.0);   // AUTO: sustained squeeze lets go slowly

    const int channels = juce::jmin (2, buffer.getNumChannels()), n = buffer.getNumSamples();
    float* data[2] = { buffer.getWritePointer (0), buffer.getWritePointer (channels > 1 ? 1 : 0) };

    const float levelTarget = value ("levelTarget"), levelPresent = dbToGain (levelTarget - 25.0f) * dbToGain (levelTarget - 25.0f);
    const float levelAverage = coeff (rate, 1500.0), levelQuick = coeff (rate, 50.0);

    for (int i = 0; i < n; ++i)
    {
        float x[2] = { data[0][i], channels > 1 ? data[1][i] : data[0][i] };
        auto peak = [&x] { return juce::jmax (std::abs (x[0]), std::abs (x[1])); };
        auto mark = [this, &peak] (bool marking, Point p) { if (marking) blockPeaks[(size_t) p] = juce::jmax (blockPeaks[(size_t) p], peak()); };
        mark (markIn, atInput);

        if (hpfOn)
            for (int c = 0; c < 2; ++c)
            {
                x[c] = highPass[(size_t) c].processSingleSampleRaw (x[c]);
                if (hpfSteep)
                    x[c] = highPass2[(size_t) c].processSingleSampleRaw (x[c]);
            }
        if (lpfOn)
            for (int c = 0; c < 2; ++c) x[c] = lowPass[(size_t) c].processSingleSampleRaw (x[c]);

        mark (markFilters, afterFilters);
        if (nr == 0)
            neuralNoise.process (x[0], x[1], nrDepth);
        else if (nr == 1)
            noiseReducer.process (x[0], x[1], nrDepth);

        mark (markNoise, afterNoise);
        // SIDECHAIN: the key (another channel, or this one's own sound here) through its filters;
        // `heard` keeps it for LISTEN.
        float heard[2] = { x[0], x[1] };
        auto keyed = [&] (std::array<juce::IIRFilter, 2>& hp, std::array<juce::IIRFilter, 2>& lp, bool keep)
        {
            float most = 0.0f;
            for (int c = 0; c < 2; ++c)
            {
                float k = x[c];
                if ((keyExt || keyLink) && i < keyLength)
                {
                    const float e = c == 0 ? keyL[i] : keyR[i];
                    k = keyExt || std::abs (e) > std::abs (k) ? e : k;   // (LINK: the louder)
                }
                if (keyHpOn) k = hp[(size_t) c].processSingleSampleRaw (k);
                if (keyLpOn) k = lp[(size_t) c].processSingleSampleRaw (k);
                if (keep) heard[c] = k;
                most = juce::jmax (most, std::abs (k));
            }
            return most;
        };
        if (gateOn || gatePreview)   // (out: its detector still runs, its LEDs showing what it would do)
        {
            const float level = useKey ? keyed (keyHpGate, keyLpGate, keyListen && gateOn) : peak();
            gateEnv = level > gateEnv ? level : gateEnv + (level - gateEnv) * gateFall;
            // Opens over the threshold; closes only under threshold less HYSTERESIS, and then
            // only after HOLD, so a voice hovering at the threshold doesn't chatter.
            if (gateEnv > gateThreshold || (gateIsOpen && gateEnv > gateClose))
            {
                gateIsOpen = true;
                gateHeld = gateHoldSamples;
            }
            else if (gateHeld > 0)
                --gateHeld;
            else
                gateIsOpen = false;
            // (EXPANDER: 2:1 under the threshold, a dB down for each dB under, as far as RANGE.)
            const float target = gateIsOpen ? 1.0f : gateExpand ? juce::jmax (gateFloor, juce::jmin (1.0f, gateEnv / juce::jmax (1.0e-9f, gateThreshold))) : gateFloor;
            gateGain += (target - gateGain) * (target > gateGain ? gateOpenC : gateCloseC);
            if (target == 0.0f && gateGain < 1.0e-4f)
                gateGain = 0.0f;   // closed all the way (RANGE -INF): true silence, not a long tail toward it
            if (gateOn)
                for (auto& v : x) v *= gateGain;
        }

        mark (markGate, afterGate);
        if (compPreview)   // (out: its detector still runs, so its LEDs show what it would do: set it before it goes in)
        {
            const float level = compLevel (useKey ? keyed (keyHpComp, keyLpComp, false) : peak());
            compEnv += (level - compEnv) * (level > compEnv ? compAttack : compRelease);
            const float over = gainToDb (compEnv) - compThreshold;
            const float reduction = over >= knee * 0.5f ? slope * over : over > -knee * 0.5f ? slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee) : 0.0f;
            previewMost = juce::jmin (previewMost, reduction);
        }
        if (compOn)
        {
            const float level = compLevel (useKey ? keyed (keyHpComp, keyLpComp, keyListen && ! gateOn) : peak());
            compEnv += (level - compEnv) * (level > compEnv ? compAttack : compRelease);
            const float over = gainToDb (compEnv) - compThreshold;
            float reduction = 0.0f;
            if (over >= knee * 0.5f)      reduction = slope * over;
            else if (over > -knee * 0.5f) reduction = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
            compMost = juce::jmin (compMost, reduction);
            // MIX: the compressed voice blended with the voice as it came in (parallel compression).
            const float g = (1.0f - compMix) + compMix * dbToGain (reduction + makeup);
            for (auto& v : x) v *= g;
        }

        mark (markComp, afterComp);
        if (eqOn)
            for (int c = 0; c < 2; ++c)
                x[c] = high[(size_t) c].processSingleSampleRaw (mid2[(size_t) c].processSingleSampleRaw (
                           mid[(size_t) c].processSingleSampleRaw (low[(size_t) c].processSingleSampleRaw (x[c]))));

        mark (markEq, afterEq);
        if (deessOn)
        {
            // The sibilance band sets how far the highs (only) are pulled down.
            const float side = juce::jmax (std::abs (sibilance[0].processSingleSampleRaw (x[0])), std::abs (sibilance[1].processSingleSampleRaw (x[1])));
            deessEnv += (side - deessEnv) * (side > deessEnv ? deessAttack : deessRelease);
            const float over = gainToDb (deessEnv) - deessThreshold;
            const float reduction = over > 0.0f ? juce::jmax (-12.0f, -over * 0.7f) : 0.0f;
            deessMost = juce::jmin (deessMost, reduction);
            const float keep = dbToGain (reduction);
            for (int c = 0; c < 2; ++c)
                x[c] -= splitHigh[(size_t) c].processSingleSampleRaw (x[c]) * (1.0f - keep);
        }

        mark (markDeess, afterDeess);
        if (levelOn)   // measured before its own gain
        {
            const float power = 0.5f * (x[0] * x[0] + x[1] * x[1]);
            levelFast += (power - levelFast) * levelQuick;
            if (levelFast > levelPresent)
                levelEnv = levelEnv > 0.0f ? levelEnv + (power - levelEnv) * levelAverage : levelFast;   // starts from the first sound, not silence
            const float g = levelFrom + (levelTo - levelFrom) * (float) i / (float) blockSamples;
            for (auto& v : x) v *= g;
        }

        mark (markLevel, afterLevel);
        if (fxOn)
        {
            if (pitchOn)
            {
                // Two read heads sweep through a short delay at the pitch ratio, each faded in and
                // out over the window, half a window apart, so the joins don't click.
                const int length = pitchLine.getNumSamples();
                const double ratio = std::pow (2.0, pitchSemis / 12.0);
                pitchTap = std::fmod (pitchTap + (1.0 - ratio) + length, (double) length);
                for (int c = 0; c < 2; ++c)
                    pitchLine.setSample (c, pitchPos, x[c]);
                float out[2] = { 0.0f, 0.0f };
                for (int head = 0; head < 2; ++head)
                {
                    const double d = std::fmod (pitchTap + head * length * 0.5, (double) length);
                    const float fadeIn = (float) std::sin (juce::MathConstants<double>::pi * d / length);
                    const double read = std::fmod (pitchPos - d + length, (double) length);
                    const int r0 = (int) read, r1 = (r0 + 1) % length;
                    const float f = (float) (read - r0);
                    for (int c = 0; c < 2; ++c)
                        out[c] += fadeIn * fadeIn * (pitchLine.getSample (c, r0) * (1.0f - f) + pitchLine.getSample (c, r1) * f);
                }
                pitchPos = (pitchPos + 1) % length;
                x[0] = out[0]; x[1] = out[1];
            }
            if (robotOn)   // ring modulation: the metallic, monotone voice
            {
                const float m = (float) std::sin (robotPhase);
                robotPhase = std::fmod (robotPhase + juce::MathConstants<double>::twoPi * robotFreq / rate, juce::MathConstants<double>::twoPi);
                for (auto& v : x) v *= m * 1.4f;
            }
            if (radioOn)   // a small speaker: band-limited and driven into soft clipping
                for (int c = 0; c < 2; ++c)
                {
                    const float band = radioHigh[(size_t) c].processSingleSampleRaw (radioLow[(size_t) c].processSingleSampleRaw (x[c]));
                    x[c] = std::tanh (band * radioDrive) / std::tanh (juce::jmax (1.0f, radioDrive) * 0.5f);
                }
            if (echoOn)
            {
                const int length = echoLine.getNumSamples();
                const int read = (echoPos - echoDelay + length) % length;
                for (int c = 0; c < 2; ++c)
                {
                    const float delayed = echoLine.getSample (c, read);
                    echoLine.setSample (c, echoPos, x[c] + delayed * echoFeedback);
                    x[c] += delayed * echoMix;
                }
                echoPos = (echoPos + 1) % length;
            }
        }

        mark (markFx, afterFx);
        if (busOn)
        {
            // Stereo-linked, a 6 dB soft knee; AUTO release: a fast envelope for peaks and a slow one
            // that only builds under sustained compression, the deeper of the two applied.
            const float over = gainToDb (peak()) - busThreshold;
            float target = 0.0f;
            if (over >= 3.0f)       target = busSlope * over;
            else if (over > -3.0f)  target = busSlope * (over + 3.0f) * (over + 3.0f) / 12.0f;
            busFast += (target - busFast) * (target < busFast ? busAttack : busRelease);
            if (busAuto)
                busSlow += (target - busSlow) * (target < busSlow ? busSlowAttack : busSlowRelease);
            const float reduction = busAuto ? juce::jmin (busFast, busSlow) : busFast;
            busMost = juce::jmin (busMost, reduction);
            const float g = (1.0f - busMix) + busMix * dbToGain (reduction + busMakeup);
            for (auto& v : x) v *= g;
        }
        if (limitOn)
        {
            const float level = peak();
            limitGain += (1.0f - limitGain) * limitRelease;
            if (level * limitGain > ceiling)
                limitGain = ceiling / level;
            limitMost = juce::jmin (limitMost, gainToDb (limitGain));
            for (auto& v : x) v *= limitGain;
        }

        mark (markOut, atOutput);
        if (keyListen && (gateOn || compOn))   // LISTEN: what the detectors hear, in place of the channel
        {
            x[0] = heard[0];
            x[1] = heard[1];
        }
        data[0][i] = x[0];
        if (channels > 1)
            data[1][i] = x[1];
    }
}

void VoiceChain::pushScope (const juce::AudioBuffer<float>& buffer) noexcept
{
    const int n = buffer.getNumSamples();
    const auto* l = buffer.getReadPointer (0);
    const auto* r = buffer.getReadPointer (buffer.getNumChannels() > 1 ? 1 : 0);
    int pos = scopePos.load();
    for (int i = 0; i < n; ++i)
        scope[(size_t) (pos++ & (scopeSize - 1))] = 0.5f * (l[i] + r[i]);
    scopePos = pos & (scopeSize - 1);
}

void VoiceChain::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = params.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void VoiceChain::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size); xml != nullptr && xml->hasTagName (params.state.getType()))
        params.replaceState (juce::ValueTree::fromXml (*xml));
}

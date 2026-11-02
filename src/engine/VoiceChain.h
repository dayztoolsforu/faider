#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "NoiseReducer.h"
#include "NeuralNoise.h"

// The built-in voice chain: high-pass, gate, compressor, EQ (low shelf, sweepable mid, high shelf), de-esser and
// limiter in one insert, so a mic sounds right without hunting for plugins. It is an ordinary
// plugin to the engine ("builtin:voice" as its path): it sits in an insert slot, saves its state
// with the session and opens its own console-style editor (VoiceChainEditor.cpp).
class VoiceChain : public juce::AudioPluginInstance
{
public:
    static constexpr const char* identifier = "builtin:voice";

    VoiceChain();

    // Presets: BROADCAST, CALLS, CLEAN.
    static juce::StringArray presetNames() { return { "BROADCAST", "CALLS", "CLEAN" }; }

    // Voice FX, each with its own IN key ("robotOn"...): what macros switch (FX ON / OFF / TOGGLE).
    static juce::StringArray fxNames() { return { "ROBOT", "ECHO", "RADIO", "PITCH" }; }
    static juce::String fxSwitch (const juce::String& fx) { return fx.toLowerCase() + "On"; }
    void loadPreset (int index);
    void setAllStages (bool in);

    // SIDECHAIN: another channel's sound for the gate's and compressor's detectors (KEY on), set
    // before each block by the engine (null: none). Audio thread.
    // `linked`: LINK, the key is a linked channel's sound, the louder of the two driving the detectors.
    void setKey (const float* left, const float* right, int length, bool linked = false) noexcept { keyL = left; keyR = right; keyLength = length; keyLinked = linked; }   // every stage's IN key (a channel strip starts with them all out)

    // Noise reduction's frame, as the audio thread last ran it, reported as the chain's latency
    // here: message thread (the engine's refreshLatency), never the audio thread.
    void applyLatency() { if (const int l = wantedLatency.load(); l != getLatencySamples()) setLatencySamples (l); }
    std::atomic<int> wantedLatency { 0 };

    juce::AudioProcessorValueTreeState params;

    // For the editor's screens (dB of gain reduction, 0 or less; gate: 1 open, 0 closed).
    std::atomic<float> gateOpen { 1.0f }, compReduction { 0.0f }, deessReduction { 0.0f }, limitReduction { 0.0f };
    // PRO's previews: the compressor's and gate's detectors run while they're out (previewDetectors
    // on), what they'd do kept apart here so every other view still shows them out.
    std::atomic<bool> previewDetectors { false };
    std::atomic<float> compPreview { 0.0f }, gatePreviewOpen { 1.0f };
    std::atomic<bool> sourceStereo { true };   // (the engine, each block: the stereo module's input keys act only on a stereo source)
    std::atomic<float> busReduction { 0.0f };   // BUS COMP's gain reduction, dB (0 or less)
    std::atomic<float> nrVoice { -2.0f };   // RNNoise's voice estimate, 0 to 1 (-1 while the spectral reducer runs, -2 out)
    std::atomic<float> levelGain { 0.0f };  // auto-level's gain now, dB

    // The level (sample peak, as gain) between stages, for the stages' meters: a stage's input is
    // the point before it, its output the point after. Falls back about 26 dB a second, as the
    // strips' meters do. The audio thread writes; any number of views read.
    enum Point { atInput, afterFilters, afterNoise, afterGate, afterComp, afterEq, afterDeess, afterLevel, afterFx, atOutput, pointCount };
    std::array<std::atomic<float>, pointCount> pointLevel {};

    // The last scopeSize samples of what leaves (L+R), for a spectrum on screen. The audio thread
    // writes; a view copies them out with copyScope (a few torn samples at the seam don't show).
    static constexpr int scopeSize = 2048;
    void copyScope (float* into) const
    {
        const int end = scopePos.load();
        for (int i = 0; i < scopeSize; ++i)
            into[i] = scope[(size_t) ((end + i) & (scopeSize - 1))];
    }

    //==========================================================================
    const juce::String getName() const override { return "VOICE CHAIN"; }
    void fillInPluginDescription (juce::PluginDescription&) const override;
    void prepareToPlay (double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;   // every stage, in order

    // The chain in parts, for the rack (each built-in unit is some of these stages, patched
    // among the plugins). Audio thread, once per block: beginBlock, then runStages any number of
    // times (each stage at most once a block), then endBlock with what leaves. A run does its
    // stages in the chain's own order.
    enum Stage : unsigned
    {
        stageInput = 1u << 0, stageFilters = 1u << 1, stageNoise = 1u << 2, stageGate = 1u << 3, stageComp = 1u << 4,
        stageEq = 1u << 5, stageDeess = 1u << 6, stageLevel = 1u << 7, stageFx = 1u << 8, stageBus = 1u << 9, stageLimit = 1u << 10,
        allStages = (1u << 11) - 1
    };
    void beginBlock (int numSamples);
    // Where the sound just after the input stage (trim, polarity, drive, delay) goes this block, as
    // the console's INPUT pickup (the engine's RAW sends); null: nowhere. Audio thread.
    juce::AudioBuffer<float>* inputTap = nullptr;
    void runStages (juce::AudioBuffer<float>&, unsigned stages);
    void endBlock (const juce::AudioBuffer<float>& out);
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void updateFilters();

    void pushScope (const juce::AudioBuffer<float>&) noexcept;
    std::array<float, scopeSize> scope {};
    std::atomic<int> scopePos { 0 };

    double rate = 48000.0;
    std::array<juce::IIRFilter, 2> highPass, highPass2, lowPass, low, mid, mid2, high, sibilance, splitHigh;   // highPass2: 24 dB/oct's second half

    // Noise reduction (audio thread): RNNoise at 48 kHz, the spectral reducer otherwise or when
    // chosen. Whichever runs is cleared when it comes in, so nothing stale plays out.
    NoiseReducer noiseReducer;
    NeuralNoise neuralNoise;
    int nrRunning = -1;   // 0 neural, 1 spectral, -1 out

    // Voice FX state (audio thread).
    std::array<juce::IIRFilter, 2> radioLow, radioHigh;
    juce::AudioBuffer<float> echoLine, pitchLine;
    int echoPos = 0, pitchPos = 0;
    double robotPhase = 0.0, pitchTap = 0.0;
    float gateEnv = 0.0f, gateGain = 1.0f, compEnv = 0.0f, deessEnv = 0.0f, limitGain = 1.0f;
    float compRmsSq = 0.0f;   // PK off: the compressor detector's average (squared)
    float busFast = 0.0f, busSlow = 0.0f;   // BUS COMP's gain reduction envelopes, dB
    bool gateIsOpen = true;   // the gate's state (HYSTERESIS: it closes lower than it opens)
    int gateHeld = 0;         // samples of HOLD left before it may close
    float levelEnv = 0.0f, levelDb = 0.0f;   // auto-level: slow mean square of the voice, gain (dB)
    float levelFast = 0.0f;                  // auto-level: fast mean square, to tell voice from pauses
    std::array<float, 17> lastFilterSettings {};   // as updateFilters lists them

    // This block, across its runs (beginBlock .. endBlock).
    std::array<float, pointCount> blockPeaks {};
    float blockFall = 1.0f, compMost = 0.0f, deessMost = 0.0f, limitMost = 0.0f, busMost = 0.0f, previewMost = 0.0f;
    float levelFrom = 1.0f, levelTo = 1.0f;   // auto-level's gain across the block
    int blockSamples = 1, nrNow = -1;
    bool filtersSet = false;
public:
    // The EQ bands' fixed corners, shared with the EQ curve on screen.
    static constexpr double lowShelfHz = 100.0, highShelfHz = 10000.0, shelfQ = 0.7;   // the shelves' default corners

    // CLASSIC EQ: the shelves' Q (a little overshoot past the corner), and the mid bell's Q for
    // its gain: broad and gentle near flat, narrowing as it's boosted (cuts stay broader).
    static constexpr double classicShelfQ = 1.1;
    static constexpr double bellQ = 0.9;   // BELL: the low or high band as a broad bell
    static double classicMidQ (float gainDb) { return gainDb >= 0.0f ? 0.7 + 0.13 * gainDb : 0.7 - 0.04 * gainDb; }

    // DRIVE's saturation: tanh of the signal pushed `drive` dB, brought back down by as much (so
    // quiet sound passes level, loud sound bends), with a small offset for the even harmonics a
    // transformer adds.
    static constexpr float driveBias = 0.03f;
    static float saturate (float x, float g) noexcept
    {
        const float t0 = std::tanh (g * driveBias), slope = 1.0f - t0 * t0;   // (its slope at rest: level-matched)
        return (std::tanh (g * (x + driveBias)) - t0) / (g * slope);
    }

    // The HPF's Qs: one biquad at 12 dB/oct; two (a 4th-order Butterworth) at 24.
    static constexpr double hpfQ = 0.7071, hpfSteepQ1 = 0.5412, hpfSteepQ2 = 1.3066;

    // The gate's RANGE turned all the way down: a closed gate is silent.
    static constexpr float gateSilent = -80.0f;

    // The compressor's RATIO turned all the way up: inf:1, nothing passes the threshold.
    static constexpr float compRatioInf = 20.0f;

    // The input stage's DELAY: at most this long. It's a delay asked for (to line up with video),
    // so it isn't reported as latency: the engine mustn't make up for it.
    static constexpr float maxInputDelayMs = 500.0f;

private:
    void processInput (juce::AudioBuffer<float>&) noexcept;   // trim, polarity, delay
    juce::AudioBuffer<float> inputLine;   // the delay's memory
    int inputPos = 0;
    float trimGain = 1.0f;                // where the trim's ramp has got to

    // DRIVE (audio thread): run at twice the rate so its harmonics don't fold back; a DC blocker
    // after (the even harmonics' offset).
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int preparedBlock = 0;
    bool driving = false;
    std::array<float, 2> dcIn {}, dcOut {};
    float dcCoeff = 0.9995f;

    // SIDECHAIN (audio thread): the key, and the detectors' filters (the gate's and the compressor's own).
    const float* keyL = nullptr;
    const float* keyR = nullptr;
    int keyLength = 0;
    bool keyLinked = false;
    std::array<juce::IIRFilter, 2> keyHpGate, keyLpGate, keyHpComp, keyLpComp;
    float lastKeyHp = -1.0f, lastKeyLp = -1.0f;
};

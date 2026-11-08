#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <functional>
#include <map>
#include <unordered_map>
#include "AppCapture.h"
#include "DriftFifo.h"
#include "Macros.h"
#include "Scenes.h"
#include "VoiceChain.h"
#include "Loudness.h"
#include "RemoteControl.h"
#include "MeterProcessor.h"
#include "Pads.h"
#include "Retro.h"
#include "HeadphoneEq.h"
#include "Recorder.h"
#include "Tape.h"
#include "../devices/EndpointWatcher.h"

// The master sets the clock: an ASIO interface, else a Windows playback device, else the
// built-in clock (no device at all). Every other device is a WASAPI "secondary" running on its
// own clock, joined to the master through a DriftFifo.
//
// Signal flow, all in the master callback:
//   strip source -> VST3 inserts -> latency compensation -> gain/mute -> sends -> buses / masters
//   pair (sum of what's sent to it) -> inserts -> gain/mute -> sends -> other buses / masters
//   bus (an output) -> gain/mute -> SAFE -> output device (silent if that device isn't connected)
//
// Pairs (groups) sit between inputs and outputs. They run in dependency order, so a pair feeding
// another pair adds no delay; wiring that would make a loop is refused.
//
// Structure (strips, masters, inserts, devices) changes on the message thread under graphLock;
// the audio thread only try-locks it and outputs one silent block if it's busy. Gain, mute
// and sends are atomics and change without locking. All public methods are message-thread only.
class AudioEngine : private juce::AudioIODeviceCallback
{
public:
    static constexpr int maxMasters = 8;
    static constexpr int maxBuses = 8;
    static inline const juce::String interfaceOutput { "<master>" };

    // As a bus candidate or a strip's deviceName: whatever Windows currently has as its default
    // playback/recording device. Follows changes (including SteamVR switching it to the headset).
    static inline const juce::String windowsDefault { "<default>" };

    // Where the inputs' meters read, as a desk's metering point: 0 IN (the source as read),
    // 1 PRE (after the processing), 2 POST (after the fader, pan and CUT).
    static inline std::atomic<int> meterPoint { 2 };

    struct Secondary;

    // What a strip's or bus's meters show. The audio thread writes, the views only read.
    struct Levels
    {
        std::atomic<float> meterL { 0.0f }, meterR { 0.0f };   // the selected meter standard (Metering.h)
        std::atomic<double> energy { 0.0 };                     // running sum of squares (mean of L and R)...
        std::atomic<std::int64_t> samples { 0 };                // ...over this many samples: for "average" readouts
        MeterProcessor processor;                               // audio thread

        void process (const juce::AudioBuffer<float>& buf, int n, bool stereo, double rate)
        {
            float l = 0.0f, r = 0.0f;
            processor.process (buf.getReadPointer (0), buf.getReadPointer (1), n, stereo, rate, l, r);
            meterL = l;
            meterR = r;

            double e = 0.0;
            for (int ch = 0; ch < (stereo ? 2 : 1); ++ch)
                for (auto* x = buf.getReadPointer (ch), *end = x + n; x < end; ++x)
                    e += (double) *x * *x;
            energy.store (energy.load() + (stereo ? e * 0.5 : e));
            samples.store (samples.load() + n);
        }
    };

    struct Source
    {
        enum Kind { interfaceInputs, device, tone, loopback, none, app, bus, pads, tape };   // append only: saved sessions store the number
        Kind kind = tone;
        int firstChannel = 0;          // interfaceInputs: the channel (mono) or first of the pair (stereo)
        juce::String deviceName;       // device: substring of a WASAPI input name, or windowsDefault. app: exe name
        bool stereo = true;            // mono strips take one channel and pan it across L/R
        int masterIndex = -1;             // bus: loopback of this output bus (one block behind)
    };

    // One of a combinator's parallel paths: its own input (what's sent to this path), plugins and
    // level. Paths are delayed to line up with the slowest one, so a high-latency plugin doesn't
    // smear the sum.
    struct Path
    {
        juce::String name;
        std::atomic<float> gain { 1.0f };
        std::atomic<bool> mute { false };
        std::vector<std::unique_ptr<juce::AudioPluginInstance>> inserts;   // changed under graphLock
        std::atomic<float> peakL { 0.0f }, peakR { 0.0f };   // what it adds to the sum (after its level and mute)
        Levels levels;                                       // its meter on the Mixer

        // Audio-thread state
        juce::AudioBuffer<float> input;   // sum of everything sent to this path this block
        juce::AudioBuffer<float> buffer, pluginBuffer, delayLine;
        juce::MidiBuffer midi;
        int latency = 0, compensation = 0, delayPos = 0;
    };

    static constexpr int maxPaths = 8;


    // One plugin insert's own settings, around the plugin: BYPASS (latency-matched: the sound
    // that passes is delayed as the plugin would delay it, so nothing jumps in time), MIX (the
    // plugin against the sound as it came in), IN and OUT gain; and how much of the time it
    // takes. Kept by the engine per plugin (insertOptions), made when the plugin is.
    struct InsertOptions
    {
        std::atomic<bool> bypassed { false };
        std::atomic<float> mix { 1.0f }, inGain { 1.0f }, outGain { 1.0f };   // mix 0 to 1; gains linear
        std::atomic<float> secondsPerSample { 0.0f };   // its processing time (audio thread, smoothed)
        bool locked = false;   // message thread: pinned in its slot, can't be moved or removed on the desk

        // Audio thread
        juce::AudioBuffer<float> dry, line;   // the sound as it came in, delayed by the plugin's latency
        int pos = 0;
        float blend = 1.0f, in = 1.0f, out = 1.0f;   // where the ramps have got to
    };
    // Message thread: a plugin's options (null if it isn't one the engine made).
    InsertOptions* insertOptions (const juce::AudioPluginInstance* plugin) const
    {
        const auto it = optionsTable.find (plugin);
        return it != optionsTable.end() ? it->second.get() : nullptr;
    }
    using InsertOptionsTable = std::unordered_map<const juce::AudioPluginInstance*, std::unique_ptr<InsertOptions>>;

    // A channel's rack (the SELECTED page's RACK tab): its processing as units in racks (the
    // built-ins', then the plugins'), and how they're patched. Units: the built-in stages
    // (RackBuiltin: parts of the channel's VoiceChain, every stage of it) and its plugins. The
    // patch is all of it, from the channel's input to the fader. Not manual, the
    // cables follow the placement: the built-ins top to bottom, then the plugins. Manual, cables
    // say which unit feeds which: a unit takes the sum of everything patched into it, parallel
    // paths are delayed to line up, and patching that would loop is refused (setRack).
    enum RackBuiltin { rackFilters, rackGate, rackComp, rackEq, rackBus,
                       rackInput, rackNoise, rackDeess, rackLevel, rackFx, rackLimit, rackBuiltins };
    static constexpr int rackIn = 100, rackOut = 101;   // the channel's own jacks: its input (a source), the fader (a destination)
    struct RackUnit
    {
        int builtin = -1;                              // a RackBuiltin, rackIn or rackOut; or -1 for
        juce::AudioPluginInstance* plugin = nullptr;   // one of the channel's plugins (inserts)
        bool operator== (const RackUnit& o) const { return builtin == o.builtin && plugin == o.plugin; }
        bool operator!= (const RackUnit& o) const { return ! (*this == o); }
    };
    struct RackPatch
    {
        std::vector<RackUnit> left, right;                    // placement, top to bottom
        bool manual = false;
        std::vector<std::pair<RackUnit, RackUnit>> cables;    // manual: from (an OUT, or rackIn) to (an IN, or rackOut)
        // The built-in units in the rack (a bit per RackBuiltin): all of them, but those deleted
        // from it. A deleted one is switched out; switched back in (the CHANNEL tab) it runs on
        // the strip, before the rack (or after it: DE-ESSER, AUTO-LEVEL, VOICE FX, LIMITER).
        unsigned inRack = (1u << rackBuiltins) - 1;
    };
    struct RackPlan;   // what the audio thread runs (compiled from the patch)
    struct Strip;
    static juce::String rackUnitName (const RackUnit&);   // FILTERS, EQ... or the plugin's name
    static unsigned rackStages (int builtin);           // a built-in unit's VoiceChain stages
    static constexpr unsigned allBuiltins = (1u << rackBuiltins) - 1;

    // A built-in unit moved from the channel strip into its rack (at the end of the left rack,
    // just before the fader if it's hand-patched), or back out to the strip (a hand-patched rack
    // joins what fed it to what it fed).
    void moveBuiltin (Strip&, int builtin, bool intoRack);

    // Message thread. getRack: placement always holds every built-in unit and plugin (new plugins
    // go at the bottom of the right rack). setRack applies a patch, and puts the plugins in
    // inserts in their placement order; false (nothing changes) if its cables would loop.
    const RackPatch& getRack (Strip&);
    // A channel's processing in the order it really runs (runRack): the built-in stages kept on the
    // strip ahead of the rack, the rack's units (by their cables when hand-patched), then the
    // strip's output stages (DE-ESSER, AUTO-LEVEL, VOICE FX, LIMITER) when they're outside it.
    std::vector<RackUnit> signalOrder (Strip&);
    bool setRack (Strip&, RackPatch);

    // AUDITION (message thread): try changes to a channel's processing in the control room only;
    // the outputs keep the settings it had when it started until COMMIT (or CANCEL puts them back).
    bool startAudition (Strip&);
    void endAudition (Strip&, bool commit);
    static bool rackLoops (const RackPatch&);
    std::vector<std::pair<RackUnit, RackUnit>> rackCables (Strip&);   // what's patched now (the auto cables when not manual)

    // Every output's last `retroSeconds` kept in memory (Retro.h), for HEAR WHAT THEY HEAR.
    // Message thread. retroTail: an output ("output", by name) as it sounded leaving, the newest
    // `seconds` of it (empty if none).
    static constexpr int retroSeconds = 10;

    // Headphone correction on an output (HeadphoneEq.h): an AutoEq profile by name, or empty for none.
    void setHeadphoneEq (int bus, const juce::String& profile);
    juce::AudioBuffer<float> retroTail (const juce::String& kind, const juce::String& name, double seconds) const;

    // A clip played once into the master interface's first pair (the headphones), on top of
    // everything: HEAR WHAT THEY HEAR plays a retro tail with it. Message thread.
    void playPreview (juce::AudioBuffer<float> clip);
    bool isPreviewPlaying() const { return previewPlaying.load(); }

    // A rack changed by setRack (its patch from before): the app keeps it as an undo step.
    std::function<void (Strip&, const RackPatch& before)> onRackChanged;

    struct Strip
    {
        juce::String name;
        Source source;
        std::atomic<float> gain { 1.0f };
        std::atomic<float> pan { 0.0f };   // -1 left .. +1 right (balance when stereo)
        std::atomic<float> width { 1.0f }; // stereo: 0 mono .. 1 as it is .. 2 twice as wide (the sides)
        std::atomic<bool> mute { false };
        std::atomic<bool> solo { false };  // inputs: while any input is soloed, only soloed inputs are heard
        std::atomic<bool> padsInto { false };   // inputs: the pads play into this one (see setPadsTarget)
        std::atomic<bool> enabled { true };     // off: not processed, its device or app released (setStripEnabled)
        std::array<std::atomic<bool>, maxMasters> sends {};
        std::array<std::atomic<bool>, maxBuses> busSends {};   // buses: only change via setBusSend (loop check)

        // Into a combinator: which of its paths the send feeds (a bit per path; none: the first).
        // Set with setPathSend, which keeps busSends in step.
        std::array<std::atomic<unsigned>, maxBuses> pathSends {};

        // Each send's level in dB (0: as it leaves the channel). SENDS ON FADER sets these.
        std::array<std::atomic<float>, maxMasters> sendDb {};
        std::array<std::atomic<float>, maxBuses> busSendDb {};
        std::atomic<float> peakL { 0.0f }, peakR { 0.0f };   // sample peak with a fast fall: clip lights, activity
        std::atomic<float> keyLevel { 0.0f };                // after the fader and ON, whatever the meters read: auto-duck's key
        Levels levels;                                       // the meters

        // RAW sends: to these outputs it sends its sound as read from its source, before its EQ,
        // dynamics, inserts, duck and fader (its mute, solo and power still apply, so
        // muting a mic mutes it everywhere). Set with `sends`; the send itself is sends[b].
        std::array<std::atomic<bool>, maxMasters> rawSends {};

        // PRE: to these outputs it sends its sound before its fader and pan (after its processing;
        // CUT still mutes it), as a desk's pre-fade aux. Set with `sends`.
        std::array<std::atomic<bool>, maxMasters> preSends {};
        juce::AudioBuffer<float> preBuffer;   // audio thread: this block, before the fader

        // LINE B (PRO): the channel's second input; the A/B key swaps it with `source`. Message thread.
        std::optional<Source> altSource;
        bool onLineB = false;   // which line `source` is now (A, or B after a flip)
        std::atomic<bool> soloIsolate { false };   // SOLO ISOLATE: never cut by another channel's solo (an effects return)
        std::atomic<bool> binaural { false };                         // BINAURAL pan (mono channels): the far ear later and shadowed, for headphones/VR
        std::array<float, 128> binauralRing {};                       // (audio thread: its delay line and head-shadow filter)
        int binauralPos = 0;
        float binauralShadow = 0.0f, binauralItd = 0.0f;
        bool binauralWas = false;

        // The source as read, before any processing (the MASTER section's record meters).
        std::atomic<float> sourcePeakL { 0.0f }, sourcePeakR { 0.0f };
        Levels sourceLevels;

        // The channel's own EQ and dynamics (a desk's channel strip, ahead of the inserts): HPF,
        // gate, compressor, EQ, de-esser, limiter. Every stage starts switched out. Shared: a view
        // showing its controls keeps it alive if the strip is removed under it.
        std::shared_ptr<VoiceChain> channel;

        std::vector<std::unique_ptr<juce::AudioPluginInstance>> inserts;
        std::atomic<bool> insertsOn { true };   // the INSERT key: off bypasses every plugin in the chain
        int chainLatency = 0;
        RackPatch rack;                         // message thread (getRack, setRack)
        int rackRevision = 0;                   // message thread: counts setRack's changes (views follow it)
        std::shared_ptr<RackPlan> rackPlan;     // swapped under graphLock
        juce::String rackCompiled;              // message thread: what rackPlan was compiled from (compileRack skips if unchanged)

        // Auto-duck: this strip dips by duckDepth dB while the input named duckBy is heard above
        // duckThreshold dBFS (after its fader, so a muted mic doesn't duck), and comes back over
        // duckRelease ms once it has been quiet for a moment.
        juce::String duckBy;                        // message thread; empty: off

        // SIDECHAIN KEY: the input whose sound this one's gate and compressor hear (its VoiceChain's
        // KEY switch on), by name (message thread; empty: none), found again by refreshDucking
        // (keySource: its index). keyTap: this strip's sound after its processing, before its fader,
        // as other strips' key (audio thread).
        juce::String keyFrom;
        std::atomic<int> keySource { -1 };
        juce::AudioBuffer<float> keyTap;

        // AUDITION: while set, `committed` (a frozen copy of the channel's VoiceChain) is what the
        // outputs get, and the channel's own chain (the one being edited) runs on the side, heard
        // alone in the control room (auditionB: the committed sound there instead, to compare).
        // Swapped under graphLock (startAudition, endAudition). Plugins run on the outputs' path only.
        std::shared_ptr<VoiceChain> committed;
        std::atomic<bool> auditionB { false };
        juce::AudioBuffer<float> auditionBuf;   // audio thread
        std::atomic<int> duckSource { -1 };         // duckBy's index in the inputs (refreshDucking)
        std::atomic<float> duckDepth { -12.0f }, duckThreshold { -40.0f }, duckRelease { 600.0f };
        std::atomic<float> duckGain { 1.0f };       // the dip now (audio thread writes; the DUCK key's lamp reads)
        int duckHold = 0;                           // audio thread: samples before the release starts
        bool locked = false;   // message thread only: pinned in place, can't be moved or removed
        juce::Colour colour;   // message thread only: user's colour; transparent = default

        // Audio-thread state
        Secondary* input = nullptr;
        AppCapture* app = nullptr;
        int compensation = 0;
        juce::AudioBuffer<float> buffer, pluginBuffer, delayLine;
        juce::AudioBuffer<float> rawBuffer;   // this block's source after its input stage (LEVEL, polarity), for RAW sends (the console's INPUT pickup)
        bool tapInput = false;                // (this block: the input stage writes rawBuffer; muted or soloed out, it stays silent)
        juce::AudioBuffer<float> busInput;   // buses: sum of everything sent to it this block
        int delayPos = 0;

        // Cables fading in and out (patching, routing keys): each send's level, 0 to 1 (audio
        // thread), and the strip's whole output while its source changes (fadeOut set: going to
        // silence before the switch; fadeRestart: start again from silence after it).
        std::array<float, maxMasters> sendFade {};
        std::array<float, maxBuses> busSendFade {};
        std::array<float, maxMasters> sendDbNow {};        // the send levels as last applied (ramped to sendDb each block)
        std::array<float, maxBuses> busSendDbNow {};
        float sourceFade = 1.0f;
        std::atomic<bool> sourceFadeOut { false }, sourceFadeRestart { false };
        std::optional<Source> sourceNext;   // message thread: a source picked while the last change still fades out

        // Combinator (a pair): each path takes what's sent to it (pathSends); the paths' sum is
        // its output, before its own inserts and fader. Changed under graphLock.
        bool combinator = false;
        std::vector<std::unique_ptr<Path>> paths;
        juce::AudioBuffer<float> pathSum;     // audio thread
        double tonePhase = 0.0;
        juce::MidiBuffer midi;
    };

    struct Master
    {
        juce::String name;
        juce::StringArray candidates;  // substrings of WASAPI output names, or interfaceOutput
        juce::String target;           // resolved device name, interfaceOutput, or empty
        std::shared_ptr<HeadphoneEq::Processor> headphoneEq;   // headphone correction, or none (swapped under graphLock)
        juce::String headphoneProfile;          // message thread: its AutoEq profile's name, or empty
        std::atomic<float> gain { 1.0f };
        std::atomic<bool> mute { false };
        std::atomic<bool> loopback { false };   // also feed this bus back in as an input source
        std::atomic<bool> monitor { false };    // MONITOR: also heard on the master interface's first pair (not saved)
        RetroBuffer retro;                      // its last few seconds, as it leaves (HEAR WHAT THEY HEAR)
        std::atomic<bool> enabled { true };     // off: not processed, device released (setMasterEnabled)
        std::atomic<float> peakL { 0.0f }, peakR { 0.0f };   // sample peak with a fast fall: clip lights, activity
        Levels levels;                                       // the meters
        juce::Colour colour;                    // message thread only: user's colour; transparent = default
        bool locked = false;                    // message thread only: can't be removed

        // Changed under graphLock (setMasterOutput)
        bool stereo = true;            // mono masters sum L+R into one channel
        int firstChannel = 0;          // which master outputs to use when on the master device

        // More devices it plays on at the same time as its own (addMasterDevice). Changed under graphLock.
        struct Device
        {
            juce::String candidate;        // as in `candidates`
            int firstChannel = 0;
            juce::String target;           // resolved, as `target`
            Secondary* output = nullptr;   // audio thread, as `output`
            bool toInterface = false;
        };
        std::vector<Device> alsoOn;

        // Every device it plays on, its own first (message thread): what the Routing view draws
        // cables to. `target` empty: not connected.
        struct Place { juce::String candidate, target; int firstChannel = 0; };
        std::vector<Place> places() const
        {
            std::vector<Place> p;
            if (! candidates.isEmpty())
                p.push_back ({ candidates[0], target, firstChannel });
            for (auto& d : alsoOn)
                p.push_back ({ d.candidate, d.target, d.firstChannel });
            return p;
        }

        // Mix-minus: this output also takes, straight from each input (before any bus), every
        // input that reaches the output named minusOf, except the input named minusInput: a
        // caller's return feed without their own voice. Buses don't send to it while it's on (they
        // would bring that voice back). Empty minusOf: off. Message thread sets the names;
        // refreshDucking finds them (minusMain, minusSource: their indexes, or -1).
        juce::String minusOf, minusInput;
        std::atomic<int> minusMain { -1 }, minusSource { -1 };

        // Alerts: the tray icon and the event log say when this output has been silent for
        // silenceAlert seconds (0: off) or clips (with SAFE off). Message thread.
        int silenceAlert = 0;
        bool clipAlert = false;
        std::atomic<bool> clipped { false };   // a sample at full scale left since the message thread last looked

        // SAFE: a brickwall limiter after the fader, so nothing leaves this output above -1 dBFS.
        std::atomic<bool> safeOn { false };
        std::atomic<float> safeReduction { 0.0f };   // dB, this block (for the screen)
        float safeGain = 1.0f;                       // audio thread
        Loudness loudness;                           // short-term LUFS of what leaves (audio thread measures)

        // MIX BUS (PRO's centre section): its own shelving EQ and BUS COMP (a VoiceChain running
        // only those stages, every stage out to start), and WIDTH (stereo: 0 mono, 1 as it is, 2
        // twice as wide). After the fader, before headphone correction and SAFE. Saved with it.
        std::shared_ptr<VoiceChain> masterChain;
        std::atomic<float> width { 1.0f };

        // Its device changing (patching it to hardware): what leaves fades to silence before the
        // switch and back up after it, as a strip's source does.
        float deviceFade = 1.0f;
        std::atomic<bool> deviceFadeOut { false }, deviceFadeRestart { false };

        // Audio-thread state
        Secondary* output = nullptr;
        bool toInterface = false;
        juce::AudioBuffer<float> buffer, lastBlock;   // lastBlock: previous output, for loopback
        int lastBlockSamples = 0;
    };

    explicit AudioEngine (std::function<void (const juce::String&)> log);
    void logEvent (const juce::String& text) const { if (log) log (text); }   // a line in the event log
    ~AudioEngine() override;

    // What clocks the engine, as a session's "master" names it: an ASIO driver's name (e.g.
    // "Focusrite USB ASIO"), windowsClockPrefix + a Windows playback device's name, or builtInClock.
    static inline const juce::String windowsClockPrefix { "windows:" };
    static inline const juce::String builtInClock { "<clock>" };
    enum class Clock { none, asio, windows, builtIn };

    // nameContains: e.g. "Focusrite USB", or one of the above. Opens at preferredBufferSize if the
    // device offers it, else its default. Returns false (and logs why) if it can't start.
    bool startInterface (const juce::String& nameContains, int preferredBufferSize = 0);

    // Stops the master and starts another in its place (the Windows device clocking it was
    // unplugged, say): everything is prepared again for its rate and block, the devices refreshed.
    // False (and no master) if it can't start.
    bool switchInterface (const juce::String& name, int preferredBufferSize = 0);

    Clock getClock() const { return clock; }
    bool hasAsio() const { return asioType != nullptr; }   // (built with the ASIO SDK)
    bool isClockOpen() const { return master != nullptr && master->isOpen(); }   // (Windows closes a device that's gone)
    juce::String getClockName() const;   // "ASIO", "Windows" or "Built-in clock"
    juce::String getClockDevice() const { return clockDevice; }   // windows: the playback device's name

    // True if this Windows device is the master's own hardware: the ASIO interface's adapter, or the
    // playback device clocking it. Played through the master, never opened again on its own.
    bool isInterfaceEndpoint (const EndpointInfo&) const;

    // The master's buffer (samples): what the driver offers, what's in use, and changing it
    // (audio stops for a moment while everything is re-prepared).
    juce::Array<int> getInterfaceBufferSizes() const;
    int getInterfaceBufferSize() const { return interfaceBlock; }
    bool setInterfaceBufferSize (int size);

    // The master is there but plays nothing: it wouldn't reopen after a buffer change (ours or
    // its driver's own reset). Cleared once it starts again (another buffer size, or a restart).
    bool isInterfaceDown() const { return interfaceDown.load(); }

    juce::StringArray getInterfaceInputNames() const;
    juce::StringArray getInterfaceOutputNames() const;

    // The Windows adapter (physical device) the master ASIO driver drives, e.g. "Focusrite USB
    // Audio". A Windows default on this adapter plays through the master directly. A Windows
    // device clocking it: that device's name. The built-in clock: empty.
    juce::String getInterfaceAdapter() const { return interfaceAdapter; }
    double getSampleRate() const { return interfaceRate; }

    // The button box (Pads.h): every strip set to Source::pads plays it.
    Pads& getPads() { return pads; }
    const Pads& getPads() const { return pads; }

    // The input strip the pads play into (through its plugins and sends), or -1 for the PADS
    // strip(s). While they play into an input, PADS strips are silent.
    int getPadsTarget() const;
    void setPadsTarget (int stripIndex);

    // The tape deck (Tape.h): every strip set to Source::tape plays it.
    Tape& getTape() { return tape; }
    const Tape& getTape() const { return tape; }

    // The tape deck's recorder (Recorder.h): one output, bus or input (or several, multitrack) to
    // a file in the TAPE tab's format.
    const Recorder& getRecorder() const { return recorder; }
    Recorder& getRecorder() { return recorder; }
    juce::File startRecording (Recorder::Kind k, int index, const juce::String& name, const juce::File& folder, juce::String& error)
    {
        return recorder.start (k, index, name, folder, interfaceRate, graphLock, error);
    }
    void stopRecording() { recorder.stop (graphLock); }
    juce::String stopRecordingIfFull() { return recorder.stopIfFull (graphLock); }   // once a second; what happened, or nothing

    // Multitrack: every input and every output at once, each to its own file in a new folder in
    // `folder` (Recorder::startMultitrack). Runs on through structure changes: a moved channel's
    // track follows it (Recorder::remapLocked), a removed one's records silence from then on, and
    // a channel added since isn't recorded. Ends with stopRecording (or a rate change).
    juce::File startMultitrack (const juce::File& folder, juce::String& error)
    {
        std::vector<Recorder::TrackSource> sources;
        for (int i = 0; i < (int) strips.size(); ++i)   // (every channel)
            sources.push_back ({ Recorder::Kind::input, i, strips[(size_t) i]->name });
        for (int i = 0; i < (int) masters.size(); ++i)
            sources.push_back ({ Recorder::Kind::output, i, masters[(size_t) i]->name });
        return recorder.startMultitrack (sources, folder, interfaceRate, graphLock, error);
    }

    // REC (TAPE's, the top bar's, a macro's): the recorder's chosen source ("multitrack", or
    // "output:NAME", "bus:NAME", "input:NAME") to its folder; none chosen, or it's gone: the first output.
    juce::File startChosenRecording (juce::String& error)
    {
        const auto& chosen = recorder.chosen;
        if (chosen == "multitrack")   // ALL CHANNELS: each to its own file
            return startMultitrack (recorder.folder, error);
        const auto kind = chosen.upToFirstOccurrenceOf (":", false, false), name = chosen.fromFirstOccurrenceOf (":", false, false);
        const auto find = [&name] (const auto& list) { for (int i = 0; i < (int) list.size(); ++i) if (list[(size_t) i]->name == name) return i; return -1; };
        if (const int i = kind == "output" ? find (masters) : -1; i >= 0)
            return startRecording (Recorder::Kind::output, i, name, recorder.folder, error);
        if (const int i = kind == "bus" ? find (buses) : -1; i >= 0)
            return startRecording (Recorder::Kind::bus, i, name, recorder.folder, error);
        if (const int i = kind == "input" ? find (strips) : -1; i >= 0)
            return startRecording (Recorder::Kind::input, i, name, recorder.folder, error);
        if (masters.empty())
        {
            error = "NO OUTPUT";
            return {};
        }
        return startRecording (Recorder::Kind::output, 0, masters.front()->name, recorder.folder, error);
    }
    void armReplay (Recorder::Kind k, int index, double seconds) { recorder.armReplay (k, index, seconds, interfaceRate, graphLock); }

    // Macros (Macros.h): steps that change the mix, fired from pads, hotkeys or the PADS tab.
    Macros& getMacros() { return macros; }
    const Macros& getMacros() const { return macros; }

    // Scenes (Scenes.h): the whole mix saved by name, recalled with a glide.
    Scenes& getScenes() { return scenes; }
    const Scenes& getScenes() const { return scenes; }

    // MIDI controllers and OSC (RemoteControl.h).
    RemoteControl& getRemote() { return remote; }
    const RemoteControl& getRemote() const { return remote; }

    Strip& addStrip (const juce::String& name, const Source&);
    void removeStrip (int index);
    void setStripSource (int index, const Source&);
    void setStripEnabled (Strip&, bool enabled);   // an input or a bus (pair)

    // An output's mix-minus (see Master::minusOf): the output it copies and the input it leaves out
    // (empty `of`: off). Logs it.
    void setMixMinus (Master&, const juce::String& of, const juce::String& without);

    // Points every strip's auto-duck and key at their inputs by name: after strips are added,
    // removed, moved or renamed, or a duck is set (also called every second).
    void refreshDucking();

    // Solo is in place: soloing an input silences the others. MONITOR, the control room: what the
    // outputs with MONITOR on (or an audition) add to the master interface's first pair.

    // The output PRO's centre section treats as the MIX bus (its EQ, WIDTH and BUS COMP there), by
    // name; empty or gone: the first output. Message thread; saved with the session.
    juce::String mainMixMaster;

    // The strips in the tray mixer ("input:NAME", "bus:NAME", "output:NAME"). Message thread.
    juce::StringArray trayStrips;

    // An input, bus or output was renamed (kind "input", "bus" or "output"): everything that names
    // it follows: ducks, app rules, the cue output, the tray mixer, the record source, scenes,
    // macro steps and MIDI mappings, then onRenamed (views keeping names: the mixer's selection). A scene or
    // a macro renamed: macro steps, pads and MIDI mappings follow. Message thread.
    void renamed (const juce::String& kind, const juce::String& oldName, const juce::String& newName);

    // The name `self` (a strip, pair or bus of `kind`: "input", "bus", "output") may take for
    // `wanted`: in capitals, trimmed, and unlike every other of its kind ("MIC" taken: "MIC 2").
    // Empty: its own name, unchanged (scenes and macros find channels by name, so none is blank
    // and no two share one).
    juce::String nameFor (const juce::String& kind, const juce::String& wanted, const void* self) const;
    void sceneRenamed (const juce::String& oldName, const juce::String& newName);
    void macroRenamed (const juce::String& oldName, const juce::String& newName);
    std::function<void (const juce::String& kind, const juce::String& oldName, const juce::String& newName)> onRenamed;

    // The structure changing, for undo (message thread). kind "input", "bus" or "output"; slot -1
    // for the strip itself, else a plugin slot. onAdded once it's there, onRemoving just before it
    // goes. A channel an app rule makes by itself doesn't count.
    std::function<void (const juce::String& kind, int index, int slot)> onAdded, onRemoving;

    // A source or device change made after its fade-out (setStripSource, setMasterOutput): views
    // showing it catch up. Message thread.
    // A channel's source or a master's device switched (after its fade-out, or at once): which one
    // (master false: its index in the strips; true: in the masters), so just that one is shown again.
    std::function<void (bool master, int index)> onRoutingApplied;

    // App rules: "DISCORD goes to CALLS". An app with a rule that starts playing and has no
    // channel yet gets one (an app-capture input named after it, assigned to the rule's bus or
    // output); from then on the channel is the user's like any other. applyAppRules() returns
    // what it added ("DISCORD -> CALLS"); call it every few seconds.
    struct AppRule { juce::String exe, to; };   // exe "Discord.exe"; to "output:CALLS" or "bus:SUMMING"
    std::vector<AppRule> appRules;               // message thread
    juce::StringArray applyAppRules();
    // An app's channel the user is removing (`stripIndex`): its rule goes too, or the rule would
    // bring the channel straight back.
    void forgetAppRule (int stripIndex);

    // Moves a strip to another position. Locked strips keep their positions: the unlocked ones
    // reorder around them. Returns false if either end is locked.
    bool moveStrip (int from, int to);

    Master* addMaster (const juce::String& name, const juce::StringArray& candidates, int firstChannel = 0, bool stereo = true);

    // AUTO-ROUTE: the output devices a channel can be sent to (the interface's pairs and the
    // Windows playback devices that are on), and the master behind one: the one already playing
    // on it, or a new one named after it (masters are made and kept by Faider, not set up by
    // hand). -1: none (all maxMasters in use).
    struct OutputChoice { juce::String label, candidate; int firstChannel = 0; };
    std::vector<OutputChoice> outputChoices() const;
    int masterFor (const OutputChoice&);
    int masterPlayingOn (const juce::String& candidate, int firstChannel) const;
    void removeMaster (int index);
    void setMasterOutput (int index, const juce::StringArray& candidates, int firstChannel, bool stereo);
    void setMasterEnabled (int index, bool enabled);

    // An output plays on any number of devices at once: its own (setMasterOutput) and these. Adding
    // to an output with no device makes it its own; removing its own device hands that role to
    // the next one. `candidate`: a WASAPI output name, interfaceOutput (with firstChannel) or windowsDefault.
    void addMasterDevice (int index, const juce::String& candidate, int firstChannel);
    void removeMasterDevice (int index, const juce::String& candidate, int firstChannel);
    static bool samePlace (const juce::String& candidateA, int firstA, const juce::String& candidateB, int firstB, bool stereo);

    // Output devices the user has hidden from lists and the Routing view (unless in use).
    const juce::StringArray& getHiddenDevices() const { return hiddenDevices; }
    void setDeviceHidden (const juce::String& deviceName, bool hidden);

    // Devices switched off in Faider (Windows still has them): never opened, so they cost
    // nothing, and left out of lists. An output or input set to one stays silent.
    const juce::StringArray& getDisabledDevices() const { return disabledDevices; }
    bool isDeviceDisabled (const juce::String& deviceName) const { return disabledDevices.contains (deviceName); }
    // `sayIt` false leaves the log alone: for a device that is not on the machine now. Its
    // setting is kept, so it is still off when it comes back, but there is nothing to say
    // about a device that is not there.
    void setDeviceDisabled (const juce::StringArray& deviceNames, bool disabled, bool sayIt = true);

    // Pairs: a Strip whose source is everything sent to it (Source::bus).
    Strip* addBus (const juce::String& name);
    void removeBus (int index);
    void setBusStereo (int index, bool stereo);

    // Combinators: a pair whose input is split into parallel paths and summed back (two to start).
    Strip* addCombinator (const juce::String& name, int paths = 2);
    void addPath (int pair, const juce::String& name);
    void removePath (int pair, int path);
    juce::String addPathInsert (int pair, int path, const juce::String& pluginPath);
    void removePathInsert (int pair, int path, int slot);

    // Pair-to-pair send. Returns false (and changes nothing) if it would create a loop.
    bool setBusSend (int fromBus, int toBus, bool on);

    // A send from an input or bus into one path of a combinator, on or off (the send itself,
    // busSends, stays on while it feeds any path). False if refused (a bus loop, no such path).
    bool setPathSend (Strip& from, int toBus, int path, bool on);

    // A feedback loop: an input recording from a virtual cable whose sound reaches an output
    // that plays back into that same cable, so it goes round again (an echo that builds).
    // Describes the first one ("WINDOWS INPUT > MICROPHONES > CABLE INPUT > WINDOWS INPUT"), or empty.
    // `changing` / `newSource`: as if that input had this source (before switching to it).
    juce::String findFeedbackLoop (const Strip* changing = nullptr, const Source* newSource = nullptr) const;
    bool feeds (const Strip& s, int bus) const { return reachesOutput (s, bus, 0); }   // its sound reaches that output (through masters too)

    // What would make sound heard twice or put a call into its own mic: Windows' default speaker
    // being a cable Faider feeds as apps' mic, and an app channel sent to an output you listen
    // to (the app also plays on its own device). One line each; empty if none.
    juce::StringArray findDoubling() const;

    const std::vector<std::unique_ptr<Strip>>& getStrips() const { return strips; }
    const std::vector<std::unique_ptr<Strip>>& getBuses() const  { return buses; }
    const std::vector<std::unique_ptr<Master>>& getMasters() const    { return masters; }

    // pluginPath: a .vst3 bundle. Returns an error message, or empty on success.
    juce::String addInsert (int strip, const juce::String& pluginPath);
    void removeInsert (int strip, int slot);
    juce::String addBusInsert (int pair, const juce::String& pluginPath);
    void removeBusInsert (int pair, int slot);
    // Swaps two plugins in a chain (any strip's or pair's `inserts`, or a combinator path's).
    // Outputs have no plugins: they go on a bus that feeds the output.
    void swapInserts (std::vector<std::unique_ptr<juce::AudioPluginInstance>>& chain, int a, int b);
    juce::StringArray findInstalledPlugins();

    // Reopens/falls back devices after hot-plug; call after any strip/bus change too.
    void updateEndpoints (const juce::Array<EndpointInfo>& endpoints);
    void refreshDevices() { auto copy = lastEndpoints; updateEndpoints (copy); }
    const juce::Array<EndpointInfo>& getEndpoints() const { return lastEndpoints; }

    // Re-reads plugin latencies (they can change at runtime) and realigns all strips.
    void refreshLatency();

    // Starts/stops app captures as apps open, close or restart (new process ID). Call often.
    void refreshApps();

    juce::String getStatusText() const;

    // Diagnostics: what each stream adds in latency, and every dropout counted so far.
    struct StreamStats
    {
        juce::String name, direction;    // direction: "in", "out" or "app"
        double rate = 0.0;               // the stream's own sample rate
        int block = 0;                   // its buffer, samples
        double driverMs = -1.0;          // latency the driver reports (-1: unknown)
        double bufferedMs = 0.0;         // waiting in Faider's drift buffer
        double totalMs = 0.0;            // driver + buffer + one master block
        double driftPpm = 0.0;           // clock correction
        int underruns = 0, overruns = 0, resyncs = 0;
        int xruns = -1;                  // as the driver counts them (-1: it doesn't)
    };

    struct Stats
    {
        float loadPercent = 0.0f, peakLoadPercent = 0.0f;   // master callback time as a share of its buffer
        int overloads = 0;      // callbacks that took longer than their buffer lasts (audible glitch)
        int skippedBlocks = 0;  // blocks sent out silent because the graph was being changed
        std::vector<StreamStats> streams;
    };

    Stats getStats() const;
    void resetStats();   // zero every counter (drops, overloads, peaks)

private:
    void noteCallbackTime (std::int64_t startTicks, int numSamples);   // audio thread

    // onAdded / onRemoving, unless an app rule is making a channel (quietStructure).
    void structureAdded (const juce::String& kind, int index, int slot)    { if (onAdded && quietStructure == 0) onAdded (kind, index, slot); }
    void structureRemoving (const juce::String& kind, int index, int slot) { if (onRemoving && quietStructure == 0) onRemoving (kind, index, slot); }
    int quietStructure = 0;

    std::atomic<float> loadAverage { 0.0f }, loadPeak { 0.0f };
    std::atomic<int> overloadCount { 0 }, skippedCount { 0 };
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

    void processStrip (Strip&, const float* const* inputs, int numInputs, int numSamples, bool soloedOut = false);
    void keyChain (Strip&, VoiceChain&, int numSamples);
    void syncRack (Strip&);      // message thread: placement holds exactly its units
    int retroFrames() const { return retroSeconds * (int) juce::jmax (8000.0, interfaceRate); }
    std::shared_ptr<juce::AudioBuffer<float>> preview;   // swapped under graphLock
    int previewPos = 0;                                   // audio thread
    std::atomic<bool> previewPlaying { false };
    void compileRack (Strip&);   // message thread: the patch, compiled and swapped in (refreshLatency)
    void runRack (Strip&, juce::AudioBuffer<float>&, int numSamples);   // audio thread
    void prepareStrip (Strip&) const;
    std::unique_ptr<juce::AudioPluginInstance> createPlugin (const juce::String& path, juce::String& error);
    juce::String addInsertTo (std::vector<std::unique_ptr<juce::AudioPluginInstance>>&, const juce::String& owner, const juce::String& path);
    void removeInsertFrom (std::vector<std::unique_ptr<juce::AudioPluginInstance>>&, const juce::String& owner, int slot);
    void forgetOptions (const juce::AudioPluginInstance*);   // a removed plugin's insert options freed
    void forgetOptions (const Strip&);                        // every plugin of a removed strip or pair
    void sendFrom (Strip&, int selfBus, int numSamples);
    bool reachesOutput (const Strip&, int bus, int depth) const;

    // Cables fade (so a connection doesn't start or stop with a crack): 500 ms in, 250 ms out.
    // stepFade (audio thread) moves `level` toward on / off over a block and returns its gains at
    // the block's start and end; afterFadeOut (message thread) makes `change` once what's playing
    // has faded out. A strip's source or an output's device changes that way (setStripSource,
    // setMasterOutput), then fades back in.
    static constexpr double fadeInSeconds = 0.5, fadeOutSeconds = 0.25;
    std::pair<float, float> stepFade (float& level, bool on, int numSamples) const;
    void afterFadeOut (std::function<void()> change);

    // While an output's device change waits for its fade-out, later changes to its devices queue
    // behind it, in order (each then sees the output as the one before left it). True if queued.
    bool waitForDeviceFade (Master&, std::function<void (int index)> retry);
    std::map<Master*, std::vector<std::function<void()>>> deviceQueue;   // message thread
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    bool busReaches (int from, int to) const;
    void updateBusOrder();
    void enableLoopbackFor (const Source&);
    Secondary* openSecondary (const juce::String& deviceName, bool isOutput);
    void closeUnused();

    std::function<void (const juce::String&)> log;

    std::unique_ptr<juce::AudioIODeviceType> asioType, wasapiType;
    std::unique_ptr<juce::AudioIODevice> master;
    double interfaceRate = 48000.0;
    int interfaceBlock = 512;

    Pads pads;
    juce::AudioBuffer<float> padsMix;   // this block's pads, rendered once for every strip that uses them
    bool padsRendered = false, padsElsewhere = false;
    void renderPads (int numSamples);

    Recorder recorder;
    Macros macros { *this };

    juce::AudioBuffer<float> monitorMix;   // audio thread: this block's MONITOR feed (outputs with MONITOR on)
    juce::AudioBuffer<float> auditionMix;   // audio thread: this block's auditioned channels, for the control room
    bool auditionThisBlock = false;

    Scenes scenes { *this };
    RemoteControl remote { *this };   // after what it drives, so it goes first

    Tape tape;
    juce::AudioBuffer<float> tapeMix;   // this block's tape, rendered once
    bool tapeRendered = false;
    float meterRelease = 0.0f;   // audio thread: this block's peak-meter fall-off (Metering.h)
    std::uint64_t blockSerial = 0;   // audio thread: counts blocks (an input device is read once per block)
    bool ownStart = false;           // the master started by us (not restarted by its driver)
    std::atomic<bool> holdAudio { false };    // a driver's own rate/buffer change waits for reprepareAll: blocks go out silent
    std::atomic<bool> interfaceDown { false };   // isInterfaceDown
    int missedSamples = 0;           // audio thread: skipped blocks' length, made up as silence next block
    juce::AudioBuffer<float> silentBlock = [] { juce::AudioBuffer<float> b (32, 8192); b.clear(); return b; }();
    void reprepareAll();             // after the master's rate or block changed, with it stopped
    double preparedRate = 0.0;       // the rate the rate-tuned things (loudness, headphone EQ, retro) were last made for

    juce::VST3PluginFormat vst3;
    InsertOptionsTable optionsTable;   // every plugin createPlugin made; changed under graphLock (the audio thread reads it)

    std::vector<std::unique_ptr<Strip>> strips;
    std::vector<std::unique_ptr<Strip>> buses;
    std::vector<int> busOrder;   // processing order: every pair after the buses feeding it (graphLock)
    std::vector<std::unique_ptr<Master>> masters;
    std::map<juce::String, std::unique_ptr<Secondary>> secondaries;   // key: device name + direction
    std::map<juce::String, std::unique_ptr<AppCapture>> appCaptures;  // key: lower-case exe name
    std::map<juce::String, uint32_t> failedApps;                      // exe -> process that refused capture
    juce::Array<EndpointInfo> lastEndpoints;
    juce::String interfaceAdapter;
    Clock clock = Clock::none;
    juce::String clockDevice;   // Clock::windows: the playback device's name
    bool openInterface (const juce::String& name, int preferredBufferSize);   // creates and opens the master, not started
    juce::StringArray hiddenDevices, disabledDevices;

    juce::SpinLock graphLock;
};

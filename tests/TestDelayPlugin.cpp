// Test plugin for Faider's insert chains: delays audio by a fixed 256 samples and reports
// that as its latency, so the host's delay compensation can be checked. One gain parameter.
#include <juce_audio_processors/juce_audio_processors.h>

class TestDelayProcessor : public juce::AudioProcessor
{
public:
    static constexpr int delaySamples = 256;

    TestDelayProcessor()
        : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                           .withOutput ("Out", juce::AudioChannelSet::stereo()))
    {
        addParameter (gain = new juce::AudioParameterFloat ({ "gain", 1 }, "Gain", 0.0f, 2.0f, 1.0f));
        setLatencySamples (delaySamples);
    }

    const juce::String getName() const override { return "Faider Test Delay"; }

    void prepareToPlay (double, int) override
    {
        delay.setSize (getTotalNumInputChannels(), delaySamples);
        delay.clear();
        pos = 0;
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        const auto in = layout.getMainInputChannelSet();
        return in == layout.getMainOutputChannelSet()
            && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int channels = juce::jmin (buffer.getNumChannels(), delay.getNumChannels());
        const float g = gain->get();
        int p = pos;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                const float in = buffer.getSample (ch, i);
                buffer.setSample (ch, i, delay.getSample (ch, p) * g);
                delay.setSample (ch, p, in);
            }

            p = (p + 1) % delaySamples;
        }

        pos = p;
    }

    bool hasEditor() const override                 { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }

    bool acceptsMidi() const override               { return false; }
    bool producesMidi() const override              { return false; }
    double getTailLengthSeconds() const override    { return 0.0; }

    int getNumPrograms() override                   { return 1; }
    int getCurrentProgram() override                { return 0; }
    void setCurrentProgram (int) override           {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& data) override
    {
        juce::MemoryOutputStream (data, false).writeFloat (gain->get());
    }

    void setStateInformation (const void* data, int size) override
    {
        if (size >= (int) sizeof (float))
            *gain = juce::MemoryInputStream (data, (size_t) size, false).readFloat();
    }

private:
    juce::AudioParameterFloat* gain = nullptr;
    juce::AudioBuffer<float> delay;
    int pos = 0;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestDelayProcessor();
}

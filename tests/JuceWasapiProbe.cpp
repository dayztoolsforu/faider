// Diagnostic, not a test: opens playback devices the way AudioEngine::openSecondary does (JUCE's
// WASAPI shared mode: all channels, then 2), and prints what JUCE reports at each step.
#include <juce_audio_devices/juce_audio_devices.h>
#include <iostream>

struct Silence : juce::AudioIODeviceCallback
{
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int n, int samples, const juce::AudioIODeviceCallbackContext&) override
    {
        for (int ch = 0; ch < n; ++ch)
            if (out[ch] != nullptr)
                juce::FloatVectorOperations::clear (out[ch], samples);
    }
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
};

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    std::unique_ptr<juce::AudioIODeviceType> wasapi (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));
    wasapi->scanForDevices();
    for (auto& name : wasapi->getDeviceNames (false))
    {
        if (! name.containsIgnoreCase ("CABLE"))
            continue;
        std::unique_ptr<juce::AudioIODevice> device (wasapi->createDevice (name, {}));
        std::cout << name << "\n  channels listed " << device->getOutputChannelNames().size()
                  << ", rates " << device->getAvailableSampleRates().size() << ", default buffer " << device->getDefaultBufferSize() << "\n";
        for (int count : { device->getOutputChannelNames().size(), 2 })
        {
            juce::BigInteger channels;
            channels.setRange (0, count, true);
            const auto error = device->open ({}, channels, 48000.0, device->getDefaultBufferSize());
            std::cout << "  open " << count << " ch: " << (error.isEmpty() ? juce::String ("OK, active " + juce::String (device->getActiveOutputChannels().countNumberOfSetBits()))
                                                                          : error) << "\n";
            device->close();
        }
    }

    // As in Faider: CABLE Input open and running in this process, then CABLE In 16ch opened.
    Silence silence;
    std::unique_ptr<juce::AudioIODevice> first (wasapi->createDevice ("CABLE Input (VB-Audio Virtual Cable)", {}));
    std::unique_ptr<juce::AudioIODevice> second (wasapi->createDevice ("CABLE In 16ch (VB-Audio Virtual Cable)", {}));
    juce::BigInteger stereo;
    stereo.setRange (0, 2, true);
    if (first != nullptr && second != nullptr)
    {
        std::cout << "together: CABLE Input " << (first->open ({}, stereo, 48000.0, 480).isEmpty() ? "OK" : "FAILED");
        first->start (&silence);
        const auto error = second->open ({}, stereo, 48000.0, 480);
        std::cout << ", then CABLE In 16ch " << (error.isEmpty() ? juce::String ("OK") : error) << "\n";
        second->close();
        first->close();
    }
    return 0;
}

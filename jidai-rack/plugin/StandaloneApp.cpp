// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

// JIDAI RACK standalone. It opens the computer's default audio output and, when there is one, the default input.
// There is no audio settings screen. With no input device the rack runs with silence in.
//
//   "JIDAI RACK" --smoke out.wav    runs one second of the default rack on the default devices, writes the output
//                                   to out.wav and exits 0. With no output device it prints SKIP and still exits 0.

#include "JidaiProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#if JucePlugin_Build_Standalone

namespace {

// Opens the default devices: stereo out, plus the default input when one exists. Returns the input channels opened.
int openDefaultDevices (juce::AudioDeviceManager& devices, juce::String& error)
{
    error = devices.initialiseWithDefaultDevices (2, 2);
    if (error.isEmpty() && devices.getCurrentAudioDevice() != nullptr
        && devices.getCurrentAudioDevice()->getActiveOutputChannels().countNumberOfSetBits() > 0)
        return devices.getCurrentAudioDevice()->getActiveInputChannels().countNumberOfSetBits();
    // No input (or the input would not open with the output): run output only, with silence in.
    error = devices.initialiseWithDefaultDevices (0, 2);
    return 0;
}

bool hasOutput (juce::AudioDeviceManager& devices)
{
    auto* device = devices.getCurrentAudioDevice();
    return device != nullptr && device->getActiveOutputChannels().countNumberOfSetBits() > 0;
}

// Plays the processor and keeps a copy of the first `wanted` output samples.
class Recorder : public juce::AudioIODeviceCallback
{
public:
    explicit Recorder (juce::AudioProcessorPlayer& p) : player (p) {}

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        rate = device->getCurrentSampleRate();
        wanted = (int) rate;                                       // one second
        taken.setSize (2, wanted);
        taken.clear();
        player.audioDeviceAboutToStart (device);
    }
    void audioDeviceStopped() override { player.audioDeviceStopped(); }

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext& context) override
    {
        player.audioDeviceIOCallbackWithContext (in, numIn, out, numOut, n, context);
        const int have = count.load();
        const int copy = juce::jmin (n, wanted - have);
        if (copy <= 0)
            return;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* src = numOut > ch ? out[ch] : (numOut > 0 ? out[0] : nullptr);
            if (src != nullptr)
                taken.copyFrom (ch, have, src, copy);
        }
        count.store (have + copy);
    }

    juce::AudioProcessorPlayer& player;
    juce::AudioBuffer<float> taken;
    std::atomic<int> count { 0 };
    int wanted = 48000;
    double rate = 48000.0;
};

int runSmoke (const juce::File& wav)
{
    juce::AudioDeviceManager devices;
    juce::String error;
    const int inputs = openDefaultDevices (devices, error);
    if (! hasOutput (devices))
    {
        std::printf ("SMOKE SKIP: no audio output device%s\n", error.isNotEmpty() ? (" (" + error + ")").toRawUTF8() : "");
        return 0;
    }
    auto* device = devices.getCurrentAudioDevice();
    std::printf ("SMOKE device: %s, %s, %.0f Hz, %d in, %d out\n", device->getTypeName().toRawUTF8(), device->getName().toRawUTF8(),
                 device->getCurrentSampleRate(), inputs, device->getActiveOutputChannels().countNumberOfSetBits());

    JidaiProcessor processor;               // a fresh instance: the default rack
    juce::AudioProcessorPlayer player;
    player.setProcessor (&processor);
    Recorder recorder (player);
    devices.addAudioCallback (&recorder);
    const auto start = juce::Time::getMillisecondCounter();
    while (recorder.count.load() < recorder.wanted && juce::Time::getMillisecondCounter() - start < 5000)
        juce::Thread::sleep (20);
    devices.removeAudioCallback (&recorder);
    player.setProcessor (nullptr);
    devices.closeAudioDevice();

    const int got = recorder.count.load();
    if (got < recorder.wanted)
    {
        std::printf ("SMOKE FAIL: the device ran %d of %d samples in 5 s\n", got, recorder.wanted);
        return 1;
    }
    wav.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (new juce::FileOutputStream (wav), recorder.rate, 2, 24, {}, 0));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (recorder.taken, 0, got))
    {
        std::printf ("SMOKE FAIL: could not write %s\n", wav.getFullPathName().toRawUTF8());
        return 1;
    }
    writer.reset();
    std::printf ("SMOKE PASS: wrote %s, %d samples, peak %.4f\n", wav.getFullPathName().toRawUTF8(), got, (double) recorder.taken.getMagnitude (0, got));
    return 0;
}

class RackWindow : public juce::DocumentWindow
{
public:
    explicit RackWindow (juce::AudioProcessor& processor)
        : DocumentWindow ("JIDAI RACK", juce::Colour (0xff0d0d0f), DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (processor.createEditor(), true);
        setResizable (true, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

}

class JidaiStandaloneApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "JIDAI RACK"; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        juce::StringArray args;
        args.addTokens (commandLine, true);
        const int smoke = args.indexOf ("--smoke");
        if (smoke >= 0)
        {
            const auto path = args[smoke + 1].unquoted();
            const auto wav = path.isNotEmpty() ? juce::File::getCurrentWorkingDirectory().getChildFile (path)
                                               : juce::File::getCurrentWorkingDirectory().getChildFile ("jidai_rack_smoke.wav");
            setApplicationReturnValue (runSmoke (wav));
            quit();
            return;
        }

        processor = std::make_unique<JidaiProcessor>();
        const auto saved = stateFile();
        if (saved.existsAsFile())
        {
            juce::MemoryBlock block;
            if (saved.loadFileAsData (block))
                processor->setStateInformation (block.getData(), (int) block.getSize());
        }
        juce::String error;
        openDefaultDevices (devices, error);
        player.setProcessor (processor.get());
        devices.addAudioCallback (&player);
        window = std::make_unique<RackWindow> (*processor);
    }

    void shutdown() override
    {
        if (processor != nullptr)
        {
            juce::MemoryBlock block;
            processor->getStateInformation (block);
            stateFile().getParentDirectory().createDirectory();
            stateFile().replaceWithData (block.getData(), block.getSize());
        }
        window.reset();
        devices.removeAudioCallback (&player);
        player.setProcessor (nullptr);
        devices.closeAudioDevice();
        processor.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    static juce::File stateFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("JIDAI RACK").getChildFile ("standalone.rack");
    }

    juce::AudioDeviceManager devices;
    juce::AudioProcessorPlayer player;
    std::unique_ptr<JidaiProcessor> processor;
    std::unique_ptr<RackWindow> window;
};

START_JUCE_APPLICATION (JidaiStandaloneApp)

#endif

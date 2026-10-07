// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// JIDAI RACK: one plugin, one rack graph, BUSHIDO and RONIN compiled in.
// VST3 effect: stereo in, stereo out. Host audio feeds the first RONIN's EXT IN.

#include "core/Rack.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>

class JidaiProcessor : public juce::AudioProcessor,
                       public juce::ChangeBroadcaster
{
public:
    JidaiProcessor();
    ~JidaiProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "JIDAI RACK"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Rack"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- the rack, for the editor (message thread) ----
    jidai::Rack& rack() { return rack_; }
    jidai::Device* addDevice (jidai::DeviceKind kind, int position = -1);    // a new BUSHIDO opens on pattern A001
    void removeDevice (jidai::Device* device);
    void moveDevice (jidai::Device* device, int position);
    void setCables (const std::vector<jidai::CableSpec>& cables);
    void loadRoninProgram (jidai::RoninDevice* ronin, int index);
    void resetToDefaultRack();       // one BUSHIDO above one RONIN
    bool browserOpen = true;         // window only: the device browser is shown, or folded to a thin strip; saved with the rack

    // BUSHIDO patterns: two banks of up to 999. Bank A starts with the factory patterns; saved patterns go to the same
    // user file the BUSHIDO plugin uses, so both see them.
    struct Pattern { juce::String name; std::vector<std::pair<juce::String, float>> params; std::vector<std::array<juce::String, 2>> cables; std::vector<int> colors; };
    static constexpr int kBankSize = 999;
    juce::StringArray patternNames (int bank) const;
    std::pair<int, int> loadedPattern (const jidai::Device* bushido) const;
    void loadPattern (jidai::BushidoDevice* bushido, int bank, int index);
    int savePattern (jidai::BushidoDevice* bushido, int bank, const juce::String& name);

private:
    void writeUserPatterns() const;
    void restoreFromXml (const juce::XmlElement& xml);

    jidai::Rack rack_;
    std::vector<Pattern> banks_[2];
    int factoryCount_ = 0;
    std::map<const jidai::Device*, std::pair<int, int>> loaded_;
    juce::HeapBlock<float> silence_;
    int silenceSize_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JidaiProcessor)
};

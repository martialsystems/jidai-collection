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
    void loadRoninProgram (jidai::RoninDevice* ronin, int index);   // factory program on this RONIN only; the screen shows it on bank A
    void resetToDefaultRack();       // one BUSHIDO above one RONIN
    bool browserOpen = true;         // window only: the device browser is shown, or folded to a thin strip; saved with the rack

    // Both screens: banks A and B, up to 999 entries. Bank A is that instrument's factory set.
    // Bank B starts with the rack patches in rack_patches.json (cleared for now, so bank B holds user entries only).
    // A rack patch sets the device whose screen was used, the first device of the other kind, and the cables on those two.
    // A BUSHIDO pattern sets that BUSHIDO only. A RONIN factory preset sets that RONIN only.
    // Saved BUSHIDO patterns go to the BUSHIDO plugin's user file, after its factory patterns.
    // Saved RONIN presets go to Application Support/RONIN/user_presets.json, after the factory entries.
    struct Pattern { juce::String name; std::vector<std::pair<juce::String, float>> params; std::vector<std::array<juce::String, 2>> cables; std::vector<int> colors; };
    static constexpr int kBankSize = 999;
    juce::StringArray patternNames (int bank) const;
    std::pair<int, int> loadedPattern (const jidai::Device* device) const;
    void loadPattern (jidai::BushidoDevice* bushido, int bank, int index);
    int savePattern (jidai::BushidoDevice* bushido, int bank, const juce::String& name);

    juce::StringArray roninPresetNames (int bank) const;
    void loadRoninPreset (jidai::RoninDevice* ronin, int bank, int index);
    int saveRoninPreset (jidai::RoninDevice* ronin, int bank, const juce::String& name);

    // Tests point user files here before constructing a processor. An empty file is Application Support.
    static void setUserStoreRootForTest (const juce::File& root);
    // Tests swap in a rack patch list (rack_patches.json format) before constructing a processor. Empty is the compiled file.
    static void setRackPatchesForTest (const juce::String& json);

#if JIDAI_PRESET_TEST
    void testRestore (const juce::XmlElement& xml) { restoreFromXml (xml); }
#endif

private:
    struct RackPatch
    {
        juce::String name;
        int preset = 0;
        bool power = true;
        std::vector<std::pair<juce::String, float>> knobs;
        std::vector<std::pair<juce::String, float>> params;
        std::vector<std::array<juce::String, 2>> cables;
    };
    struct RoninStored
    {
        juce::String name;
        int base = 0;
        bool power = true;
        std::vector<std::pair<juce::String, float>> knobs;
        std::vector<std::array<juce::String, 2>> cables;
        std::vector<int> colors;
    };

    void writeUserPatterns() const;
    void writeUserRonin() const;
    void loadRackPatch (jidai::BushidoDevice* bushido, jidai::RoninDevice* ronin, int index);
    static RoninStored roninFromVar (const juce::var&);
    static juce::var roninToVar (const RoninStored&);
    void restoreFromXml (const juce::XmlElement& xml);
    jidai::RoninDevice* firstRonin() const;
    jidai::BushidoDevice* firstBushido() const;

    jidai::Rack rack_;
    std::vector<Pattern> banks_[2];
    std::vector<RoninStored> roninUser_[2];
    std::vector<RackPatch> rackPatches_;
    std::vector<std::pair<juce::String, float>> bushidoDefaults_;
    int factoryCount_ = 0;
    std::map<const jidai::Device*, std::pair<int, int>> loaded_;
    juce::HeapBlock<float> silence_;
    int silenceSize_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JidaiProcessor)
};

// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// JIDAI RACK: one plugin, one rack graph, BUSHIDO, RONIN and ORIGAMI compiled in.
// VST3 effect: stereo in, stereo out, MIDI in (for RACK I/O's MIDI -> CV jacks; JIDAI_RACK_Redesign open question 3).
// RACK I/O is the host connection (JCS R13): host audio, MIDI and transport come out of its jacks, and the host
// hears what is patched into MAIN OUT. The reported latency is the rack's path latency (JCS R11).

#include "core/Rack.h"
#include "jidai/ui/CableEdit.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>

// Threads. The rack's devices and cables change on the message thread only, and the editor is told synchronously, so a
// view never paints a device the rack has dropped (Rack retires devices; they are freed after the view rebuilt).
// A host may call setCurrentProgram / setStateInformation on any thread (the VST3 program parameter, background state
// loads): off the message thread the request is staged (newest wins) and applied on the message thread by an
// AsyncUpdater. getCurrentProgram reports a staged program at once; getStateInformation returns the staged state until
// it is applied (on the message thread it applies it first). The audio thread only try-locks the rack's graph.
class JidaiProcessor : public juce::AudioProcessor,
                       public juce::ChangeBroadcaster,
                       private juce::AsyncUpdater
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
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    // Programs are the starter racks (StarterRacks.h), INIT first. Choosing one replaces the whole rack.
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram_.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- the rack, for the editor (message thread) ----
    jidai::Rack& rack() { return rack_; }
    // A user insert: a new BUSHIDO opens on pattern A001 and takes its new-instance defaults (HOST clock while the
    // host plays); then auto-route (JIDAI_RACK_Redesign 3.6) unless autoRoute is false (Shift held).
    jidai::Device* addDevice (jidai::DeviceKind kind, int position = -1, bool autoRoute = true, bool asEffect = false);
    void removeDevice (jidai::Device* device);
    void moveDevice (jidai::Device* device, int position);
    void setCables (const std::vector<jidai::CableSpec>& cables);
    bool setCableColor (int index, int color);
    // A cable edit made by the user (patch, reroute, remove, colour, bring to front): one undo step named `label`.
    // Colour and z alone never rebuild the graph.
    void editCables (const std::vector<jidai::CableSpec>& next, const std::string& label);
    bool undoCables();               // false when there is nothing to undo or the patch changed some other way since
    bool redoCables();
    const jidai::cable::History<jidai::CableSpec>& cableHistory() const { return cableHistory_; }
    void loadRoninProgram (jidai::RoninDevice* ronin, int index);   // factory program on this RONIN only; the screen shows it on bank A
    void resetToDefaultRack();       // RACK I/O only: the INIT starter rack
    // Message thread: picks up device latency changes (ORIGAMI 2x) and reports the rack's latency to the host.
    // Called after every edit made through the processor and by a 10 Hz timer.
    void refreshLatency();
    // Message thread: frees the devices the rack dropped. RackView::rebuild calls it once its old components are gone.
    void releaseRetiredDevices() { rack_.releaseRetired(); }
    // Message thread: applies a program or state the host staged off the message thread (the AsyncUpdater does this;
    // tests and getStateInformation call it directly). True when something was applied.
    bool applyPendingState();
    bool hasPendingState() const;

    // Window state, saved with the rack (state v3).
    enum CableMode { CablesAll = 0, CablesHidePassThru, CablesSelected, CablesHide };
    bool browserOpen = true;         // the device browser is shown, or folded to a thin strip
    bool showBack = false;           // FRONT / BACK (Tab)
    int cableModeFront = CablesHidePassThru;
    int cableModeBack = CablesAll;
    int scalePercent = 100;
    // Not saved: the colour picked on the cable bar (jidai::cable::kAnyRole or a jcs::Role index). Jacks that cannot
    // take a cable of that colour are dimmed and refuse new cables.
    int cablePalette = jidai::cable::kAnyRole;
    juce::String migrationNotice;    // set when an older rack was migrated ("N cables kept their old S-trig inversion")
    static constexpr int kStateVersion = 3;

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
    void handleAsyncUpdate() override { applyPendingState(); }
    static bool onMessageThread();
    void discardPendingState();
    std::unique_ptr<juce::XmlElement> pendingStateXml() const;     // a copy of the staged state, or null
    void restoreProgram (int index);                               // message thread, keeps the window size
    // Message thread, after the rack was replaced: the editor rebuilds now (synchronous change message), then the
    // dropped devices are freed.
    void rackReplaced();

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
        int format = 1;         // RONIN state format: 1 = before RONIN's redesign (migrated on load), 2 = current
        int triShape = 0;       // format 2: 0 TRIANGLE, 1 PARABOLA
    };
    // RONIN format 1 -> 2 for one RONIN whose knobs and cables are already in the rack; returns the notice text.
    juce::String migrateRoninFormat1 (jidai::RoninDevice* ronin);

    void writeUserPatterns() const;
    void writeUserRonin() const;
    void loadRackPatch (jidai::BushidoDevice* bushido, jidai::RoninDevice* ronin, int index);
    static RoninStored roninFromVar (const juce::var&);
    static juce::var roninToVar (const RoninStored&);
    void restoreFromXml (const juce::XmlElement& xml);
    jidai::RoninDevice* firstRonin() const;
    jidai::BushidoDevice* firstBushido() const;

    jidai::Rack rack_;
    jidai::cable::History<jidai::CableSpec> cableHistory_;
    std::atomic<int> currentProgram_ { 0 };
    // Held by every change to the rack's devices and cables (message thread) and by getStateInformation, so a state
    // saved on another thread is never half a rack. Recursive.
    juce::CriticalSection stateLock_;
    // A request staged off the message thread. Program: (sequence << 32) | (index + 1), 0 = none (one atomic, so the
    // audio thread can stage it without a lock). State: the parsed XML and its sequence, under pendingLock_.
    std::atomic<std::uint32_t> requestSeq_ { 0 };
    std::atomic<std::uint64_t> pendingProgram_ { 0 };
    mutable std::mutex pendingLock_;
    std::unique_ptr<juce::XmlElement> pendingXml_;
    std::uint32_t pendingXmlSeq_ = 0;
    std::vector<Pattern> banks_[2];
    std::vector<RoninStored> roninUser_[2];
    std::vector<RackPatch> rackPatches_;
    std::vector<std::pair<juce::String, float>> bushidoDefaults_;
    int factoryCount_ = 0;
    std::map<const jidai::Device*, std::pair<int, int>> loaded_;
    std::vector<jidai::MidiNote> midiScratch_;
    std::atomic<int> preparedBlock_ { 512 };     // prepareToPlay's block size: processBlock feeds the rack at most this much at once
    struct LatencyTimer : juce::Timer { JidaiProcessor& p; explicit LatencyTimer (JidaiProcessor& o) : p (o) {} void timerCallback() override { p.refreshLatency(); } };
    LatencyTimer latencyTimer_ { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JidaiProcessor)
};

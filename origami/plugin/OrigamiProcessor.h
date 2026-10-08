// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// ORIGAMI standalone effect: stereo in / stereo out, plus an optional sidechain input. Runs origami::OrigamiCore,
// the same core as the rack device. Outside the rack the jacks are normalled (ORIGAMI_Proposal.md 6):
// host in -> IN L/R, OUT L/R -> host out. VC 1-3 and VCA CV then take only their internal sources:
//   - VC n SOURCE = SIDECHAIN reads the sidechain signal (mono sum, audio rate) when the host feeds that bus;
//   - VCA SOURCE = CV reads the sidechain's envelope (peak follower, ATTACK/RELEASE) as the VCA CV, 0..5 V.
// Host floats are volts / 5 (+-1.0 = +-5 V), converted exactly in double.

#include "origami/dsp/OrigamiCore.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

class OrigamiProcessor : public juce::AudioProcessor
{
public:
    OrigamiProcessor();
    ~OrigamiProcessor() override;

    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "ORIGAMI"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "INIT"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState& state() { return apvts_; }
    double value (int p) const;                  // natural units
    void setValue (int p, double v);             // natural units, notifies the host
    void beginGesture (int p);
    void endGesture (int p);
    const origami::OrigamiCore& core() const { return core_; }
    double currentSampleRate() const { return sampleRate_; }

    float uiScale = 1.0f;                        // saved with the state (editor size)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void pullParams (bool force);

    juce::AudioProcessorValueTreeState apvts_;
    std::array<juce::RangedAudioParameter*, origami::kParamCount> params_ {};
    std::array<std::atomic<float>*, origami::kParamCount> raw_ {};
    origami::OrigamiCore core_;
    double sampleRate_ = 48000.0;
    double scFollow_ = 0.0;
};

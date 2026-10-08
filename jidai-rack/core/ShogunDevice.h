// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// SHOGUN in the rack: the SHOGUN engine (engine/shogun.h, compiled from the SHOGUN sources at the pinned commit) as
// one unit of the rack graph, run one sample at a time through Engine::processSample, its per-sample contract for
// the rack (SHOGUN spec v2.2 §13.4).
//
// Jacks: all 153 of SHOGUN's jack table (engine/ports.h), ids SHOGUN#N/SECTION:LABEL (JCS R6), types and R14 roles
// from the table: per drum voice TRIG VEL PITCH DECAY TONE RET OUT ENV, per synth GATE VEL NOTE V/OCT CUTOFF RET OUT
// NOTE OUT, then MOD (LD/BS GATE, LFO 1-4, RND, LANE A), CLOCK (CLK/RST/RUN/FILL IN, CLK/RST/RUN/ACC OUT), MIX L/R
// and the WAVE / FOLD VC inputs of the five WAVE voices. Gates are plain 0/5 V (SHOGUN kPlainVoltGates).
//
// Latency (JCS R11): the engine's oversampled domain (GLOBAL:OS 1x/2x/4x) delays every AUDIO output by 0/23/26
// samples; CV and gate outputs are at zero latency. The device reports the latency of its audio outputs and the rack
// compensates every path into MAIN OUT. GLOBAL:OS needs the engine re-prepared (it allocates), so the rack applies a
// change on the message thread (Rack::reprepare) and rebuilds the compensation.
//
// Clock: SHOGUN's own CLOCK:MODE (HOST / INT / EXT). The rack hands every block's host transport to the engine.
// State: the SHOGUN patch (params, mod matrix, CV AMT, input laws, pattern) is saved by the rack (JidaiProcessor),
// in the same JSON form as the SHOGUN plugin's state (§14), so a patch moves between the two. SHOGUN's own internal
// bay (ROUTE tab cables) is not used in the rack: rack cables replace it.

#include "Device.h"

#include "shogun.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>

namespace jidai {

class ShogunDevice : public Device {
public:
    static constexpr int kJackCount = shogun::kPorts;    // 153

    ShogunDevice();
    ~ShogunDevice() override;
    DeviceKind kind() const override { return DeviceKind::Shogun; }

    void prepare (double sampleRate) override;
    void beginBlock() override;
    void setTransport (const Transport& t) override;
    int latencySamples() const override { return latency_.load(); }
    std::vector<JackGroup> jackGroups() const override;
    jidai::jcs::Role jackRole (int jack) const override;

    // Parameters, u in [0,1] by SHOGUN id (engine/params_table.h). Any thread; applied at the next block (smoothed).
    double param (int id) const;
    void setParam (int id, double u);
    int paramIndex (const std::string& id) const { return shogun::findParam (id.c_str()); }

    // GLOBAL:OS: 1, 2 or 4. The engine runs at that factor after the next prepare (Rack::reprepare).
    int wantedOs() const;
    int osFactor() const { return os_.load(); }
    bool needsPrepare() const override { return wantedOs() != osFactor(); }

    // Pattern, mod matrix rows, CV AMT and input laws: edited on the message thread in a copy, picked up by the audio
    // thread at the next block (try-lock; it never waits).
    struct Edits
    {
        shogun::Pattern pattern;
        std::array<shogun::mod::Row, shogun::mod::kRows> rows {};
        std::array<double, shogun::kPorts> cvAmt {};
        std::array<std::uint8_t, shogun::kPorts> inLaw {};
    };
    Edits edits() const;
    void setEdits (const Edits& e);
    void loadInit();                       // INIT kit (noon defaults) and the empty pattern
    // The factory program last loaded (0 = INIT, 1..21 = the SHOGUN factory kits), shown on the KIT display and
    // saved with the device like the SHOGUN plugin's "program". Message thread only.
    int program() const { return program_; }
    void setProgram (int p) { program_ = p; }

    void requestRun (bool run) { runRequest_.store (run ? 1 : 0); }
    bool runRequested() const { return runRequest_.load() > 0; }   // pending, not yet taken by the audio thread
    void requestRestart() { restartRequest_.store (true); }
    void trigger (int voice) { padMask_.fetch_or (1u << (unsigned) voice); }
    bool running() const { return running_.load(); }     // the sequencer plays (RUN, or the host transport on HOST)
    int step() const { return step_.load(); }
    long globalStep() const { return globalStep_.load(); }
    float meter (int channel) const { return meters_[(size_t) (channel & 1)].load(); }
    float voiceMeter (int voice) const { return voiceMeters_[(size_t) voice].load(); }

    float jackVolts (int jack) const;
    shogun::Engine& engine() { return engine_; }        // tests, offline

private:
    int program_ = 0;
    class EngineUnit;
    shogun::Engine engine_;
    std::unique_ptr<EngineUnit> unit_;
    std::array<std::atomic<double>, shogun::kParamCount> params_ {};
    std::array<double, shogun::kParamCount> applied_ {};
    std::atomic<bool> dirty_ { true };
    std::atomic<int> os_ { 2 }, latency_ { 23 };
    double sampleRate_ = 48000.0;

    mutable std::mutex editLock_;
    Edits edits_;
    std::atomic<std::uint32_t> editEpoch_ { 1 };
    std::uint32_t appliedEpoch_ = 0;

    std::atomic<int> runRequest_ { -1 };
    std::atomic<bool> restartRequest_ { false };
    std::atomic<std::uint32_t> padMask_ { 0 };
    std::atomic<bool> running_ { false };
    bool hostPlaying_ = false;     // audio thread: the last host transport was valid and playing
    std::atomic<int> step_ { 1 };
    std::atomic<long> globalStep_ { 0 };
    std::array<std::atomic<float>, 2> meters_ {};
    std::array<std::atomic<float>, shogun::kVoices> voiceMeters_ {};
};

} // namespace jidai

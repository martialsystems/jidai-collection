// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// RACK I/O (JCS R13, JIDAI_RACK_Redesign 3.7): the rack's one explicit host connection, 1 U, always at the top.
// It replaces the hidden sums the v2 rack did in Rack::process.
//   HOST:IN L, HOST:IN R          AUDIO out   host input, volts = host x 5
//   MAIN:OUT L, MAIN:OUT R        AUDIO in    summed, to the host (volts / 5, times MAIN level). OUT R <- OUT L
//                                             while OUT R is unpatched. The rack adds the R11 compensation delays
//                                             on these cables.
//   MIDI:NOTE                     V/OCT out   last note held, (note - 48) / 12 V, 0 V = C3 (JCS R4), +-5 V rail
//   MIDI:HZ/V LIN                 HZ/V LIN    2^((note - 48) / 12) V: 1 V = C3 (130.81 Hz), for RONIN's linear input
//   MIDI:GATE                     GATE out    0/5 V while any note is held (JCS R2)
//   MIDI:VEL                      CV out      velocity, 0..5 V
//   TRANSPORT:CLK 1/16            GATE out    one 1 ms pulse per host 16th note while playing (JCS R5.1: label rate)
//   TRANSPORT:RUN                 GATE out    5 V while the host plays
//   TRANSPORT:RESET               GATE out    one 1 ms pulse when the host starts
// Two graph units, so HOST IN -> any device -> MAIN OUT is never a loop.

#include "Device.h"

#include <array>
#include <atomic>
#include <memory>

namespace jidai {

struct MidiNote {
    int sample = 0;      // offset in the block
    int note = 60;
    int velocity = 100;  // 1..127; note-off ignores it
    bool on = true;
};

class RackIODevice : public Device {
public:
    enum Jack { HostInL, HostInR, MainOutL, MainOutR, MidiNoteJack, MidiHzv, MidiGate, MidiVel, Clk16, Run, Reset, kJackCount };

    RackIODevice();
    ~RackIODevice() override;
    DeviceKind kind() const override { return DeviceKind::RackIO; }
    void prepare (double sampleRate) override;
    void setTransport (const Transport& t) override;
    std::vector<JackGroup> jackGroups() const override;
    jidai::jcs::Role jackRole (int jack) const override;

    // Audio thread, from Rack::process.
    void setBlockMidi (const MidiNote* events, int count);
    void setHostSample (int sampleInBlock, float leftVolts, float rightVolts);
    float mainLeftVolts() const;
    float mainRightVolts() const;

    // MAIN level (0..2, 1 = unity) and the meters (peak host units), any thread.
    float mainLevel() const { return level_.load(); }
    void setMainLevel (float v) { level_.store (v < 0.0f ? 0.0f : (v > 2.0f ? 2.0f : v)); }
    float meter (int which) const { return meters_[(size_t) (which & 3)].load (std::memory_order_relaxed); }   // in L, in R, out L, out R
    void updateMeters (float inL, float inR, float outL, float outR, int numSamples);
    const Transport& transport() const { return transport_; }      // audio thread
    // The transport as last seen, for the window (any thread).
    struct TransportView { bool valid = false, playing = false; double bpm = 120.0, ppq = 0.0; };
    TransportView transportView() const
    {
        TransportView v;
        v.valid = viewValid_.load (std::memory_order_relaxed);
        v.playing = viewPlaying_.load (std::memory_order_relaxed);
        v.bpm = viewBpm_.load (std::memory_order_relaxed);
        v.ppq = viewPpq_.load (std::memory_order_relaxed);
        return v;
    }
    float jackVolts (int jack) const;

private:
    class InUnit;
    class OutUnit;
    std::unique_ptr<InUnit> in_;
    std::unique_ptr<OutUnit> out_;
    std::atomic<float> level_ { 1.0f };
    std::array<std::atomic<float>, 4> meters_ {};
    Transport transport_;
    std::atomic<bool> viewValid_ { false }, viewPlaying_ { false };
    std::atomic<double> viewBpm_ { 120.0 }, viewPpq_ { 0.0 };
    double sampleRate_ = 48000.0;
};

} // namespace jidai

// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// BUSHIDO in the rack: the BUSHIDO engine (engine/Sq10Module, compiled from the BUSHIDO sources) as one unit
// of the rack graph, run one sample at a time like the web rack.
// In the rack the MIXER IN normals are off: host audio goes to the first RONIN's EXT IN, never into BUSHIDO.

#include "Device.h"

#include "engine/Sq10Module.h"

#include <array>
#include <atomic>

namespace jidai {

class BushidoDevice : public Device {
public:
    BushidoDevice();
    ~BushidoDevice() override;
    DeviceKind kind() const override { return DeviceKind::Bushido; }

    void prepare (double sampleRate) override;
    void beginBlock() override;

    Sq10Module& engine() { return sq; }
    int paramIndex (const std::string& id) const;     // engine parameter by layout id ("CH:PORTA A"), -1 if none
    void setParam (const std::string& id, float value);
    float param (const std::string& id) const;
    void press (const std::string& id, bool down);    // START/STOP, STEP, RESET
    float indicator (const std::string& id) const;    // lamps by layout id ("STEP:3"), 0..1

    // BYPASS (the rocker at the top left): the engine keeps running, GATE A and B stay low, no notes.
    bool bypassed() const { return bypass_.load(); }
    void setBypassed (bool on) { bypass_.store (on); }

    // MIDI out, as in the BUSHIDO plugin: GATE A -> channel 1, GATE B -> channel 2, the note is the CV read as Hz/V.
    // Inside the rack it is collected per block and not sent to the host.
    struct NoteEvent { int sample = 0; int channel = 1; int note = 0; bool on = false; };
    static constexpr int kMaxNoteEvents = 64;
    int noteEventCount() const { return noteCount_; }
    const NoteEvent& noteEvent (int i) const { return notes_[(size_t) i]; }
    void collectMidi (int sampleInBlock);       // audio thread, after each graph sample

    float jackVolts (int jack) const;

private:
    class EngineUnit;
    Sq10Module sq;
    std::unique_ptr<EngineUnit> unit_;
    std::atomic<bool> bypass_ { false };
    std::array<NoteEvent, kMaxNoteEvents> notes_ {};
    int noteCount_ = 0;
    int midiNote_[2] = { -1, -1 };
    bool gatePrev_[2] = { false, false };
};

} // namespace jidai

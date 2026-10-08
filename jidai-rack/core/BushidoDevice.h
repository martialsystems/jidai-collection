// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// BUSHIDO in the rack: the BUSHIDO engine (engine/BushidoModule, compiled from the BUSHIDO sources) as one unit
// of the rack graph, run one sample at a time like the web rack.
// In the rack the MIXER IN normals are off: host audio reaches devices only through RACK I/O cables.
// Host transport goes to the engine through setTransport (BUSHIDO's HOST clock, JCS R5.7).

#include "Device.h"

#include "engine/BushidoModule.h"
#include "engine/MidiOut.h"

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
    void setTransport (const Transport& t) override;
    // CV A / CV B carry the role of their row's PITCH LAW (V/OCT or HZ/V LIN, JCS R4); the rest by type.
    jidai::jcs::Role jackRole (int jack) const override;

    BushidoModule& engine() { return sq; }
    int paramIndex (const std::string& id) const;     // engine parameter by layout id ("CH:PORTA A"), -1 if none
    void setParam (const std::string& id, float value);
    float param (const std::string& id) const;
    void press (const std::string& id, bool down);    // START/STOP, STEP, RESET
    float indicator (const std::string& id) const;    // lamps by layout id ("STEP:3"), 0..1

    // BYPASS (the rocker at the top left): the engine keeps running, GATE A and B stay low, no notes.
    bool bypassed() const { return bypass_.load(); }
    void setBypassed (bool on) { bypass_.store (on); }

    // MIDI out, as in the BUSHIDO plugin (engine/MidiOut.h): GATE A -> CH A, GATE B -> CH B, the note from the row's
    // target volts under its PITCH LAW. Inside the rack it is collected per block and not sent to the host.
    struct NoteEvent { int sample = 0; int channel = 1; int note = 0; bool on = false; int velocity = 100; };
    static constexpr int kMaxNoteEvents = 64;
    int noteEventCount() const { return noteCount_; }
    const NoteEvent& noteEvent (int i) const { return notes_[(size_t) i]; }
    void collectMidi (int sampleInBlock);       // audio thread, after each graph sample

    float jackVolts (int jack) const;

private:
    class EngineUnit;
    BushidoModule sq;
    std::unique_ptr<EngineUnit> unit_;
    std::atomic<bool> bypass_ { false };
    std::array<NoteEvent, kMaxNoteEvents> notes_ {};
    int noteCount_ = 0;
    BushidoMidiOut midiOut_;
    long long blockStart_ = 0;
};

} // namespace jidai

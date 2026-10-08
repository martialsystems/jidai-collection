// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// The Jidai rack: an ordered list of devices and the cables between their jacks, all in one RackGraph (JCS v1.1).
// RACK I/O (JCS R13) is the only host connection: host audio comes out of RACK#1/HOST:IN L/R, the host hears what is
// patched into RACK#1/MAIN:OUT L/R, MIDI and transport come out of its MIDI and TRANSPORT jacks. A rack without
// RACK I/O is silent. There is at most one RACK I/O; it always sits at the top and cannot be removed.
// Latency (JCS R11): P(unit) = L(device) + max P over the non-feedback audio cables into it. Each cable into MAIN OUT
// carries a delay of maxP - P(source) (a control source counts 0), and latency() reports maxP to the host.
// Framework-free, so the unit tests run without JUCE.
//
// Threads: edits (devices, cables, programs) come from one thread, the message thread. process() runs on the
// audio thread. Edits build the new routing first, then swap it in under a lock that process() only try-locks:
// when an edit holds it, that block is silent rather than waiting. A removed device is freed after the swap,
// so the audio thread never sees it again.

#include "BushidoDevice.h"
#include "OrigamiDevice.h"
#include "RackIODevice.h"
#include "RoninDevice.h"

#include "jidai/jcs/Roles.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace jidai {

// A cable as the user patched it: two global jack ids ("RONIN#1/VCO:HZ/V"), in either order.
struct CableSpec {
    std::string a, b;
    int color = -1;             // -1 = role colour of the source jack (JCS R14); 0..5 = user override swatch
    int age = 0;                // larger = patched later; R9 walks cables oldest -> newest
    bool autoRouted = false;    // made by auto-route on insert (saved as auto="1")
    bool legacyInvert = false;  // M3: keeps the v2 S-15 inversion on this one cable
};

// What the rack worked out for one cable (index-aligned with cables()). Message thread.
struct CableInfo {
    bool live = false;          // carries signal (resolves, Out -> In, types allowed)
    bool feedback = false;      // R9: delayed one sample
    int comp = 0;               // R11 compensation delay on a MAIN OUT cable, samples
    int skew = 0;               // R11.6: arrives this many samples before the latest audio input of its destination
    jidai::jcs::Role role = jidai::jcs::Role::CV;     // source jack's role: the default colour and glyph
    jidai::jcs::Badge badge = jidai::jcs::Badge::None;
};

class Rack {
public:
    enum class Check { Ok, UnknownJack, TwoOutputs, TwoInputs, SameJack, BadType };
    static const char* checkText (Check);

    Rack();
    ~Rack();

    void prepare (double sampleRate, int maxBlock);
    double sampleRate() const { return sampleRate_; }

    // Devices, top to bottom. position < 0 appends. number 0 takes the lowest free number for that kind
    // (a saved rack passes its own). Returns the new device, or null when that number is taken (or a second RACK I/O).
    // A new RONIN comes with its Voice program cables; the others with none. RACK I/O always goes to the top, and no
    // other device goes above it. This is the plain add (state loading); a user insert uses insertNew().
    Device* addDevice (DeviceKind kind, int position = -1, int number = 0);
    // Takes a device made elsewhere (tests, future devices). Same placement rules.
    Device* adoptDevice (std::unique_ptr<Device> device, int position = -1);
    // A user insert from the browser: addDevice, then BUSHIDO's new-instance defaults (HOST clock when the host
    // transport plays, BUSHIDO_Redesign), then auto-route unless the user held Shift (JIDAI_RACK_Redesign 3.6).
    Device* insertNew (DeviceKind kind, int position = -1, bool autoRoute = true);
    // Auto-route (3.6): audio outs -> MAIN OUT; ORIGAMI also HOST IN -> its HOST IN; a RONIN directly under a
    // BUSHIDO also CV A -> VCO:V/OCT (or VCO:HZ/V when that row's PITCH LAW is HZ/V LIN) and GATE A -> EG 1:TRIG.
    // Cables are marked autoRouted. Returns how many it made.
    int autoRoute (Device* device);
    // Migration M5: RACK I/O (made if missing) plus HOST IN -> first RONIN and every RONIN out -> MAIN OUT,
    // reproducing the v2 hidden routing.
    void applyLegacyHostRouting();
    RackIODevice* rackIO() const { return rackIO_; }
    // Migration M3: whether the v2 gate law (S-15 into every RONIN input) sounds different from JCS R2/R3s on a
    // cable between these jacks. Only those cables get legacyInvert when a v2 rack loads.
    bool legacyInversionDiffers (const std::string& jackA, const std::string& jackB) const;
    bool removeDevice (Device* device);           // also removes every cable on its jacks; RACK I/O stays
    bool moveDevice (Device* device, int position);
    int deviceCount() const { return (int) devices_.size(); }
    Device* device (int index) const;
    int indexOf (const Device* device) const;
    Device* findDevice (const std::string& rackId) const;
    void clear();

    // Cables. setCables replaces the whole patch (stack order is visual only; age decides feedback).
    void setCables (const std::vector<CableSpec>& cables);
    const std::vector<CableSpec>& cables() const { return cables_; }
    Check check (const std::string& jackA, const std::string& jackB) const;
    Check connect (const std::string& jackA, const std::string& jackB, int color = -1);    // appends as the newest
    bool setCableColor (int index, int color);
    bool disconnect (const std::string& jackA, const std::string& jackB);
    // Replaces the cables that have both ends on this device. Cables to other devices stay.
    void replaceInternalCables (Device* device, const std::vector<CableSpec>& cables);
    bool loadRoninProgram (RoninDevice* ronin, int index);     // its own cables are replaced by the program's

    static std::string jackId (const Device& device, int jack);
    bool resolve (const std::string& globalJack, Device*& device, int& jack) const;
    float jackVolts (const std::string& globalJack) const;     // last processed value, any thread (tests, meters)

    int delayedCableCount() const { return graph_.delayedCableCount(); }
    int liveCableCount() const { return graph_.cableCount(); }
    int unitRunsPerSample() const { return graph_.unitRunsPerSample(); }
    const std::vector<CableInfo>& cableInfo() const { return info_; }
    jidai::jcs::Role jackRole (const std::string& globalJack) const;

    // JCS R11. latency(): maxP over the audio paths into MAIN OUT, any thread. pathLatency(): P of a device's
    // audio outputs (message thread). updateLatency(): call on the message thread when a device may have changed its
    // latency (ORIGAMI 2x); rebuilds the compensation and returns true when anything changed.
    int latency() const { return latency_.load(); }
    int pathLatency (const Device* device) const;
    bool updateLatency();

    // Audio thread. Host audio (host units, +-1 = +-5 V) comes out of RACK I/O HOST IN; the output is what reaches
    // MAIN OUT. inL/inR may be null (silence in), inR null with inL set is mono. MIDI events are for this block,
    // sorted by sample. setTransport() is called before process() each block.
    // Returns false when an edit held the lock and the block was silenced.
    void setTransport (const Transport& t) { transport_ = t; hostPlaying_.store (t.valid && t.playing); }
    bool hostPlaying() const { return hostPlaying_.load(); }
    bool process (const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                  const MidiNote* midi = nullptr, int numMidi = 0);

private:
    void rebuild();
    int nextNumber (DeviceKind kind) const;
    Device* place (std::unique_ptr<Device> made, int position, std::vector<CableSpec> internal);
    int firstPosition() const { return rackIO_ != nullptr ? 1 : 0; }
    bool hasCable (const std::string& a, const std::string& b) const;
    void addCableLocked (const std::string& a, const std::string& b, bool autoRouted);

    std::vector<std::unique_ptr<Device>> devices_;
    std::vector<CableSpec> cables_;
    std::vector<CableInfo> info_;
    std::vector<BushidoDevice*> bushidos_;
    std::vector<int> deviceLatency_;              // per device, as used by the last rebuild
    std::vector<int> devicePath_;                 // per device, P of its audio outputs
    RackIODevice* rackIO_ = nullptr;
    RackGraph graph_;
    std::mutex lock_;
    std::atomic<int> latency_ { 0 };
    std::atomic<bool> hostPlaying_ { false };
    Transport transport_;
    double sampleRate_ = 48000.0;
    int maxBlock_ = 512;
    int nextAge_ = 0;
};

} // namespace jidai

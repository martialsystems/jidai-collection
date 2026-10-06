// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// The Jidai rack: an ordered list of devices (BUSHIDO and RONIN, any number of each, or none) and the cables
// between their jacks, all in one RackGraph. Framework-free, so the unit tests run without JUCE.
//
// Threads: edits (devices, cables, programs) come from one thread, the message thread. process() runs on the
// audio thread. Edits build the new routing first, then swap it in under a lock that process() only try-locks:
// when an edit holds it, that block is silent rather than waiting. A removed device is freed after the swap,
// so the audio thread never sees it again.

#include "BushidoDevice.h"
#include "RoninDevice.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace jidai {

// A cable as the user patched it: two global jack ids ("MS-50#1/VCO:HZ/V"), in either order.
struct CableSpec {
    std::string a, b;
    int color = 0;
    int age = 0;        // larger = patched later; the graph delays only the newest cable of a feedback loop
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
    // (a saved rack passes its own). Returns the new device, or null when that number is taken.
    // A new RONIN comes with its Voice program cables; a new BUSHIDO with none.
    Device* addDevice (DeviceKind kind, int position = -1, int number = 0);
    bool removeDevice (Device* device);           // also removes every cable on its jacks
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
    Check connect (const std::string& jackA, const std::string& jackB, int color = 0);    // appends as the newest
    bool disconnect (const std::string& jackA, const std::string& jackB);
    // Replaces the cables that have both ends on this device. Cables to other devices stay.
    void replaceInternalCables (Device* device, const std::vector<CableSpec>& cables);
    bool loadRoninProgram (RoninDevice* ronin, int index);     // its own cables are replaced by the program's

    static std::string jackId (const Device& device, int jack);
    bool resolve (const std::string& globalJack, Device*& device, int& jack) const;
    float jackVolts (const std::string& globalJack) const;     // last processed value, any thread (tests, meters)

    int delayedCableCount() const { return graph_.delayedCableCount(); }
    int liveCableCount() const { return graph_.cableCount(); }

    // Audio thread. Host audio feeds the first RONIN's EXT IN; the output is the sum of every RONIN's OUTPUT.
    // inL/inR may be null (silence in). Returns false when an edit held the lock and the block was silenced.
    bool process (const float* inL, const float* inR, float* outL, float* outR, int numSamples);

private:
    void rebuild();
    int nextNumber (DeviceKind kind) const;

    std::vector<std::unique_ptr<Device>> devices_;
    std::vector<CableSpec> cables_;
    std::vector<RoninDevice*> ronins_;
    std::vector<BushidoDevice*> bushidos_;
    RackGraph graph_;
    std::mutex lock_;
    double sampleRate_ = 48000.0;
    int maxBlock_ = 512;
    int nextAge_ = 0;
};

} // namespace jidai

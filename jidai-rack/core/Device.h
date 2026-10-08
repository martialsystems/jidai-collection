// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// A device is one instrument in the rack: RACK I/O, a BUSHIDO, a RONIN or an ORIGAMI. It owns its engine and puts its
// units in the rack's one graph. Jacks are named "SECTION:LABEL" inside a device and "PREFIX#N/SECTION:LABEL" across
// the rack (JCS R6), where PREFIX is the device name (RACK, BUSHIDO, RONIN, ORIGAMI) and N numbers the instances.

#include "RackGraph.h"

#include "jidai/jcs/Roles.h"

#include <string>
#include <vector>

namespace jidai {

enum class DeviceKind { Bushido, Ronin, Origami, RackIO };

const char* deviceKindName (DeviceKind kind);     // "BUSHIDO", "RONIN", "ORIGAMI", "RACK I/O"
const char* deviceKindPrefix (DeviceKind kind);   // "BUSHIDO", "RONIN", "ORIGAMI", "RACK" (JCS R6 prefixes)
const char* deviceKindFromName (const std::string& name, DeviceKind& kind);   // name or prefix; null if unknown

struct JackDesc {
    std::string id;        // SECTION:LABEL
    Unit* unit = nullptr;  // null when the jack is not on the graph
    int port = -1;
    PortDesc desc { "", PortType::CV, PortDir::In };
    bool backOnly = false; // on the rear bay only (HOST IN/OUT, RACK I/O)
};

// Host transport, passed to every device each block (JCS R5.7).
struct Transport {
    bool valid = false;        // the host reported a position
    bool playing = false;
    double bpm = 120.0;
    double ppq = 0.0;          // quarter notes at the block's first sample
    long long samplePosition = 0;
};

// A section of jacks on the generated rear bay (JIDAI_RACK_Redesign 3.4).
struct JackGroup {
    std::string title;
    std::vector<int> jacks;    // indices into Device::jacks()
};

class Device {
public:
    virtual ~Device() = default;
    virtual DeviceKind kind() const = 0;

    int number = 1;                                 // N in RACK#N, unique per kind inside one rack
    std::string rackId() const { return std::string (deviceKindPrefix (kind())) + "#" + std::to_string (number); }
    std::string title() const { return std::string (deviceKindName (kind())) + " " + std::to_string (number); }
    bool folded = false;                            // window only: shown as a strip in the rack; saved with the rack
    bool closed = false;                            // window only: CLOSED 1 U front (OPEN by default); saved with the rack
    std::string name;                               // user name ("BASS SEQ"); empty shows the title
    std::string displayName() const { return name.empty() ? title() : name; }

    const std::vector<JackDesc>& jacks() const { return jacks_; }
    int findJack (const std::string& id) const;
    const std::vector<Unit*>& units() const { return units_; }

    // Message thread, while the audio thread is not processing this device.
    virtual void prepare (double sampleRate) = 0;
    // Audio thread, once per block before the first sample: picks up knob changes.
    virtual void beginBlock() {}
    // Processing latency in samples on this device's audio outputs (JCS R11 L_d). Any thread.
    virtual int latencySamples() const { return 0; }
    // The units whose outputs carry that latency. Empty (the default): every unit with an audio output, which suits
    // single-unit devices. A multi-unit device names its output stage, so the latency counts once on a path.
    virtual std::vector<const Unit*> latencyUnits() const { return {}; }
    // Host transport for this block (audio thread, before beginBlock).
    virtual void setTransport (const Transport&) {}
    // "Run A before B" inside the device. Never feedback; for path latency they count as internal audio paths.
    virtual std::vector<OrderEdge> orderEdges() const { return {}; }
    // Rear bay sections. The default groups jacks by SECTION, in jack order.
    virtual std::vector<JackGroup> jackGroups() const;
    // JCS R14 role of a jack (cable colour = the source jack's role). The default reads the port type, the S-trig
    // flags and the labels "V/OCT" and "HZ/V".
    virtual jidai::jcs::Role jackRole (int jack) const;

protected:
    std::vector<JackDesc> jacks_;
    std::vector<Unit*> units_;
};

} // namespace jidai

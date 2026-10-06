// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// A device is one instrument in the rack: a BUSHIDO or a RONIN. It owns its engine and puts its units in the
// rack's one graph. Jacks are named "SECTION:LABEL" inside a device and "RACK#N/SECTION:LABEL" across the rack,
// where RACK is the instrument's jack prefix (SQ-10 for BUSHIDO, MS-50 for RONIN) and N numbers the instances.

#include "RackGraph.h"

#include <string>
#include <vector>

namespace jidai {

enum class DeviceKind { Bushido, Ronin };

const char* deviceKindName (DeviceKind kind);     // "BUSHIDO", "RONIN"
const char* deviceKindPrefix (DeviceKind kind);   // "SQ-10", "MS-50"

struct JackDesc {
    std::string id;        // SECTION:LABEL
    Unit* unit = nullptr;  // null when the jack is not on the graph
    int port = -1;
    PortDesc desc { "", PortType::CV, PortDir::In };
};

class Device {
public:
    virtual ~Device() = default;
    virtual DeviceKind kind() const = 0;

    int number = 1;                                 // N in RACK#N, unique per kind inside one rack
    std::string rackId() const { return std::string (deviceKindPrefix (kind())) + "#" + std::to_string (number); }
    std::string title() const { return std::string (deviceKindName (kind())) + " " + std::to_string (number); }
    bool folded = false;                            // window only: shown as a strip in the rack; saved with the rack

    const std::vector<JackDesc>& jacks() const { return jacks_; }
    int findJack (const std::string& id) const;
    const std::vector<Unit*>& units() const { return units_; }

    // Message thread, while the audio thread is not processing this device.
    virtual void prepare (double sampleRate) = 0;
    // Audio thread, once per block before the first sample: picks up knob changes.
    virtual void beginBlock() {}

protected:
    std::vector<JackDesc> jacks_;
    std::vector<Unit*> units_;
};

} // namespace jidai

// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// One patch graph for the whole rack. Every device (BUSHIDO, RONIN, any number of each) puts its units in it,
// and a cable can join any two jacks in the rack.
//
// Cable law, the same as the web rack (web/rack_engine.js in BUSHIDO, a port of RONIN's PatchGraph):
//  - a cable runs Out -> In; one output into several inputs fans out, several cables into one input are summed;
//  - unpatched Audio/CV inputs sit at their rest volts, unpatched Gate inputs keep their last value;
//  - walking the cables oldest to newest, a cable that closes a loop is feedback; only the newest feedback cable
//    is delayed one sample, older feedback cables stay zero-delay (their destinations run a second time);
//  - S-15: a logic Gate source into a non-Gate RONIN input gives 0 V while high and +5 V while low;
//    a RONIN gate into a BUSHIDO input (plain volts, high above 1 V) arrives as 0/5 V.
// Framework-free. process() does not allocate, lock or log.

#include "Modular/Port.h"

#include <memory>
#include <vector>

namespace jidai {

// One processing unit in the graph: a RONIN module or a BUSHIDO engine. Values are volts, one float per port.
class Unit {
public:
    virtual ~Unit() = default;
    virtual int numPorts() const = 0;
    virtual PortDesc port (int index) const = 0;
    virtual float* values() = 0;          // inputs are written by the graph, outputs are read from here
    virtual bool* connected() = 0;        // set by the graph for input ports
    virtual void processSample() = 0;
    // BUSHIDO's inputs read plain volts and its gates are 0/5 V. RONIN's gates are logic levels.
    virtual bool plainVoltGates() const { return false; }
};

struct GraphCable {
    Unit* source = nullptr;
    int sourcePort = -1;
    Unit* dest = nullptr;
    int destPort = -1;
};

class RackGraph {
public:
    struct Snapshot;
    struct SnapshotDeleter { void operator() (Snapshot*) const; };
    using SnapshotPtr = std::unique_ptr<Snapshot, SnapshotDeleter>;

    RackGraph();
    ~RackGraph();

    // Message thread. Builds the routing for these units and cables (oldest first). Illegal cables are skipped.
    static SnapshotPtr build (const std::vector<Unit*>& units, const std::vector<GraphCable>& cablesOldestFirst);

    // Swaps in a new snapshot and frees the old one. The caller makes sure process() is not running
    // (the rack holds its lock). A delayed cable that was delayed before keeps its held sample.
    void install (SnapshotPtr next);

    // Audio thread: one sample through every unit.
    void process();

    int cableCount() const;
    int delayedCableCount() const;
    bool cableIsDelayed (int index) const;     // index into the cables build() kept, oldest first

private:
    SnapshotPtr active_;
};

// Whether a cable from this output type into this input type carries signal (RONIN's SCHEMATICS table).
bool portTypesAllowed (PortType from, PortType to);

} // namespace jidai

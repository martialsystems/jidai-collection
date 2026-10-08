// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// One patch graph for the whole rack. Every device (RACK I/O, BUSHIDO, RONIN, ORIGAMI, any number of each) puts its
// units in it, and a cable can join any two jacks in the rack. The law is the Jidai Cable Standard v1.1:
//  - R8:  a cable runs Out -> In; one output fans out, several cables into one input are summed (per-cable
//         conversion happens before the sum; stack order and colour never change the sum);
//  - R9:  walking cables oldest -> newest, a cable that closes a loop over the cables kept so far (or a self-patch)
//         is a feedback cable. EVERY feedback cable is delayed exactly one sample, all others are zero-delay, and
//         every unit runs exactly once per sample, in one topological order (jidai/jcs/Graph.h);
//  - R10: unpatched inputs sit at their rest volts every sample (S-trig inputs rest at +5 V). Nothing latches;
//  - R2/R3s: only a cable that lands on an S-trig input is polarity-converted: a Gate source that is not S-trig
//         writes high ? 0 V : 5 V (a 0/5 V gate source is read with the R3 Schmitt, 1.0/0.5 V, per cable);
//         an S-trig source passes as written. RONIN's logic-level gates (0/1) are written as 0/5 V everywhere else.
//         Every other input receives raw volts;
//  - M3:  a cable loaded from an older patch with legacyInvert keeps the old S-15 law on that one cable;
//  - R11: a cable may carry a latency-compensation delay (only the rack's MAIN OUT inputs use it).
// Fixed order edges are a device's internal "run A before B" constraints (RONIN's HOST IN before EXT IN); they carry
// no signal and are never feedback. Framework-free. process() does not allocate, lock or log.

#include "Modular/Port.h"

#include <memory>
#include <vector>

namespace jidai {

// One processing unit in the graph: a RONIN module, a BUSHIDO engine, an ORIGAMI core, a RACK I/O block.
// Values are volts, one float per port.
class Unit {
public:
    virtual ~Unit() = default;
    virtual int numPorts() const = 0;
    virtual PortDesc port (int index) const = 0;
    virtual float* values() = 0;          // inputs are written by the graph, outputs are read from here
    virtual bool* connected() = 0;        // set by the graph for input ports
    virtual void processSample() = 0;
    // True for units whose Gate outputs are already 0/5 V (BUSHIDO, ORIGAMI, RACK I/O). RONIN's are logic levels.
    virtual bool plainVoltGates() const { return false; }
    // JCS R3s: this input is an S-trig input (RONIN EG 1/EG 2 TRIG). Its rest is +5 V.
    virtual bool strigInput (int port) const { (void) port; return false; }
};

struct GraphCable {
    Unit* source = nullptr;
    int sourcePort = -1;
    Unit* dest = nullptr;
    int destPort = -1;
    bool legacyInvert = false;   // M3: the old S-15 inversion on this cable only
    int delay = 0;               // R11 latency compensation, samples (MAIN OUT inputs)
};

struct OrderEdge {
    Unit* before = nullptr;
    Unit* after = nullptr;
};

class RackGraph {
public:
    struct Snapshot;
    struct SnapshotDeleter { void operator() (Snapshot*) const; };
    using SnapshotPtr = std::unique_ptr<Snapshot, SnapshotDeleter>;

    RackGraph();
    ~RackGraph();

    // Message thread. Builds the routing for these units and cables (oldest first). Illegal cables are skipped.
    // status, when given, gets one entry per input cable: -1 skipped (illegal), 0 zero-delay, 1 feedback (delayed).
    static SnapshotPtr build (const std::vector<Unit*>& units, const std::vector<GraphCable>& cablesOldestFirst,
                              const std::vector<OrderEdge>& order = {}, std::vector<signed char>* status = nullptr);

    // Swaps in a new snapshot and frees the old one. The caller makes sure process() is not running
    // (the rack holds its lock). A delayed cable that was delayed before keeps its held sample.
    void install (SnapshotPtr next);

    // Audio thread: one sample through every unit, each exactly once.
    void process();

    int cableCount() const;
    int delayedCableCount() const;
    bool cableIsDelayed (int index) const;     // index into the cables build() kept, oldest first
    int unitRunsPerSample() const;             // most times any unit appears in the run order: 1 (R9.4); for tests

private:
    SnapshotPtr active_;
};

// Whether a cable from this output type into this input type carries signal (RONIN's SCHEMATICS table).
bool portTypesAllowed (PortType from, PortType to);

} // namespace jidai

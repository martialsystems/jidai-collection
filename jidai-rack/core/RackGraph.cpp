// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "RackGraph.h"

#include "jidai/jcs/Detect.h"
#include "jidai/jcs/Graph.h"

#include <algorithm>

namespace jidai {

namespace {

// Rows are the source type (Audio, CV, Gate). Columns are the destination type.
// Audio and CV may feed Audio or CV. Gate may feed Audio, CV, or Gate. Audio or CV into a Gate input is refused.
constexpr bool kAllowed[3][3] = {
    { true, true, false },
    { true, true, false },
    { true, true, true },
};

int typeIndex (PortType type)
{
    if (type == PortType::Audio)
        return 0;
    if (type == PortType::CV)
        return 1;
    return 2;
}

}

bool portTypesAllowed (PortType from, PortType to)
{
    return kAllowed[typeIndex (from)][typeIndex (to)];
}

struct RackGraph::Snapshot {
    struct Edge {
        int source = -1;
        int sourcePort = -1;
        int dest = -1;
        int destPort = -1;
        bool delayed = false;          // R9 feedback cable: one sample late
        float held = 0.0f;
        bool sourcePlain = false;
        bool destPlain = false;
        bool sourceGate = false;
        bool sourceStrig = false;
        bool destGate = false;
        bool destStrig = false;
        bool legacyInvert = false;
        jidai::jcs::Schmitt schmitt;   // R3 reading of a 0/5 V gate into an S-trig input
        std::vector<float> ring;       // R11 compensation delay line
        int ringPos = 0;

        float convert (float v) noexcept
        {
            if (legacyInvert)
            {
                // The v2 rack law (S-15 into every non-plain input), kept per cable by migration M3.
                if (! destPlain)
                {
                    if (sourceGate && ! destGate && ! sourceStrig)
                        v = v >= 0.5f ? 0.0f : 5.0f;
                }
                else if (! sourcePlain && sourceGate)
                    v = sourceStrig ? (v < 1.5f ? 5.0f : 0.0f) : (v >= 0.5f ? 5.0f : 0.0f);
                return v;
            }
            if (! sourceGate || sourceStrig)
                return v;                                   // raw volts; an S-trig source passes as written
            if (destStrig)
            {
                bool high = v >= 0.5f;
                if (sourcePlain)
                {
                    schmitt.process (v);
                    high = schmitt.high;
                }
                return jidai::jcs::strigVoltsFor (high);    // 0 V held, +5 V released
            }
            if (! sourcePlain)
                return v >= 0.5f ? jidai::jcs::kGateHigh : jidai::jcs::kGateLow;      // R2 levels on the cable
            return v;
        }
    };

    std::vector<Unit*> units;
    std::vector<std::vector<PortDesc>> ports;     // cached port descriptions, [unit][port]
    std::vector<std::vector<char>> patched;       // [unit][port]
    std::vector<std::vector<char>> strig;         // [unit][port] S-trig inputs
    std::vector<std::vector<int>> incoming;       // [unit] -> edge indices, oldest first
    std::vector<Edge> edges;
    std::vector<int> order;

    void clearInputs (int unit)
    {
        Unit* u = units[(size_t) unit];
        float* value = u->values();
        bool* connected = u->connected();
        const auto& desc = ports[(size_t) unit];
        const auto& isPatched = patched[(size_t) unit];
        const auto& isStrig = strig[(size_t) unit];
        for (size_t port = 0; port < desc.size(); ++port)
        {
            if (desc[port].dir != PortDir::In)
            {
                connected[port] = false;
                continue;
            }
            connected[port] = isPatched[port] != 0;
            if (isPatched[port])
                value[port] = 0.0f;
            else
                value[port] = isStrig[port] ? jidai::jcs::kStrigRest : desc[port].rest;   // R10: no latching
        }
    }

    void contribute (int unit)
    {
        float* value = units[(size_t) unit]->values();
        for (int index : incoming[(size_t) unit])
        {
            Edge& e = edges[(size_t) index];
            float v = e.delayed ? e.held : units[(size_t) e.source]->values()[e.sourcePort];
            v = e.convert (v);
            if (! e.ring.empty())
            {
                const float out = e.ring[(size_t) e.ringPos];
                e.ring[(size_t) e.ringPos] = v;
                e.ringPos = (e.ringPos + 1) % (int) e.ring.size();
                v = out;
            }
            value[e.destPort] += v;
        }
    }
};

void RackGraph::SnapshotDeleter::operator() (Snapshot* s) const { delete s; }

RackGraph::RackGraph() : active_ (new Snapshot()) {}
RackGraph::~RackGraph() = default;

RackGraph::SnapshotPtr RackGraph::build (const std::vector<Unit*>& units, const std::vector<GraphCable>& cables,
                                         const std::vector<OrderEdge>& orderEdges, std::vector<signed char>* status)
{
    if (status != nullptr)
        status->assign (cables.size(), -1);
    std::vector<size_t> keptFrom;
    SnapshotPtr snap (new Snapshot());
    const int n = (int) units.size();
    snap->units = units;
    snap->ports.resize ((size_t) n);
    snap->patched.resize ((size_t) n);
    snap->strig.resize ((size_t) n);
    snap->incoming.resize ((size_t) n);
    for (int u = 0; u < n; ++u)
    {
        const int count = units[(size_t) u]->numPorts();
        for (int p = 0; p < count; ++p)
        {
            snap->ports[(size_t) u].push_back (units[(size_t) u]->port (p));
            snap->strig[(size_t) u].push_back (units[(size_t) u]->strigInput (p) ? 1 : 0);
        }
        snap->patched[(size_t) u].assign ((size_t) count, 0);
    }

    auto indexOf = [&units] (const Unit* unit)
    {
        const auto it = std::find (units.begin(), units.end(), unit);
        return it == units.end() ? -1 : (int) (it - units.begin());
    };

    for (size_t ci = 0; ci < cables.size(); ++ci)
    {
        const auto& c = cables[ci];
        const int s = indexOf (c.source);
        const int d = indexOf (c.dest);
        if (s < 0 || d < 0)
            continue;
        const auto& sp = snap->ports[(size_t) s];
        const auto& dp = snap->ports[(size_t) d];
        if (c.sourcePort < 0 || c.sourcePort >= (int) sp.size() || c.destPort < 0 || c.destPort >= (int) dp.size())
            continue;
        const PortDesc& sd = sp[(size_t) c.sourcePort];
        const PortDesc& dd = dp[(size_t) c.destPort];
        if (sd.dir != PortDir::Out || dd.dir != PortDir::In || ! portTypesAllowed (sd.type, dd.type))
            continue;
        if (s == d && c.sourcePort == c.destPort)
            continue;
        Snapshot::Edge e;
        e.source = s;
        e.sourcePort = c.sourcePort;
        e.dest = d;
        e.destPort = c.destPort;
        e.sourcePlain = units[(size_t) s]->plainVoltGates();
        e.destPlain = units[(size_t) d]->plainVoltGates();
        e.sourceGate = sd.type == PortType::Gate;
        e.sourceStrig = sd.strigVolts;
        e.destGate = dd.type == PortType::Gate;
        e.destStrig = snap->strig[(size_t) d][(size_t) c.destPort] != 0;
        e.legacyInvert = c.legacyInvert;
        if (c.delay > 0)
            e.ring.assign ((size_t) c.delay, 0.0f);
        snap->edges.push_back (std::move (e));
        keptFrom.push_back (ci);
    }

    // R9: every loop-closing cable is delayed one sample; one topological order, each unit once.
    std::vector<jidai::jcs::GraphEdge> fixed, graphEdges;
    for (const auto& o : orderEdges)
    {
        const int a = indexOf (o.before), b = indexOf (o.after);
        if (a >= 0 && b >= 0)
            fixed.push_back ({ a, b });
    }
    for (const auto& e : snap->edges)
        graphEdges.push_back ({ e.source, e.dest });
    const auto feedback = jidai::jcs::classifyFeedback (n, fixed, graphEdges);
    for (size_t i = 0; i < snap->edges.size(); ++i)
    {
        snap->edges[i].delayed = feedback[i] != 0;
        if (status != nullptr)
            (*status)[keptFrom[i]] = feedback[i] != 0 ? 1 : 0;
    }
    snap->order = jidai::jcs::runOrder (n, fixed, graphEdges, feedback);

    for (size_t i = 0; i < snap->edges.size(); ++i)
    {
        const auto& e = snap->edges[i];
        snap->patched[(size_t) e.dest][(size_t) e.destPort] = 1;
        snap->incoming[(size_t) e.dest].push_back ((int) i);
    }
    return snap;
}

void RackGraph::install (SnapshotPtr next)
{
    if (next == nullptr)
        next.reset (new Snapshot());
    for (auto& e : next->edges)
    {
        for (const auto& old : active_->edges)
        {
            if (active_->units[(size_t) old.source] != next->units[(size_t) e.source] || old.sourcePort != e.sourcePort)
                continue;
            if (active_->units[(size_t) old.dest] != next->units[(size_t) e.dest] || old.destPort != e.destPort)
                continue;
            if (e.delayed && old.delayed)
                e.held = old.held;
            e.schmitt = old.schmitt;
            if (! e.ring.empty() && e.ring.size() == old.ring.size())
            {
                e.ring = old.ring;          // message thread: allocation is fine here
                e.ringPos = old.ringPos;
            }
            break;
        }
    }
    std::swap (active_, next);
}

void RackGraph::process()
{
    Snapshot& s = *active_;
    for (int u : s.order)
    {
        s.clearInputs (u);
        s.contribute (u);
        s.units[(size_t) u]->processSample();
    }
    for (auto& e : s.edges)
        if (e.delayed)
            e.held = s.units[(size_t) e.source]->values()[e.sourcePort];
}

int RackGraph::cableCount() const
{
    return (int) active_->edges.size();
}

int RackGraph::delayedCableCount() const
{
    int count = 0;
    for (const auto& e : active_->edges)
        if (e.delayed)
            ++count;
    return count;
}

bool RackGraph::cableIsDelayed (int index) const
{
    if (index < 0 || index >= (int) active_->edges.size())
        return false;
    return active_->edges[(size_t) index].delayed;
}

int RackGraph::unitRunsPerSample() const
{
    std::vector<int> runs (active_->units.size(), 0);
    int most = 0;
    for (int u : active_->order)
        most = std::max (most, ++runs[(size_t) u]);
    return most;
}

} // namespace jidai

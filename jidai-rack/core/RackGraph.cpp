// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "RackGraph.h"

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
        bool feedback = false;
        bool delayed = false;
        float held = 0.0f;
        bool sourcePlain = false;
        bool destPlain = false;
        bool sourceGate = false;
        bool sourceStrig = false;
        bool destGate = false;
    };

    std::vector<Unit*> units;
    std::vector<std::vector<PortDesc>> ports;     // cached port descriptions, [unit][port]
    std::vector<std::vector<char>> patched;       // [unit][port]
    std::vector<std::vector<int>> incoming;       // [unit] -> edge indices, oldest first
    std::vector<Edge> edges;
    std::vector<int> order;
    std::vector<char> again;

    void clearInputs (int unit)
    {
        Unit* u = units[(size_t) unit];
        float* value = u->values();
        bool* connected = u->connected();
        const auto& desc = ports[(size_t) unit];
        const auto& isPatched = patched[(size_t) unit];
        for (size_t port = 0; port < desc.size(); ++port)
        {
            if (desc[port].dir != PortDir::In)
            {
                connected[port] = false;
                continue;
            }
            connected[port] = isPatched[port] != 0;
            if (! isPatched[port])
            {
                if (desc[port].type == PortType::Gate)
                    continue;
                value[port] = desc[port].rest;
                continue;
            }
            value[port] = 0.0f;
        }
    }

    void contribute (int unit, bool includeZeroDelayFeedback)
    {
        float* value = units[(size_t) unit]->values();
        for (int index : incoming[(size_t) unit])
        {
            const Edge& e = edges[(size_t) index];
            const bool zeroDelayFeedback = e.feedback && ! e.delayed;
            if (zeroDelayFeedback && ! includeZeroDelayFeedback)
                continue;
            float v = e.delayed ? e.held : units[(size_t) e.source]->values()[e.sourcePort];
            if (! e.destPlain)
            {
                // S-15: logic 1 is held and contributes 0 V. Logic 0 is released and contributes +5 V.
                if (e.sourceGate && ! e.destGate && ! e.sourceStrig)
                    v = v >= 0.5f ? 0.0f : 5.0f;
            }
            else if (! e.sourcePlain && e.sourceGate)
            {
                // A RONIN gate into a BUSHIDO input: high is 5 V.
                v = e.sourceStrig ? (v < 1.5f ? 5.0f : 0.0f) : (v >= 0.5f ? 5.0f : 0.0f);
            }
            value[e.destPort] += v;
        }
    }
};

void RackGraph::SnapshotDeleter::operator() (Snapshot* s) const { delete s; }

RackGraph::RackGraph() : active_ (new Snapshot()) {}
RackGraph::~RackGraph() = default;

RackGraph::SnapshotPtr RackGraph::build (const std::vector<Unit*>& units, const std::vector<GraphCable>& cables)
{
    SnapshotPtr snap (new Snapshot());
    const int n = (int) units.size();
    snap->units = units;
    snap->ports.resize ((size_t) n);
    snap->patched.resize ((size_t) n);
    snap->incoming.resize ((size_t) n);
    for (int u = 0; u < n; ++u)
    {
        const int count = units[(size_t) u]->numPorts();
        for (int p = 0; p < count; ++p)
            snap->ports[(size_t) u].push_back (units[(size_t) u]->port (p));
        snap->patched[(size_t) u].assign ((size_t) count, 0);
    }

    auto indexOf = [&units] (const Unit* unit)
    {
        const auto it = std::find (units.begin(), units.end(), unit);
        return it == units.end() ? -1 : (int) (it - units.begin());
    };

    for (const auto& c : cables)
    {
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
        snap->edges.push_back (e);
    }

    // Walk oldest to newest. An edge is feedback when it closes on the edges kept so far.
    // Only the newest feedback edge is delayed. The kept edges stay a DAG.
    std::vector<std::vector<int>> keptNext ((size_t) n);
    auto keptReaches = [&] (int from, int target)
    {
        std::vector<char> seen ((size_t) n, 0);
        std::vector<int> queue { from };
        seen[(size_t) from] = 1;
        for (size_t head = 0; head < queue.size(); ++head)
        {
            for (int next : keptNext[(size_t) queue[head]])
            {
                if (next == target)
                    return true;
                if (! seen[(size_t) next])
                {
                    seen[(size_t) next] = 1;
                    queue.push_back (next);
                }
            }
        }
        return false;
    };

    int newestFeedback = -1;
    for (size_t i = 0; i < snap->edges.size(); ++i)
    {
        auto& e = snap->edges[i];
        const bool closes = e.source == e.dest || keptReaches (e.dest, e.source);
        e.feedback = closes;
        if (closes)
            newestFeedback = (int) i;
        else
            keptNext[(size_t) e.source].push_back (e.dest);
    }
    if (newestFeedback >= 0)
        snap->edges[(size_t) newestFeedback].delayed = true;

    // Run order: Kahn's algorithm over the non-feedback edges, then any unit left unplaced.
    std::vector<std::vector<int>> adjacent ((size_t) n);
    std::vector<int> indegree ((size_t) n, 0);
    for (const auto& e : snap->edges)
    {
        if (e.feedback || e.source == e.dest)
            continue;
        auto& list = adjacent[(size_t) e.source];
        if (std::find (list.begin(), list.end(), e.dest) != list.end())
            continue;
        list.push_back (e.dest);
        ++indegree[(size_t) e.dest];
    }
    std::vector<int> queue;
    for (int u = 0; u < n; ++u)
        if (indegree[(size_t) u] == 0)
            queue.push_back (u);
    for (size_t head = 0; head < queue.size(); ++head)
    {
        const int u = queue[head];
        snap->order.push_back (u);
        for (int d : adjacent[(size_t) u])
            if (--indegree[(size_t) d] == 0)
                queue.push_back (d);
    }
    if ((int) snap->order.size() < n)
    {
        std::vector<char> placed ((size_t) n, 0);
        for (int u : snap->order)
            placed[(size_t) u] = 1;
        for (int u = 0; u < n; ++u)
            if (! placed[(size_t) u])
                snap->order.push_back (u);
    }

    snap->again.assign ((size_t) n, 0);
    for (size_t i = 0; i < snap->edges.size(); ++i)
    {
        const auto& e = snap->edges[i];
        snap->patched[(size_t) e.dest][(size_t) e.destPort] = 1;
        snap->incoming[(size_t) e.dest].push_back ((int) i);
        if (e.feedback && ! e.delayed)
            snap->again[(size_t) e.dest] = 1;
    }
    return snap;
}

void RackGraph::install (SnapshotPtr next)
{
    if (next == nullptr)
        next.reset (new Snapshot());
    for (auto& e : next->edges)
    {
        if (! e.delayed)
            continue;
        for (const auto& old : active_->edges)
        {
            if (! old.delayed)
                continue;
            if (active_->units[(size_t) old.source] != next->units[(size_t) e.source] || old.sourcePort != e.sourcePort)
                continue;
            if (active_->units[(size_t) old.dest] != next->units[(size_t) e.dest] || old.destPort != e.destPort)
                continue;
            e.held = old.held;
            break;
        }
    }
    std::swap (active_, next);
}

void RackGraph::process()
{
    Snapshot& s = *active_;
    const int n = (int) s.units.size();
    for (int u = 0; u < n; ++u)
        s.clearInputs (u);

    for (int u : s.order)
    {
        s.contribute (u, false);
        s.units[(size_t) u]->processSample();
    }

    for (int u : s.order)
    {
        if (! s.again[(size_t) u])
            continue;
        s.clearInputs (u);
        s.contribute (u, true);
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

} // namespace jidai

// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
//
// JCS v1.1 rack tests (JIDAI_Cross_Unit_Patching.md): the audit issues X2, X5 and X6 are reproduced with the v2
// rules and shown fixed with the JCS rules, plus RACK I/O (R13), path latency (R11), auto-route, BUSHIDO's
// new-instance defaults and host transport, role colours (R14) and the R10 rest values.

#include "core/Rack.h"

#include "jidai/jcs/Detect.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <random>
#include <string>
#include <vector>

using namespace jidai;

namespace {

int* gChecks = nullptr;
int* gFailures = nullptr;

void check (bool ok, const std::string& what)
{
    ++*gChecks;
    if (! ok)
    {
        ++*gFailures;
        std::printf ("FAIL %s\n", what.c_str());
    }
}

bool near (float a, float b, float tolerance = 1.0e-5f) { return std::fabs (a - b) <= tolerance; }

void run (Rack& rack, int samples, const float* inL = nullptr)
{
    std::vector<float> l ((size_t) samples), r ((size_t) samples);
    rack.process (inL, inL, l.data(), r.data(), samples);
}

// ------------------------------------------------------------------------------------------------------------------
// The v2 scheduler, kept here only to reproduce X2: walking oldest -> newest, only the NEWEST loop-closing cable is
// delayed; older loop-closing cables stay zero-delay and their destination runs a second time each sample
// (the old RackGraph::process second pass). Raw volts, no gate conversion (the X2 patch has no gates).
struct LegacyScheduler
{
    struct Edge { int s, sp, d, dp; bool feedback = false, delayed = false; float held = 0.0f; };
    std::vector<Unit*> units;
    std::vector<Edge> edges;
    std::vector<int> order;
    std::vector<char> again;
    std::vector<int> runs;

    void build (const std::vector<Unit*>& u, const std::vector<GraphCable>& cables)
    {
        units = u;
        const int n = (int) u.size();
        auto idx = [&] (const Unit* x) { return (int) (std::find (units.begin(), units.end(), x) - units.begin()); };
        std::vector<std::vector<int>> kept ((size_t) n);
        auto reaches = [&] (int from, int to)
        {
            std::vector<char> seen ((size_t) n, 0);
            std::vector<int> q { from };
            for (size_t h = 0; h < q.size(); ++h)
            {
                if (q[h] == to) return true;
                for (int nx : kept[(size_t) q[h]])
                    if (! seen[(size_t) nx]) { seen[(size_t) nx] = 1; q.push_back (nx); }
            }
            return false;
        };
        int newest = -1;
        for (const auto& c : cables)
        {
            Edge e { idx (c.source), c.sourcePort, idx (c.dest), c.destPort };
            e.feedback = e.s == e.d || reaches (e.d, e.s);
            if (e.feedback) newest = (int) edges.size();
            else kept[(size_t) e.s].push_back (e.d);
            edges.push_back (e);
        }
        if (newest >= 0) edges[(size_t) newest].delayed = true;
        std::vector<int> indeg ((size_t) n, 0);
        for (int a = 0; a < n; ++a)
            for (int b : kept[(size_t) a]) ++indeg[(size_t) b];
        for (int a = 0; a < n; ++a)
            if (indeg[(size_t) a] == 0) order.push_back (a);
        for (size_t h = 0; h < order.size(); ++h)
            for (int b : kept[(size_t) order[h]])
                if (--indeg[(size_t) b] == 0) order.push_back (b);
        again.assign ((size_t) n, 0);
        for (const auto& e : edges)
            if (e.feedback && ! e.delayed) again[(size_t) e.d] = 1;
        runs.assign ((size_t) n, 0);
    }

    void inputs (int u, bool withZeroDelayFeedback)
    {
        Unit* x = units[(size_t) u];
        for (int p = 0; p < x->numPorts(); ++p)
            if (x->port (p).dir == PortDir::In) { x->values()[p] = x->port (p).rest; x->connected()[p] = false; }
        for (const auto& e : edges)
            if (e.d == u) { x->values()[e.dp] = 0.0f; x->connected()[e.dp] = true; }
        for (const auto& e : edges)
        {
            if (e.d != u || (e.feedback && ! e.delayed && ! withZeroDelayFeedback)) continue;
            x->values()[e.dp] += e.delayed ? e.held : units[(size_t) e.s]->values()[e.sp];
        }
    }

    void process()
    {
        std::fill (runs.begin(), runs.end(), 0);
        for (int u : order) { inputs (u, false); units[(size_t) u]->processSample(); ++runs[(size_t) u]; }
        for (int u : order)
            if (again[(size_t) u]) { inputs (u, true); units[(size_t) u]->processSample(); ++runs[(size_t) u]; }
        for (auto& e : edges)
            if (e.delayed) e.held = units[(size_t) e.s]->values()[e.sp];
    }
};

// A unit that counts its runs, to show no unit runs twice per sample.
class CountingUnit : public Unit {
public:
    int numPorts() const override { return 2; }
    PortDesc port (int i) const override { return { i == 0 ? "IN" : "OUT", PortType::Audio, i == 0 ? PortDir::In : PortDir::Out }; }
    float* values() override { return v; }
    bool* connected() override { return c; }
    void processSample() override { ++runs; v[1] = 0.5f * v[0]; }
    float v[2] {};
    bool c[2] {};
    int runs = 0;
};

// A test-only device with a fixed latency on one audio path (stands in for SHOGUN at OS 2x, 23 samples): IN -> OUT
// delayed by L. It reports kind Origami only so it gets a rack id; nothing else reads the kind.
class DelayDevice : public Device {
public:
    explicit DelayDevice (int latency) : unit_ (latency)
    {
        units_.push_back (&unit_);
        for (int i = 0; i < 2; ++i)
        {
            JackDesc j;
            j.id = i == 0 ? "DELAY:IN" : "DELAY:OUT";
            j.unit = &unit_;
            j.port = i;
            j.desc = unit_.port (i);
            jacks_.push_back (j);
        }
        number = 9;
    }
    DeviceKind kind() const override { return DeviceKind::Origami; }
    void prepare (double) override {}
    int latencySamples() const override { return unit_.L; }

    struct DelayUnit : public Unit {
        explicit DelayUnit (int l) : L (l), line ((size_t) std::max (1, l), 0.0f) {}
        int numPorts() const override { return 2; }
        PortDesc port (int i) const override { return { i == 0 ? "DELAY:IN" : "DELAY:OUT", PortType::Audio, i == 0 ? PortDir::In : PortDir::Out }; }
        float* values() override { return v; }
        bool* connected() override { return c; }
        void processSample() override
        {
            if (L == 0) { v[1] = v[0]; return; }
            v[1] = line[(size_t) pos];
            line[(size_t) pos] = v[0];
            pos = (pos + 1) % L;
        }
        int L;
        std::vector<float> line;
        int pos = 0;
        float v[2] {};
        bool c[2] {};
    };
    DelayUnit unit_;
};

// A test-only plain 0/5 V style gate source (like a BUSHIDO or SHOGUN gate output) that plays a buffer.
class SourceDevice : public Device {
public:
    SourceDevice()
    {
        units_.push_back (&unit_);
        JackDesc j;
        j.id = "SRC:GATE";
        j.unit = &unit_;
        j.port = 0;
        j.desc = unit_.port (0);
        jacks_.push_back (j);
        number = 8;
    }
    DeviceKind kind() const override { return DeviceKind::Origami; }
    void prepare (double) override {}
    struct SrcUnit : public Unit {
        int numPorts() const override { return 1; }
        PortDesc port (int) const override { return { "SRC:GATE", PortType::Gate, PortDir::Out }; }
        float* values() override { return v; }
        bool* connected() override { return c; }
        bool plainVoltGates() const override { return true; }
        void processSample() override { v[0] = pos < buffer.size() ? buffer[pos++] : 0.0f; }
        std::vector<float> buffer;
        size_t pos = 0;
        float v[1] {};
        bool c[1] {};
    };
    SrcUnit unit_;
};

int firstAbove (const std::vector<float>& x, float th)
{
    for (size_t i = 0; i < x.size(); ++i)
        if (std::fabs (x[i]) > th) return (int) i;
    return -1;
}

// ------------------------------------------------------------------------------------------------------------------
// X2: two loops through one VCO. v2 rule: the older loop's cable stays zero-delay and the VCO runs twice per sample,
// so it plays 261 Hz. JCS R9: both loop cables are delayed, one pass, the VCO plays 130.8 Hz.
void testX2()
{
    auto patch = [] (Rack& rack)
    {
        rack.prepare (48000.0, 512);
        auto* r = static_cast<RoninDevice*> (rack.addDevice (DeviceKind::Ronin));
        rack.replaceInternalCables (r, {});
        rack.connect ("RONIN#1/VCO:SAW", "RONIN#1/RING:A");
        rack.connect ("RONIN#1/RING:OUT", "RONIN#1/VCO:FM 1");     // loop 1 (older); FM 1 amount is 0
        rack.connect ("RONIN#1/VCO:TRI", "RONIN#1/INV:IN");
        rack.connect ("RONIN#1/INV:OUT", "RONIN#1/VCO:PWM");       // loop 2 (newest)
        return r;
    };
    auto countHz = [] (auto&& step, auto&& saw)
    {
        int wraps = 0;
        float prev = saw();
        for (int i = 0; i < 48000; ++i)
        {
            step();
            const float now = saw();
            if (prev - now > 5.0f) ++wraps;      // the saw falls through its wrap
            prev = now;
        }
        return wraps;
    };

    // v2 scheduler over the same RONIN units and cables.
    {
        Rack rack;
        auto* r = patch (rack);
        std::vector<GraphCable> cables;
        for (const auto& c : rack.cables())
        {
            Device* da; Device* db; int ja, jb;
            rack.resolve (c.a, da, ja);
            rack.resolve (c.b, db, jb);
            const auto& A = da->jacks()[(size_t) ja];
            const auto& B = db->jacks()[(size_t) jb];
            cables.push_back ({ A.unit, A.port, B.unit, B.port });
        }
        LegacyScheduler old;
        old.build (r->units(), cables);
        const int vco = r->findJack ("VCO:SAW");
        Unit* vcoUnit = r->jacks()[(size_t) vco].unit;
        const int port = r->jacks()[(size_t) vco].port;
        const int hz = countHz ([&] { old.process(); }, [&] { return vcoUnit->values()[port]; });
        int most = 0;
        for (int x : old.runs) most = std::max (most, x);
        std::printf ("INFO X2 v2 rule: VCO %d Hz, max runs per sample %d\n", hz, *std::max_element (old.runs.begin(), old.runs.end()));
        check (hz >= 259 && hz <= 263, "X2 reproduced: the v2 rule plays the VCO at " + std::to_string (hz) + " Hz (expected about 261)");
        check (most == 2, "X2 reproduced: the v2 rule runs a unit twice per sample");
    }
    // JCS R9.
    {
        Rack rack;
        patch (rack);
        check (rack.delayedCableCount() == 2, "X2 fixed: both loop-closing cables are delayed one sample");
        check (rack.unitRunsPerSample() == 1, "X2 fixed: every unit runs once per sample");
        std::vector<float> l (1), rr (1);
        const int hz = countHz ([&] { rack.process (nullptr, nullptr, l.data(), rr.data(), 1); },
                                [&] { return rack.jackVolts ("RONIN#1/VCO:SAW"); });
        std::printf ("INFO X2 JCS R9: VCO %d Hz, delayed cables %d, runs per sample %d\n", hz, rack.delayedCableCount(), rack.unitRunsPerSample());
        check (hz >= 129 && hz <= 132, "X2 fixed: the VCO plays " + std::to_string (hz) + " Hz (expected 130.8)");
    }
    // A counting unit in a two-cable loop with a third cable: still one run per sample.
    {
        CountingUnit a, b;
        RackGraph g;
        g.install (RackGraph::build ({ &a, &b }, { { &a, 1, &b, 0 }, { &b, 1, &a, 0 }, { &a, 1, &a, 0 } }));
        for (int i = 0; i < 100; ++i) g.process();
        check (a.runs == 100 && b.runs == 100, "R9.4: a unit in two loops runs exactly once per sample");
        check (g.delayedCableCount() == 2, "R9: both loop-closing cables delayed (B -> A and the self patch)");
    }
}

// ------------------------------------------------------------------------------------------------------------------
// X5: a 23-sample device feeding a zero-latency device, both to the host. The per-device rule delays the second
// device's host contribution by Lmax - L = 23 on top of the 23 it already carries (46). R11 per-path: 23.
void testX5()
{
    for (int variant = 0; variant < 2; ++variant)
    {
        Rack rack;
        rack.prepare (48000.0, 512);
        rack.addDevice (DeviceKind::RackIO);
        auto* r = static_cast<RoninDevice*> (rack.addDevice (DeviceKind::Ronin));
        rack.replaceInternalCables (r, {});
        std::string src;
        int L = 23;
        if (variant == 0)
        {
            auto* d = rack.adoptDevice (std::make_unique<DelayDevice> (23));
            src = d->rackId() + "/DELAY";
            rack.connect ("RACK#1/HOST:IN L", src + ":IN");
            src += ":OUT";
        }
        else
        {
            auto* o = static_cast<OrigamiDevice*> (rack.addDevice (DeviceKind::Origami));
            o->setParam (origami::kQuality, 1.0);       // 2x: 46 samples (23 per halfband direction)
            L = 46;
            rack.connect ("RACK#1/HOST:IN L", "ORIGAMI#1/IN:IN L");
            src = "ORIGAMI#1/OUT:OUT L";
        }
        rack.connect (src, "RACK#1/MAIN:OUT L");              // the device straight to the host
        rack.connect (src, "RONIN#1/MIX:IN 1");                // and through RONIN's mixer (L = 0)
        rack.connect ("RONIN#1/MIX:OUT", "RACK#1/MAIN:OUT R");
        rack.updateLatency();
        const std::string name = variant == 0 ? "X5 (23-sample device)" : "X5 (ORIGAMI 2x)";

        check (rack.latency() == L, name + ": rack latency = " + std::to_string (rack.latency()) + ", expected " + std::to_string (L));
        check (rack.pathLatency (r) == L, name + ": RONIN's path latency carries the upstream " + std::to_string (L));
        int compRonin = -1, compDirect = -1;
        for (size_t i = 0; i < rack.cables().size(); ++i)
        {
            if (rack.cables()[i].a == "RONIN#1/MIX:OUT") compRonin = rack.cableInfo()[i].comp;
            if (rack.cables()[i].a == src && rack.cables()[i].b == "RACK#1/MAIN:OUT L") compDirect = rack.cableInfo()[i].comp;
        }
        check (compRonin == 0 && compDirect == 0, name + ": no compensation needed on either path");

        // Measure: an impulse in, and where it leaves on L (direct) and R (via RONIN).
        const int n = 256;
        std::vector<float> in ((size_t) n, 0.0f), l ((size_t) n), rr ((size_t) n);
        in[0] = 0.1f;
        run (rack, 4096);                  // settle the ORIGAMI smoothing
        rack.process (in.data(), nullptr, l.data(), rr.data(), n);
        const int atL = firstAbove (l, 1.0e-3f), atR = firstAbove (rr, 1.0e-4f);
        const int perDevice = L + (L - 0);                    // v2 / SHOGUN v2.1 §13.10 rule
        std::printf ("INFO %s: direct %d, via RONIN %d (per-device rule %d), rack latency %d\n", name.c_str(), atL, atR, perDevice, rack.latency());
        check (atL == L, name + ": direct path leaves at sample " + std::to_string (atL));
        check (atR == L, name + ": the path through RONIN leaves at sample " + std::to_string (atR) + " (per-device rule: " + std::to_string (perDevice) + ")");
    }

    // A path that is shorter than the longest gets compensated: RONIN fed by host directly, plus a 23-sample device.
    Rack rack;
    rack.prepare (48000.0, 512);
    rack.addDevice (DeviceKind::RackIO);
    auto* r = static_cast<RoninDevice*> (rack.addDevice (DeviceKind::Ronin));
    rack.replaceInternalCables (r, {});
    auto* d = rack.adoptDevice (std::make_unique<DelayDevice> (23));
    rack.connect ("RACK#1/HOST:IN L", d->rackId() + "/DELAY:IN");
    rack.connect (d->rackId() + "/DELAY:OUT", "RACK#1/MAIN:OUT L");
    rack.connect ("RACK#1/HOST:IN L", "RONIN#1/MIX:IN 1");
    rack.connect ("RONIN#1/MIX:OUT", "RACK#1/MAIN:OUT R");
    int comp = -1;
    for (size_t i = 0; i < rack.cables().size(); ++i)
        if (rack.cables()[i].a == "RONIN#1/MIX:OUT") comp = rack.cableInfo()[i].comp;
    check (rack.latency() == 23 && comp == 23, "R11: the zero-latency path into MAIN OUT gets +23 comp");
    const int n = 128;
    std::vector<float> in ((size_t) n, 0.0f), l ((size_t) n), rr ((size_t) n);
    run (rack, 64);
    in[0] = 0.1f;
    rack.process (in.data(), nullptr, l.data(), rr.data(), n);
    check (firstAbove (l, 1.0e-3f) == 23 && firstAbove (rr, 1.0e-4f) == 23, "R11: both paths reach the host aligned at 23");

    // Skew badge: a device input fed from two paths of different latency.
    rack.connect (d->rackId() + "/DELAY:OUT", "RONIN#1/MIX:IN 2");
    int skew = -1;
    for (size_t i = 0; i < rack.cables().size(); ++i)
        if (rack.cables()[i].a == "RACK#1/HOST:IN L" && rack.cables()[i].b == "RONIN#1/MIX:IN 1") skew = rack.cableInfo()[i].skew;
    check (skew == 23, "R11.6: the early input to RONIN's mixer shows a 23-sample skew badge");
}

// ------------------------------------------------------------------------------------------------------------------
// X6: a noisy 2 Hz ramp (2.5 V sine + 0.8 V, 50 mV noise) as a plain gate into RONIN EG 1 TRIG (S-trig).
// v2 (S-15 per sample at 0.5 V, no hysteresis) chatters; JCS R3 Schmitt 1.0/0.5 on the cable gives exactly 2 holds.
void testX6()
{
    std::mt19937 rng (1);
    std::normal_distribution<float> noise (0.0f, 0.05f);
    std::vector<float> sig (48000);
    for (int i = 0; i < 48000; ++i)
        sig[(size_t) i] = std::clamp (2.5f * (float) std::sin (2.0 * M_PI * 2.0 * i / 48000.0) + 0.8f, -5.0f, 5.0f) + noise (rng);

    for (int legacy = 0; legacy < 2; ++legacy)
    {
        Rack rack;
        rack.prepare (48000.0, 512);
        auto* r = static_cast<RoninDevice*> (rack.addDevice (DeviceKind::Ronin));
        rack.replaceInternalCables (r, {});
        auto* src = static_cast<SourceDevice*> (rack.adoptDevice (std::make_unique<SourceDevice>()));
        src->unit_.buffer = sig;
        CableSpec c { src->rackId() + "/SRC:GATE", "RONIN#1/EG 1:TRIG" };
        c.legacyInvert = legacy == 1;
        rack.setCables ({ c });
        int holds = 0;
        bool held = false;
        std::vector<float> l (1), rr (1);
        for (int i = 0; i < 48000; ++i)
        {
            rack.process (nullptr, nullptr, l.data(), rr.data(), 1);
            const float v = rack.jackVolts ("RONIN#1/EG 1:TRIG");
            if (! held && v < jidai::jcs::kStrigHeld) { held = true; ++holds; }
            else if (held && v > jidai::jcs::kStrigRelease) held = false;
        }
        std::printf ("INFO X6 %s: %d holds\n", legacy ? "v2 law" : "JCS R3/R3s", holds);
        if (legacy == 1)
            check (holds > 10, "X6 reproduced: the v2 law chatters, " + std::to_string (holds) + " holds on a 2 Hz ramp");
        else
            check (holds == 2, "X6 fixed: R3 Schmitt per cable gives exactly 2 holds, got " + std::to_string (holds));
    }
}

// ------------------------------------------------------------------------------------------------------------------
void testRackIO()
{
    Rack rack;
    rack.prepare (48000.0, 64);
    Device* io = rack.addDevice (DeviceKind::RackIO);
    check (io != nullptr && io->rackId() == "RACK#1" && io->title() == "RACK I/O 1", "RACK I/O: RACK#1, titled RACK I/O 1");
    check (io->jacks().size() == 11, "RACK I/O: 11 back-only jacks");
    check (rack.addDevice (DeviceKind::RackIO) == nullptr, "RACK I/O: exactly one");
    Device* b = rack.addDevice (DeviceKind::Bushido, 0);
    check (rack.indexOf (io) == 0 && rack.indexOf (b) == 1, "RACK I/O stays at the top");
    check (! rack.moveDevice (io, 1) && ! rack.removeDevice (io), "RACK I/O cannot be moved or removed");
    rack.moveDevice (b, 0);
    check (rack.indexOf (b) == 1, "nothing goes above RACK I/O");

    // Unpatched: silent. Patched HOST IN -> MAIN OUT: the host hears itself (x5 then /5 in float).
    std::vector<float> in (64, 0.3f), l (64), r (64);
    rack.process (in.data(), in.data(), l.data(), r.data(), 64);
    check (l[10] == 0.0f && r[10] == 0.0f, "RACK I/O: nothing patched to MAIN OUT is silence");
    rack.connect ("RACK#1/HOST:IN L", "RACK#1/MAIN:OUT L");
    rack.process (in.data(), in.data(), l.data(), r.data(), 64);
    check (near (l[10], 0.3f, 1.0e-6f) && near (r[10], 0.3f, 1.0e-6f), "HOST IN L -> MAIN OUT L, R normalled to L");
    check (rack.delayedCableCount() == 0, "HOST IN -> MAIN OUT is not a loop (two units)");
    static_cast<RackIODevice*> (io)->setMainLevel (0.5f);
    rack.process (in.data(), in.data(), l.data(), r.data(), 64);
    check (near (l[10], 0.15f, 1.0e-6f), "MAIN level 0.5 halves the output");

    // MIDI -> CV (JCS R4: 0 V = C3 = MIDI 48), velocity, gate.
    std::vector<MidiNote> midi { { 4, 60, 127, true }, { 40, 60, 0, false } };
    std::vector<float> note, lin, gate, vel;
    for (int i = 0; i < 64; ++i)
    {
        // one sample per call, so the events are re-timed to sample 0 of the call that carries them
        std::vector<MidiNote> now;
        for (auto m : midi)
            if (m.sample == i) { m.sample = 0; now.push_back (m); }
        rack.process (&in[0], &in[0], l.data(), r.data(), 1, now.data(), (int) now.size());
        note.push_back (rack.jackVolts ("RACK#1/MIDI:NOTE"));
        lin.push_back (rack.jackVolts ("RACK#1/MIDI:HZ/V LIN"));
        gate.push_back (rack.jackVolts ("RACK#1/MIDI:GATE"));
        vel.push_back (rack.jackVolts ("RACK#1/MIDI:VEL"));
    }
    check (gate[3] == 0.0f && gate[4] == 5.0f && gate[39] == 5.0f && gate[40] == 0.0f, "MIDI GATE: 0/5 V from note on to note off (R2)");
    check (near (note[10], 1.0f) && near (lin[10], 2.0f) && near (vel[10], 5.0f), "MIDI 60: NOTE 1 V (V/OCT), HZ/V LIN 2 V, VEL 5 V");
    check (near (note[50], 1.0f), "MIDI NOTE holds the last note after release");
    check (io->jackRole (RackIODevice::MidiNoteJack) == jidai::jcs::Role::VOct && io->jackRole (RackIODevice::MidiHzv) == jidai::jcs::Role::HzvLin,
           "roles: MIDI NOTE is V/OCT, MIDI HZ/V LIN is HZ/V LIN");

    // Transport: 120 BPM = 2 quarter notes/s = 8 sixteenths/s, RUN high while playing, one RESET pulse at start.
    Transport t;
    t.valid = true;
    t.playing = true;
    t.bpm = 120.0;
    t.ppq = 0.0;
    rack.setTransport (t);
    int clocks = 0, resets = 0;
    bool clkPrev = false, resetPrev = false, runAlways = true;
    for (int block = 0; block < 48000 / 64; ++block)
    {
        for (int i = 0; i < 64; ++i)
        {
            rack.process (&in[0], &in[0], l.data(), r.data(), 1);
            const bool clk = rack.jackVolts ("RACK#1/TRANSPORT:CLK 1/16") > 1.0f;
            const bool rst = rack.jackVolts ("RACK#1/TRANSPORT:RESET") > 1.0f;
            runAlways = runAlways && rack.jackVolts ("RACK#1/TRANSPORT:RUN") == 5.0f;
            clocks += clk && ! clkPrev;
            resets += rst && ! resetPrev;
            clkPrev = clk;
            resetPrev = rst;
        }
    }
    std::printf ("INFO transport: %d clocks, %d resets\n", clocks, resets);
    check (clocks == 8, "TRANSPORT CLK 1/16: 8 pulses in 1 s at 120 BPM, got " + std::to_string (clocks));
    check (resets == 1 && runAlways, "TRANSPORT: RUN 5 V while playing, one RESET pulse at start");
}

void testAutoRouteAndBushidoDefaults()
{
    Rack rack;
    rack.prepare (48000.0, 64);
    rack.addDevice (DeviceKind::RackIO);
    Transport t;
    t.valid = true;
    t.playing = true;
    t.bpm = 120.0;
    rack.setTransport (t);

    auto* b = static_cast<BushidoDevice*> (rack.insertNew (DeviceKind::Bushido));
    check (b->param ("CLOCK:SOURCE") == 1.0f && b->param ("CLOCK:EXT SOURCE") == 1.0f,
           "insertNew BUSHIDO while the host plays: SOURCE EXT, EXT SOURCE HOST (applyNewInstanceDefaults)");
    auto* added = static_cast<BushidoDevice*> (rack.addDevice (DeviceKind::Bushido));
    check (added->param ("CLOCK:SOURCE") == 0.0f, "addDevice (state loading) leaves BUSHIDO's SOURCE alone");
    rack.removeDevice (added);

    Device* r = rack.insertNew (DeviceKind::Ronin);
    auto has = [&] (const std::string& a, const std::string& bb, bool autoFlag)
    {
        for (const auto& c : rack.cables())
            if (c.a == a && c.b == bb) return c.autoRouted == autoFlag;
        return false;
    };
    check (has ("RONIN#1/HOST:OUT L", "RACK#1/MAIN:OUT L", true) && has ("RONIN#1/HOST:OUT R", "RACK#1/MAIN:OUT R", true),
           "auto-route: RONIN HOST OUT -> MAIN OUT, marked auto");
    check (has ("BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:V/OCT", true) && has ("BUSHIDO#1/OUTPUTS:GATE A", "RONIN#1/EG 1:TRIG", true),
           "auto-route: RONIN under BUSHIDO gets CV A -> V/OCT (row law V/OCT) and GATE A -> EG 1 TRIG");

    // HOST clock: BUSHIDO follows the host transport through setTransport.
    const int before = b->engine().currentStep();
    std::vector<float> l (64), rr (64);
    for (int i = 0; i < 48000 / 64; ++i)
    {
        rack.process (nullptr, nullptr, l.data(), rr.data(), 64);
        t.ppq += 2.0 * 64.0 / 48000.0;
        rack.setTransport (t);
    }
    check (b->engine().isRunning() && b->engine().currentStep() != before, "BUSHIDO on HOST clock runs from the host transport");

    Device* o = rack.insertNew (DeviceKind::Origami);
    check (has ("RACK#1/HOST:IN L", "ORIGAMI#1/HOST:IN L", true) && has ("ORIGAMI#1/HOST:OUT R", "RACK#1/MAIN:OUT R", true),
           "auto-route: ORIGAMI gets HOST IN and its HOST OUT to MAIN OUT");
    const auto count = rack.cables().size();
    rack.insertNew (DeviceKind::Origami, -1, false);
    check (rack.cables().size() == count, "Shift-insert: no auto-route");

    // ORIGAMI 2x: latency 46 after updateLatency, and RONIN's MAIN OUT cables carry +46.
    static_cast<OrigamiDevice*> (o)->setParam (origami::kQuality, 1.0);
    check (rack.updateLatency() && rack.latency() == 46, "ORIGAMI 2x: rack latency 46 after updateLatency()");
    int comp = -1;
    for (size_t i = 0; i < rack.cables().size(); ++i)
        if (rack.cables()[i].a == "RONIN#1/HOST:OUT L") comp = rack.cableInfo()[i].comp;
    check (comp == 46, "ORIGAMI 2x: RONIN's host path gets +46 comp");
    check (! rack.updateLatency(), "updateLatency() is a no-op when nothing changed");

    // Roles and badges (R14, R4.3, R3s).
    for (size_t i = 0; i < rack.cables().size(); ++i)
    {
        const auto& c = rack.cables()[i];
        const auto& info = rack.cableInfo()[i];
        if (c.a == "BUSHIDO#1/OUTPUTS:CV A")
            check (info.role == jidai::jcs::Role::VOct && info.badge == jidai::jcs::Badge::None, "CV A (V/OCT law) -> VCO V/OCT: V/OCT role, no badge");
        if (c.a == "BUSHIDO#1/OUTPUTS:GATE A")
            check (info.role == jidai::jcs::Role::GateClk && info.badge == jidai::jcs::Badge::GateToStrig, "GATE A -> EG 1 TRIG: GATE/CLK role, converted badge");
        if (c.a == "RONIN#1/HOST:OUT L")
            check (info.role == jidai::jcs::Role::Audio, "HOST OUT: AUDIO role");
    }
    rack.connect ("BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V");
    check (rack.cableInfo().back().badge == jidai::jcs::Badge::PitchLaw, "V/OCT -> HZ/V LIN shows the not-equal badge (R4.3)");
    b->setParam ("STEPS:LAW A", 1.0f);
    check (rack.jackRole ("BUSHIDO#1/OUTPUTS:CV A") == jidai::jcs::Role::HzvLin, "row law HZ/V LIN: CV A takes the HZ/V LIN role");
    check (rack.jackRole ("RONIN#1/EG 1:TRIG") == jidai::jcs::Role::STrig && rack.jackRole ("RONIN#1/EXT IN:GATE") == jidai::jcs::Role::STrig,
           "S-TRIG role on EG TRIG inputs and EXT IN GATE");
    (void) r;
}

// R10: unpatched inputs sit at rest every sample, nothing latches; the S-trig inputs rest at +5 V.
void testRestNoLatch()
{
    Rack rack;
    rack.prepare (48000.0, 64);
    auto* r = static_cast<RoninDevice*> (rack.addDevice (DeviceKind::Ronin));
    rack.replaceInternalCables (r, {});
    auto* src = static_cast<SourceDevice*> (rack.adoptDevice (std::make_unique<SourceDevice>()));
    src->unit_.buffer.assign (1000, 5.0f);
    rack.connect (src->rackId() + "/SRC:GATE", "RONIN#1/S&H:CLOCK");
    run (rack, 64);
    check (near (rack.jackVolts ("RONIN#1/S&H:CLOCK"), 5.0f), "patched S&H CLOCK reads the gate");
    rack.setCables ({});
    run (rack, 4);
    check (near (rack.jackVolts ("RONIN#1/S&H:CLOCK"), 0.0f), "R10: unpatched gate input drops to rest, no latching");
    check (near (rack.jackVolts ("RONIN#1/EG 2:TRIG"), 5.0f), "R10/R3s: unpatched EG 2 TRIG rests at +5 V");
}

}

void runJcsRackTests (int& checks, int& failures)
{
    gChecks = &checks;
    gFailures = &failures;
    testX2();
    testX5();
    testX6();
    testRackIO();
    testAutoRouteAndBushidoDefaults();
    testRestNoLatch();
}

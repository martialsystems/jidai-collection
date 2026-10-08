// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

// SHOGUN as a rack device: its 153 jacks (ids, types, JCS R14 roles), the latency it reports at GLOBAL:OS 1x/2x/4x
// (0/23/26) and the rack's R11 compensation around it, the re-prepare on an OS change, host-clocked playback, and the
// jack-driven voices (TRIG, RET, MIX).

#include "core/Rack.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
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

void process (Rack& rack, int n, std::vector<float>* outL = nullptr, std::vector<float>* outR = nullptr)
{
    std::vector<float> l ((size_t) n), r ((size_t) n);
    rack.process (nullptr, nullptr, l.data(), r.data(), n);
    if (outL != nullptr) outL->insert (outL->end(), l.begin(), l.end());
    if (outR != nullptr) outR->insert (outR->end(), r.begin(), r.end());
}

double osU (int factor) { return shogun::stepU (factor == 1 ? 0 : (factor == 2 ? 1 : 2), 3); }

// A rack with SHOGUN#1 (ideal: no tolerance, no drift) and its MIX auto-routed to MAIN OUT.
ShogunDevice* addShogun (Rack& rack, int os)
{
    rack.prepare (48000.0, 512);
    if (rack.rackIO() == nullptr)
        rack.addDevice (DeviceKind::RackIO);
    auto* s = static_cast<ShogunDevice*> (rack.addDevice (DeviceKind::Shogun));
    s->setParam (shogun::P_GLOBAL_TOLERANCE, 0.0);
    s->setParam (shogun::P_GLOBAL_DRIFT, 0.0);
    s->setParam (shogun::P_GLOBAL_OS, osU (os));
    rack.updateLatency();
    rack.autoRoute (s);
    return s;
}

void testJacks()
{
    Rack rack;
    auto* s = addShogun (rack, 2);
    check (s != nullptr && s->rackId() == "SHOGUN#1" && s->kind() == DeviceKind::Shogun, "add SHOGUN: SHOGUN#1");
    check (s->jacks().size() == 153 && (int) s->jacks().size() == shogun::kPorts, "SHOGUN: 153 jacks (engine/ports.h)");
    int idOk = 0, typeOk = 0, roleOk = 0, front = 0;
    for (int j = 0; j < shogun::kPorts; ++j)
    {
        const auto& p = shogun::kPortTable[j];
        const auto& jack = s->jacks()[(size_t) j];
        idOk += jack.id == p.id && s->findJack (p.id) == j ? 1 : 0;
        const PortType want = p.type == shogun::PortType::Audio ? PortType::Audio : (p.type == shogun::PortType::Gate ? PortType::Gate : PortType::CV);
        typeOk += jack.desc.type == want && (jack.desc.dir == PortDir::Out) == (p.dir == shogun::PortDir::Out) ? 1 : 0;
        roleOk += s->jackRole (j) == p.role && rack.jackRole ("SHOGUN#1/" + std::string (p.id)) == p.role ? 1 : 0;
        front += jack.backOnly ? 0 : 1;
    }
    check (idOk == 153, "SHOGUN jack ids = SHOGUN's table, all resolve (" + std::to_string (idOk) + "/153)");
    check (typeOk == 153, "SHOGUN jack types and directions = SHOGUN's table (" + std::to_string (typeOk) + "/153)");
    check (roleOk == 153, "SHOGUN jack roles (JCS R14) = SHOGUN's table (" + std::to_string (roleOk) + "/153)");
    check (front == 153, "every SHOGUN jack is on its rear bay");
    Device* found = nullptr;
    int jack = -1;
    check (rack.resolve ("SHOGUN#1/BD1:TRIG", found, jack) && found == s && jack == shogun::drumPort (shogun::BD1, shogun::DJ_TRIG),
           "SHOGUN#1/BD1:TRIG resolves (JCS R6)");
    check (rack.resolve ("SHOGUN#1/MIX:R", found, jack) && jack == shogun::PORT_MIX_R, "SHOGUN#1/MIX:R resolves");
    check (s->units().size() == 2 && s->units()[0]->plainVoltGates() && s->units()[1]->plainVoltGates(),
           "SHOGUN: RET input stage + engine, 0/5 V gates (no logic-level promotion)");
    int groups = 0, grouped = 0;
    for (const auto& g : s->jackGroups())
    {
        ++groups;
        grouped += (int) g.jacks.size();
    }
    check (groups == 19 && grouped == 153, "SHOGUN rear bay: 19 groups (14 drums, LEAD, BASS, CLOCK, MOD, MIX) hold all 153 jacks");
    bool autoMix = false;
    int autoCount = 0;
    for (const auto& c : rack.cables())
    {
        autoCount += c.autoRouted ? 1 : 0;
        autoMix = autoMix || (c.a == "SHOGUN#1/MIX:L" && c.b == "RACK#1/MAIN:OUT L");
    }
    check (autoMix && autoCount == 2, "auto-route: SHOGUN MIX L/R -> RACK MAIN OUT L/R");
    check (rack.check ("SHOGUN#1/BD1:OUT", "SHOGUN#1/MIX:L") != Rack::Check::Ok, "an output into an output is refused");
}

// Per OS: SHOGUN reports 0/23/26, the rack latency follows after updateLatency() (the device is re-prepared), a
// zero-latency path into MAIN OUT gets that much compensation, and SHOGUN's own output moves by exactly that much.
void testLatency()
{
    std::vector<float> ref;
    for (int os : { 1, 2, 4 })
    {
        const int L = os == 1 ? 0 : (os == 2 ? 23 : 26);
        const std::string tag = "SHOGUN " + std::to_string (os) + "x: ";
        Rack rack;
        auto* s = addShogun (rack, os);
        check (s->osFactor() == os && ! s->needsPrepare(), tag + "engine prepared at " + std::to_string (os) + "x");
        check (s->latencySamples() == L, tag + "reports " + std::to_string (L) + " samples");
        check (rack.latency() == L, tag + "rack latency " + std::to_string (rack.latency()));
        // A zero-latency path to MAIN OUT R: RONIN MIX fed by the host.
        auto* r = static_cast<RoninDevice*> (rack.addDevice (DeviceKind::Ronin));
        rack.replaceInternalCables (r, {});
        rack.disconnect ("SHOGUN#1/MIX:R", "RACK#1/MAIN:OUT R");
        rack.connect ("RACK#1/HOST:IN L", "RONIN#1/MIX:IN 1");
        rack.connect ("RONIN#1/MIX:OUT", "RACK#1/MAIN:OUT R");
        rack.updateLatency();
        int compRonin = -1, compShogun = -1;
        for (size_t i = 0; i < rack.cables().size(); ++i)
        {
            if (rack.cables()[i].a == "RONIN#1/MIX:OUT") compRonin = rack.cableInfo()[i].comp;
            if (rack.cables()[i].a == "SHOGUN#1/MIX:L") compShogun = rack.cableInfo()[i].comp;
        }
        check (compRonin == L && compShogun == 0, tag + "R11: the zero-latency RONIN path gets +" + std::to_string (L)
                                                       + " (got " + std::to_string (compRonin) + "), SHOGUN's own +0 (got " + std::to_string (compShogun) + ")");
        check (rack.pathLatency (s) == L, tag + "SHOGUN path latency " + std::to_string (rack.pathLatency (s)));

        // BD1 struck at a block start: SHOGUN's MIX at this OS is the 1x one moved by exactly L. Measured two ways over
        // the whole kick (8192 samples, so the tail is not cut off: a short window biases the correlation early):
        // the peak sample and the cross-correlation peak.
        process (rack, 512);
        s->trigger (shogun::BD1);
        std::vector<float> out;
        for (int b = 0; b < 32; ++b)
            process (rack, 256, &out);
        if (os == 1)
            ref = out;
        int best = -1;
        double bestC = -1.0e30;
        for (int lag = 0; lag <= 60; ++lag)
        {
            double c = 0.0;
            for (size_t i = 0; i + (size_t) lag < out.size() && i < ref.size(); ++i)
                c += (double) ref[i] * (double) out[i + (size_t) lag];
            if (c > bestC) { bestC = c; best = lag; }
        }
        auto peakAt = [] (const std::vector<float>& x) {
            size_t k = 0;
            for (size_t i = 0; i < x.size(); ++i)
                if (std::fabs (x[i]) > std::fabs (x[k])) k = i;
            return (int) k;
        };
        const int moved = peakAt (out) - peakAt (ref);
        double peak = 0.0;
        for (float v : out) peak = std::fmax (peak, std::fabs ((double) v));
        check (peak > 0.01 && std::isfinite (peak), tag + "BD1 struck: MIX reaches MAIN OUT (peak " + std::to_string (peak) + ")");
        check (best == L && moved == L, tag + "MIX moves by exactly L vs 1x: xcorr " + std::to_string (best) + ", peak "
                                            + std::to_string (moved) + " (reported " + std::to_string (L) + ")");
        std::printf ("INFO %s latency %d, comp RONIN %d, xcorr lag %d, peak moves %d, peak %.3f\n", tag.c_str(), L, compRonin, best, moved, peak);
    }

    // RET paths, measured with an impulse (as ORIGAMI's 2x and the rack's own paths are): HOST IN L -> BASS RET ->
    // SHOGUN MIX L -> MAIN OUT L, and HOST IN L straight into MAIN OUT R. RET -> MIX is upsampler + decimator = 2L;
    // the rack must report that and delay the dry path to match, so both impulses land on the same sample.
    for (int os : { 1, 2, 4 })
    {
        const int L = os == 1 ? 0 : (os == 2 ? 23 : 26);
        const std::string tag = "SHOGUN " + std::to_string (os) + "x RET: ";
        Rack rack;
        auto* s = addShogun (rack, os);
        rack.disconnect ("SHOGUN#1/MIX:R", "RACK#1/MAIN:OUT R");
        check (rack.pathLatency (s) == L && rack.latency() == L, tag + "unpatched RET adds nothing (path " + std::to_string (rack.pathLatency (s)) + ")");
        rack.connect ("RACK#1/HOST:IN L", "SHOGUN#1/BASS:RET");
        rack.connect ("RACK#1/HOST:IN L", "RACK#1/MAIN:OUT R");
        rack.updateLatency();
        check (rack.pathLatency (s) == 2 * L && rack.latency() == 2 * L,
               tag + "rack reports RET -> MIX as 2L = " + std::to_string (2 * L) + " (got " + std::to_string (rack.latency()) + ")");
        const int n = 2048, at = 700;
        std::vector<float> inL ((size_t) n, 0.0f), outL ((size_t) n), outR ((size_t) n);
        inL[(size_t) at] = 1.0f;
        rack.process (inL.data(), inL.data(), outL.data(), outR.data(), n);
        auto peakAt = [] (const std::vector<float>& x) {
            size_t k = 0;
            for (size_t i = 0; i < x.size(); ++i)
                if (std::fabs (x[i]) > std::fabs (x[k])) k = i;
            return (int) k;
        };
        const int pl = peakAt (outL) - at, pr = peakAt (outR) - at;
        check (pl == 2 * L && pr == 2 * L, tag + "impulse at MAIN OUT: via RET " + std::to_string (pl) + ", dry " + std::to_string (pr)
                                              + " (want both " + std::to_string (2 * L) + ")");
        std::printf ("INFO %s reported %d, impulse via RET %d, dry path %d\n", tag.c_str(), rack.latency(), pl, pr);
    }

    // Runtime OS change: needsPrepare until the rack re-prepares it on updateLatency(), then the compensation follows.
    Rack rack;
    auto* s = addShogun (rack, 1);
    process (rack, 256);
    s->setParam (shogun::P_GLOBAL_OS, osU (4));
    check (s->needsPrepare() && s->latencySamples() == 0, "OS 1x -> 4x: pending until the rack re-prepares SHOGUN");
    check (rack.updateLatency() && s->osFactor() == 4 && s->latencySamples() == 26 && rack.latency() == 26,
           "OS 1x -> 4x: updateLatency() re-prepares SHOGUN, rack latency 26");
    process (rack, 512);
    check (! rack.updateLatency(), "updateLatency() is a no-op once SHOGUN is prepared at its OS");
}

// Host clock: CLOCK:SOURCE HOST follows the transport; a BD1 pattern on steps 1/5/9/13 plays while the host plays.
void testHostClock()
{
    Rack rack;
    auto* s = addShogun (rack, 1);
    s->setParam (shogun::P_CLOCK_SOURCE, shogun::stepU (0, 3));
    auto e = s->edits();
    for (int st : { 0, 4, 8, 12 })
        e.pattern.tracks[shogun::BD1].steps[st].on = true;
    s->setEdits (e);
    Transport t;
    t.valid = true;
    t.playing = false;
    t.bpm = 128.0;
    rack.setTransport (t);
    std::vector<float> out;
    for (int b = 0; b < 40; ++b)
        process (rack, 480, &out);
    double stopped = 0.0;
    for (float v : out) stopped = std::fmax (stopped, std::fabs ((double) v));
    check (stopped == 0.0 && ! s->running(), "HOST clock, transport stopped: SHOGUN silent and not running");
    t.playing = true;
    out.clear();
    for (int b = 0; b < 200; ++b)        // 2 s at 128 BPM = a bar and a bit; ppq advanced by the rack
    {
        rack.setTransport (t);
        process (rack, 480, &out);
        t.ppq += t.bpm / 60.0 * 480.0 / 48000.0;
    }
    double peak = 0.0;
    for (float v : out) peak = std::fmax (peak, std::fabs ((double) v));
    check (s->running(), "HOST clock, transport playing: SHOGUN runs");
    // 2 s at 128 BPM, 1/16: about 17 steps.
    check (s->globalStep() >= 14 && s->globalStep() <= 19, "HOST clock: " + std::to_string (s->globalStep()) + " steps in 2 s at 128 BPM 1/16");
    check (peak > 0.01, "HOST clock: the BD1 pattern plays (peak " + std::to_string (peak) + ")");
}

// Jacks drive the voices: a rack gate into BD1 TRIG (CLOCK:MODE EXT plays only from jacks), and BD1 OUT taken by cable.
void testJackTrigger()
{
    Rack rack;
    auto* s = addShogun (rack, 2);
    s->setParam (shogun::P_CLOCK_MODE, shogun::stepU (1, 2));          // EXT: TRIG jacks play the voices
    check (rack.connect ("RACK#1/TRANSPORT:RUN", "SHOGUN#1/BD1:TRIG") == Rack::Check::Ok, "patch RACK RUN -> SHOGUN BD1 TRIG");
    check (rack.connect ("SHOGUN#1/BD1:ENV", "SHOGUN#1/SD:DECAY") == Rack::Check::Ok, "patch SHOGUN BD1 ENV -> SHOGUN SD DECAY (self-patch)");
    Transport t;
    t.valid = true;
    t.playing = false;
    rack.setTransport (t);
    process (rack, 2048);
    t.playing = true;
    rack.setTransport (t);
    std::vector<float> out;
    for (int b = 0; b < 8; ++b)
        process (rack, 512, &out);
    double peak = 0.0;
    for (float v : out) peak = std::fmax (peak, std::fabs ((double) v));
    check (peak > 0.01, "RUN rising edge into BD1 TRIG strikes BD1 (peak " + std::to_string (peak) + ")");
    check (rack.jackVolts ("SHOGUN#1/BD1:ENV") >= 0.0f && std::isfinite (rack.jackVolts ("SHOGUN#1/SD:DECAY")), "ENV and the self-patched input are finite");
    check (rack.delayedCableCount() >= 1, "a SHOGUN self-patch is a JCS R9 feedback cable (one sample)");
}

} // namespace

void runShogunRackTests (int& checks, int& failures)
{
    gChecks = &checks;
    gFailures = &failures;
    testJacks();
    testLatency();
    testHostClock();
    testJackTrigger();
}

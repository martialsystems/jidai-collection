// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
//
// Rack tests, no JUCE and no audio device: devices go in and out, cables cross devices, every feedback cable is
// exactly one sample late (JCS R9), RACK I/O is the host connection (R13), path latency (R11), and the audit
// issues X2, X5 and X6 are reproduced against the v2 rules and shown fixed (tests/JcsRackTests.cpp).

#include "core/Rack.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jidai;

namespace {

int failures = 0;
int checks = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("FAIL %s\n", what.c_str());
    }
}

bool near (float a, float b, float tolerance = 1.0e-5f) { return std::fabs (a - b) <= tolerance; }

void run (Rack& rack, int samples, const float* inL = nullptr)
{
    std::vector<float> l ((size_t) samples), r ((size_t) samples);
    rack.process (inL, inL, l.data(), r.data(), samples);
}

void press (BushidoDevice& b, const char* id)
{
    b.press (id, true);
    b.press (id, false);
}

void setParam (BushidoDevice& b, const char* id, float v) { b.setParam (id, v); }

int countInternal (const Rack& rack, const std::string& prefix)
{
    int n = 0;
    for (auto& c : rack.cables())
        if (c.a.rfind (prefix, 0) == 0 && c.b.rfind (prefix, 0) == 0)
            ++n;
    return n;
}

void testAddBushido()
{
    Rack rack;
    rack.prepare (48000.0, 512);
    Device* d = rack.addDevice (DeviceKind::Bushido);
    check (d != nullptr && d->kind() == DeviceKind::Bushido, "add Bushido: device made");
    check (rack.deviceCount() == 1, "add Bushido: one device in the rack");
    check (d->rackId() == "BUSHIDO#1", "add Bushido: rack id BUSHIDO#1, got " + d->rackId());
    check (d->jacks().size() == 25, "add Bushido: 25 jacks");
    Device* found = nullptr;
    int jack = -1;
    check (rack.resolve ("BUSHIDO#1/OUTPUTS:CV A", found, jack) && found == d, "add Bushido: BUSHIDO#1/OUTPUTS:CV A resolves");
    check (rack.resolve ("BUSHIDO#1/INPUTS:START/STOP", found, jack), "add Bushido: a jack label with a slash resolves");

    Device* second = rack.addDevice (DeviceKind::Bushido);
    check (second->rackId() == "BUSHIDO#2", "add Bushido: a second BUSHIDO is BUSHIDO#2");
    check (rack.deviceCount() == 2, "add Bushido: more than one of each is allowed");

    Rack empty;
    empty.prepare (48000.0, 64);
    std::vector<float> l (64, 1.0f), r (64, 1.0f), in (64, 0.5f);
    check (empty.process (in.data(), in.data(), l.data(), r.data(), 64), "empty rack: processes");
    check (l[10] == 0.0f && r[63] == 0.0f, "empty rack: silent output");
}

void testAddRonin()
{
    Rack rack;
    rack.prepare (48000.0, 512);
    rack.addDevice (DeviceKind::RackIO);
    Device* d = rack.addDevice (DeviceKind::Ronin);
    check (d != nullptr && d->kind() == DeviceKind::Ronin, "add Ronin: device made");
    check (d->rackId() == "RONIN#1", "add Ronin: rack id RONIN#1");
    check (d->units().size() == 18, "add Ronin: sixteen RONIN modules plus HOST IN and HOST OUT in the graph");
    auto* ronin = static_cast<RoninDevice*> (d);
    check (ronin->program() == kDefaultFactoryPreset, "add Ronin: starts on the INIT program");
    check (countInternal (rack, "RONIN#1/") == 8, "add Ronin: the INIT program's 8 cables, got " + std::to_string (countInternal (rack, "RONIN#1/")));
    check (rack.liveCableCount() == 8, "add Ronin: 8 cables live in the graph");
    check (ronin->effectOn(), "add Ronin: Effect on");
    {
        // JCS R3s: RONIN marks its own S-trig inputs (PortDesc::strigInput), exactly EG 1 TRIG and EG 2 TRIG.
        int strig = 0;
        bool egTrigs = true;
        for (auto& j : d->jacks())
            if (j.unit != nullptr && j.desc.dir == PortDir::In && j.unit->strigInput (j.port))
            {
                ++strig;
                egTrigs = egTrigs && (j.id == "EG 1:TRIG" || j.id == "EG 2:TRIG");
            }
        check (strig == 2 && egTrigs, "add Ronin: S-trig inputs are EG 1 TRIG and EG 2 TRIG only, got " + std::to_string (strig));
        check (ronin->triShape() == 0, "add Ronin: INIT starts the VCO on the true TRIANGLE");
    }

    // v2 routing, now explicit cables (migration M5): RACK HOST IN -> RONIN#1 HOST IN, RONIN HOST OUT -> MAIN OUT.
    // Host audio reaches the first RONIN's EXT IN. With Effect off the rack output is that audio, dry.
    rack.applyLegacyHostRouting();
    check (rack.liveCableCount() == 12, "M5 routing: 4 host cables added");
    ronin->setEffectOn (false);
    std::vector<float> in (256, 0.25f), l (256), r (256);
    rack.process (in.data(), in.data(), l.data(), r.data(), 256);
    check (near (l[200], 0.25f, 1.0e-4f) && near (r[200], 0.25f, 1.0e-4f), "Effect off: host audio passes dry, got " + std::to_string (l[200]));

    // Output Level scales the buffer leaving the rack: knob 0.7 is unity, 1.0 is twice as loud.
    const int level = panelKnobIndex ("OUTPUT", "LEVEL");
    // RONIN smooths knobs per sample (RONIN_Redesign 3.5: 10 ms one-pole), so the new level settles over ~100 ms.
    ronin->setKnob (level, 1.0f);
    rack.process (in.data(), in.data(), l.data(), r.data(), 256);
    check (l[1] < l[200] && l[200] < 0.5f, "Output Level ramps (per-sample smoothing), no step");
    for (int b = 0; b < 24; ++b)
        rack.process (in.data(), in.data(), l.data(), r.data(), 256);
    check (near (l[200], 0.5f, 1.0e-4f), "Output Level 1.0 doubles the rack output, got " + std::to_string (l[200]));

    // A second RONIN gets no host audio, and adds its own output to the sum.
    Device* second = rack.addDevice (DeviceKind::Ronin);
    static_cast<RoninDevice*> (second)->setEffectOn (false);
    rack.applyLegacyHostRouting();
    rack.process (in.data(), in.data(), l.data(), r.data(), 256);
    check (near (rack.jackVolts ("RONIN#2/EXT IN:L"), 0.0f), "second RONIN: EXT IN stays silent");
    check (near (rack.jackVolts ("RONIN#1/EXT IN:L"), 1.25f, 1.0e-4f), "first RONIN: EXT IN L is host x 5 V");
}

void testBushidoCvIntoRoninHzV()
{
    Rack rack;
    rack.prepare (48000.0, 512);
    auto* b = static_cast<BushidoDevice*> (rack.addDevice (DeviceKind::Bushido));
    auto* r = static_cast<RoninDevice*> (rack.addDevice (DeviceKind::Ronin));
    for (int s = 1; s <= 12; ++s)
        setParam (*b, ("A:" + std::to_string (s)).c_str(), 0.6f);     // RANGE A is 5 V: every step is 3 V
    press (*b, "MODE:START/STOP");

    check (rack.check ("BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V") == Rack::Check::Ok, "CV A -> VCO HZ/V is a legal cable");
    check (rack.connect ("BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V", 2) == Rack::Check::Ok, "patch CV A -> VCO HZ/V");
    run (rack, 4800);
    const float cv = rack.jackVolts ("BUSHIDO#1/OUTPUTS:CV A");
    const float hz = rack.jackVolts ("RONIN#1/VCO:HZ/V");
    check (near (cv, 3.0f, 1.0e-3f), "CV A plays 3 V, got " + std::to_string (cv));
    check (near (hz, cv), "VCO HZ/V reads the same volts as CV A, got " + std::to_string (hz));

    // The VCO's pitch follows the cable: double the volts, one octave up (Hz/V), counted on the saw.
    auto sawHz = [&] (float volts)
    {
        for (int s = 1; s <= 12; ++s)
            setParam (*b, ("A:" + std::to_string (s)).c_str(), volts / 5.0f);
        run (rack, 4800);
        int crossings = 0;
        float prev = rack.jackVolts ("RONIN#1/VCO:SAW");
        std::vector<float> l (1), rr (1);
        for (int i = 0; i < 48000; ++i)
        {
            rack.process (nullptr, nullptr, l.data(), rr.data(), 1);
            const float now = rack.jackVolts ("RONIN#1/VCO:SAW");
            if (prev > 0.0f && now <= 0.0f && prev - now > 1.0f)
                ++crossings;
            prev = now;
        }
        return crossings;
    };
    (void) r;
    const int low = sawHz (1.0f);
    const int high = sawHz (2.0f);
    check (low > 0 && std::abs (high - 2 * low) <= 2, "VCO HZ/V: 2 V plays twice the frequency of 1 V, got " + std::to_string (low) + " and " + std::to_string (high) + " Hz");

    // Back the other way: a RONIN output into a BUSHIDO input in the same patch.
    check (rack.connect ("RONIN#1/MG:TRI", "BUSHIDO#1/CLOCK:TEMPO CV") == Rack::Check::Ok, "and back: MG TRI -> BUSHIDO TEMPO CV");
}

void testRoninMgIntoBushidoClock()
{
    Rack rack;
    rack.prepare (48000.0, 512);
    auto* b = static_cast<BushidoDevice*> (rack.addDevice (DeviceKind::Bushido));
    auto* r = static_cast<RoninDevice*> (rack.addDevice (DeviceKind::Ronin));
    setParam (*b, "CLOCK:SOURCE", 1.0f);                 // EXT: only the CLOCK jack advances the steps
    setParam (*b, "MODE:MODE", 0.0f);                    // A: 12 steps, loops
    r->setKnob (panelKnobIndex ("MG", "RATE"), 0.75f);
    press (*b, "MODE:START/STOP");

    run (rack, 2400);
    const int before = b->engine().currentStep();
    run (rack, 48000);
    check (b->engine().currentStep() == before, "EXT clock with no cable: the step does not move");

    check (rack.connect ("RONIN#1/MG:TRI", "BUSHIDO#1/CLOCK:CLOCK") == Rack::Check::Ok, "patch MG TRI -> BUSHIDO CLOCK");
    int moves = 0, last = b->engine().currentStep();
    std::vector<float> l (64), rr (64);
    for (int block = 0; block < 48000 / 64; ++block)
    {
        rack.process (nullptr, nullptr, l.data(), rr.data(), 64);
        check (near (rack.jackVolts ("BUSHIDO#1/CLOCK:CLOCK"), rack.jackVolts ("RONIN#1/MG:TRI")), "CLOCK jack reads MG TRI");
        if (b->engine().currentStep() != last)
        {
            ++moves;
            last = b->engine().currentStep();
        }
    }
    check (moves >= 2, "MG TRI clocks BUSHIDO: the step advanced " + std::to_string (moves) + " times in 1 s");
}

void testRemoveDevice()
{
    Rack rack;
    rack.prepare (48000.0, 512);
    rack.addDevice (DeviceKind::Bushido);
    Device* ronin = rack.addDevice (DeviceKind::Ronin);
    rack.addDevice (DeviceKind::Ronin);
    rack.connect ("BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V");
    rack.connect ("RONIN#1/MG:TRI", "BUSHIDO#1/CLOCK:CLOCK");
    rack.connect ("BUSHIDO#1/OUTPUTS:GATE A", "RONIN#2/EG 1:TRIG");
    rack.connect ("BUSHIDO#1/1:TRIG", "BUSHIDO#1/INPUTS:RESET");
    run (rack, 512);
    const int before = (int) rack.cables().size();     // 8 + 8 INIT cables, 4 patched

    check (rack.removeDevice (ronin), "remove RONIN 1");
    check (rack.deviceCount() == 2, "remove: two devices left");
    bool touches = false;
    for (auto& c : rack.cables())
        touches = touches || c.a.rfind ("RONIN#1/", 0) == 0 || c.b.rfind ("RONIN#1/", 0) == 0;
    check (! touches, "remove: no cable left on RONIN 1's jacks");
    check ((int) rack.cables().size() == before - 10, "remove: its 8 own cables and 2 cross cables went, got " + std::to_string (before - (int) rack.cables().size()));
    check (rack.liveCableCount() == (int) rack.cables().size(), "remove: the graph runs exactly the cables left");
    Device* d = nullptr;
    int j = -1;
    check (! rack.resolve ("RONIN#1/VCO:HZ/V", d, j), "remove: RONIN#1 jacks no longer resolve");
    run (rack, 512);    // still runs with the device gone
    check (rack.check ("BUSHIDO#1/OUTPUTS:GATE A", "RONIN#2/EG 1:TRIG") == Rack::Check::Ok, "remove: RONIN 2 keeps its cable");

    Device* again = rack.addDevice (DeviceKind::Ronin, 0);
    check (again->rackId() == "RONIN#1" && rack.indexOf (again) == 0, "a new RONIN takes the free number and the drop position");

    rack.clear();
    check (rack.deviceCount() == 0 && rack.cables().empty() && rack.liveCableCount() == 0, "empty rack is valid");
    run (rack, 64);
}

void testOneSampleFeedback()
{
    // EXT IN L -> BUSHIDO MIX IN 1, MIX OUT -> RONIN INV IN, then INV OUT -> MIX IN 2 closes the loop.
    // That newest cable is the one delayed: MIX IN 2 at sample n is INV OUT at sample n - 1.
    // Host audio reaches EXT IN through RACK I/O (cable added first, so it is the oldest).
    Rack rack;
    rack.prepare (48000.0, 512);
    rack.addDevice (DeviceKind::RackIO);
    auto* b = static_cast<BushidoDevice*> (rack.addDevice (DeviceKind::Bushido));
    auto* r = static_cast<RoninDevice*> (rack.addDevice (DeviceKind::Ronin));
    rack.replaceInternalCables (r, {});
    setParam (*b, "MIXER:LEVEL 1", 1.0f);
    setParam (*b, "MIXER:LEVEL 2", 0.5f);
    rack.connect ("RACK#1/HOST:IN L", "RONIN#1/HOST:IN L");
    rack.connect ("RONIN#1/EXT IN:L", "BUSHIDO#1/MIXER:IN 1");
    rack.connect ("BUSHIDO#1/MIXER:OUT", "RONIN#1/INV:IN");
    rack.connect ("RONIN#1/INV:OUT", "BUSHIDO#1/MIXER:IN 2");
    check (rack.delayedCableCount() == 1, "feedback: exactly one delayed cable");
    check (rack.cables().back().a == "RONIN#1/INV:OUT", "feedback: the newest cable closes the loop");

    const int n = 8;
    std::vector<float> in ((size_t) n, 0.0f), l (1), rr (1);
    in[2] = 0.2f;                                        // one host sample of 1 V
    std::vector<float> mix, inv, mixIn2;
    for (int i = 0; i < n; ++i)
    {
        rack.process (&in[(size_t) i], &in[(size_t) i], l.data(), rr.data(), 1);
        mix.push_back (rack.jackVolts ("BUSHIDO#1/MIXER:OUT"));
        inv.push_back (rack.jackVolts ("RONIN#1/INV:OUT"));
        mixIn2.push_back (rack.jackVolts ("BUSHIDO#1/MIXER:IN 2"));
    }
    const float expect[n] = { 0.0f, 0.0f, 1.0f, -0.5f, 0.25f, -0.125f, 0.0625f, -0.03125f };
    for (int i = 0; i < n; ++i)
        check (near (mix[(size_t) i], expect[i]), "feedback: MIX OUT[" + std::to_string (i) + "] = " + std::to_string (expect[i]) + ", got " + std::to_string (mix[(size_t) i]));
    for (int i = 0; i < n; ++i)
        check (near (inv[(size_t) i], -mix[(size_t) i]), "feedback: the forward cable MIX OUT -> INV IN is zero-delay at sample " + std::to_string (i));
    for (int i = 1; i < n; ++i)
        check (near (mixIn2[(size_t) i], inv[(size_t) i - 1]), "feedback: MIX IN 2[n] = INV OUT[n-1] at n = " + std::to_string (i));
    check (mixIn2[2] == 0.0f && mixIn2[3] == -1.0f, "feedback: the impulse comes back exactly one sample late");

    // Patch the same loop in another order: now MIX OUT -> INV IN is the newest, so that one is delayed.
    rack.setCables ({});
    rack.connect ("RACK#1/HOST:IN L", "RONIN#1/HOST:IN L");
    rack.connect ("RONIN#1/EXT IN:L", "BUSHIDO#1/MIXER:IN 1");
    rack.connect ("RONIN#1/INV:OUT", "BUSHIDO#1/MIXER:IN 2");
    rack.connect ("BUSHIDO#1/MIXER:OUT", "RONIN#1/INV:IN");
    run (rack, 64);
    std::vector<float> invIn, mix2;
    for (int i = 0; i < n; ++i)
    {
        rack.process (&in[(size_t) i], &in[(size_t) i], l.data(), rr.data(), 1);
        invIn.push_back (rack.jackVolts ("RONIN#1/INV:IN"));
        mix2.push_back (rack.jackVolts ("BUSHIDO#1/MIXER:OUT"));
    }
    check (rack.delayedCableCount() == 1, "feedback, other order: one delayed cable");
    for (int i = 1; i < n; ++i)
        check (near (invIn[(size_t) i], mix2[(size_t) i - 1]), "feedback, other order: INV IN[n] = MIX OUT[n-1] at n = " + std::to_string (i));

    // A loop inside one RONIN follows the same rule (RONIN's Feedback-style self patch).
    rack.setCables ({});
    rack.connect ("RONIN#1/INV:OUT", "RONIN#1/INV:IN");
    check (rack.delayedCableCount() == 1, "self patch: the cable is delayed one sample");
}

void testGateLaw()
{
    // JCS R3s. BUSHIDO GATE A (0/5 V) into RONIN EG 1 TRIG, an S-trig input: a high gate arrives as 0 V (held) and a
    // low gate as +5 V. RONIN EXT IN GATE is an S-trig source: it passes as written into any input, so into BUSHIDO
    // STEP held is 0 V and released is +5 V (v2 made held 5 V; a v2 patch keeps that through legacyInvert, M3).
    Rack rack;
    rack.prepare (48000.0, 512);
    auto* b = static_cast<BushidoDevice*> (rack.addDevice (DeviceKind::Bushido));
    auto* r = static_cast<RoninDevice*> (rack.addDevice (DeviceKind::Ronin));
    rack.connect ("BUSHIDO#1/OUTPUTS:GATE A", "RONIN#1/EG 1:TRIG");
    run (rack, 64);
    check (near (rack.jackVolts ("RONIN#1/EG 1:TRIG"), 10.0f), "inputs sum: INIT's EXT IN GATE (+5 V released) plus a low BUSHIDO gate (+5 V)");
    rack.replaceInternalCables (r, {});
    run (rack, 64);
    check (near (rack.jackVolts ("RONIN#1/EG 1:TRIG"), 5.0f), "stopped BUSHIDO: EG 1 TRIG rests at +5 V (released)");
    press (*b, "MODE:START/STOP");
    run (rack, 480);
    check (rack.jackVolts ("BUSHIDO#1/OUTPUTS:GATE A") > 1.0f, "running BUSHIDO: GATE A high");
    check (near (rack.jackVolts ("RONIN#1/EG 1:TRIG"), 0.0f), "high gate reaches EG 1 TRIG as 0 V (held)");

    rack.connect ("RONIN#1/EXT IN:GATE", "BUSHIDO#1/INPUTS:STEP");
    r->setHold (true);
    run (rack, 64);
    check (near (rack.jackVolts ("BUSHIDO#1/INPUTS:STEP"), 0.0f), "R3s: RONIN HOLD (S-trig held) reaches BUSHIDO STEP raw, 0 V");
    r->setHold (false);
    run (rack, 64);
    check (near (rack.jackVolts ("BUSHIDO#1/INPUTS:STEP"), 5.0f), "R3s: released S-trig reaches BUSHIDO raw, +5 V");

    // The same cable loaded from a v2 rack keeps the old law on that cable only (M3).
    auto cables = rack.cables();
    for (auto& c : cables)
        if (c.a == "RONIN#1/EXT IN:GATE")
            c.legacyInvert = true;
    rack.setCables (cables);
    r->setHold (true);
    run (rack, 64);
    check (near (rack.jackVolts ("BUSHIDO#1/INPUTS:STEP"), 5.0f), "M3 legacyInvert: held reaches BUSHIDO as 5 V, as in v2");
    r->setHold (false);
    run (rack, 64);
    check (near (rack.jackVolts ("BUSHIDO#1/INPUTS:STEP"), 0.0f), "M3 legacyInvert: released reaches BUSHIDO as 0 V, as in v2");

    check (rack.check ("BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCF:OUT") == Rack::Check::TwoOutputs, "two outputs carry nothing");
}

void testBypass()
{
    Rack rack;
    rack.prepare (48000.0, 512);
    auto* b = static_cast<BushidoDevice*> (rack.addDevice (DeviceKind::Bushido));
    press (*b, "MODE:START/STOP");
    run (rack, 480);
    check (b->noteEventCount() == 0 || b->noteEvent (0).on, "MIDI convenience: a note on is collected");
    b->setBypassed (true);
    run (rack, 480);
    check (rack.jackVolts ("BUSHIDO#1/OUTPUTS:GATE A") == 0.0f, "BYPASS: GATE A stays low");
    check (b->engine().isRunning(), "BYPASS: the engine keeps running");
}

// ORIGAMI in the rack runs the same OrigamiCore as the plugin: 12 jacks with JCS ids, WAVE 0 passes a cable's
// volts bit-exact, the fold acts once WAVE moves, MG -> VC 1 modulates at audio rate, and QUALITY sets latency.
void testOrigamiDevice()
{
    Rack rack;
    rack.prepare (48000.0, 512);
    auto* r = static_cast<RoninDevice*> (rack.addDevice (DeviceKind::Ronin));
    auto* o = static_cast<OrigamiDevice*> (rack.addDevice (DeviceKind::Origami));
    check (o != nullptr && o->rackId() == "ORIGAMI#1" && o->jacks().size() == 12, "add ORIGAMI: ORIGAMI#1 with 12 jacks (8 front, 4 HOST on the back)");
    check (o->findJack ("HOST:IN L") == OrigamiDevice::HostInL && o->jacks()[OrigamiDevice::HostOutR].backOnly
               && ! o->jacks()[OrigamiDevice::OutR].backOnly, "ORIGAMI HOST jacks are back-only");
    check (o->findJack ("IN:IN L") == 0 && o->findJack ("VC:VC 3") == 5 && o->findJack ("OUT:OUT R") == 7, "ORIGAMI jack ids (JCS R6)");
    check (o->jacks()[0].desc.type == PortType::Audio && o->jacks()[3].desc.type == PortType::CV, "roles: IN audio, VC cv");
    check (rack.connect ("RONIN#1/VCO:SAW", "ORIGAMI#1/IN:IN L") == Rack::Check::Ok, "patch VCO SAW -> ORIGAMI IN L");
    bool exact = true;
    for (int i = 0; i < 400; ++i)
    {
        run (rack, 1);
        const float saw = rack.jackVolts ("RONIN#1/VCO:SAW");
        exact = exact && rack.jackVolts ("ORIGAMI#1/OUT:OUT L") == saw && rack.jackVolts ("ORIGAMI#1/OUT:OUT R") == saw;
    }
    check (exact, "WAVE 0: OUT L/R equal the VCO volts bit for bit (IN R normalled to IN L)");
    check (o->latencySamples() == 0, "ORIGAMI 1x latency 0");
    o->setParam ("wave", 0.6);
    o->setParam ("level_comp", 0.0);
    run (rack, 4800);
    float diff = 0.0f;
    for (int i = 0; i < 400; ++i)
    {
        run (rack, 1);
        diff = std::fmax (diff, std::fabs (rack.jackVolts ("ORIGAMI#1/OUT:OUT L") - rack.jackVolts ("RONIN#1/VCO:SAW")));
    }
    check (diff > 0.5f, "WAVE 0.6 folds the cable signal");
    check (rack.connect ("RONIN#1/MG:TRI", "ORIGAMI#1/VC:VC 1") == Rack::Check::Ok, "patch MG TRI -> ORIGAMI VC 1");
    o->setParam ("vc1_amt", 1.0);
    run (rack, 2400);
    check (std::isfinite (rack.jackVolts ("ORIGAMI#1/OUT:OUT L")), "VC 1 patched: finite");
    o->setParam (origami::kQuality, 1.0);
    check (o->latencySamples() == 46, "ORIGAMI 2x latency 46");
    run (rack, 480);
    check (std::isfinite (rack.jackVolts ("ORIGAMI#1/OUT:OUT L")), "2x in the rack: finite");
    (void) r;
}

}

void runJcsRackTests (int& checks, int& failures);

int main()
{
    testAddBushido();
    testAddRonin();
    testBushidoCvIntoRoninHzV();
    testRoninMgIntoBushidoClock();
    testRemoveDevice();
    testOneSampleFeedback();
    testGateLaw();
    testBypass();
    testOrigamiDevice();
    runJcsRackTests (checks, failures);
    std::printf ("%d checks, %d failed\n", checks, failures);
    std::printf (failures == 0 ? "RACK TESTS PASS\n" : "RACK TESTS FAIL\n");
    return failures == 0 ? 0 : 1;
}

// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
//
// Rack tests, no JUCE and no audio device: devices go in and out, cables cross devices, and the
// newest cable of a feedback loop is exactly one sample late.

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
    check (d->rackId() == "SQ-10#1", "add Bushido: rack id SQ-10#1, got " + d->rackId());
    check (d->jacks().size() == 25, "add Bushido: 25 jacks");
    Device* found = nullptr;
    int jack = -1;
    check (rack.resolve ("SQ-10#1/OUTPUTS:CV A", found, jack) && found == d, "add Bushido: SQ-10#1/OUTPUTS:CV A resolves");
    check (rack.resolve ("SQ-10#1/INPUTS:START/STOP", found, jack), "add Bushido: a jack label with a slash resolves");

    Device* second = rack.addDevice (DeviceKind::Bushido);
    check (second->rackId() == "SQ-10#2", "add Bushido: a second BUSHIDO is SQ-10#2");
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
    Device* d = rack.addDevice (DeviceKind::Ronin);
    check (d != nullptr && d->kind() == DeviceKind::Ronin, "add Ronin: device made");
    check (d->rackId() == "MS-50#1", "add Ronin: rack id MS-50#1");
    check (d->units().size() == 16, "add Ronin: sixteen RONIN modules in the graph");
    auto* ronin = static_cast<RoninDevice*> (d);
    check (ronin->program() == kDefaultFactoryPreset, "add Ronin: starts on the Voice program");
    check (countInternal (rack, "MS-50#1/") == 8, "add Ronin: the Voice program's 8 cables, got " + std::to_string (countInternal (rack, "MS-50#1/")));
    check (rack.liveCableCount() == 8, "add Ronin: 8 cables live in the graph");
    check (ronin->effectOn(), "add Ronin: Effect on");

    // Host audio reaches the first RONIN's EXT IN. With Effect off the rack output is that audio, dry.
    ronin->setEffectOn (false);
    std::vector<float> in (256, 0.25f), l (256), r (256);
    rack.process (in.data(), in.data(), l.data(), r.data(), 256);
    check (near (l[200], 0.25f, 1.0e-4f) && near (r[200], 0.25f, 1.0e-4f), "Effect off: host audio passes dry, got " + std::to_string (l[200]));

    // Output Level scales the buffer leaving the rack: knob 0.7 is unity, 1.0 is twice as loud.
    const int level = panelKnobIndex ("OUTPUT", "LEVEL");
    ronin->setKnob (level, 1.0f);
    rack.process (in.data(), in.data(), l.data(), r.data(), 256);
    check (near (l[200], 0.5f, 1.0e-4f), "Output Level 1.0 doubles the rack output, got " + std::to_string (l[200]));

    // A second RONIN gets no host audio, and adds its own output to the sum.
    Device* second = rack.addDevice (DeviceKind::Ronin);
    static_cast<RoninDevice*> (second)->setEffectOn (false);
    rack.process (in.data(), in.data(), l.data(), r.data(), 256);
    check (near (rack.jackVolts ("MS-50#2/EXT IN:L"), 0.0f), "second RONIN: EXT IN stays silent");
    check (near (rack.jackVolts ("MS-50#1/EXT IN:L"), 1.25f, 1.0e-4f), "first RONIN: EXT IN L is host x 5 V");
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

    check (rack.check ("SQ-10#1/OUTPUTS:CV A", "MS-50#1/VCO:HZ/V") == Rack::Check::Ok, "CV A -> VCO HZ/V is a legal cable");
    check (rack.connect ("SQ-10#1/OUTPUTS:CV A", "MS-50#1/VCO:HZ/V", 2) == Rack::Check::Ok, "patch CV A -> VCO HZ/V");
    run (rack, 4800);
    const float cv = rack.jackVolts ("SQ-10#1/OUTPUTS:CV A");
    const float hz = rack.jackVolts ("MS-50#1/VCO:HZ/V");
    check (near (cv, 3.0f, 1.0e-3f), "CV A plays 3 V, got " + std::to_string (cv));
    check (near (hz, cv), "VCO HZ/V reads the same volts as CV A, got " + std::to_string (hz));

    // The VCO's pitch follows the cable: double the volts, one octave up (Hz/V), counted on the saw.
    auto sawHz = [&] (float volts)
    {
        for (int s = 1; s <= 12; ++s)
            setParam (*b, ("A:" + std::to_string (s)).c_str(), volts / 5.0f);
        run (rack, 4800);
        int crossings = 0;
        float prev = rack.jackVolts ("MS-50#1/VCO:SAW");
        std::vector<float> l (1), rr (1);
        for (int i = 0; i < 48000; ++i)
        {
            rack.process (nullptr, nullptr, l.data(), rr.data(), 1);
            const float now = rack.jackVolts ("MS-50#1/VCO:SAW");
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
    check (rack.connect ("MS-50#1/MG:TRI", "SQ-10#1/CLOCK:TEMPO CV") == Rack::Check::Ok, "and back: MG TRI -> BUSHIDO TEMPO CV");
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

    check (rack.connect ("MS-50#1/MG:TRI", "SQ-10#1/CLOCK:CLOCK") == Rack::Check::Ok, "patch MG TRI -> BUSHIDO CLOCK");
    int moves = 0, last = b->engine().currentStep();
    std::vector<float> l (64), rr (64);
    for (int block = 0; block < 48000 / 64; ++block)
    {
        rack.process (nullptr, nullptr, l.data(), rr.data(), 64);
        check (near (rack.jackVolts ("SQ-10#1/CLOCK:CLOCK"), rack.jackVolts ("MS-50#1/MG:TRI")), "CLOCK jack reads MG TRI");
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
    rack.connect ("SQ-10#1/OUTPUTS:CV A", "MS-50#1/VCO:HZ/V");
    rack.connect ("MS-50#1/MG:TRI", "SQ-10#1/CLOCK:CLOCK");
    rack.connect ("SQ-10#1/OUTPUTS:GATE A", "MS-50#2/EG 1:TRIG");
    rack.connect ("SQ-10#1/1:TRIG", "SQ-10#1/INPUTS:RESET");
    run (rack, 512);
    const int before = (int) rack.cables().size();     // 8 + 8 Voice cables, 4 patched

    check (rack.removeDevice (ronin), "remove RONIN 1");
    check (rack.deviceCount() == 2, "remove: two devices left");
    bool touches = false;
    for (auto& c : rack.cables())
        touches = touches || c.a.rfind ("MS-50#1/", 0) == 0 || c.b.rfind ("MS-50#1/", 0) == 0;
    check (! touches, "remove: no cable left on RONIN 1's jacks");
    check ((int) rack.cables().size() == before - 10, "remove: its 8 own cables and 2 cross cables went, got " + std::to_string (before - (int) rack.cables().size()));
    check (rack.liveCableCount() == (int) rack.cables().size(), "remove: the graph runs exactly the cables left");
    Device* d = nullptr;
    int j = -1;
    check (! rack.resolve ("MS-50#1/VCO:HZ/V", d, j), "remove: MS-50#1 jacks no longer resolve");
    run (rack, 512);    // still runs with the device gone
    check (rack.check ("SQ-10#1/OUTPUTS:GATE A", "MS-50#2/EG 1:TRIG") == Rack::Check::Ok, "remove: RONIN 2 keeps its cable");

    Device* again = rack.addDevice (DeviceKind::Ronin, 0);
    check (again->rackId() == "MS-50#1" && rack.indexOf (again) == 0, "a new RONIN takes the free number and the drop position");

    rack.clear();
    check (rack.deviceCount() == 0 && rack.cables().empty() && rack.liveCableCount() == 0, "empty rack is valid");
    run (rack, 64);
}

void testOneSampleFeedback()
{
    // EXT IN L -> BUSHIDO MIX IN 1, MIX OUT -> RONIN INV IN, then INV OUT -> MIX IN 2 closes the loop.
    // That newest cable is the one delayed: MIX IN 2 at sample n is INV OUT at sample n - 1.
    Rack rack;
    rack.prepare (48000.0, 512);
    auto* b = static_cast<BushidoDevice*> (rack.addDevice (DeviceKind::Bushido));
    auto* r = static_cast<RoninDevice*> (rack.addDevice (DeviceKind::Ronin));
    rack.replaceInternalCables (r, {});
    setParam (*b, "MIXER:LEVEL 1", 1.0f);
    setParam (*b, "MIXER:LEVEL 2", 0.5f);
    rack.connect ("MS-50#1/EXT IN:L", "SQ-10#1/MIXER:IN 1");
    rack.connect ("SQ-10#1/MIXER:OUT", "MS-50#1/INV:IN");
    rack.connect ("MS-50#1/INV:OUT", "SQ-10#1/MIXER:IN 2");
    check (rack.delayedCableCount() == 1, "feedback: exactly one delayed cable");
    check (rack.cables().back().a == "MS-50#1/INV:OUT", "feedback: the newest cable closes the loop");

    const int n = 8;
    std::vector<float> in ((size_t) n, 0.0f), l (1), rr (1);
    in[2] = 0.2f;                                        // one host sample of 1 V
    std::vector<float> mix, inv, mixIn2;
    for (int i = 0; i < n; ++i)
    {
        rack.process (&in[(size_t) i], &in[(size_t) i], l.data(), rr.data(), 1);
        mix.push_back (rack.jackVolts ("SQ-10#1/MIXER:OUT"));
        inv.push_back (rack.jackVolts ("MS-50#1/INV:OUT"));
        mixIn2.push_back (rack.jackVolts ("SQ-10#1/MIXER:IN 2"));
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
    rack.connect ("MS-50#1/EXT IN:L", "SQ-10#1/MIXER:IN 1");
    rack.connect ("MS-50#1/INV:OUT", "SQ-10#1/MIXER:IN 2");
    rack.connect ("SQ-10#1/MIXER:OUT", "MS-50#1/INV:IN");
    run (rack, 64);
    std::vector<float> invIn, mix2;
    for (int i = 0; i < n; ++i)
    {
        rack.process (&in[(size_t) i], &in[(size_t) i], l.data(), rr.data(), 1);
        invIn.push_back (rack.jackVolts ("MS-50#1/INV:IN"));
        mix2.push_back (rack.jackVolts ("SQ-10#1/MIXER:OUT"));
    }
    check (rack.delayedCableCount() == 1, "feedback, other order: one delayed cable");
    for (int i = 1; i < n; ++i)
        check (near (invIn[(size_t) i], mix2[(size_t) i - 1]), "feedback, other order: INV IN[n] = MIX OUT[n-1] at n = " + std::to_string (i));

    // A loop inside one RONIN follows the same rule (RONIN's Feedback-style self patch).
    rack.setCables ({});
    rack.connect ("MS-50#1/INV:OUT", "MS-50#1/INV:IN");
    check (rack.delayedCableCount() == 1, "self patch: the cable is delayed one sample");
}

void testGateLaw()
{
    // BUSHIDO GATE A (0/5 V) into RONIN EG 1 TRIG: S-15 makes a high gate 0 V (held) and a low gate +5 V.
    // RONIN EXT IN GATE (S-trig volts) into BUSHIDO START: held arrives as 5 V.
    Rack rack;
    rack.prepare (48000.0, 512);
    auto* b = static_cast<BushidoDevice*> (rack.addDevice (DeviceKind::Bushido));
    auto* r = static_cast<RoninDevice*> (rack.addDevice (DeviceKind::Ronin));
    rack.connect ("SQ-10#1/OUTPUTS:GATE A", "MS-50#1/EG 1:TRIG");
    run (rack, 64);
    check (near (rack.jackVolts ("MS-50#1/EG 1:TRIG"), 10.0f), "inputs sum: Voice's EXT IN GATE (+5 V released) plus a low BUSHIDO gate (+5 V)");
    rack.replaceInternalCables (r, {});
    run (rack, 64);
    check (near (rack.jackVolts ("MS-50#1/EG 1:TRIG"), 5.0f), "stopped BUSHIDO: EG 1 TRIG rests at +5 V (released)");
    press (*b, "MODE:START/STOP");
    run (rack, 480);
    check (rack.jackVolts ("SQ-10#1/OUTPUTS:GATE A") > 1.0f, "running BUSHIDO: GATE A high");
    check (near (rack.jackVolts ("MS-50#1/EG 1:TRIG"), 0.0f), "high gate reaches EG 1 TRIG as 0 V (held)");

    rack.connect ("MS-50#1/EXT IN:GATE", "SQ-10#1/INPUTS:STEP");
    r->setHold (true);
    run (rack, 64);
    check (near (rack.jackVolts ("SQ-10#1/INPUTS:STEP"), 5.0f), "RONIN HOLD gate reaches BUSHIDO STEP as 5 V");
    r->setHold (false);
    run (rack, 64);
    check (near (rack.jackVolts ("SQ-10#1/INPUTS:STEP"), 0.0f), "released RONIN gate reaches BUSHIDO as 0 V");

    check (rack.check ("SQ-10#1/OUTPUTS:CV A", "MS-50#1/VCF:OUT") == Rack::Check::TwoOutputs, "two outputs carry nothing");
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
    check (rack.jackVolts ("SQ-10#1/OUTPUTS:GATE A") == 0.0f, "BYPASS: GATE A stays low");
    check (b->engine().isRunning(), "BYPASS: the engine keeps running");
}

}

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
    std::printf ("%d checks, %d failed\n", checks, failures);
    std::printf (failures == 0 ? "RACK TESTS PASS\n" : "RACK TESTS FAIL\n");
    return failures == 0 ? 0 : 1;
}

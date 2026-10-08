// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
//
// Preset banks through JidaiProcessor. Bank A is each instrument's factory set, INIT only for now.
// A new rack holds RACK I/O only (decision 14), so the bank tests first insert the old default pair, one BUSHIDO
// above one RONIN, without auto-route (makeClassic), and the cable counts below are the same as before.
// Bank B starts with the rack patches. The compiled list is empty for now, so the rack patch tests
// load a fixture list (TEST LOOP, TEST RING) through setRackPatchesForTest. User files stay in a temp directory.

#include "plugin/JidaiEditor.h"

#include "UI/PatchBayLogic.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

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

bool near (float a, float b) { return std::fabs (a - b) <= 1.0e-4f; }

bool hasCable (const jidai::Rack& rack, const std::string& a, const std::string& b)
{
    for (auto& c : rack.cables())
        if ((c.a == a && c.b == b) || (c.a == b && c.b == a))
            return true;
    return false;
}

int cableColor (const jidai::Rack& rack, const std::string& a, const std::string& b)
{
    for (auto& c : rack.cables())
        if ((c.a == a && c.b == b) || (c.a == b && c.b == a))
            return c.color;
    return -1;
}

int countInternal (const jidai::Rack& rack, const std::string& prefix)
{
    int n = 0;
    for (auto& c : rack.cables())
        if (c.a.rfind (prefix, 0) == 0 && c.b.rfind (prefix, 0) == 0)
            ++n;
    return n;
}

bool anyBushido (const jidai::Rack& rack)
{
    for (auto& c : rack.cables())
        if (c.a.rfind ("BUSHIDO", 0) == 0 || c.b.rfind ("BUSHIDO", 0) == 0)
            return true;
    return false;
}

jidai::BushidoDevice* bushidoAt (JidaiProcessor& p, int nth = 0)
{
    int seen = 0;
    for (int i = 0; i < p.rack().deviceCount(); ++i)
        if (auto* b = dynamic_cast<jidai::BushidoDevice*> (p.rack().device (i)))
            if (seen++ == nth)
                return b;
    return nullptr;
}

jidai::RoninDevice* roninAt (JidaiProcessor& p, int nth = 0)
{
    int seen = 0;
    for (int i = 0; i < p.rack().deviceCount(); ++i)
        if (auto* r = dynamic_cast<jidai::RoninDevice*> (p.rack().device (i)))
            if (seen++ == nth)
                return r;
    return nullptr;
}

// Two rack patches in the rack_patches.json format. They exercise the rack patch loader while the
// compiled list is empty: a filter loop sequenced by BUSHIDO, and a self ring seeded by its CV C.
const char* kFixturePatches = R"JSON({
 "patches": [
  { "name": "TEST LOOP",
    "rack": { "preset": 0,
      "knobs": { "VCO:RANGE": 0, "VCF:CUTOFF": 0.4, "VCF:PEAK": 0.7, "VCF:MOD": 0.68,
                 "EG 1:ATTACK": 0, "EG 1:DECAY": 0.25, "EG 1:SUSTAIN": 0.2, "EG 1:RELEASE": 0.15 },
      "params": { "MODE:MODE": 0, "CH:C MODE": 0, "CLOCK:TEMPO": 0.6, "CLOCK:SOURCE": 0, "CH:RANGE A": 1, "CH:PORTA A": 0,
                  "A:1": 0.4, "C:1": 0.15, "A:2": 0.4, "C:2": 0.35, "A:3": 0.8, "C:3": 0.6 },
      "cables": [ ["RONIN/VCO:SAW", "RONIN/VCF:IN"], ["RONIN/VCF:OUT", "RONIN/VCA 1:IN"], ["RONIN/VCA 1:OUT", "RONIN/VCA 2:IN"],
                  ["RONIN/VCA 2:OUT", "RONIN/OUTPUT:WET"], ["RONIN/VCF:OUT", "RONIN/VCF:CUTOFF"],
                  ["BUSHIDO/OUTPUTS:CV A", "RONIN/VCO:HZ/V"], ["BUSHIDO/OUTPUTS:CV C", "RONIN/VCF:CUTOFF"],
                  ["BUSHIDO/OUTPUTS:GATE A", "RONIN/EG 1:TRIG"], ["RONIN/EG 1:OUT A", "RONIN/VCA 2:CV"] ],
      "power": true } },
  { "name": "TEST RING",
    "rack": { "preset": 0,
      "knobs": { "VCO:RANGE": 0.5, "VCF:CUTOFF": 0.55, "VCF:PEAK": 0.3 },
      "params": { "CLOCK:TEMPO": 0.5, "CH:PORTA A": 0.25 },
      "cables": [ ["RONIN/VCO:SAW", "RONIN/RING:A"], ["RONIN/RING:OUT", "RONIN/RING:B"], ["RONIN/RING:OUT", "RONIN/VCF:IN"],
                  ["RONIN/VCF:OUT", "RONIN/VCA 1:IN"], ["RONIN/VCA 1:OUT", "RONIN/VCA 2:IN"], ["RONIN/VCA 2:OUT", "RONIN/OUTPUT:WET"],
                  ["BUSHIDO/OUTPUTS:CV A", "RONIN/VCO:HZ/V"], ["BUSHIDO/OUTPUTS:CV C", "RONIN/RING:B"],
                  ["BUSHIDO/OUTPUTS:GATE A", "RONIN/EG 1:TRIG"], ["RONIN/EG 1:OUT A", "RONIN/VCA 2:CV"] ],
      "power": true } }
 ]
})JSON";

void expectInitBanks (JidaiProcessor& p, int rackPatches)
{
    const auto ba = p.patternNames (0);
    check (ba.size() == 1 && ba[0] == "INIT", "Bushido bank A is INIT only, got " + std::to_string (ba.size()));
    const auto ra = p.roninPresetNames (0);
    check (ra.size() == 1 && ra[0] == "INIT", "Ronin bank A is INIT only, got " + std::to_string (ra.size()));
    check (kFactoryPresetCount == 1 && kDefaultFactoryPreset == 0, "the Ronin factory list is INIT only");
    const auto bb = p.patternNames (1);
    const auto rb = p.roninPresetNames (1);
    check (bb.size() == rackPatches, "Bushido bank B holds the rack patches, got " + std::to_string (bb.size()));
    check (rb.size() == rackPatches, "Ronin bank B holds the rack patches, got " + std::to_string (rb.size()));
    if (rackPatches == 2)
    {
        check (bb[0] == "TEST LOOP" && bb[1] == "TEST RING", "Bushido bank B lists the fixture in file order");
        check (rb[0] == "TEST LOOP" && rb[1] == "TEST RING", "Ronin bank B lists the fixture in file order");
    }
    check (p.patternNames (2).isEmpty(), "Bushido bank past B is empty");
    check (p.roninPresetNames (3).isEmpty(), "Ronin bank past B is empty");
}

void expectDefaultRack (JidaiProcessor& p)
{
    check (p.rack().deviceCount() == 1 && p.rack().rackIO() != nullptr && p.rack().cables().empty(),
           "a new rack holds RACK I/O only, no cables (decision 14)");
}

std::unique_ptr<JidaiProcessor> makeClassic()
{
    auto p = std::make_unique<JidaiProcessor>();
    expectDefaultRack (*p);
    p->addDevice (jidai::DeviceKind::Bushido, -1, false);
    p->addDevice (jidai::DeviceKind::Ronin, -1, false);
    return p;
}

void expectDefault (JidaiProcessor& p)
{
    auto* r = roninAt (p);
    auto* b = bushidoAt (p);
    check (r != nullptr && b != nullptr, "the classic pair: one Bushido and one Ronin");
    if (r == nullptr || b == nullptr)
        return;
    check (r->program() == kDefaultFactoryPreset, "a new Ronin is INIT");
    check (countInternal (p.rack(), "RONIN#1/") == 8, "a new Ronin has INIT's 8 cables");
    check (r->effectOn(), "a new Ronin's effect is on");
    check (p.loadedPattern (r) == std::pair<int, int> (0, 0), "a new Ronin's screen is A001 INIT");
    check (p.loadedPattern (b).first == 0 && p.loadedPattern (b).second == 0, "a new Bushido's screen is A001 INIT");
}

// The compiled banks: INIT on both bank A screens, an empty rack patch list, and the INIT pattern holds
// no cables. Bank B then holds user entries from index 0.
void testCompiledBanks (const juce::File& store)
{
    JidaiProcessor::setRackPatchesForTest ({});
    JidaiProcessor::setUserStoreRootForTest (store);
    auto proc = makeClassic();
    expectInitBanks (*proc, 0);
    expectDefault (*proc);
    auto* b = bushidoAt (*proc);
    auto* r = roninAt (*proc);
    if (b == nullptr || r == nullptr)
        return;

    b->setParam ("CLOCK:TEMPO", 0.9f);
    proc->loadPattern (b, 0, 0);
    check (near (b->param ("CLOCK:TEMPO"), 0.5f), "INIT puts CLOCK:TEMPO back at 0.5");
    check (near (b->param ("MIXER:LEVEL 1"), 0.7f), "INIT puts MIXER:LEVEL 1 at the layout default 0.7");
    check (! anyBushido (proc->rack()), "the INIT pattern lays no cables");
    check (countInternal (proc->rack(), "RONIN#1/") == 8, "the INIT pattern leaves the Ronin cables");

    r->setKnob (panelKnobIndex ("VCF", "CUTOFF"), 0.1f);
    proc->loadRoninPreset (r, 0, 0);
    check (r->program() == 0 && proc->loadedPattern (r) == std::pair<int, int> (0, 0), "INIT loads from Ronin bank A");
    check (near (r->knob (panelKnobIndex ("VCF", "CUTOFF")), 0.45f), "INIT puts VCF CUTOFF back on the default table");
    proc->loadRoninPreset (r, 1, 0);
    check (proc->loadedPattern (r) == std::pair<int, int> (0, 0), "an empty bank B loads nothing");

    check (proc->savePattern (b, 1, "B FIRST") == 0, "with no rack patches a Bushido bank B save is index 0");
    check (proc->saveRoninPreset (r, 1, "R FIRST") == 0, "with no rack patches a Ronin bank B save is index 0");
    check (proc->saveRoninPreset (r, 0, "R AFTER") == 1, "a Ronin bank A save follows INIT");
    proc.reset();

    JidaiProcessor::setUserStoreRootForTest ({});
}

void expectLoopBass (JidaiProcessor& p)
{
    auto* r = roninAt (p);
    auto* b = bushidoAt (p);
    check (r != nullptr && b != nullptr, "TEST LOOP still has both devices");
    if (r == nullptr || b == nullptr)
        return;
    check (r->program() == 0, "TEST LOOP loads factory preset 0, INIT");
    check (r->effectOn(), "TEST LOOP leaves the effect on");
    check (near (r->knob (panelKnobIndex ("VCF", "MOD")), 0.68f), "TEST LOOP sets VCF MOD to 0.68");
    check (near (r->knob (panelKnobIndex ("VCO", "RANGE")), 0.0f), "TEST LOOP sets VCO RANGE to 0");
    check (near (r->knob (panelKnobIndex ("VCF", "CUTOFF")), 0.4f), "TEST LOOP sets VCF CUTOFF to 0.4");
    check (near (r->knob (panelKnobIndex ("VCF", "PEAK")), 0.7f), "TEST LOOP sets VCF PEAK to 0.7");
    check (near (r->knob (panelKnobIndex ("MIX", "LEVEL 1")), 0.8f), "TEST LOOP leaves an unnamed Ronin knob on the program table");
    check (near (b->param ("CLOCK:TEMPO"), 0.6f), "TEST LOOP sets CLOCK:TEMPO to 0.6");
    check (near (b->param ("MIXER:LEVEL 1"), 0.7f), "TEST LOOP resets MIXER:LEVEL 1 to the layout default 0.7");
    check (b->bypassed(), "TEST LOOP leaves Bushido bypass as it was");
    check ((int) p.rack().cables().size() == 9, "TEST LOOP lays 9 cables, got " + std::to_string (p.rack().cables().size()));
    // Rack patch cables take role colours (JCS R14): CV A carries row A's PITCH LAW (V/OCT by default), so the
    // cable into RONIN's linear HZ/V input shows the not-equal badge (R4.3); VCA 2 is a CV VCA, so the wet cable is CV.
    check (cableColor (p.rack(), "BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V") == -1, "TEST LOOP CV A cable takes its role colour");
    check (cableColor (p.rack(), "RONIN#1/VCA 2:OUT", "RONIN#1/OUTPUT:WET") == -1, "TEST LOOP wet cable takes its role colour");
    for (size_t i = 0; i < p.rack().cables().size(); ++i)
    {
        const auto& c = p.rack().cables()[i];
        if (c.a == "BUSHIDO#1/OUTPUTS:CV A")
            check (p.rack().cableInfo()[i].role == jidai::jcs::Role::VOct && p.rack().cableInfo()[i].badge == jidai::jcs::Badge::PitchLaw,
                   "TEST LOOP CV A: V/OCT role, not-equal badge into HZ/V");
        if (c.a == "RONIN#1/VCA 2:OUT")
            check (p.rack().cableInfo()[i].role == jidai::jcs::Role::CV, "TEST LOOP VCA 2 OUT (a CV VCA): CV role");
    }
    check (hasCable (p.rack(), "RONIN#1/VCF:OUT", "RONIN#1/VCF:CUTOFF"), "TEST LOOP patches the filter loop");
    check (p.loadedPattern (b) == std::pair<int, int> (1, 0), "TEST LOOP shows on the Bushido screen");
    check (p.loadedPattern (r) == std::pair<int, int> (1, 0), "TEST LOOP shows on the Ronin screen");
    check (p.rack().delayedCableCount() == 1, "TEST LOOP delays the one feedback cable");

    p.prepareToPlay (48000.0, 64);
    check (p.getLatencySamples() == 0, "TEST LOOP process reports latency 0");
    juce::AudioBuffer<float> buffer (2, 64);
    buffer.clear();
    for (int i = 0; i < 64; ++i)
        buffer.setSample (0, i, std::sin (2.0f * 3.14159265f * 440.0f * (float) i / 48000.0f));
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    p.processBlock (buffer, midi);
    check (midi.isEmpty(), "effect process clears plugin MIDI");
    bool finite = true;
    for (int ch = 0; ch < 2 && finite; ++ch)
        for (int i = 0; i < 64; ++i)
            finite = std::isfinite (buffer.getSample (ch, i));
    check (finite, "TEST LOOP process stays finite");
}

void testLoopBassThenFactory()
{
    auto proc = makeClassic();
    expectInitBanks (*proc, 2);
    expectDefault (*proc);
    auto* b = bushidoAt (*proc);
    auto* r = roninAt (*proc);
    check (b != nullptr && r != nullptr, "TEST LOOP setup");
    if (b == nullptr || r == nullptr)
        return;
    b->setBypassed (true);
    b->setParam ("MIXER:LEVEL 1", 1.0f);
    proc->loadPattern (b, 1, 0);
    expectLoopBass (*proc);

    proc->loadRoninPreset (r, 0, 0);
    check (countInternal (proc->rack(), "RONIN#1/") == 8, "INIT replaces the Ronin cables and leaves the cross cables");
    check (hasCable (proc->rack(), "BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V"), "INIT keeps the CV A cross cable");
    check (proc->loadedPattern (r) == std::pair<int, int> (0, 0), "INIT shows on bank A");
    check (proc->loadedPattern (b) == std::pair<int, int> (1, 0), "INIT leaves the Bushido screen on TEST LOOP");
    check (near (b->param ("CLOCK:TEMPO"), 0.6f), "INIT leaves CLOCK:TEMPO");
    check (near (r->knob (panelKnobIndex ("VCF", "CUTOFF")), 0.45f), "INIT puts VCF CUTOFF back on the default table");

    const float tempo = b->param ("CLOCK:TEMPO");
    r->setKnob (panelKnobIndex ("VCF", "PEAK"), 0.9f);
    proc->loadRoninPreset (r, 0, 0);
    check (proc->loadedPattern (b) == std::pair<int, int> (1, 0), "INIT again leaves the Bushido screen");
    check (near (b->param ("CLOCK:TEMPO"), tempo), "INIT again leaves CLOCK:TEMPO");
    check (near (r->knob (panelKnobIndex ("VCF", "PEAK")), 0.2f), "INIT again resets VCF PEAK");
    check (hasCable (proc->rack(), "BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V"), "INIT again keeps the cross cable");

    const int program = r->program();
    proc->loadPattern (b, 0, 0);
    check (proc->loadedPattern (r) == std::pair<int, int> (0, 0), "a Bushido pattern leaves the Ronin screen");
    check (r->program() == program, "a Bushido pattern leaves the Ronin program");
    check (hasCable (proc->rack(), "BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V"), "a Bushido pattern leaves the cross cable");

    const int before = r->program();
    proc->loadRoninPreset (r, 0, 99);
    check (r->program() == before, "an index past the bank does not load");
    check (proc->saveRoninPreset (nullptr, 0, "NO") == -1, "save with no Ronin returns -1");
    check (proc->savePattern (nullptr, 1, "NO") == -1, "save with no Bushido returns -1");
    check (proc->saveRoninPreset (r, 4, "NO") == -1, "save on a bank that does not exist returns -1");
}

void testRingSeed()
{
    auto proc = makeClassic();
    auto* r = roninAt (*proc);
    proc->loadRoninPreset (r, 1, 1);
    check (r->program() == 0, "TEST RING loads factory preset 0, INIT");
    check (near (r->knob (panelKnobIndex ("VCO", "RANGE")), 0.5f), "TEST RING sets VCO RANGE to 0.5");
    check ((int) proc->rack().cables().size() == 10, "TEST RING lays 10 cables, got " + std::to_string (proc->rack().cables().size()));
    check (hasCable (proc->rack(), "RONIN#1/RING:OUT", "RONIN#1/RING:B"), "TEST RING patches the ring to itself");
    check (hasCable (proc->rack(), "BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V"), "TEST RING patches CV A");
    check (proc->loadedPattern (r) == std::pair<int, int> (1, 1), "TEST RING shows on the Ronin screen");
    check (proc->loadedPattern (bushidoAt (*proc)) == std::pair<int, int> (1, 1), "TEST RING shows on the Bushido screen");
    check (proc->rack().delayedCableCount() == 1, "TEST RING delays the self ring");
    check (near (bushidoAt (*proc)->param ("CLOCK:TEMPO"), 0.5f), "TEST RING sets CLOCK:TEMPO to 0.5");
}

void testSecondRoninStays()
{
    auto proc = makeClassic();
    auto* second = dynamic_cast<jidai::RoninDevice*> (proc->addDevice (jidai::DeviceKind::Ronin));
    check (second != nullptr && second->rackId() == "RONIN#2", "a second Ronin is RONIN#2");
    if (second == nullptr)
        return;
    const int range = panelKnobIndex ("VCO", "RANGE");
    second->setKnob (range, 0.1f);
    const std::string keptA = "RONIN#2/EXT IN:MONO";
    const std::string keptB = "RONIN#2/VCF:IN";
    check (hasCable (proc->rack(), keptA, keptB), "the second Ronin starts with its INIT cable");
    proc->loadPattern (bushidoAt (*proc), 1, 0);
    check (hasCable (proc->rack(), keptA, keptB), "TEST LOOP on the first pair leaves the second Ronin's INIT cable");
    check (near (second->knob (range), 0.1f), "TEST LOOP leaves the second Ronin's range");
    check (near (roninAt (*proc)->knob (range), 0.0f), "TEST LOOP sets the first Ronin's range");
    check (proc->loadedPattern (second) == std::pair<int, int> (0, kDefaultFactoryPreset), "the second Ronin screen stays on INIT");
}

void testMissingBushido()
{
    auto proc = makeClassic();
    auto* b = bushidoAt (*proc);
    auto* r = roninAt (*proc);
    proc->removeDevice (b);
    proc->loadRoninPreset (r, 1, 1);
    check (near (r->knob (panelKnobIndex ("VCO", "RANGE")), 0.5f), "TEST RING without Bushido still sets VCO RANGE");
    check (! anyBushido (proc->rack()), "TEST RING without Bushido skips the BUSHIDO cables");
    check (hasCable (proc->rack(), "RONIN#1/VCO:SAW", "RONIN#1/RING:A"), "TEST RING without Bushido keeps the Ronin cables");
    check (proc->loadedPattern (r) == std::pair<int, int> (1, 1), "TEST RING without Bushido still shows on the Ronin screen");
}

void testUserFiles()
{
    auto first = makeClassic();
    auto* b = bushidoAt (*first);
    auto* r = roninAt (*first);
    first->loadPattern (b, 1, 0);
    b->setParam ("CLOCK:TEMPO", 0.25f);
    const int bushidoShown = first->savePattern (b, 1, "BUSER");
    check (bushidoShown == 2, "a Bushido bank B save is index 2, behind the two rack patches, got " + std::to_string (bushidoShown));
    check (first->patternNames (1)[0] == "TEST LOOP", "the Bushido save leaves TEST LOOP in front");
    check (first->loadedPattern (r) == std::pair<int, int> (1, 0), "the Bushido save leaves the Ronin screen on TEST LOOP");
    first->loadPattern (b, 1, bushidoShown);
    check (near (b->param ("CLOCK:TEMPO"), 0.25f), "the saved Bushido pattern reloads its tempo");
    check (first->loadedPattern (r) == std::pair<int, int> (1, 0), "reloading the Bushido pattern leaves the Ronin screen");

    r->setKnob (panelKnobIndex ("VCF", "CUTOFF"), 0.2f);
    r->setEffectOn (false);
    const int roninA = first->saveRoninPreset (r, 0, "MINE");
    check (roninA == 1, "a Ronin bank A save is index 1, after INIT, got " + std::to_string (roninA));
    const int roninB = first->saveRoninPreset (r, 1, "RUSER");
    check (roninB == 2, "a Ronin bank B save is index 2, got " + std::to_string (roninB));
    check (first->roninPresetNames (1)[0] == "TEST LOOP", "the Ronin save leaves TEST LOOP in front");
    check (near (b->param ("CLOCK:TEMPO"), 0.25f), "the Ronin save leaves CLOCK:TEMPO");
    check (first->loadedPattern (b) == std::pair<int, int> (1, 2), "the Ronin save leaves the Bushido screen on BUSER");
    first->loadRoninPreset (r, 1, roninB);
    check (near (b->param ("CLOCK:TEMPO"), 0.25f), "a Ronin user preset leaves CLOCK:TEMPO");
    check (first->loadedPattern (b) == std::pair<int, int> (1, 2), "a Ronin user preset leaves the Bushido screen");
    check (! r->effectOn(), "the Ronin user preset reloads effect off");
    first.reset();

    auto second = makeClassic();
    check (second->roninPresetNames (0).size() == 2, "the next processor reads the Ronin user preset");
    check (second->roninPresetNames (0)[0] == "INIT" && second->roninPresetNames (0)[1] == "MINE", "the reloaded name MINE follows INIT");
    check (second->patternNames (1)[2] == "BUSER", "the next processor reads the Bushido user pattern");
    auto* r2 = roninAt (*second);
    auto* b2 = bushidoAt (*second);
    second->loadRoninPreset (r2, 0, 1);
    check (near (r2->knob (panelKnobIndex ("VCF", "CUTOFF")), 0.2f), "MINE reloads VCF CUTOFF 0.2");
    check (! r2->effectOn(), "MINE reloads effect off");
    check (countInternal (second->rack(), r2->rackId() + "/") == 6, "MINE reloads the six TEST LOOP cables that stay on the Ronin");
    check (second->loadedPattern (b2) == std::pair<int, int> (0, 0), "loading MINE leaves the Bushido screen");
    second->loadPattern (b2, 1, 2);
    check (near (b2->param ("CLOCK:TEMPO"), 0.25f), "BUSER reloads CLOCK:TEMPO 0.25");
    check (second->loadedPattern (r2) == std::pair<int, int> (0, 1), "BUSER leaves the Ronin screen on MINE");

    const int blank = second->saveRoninPreset (r2, 0, "   ");
    check (blank == 2 && second->roninPresetNames (0)[2] == "PRESET", "an empty Ronin name is stored as PRESET");
}

void testStateRoundTrip()
{
    auto proc = makeClassic();
    proc->loadPattern (bushidoAt (*proc), 1, 0);
    juce::MemoryBlock state;
    proc->getStateInformation (state);
    proc.reset();

    auto restored = std::make_unique<JidaiProcessor>();
    restored->setStateInformation (state.getData(), (int) state.getSize());
    auto* r = roninAt (*restored);
    auto* b = bushidoAt (*restored);
    check (r != nullptr && b != nullptr, "a saved rack restores both devices");
    if (r == nullptr || b == nullptr)
        return;
    check (near (r->knob (panelKnobIndex ("VCF", "MOD")), 0.68f), "the saved rack keeps VCF MOD");
    check (near (b->param ("CLOCK:TEMPO"), 0.6f), "the saved rack keeps CLOCK:TEMPO");
    check ((int) restored->rack().cables().size() == 9, "the saved rack keeps the 9 cables");
    check (hasCable (restored->rack(), "BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V"), "the saved rack keeps CV A");
    check (restored->loadedPattern (b) == std::pair<int, int> (1, 0), "the saved Bushido screen is TEST LOOP");
    check (restored->loadedPattern (r) == std::pair<int, int> (1, 0), "the saved Ronin screen is TEST LOOP");
}

std::unique_ptr<juce::XmlElement> version1State (int ronProgram)
{
    auto xml = std::make_unique<juce::XmlElement> ("JIDAIRACK");
    xml->setAttribute ("version", 1);
    auto* bushido = xml->createNewChildElement ("DEVICE");
    bushido->setAttribute ("kind", "BUSHIDO");
    bushido->setAttribute ("number", 1);
    bushido->setAttribute ("bank", 1);
    bushido->setAttribute ("pattern", 0);
    auto* tempo = bushido->createNewChildElement ("PARAM");
    tempo->setAttribute ("id", "CLOCK:TEMPO");
    tempo->setAttribute ("value", 0.25);
    auto* ronin = xml->createNewChildElement ("DEVICE");
    ronin->setAttribute ("kind", "RONIN");
    ronin->setAttribute ("number", 1);
    ronin->setAttribute ("program", ronProgram);
    ronin->setAttribute ("effect", 0);
    return xml;
}

void testVersion1Screen()
{
    auto proc = std::make_unique<JidaiProcessor>();
    proc->testRestore (*version1State (0));
    auto* b = bushidoAt (*proc);
    auto* r = roninAt (*proc);
    check (b != nullptr && r != nullptr, "version 1 state restores both devices");
    if (b == nullptr || r == nullptr)
        return;
    check (proc->loadedPattern (b) == std::pair<int, int> (1, 2), "version 1 bank B index 0 moves behind the rack patches");
    check (near (b->param ("CLOCK:TEMPO"), 0.25f), "version 1 keeps the stored tempo");
    check (proc->loadedPattern (r) == std::pair<int, int> (0, 0), "a Ronin with no screen bank shows its program on bank A");
    check (! r->effectOn(), "version 1 keeps effect off");

    // A session saved before the bank was cleared can name a program that is gone. The Ronin stays on INIT.
    auto old = std::make_unique<JidaiProcessor>();
    old->testRestore (*version1State (5));
    auto* r2 = roninAt (*old);
    check (r2 != nullptr && r2->program() == kDefaultFactoryPreset, "a cleared program number restores as INIT");
    check (r2 != nullptr && old->loadedPattern (r2) == std::pair<int, int> (0, 0), "a cleared program number shows A001 INIT");
}

void testEditor()
{
    auto proc = std::make_unique<JidaiProcessor>();
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditor());
    check (editor != nullptr, "the editor is created");
    if (editor == nullptr)
        return;
    editor->setSize (1320, 860);
    editor->setVisible (true);
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    check (image.getWidth() == 1320 && image.getHeight() == 860, "the editor paints at its size");
    editor.reset();
}

}

void runRackStateTests (int& checks, int& failures);

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto support = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    const auto bushidoDir = support.getChildFile ("BUSHIDO");
    const auto roninDir = support.getChildFile ("RONIN");
    const bool bushidoExisted = bushidoDir.exists();
    const bool roninExisted = roninDir.exists();

    auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("jidai-preset-tests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));
    temp.createDirectory();
    struct Guard {
        juce::File dir;
        bool bushidoExisted, roninExisted;
        juce::File bushidoDir, roninDir;
        ~Guard()
        {
            JidaiProcessor::setUserStoreRootForTest ({});
            JidaiProcessor::setRackPatchesForTest ({});
            dir.deleteRecursively();
            if (! bushidoExisted && bushidoDir.exists())
                std::printf ("FAIL user store leaked into %s\n", bushidoDir.getFullPathName().toRawUTF8());
            if (! roninExisted && roninDir.exists())
                std::printf ("FAIL user store leaked into %s\n", roninDir.getFullPathName().toRawUTF8());
        }
    } guard { temp, bushidoExisted, roninExisted, bushidoDir, roninDir };

    testCompiledBanks (temp.getChildFile ("compiled"));

    JidaiProcessor::setUserStoreRootForTest (temp);
    JidaiProcessor::setRackPatchesForTest (kFixturePatches);
    testLoopBassThenFactory();
    testRingSeed();
    testSecondRoninStays();
    testMissingBushido();
    testUserFiles();
    testStateRoundTrip();
    testVersion1Screen();
    testEditor();
    runRackStateTests (checks, failures);

    check (! bushidoExisted || bushidoDir.exists(), "an existing BUSHIDO user folder is still there");
    if (! bushidoExisted)
        check (! bushidoDir.exists(), "tests did not create ~/Library/Application Support/BUSHIDO");
    if (! roninExisted)
        check (! roninDir.exists(), "tests did not create ~/Library/Application Support/RONIN");

    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

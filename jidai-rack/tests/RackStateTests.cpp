// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
//
// Rack state v3 through JidaiProcessor (JIDAI_RACK_Redesign 4): the default rack, a full save/restore round trip
// (devices, names, OPEN/CLOSED, fold, ORIGAMI state, cable colour overrides, auto and legacyInvert flags, view and
// cable modes), the v2 -> v3 migration (M3, M4c, M5, BUSHIDO format 0 -> 1) with the host audio equal to the v2
// routing within float rounding, latency reporting, MIDI into RACK I/O, and a newer version refused.

#include "plugin/JidaiProcessor.h"

#include "Modular/PatchState.h"
#include "UI/PatchBayLogic.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

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

const CableSpec* findCable (const Rack& rack, const std::string& a, const std::string& b)
{
    for (auto& c : rack.cables())
        if ((c.a == a && c.b == b) || (c.a == b && c.b == a))
            return &c;
    return nullptr;
}

juce::String stateXml (JidaiProcessor& p)
{
    juce::MemoryBlock m;
    p.getStateInformation (m);
    auto xml = juce::AudioProcessor::getXmlFromBinary (m.getData(), (int) m.getSize());
    return xml != nullptr ? xml->toString() : juce::String();
}

void testDefaultRack()
{
    JidaiProcessor p;
    check (p.rack().deviceCount() == 1 && p.rack().device (0)->kind() == DeviceKind::RackIO, "default rack: RACK I/O only");
    p.prepareToPlay (48000.0, 64);
    check (p.getLatencySamples() == 0, "default rack: latency 0");
    juce::AudioBuffer<float> buffer (2, 64);
    for (int i = 0; i < 64; ++i)
        buffer.setSample (0, i, 0.5f), buffer.setSample (1, i, 0.5f);
    juce::MidiBuffer midi;
    p.processBlock (buffer, midi);
    check (buffer.getMagnitude (0, 64) == 0.0f, "default rack: nothing patched to MAIN OUT is silence");
}

void testRoundTrip()
{
    JidaiProcessor p;
    p.prepareToPlay (48000.0, 64);
    auto* b = static_cast<BushidoDevice*> (p.addDevice (DeviceKind::Bushido));
    auto* r = static_cast<RoninDevice*> (p.addDevice (DeviceKind::Ronin));
    auto* o = static_cast<OrigamiDevice*> (p.addDevice (DeviceKind::Origami));
    b->name = "BASS SEQ";
    b->closed = true;
    b->setParam ("CLOCK:TEMPO", 0.66f);
    b->setParam ("STEPS:QUANT A", 1.0f);
    r->folded = true;
    r->setKnob (panelKnobIndex ("VCF", "CUTOFF"), 0.33f);
    o->closed = true;
    o->setParam ("wave", 0.71);
    o->setParam (origami::kQuality, 1.0);
    p.rack().rackIO()->setMainLevel (0.8f);
    p.rack().connect ("RONIN#1/MG:TRI", "ORIGAMI#1/VC:VC 2", 3);      // blue override
    auto cables = p.rack().cables();
    cables.push_back ({ "RONIN#1/EXT IN:GATE", "BUSHIDO#1/INPUTS:STEP", -1, 999, false, true });
    p.setCables (cables);
    p.showBack = true;
    p.cableModeFront = JidaiProcessor::CablesSelected;
    p.cableModeBack = JidaiProcessor::CablesHide;
    p.scalePercent = 125;
    p.refreshLatency();
    check (p.getLatencySamples() == 46, "ORIGAMI 2x in the auto-routed rack: the host gets latency 46, got " + std::to_string (p.getLatencySamples()));

    const auto first = stateXml (p);
    juce::MemoryBlock m;
    p.getStateInformation (m);
    JidaiProcessor q;
    q.setStateInformation (m.getData(), (int) m.getSize());
    const auto second = stateXml (q);
    check (first == second && first.contains ("version=\"3\""), "v3 round trip: the restored rack saves byte-identical XML");

    auto* b2 = dynamic_cast<BushidoDevice*> (q.rack().findDevice ("BUSHIDO#1"));
    auto* r2 = dynamic_cast<RoninDevice*> (q.rack().findDevice ("RONIN#1"));
    auto* o2 = dynamic_cast<OrigamiDevice*> (q.rack().findDevice ("ORIGAMI#1"));
    check (b2 != nullptr && r2 != nullptr && o2 != nullptr && q.rack().indexOf (q.rack().rackIO()) == 0, "round trip: all four devices, RACK I/O on top");
    if (b2 == nullptr || r2 == nullptr || o2 == nullptr)
        return;
    check (b2->name == "BASS SEQ" && b2->closed && ! b2->folded, "round trip: BUSHIDO name and CLOSED");
    check (std::fabs (b2->param ("CLOCK:TEMPO") - 0.66f) < 1e-6f && b2->param ("STEPS:QUANT A") == 1.0f, "round trip: BUSHIDO params incl. tab params");
    check (r2->folded && ! r2->closed && std::fabs (r2->knob (panelKnobIndex ("VCF", "CUTOFF")) - 0.33f) < 1e-6f, "round trip: RONIN fold and knob ids");
    check (o2->closed && std::fabs (o2->param (origami::paramIndex ("wave")) - 0.71) < 1e-9 && o2->latencySamples() == 46,
           "round trip: ORIGAMI CLOSED, WAVE, 2x");
    check (std::fabs (q.rack().rackIO()->mainLevel() - 0.8f) < 1e-6f, "round trip: MAIN level");
    const auto* blue = findCable (q.rack(), "RONIN#1/MG:TRI", "ORIGAMI#1/VC:VC 2");
    check (blue != nullptr && blue->color == 3, "round trip: colour override kept");
    const auto* autoCable = findCable (q.rack(), "ORIGAMI#1/HOST:OUT L", "RACK#1/MAIN:OUT L");
    check (autoCable != nullptr && autoCable->autoRouted && autoCable->color == -1, "round trip: auto flag and role colour kept");
    const auto* legacy = findCable (q.rack(), "RONIN#1/EXT IN:GATE", "BUSHIDO#1/INPUTS:STEP");
    check (legacy != nullptr && legacy->legacyInvert, "round trip: legacyInvert kept");
    check (q.showBack && q.cableModeFront == JidaiProcessor::CablesSelected && q.cableModeBack == JidaiProcessor::CablesHide && q.scalePercent == 125,
           "round trip: view, cable modes and scale");
    q.prepareToPlay (48000.0, 64);
    check (q.getLatencySamples() == 46, "round trip: latency reported again");
}

// A v2 rack exactly as the v2 plugin wrote it.
std::unique_ptr<juce::XmlElement> version2State()
{
    auto xml = std::make_unique<juce::XmlElement> ("JIDAIRACK");
    xml->setAttribute ("version", 2);
    xml->setAttribute ("browser", 1);
    auto* bushido = xml->createNewChildElement ("DEVICE");
    bushido->setAttribute ("kind", "BUSHIDO");
    bushido->setAttribute ("number", 1);
    bushido->setAttribute ("bank", 0);
    bushido->setAttribute ("pattern", 0);
    auto* tempo = bushido->createNewChildElement ("PARAM");
    tempo->setAttribute ("id", "CLOCK:TEMPO");
    tempo->setAttribute ("value", 0.4);
    for (int n = 1; n <= 2; ++n)
    {
        auto* ronin = xml->createNewChildElement ("DEVICE");
        ronin->setAttribute ("kind", "RONIN");
        ronin->setAttribute ("number", n);
        ronin->setAttribute ("program", 0);
        ronin->setAttribute ("effect", 1);
        juce::StringArray knobs;
        for (int k = 0; k < kPanelKnobCount; ++k)
            knobs.add (juce::String (kPanelKnobs[k].valueDefault, 6));
        if (n == 1)
            knobs.set (panelKnobIndex ("VCF", "CUTOFF"), "0.300000");
        ronin->setAttribute ("knobs", knobs.joinIntoString (","));
    }
    // RONIN#1's INIT program cables are re-made by the program; the cables below are the patch.
    struct C { const char* a; const char* b; int color; };
    const C list[] = {
        { "BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V", 2 },          // automatic yellow -> role colour
        { "BUSHIDO#1/OUTPUTS:GATE A", "RONIN#1/VCA 1:ENV", 1 },       // gate into a non-S-trig RONIN input: M3
        { "RONIN#1/EXT IN:GATE", "BUSHIDO#1/INPUTS:STEP", 1 },        // S-trig source into BUSHIDO: M3
        { "BUSHIDO#1/OUTPUTS:GATE B", "RONIN#2/EG 1:TRIG", 1 },       // into an S-trig input: same law, no flag
        { "RONIN#2/MG:TRI", "BUSHIDO#1/CLOCK:TEMPO CV", 3 },          // user picked green (automatic was red: MG TRI is audio)
    };
    int age = 100;
    for (auto& c : list)
    {
        auto* e = xml->createNewChildElement ("CABLE");
        e->setAttribute ("a", c.a);
        e->setAttribute ("b", c.b);
        e->setAttribute ("color", c.color);
        e->setAttribute ("age", age++);
    }
    return xml;
}

void testVersion2Migration()
{
    JidaiProcessor p;
    p.testRestore (*version2State());
    auto& rack = p.rack();
    check (rack.rackIO() != nullptr && rack.indexOf (rack.rackIO()) == 0 && rack.deviceCount() == 4, "v2: RACK I/O inserted at the top (M5)");
    check (findCable (rack, "RACK#1/HOST:IN L", "RONIN#1/HOST:IN L") && findCable (rack, "RACK#1/HOST:IN R", "RONIN#1/HOST:IN R"),
           "v2: HOST IN -> first RONIN (M5)");
    check (findCable (rack, "RONIN#1/HOST:OUT L", "RACK#1/MAIN:OUT L") && findCable (rack, "RONIN#2/HOST:OUT R", "RACK#1/MAIN:OUT R")
               && ! findCable (rack, "RACK#1/HOST:IN L", "RONIN#2/HOST:IN L"),
           "v2: every RONIN -> MAIN OUT, only the first gets host audio (M5)");
    auto flag = [&] (const char* a, const char* b) { auto* c = findCable (rack, a, b); return c != nullptr && c->legacyInvert; };
    check (flag ("BUSHIDO#1/OUTPUTS:GATE A", "RONIN#1/VCA 1:ENV") && flag ("RONIN#1/EXT IN:GATE", "BUSHIDO#1/INPUTS:STEP"),
           "v2: cables whose gate law changes keep it (legacyInvert, M3)");
    check (! flag ("BUSHIDO#1/OUTPUTS:GATE B", "RONIN#2/EG 1:TRIG") && ! flag ("BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V"),
           "v2: cables whose law is unchanged get no flag");
    check (p.migrationNotice.contains ("2 cables kept their old S-trig inversion"), "v2: the notice counts the flagged cables: " + p.migrationNotice.toStdString());
    auto colour = [&] (const char* a, const char* b) { auto* c = findCable (rack, a, b); return c != nullptr ? c->color : -9; };
    check (colour ("BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V") == -1, "v2: an automatic colour loads as the role colour (M4c)");
    check (colour ("RONIN#2/MG:TRI", "BUSHIDO#1/CLOCK:TEMPO CV") == 2, "v2: a user colour loads as an override (green)");
    auto* b = dynamic_cast<BushidoDevice*> (rack.findDevice ("BUSHIDO#1"));
    check (b != nullptr && b->param ("CLOCK:SETTLE") == 1.0f && b->param ("STEPS:LAW A") == 1.0f && b->param ("STEPS:LAW B") == 0.0f
               && std::fabs (b->param ("CLOCK:TEMPO") - 0.4f) < 1e-6f,
           "v2: BUSHIDO format 0 -> 1 (SETTLE VINTAGE, row A law HZ/V LIN from its RONIN HZ/V cable, tempo kept)");
    auto* r = dynamic_cast<RoninDevice*> (rack.findDevice ("RONIN#1"));
    check (r != nullptr && std::fabs (r->knob (panelKnobIndex ("VCF", "CUTOFF")) - 0.3f) < 1e-6f, "v2: RONIN knob CSV restored");
    check (! p.showBack && p.cableModeFront == JidaiProcessor::CablesHidePassThru && p.cableModeBack == JidaiProcessor::CablesAll,
           "v2: view defaults FRONT, HIDE PASS-THRU front, ALL back");
    const auto saved = stateXml (p);
    check (saved.contains ("version=\"3\"") && saved.contains ("<KNOB") && ! saved.contains ("knobs="), "v2 saves back as v3 with knob ids");
}

// v2 audio: host -> first RONIN's EXT IN, output = sum of every RONIN's OUTPUT. Rebuilt here on a bare Rack (no
// RACK I/O) driving setHostSample and reading hostLeft/Right, against the migrated processor.
void testVersion2Audio()
{
    auto xml = std::make_unique<juce::XmlElement> ("JIDAIRACK");
    xml->setAttribute ("version", 2);
    for (int n = 1; n <= 2; ++n)
    {
        auto* ronin = xml->createNewChildElement ("DEVICE");
        ronin->setAttribute ("kind", "RONIN");
        ronin->setAttribute ("number", n);
        ronin->setAttribute ("program", 0);
        ronin->setAttribute ("effect", n == 1 ? 0 : 1);      // RONIN#1 passes the host dry, RONIN#2 plays its saw
    }
    auto* saw = xml->createNewChildElement ("CABLE");
    saw->setAttribute ("a", "RONIN#2/VCO:SAW");
    saw->setAttribute ("b", "RONIN#2/OUTPUT:WET");
    saw->setAttribute ("color", 0);
    saw->setAttribute ("age", 50);
    JidaiProcessor p;
    p.testRestore (*xml);
    p.prepareToPlay (48000.0, 256);

    Rack old;
    old.prepare (48000.0, 256);
    auto* r1 = static_cast<RoninDevice*> (old.addDevice (DeviceKind::Ronin));
    auto* r2 = static_cast<RoninDevice*> (old.addDevice (DeviceKind::Ronin));
    r1->setEffectOn (false);
    old.connect ("RONIN#2/VCO:SAW", "RONIN#2/OUTPUT:WET");

    const int blocks = 40, n = 256;
    double worst = 0.0, peak = 0.0;
    juce::AudioBuffer<float> buffer (2, n);
    juce::MidiBuffer midi;
    std::vector<float> scratch (1);
    for (int blk = 0; blk < blocks; ++blk)
    {
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) (blk * n + i) / 48000.0f;
            buffer.setSample (0, i, 0.4f * std::sin (2.0f * 3.14159265f * 220.0f * t));
            buffer.setSample (1, i, 0.3f * std::sin (2.0f * 3.14159265f * 330.0f * t));
        }
        juce::AudioBuffer<float> input (buffer);
        p.processBlock (buffer, midi);
        for (int i = 0; i < n; ++i)
        {
            r1->setHostSample (input.getSample (0, i), input.getSample (1, i));
            r2->setHostSample (0.0f, 0.0f);
            old.process (nullptr, nullptr, scratch.data(), scratch.data(), 1);
            const float l = r1->hostLeft() + r2->hostLeft();
            const float r = r1->hostRight() + r2->hostRight();
            worst = std::fmax (worst, std::fabs ((double) buffer.getSample (0, i) - l));
            worst = std::fmax (worst, std::fabs ((double) buffer.getSample (1, i) - r));
            peak = std::fmax (peak, std::fabs ((double) l));
        }
    }
    std::printf ("INFO v2 audio: peak %.6f, max |v3 - v2| %.3g (float eps %.3g)\n", peak, worst, (double) FLT_EPSILON);
    check (peak > 0.01, "v2 audio: the RONIN pair makes sound");
    check (worst <= 4.0 * FLT_EPSILON * std::fmax (1.0, peak), "v2 audio: migrated rack equals the v2 routing within float rounding, max diff " + std::to_string (worst));
}

void testMidiAndDefaults()
{
    JidaiProcessor p;
    p.prepareToPlay (48000.0, 64);
    juce::AudioBuffer<float> buffer (2, 64);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 64), 10);
    p.processBlock (buffer, midi);
    check (midi.isEmpty(), "MIDI in is consumed, nothing goes out");
    check (p.rack().jackVolts ("RACK#1/MIDI:GATE") == 5.0f && std::fabs (p.rack().jackVolts ("RACK#1/MIDI:NOTE") - 2.0f) < 1e-6f,
           "host MIDI note 72 reaches RACK I/O: GATE 5 V, NOTE 2 V (C5, 0 V = C3)");

    Transport t;
    t.valid = true;
    t.playing = true;
    p.rack().setTransport (t);
    auto* b = static_cast<BushidoDevice*> (p.addDevice (DeviceKind::Bushido));
    check (b->param ("CLOCK:SOURCE") == 1.0f && b->param ("CLOCK:EXT SOURCE") == 1.0f, "a BUSHIDO inserted while the host plays clocks from HOST");
    t.playing = false;
    p.rack().setTransport (t);
    auto* b2 = static_cast<BushidoDevice*> (p.addDevice (DeviceKind::Bushido));
    check (b2->param ("CLOCK:SOURCE") == 0.0f, "a BUSHIDO inserted while the host is stopped keeps INT");
}

// RONIN state format 2 (RONIN_Redesign 6): a RONIN saved before RONIN's redesign (format 1) is migrated once
// (M-R1 EG knobs, M-R2 PARABOLA, M-R5 VCF CUTOFF for one direct VCO SAW cable); format 2 round-trips untouched.
void testRoninFormat2()
{
    juce::XmlElement xml ("JIDAIRACK");
    xml.setAttribute ("version", 3);
    xml.createNewChildElement ("DEVICE")->setAttribute ("kind", "RACK");
    auto* d = xml.createNewChildElement ("DEVICE");
    d->setAttribute ("kind", "RONIN");
    d->setAttribute ("number", 1);
    d->setAttribute ("format", 1);
    auto knob = [&] (const char* id, double v) { auto* k = d->createNewChildElement ("KNOB"); k->setAttribute ("id", id); k->setAttribute ("value", v); };
    knob ("EG 1:ATTACK", 0.5);
    knob ("EG 1:DECAY", 0.5);
    knob ("EG 2:RELEASE", 1.0);
    knob ("VCF:CUTOFF", 0.4);
    auto* c = xml.createNewChildElement ("CABLE");
    c->setAttribute ("a", "RONIN#1/VCO:SAW");
    c->setAttribute ("b", "RONIN#1/VCF:IN");
    JidaiProcessor p;
    p.testRestore (xml);
    auto* r = dynamic_cast<RoninDevice*> (p.rack().findDevice ("RONIN#1"));
    check (r != nullptr, "RONIN format 1: device restored");
    if (r == nullptr)
        return;
    auto k = [&] (const char* s, const char* l) { return (double) r->knob (panelKnobIndex (s, l)); };
    check (std::fabs (k ("EG 1", "ATTACK") - 0.5846) < 1e-4 && std::fabs (k ("EG 1", "DECAY") - 0.5574) < 1e-4
               && std::fabs (k ("EG 2", "RELEASE") - 0.9760) < 1e-4,
           "RONIN format 1: M-R1 EG knobs keep their segment times (0.5 -> 0.5846 attack, 0.5574 decay, 1.0 -> 0.9760 release)");
    check (r->triShape() == 1, "RONIN format 1: M-R2 VCO TRI SHAPE PARABOLA");
    const double comp = patchstate::compensateCutoff (0.4, 2.5);
    check (std::fabs (k ("VCF", "CUTOFF") - comp) < 1e-6 && std::fabs (comp - 0.4) > 1e-4,
           "RONIN format 1: M-R5 VCF CUTOFF compensated for the direct VCO SAW (0.4 -> " + std::to_string (comp) + ")");
    check (p.migrationNotice.contains ("RONIN") && p.migrationNotice.contains ("PARABOLA"), "RONIN format 1: reported: " + p.migrationNotice.toStdString());
    const auto saved = stateXml (p);
    check (saved.contains ("format=\"2\"") && saved.contains ("triShape=\"parabola\""), "RONIN saves as format 2 with its TRI SHAPE");

    JidaiProcessor q;
    q.testRestore (*juce::parseXML (saved));
    auto* r2 = dynamic_cast<RoninDevice*> (q.rack().findDevice ("RONIN#1"));
    check (r2 != nullptr && std::fabs ((double) r2->knob (panelKnobIndex ("EG 1", "ATTACK")) - k ("EG 1", "ATTACK")) < 1e-6
               && r2->triShape() == 1 && q.migrationNotice.isEmpty(),
           "RONIN format 2 round-trips without a second migration");

    JidaiProcessor fresh;
    fresh.addDevice (DeviceKind::Ronin);
    check (stateXml (fresh).contains ("triShape=\"triangle\""), "a new RONIN starts on the true TRIANGLE");
}

void testNewerRefused()
{
    JidaiProcessor p;
    p.addDevice (DeviceKind::Ronin);
    juce::XmlElement xml ("JIDAIRACK");
    xml.setAttribute ("version", 4);
    xml.createNewChildElement ("DEVICE")->setAttribute ("kind", "BUSHIDO");
    p.testRestore (xml);
    check (p.rack().findDevice ("RONIN#1") != nullptr && p.rack().findDevice ("BUSHIDO#1") == nullptr && p.migrationNotice.contains ("newer"),
           "a version 4 rack is refused and the current rack stays (JCS R7)");
}

}

void runRackStateTests (int& checks, int& failures)
{
    gChecks = &checks;
    gFailures = &failures;
    testDefaultRack();
    testRoundTrip();
    testVersion2Migration();
    testVersion2Audio();
    testMidiAndDefaults();
    testNewerRefused();
    testRoninFormat2();
}

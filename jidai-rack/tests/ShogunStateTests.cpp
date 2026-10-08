// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

// SHOGUN's state in a rack: every parameter, the pattern (steps, accents, locks, lengths), the mod matrix, CV AMT and
// input laws survive a save / load of the rack byte for byte, a SHOGUN plugin patch (with cables of SHOGUN's own bay
// and old alias ids) loads into the rack device, and GLOBAL:OS from a state sets the device latency. SHOGUN's factory
// programs (INIT + 21 kits) load into a rack SHOGUN, keep it on the host clock, save their program number, and play.

#include "plugin/JidaiProcessor.h"
#include "plugin/ShogunState.h"

#include <cmath>
#include <algorithm>
#include <cstdio>

using namespace jidai;

namespace {

int* gChecks = nullptr;
int* gFailures = nullptr;

void check (bool ok, const juce::String& what)
{
    ++*gChecks;
    if (! ok)
    {
        ++*gFailures;
        std::printf ("FAIL %s\n", what.toRawUTF8());
    }
}

juce::String stateText (JidaiProcessor& p)
{
    juce::MemoryBlock m;
    p.getStateInformation (m);
    auto xml = juce::AudioProcessor::getXmlFromBinary (m.getData(), (int) m.getSize());
    return xml != nullptr ? xml->toString() : juce::String();
}

ShogunDevice* firstShogun (JidaiProcessor& p)
{
    for (int i = 0; i < p.rack().deviceCount(); ++i)
        if (auto* s = dynamic_cast<ShogunDevice*> (p.rack().device (i)))
            return s;
    return nullptr;
}

void testRoundTrip()
{
    JidaiProcessor a;
    a.prepareToPlay (48000.0, 512);
    auto* s = static_cast<ShogunDevice*> (a.addDevice (DeviceKind::Shogun, -1, true, false));
    check (s != nullptr, "SHOGUN added to a rack");
    if (s == nullptr)
        return;
    check (std::abs (s->param (shogun::P_CLOCK_SOURCE) - shogun::stepU (0, 3)) < 1.0e-6, "a new SHOGUN follows the host clock (CLOCK:SOURCE HOST)");
    // Parameters: a spread of values over every kind (continuous, bipolar, stepped, toggle).
    for (int p = 0; p < shogun::kParamCount; p += 7)
        s->setParam (p, std::fmod (0.137 * (p + 1), 1.0));
    s->setParam (shogun::P_GLOBAL_OS, shogun::stepU (2, 3));             // 4x
    auto e = s->edits();
    e.pattern.setName ("RT TEST");
    e.pattern.seed = 0x1234ABCDu;
    auto& bd = e.pattern.tracks[shogun::BD1];
    bd.len = 12;
    bd.scale = 2;
    bd.swing = 0.6;
    bd.shift = 0.25;
    bd.steps[0].on = true;
    bd.steps[0].acc = 3;
    bd.steps[3].on = true;
    bd.steps[3].prob = 0.5f;
    bd.steps[3].ratchet = 3;
    bd.steps[3].setLock (shogun::findParam ("BD1:TUNE"), 0.75f);
    auto& bass = e.pattern.tracks[shogun::BASS];
    bass.steps[1].on = true;
    bass.steps[1].note = 41;
    bass.steps[1].tie = true;
    bass.steps[2].on = true;
    bass.steps[2].bend = -3.0f;
    e.rows[0].src = shogun::mod::SRC_LFO1;
    e.rows[0].dst = shogun::findParam ("SD:TUNE");
    e.rows[0].depth = -0.4;
    e.rows[0].curve = 2;
    e.cvAmt[(size_t) shogun::drumPort (shogun::SD, shogun::DJ_PITCH)] = -0.5;
    e.inLaw[(size_t) shogun::synthPort (1, shogun::SJ_NOTE)] = 1;
    s->setEdits (e);
    a.refreshLatency();
    check (s->osFactor() == 4 && a.getLatencySamples() == 26, "GLOBAL:OS 4x: SHOGUN re-prepared, plugin latency 26 (got "
                                                                   + juce::String (a.getLatencySamples()) + ")");

    juce::MemoryBlock m;
    a.getStateInformation (m);
    const juce::String first = stateText (a);
    check (first.contains ("<SHOGUN") && first.contains ("kind=\"SHOGUN\""), "rack state carries <DEVICE kind=\"SHOGUN\"> with the SHOGUN state");

    JidaiProcessor b;
    b.prepareToPlay (48000.0, 512);
    b.setStateInformation (m.getData(), (int) m.getSize());
    auto* t = firstShogun (b);
    check (t != nullptr && b.migrationNotice.isEmpty(), "SHOGUN reloads with no migration notice");
    if (t == nullptr)
        return;
    int bad = 0;
    for (int p = 0; p < shogun::kParamCount; ++p)
        bad += std::abs (t->param (p) - s->param (p)) > 1.0e-12 ? 1 : 0;
    check (bad == 0, "all " + juce::String (shogun::kParamCount) + " SHOGUN parameters hold (" + juce::String (bad) + " off)");
    const auto f = t->edits();
    const auto& tb = f.pattern.tracks[shogun::BD1];
    check (juce::String (f.pattern.name) == "RT TEST" && f.pattern.seed == 0x1234ABCDu, "pattern name and seed hold");
    check (tb.len == 12 && tb.scale == 2 && std::abs (tb.swing - 0.6) < 1e-12 && std::abs (tb.shift - 0.25) < 1e-12, "track length, scale, swing, shift hold");
    check (tb.steps[0].on && tb.steps[0].acc == 3 && tb.steps[3].on && std::abs (tb.steps[3].prob - 0.5f) < 1e-6f && tb.steps[3].ratchet == 3
               && tb.steps[3].nLocks == 1 && std::abs (tb.steps[3].locks[0].u - 0.75f) < 1e-6f,
           "steps, accent, probability, ratchet and the p-lock hold");
    const auto& tbass = f.pattern.tracks[shogun::BASS];
    check (tbass.steps[1].note == 41 && tbass.steps[1].tie && std::abs (tbass.steps[2].bend + 3.0f) < 1e-6f, "synth notes, ties and bends hold");
    check (f.rows[0].src == shogun::mod::SRC_LFO1 && f.rows[0].dst == e.rows[0].dst && std::abs (f.rows[0].depth + 0.4) < 1e-12 && f.rows[0].curve == 2,
           "mod matrix row holds");
    check (std::abs (f.cvAmt[(size_t) shogun::drumPort (shogun::SD, shogun::DJ_PITCH)] + 0.5) < 1e-12
               && f.inLaw[(size_t) shogun::synthPort (1, shogun::SJ_NOTE)] == 1, "CV AMT and the input law hold");
    check (t->osFactor() == 4 && b.getLatencySamples() == 26, "a loaded 4x SHOGUN reports 26 samples");
    check (stateText (b) == first, "SHOGUN rack state round trip is byte-identical");
}

// A SHOGUN plugin patch (the plugin's own <SHOGUN version="2"> state) with an old alias id and a cable of SHOGUN's
// bay: the alias resolves through shogun::resolvePort, the bay cable becomes a rack cable on the device.
void testPluginPatch()
{
    const juce::String json = R"({"format":"shogun-patch","version":2,"params":{"CLOCK:TEMPO":0.62,"BD1:LEVEL":0.9},
        "mod":[],"cables":[["SHOGUN/MOD:LFO 1","SHOGUN/SD:TONE"],["SHOGUN/BD1:ENV","SHOGUN/BASS:HZ/V"]],
        "cvAmt":{},"inLaw":{},"seq":{"pattern":"PLUGIN","seed":7,"tracks":[{"id":"BD1","len":16,"steps":[{"i":0,"on":true,"acc":3}]}]}})";
    juce::XmlElement rack ("JIDAIRACK");
    rack.setAttribute ("version", 3);
    rack.createNewChildElement ("DEVICE")->setAttribute ("kind", "RACK I/O");
    auto* d = rack.createNewChildElement ("DEVICE");
    d->setAttribute ("kind", "SHOGUN");
    d->setAttribute ("number", 1);
    auto* sx = d->createNewChildElement ("SHOGUN");
    sx->setAttribute ("version", 2);
    sx->setAttribute ("running", 0);
    sx->addTextElement (json);
    JidaiProcessor p;
    p.prepareToPlay (48000.0, 512);
    p.testRestore (rack);
    auto* s = firstShogun (p);
    check (s != nullptr, "a SHOGUN plugin patch loads into a rack SHOGUN");
    if (s == nullptr)
        return;
    check (std::abs (s->param (shogun::P_CLOCK_TEMPO) - 0.62) < 1e-6 && juce::String (s->edits().pattern.name) == "PLUGIN"
               && s->edits().pattern.tracks[shogun::BD1].steps[0].acc == 3, "plugin patch: parameters and pattern");
    bool lfo = false, alias = false;
    for (const auto& c : p.rack().cables())
    {
        lfo = lfo || (c.a == "SHOGUN#1/MOD:LFO 1" && c.b == "SHOGUN#1/SD:TONE");
        alias = alias || (c.a == "SHOGUN#1/BD1:ENV" && c.b == "SHOGUN#1/BASS:NOTE");
    }
    check (lfo, "plugin patch: SHOGUN's bay cable LFO 1 -> SD TONE is a rack cable");
    check (alias && s->edits().inLaw[(size_t) shogun::synthPort (1, shogun::SJ_NOTE)] == 1,
           "plugin patch: the old BASS:HZ/V id lands on BASS:NOTE with the Lin55 law (shogun::resolvePort)");
}

struct PlayHead : juce::AudioPlayHead
{
    double bpm = 120.0, sampleRate = 48000.0;
    long long sample = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (true);
        info.setBpm (bpm);
        info.setTimeInSamples (sample);
        info.setPpqPosition ((double) sample / sampleRate * bpm / 60.0);
        return info;
    }
};

void testFactoryKits()
{
    check (shogunstate::programCount() == 22 && shogunstate::programName (0) == "INIT", "SHOGUN's factory list: INIT + 21 kits");
    for (int prog = 0; prog < shogunstate::programCount(); ++prog)
    {
        const juce::String tag = "kit " + juce::String (prog) + " " + juce::String (shogunstate::programName (prog)) + ": ";
        JidaiProcessor a;
        PlayHead head;
        a.setPlayHead (&head);
        a.prepareToPlay (48000.0, 512);
        auto* s = static_cast<ShogunDevice*> (a.addDevice (DeviceKind::Shogun, -1, true, false));
        if (s == nullptr || ! shogunstate::loadProgram (*s, prog))
        {
            check (false, tag + "loads");
            continue;
        }
        check (s->program() == prog, tag + "is the device's program");
        check (std::abs (s->param (shogun::P_CLOCK_SOURCE) - shogun::stepU (0, 3)) < 1.0e-6, tag + "keeps the host clock");
        char want[8];
        std::snprintf (want, sizeof want, "%03d", prog + 1);
        check (juce::String (s->edits().pattern.name).startsWith (want), tag + "loads its own pattern " + juce::String (want));

        // 4 s at 120 BPM from the host transport, SHOGUN's MIX on MAIN OUT (auto-routed).
        juce::AudioBuffer<float> buffer (2, 512);
        double peak = 0.0;
        bool finite = true;
        for (int blk = 0; blk < (int) (4.0 * 48000.0 / 512.0); ++blk)
        {
            buffer.clear();
            juce::MidiBuffer midi;
            a.processBlock (buffer, midi);
            head.sample += 512;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                {
                    finite = finite && std::isfinite (buffer.getSample (c, i));
                    peak = std::max (peak, (double) std::abs (buffer.getSample (c, i)));
                }
        }
        a.setPlayHead (nullptr);
        const double db = 20.0 * std::log10 (std::max (peak, 1.0e-12));
        std::printf ("  shogun kit %2d %-26s peak %7.2f dBFS\n", prog, shogunstate::programName (prog).c_str(), db);
        check (finite, tag + "plays finite");
        if (prog == 0)
            check (peak < 1.0e-4, tag + "INIT's empty pattern is silent");
        else
            check (db > -16.0 && db < -6.0, tag + "plays at kit level on the host clock, peak " + juce::String (db, 2) + " dBFS");

        // The program number is saved with the device and restored.
        const auto text = stateText (a);
        JidaiProcessor b;
        if (auto xml = juce::parseXML (text))
        {
            juce::MemoryBlock m;
            juce::AudioProcessor::copyXmlToBinary (*xml, m);
            b.setStateInformation (m.getData(), (int) m.getSize());
        }
        auto* t = firstShogun (b);
        check (t != nullptr && t->program() == prog && stateText (b) == text, tag + "saves and restores with its program number");
    }
}

} // namespace

void runShogunStateTests (int& checks, int& failures)
{
    gChecks = &checks;
    gFailures = &failures;
    testRoundTrip();
    testPluginPatch();
    testFactoryKits();
}

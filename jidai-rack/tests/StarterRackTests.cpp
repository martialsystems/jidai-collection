// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
//
// The starter racks (plugin/StarterRacks.h, racks/starter_racks.xml): the compiled list parses with nothing skipped,
// INIT is first and equals the default rack, every rack loads as a host program with every device, parameter and
// cable it names, its state round-trips byte for byte and parameter for parameter, every cable is valid under the
// Jidai Cable Standard (R6 ids, Out -> In, one cable per input, allowed types, no pitch-law mismatch, feedback only
// where a rack patches a loop on purpose), and played on the host transport (128 BPM) with a test signal at
// HOST IN and a few MIDI notes, its output is finite, audible and peaks below 0 dBFS.

#include "plugin/JidaiProcessor.h"
#include "plugin/StarterRacks.h"
#include "origami/plugin/OrigamiState.h"

#include "UI/PatchBayLogic.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>

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

struct PlayHead : juce::AudioPlayHead
{
    double bpm = 128.0, sampleRate = 48000.0;
    long long sample = 0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setBpm (bpm);
        info.setTimeInSamples (sample);
        info.setPpqPosition ((double) sample / sampleRate * bpm / 60.0);
        return info;
    }
};

// Host input: a log sine sweep 40 Hz -> 8 kHz at -6 dBFS with drum-like hits on every beat (128 BPM) at -3 dBFS.
float hostSignal (long long n, int channel)
{
    const double fs = 48000.0, t = (double) n / fs, len = 4.0;
    const double f0 = 40.0, f1 = 8000.0, k = std::log (f1 / f0);
    const double phase = 2.0 * juce::MathConstants<double>::pi * f0 * len / k * (std::exp (k * std::fmod (t, len) / len) - 1.0);
    double v = 0.5 * std::sin (phase) * (channel == 0 ? 1.0 : 0.8);
    const double beat = 60.0 / 128.0, tb = std::fmod (t, beat);
    v += 0.2 * std::exp (-tb / 0.08) * std::sin (2.0 * juce::MathConstants<double>::pi * (55.0 * tb + 2.0 * (1.0 - std::exp (-tb / 0.02))));
    return (float) v;
}

std::map<std::string, std::string> paramsOf (const juce::XmlElement& device)
{
    std::map<std::string, std::string> out;
    for (auto* e : device.getChildIterator())
        if (e->hasTagName ("PARAM") || e->hasTagName ("KNOB"))
            out[e->getStringAttribute ("id").toStdString()] = e->getStringAttribute ("value").toStdString();
    if (auto* o = device.getChildByName ("ORIGAMI"))
        for (auto* e : o->getChildWithTagNameIterator ("PARAM"))
            out["origami:" + e->getStringAttribute ("id").toStdString()] = e->getStringAttribute ("value").toStdString();
    return out;
}

// The device's parameters as the rack holds them, against the stored values (a float knob holds the stored double
// rounded to float; a switch snaps).
void checkParams (JidaiProcessor& p, const juce::XmlElement& device, const juce::String& tag)
{
    Device* d = p.rack().findDevice (std::string (device.getStringAttribute ("kind") == "RACK I/O" ? "RACK" : device.getStringAttribute ("kind").toRawUTF8())
                                     + "#" + std::to_string (device.getIntAttribute ("number")));
    check (d != nullptr, tag + "device " + device.getStringAttribute ("kind") + " " + juce::String (device.getIntAttribute ("number")) + " is in the rack");
    if (d == nullptr)
        return;
    check (juce::String::fromUTF8 (d->name.c_str()) == device.getStringAttribute ("name"), tag + "device name " + device.getStringAttribute ("name"));
    int bad = 0, n = 0;
    juce::String first;
    if (auto* b = dynamic_cast<BushidoDevice*> (d))
        for (auto* e : device.getChildWithTagNameIterator ("PARAM"))
        {
            ++n;
            const auto id = e->getStringAttribute ("id").toStdString();
            const double want = e->getDoubleAttribute ("value");
            const int at = b->paramIndex (id);
            const int positions = at >= 0 ? b->engine().params()[(size_t) at].positions : 0;
            const double snapped = positions >= 2 ? std::round (want * (positions - 1)) / (positions - 1) : want;
            if (at < 0 || std::abs ((double) b->param (id) - snapped) > 1.0e-6) { ++bad; if (first.isEmpty()) first = id; }
        }
    if (auto* r = dynamic_cast<RoninDevice*> (d))
        for (auto* e : device.getChildWithTagNameIterator ("KNOB"))
        {
            ++n;
            const auto id = e->getStringAttribute ("id");
            const int k = panelKnobIndex (id.upToFirstOccurrenceOf (":", false, false).toRawUTF8(), id.fromFirstOccurrenceOf (":", false, false).toRawUTF8());
            if (k < 0 || std::abs ((double) r->knob (k) - e->getDoubleAttribute ("value")) > 1.0e-6) { ++bad; if (first.isEmpty()) first = id; }
        }
    if (auto* o = dynamic_cast<OrigamiDevice*> (d))
        if (auto* oe = device.getChildByName ("ORIGAMI"))
        {
            double values[origami::kParamCount];
            check (origami::stateFromXml (*oe, values).ok, tag + "ORIGAMI state parses");
            for (int i = 0; i < origami::kParamCount; ++i)
            {
                ++n;
                if (std::abs (o->param (i) - values[i]) > 1.0e-9) { ++bad; if (first.isEmpty()) first = origami::paramInfo (i).id; }
            }
        }
    if (auto* io = dynamic_cast<RackIODevice*> (d))
    {
        ++n;
        if (std::abs ((double) io->mainLevel() - device.getDoubleAttribute ("level", 1.0)) > 1.0e-6) ++bad;
    }
    check (bad == 0, tag + device.getStringAttribute ("kind") + ": all " + juce::String (n) + " stored parameters hold their values" + (bad ? " (first off: " + first + ")" : juce::String()));
}

void checkCables (JidaiProcessor& p, const juce::XmlElement& state, const juce::String& tag)
{
    auto& rack = p.rack();
    const auto& cables = rack.cables();
    const auto& info = rack.cableInfo();
    int stored = 0;
    for (auto* e : state.getChildWithTagNameIterator ("CABLE"))
    {
        juce::ignoreUnused (e);
        ++stored;
    }
    check ((int) cables.size() == stored && info.size() == cables.size(), tag + "every stored cable is patched (" + juce::String (stored) + ")");
    std::set<std::string> inputs;
    int feedback = 0, intendedLoops = 0;
    for (size_t i = 0; i < cables.size() && i < info.size(); ++i)
    {
        const auto& c = cables[i];
        const juce::String what = tag + "cable " + juce::String (c.a) + " -> " + juce::String (c.b) + ": ";
        Device* da = nullptr; Device* db = nullptr;
        int ja = -1, jb = -1;
        const bool resolves = rack.resolve (c.a, da, ja) && rack.resolve (c.b, db, jb);
        check (resolves, what + "both jack ids resolve (JCS R6)");
        if (! resolves)
            continue;
        const auto& A = da->jacks()[(size_t) ja];
        const auto& B = db->jacks()[(size_t) jb];
        check (A.desc.dir == PortDir::Out && B.desc.dir == PortDir::In, what + "goes from an output to an input");
        check (inputs.insert (c.b).second, what + "is the only cable into its input");
        check (info[i].live, what + "carries signal");
        check (info[i].badge == jidai::jcs::Badge::None || info[i].badge == jidai::jcs::Badge::GateToStrig,
               what + "no pitch-law or audio-into-clock warning (JCS R4.3, R14)");
        check (c.color == -1 && ! c.autoRouted && ! c.legacyInvert, what + "role colour (JCS R14), not auto-routed, no legacy inversion");
        if (info[i].feedback)
        {
            ++feedback;
            // The only loops a starter rack patches on purpose: BUSHIDO TRIG N -> its own RESET (an N-1 step loop).
            if (da == db && da->kind() == DeviceKind::Bushido && B.id == "INPUTS:RESET")
                ++intendedLoops;
        }
    }
    check (feedback == intendedLoops, tag + "feedback (JCS R9) only on BUSHIDO TRIG -> RESET loops (" + juce::String (feedback) + " delayed)");
    // R13: something reaches MAIN OUT unless the rack is INIT.
    bool toMain = false;
    for (auto& c : cables)
        toMain = toMain || c.b == "RACK#1/MAIN:OUT L";
    check (toMain || rack.deviceCount() == 1, tag + "MAIN OUT L is patched");
}

void testStarterRacks()
{
    juce::StringArray errors;
    auto list = juce::parseXML (starterRacksXmlText());
    check (list != nullptr, "starter racks: the compiled XML parses");
    if (list == nullptr)
        return;
    const auto parsed = parseStarterRacks (*list, &errors);
    const auto& racks = starterRacks();
    check (errors.isEmpty() && parsed.size() == racks.size(), "starter racks: nothing skipped " + errors.joinIntoString ("; "));
    check (racks.size() >= 7 && racks.size() <= 11, "starter racks: 6..10 racks after INIT, got " + juce::String ((int) racks.size()));
    check (racks.front().name == "INIT" && racks.front().category == "INIT", "starter racks: INIT first");
    std::set<juce::String> names;
    std::map<juce::String, int> perCategory;
    for (auto& r : racks)
    {
        check (names.insert (r.name).second, "starter rack names are unique: " + r.name);
        check (! r.name.containsAnyOf ("0123456789") && r.about.isNotEmpty(), "starter rack " + r.name + ": descriptive name and an about text");
        ++perCategory[r.category];
    }
    check (perCategory["ACID"] >= 2 && perCategory["EDM"] >= 3 && perCategory["FX"] >= 2, "starter racks: at least 2 ACID, 3 EDM and 2 FX");

    JidaiProcessor fresh;
    check (fresh.getNumPrograms() == (int) racks.size() && fresh.getProgramName (0) == "INIT"
           && fresh.getProgramName (1) == racks[1].category + ": " + racks[1].name, "host programs are the starter racks (INIT, then CATEGORY: Name)");
    const auto defaultState = stateText (fresh);

    for (int i = 0; i < (int) racks.size(); ++i)
    {
        const auto& sr = racks[(size_t) i];
        const juce::String tag = "starter rack " + juce::String (i) + " \"" + sr.name + "\": ";
        JidaiProcessor p;
        p.setCurrentProgram (i);
        check (p.getCurrentProgram() == i && p.migrationNotice.isEmpty(), tag + "loads as a program with no migration notice");
        check (sr.state->getIntAttribute ("version") == JidaiProcessor::kStateVersion, tag + "is a version 3 rack state");
        int devices = 0;
        for (auto* d : sr.state->getChildWithTagNameIterator ("DEVICE"))
        {
            ++devices;
            checkParams (p, *d, tag);
        }
        check (p.rack().deviceCount() == devices && p.rack().device (0)->kind() == DeviceKind::RackIO, tag + "has its " + juce::String (devices) + " devices, RACK I/O on top");
        checkCables (p, *sr.state, tag);
        if (i == 0)
            check (stateText (p) == defaultState, tag + "INIT equals the default rack");

        // Round trip: save, load into a fresh rack, save again.
        juce::MemoryBlock a, b;
        p.getStateInformation (a);
        JidaiProcessor q;
        q.setStateInformation (a.getData(), (int) a.getSize());
        q.getStateInformation (b);
        bool sameParams = q.rack().deviceCount() == p.rack().deviceCount() && q.rack().cables().size() == p.rack().cables().size();
        for (auto* d : sr.state->getChildWithTagNameIterator ("DEVICE"))
            sameParams = sameParams && p.rack().findDevice (std::string (d->getStringAttribute ("kind") == "RACK I/O" ? "RACK" : d->getStringAttribute ("kind").toRawUTF8()) + "#" + std::to_string (d->getIntAttribute ("number"))) != nullptr;
        auto xa = juce::AudioProcessor::getXmlFromBinary (a.getData(), (int) a.getSize());
        auto xb = juce::AudioProcessor::getXmlFromBinary (b.getData(), (int) b.getSize());
        if (xa != nullptr && xb != nullptr)
        {
            auto da = xa->getChildWithTagNameIterator ("DEVICE").begin();
            for (auto* d : xb->getChildWithTagNameIterator ("DEVICE"))
            {
                sameParams = sameParams && *da != nullptr && paramsOf (**da) == paramsOf (*d);
                ++da;
            }
        }
        check (a == b && sameParams, tag + "state round trip is byte- and parameter-identical");

        // Stored values in the list = what the rack saves (the list is in canonical form).
        bool canonical = xa != nullptr;
        if (xa != nullptr)
        {
            auto ds = sr.state->getChildWithTagNameIterator ("DEVICE").begin();
            for (auto* d : xa->getChildWithTagNameIterator ("DEVICE"))
            {
                if (*ds == nullptr) { canonical = false; break; }
                const auto want = paramsOf (**ds), got = paramsOf (*d);
                for (auto& [id, v] : want)
                    canonical = canonical && got.count (id) && std::abs (juce::String (got.at (id)).getDoubleValue() - juce::String (v).getDoubleValue()) < 1.0e-6;
                ++ds;
            }
        }
        check (canonical, tag + "the rack saves the values the list stores");

        // Play: 128 BPM host transport, 6 s, host audio in, MIDI notes for RACK I/O.
        PlayHead head;
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 512);
        const int blocks = (int) (6.0 * 48000.0 / 512.0);
        double peak = 0.0, sum = 0.0;
        long long count = 0;
        bool finite = true;
        juce::AudioBuffer<float> buffer (2, 512);
        const int notes[] = { 48, 55, 60, 63 };
        for (int blk = 0; blk < blocks; ++blk)
        {
            juce::MidiBuffer midi;
            const int beatSamples = (int) (48000.0 * 60.0 / 128.0);
            for (int s = 0; s < 512; ++s)
            {
                const long long n = head.sample + s;
                for (int c = 0; c < 2; ++c)
                    buffer.setSample (c, s, hostSignal (n, c));
                if (n % beatSamples == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, notes[(n / beatSamples) % 4], (juce::uint8) 100), s);
                if (n % beatSamples == beatSamples / 2)
                    midi.addEvent (juce::MidiMessage::noteOff (1, notes[(n / beatSamples) % 4]), s);
            }
            p.processBlock (buffer, midi);
            head.sample += 512;
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < 512; ++s)
                {
                    const double v = buffer.getSample (c, s);
                    finite = finite && std::isfinite (v);
                    peak = std::max (peak, std::abs (v));
                    sum += v * v;
                    ++count;
                }
        }
        p.setPlayHead (nullptr);
        const double peakDb = 20.0 * std::log10 (std::max (peak, 1e-12));
        const double rmsDb = 10.0 * std::log10 (std::max (sum / (double) count, 1e-24));
        std::printf ("  starter %-2d %-16s %-5s devices %d cables %2d lat %3d  peak %7.2f dBFS  rms %7.2f dBFS\n", i, sr.name.toRawUTF8(),
                     sr.category.toRawUTF8(), devices, (int) p.rack().cables().size(), p.getLatencySamples(), peakDb, rmsDb);
        check (p.getLatencySamples() == p.rack().latency(), tag + "reports the rack's path latency to the host (JCS R11)");
        if (i == 0)
            check (finite && peak == 0.0, tag + "INIT is silent (nothing patched to MAIN OUT)");
        else
            check (finite && rmsDb > -40.0 && peakDb < 0.0, tag + "plays: finite, audible (rms " + juce::String (rmsDb, 1) + " dBFS) and below 0 dBFS (peak "
                   + juce::String (peakDb, 2) + " dBFS)");
    }
}

}

void runStarterRackTests (int& checks, int& failures)
{
    gChecks = &checks;
    gFailures = &failures;
    testStarterRacks();
}

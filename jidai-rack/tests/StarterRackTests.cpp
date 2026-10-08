// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
//
// The starter racks (plugin/StarterRacks.h, racks/starter_racks.xml): the compiled list parses with nothing skipped,
// INIT is first and equals the default rack, every rack loads as a host program with every device, parameter and
// cable it names, its state round-trips byte for byte and parameter for parameter, every cable is valid under the
// Jidai Cable Standard (R6 ids, Out -> In, one cable per input, allowed types, no pitch-law mismatch, feedback only
// where a rack patches a loop on purpose), and played on the host transport (128 BPM) with a test signal at
// HOST IN and a few MIDI notes, its output is finite, audible and peaks below 0 dBFS. The EDM Starter (the first JIDAI
// Patch Cookbook recipe) is also rendered on its own: level, stems, kick ducking and sequencer lock (testEdmStarter).

#include "plugin/JidaiProcessor.h"
#include "plugin/StarterRacks.h"
#include "plugin/ShogunState.h"
#include "origami/plugin/OrigamiState.h"
#include "origami/plugin/OrigamiPresets.h"

#include "UI/PatchBayLogic.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>
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
    if (auto* s = device.getChildByName ("SHOGUN"))
    {
        const juce::var patch = juce::JSON::parse (s->getAllSubText());   // keep the parsed tree alive while iterating
        const juce::var params = patch["params"];
        if (auto* obj = params.getDynamicObject())
            for (const auto& kv : obj->getProperties())
                out["shogun:" + kv.name.toString().toStdString()] = kv.value.toString().toStdString();
    }
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
            const int program = oe->getIntAttribute ("program", -1);
            check (program >= 0 && o->program() == program, tag + "ORIGAMI shows its factory preset " + juce::String (program)
                                                                 + " (" + (program >= 0 && program < (int) origami::factoryPresets().size()
                                                                               ? origami::factoryPresets()[(size_t) program].displayName() : juce::String ("?")) + ")");
            for (int i = 0; i < origami::kParamCount; ++i)
            {
                ++n;
                if (std::abs (o->param (i) - values[i]) > 1.0e-9) { ++bad; if (first.isEmpty()) first = origami::paramInfo (i).id; }
            }
        }
    if (auto* sg = dynamic_cast<ShogunDevice*> (d))
        if (auto* se = device.getChildByName ("SHOGUN"))
        {
            const auto patch = juce::JSON::parse (se->getAllSubText());
            if (auto* params = patch["params"].getDynamicObject())
                for (const auto& kv : params->getProperties())
                {
                    ++n;
                    const int at = sg->paramIndex (kv.name.toString().toStdString());
                    if (at < 0 || std::abs (sg->param (at) - (double) kv.value) > 1.0e-6) { ++bad; if (first.isEmpty()) first = kv.name.toString(); }
                }
            const auto e = sg->edits();
            if (auto* tracks = patch["seq"]["tracks"].getArray())
                for (const auto& t : *tracks)
                {
                    int v = -1;
                    for (int k = 0; k < shogun::kVoices; ++k)
                        if (t["id"].toString() == shogun::kVoiceNames[k]) v = k;
                    ++n;
                    if (v < 0 || e.pattern.tracks[v].len != (int) t["len"]) { ++bad; if (first.isEmpty()) first = t["id"].toString() + " LEN"; continue; }
                    int on = 0;
                    for (const auto& st : e.pattern.tracks[v].steps) on += st.on ? 1 : 0;
                    if (auto* steps = t["steps"].getArray())
                    {
                        if (on != steps->size()) { ++bad; if (first.isEmpty()) first = t["id"].toString() + " steps"; }
                        for (const auto& st : *steps)
                        {
                            ++n;
                            const auto& got = e.pattern.tracks[v].steps[(int) st["i"]];
                            const bool same = got.on && got.acc == (int) st["acc"] && (! st.hasProperty ("note") || got.note == (int) st["note"]);
                            if (! same) { ++bad; if (first.isEmpty()) first = t["id"].toString() + " step " + st["i"].toString(); }
                        }
                    }
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
        // Stacked cables into one input are summed (the acid racks stack EG 2 and the accent on VCF CUTOFF, as
        // RONIN's own ACID programs do); the same cable twice is a mistake.
        check (inputs.insert (c.a + " > " + c.b).second, what + "is not patched twice");
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
    check (racks.size() >= 7 && racks.size() <= 13, "starter racks: 6..12 racks after INIT, got " + juce::String ((int) racks.size()));
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
                {
                    const bool same = got.count (id) && std::abs (juce::String (got.at (id)).getDoubleValue() - juce::String (v).getDoubleValue()) < 1.0e-6;
                    if (! same)
                        std::printf ("  %s %s: list %s, saved %s\n", tag.toRawUTF8(), id.c_str(), v.c_str(),
                                     got.count (id) ? got.at (id).c_str() : "(missing)");
                    canonical = canonical && same;
                }
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


// EDM Starter (the first JIDAI Patch Cookbook recipe), rendered 8 bars at 125 BPM on the host transport. The mix is
// finite and peaks between -6.5 and -2.5 dBFS; drums alone and bass alone are both audible (the kick in the low band);
// the kick ducks the bass (the bass with and without the BD1 ENV -> MIX IN 1 cable, 0-60 ms after each kick, and
// open again before the next one); and BUSHIDO's step stays locked to SHOGUN's bar (step = bar position mod 12)
// before and after transport jumps: a jump to a bar line locks at once, a jump into the middle of a bar locks again
// at the next downbeat (SHOGUN's RST OUT restarts BUSHIDO there).
struct SongHead : juce::AudioPlayHead
{
    double bpm = 125.0, sampleRate = 48000.0;
    long long song = 0;          // song position in samples (jumps move it)
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (true);
        info.setBpm (bpm);
        info.setTimeInSamples (song);
        info.setPpqPosition ((double) song / sampleRate * bpm / 60.0);
        return info;
    }
};

struct EdmRender
{
    std::vector<float> l, r;
    int latency = 0;
    int lockChecks = 0, lockMisses = 0, relockSteps = -1;
    double peak = 0.0;
    bool finite = true;
};

int edmStarterProgram()
{
    const auto& racks = starterRacks();
    for (int i = 0; i < (int) racks.size(); ++i)
        if (racks[(size_t) i].name == "EDM Starter")
            return i;
    return -1;
}

// Renders the EDM Starter with some cables removed. jumps: { block, new song position in samples }.
EdmRender renderEdmStarter (const std::vector<std::pair<std::string, std::string>>& remove, int bars,
                            const std::vector<std::pair<int, long long>>& jumps = {})
{
    EdmRender out;
    JidaiProcessor p;
    p.setCurrentProgram (edmStarterProgram());
    for (auto& [a, b] : remove)
        check (p.rack().disconnect (a, b), "EDM Starter: remove " + juce::String (a) + " -> " + juce::String (b));
    SongHead head;
    p.setPlayHead (&head);
    const int block = 512;
    p.prepareToPlay (head.sampleRate, block);
    out.latency = p.getLatencySamples();
    const long long total = (long long) std::llround (bars * 4 * 60.0 / head.bpm * head.sampleRate);
    const int blocks = (int) ((total + block - 1) / block);
    auto* bushido = dynamic_cast<BushidoDevice*> (p.rack().findDevice ("BUSHIDO#1"));
    auto* shogun = dynamic_cast<ShogunDevice*> (p.rack().findDevice ("SHOGUN#1"));
    juce::AudioBuffer<float> buffer (2, block);
    bool locked = true;          // false from a jump into the middle of a bar until BUSHIDO matches again
    long long unlockedAt = -1;
    int litPrev = -1;
    for (int blk = 0; blk < blocks; ++blk)
    {
        for (auto& [at, to] : jumps)
            if (at == blk)
            {
                const long long stepLen = (long long) std::llround (60.0 / head.bpm / 4.0 * head.sampleRate);
                head.song = to;
                if ((to / stepLen) % 16 != 0)
                {
                    locked = false;
                    unlockedAt = to / stepLen;
                }
            }
        buffer.clear();
        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);
        head.song += block;
        for (int s = 0; s < block; ++s)
        {
            const float a = buffer.getSample (0, s), b = buffer.getSample (1, s);
            out.finite = out.finite && std::isfinite (a) && std::isfinite (b);
            out.peak = std::max (out.peak, (double) std::max (std::abs (a), std::abs (b)));
            out.l.push_back (a);
            out.r.push_back (b);
        }
        // Lock: BUSHIDO's lit step against SHOGUN's bar position at the same moment. SHOGUN publishes its step at the
        // start of each block (the end of the one before), so its value now is compared with BUSHIDO's lamp read
        // after the previous block.
        if (bushido != nullptr && shogun != nullptr)
        {
            const long g = shogun->globalStep();
            if (blk > 1 && litPrev >= 0)
            {
                const int want = (int) (((g % 16) + 16) % 16) % 12;
                if (! locked && litPrev == want && (g % 16) == 0)
                {
                    locked = true;
                    out.relockSteps = (int) (g - unlockedAt);
                }
                if (locked)
                {
                    ++out.lockChecks;
                    out.lockMisses += litPrev == want ? 0 : 1;
                }
            }
            litPrev = -1;
            for (int k = 1; k <= 12; ++k)
                if (bushido->indicator ("STEP:" + std::to_string (k)) > 0.5f)
                    litPrev = k - 1;
        }
    }
    p.setPlayHead (nullptr);
    return out;
}

double rmsDb (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    to = std::min (to, x.size());
    for (size_t i = from; i < to; ++i)
        s += (double) x[i] * (double) x[i];
    return 10.0 * std::log10 (std::max (s / (double) std::max<size_t> (1, to - from), 1e-24));
}

// The cookbook recipe (docs/cookbook/JIDAI_Patch_Cookbook.md, "EDM Starter") patched by hand: from INIT, add SHOGUN,
// BUSHIDO, RONIN and ORIGAMI with no automatic cables (Shift), remove RONIN's INIT cables except the two the recipe
// keeps, then make the recipe's cables in its order. Every cable must be accepted, and the result must be exactly the
// EDM Starter preset's cables.
const std::vector<std::pair<std::string, std::string>> kEdmRecipeKeep {
    { "RONIN#1/EG 1:OUT A", "RONIN#1/VCA 1:ENV" },
    { "RONIN#1/VCA 1:OUT", "RONIN#1/OUTPUT:WET" },
};
const std::vector<std::pair<std::string, std::string>> kEdmRecipeRemove {     // the cookbook's "unplug these six"
    { "RONIN#1/EXT IN:MONO", "RONIN#1/VCF:IN" },
    { "RONIN#1/VCF:OUT", "RONIN#1/VCA 1:IN" },
    { "RONIN#1/EXT IN:L", "RONIN#1/OUTPUT:L" },
    { "RONIN#1/EXT IN:R", "RONIN#1/OUTPUT:R" },
    { "RONIN#1/EXT IN:GATE", "RONIN#1/EG 1:TRIG" },
    { "RONIN#1/EG 1:OUT A", "RONIN#1/VCF:CUTOFF" },
};
const std::vector<std::pair<std::string, std::string>> kEdmRecipe {
    { "SHOGUN#1/CLOCK:RST OUT", "BUSHIDO#1/INPUTS:RESET" },     //  1 lock
    { "BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/INT:IN" },             //  2 notes
    { "RONIN#1/INT:OUT", "RONIN#1/VCO:V/OCT" },                 //  3 slide
    { "RONIN#1/VCO:SAW", "RONIN#1/VCF:IN" },                    //  4
    { "BUSHIDO#1/OUTPUTS:GATE A", "RONIN#1/EG 1:TRIG" },        //  5
    { "BUSHIDO#1/OUTPUTS:GATE A", "RONIN#1/EG 2:TRIG" },        //  6
    { "RONIN#1/EG 2:OUT +", "RONIN#1/VCF:CUTOFF" },             //  7
    { "BUSHIDO#1/OUTPUTS:CV C", "RONIN#1/VCF:CUTOFF" },         //  8 accent
    { "RONIN#1/VCF:OUT", "ORIGAMI#1/IN:IN L" },                 //  9 fold
    { "ORIGAMI#1/OUT:OUT L", "RONIN#1/VCA 1:IN" },              // 10
    { "SHOGUN#1/BD1:ENV", "RONIN#1/MIX:IN 1" },                 // 11 duck
    { "RONIN#1/MIX:OUT", "RONIN#1/VCA 1:ENV" },                 // 12
    { "RONIN#1/HOST:OUT L", "RACK#1/MAIN:OUT L" },              // 13 out
    { "RONIN#1/HOST:OUT R", "RACK#1/MAIN:OUT R" },              // 14
    { "SHOGUN#1/MIX:L", "RACK#1/MAIN:OUT L" },                  // 15
    { "SHOGUN#1/MIX:R", "RACK#1/MAIN:OUT R" },                  // 16
};

void testEdmRecipeByHand()
{
    JidaiProcessor preset;
    preset.setCurrentProgram (edmStarterProgram());
    std::set<std::string> want;
    for (auto& c : preset.rack().cables())
        want.insert (c.a + " > " + c.b);

    JidaiProcessor p;          // INIT
    auto& rack = p.rack();
    const DeviceKind order[] { DeviceKind::Shogun, DeviceKind::Bushido, DeviceKind::Ronin, DeviceKind::Origami };
    for (auto k : order)
        check (rack.insertNew (k, -1, false) != nullptr, "recipe: add a device with Shift (no automatic cables)");
    bool sameOrder = rack.deviceCount() == preset.rack().deviceCount();
    for (int i = 0; sameOrder && i < rack.deviceCount(); ++i)
        sameOrder = rack.device (i)->rackId() == preset.rack().device (i)->rackId();
    check (sameOrder, "recipe: the devices sit in the preset's order (RACK I/O, SHOGUN, BUSHIDO, RONIN, ORIGAMI)");
    int removed = 0;
    std::set<std::string> unplugged, listed;
    const auto initial = rack.cables();
    for (auto& c : initial)
    {
        bool keep = false;
        for (auto& k : kEdmRecipeKeep)
            keep = keep || (c.a == k.first && c.b == k.second);
        if (! keep && rack.disconnect (c.a, c.b))
        {
            ++removed;
            unplugged.insert (c.a + " > " + c.b);
        }
    }
    for (auto& [a, b] : kEdmRecipeRemove)
        listed.insert (a + " > " + b);
    check (removed == 6 && rack.cables().size() == kEdmRecipeKeep.size(),
           "recipe: RONIN arrives with its 8 INIT cables; removing 6 leaves the 2 the recipe keeps (removed " + juce::String (removed) + ")");
    check (unplugged == listed, "recipe: the six cables the cookbook says to unplug are exactly RONIN's other INIT cables");
    for (auto& [a, b] : kEdmRecipe)
        check (rack.connect (a, b) == Rack::Check::Ok, "recipe: cable " + juce::String (a) + " -> " + juce::String (b) + " is accepted");
    std::set<std::string> got;
    for (auto& c : rack.cables())
        got.insert (c.a + " > " + c.b);
    check (got == want && got.size() == kEdmRecipe.size() + kEdmRecipeKeep.size(),
           "recipe: the hand-patched rack has exactly the EDM Starter preset's " + juce::String ((int) want.size()) + " cables");
    for (auto& c : rack.cables())
        check (c.color == -1 && ! c.autoRouted, "recipe: " + juce::String (c.a) + " -> " + juce::String (c.b) + " takes its role colour");
}

void testEdmStarter()
{
    const int program = edmStarterProgram();
    check (program > 0 && starterRacks()[(size_t) program].category == "EDM", "EDM Starter is a starter rack in EDM");
    if (program <= 0)
        return;
    const double fs = 48000.0, bpm = 125.0;
    const int beat = (int) std::llround (60.0 / bpm * fs);              // 23040 samples
    const std::pair<std::string, std::string> drumsL { "SHOGUN#1/MIX:L", "RACK#1/MAIN:OUT L" }, drumsR { "SHOGUN#1/MIX:R", "RACK#1/MAIN:OUT R" };
    const std::pair<std::string, std::string> bassL { "RONIN#1/HOST:OUT L", "RACK#1/MAIN:OUT L" }, bassR { "RONIN#1/HOST:OUT R", "RACK#1/MAIN:OUT R" };
    const std::pair<std::string, std::string> duck { "SHOGUN#1/BD1:ENV", "RONIN#1/MIX:IN 1" };

    // Full mix, 8 bars, with two transport jumps: at bar 5 (mid-bar) back to bar 2's downbeat, and at bar 7 into
    // the middle of bar 3 (step 6). Lock is checked on every block.
    const long long bar = 4LL * beat, step = beat / 4;
    const auto mix = renderEdmStarter ({}, 8, { { (int) (4 * bar + 6 * step) / 512, 1 * bar }, { (int) (6 * bar) / 512, 2 * bar + 5 * step } });
    const double peakDb = 20.0 * std::log10 (std::max (mix.peak, 1e-12));
    check (mix.finite && peakDb > -6.5 && peakDb < -2.5, "EDM Starter: 8 bars finite, peak " + juce::String (peakDb, 2) + " dBFS (want -6.5..-2.5)");
    check (mix.lockChecks > 1000 && mix.lockMisses == 0, "EDM Starter: BUSHIDO's step = SHOGUN's bar position mod 12 on every block while locked ("
           + juce::String (mix.lockChecks) + " blocks, " + juce::String (mix.lockMisses) + " off)");
    check (mix.relockSteps > 0 && mix.relockSteps <= 16, "EDM Starter: a jump into the middle of a bar locks again at the next downbeat (after "
           + juce::String (mix.relockSteps) + " steps)");

    // Plain 8 bars (no jumps) for the stems.
    const auto drums = renderEdmStarter ({ bassL, bassR }, 8);
    const auto bass = renderEdmStarter ({ drumsL, drumsR }, 8);
    const auto bassOpen = renderEdmStarter ({ drumsL, drumsR, duck }, 8);
    // Kick in the low band (two one-pole low-passes at 120 Hz) in the 80 ms after each beat.
    std::vector<float> low (drums.l.size());
    {
        const double a = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 120.0 / fs);
        double y1 = 0.0, y2 = 0.0;
        for (size_t i = 0; i < drums.l.size(); ++i) { y1 += a * (drums.l[i] - y1); y2 += a * (y1 - y2); low[i] = (float) y2; }
    }
    double kick = 0.0, duckDb = 0.0, openDb = 0.0;
    int kicks = 0;
    for (long long b = 1; (b + 1) * beat < (long long) bass.l.size(); ++b)
    {
        const size_t kat = (size_t) (b * beat + drums.latency), at = (size_t) (b * beat + bass.latency);
        kick += std::pow (10.0, rmsDb (low, kat, kat + (size_t) (0.08 * fs)) / 10.0);
        duckDb += rmsDb (bassOpen.l, at, at + (size_t) (0.06 * fs)) - rmsDb (bass.l, at, at + (size_t) (0.06 * fs));
        const size_t late = at + (size_t) (0.36 * fs);
        openDb += rmsDb (bassOpen.l, late, late + (size_t) (0.06 * fs)) - rmsDb (bass.l, late, late + (size_t) (0.06 * fs));
        ++kicks;
    }
    const double kickDb = 10.0 * std::log10 (std::max (kick / std::max (1, kicks), 1e-24));
    duckDb /= std::max (1, kicks);
    openDb /= std::max (1, kicks);
    const double drumsDb = rmsDb (drums.l, 0, drums.l.size()), bassDb = rmsDb (bass.l, 0, bass.l.size());
    std::printf ("  EDM Starter: peak %.2f dBFS, drums rms %.2f dBFS (kick low band %.2f dBFS), bass rms %.2f dBFS, duck %.2f dB (0-60 ms),"
                 " %.2f dB at 360-420 ms, lock %d blocks / %d off, relock after %d steps, latency %d\n",
                 peakDb, drumsDb, kickDb, bassDb, duckDb, openDb, mix.lockChecks, mix.lockMisses, mix.relockSteps, mix.latency);
    check (drums.finite && drumsDb > -30.0 && kickDb > -30.0, "EDM Starter: drums audible (rms " + juce::String (drumsDb, 1) + " dBFS, kick low band "
           + juce::String (kickDb, 1) + " dBFS)");
    check (bass.finite && bassDb > -30.0, "EDM Starter: bass audible (rms " + juce::String (bassDb, 1) + " dBFS)");
    check (kicks >= 28 && duckDb > 6.0, "EDM Starter: the kick ducks the bass by " + juce::String (duckDb, 1) + " dB in the 60 ms after each kick (want > 6)");
    check (std::abs (openDb) < 2.0, "EDM Starter: the bass is open again before the next kick (" + juce::String (openDb, 2) + " dB at 360-420 ms)");
}

}

void runStarterRackTests (int& checks, int& failures)
{
    gChecks = &checks;
    gFailures = &failures;
    testStarterRacks();
    testEdmStarter();
    testEdmRecipeByHand();
}

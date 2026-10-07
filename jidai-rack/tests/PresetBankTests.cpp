// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
//
// Preset banks through JidaiProcessor. Bank A is each instrument's factory set.
// Bank B starts with the compiled rack patches. User files stay in a temp directory.

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

bool anySq10 (const jidai::Rack& rack)
{
    for (auto& c : rack.cables())
        if (c.a.rfind ("SQ-10", 0) == 0 || c.b.rfind ("SQ-10", 0) == 0)
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

const char* kRoninA[] = {
    "DRY", "NOISE MIXER", "VOICE", "RING", "S&H", "FEEDBACK", "HOLD",
    "FILTER LOOP", "MG FILTER", "STEP CUTOFF", "RING DRONE", "DELAY BOUNCE", "SELF RING"
};

void expectNames (JidaiProcessor& p)
{
    const auto ba = p.patternNames (0);
    check (ba.size() == 28, "Bushido bank A has 28 factory patterns, got " + std::to_string (ba.size()));
    check (ba[0] == "LOOP 8", "Bushido bank A opens on LOOP 8");
    check (ba[ba.size() - 1] == "DUB TECHNO", "Bushido bank A ends on DUB TECHNO");
    const auto bb = p.patternNames (1);
    check (bb.size() == 2, "Bushido bank B starts with the two rack patches, got " + std::to_string (bb.size()));
    check (bb[0] == "LOOP BASS" && bb[1] == "RING SEED", "Bushido bank B is LOOP BASS then RING SEED");
    check (p.patternNames (2).isEmpty(), "Bushido bank past B is empty");

    const auto ra = p.roninPresetNames (0);
    check (ra.size() == 13, "Ronin bank A has 13 factory presets, got " + std::to_string (ra.size()));
    bool names = ra.size() == 13;
    for (int i = 0; names && i < 13; ++i)
        names = ra[i] == kRoninA[i];
    check (names, "Ronin bank A uses the screen names");
    check (ra[2] == "VOICE" && ra[7] == "FILTER LOOP" && ra[12] == "SELF RING", "Ronin bank A marks Voice, Filter loop, Self ring");
    const auto rb = p.roninPresetNames (1);
    check (rb.size() == 2 && rb[0] == "LOOP BASS" && rb[1] == "RING SEED", "Ronin bank B is LOOP BASS then RING SEED");
    check (p.roninPresetNames (3).isEmpty(), "Ronin bank past B is empty");
}

void expectDefault (JidaiProcessor& p)
{
    auto* r = roninAt (p);
    auto* b = bushidoAt (p);
    check (r != nullptr && b != nullptr, "default rack has one Bushido and one Ronin");
    if (r == nullptr || b == nullptr)
        return;
    check (r->program() == kDefaultFactoryPreset, "default Ronin is Voice");
    check (countInternal (p.rack(), "MS-50#1/") == 8, "default Ronin has Voice's 8 cables");
    check (r->effectOn(), "default Ronin effect is on");
    check (p.loadedPattern (r) == std::pair<int, int> (0, 2), "default Ronin screen is A003 VOICE");
    check (p.loadedPattern (b).first == 0 && p.loadedPattern (b).second == 0, "default Bushido screen is A001");
}

void expectLoopBass (JidaiProcessor& p)
{
    auto* r = roninAt (p);
    auto* b = bushidoAt (p);
    check (r != nullptr && b != nullptr, "LOOP BASS still has both devices");
    if (r == nullptr || b == nullptr)
        return;
    check (r->program() == 7, "LOOP BASS loads factory preset 7, Filter loop");
    check (r->effectOn(), "LOOP BASS leaves the effect on");
    check (near (r->knob (panelKnobIndex ("VCF", "MOD")), 0.68f), "LOOP BASS sets VCF MOD to 0.68");
    check (near (r->knob (panelKnobIndex ("VCO", "RANGE")), 0.0f), "LOOP BASS sets VCO RANGE to 0");
    check (near (r->knob (panelKnobIndex ("VCF", "CUTOFF")), 0.4f), "LOOP BASS sets VCF CUTOFF to 0.4");
    check (near (r->knob (panelKnobIndex ("VCF", "PEAK")), 0.7f), "LOOP BASS sets VCF PEAK to 0.7");
    check (near (r->knob (panelKnobIndex ("MIX", "LEVEL 1")), 0.8f), "LOOP BASS leaves an unnamed Ronin knob on the program table");
    check (near (b->param ("CLOCK:TEMPO"), 0.6f), "LOOP BASS sets CLOCK:TEMPO to 0.6");
    check (near (b->param ("MIXER:LEVEL 1"), 0.7f), "LOOP BASS resets MIXER:LEVEL 1 to the layout default 0.7");
    check (b->bypassed(), "LOOP BASS leaves Bushido bypass as it was");
    check ((int) p.rack().cables().size() == 9, "LOOP BASS lays 9 cables, got " + std::to_string (p.rack().cables().size()));
    check (cableColor (p.rack(), "SQ-10#1/OUTPUTS:CV A", "MS-50#1/VCO:HZ/V") == 2, "LOOP BASS CV A cable is yellow");
    check (cableColor (p.rack(), "MS-50#1/VCA 2:OUT", "MS-50#1/OUTPUT:WET") == 3, "LOOP BASS wet cable is green");
    check (hasCable (p.rack(), "MS-50#1/VCF:OUT", "MS-50#1/VCF:CUTOFF"), "LOOP BASS patches the filter loop");
    check (p.loadedPattern (b) == std::pair<int, int> (1, 0), "LOOP BASS shows on the Bushido screen");
    check (p.loadedPattern (r) == std::pair<int, int> (1, 0), "LOOP BASS shows on the Ronin screen");
    check (p.rack().delayedCableCount() == 1, "LOOP BASS delays the one feedback cable");

    p.prepareToPlay (48000.0, 64);
    check (p.getLatencySamples() == 0, "LOOP BASS process reports latency 0");
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
    check (finite, "LOOP BASS process stays finite");
}

void testLoopBassThenFactory()
{
    auto proc = std::make_unique<JidaiProcessor>();
    expectNames (*proc);
    expectDefault (*proc);
    auto* b = bushidoAt (*proc);
    auto* r = roninAt (*proc);
    check (b != nullptr && r != nullptr, "LOOP BASS setup");
    if (b == nullptr || r == nullptr)
        return;
    b->setBypassed (true);
    b->setParam ("MIXER:LEVEL 1", 1.0f);
    proc->loadPattern (b, 1, 0);
    expectLoopBass (*proc);

    proc->loadRoninPreset (r, 0, 2);
    check (countInternal (proc->rack(), "MS-50#1/") == 8, "Voice replaces the Ronin cables and leaves the cross cables");
    check (hasCable (proc->rack(), "SQ-10#1/OUTPUTS:CV A", "MS-50#1/VCO:HZ/V"), "Voice keeps the CV A cross cable");
    check (proc->loadedPattern (r) == std::pair<int, int> (0, 2), "Voice shows on bank A");
    check (proc->loadedPattern (b) == std::pair<int, int> (1, 0), "Voice leaves the Bushido screen on LOOP BASS");
    check (near (b->param ("CLOCK:TEMPO"), 0.6f), "Voice leaves CLOCK:TEMPO");

    const float tempo = b->param ("CLOCK:TEMPO");
    proc->loadRoninPreset (r, 0, 4);
    check (proc->loadedPattern (b) == std::pair<int, int> (1, 0), "S&H leaves the Bushido screen");
    check (near (b->param ("CLOCK:TEMPO"), tempo), "S&H leaves CLOCK:TEMPO");
    check (proc->loadedPattern (r) == std::pair<int, int> (0, 4), "S&H shows on bank A");
    check (hasCable (proc->rack(), "SQ-10#1/OUTPUTS:CV A", "MS-50#1/VCO:HZ/V"), "S&H keeps the cross cable");

    const int program = r->program();
    proc->loadPattern (b, 0, 0);
    check (proc->loadedPattern (r) == std::pair<int, int> (0, 4), "a Bushido pattern leaves the Ronin screen");
    check (r->program() == program, "a Bushido pattern leaves the Ronin program");
    check (hasCable (proc->rack(), "SQ-10#1/OUTPUTS:CV A", "MS-50#1/VCO:HZ/V"), "a Bushido pattern leaves the cross cable");

    const int before = r->program();
    proc->loadRoninPreset (r, 0, 99);
    check (r->program() == before, "an index past the bank does not load");
    check (proc->saveRoninPreset (nullptr, 0, "NO") == -1, "save with no Ronin returns -1");
    check (proc->savePattern (nullptr, 1, "NO") == -1, "save with no Bushido returns -1");
    check (proc->saveRoninPreset (r, 4, "NO") == -1, "save on a bank that does not exist returns -1");
}

void testRingSeed()
{
    auto proc = std::make_unique<JidaiProcessor>();
    auto* r = roninAt (*proc);
    proc->loadRoninPreset (r, 1, 1);
    check (r->program() == 12, "RING SEED loads factory preset 12, Self ring");
    check (near (r->knob (panelKnobIndex ("VCO", "RANGE")), 0.5f), "RING SEED sets VCO RANGE to 0.5");
    check ((int) proc->rack().cables().size() == 10, "RING SEED lays 10 cables, got " + std::to_string (proc->rack().cables().size()));
    check (hasCable (proc->rack(), "MS-50#1/RING:OUT", "MS-50#1/RING:B"), "RING SEED patches the ring to itself");
    check (hasCable (proc->rack(), "SQ-10#1/OUTPUTS:CV A", "MS-50#1/VCO:HZ/V"), "RING SEED patches CV A");
    check (proc->loadedPattern (r) == std::pair<int, int> (1, 1), "RING SEED shows on the Ronin screen");
    check (proc->loadedPattern (bushidoAt (*proc)) == std::pair<int, int> (1, 1), "RING SEED shows on the Bushido screen");
    check (proc->rack().delayedCableCount() == 1, "RING SEED delays the self ring");
    check (near (bushidoAt (*proc)->param ("CLOCK:TEMPO"), 0.5f), "RING SEED sets CLOCK:TEMPO to 0.5");
}

void testSecondRoninStays()
{
    auto proc = std::make_unique<JidaiProcessor>();
    auto* second = dynamic_cast<jidai::RoninDevice*> (proc->addDevice (jidai::DeviceKind::Ronin));
    check (second != nullptr && second->rackId() == "MS-50#2", "a second Ronin is MS-50#2");
    if (second == nullptr)
        return;
    const int range = panelKnobIndex ("VCO", "RANGE");
    second->setKnob (range, 0.1f);
    const std::string keptA = "MS-50#2/EXT IN:MONO";
    const std::string keptB = "MS-50#2/VCF:IN";
    check (hasCable (proc->rack(), keptA, keptB), "the second Ronin starts with its Voice cable");
    proc->loadPattern (bushidoAt (*proc), 1, 0);
    check (hasCable (proc->rack(), keptA, keptB), "LOOP BASS on the first pair leaves the second Ronin's Voice cable");
    check (near (second->knob (range), 0.1f), "LOOP BASS leaves the second Ronin's range");
    check (near (roninAt (*proc)->knob (range), 0.0f), "LOOP BASS sets the first Ronin's range");
    check (proc->loadedPattern (second) == std::pair<int, int> (0, kDefaultFactoryPreset), "the second Ronin screen stays on Voice");
}

void testMissingBushido()
{
    auto proc = std::make_unique<JidaiProcessor>();
    auto* b = bushidoAt (*proc);
    auto* r = roninAt (*proc);
    proc->removeDevice (b);
    proc->loadRoninPreset (r, 1, 1);
    check (near (r->knob (panelKnobIndex ("VCO", "RANGE")), 0.5f), "RING SEED without Bushido still sets VCO RANGE");
    check (! anySq10 (proc->rack()), "RING SEED without Bushido skips the SQ-10 cables");
    check (hasCable (proc->rack(), "MS-50#1/VCO:SAW", "MS-50#1/RING:A"), "RING SEED without Bushido keeps the Ronin cables");
    check (proc->loadedPattern (r) == std::pair<int, int> (1, 1), "RING SEED without Bushido still shows on the Ronin screen");
}

void testUserFiles()
{
    auto first = std::make_unique<JidaiProcessor>();
    auto* b = bushidoAt (*first);
    auto* r = roninAt (*first);
    first->loadPattern (b, 1, 0);
    b->setParam ("CLOCK:TEMPO", 0.25f);
    const int bushidoShown = first->savePattern (b, 1, "BUSER");
    check (bushidoShown == 2, "a Bushido bank B save is index 2, got " + std::to_string (bushidoShown));
    check (first->patternNames (1)[0] == "LOOP BASS", "the Bushido save leaves LOOP BASS in front");
    check (first->loadedPattern (r) == std::pair<int, int> (1, 0), "the Bushido save leaves the Ronin screen on LOOP BASS");
    first->loadPattern (b, 1, bushidoShown);
    check (near (b->param ("CLOCK:TEMPO"), 0.25f), "the saved Bushido pattern reloads its tempo");
    check (first->loadedPattern (r) == std::pair<int, int> (1, 0), "reloading the Bushido pattern leaves the Ronin screen");

    r->setKnob (panelKnobIndex ("VCF", "CUTOFF"), 0.2f);
    r->setEffectOn (false);
    const int roninA = first->saveRoninPreset (r, 0, "MINE");
    check (roninA == 13, "a Ronin bank A save is index 13, got " + std::to_string (roninA));
    const int roninB = first->saveRoninPreset (r, 1, "RUSER");
    check (roninB == 2, "a Ronin bank B save is index 2, got " + std::to_string (roninB));
    check (first->roninPresetNames (1)[0] == "LOOP BASS", "the Ronin save leaves LOOP BASS in front");
    check (near (b->param ("CLOCK:TEMPO"), 0.25f), "the Ronin save leaves CLOCK:TEMPO");
    check (first->loadedPattern (b) == std::pair<int, int> (1, 2), "the Ronin save leaves the Bushido screen on BUSER");
    first->loadRoninPreset (r, 1, roninB);
    check (near (b->param ("CLOCK:TEMPO"), 0.25f), "a Ronin user preset leaves CLOCK:TEMPO");
    check (first->loadedPattern (b) == std::pair<int, int> (1, 2), "a Ronin user preset leaves the Bushido screen");
    check (! r->effectOn(), "the Ronin user preset reloads effect off");
    first.reset();

    auto second = std::make_unique<JidaiProcessor>();
    check (second->roninPresetNames (0).size() == 14, "the next processor reads the Ronin user preset");
    check (second->roninPresetNames (0)[13] == "MINE", "the reloaded name is MINE");
    check (second->patternNames (1)[2] == "BUSER", "the next processor reads the Bushido user pattern");
    auto* r2 = roninAt (*second);
    auto* b2 = bushidoAt (*second);
    second->loadRoninPreset (r2, 0, 13);
    check (near (r2->knob (panelKnobIndex ("VCF", "CUTOFF")), 0.2f), "MINE reloads VCF CUTOFF 0.2");
    check (! r2->effectOn(), "MINE reloads effect off");
    check (countInternal (second->rack(), r2->rackId() + "/") == 6, "MINE reloads the six LOOP BASS cables that stay on the Ronin");
    check (second->loadedPattern (b2) == std::pair<int, int> (0, 0), "loading MINE leaves the Bushido screen");
    second->loadPattern (b2, 1, 2);
    check (near (b2->param ("CLOCK:TEMPO"), 0.25f), "BUSER reloads CLOCK:TEMPO 0.25");
    check (second->loadedPattern (r2) == std::pair<int, int> (0, 13), "BUSER leaves the Ronin screen on MINE");

    const int blank = second->saveRoninPreset (r2, 0, "   ");
    check (blank == 14 && second->roninPresetNames (0)[14] == "PRESET", "an empty Ronin name is stored as PRESET");
}

void testStateRoundTrip()
{
    auto proc = std::make_unique<JidaiProcessor>();
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
    check (hasCable (restored->rack(), "SQ-10#1/OUTPUTS:CV A", "MS-50#1/VCO:HZ/V"), "the saved rack keeps CV A");
    check (restored->loadedPattern (b) == std::pair<int, int> (1, 0), "the saved Bushido screen is LOOP BASS");
    check (restored->loadedPattern (r) == std::pair<int, int> (1, 0), "the saved Ronin screen is LOOP BASS");
}

void testVersion1Screen()
{
    juce::XmlElement xml ("JIDAIRACK");
    xml.setAttribute ("version", 1);
    auto* bushido = xml.createNewChildElement ("DEVICE");
    bushido->setAttribute ("kind", "BUSHIDO");
    bushido->setAttribute ("number", 1);
    bushido->setAttribute ("bank", 1);
    bushido->setAttribute ("pattern", 0);
    auto* tempo = bushido->createNewChildElement ("PARAM");
    tempo->setAttribute ("id", "CLOCK:TEMPO");
    tempo->setAttribute ("value", 0.25);
    auto* ronin = xml.createNewChildElement ("DEVICE");
    ronin->setAttribute ("kind", "RONIN");
    ronin->setAttribute ("number", 1);
    ronin->setAttribute ("program", 5);
    ronin->setAttribute ("effect", 0);

    auto proc = std::make_unique<JidaiProcessor>();
    proc->testRestore (xml);
    auto* b = bushidoAt (*proc);
    auto* r = roninAt (*proc);
    check (b != nullptr && r != nullptr, "version 1 state restores both devices");
    if (b == nullptr || r == nullptr)
        return;
    check (proc->loadedPattern (b) == std::pair<int, int> (1, 2), "version 1 bank B index 0 moves behind the rack patches");
    check (near (b->param ("CLOCK:TEMPO"), 0.25f), "version 1 keeps the stored tempo");
    check (proc->loadedPattern (r) == std::pair<int, int> (0, 5), "a Ronin with no screen bank shows its program on bank A");
    check (! r->effectOn(), "version 1 keeps effect off");
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
            dir.deleteRecursively();
            if (! bushidoExisted && bushidoDir.exists())
                std::printf ("FAIL user store leaked into %s\n", bushidoDir.getFullPathName().toRawUTF8());
            if (! roninExisted && roninDir.exists())
                std::printf ("FAIL user store leaked into %s\n", roninDir.getFullPathName().toRawUTF8());
        }
    } guard { temp, bushidoExisted, roninExisted, bushidoDir, roninDir };

    JidaiProcessor::setUserStoreRootForTest (temp);
    testLoopBassThenFactory();
    testRingSeed();
    testSecondRoninStays();
    testMissingBushido();
    testUserFiles();
    testStateRoundTrip();
    testVersion1Screen();
    testEditor();

    check (! bushidoExisted || bushidoDir.exists(), "an existing BUSHIDO user folder is still there");
    if (! bushidoExisted)
        check (! bushidoDir.exists(), "tests did not create ~/Library/Application Support/BUSHIDO");
    if (! roninExisted)
        check (! roninDir.exists(), "tests did not create ~/Library/Application Support/RONIN");

    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
// ORIGAMI plugin tests (no host, no audio device): testFxStandaloneNormals, latency reporting, state format and
// round trip, the editor's sizes and controls. Writes PNGs of the editor and the rack panel modes when given an
// output directory:  OrigamiPluginTests [out-dir]

#include "../plugin/OrigamiEditor.h"
#include "../plugin/OrigamiProcessor.h"
#include "../plugin/OrigamiState.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

using namespace origami;

namespace {

int checks = 0, failures = 0;
void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok) { ++failures; std::printf ("FAIL %s\n", what.toRawUTF8()); }
}

void snapshot (juce::Component& c, const juce::File& file, float scale = 1.0f)
{
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (image, out);
    std::printf ("wrote %s (%d x %d)\n", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
}

juce::MouseEvent mouse (juce::Component& c, juce::Point<float> p, juce::Point<float> down, bool dragged)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent (source, p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             &c, &c, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
}

void run (OrigamiProcessor& p, juce::AudioBuffer<float>& b)
{
    juce::MidiBuffer m;
    p.processBlock (b, m);
}

void testStandaloneNormals()
{
    OrigamiProcessor p;
    p.prepareToPlay (48000.0, 512);
    check (p.getLatencySamples() == 0, "INIT reports latency 0");
    check (p.value (kWave) == 0.0 && p.value (kLevelComp) == 1.0 && p.value (kQuality) == 0.0, "INIT: WAVE 0, LEVEL COMP ON, 2x OFF");
    juce::Random r (7);
    juce::AudioBuffer<float> b (2, 512), ref (2, 512);
    bool exact = true;
    for (int block = 0; block < 8; ++block)
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                b.setSample (c, i, (r.nextFloat() * 2.0f - 1.0f) * (c == 0 ? 1.0f : 0.3f));
        ref.makeCopyOf (b);
        run (p, b);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                exact = exact && b.getSample (c, i) == ref.getSample (c, i);
    }
    check (exact, "testFxStandaloneNormals: host in -> IN L/R -> OUT L/R -> host out, bit-exact at INIT (x5 / /5 exact)");

    // Folding something: stays finite, and L == R for identical input (a fresh instance: no L/R history).
    OrigamiProcessor q;
    q.prepareToPlay (48000.0, 512);
    q.setValue (kWave, 0.6);
    for (int i = 0; i < 512; ++i) b.setSample (0, i, 0.8f * std::sin ((float) i * 0.05f)), b.setSample (1, i, b.getSample (0, i));
    run (q, b); run (q, b);
    bool finite = true, linked = true;
    for (int i = 0; i < 512; ++i) { finite = finite && std::isfinite (b.getSample (0, i)); linked = linked && b.getSample (0, i) == b.getSample (1, i); }
    check (finite && linked, "WAVE 0.6: finite, L == R for identical input");

    p.setValue (kQuality, 1.0);
    run (p, b);
    check (p.getLatencySamples() == kLatency2x, "QUALITY 2x reports 46 samples to the host");
    p.setValue (kQuality, 0.0);
    run (p, b);
    check (p.getLatencySamples() == 0, "back to 1x reports 0");
    check (p.getBypassParameter() != nullptr && p.getBypassParameter()->getName (32) == "BYPASS", "host bypass maps to the BYPASS parameter");
}

void testSidechain()
{
    OrigamiProcessor p;
    auto layout = p.getBusesLayout();
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    check (p.setBusesLayout (layout), "sidechain bus can be enabled");
    p.prepareToPlay (48000.0, 256);
    p.setValue (kWave, 0.5);
    p.setValue (kVc1Src, 3.0);    // SIDECHAIN
    p.setValue (kVc1Amt, 1.0);
    p.setValue (kLevelComp, 0.0);
    juce::AudioBuffer<float> b (4, 256);
    auto fill = [&b] (float sc) {
        for (int i = 0; i < 256; ++i)
        {
            b.setSample (0, i, 0.5f * std::sin ((float) i * 0.07f)); b.setSample (1, i, b.getSample (0, i));
            b.setSample (2, i, sc); b.setSample (3, i, sc);
        }
    };
    fill (0.0f); run (p, b); fill (0.0f); run (p, b);
    const float quiet = b.getSample (0, 100);
    fill (0.8f); run (p, b); fill (0.8f); run (p, b);
    check (std::abs (b.getSample (0, 100) - quiet) > 1e-3f, "VC 1 SOURCE = SIDECHAIN: the sidechain moves the fold");
}

void testState()
{
    OrigamiProcessor a;
    a.setValue (kWave, 0.42);
    a.setValue (kSym2, -0.3);
    a.setValue (kVc3Src, 2.0);
    a.setValue (kRelease, 250.0);
    a.uiScale = 1.25f;
    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    OrigamiProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    bool same = true;
    for (int p = 0; p < kParamCount; ++p)
        same = same && std::abs (a.value (p) - b.value (p)) < 1e-4 * (paramInfo (p).max - paramInfo (p).min);
    check (same && b.uiScale == 1.25f, "state round trip (every param, UI scale)");

    auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
    check (xml != nullptr && xml->hasTagName ("ORIGAMI") && xml->getIntAttribute ("format") == 1 && xml->getStringAttribute ("unit") == "ORIGAMI",
           "state is format 1, unit ORIGAMI (the rack device's child format)");
    check (xml != nullptr && xml->getNumChildElements() == kParamCount, "one PARAM per id");
    double v[kParamCount];
    check (xml != nullptr && stateFromXml (*xml, v).ok && std::abs (v[kWave] - 0.42) < 1e-6 && v[kVc3Src] == 2.0, "the shared reader (used by the rack) loads the plugin state");

    juce::XmlElement newer ("ORIGAMI");
    newer.setAttribute ("format", 2);
    newer.setAttribute ("unit", "ORIGAMI");
    for (int p = 0; p < kParamCount; ++p) v[p] = -99.0;
    check (! stateFromXml (newer, v).ok && v[0] == -99.0, "a newer format is refused and leaves the values alone");
    juce::XmlElement partial ("ORIGAMI");
    partial.setAttribute ("format", 1);
    auto* e = partial.createNewChildElement ("PARAM"); e->setAttribute ("id", "wave"); e->setAttribute ("value", 7.0);
    auto* u = partial.createNewChildElement ("PARAM"); u->setAttribute ("id", "future_knob"); u->setAttribute ("value", 1.0);
    check (stateFromXml (partial, v).ok && v[kWave] == 1.0 && v[kMix] == 1.0 && v[kLevelComp] == 1.0, "unknown ids ignored, missing ids default, values clamped");
}

void testEditor (const juce::File& outDir)
{
    OrigamiProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* e = dynamic_cast<OrigamiEditor*> (ed.get());
    check (e != nullptr && e->getWidth() == 1200 && e->getHeight() == 360, "editor opens at 1200 x 360");
    if (e == nullptr) return;
    auto& panel = e->panel();

    // Drag WAVE up by 100 px: half the range.
    const auto c = panel.controlCentre (kWave);
    panel.mouseDown (mouse (panel, c, c, false));
    for (int i = 1; i <= 10; ++i) panel.mouseDrag (mouse (panel, c - juce::Point<float> (0, 10.0f * (float) i), c, true));
    panel.mouseUp (mouse (panel, c - juce::Point<float> (0, 100), c, true));
    check (std::abs (p.value (kWave) - 0.5) < 0.01, "dragging WAVE 100 px sets it to 0.5, got " + juce::String (p.value (kWave)));
    const auto t = panel.controlCentre (kLevelComp);
    panel.mouseDown (mouse (panel, t, t, false)); panel.mouseUp (mouse (panel, t, t, false));
    check (p.value (kLevelComp) == 0.0, "clicking LEVEL COMP turns it off");
    panel.mouseDown (mouse (panel, t, t, false)); panel.mouseUp (mouse (panel, t, t, false));
    panel.mouseDoubleClick (mouse (panel, c, c, false));
    check (p.value (kWave) == 0.0, "double-click resets WAVE to 0 (true bypass)");

    // Jacks: 8, symmetric about the centre line, >= 16 px targets at the smallest scale.
    bool sym = true;
    for (int j = 0; j < 4; ++j)
        sym = sym && std::abs ((panel.jackCentre (j).x + panel.jackCentre (7 - j).x) - 2.0f * (32.0f + 568.0f)) < 0.5f;
    check (sym, "front jack row is mirror-symmetric (4 | 4)");

    // A demo setting for the pictures.
    p.setValue (kWave, 0.5); p.setValue (kGain, 3.0); p.setValue (kVcaDepth, 0.4); p.setValue (kMix, 0.75); p.setValue (kSym, 0.2);
    p.setValue (kStage3, -0.2); p.setValue (kSym2, 0.4); p.setValue (kVc1Amt, 0.5); p.setValue (kVc2Src, 2.0);
    juce::AudioBuffer<float> b (2, 2048);
    for (int i = 0; i < 2048; ++i) { b.setSample (0, i, 0.7f * std::sin ((float) i * 0.03f)); b.setSample (1, i, 0.6f * std::sin ((float) i * 0.031f)); }
    run (p, b);
    if (outDir != juce::File())
    {
        snapshot (panel, outDir.getChildFile ("origami_standalone_main.png"));
        snapshot (panel, outDir.getChildFile ("origami_standalone_main@2x.png"), 2.0f);
    }
    for (auto pg : { Page::Stages, Page::Dynamics, Page::Setup })
    {
        const auto tc = panel.tabCentre (pg);
        panel.mouseDown (mouse (panel, tc, tc, false)); panel.mouseUp (mouse (panel, tc, tc, false));
        check (panel.page() == pg, juce::String ("tab ") + OrigamiPanel::pageName (pg));
        if (outDir != juce::File())
            snapshot (panel, outDir.getChildFile (juce::String ("origami_standalone_") + juce::String (OrigamiPanel::pageName (pg)).toLowerCase() + ".png"));
    }
    // SETUP: UI SCALE 75 % and 200 % resize the editor.
    const auto s75 = panel.toLocal ({ 32.0f + 568.0f - 128.0f, 28.0f + 80.0f });
    panel.mouseDown (mouse (panel, s75, s75, false)); panel.mouseUp (mouse (panel, s75, s75, false));
    check (e->getWidth() == 900 && e->getHeight() == 270, "UI SCALE 75 % -> 900 x 270");
    const auto s200 = panel.toLocal ({ 32.0f + 568.0f + 128.0f, 28.0f + 80.0f });
    panel.mouseDown (mouse (panel, s200, s200, false)); panel.mouseUp (mouse (panel, s200, s200, false));
    check (e->getWidth() == 2400 && e->getHeight() == 720 && std::abs (p.uiScale - 2.0f) < 0.01f, "UI SCALE 200 % -> 2400 x 720");
    e->setSize (900, 270);
    check (panel.jackRadius() * 2.0f >= 16.0f, "jack targets >= 16 px at 75 %");

    // The rack modes of the same panel (the rack device binds it to OrigamiDevice; here to the plugin).
    struct Bind : OrigamiPanel::Access
    {
        OrigamiProcessor& p;
        explicit Bind (OrigamiProcessor& q) : p (q) {}
        double get (int i) const override { return p.value (i); }
        void set (int i, double v) override { p.setValue (i, v); }
        float inPeak (int ch) const override { return p.core().inPeak (ch); }
        float outPeak (int ch) const override { return p.core().outPeak (ch); }
        float follower() const override { return p.core().follower(); }
        bool over() const override { return p.core().overLit(); }
        int latencySamples() const override { return p.value (kQuality) >= 0.5 ? kLatency2x : 0; }
        double stageCurve (int s, double x) const override
        {
            double v[kParamCount];
            for (int i = 0; i < kParamCount; ++i) v[i] = p.value (i);
            return OrigamiCore::stageCurve (v, s, x);
        }
    } bind (p);
    OrigamiPanel open (bind, OrigamiPanel::Mode::RackOpen), closed (bind, OrigamiPanel::Mode::RackClosed);
    open.setSize (1136, 332);
    closed.setSize (1136, 87);
    check (closed.jackAt (closed.getLocalBounds().getCentre().toFloat()) == -1 && closed.jackCentre (0) == juce::Point<float>(), "CLOSED shows no jacks");
    check (open.jackCentre (0) == juce::Point<float> (80.0f, 284.0f), "OPEN face jack IN L at (80, 284)");
    const auto bc = closed.controlCentre (kBypass);
    closed.mouseDown (mouse (closed, bc, bc, false)); closed.mouseUp (mouse (closed, bc, bc, false));
    check (p.value (kBypass) == 1.0, "CLOSED strip BYPASS button");
    closed.mouseDown (mouse (closed, bc, bc, false)); closed.mouseUp (mouse (closed, bc, bc, false));
    if (outDir != juce::File())
    {
        snapshot (open, outDir.getChildFile ("origami_rack_open_face.png"));
        snapshot (closed, outDir.getChildFile ("origami_rack_closed_strip.png"));
    }
}

}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File();
    if (outDir != juce::File()) outDir.createDirectory();
    testStandaloneNormals();
    testSidechain();
    testState();
    testEditor (outDir);
    std::printf ("%d checks, %d failed\n%s\n", checks, failures, failures == 0 ? "ORIGAMI PLUGIN TESTS PASS" : "ORIGAMI PLUGIN TESTS FAIL");
    return failures == 0 ? 0 : 1;
}

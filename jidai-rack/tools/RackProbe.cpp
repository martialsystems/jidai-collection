// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
//
// Rack window probe (no host, no audio device, no window): builds the JIDAI RACK editor offscreen and drives it the
// way a user would, with mouse events on the real components and the editor's keys. It checks the processor after
// each step and writes PNGs of the rack (front, back, ORIGAMI OPEN and CLOSED in the rack, the 1200 x 672 window).
//   JidaiRackProbe <out-dir>      (registered with ctest as rack_ui)

#include "plugin/JidaiEditor.h"
#include "plugin/RackCableLayer.h"
#include "plugin/StarterRacks.h"
#include "plugin/RearPanel.h"
#include "plugin/ShogunFace.h"
#include "plugin/ShogunState.h"
#include "origami/plugin/OrigamiPanel.h"
#include "origami/plugin/OrigamiPresets.h"
#include "core/OrigamiDevice.h"
#include "core/FloatCompare.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <functional>
#include <thread>
#if JUCE_LINUX
 #include <execinfo.h>
 #include <csignal>
 #include <unistd.h>
#endif

namespace {

int failures = 0, checks = 0;

void expect (bool ok, const juce::String& what)
{
    ++checks;
    std::printf ("%s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (! ok)
        ++failures;
}

void pump (int ms = 30)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

juce::MouseEvent mouse (juce::Component& c, juce::Point<float> p, juce::Point<float> down, bool dragged, juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent (source, p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             &c, &c, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
}

void click (juce::Component& c, juce::Point<float> p, bool right = false)
{
    const juce::ModifierKeys mods (right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier);
    c.mouseDown (mouse (c, p, p, false, mods));
    c.mouseUp (mouse (c, p, p, false, mods));
    pump();
}

// Click whatever child of `root` is under p (root coordinates), as the real mouse would.
void clickAt (juce::Component& root, juce::Point<int> p, bool right = false)
{
    if (auto* c = root.getComponentAt (p))
        click (*c, c->getLocalPoint (&root, p).toFloat(), right);
}

void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to, bool release = true,
           juce::ModifierKeys extra = {})
{
    const auto mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | extra.getRawFlags());
    c.mouseDown (mouse (c, from, from, false, mods));
    for (int i = 1; i <= 10; ++i)
        c.mouseDrag (mouse (c, from + (to - from) * ((float) i / 10.0f), from, true, mods));
    if (release)
        c.mouseUp (mouse (c, to, from, true, mods));
    pump();
}

void snapshot (juce::Component& c, const juce::File& file, juce::Rectangle<int> area = {})
{
    const auto image = c.createComponentSnapshot (area.isEmpty() ? c.getLocalBounds() : area, true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (image, out);
    std::printf ("wrote %s (%d x %d)\n", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
}

int cableIndex (JidaiProcessor& p, const std::string& a, const std::string& b)
{
    const auto& cs = p.rack().cables();
    for (size_t i = 0; i < cs.size(); ++i)
        if ((cs[i].a == a && cs[i].b == b) || (cs[i].a == b && cs[i].b == a))
            return (int) i;
    return -1;
}

const RackCableLayer::Drawn* drawnFor (RackView& rack, int index)
{
    for (auto& d : rack.cableLayer().drawn())
        if (d.index == index)
            return &d;
    return nullptr;
}

// A host transport playing at 120 BPM from sample 0; the caller sets the block's first sample before each block.
struct PlayingHead : juce::AudioPlayHead
{
    std::atomic<long long> sample { 0 };
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        const long long s = sample.load();
        p.setIsPlaying (true);
        p.setBpm (120.0);
        p.setTimeInSamples (s);
        p.setPpqPosition ((double) s / 48000.0 * 2.0);
        return p;
    }
};

// Plays `total` samples of program `program` through a fresh processor prepared for 256-sample blocks, cut into
// blocks by `nextSize`, with the same MIDI notes and host noise whatever the cut. Returns the left+right output.
std::vector<float> render (int program, int total, const std::function<int()>& nextSize)
{
    JidaiProcessor p;
    PlayingHead head;
    p.setPlayHead (&head);
    p.prepareToPlay (48000.0, 256);
    p.setCurrentProgram (program);
    juce::AudioBuffer<float> buffer (2, 512);
    std::vector<float> out;
    juce::Random noise (7);
    std::vector<float> in ((size_t) total * 2);
    for (auto& x : in)
        x = noise.nextFloat() * 0.2f - 0.1f;
    for (int at = 0; at < total;)
    {
        const int n = juce::jmin (nextSize(), total - at, 512);
        buffer.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            buffer.setSample (0, i, in[(size_t) (at + i) * 2]);
            buffer.setSample (1, i, in[(size_t) (at + i) * 2 + 1]);
        }
        juce::MidiBuffer midi;
        for (int i = 0; i < n; ++i)            // a note every 3000 samples, off 1500 later
        {
            const int s = at + i;
            if (s % 3000 == 100)  midi.addEvent (juce::MidiMessage::noteOn (1, 48 + (s / 3000) % 24, (juce::uint8) 100), i);
            if (s % 3000 == 1600) midi.addEvent (juce::MidiMessage::noteOff (1, 48 + (s / 3000) % 24), i);
        }
        head.sample = at;
        p.processBlock (buffer, midi);
        for (int i = 0; i < n; ++i)
        {
            out.push_back (buffer.getSample (0, i));
            out.push_back (buffer.getSample (1, i));
        }
        at += n;
    }
    p.setPlayHead (nullptr);
    return out;
}

void key (JidaiEditor& e, int code, juce::ModifierKeys mods = {}, juce::juce_wchar ch = 0)
{
    e.keyPressed (juce::KeyPress (code, mods, ch));
    pump();
}

}

int main (int argc, char** argv)
{
   #if JUCE_LINUX
    // A crash prints a raw backtrace (resolve with addr2line -e JidaiRackProbe -f -C <offsets>).
    std::signal (SIGSEGV, [] (int) { void* b[64]; const int n = backtrace (b, 64); backtrace_symbols_fd (b, n, 2); _exit (3); });
   #endif
    juce::ScopedJuceInitialiser_GUI gui;
    // JIDAI_HASHES=1: print a hash of every starter rack's render (2 s, mixed block sizes) and of its saved state,
    // then exit. Two builds that print the same lines sound and save the same.
    if (std::getenv ("JIDAI_HASHES") != nullptr)
    {
        JidaiProcessor probe;
        for (int prog = 0; prog < probe.getNumPrograms(); ++prog)
        {
            int k = 0;
            const int sizes[] = { 256, 17, 512, 1, 100, 333 };
            const auto audio = render (prog, 96000, [&] { return sizes[(k++) % 6]; });
            std::uint64_t h = 1469598103934665603ull;
            for (float x : audio)
            {
                std::uint32_t b;
                std::memcpy (&b, &x, 4);
                h = (h ^ b) * 1099511628211ull;
            }
            JidaiProcessor p;
            p.prepareToPlay (48000.0, 256);
            p.setCurrentProgram (prog);
            juce::MemoryBlock st;
            p.getStateInformation (st);
            std::uint64_t hs = 1469598103934665603ull;
            for (size_t i = 0; i < st.getSize(); ++i)
                hs = (hs ^ (std::uint8_t) st[i]) * 1099511628211ull;
            std::printf ("HASH %2d %-24s audio %016llx state %016llx (%d bytes)\n", prog, p.getProgramName (prog).toRawUTF8(),
                         (unsigned long long) h, (unsigned long long) hs, (int) st.getSize());
        }
        return 0;
    }
    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "probe");
    out.createDirectory();

    JidaiProcessor proc;
    proc.prepareToPlay (48000.0, 256);
    std::unique_ptr<JidaiEditor> editor (static_cast<JidaiEditor*> (proc.createEditor()));
    editor->setVisible (true);
    pump();
    auto& rack = editor->rackView();
    auto& layer = rack.cableLayer();

    expect (editor->getWidth() == 1200 && editor->getHeight() == 672, "the window opens at 1200 x 672");
    expect (proc.rack().deviceCount() == 1 && proc.rack().device (0)->kind() == jidai::DeviceKind::RackIO, "fresh instance: RACK I/O only");
    expect (rack.slotCount() == 1 && rack.slotBounds (0).getHeight() == juce::roundToInt (RackView::kUnit * rack.scale()), "RACK I/O is 1 U");
    expect (rack.removeButtonBounds (0).isEmpty(), "RACK I/O has no remove button");

    // Browser: groups, cards, search, RACK I/O at most one.
    {
        auto& b = editor->browser();
        expect (b.getWidth() == JidaiEditor::kListWidth, "browser is 184 wide");
        expect (b.rowCount() == 6, "browser: BUSHIDO, RONIN, SHOGUN, RONIN FX, ORIGAMI, RACK I/O");
        expect (juce::String (b.row (2).group) == "DRUMS" && b.row (2).kind == jidai::DeviceKind::Shogun, "SHOGUN under DRUMS");
        expect (juce::String (b.row (4).group) == "EFFECT" && b.row (4).kind == jidai::DeviceKind::Origami
                && juce::String (b.row (5).group) == "UTILITY" && b.row (5).kind == jidai::DeviceKind::RackIO, "ORIGAMI under EFFECT, RACK I/O under UTILITY");
        expect (! b.available (b.row (5)), "RACK I/O card is used up (one per rack)");
        b.setSearch ("ori");
        expect (b.rowCount() == 1 && b.row (0).kind == jidai::DeviceKind::Origami, "search 'ori' shows ORIGAMI");
        b.setSearch ("");
        click (b, b.rowBounds (5).toFloat().getCentre());
        expect (proc.rack().deviceCount() == 1, "clicking the used-up RACK I/O card adds nothing");
    }
    snapshot (*editor, out.getChildFile ("window_default_rack_1200x672.png"));

    // Build BUSHIDO > RONIN > ORIGAMI by drops at the bottom of the rack (auto-route on).
    for (const char* what : { "add:BUSHIDO", "add:RONIN", "add:ORIGAMI" })
    {
        juce::DragAndDropTarget::SourceDetails drop (what, &editor->deviceList(), { 10, rack.getHeight() - 5 });
        rack.itemDropped (drop);
        pump();
    }
    expect (proc.rack().deviceCount() == 4 && rack.slotCount() == 4, "three devices dropped in");
    expect (cableIndex (proc, "BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:V/OCT") >= 0 && cableIndex (proc, "BUSHIDO#1/OUTPUTS:GATE A", "RONIN#1/EG 1:TRIG") >= 0,
            "auto-route: BUSHIDO CV A > RONIN V/OCT, GATE A > EG 1 TRIG");
    expect (cableIndex (proc, "RONIN#1/HOST:OUT L", "RACK#1/MAIN:OUT L") >= 0, "auto-route: RONIN HOST OUT > MAIN OUT");
    expect (cableIndex (proc, "RACK#1/HOST:IN L", "ORIGAMI#1/HOST:IN L") >= 0 && cableIndex (proc, "ORIGAMI#1/HOST:OUT L", "RACK#1/MAIN:OUT L") >= 0,
            "auto-route: HOST IN > ORIGAMI > MAIN OUT");
    const int iB = 1, iR = 2, iO = 3;
    expect (std::abs (rack.slotBounds (iB).getHeight() - 3 * RackView::kUnit * rack.scale()) < 2.0f
                && std::abs (rack.slotBounds (iR).getHeight() - 4 * RackView::kUnit * rack.scale()) < 2.0f
                && std::abs (rack.slotBounds (iO).getHeight() - 3 * RackView::kUnit * rack.scale()) < 2.0f,
            "OPEN fronts: BUSHIDO 3 U, RONIN 4 U, ORIGAMI 3 U");
    expect (! rack.tabBounds (iO, 3).isEmpty() && rack.tabBounds (iO, 4).isEmpty(), "ORIGAMI strip has 4 tabs");

    // Patch by dragging a cable: BUSHIDO CV B > RONIN VCF CUTOFF; then a refusal (two outputs).
    {
        const auto* from = rack.spotFor ("BUSHIDO#1/OUTPUTS:CV B");
        const auto* to = rack.spotFor ("RONIN#1/VCF:CUTOFF");
        expect (from != nullptr && to != nullptr, "front jacks of BUSHIDO and RONIN are on the cable layer");
        if (from != nullptr && to != nullptr)
        {
            drag (layer, from->p, to->p);
            expect (cableIndex (proc, "BUSHIDO#1/OUTPUTS:CV B", "RONIN#1/VCF:CUTOFF") >= 0, "drag from CV B to CUTOFF patches a cable");
        }
        const auto* saw = rack.spotFor ("RONIN#1/VCO:SAW");
        const auto* tri = rack.spotFor ("RONIN#1/MG:TRI");
        if (saw != nullptr && tri != nullptr)
        {
            const auto before = proc.rack().cables().size();
            drag (layer, saw->p, tri->p);
            expect (proc.rack().cables().size() == before && layer.lastMessage() == "two outputs: nothing flows", "output onto output is refused and says why");
        }
        // Esc cancels a drag in progress.
        if (saw != nullptr && to != nullptr)
        {
            drag (layer, saw->p, saw->p + juce::Point<float> (60.0f, 60.0f), false);
            expect (layer.isDragging(), "a cable drag is in progress");
            key (*editor, juce::KeyPress::escapeKey);
            expect (! layer.isDragging(), "Esc cancels it");
            layer.mouseUp (mouse (layer, saw->p, saw->p, true));
        }
    }

    // Front, HIDE PASS-THRU (default): adjacent ropes, back-only cables as stubs with a tag.
    {
        expect (rack.cableMode() == JidaiProcessor::CablesHidePassThru, "front cable mode defaults to HIDE PASS-THRU");
        const auto* adj = drawnFor (rack, cableIndex (proc, "BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:V/OCT"));
        expect (adj != nullptr && adj->shown == RackCableLayer::Shown::Rope, "BUSHIDO > RONIN (adjacent) is a rope");
        const auto* host = drawnFor (rack, cableIndex (proc, "RONIN#1/HOST:OUT L", "RACK#1/MAIN:OUT L"));
        expect (host != nullptr && host->shown == RackCableLayer::Shown::None, "RONIN HOST OUT > MAIN OUT: both ends back-only, nothing on the front");
        expect (rack.whyHidden ("RACK#1/MAIN:OUT L") == "back", "MAIN OUT is a back-only jack");

        // A pass-through cable (BUSHIDO over RONIN into ORIGAMI, non-adjacent): HIDE PASS-THRU draws stubs with tags
        // naming the far end; ALL draws the rope; SELECTED dims cables that do not touch the selected device.
        layer.clearMessage();     // the refusal above times out on screen; the shots below are about the cables
        proc.rack().connect ("BUSHIDO#1/OUTPUTS:CV B", "ORIGAMI#1/VC:VC 1");
        proc.sendChangeMessage();
        pump();
        layer.refresh();
        const int pass = cableIndex (proc, "BUSHIDO#1/OUTPUTS:CV B", "ORIGAMI#1/VC:VC 1");
        const auto* stub = drawnFor (rack, pass);
        expect (stub != nullptr && stub->shown == RackCableLayer::Shown::Stub && stub->tagA.contains ("ORIGAMI") && stub->tagB.contains ("BUSHIDO"),
                "HIDE PASS-THRU: a non-adjacent cable is two tagged stubs");
        snapshot (rack, out.getChildFile ("rack_front_hide_passthru_stubs.png"));
        rack.setCableMode (JidaiProcessor::CablesAll);
        layer.refresh();
        const auto* rope = drawnFor (rack, pass);
        expect (rope != nullptr && rope->shown == RackCableLayer::Shown::Rope, "ALL: the same cable is a rope");
        snapshot (rack, out.getChildFile ("rack_front_all.png"));
        rack.selectDevice (proc.rack().findDevice ("RONIN#1"));
        rack.setCableMode (JidaiProcessor::CablesSelected);
        layer.refresh();
        const auto* dim = drawnFor (rack, pass);
        const auto* lit = drawnFor (rack, cableIndex (proc, "BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:V/OCT"));
        expect (dim != nullptr && lit != nullptr && dim->alpha < 0.2f && lit->alpha > 0.9f, "SELECTED: RONIN's cables stay lit, the rest are dimmed");
        snapshot (rack, out.getChildFile ("rack_front_selected.png"));
        proc.rack().disconnect ("BUSHIDO#1/OUTPUTS:CV B", "ORIGAMI#1/VC:VC 1");
        rack.selectDevice (nullptr);
        rack.setCableMode (JidaiProcessor::CablesHidePassThru);
        proc.sendChangeMessage();
        pump();
        layer.refresh();
    }
    snapshot (rack, out.getChildFile ("rack_front.png"));
    {
        // ORIGAMI's factory banks on the rack face: the preset box steps the same list as the plugin.
        auto* od = dynamic_cast<jidai::OrigamiDevice*> (proc.rack().findDevice ("ORIGAMI#1"));
        auto* op = dynamic_cast<OrigamiPanel*> (rack.faceComponent (iO));
        expect (od != nullptr && op != nullptr, "ORIGAMI open face is the ORIGAMI panel");
        if (od != nullptr && op != nullptr)
        {
            double saved[origami::kParamCount];
            for (int p = 0; p < origami::kParamCount; ++p) saved[p] = od->param (p);
            const int savedProgram = od->program();
            const auto& bank = origami::factoryPresets();
            clickAt (*op, op->presetPartCentre (OrigamiPanel::PresetPart::Next).toInt());
            pump (5);
            bool same = od->program() == 1;
            for (int p = 0; p < origami::kParamCount && same; ++p)
                same = p == origami::kBypass || std::abs (od->param (p) - bank[1].values[p]) < 1e-9;
            expect (bank.size() == 33 && same, "ORIGAMI preset box next arrow in the rack loads factory preset 2 of "
                                                   + juce::String ((int) bank.size()) + " (" + bank[1].displayName() + ")");
            clickAt (*op, op->presetPartCentre (OrigamiPanel::PresetPart::Prev).toInt());
            pump (5);
            expect (od->program() == 0, "ORIGAMI preset box previous arrow steps back to INIT");
            const auto menu = op->presetMenu();
            expect (menu.getNumItems() == 5, "ORIGAMI preset menu: INIT and four bank submenus (" + juce::String (menu.getNumItems()) + " items)");
            for (int p = 0; p < origami::kParamCount; ++p) od->setParam (p, saved[p]);
            od->setProgram (savedProgram);
            pump (5);
        }
    }
    snapshot (rack, out.getChildFile ("origami_open_in_rack.png"), rack.slotBounds (iO));

    // ORIGAMI CLOSED: 1 U, no jacks.
    clickAt (rack, rack.openCloseBounds (iO).getCentre());
    pump();
    jidai::Device* origami = proc.rack().findDevice ("ORIGAMI#1");
    expect (origami->closed && std::abs (rack.slotBounds (iO).getHeight() - RackView::kUnit * rack.scale()) < 2.0f, "OPEN/CLOSED button closes ORIGAMI to 1 U");
    expect (rack.spotFor ("ORIGAMI#1/IN:IN L") == nullptr && rack.whyHidden ("ORIGAMI#1/IN:IN L") == "closed", "a CLOSED device shows no jacks");
    snapshot (rack, out.getChildFile ("origami_closed_in_rack.png"), rack.slotBounds (iO));

    // BUSHIDO tabs: STEPS replaces the face; its jacks leave the layer and its cables become tagged stubs.
    {
        clickAt (rack, rack.tabBounds (iB, 1).getCentre());
        pump();
        expect (rack.tabOf (proc.rack().findDevice ("BUSHIDO#1")) == 1, "STEPS tab selected");
        expect (rack.spotFor ("BUSHIDO#1/OUTPUTS:CV A") == nullptr, "BUSHIDO jacks are off the layer on STEPS");
        const auto* d = drawnFor (rack, cableIndex (proc, "BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:V/OCT"));
        expect (d != nullptr && d->shown == RackCableLayer::Shown::Stub && d->tagB.contains ("(tab)"), "its cable is a stub tagged '(tab)'");
        snapshot (rack, out.getChildFile ("bushido_steps_tab_in_rack.png"), rack.slotBounds (iB));
        clickAt (rack, rack.tabBounds (iB, 0).getCentre());
        pump();
    }

    // Keys: K cycles the cable mode, C closes the selected device, F folds it, Shift+F folds all.
    {
        key (*editor, 'K', {}, 'k');
        expect (rack.cableMode() == JidaiProcessor::CablesSelected, "K: HIDE PASS-THRU > SELECTED");
        key (*editor, 'K', {}, 'k');
        expect (rack.cableMode() == JidaiProcessor::CablesHide, "K: SELECTED > HIDE");
        const auto* d = drawnFor (rack, cableIndex (proc, "BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:V/OCT"));
        expect (d != nullptr && d->shown == RackCableLayer::Shown::Dot, "HIDE: plugs only");
        rack.setCableMode (JidaiProcessor::CablesHidePassThru);
        auto* ronin = proc.rack().findDevice ("RONIN#1");
        clickAt (rack, rack.stripBounds (iR).getCentre() + juce::Point<int> (juce::roundToInt (-500 * rack.scale()), 0));
        expect (rack.selectedDevice() == ronin, "clicking a strip selects the device");
        key (*editor, 'C', {}, 'c');
        expect (ronin->closed, "C closes the selected device");
        snapshot (rack, out.getChildFile ("ronin_closed_in_rack.png"), rack.slotBounds (iR));
        expect (! rack.partBounds (iR, RackView::PartBackBadge).isEmpty(), "a CLOSED device with front cables shows the 'patch on BACK' badge");
        key (*editor, 'C', {}, 'c');
        key (*editor, 'F', {}, 'f');
        expect (ronin->folded && rack.slotBounds (iR).getHeight() == juce::roundToInt (RackView::kFoldHeight * rack.scale()), "F folds the selected device to a strip");
        key (*editor, 'F', {}, 'f');
        key (*editor, 'F', juce::ModifierKeys::shiftModifier, 'F');
        expect (rack.allFolded(), "Shift+F folds every device");
        key (*editor, 'F', juce::ModifierKeys::shiftModifier, 'F');
        expect (! ronin->folded, "Shift+F opens them again");
    }

    // BACK (Tab): every jack on the rear plates, ALL cables as ropes; MAIN OUT comp badge data.
    {
        key (*editor, juce::KeyPress::tabKey);
        expect (proc.showBack, "Tab flips to the BACK");
        expect (rack.cableMode() == JidaiProcessor::CablesAll, "back cable mode defaults to ALL");
        size_t jacks = 0;
        for (int i = 0; i < proc.rack().deviceCount(); ++i)
            jacks += proc.rack().device (i)->jacks().size();
        expect (rack.jackSpots().size() == jacks, "BACK shows every jack (" + juce::String ((int) rack.jackSpots().size()) + " of " + juce::String ((int) jacks) + ")");
        expect (std::abs (rack.slotBounds (iO).getHeight() - RackView::kUnit * rack.scale()) < 2.0f
                    && std::abs (rack.slotBounds (iB).getHeight() - 3 * RackView::kUnit * rack.scale()) < 2.0f
                    && std::abs (rack.slotBounds (iR).getHeight() - 4 * RackView::kUnit * rack.scale()) < 2.0f,
                "rear plates: ORIGAMI 1 U, BUSHIDO 3 U, RONIN 4 U");
        bool allRopes = true;
        for (auto& d : layer.drawn())
            allRopes = allRopes && d.shown == RackCableLayer::Shown::Rope;
        expect (allRopes, "ALL: every cable is a rope on the back");
        // A rope's sag follows 18 + 0.14|dx| + 0.06|dy| design units.
        expect (std::abs (RackCableLayer::sagFor (1000.0f, 100.0f, 1.0f) - (18.0f + 140.0f + 6.0f)) < 1.0e-3f, "sag rule");
        // Patch on the back: ORIGAMI OUT R into RONIN MIX IN 2 (back-only and front jacks alike are reachable).
        const auto* a = rack.spotFor ("ORIGAMI#1/OUTPUT:OUT R");
        const auto* b = rack.spotFor ("RONIN#1/MIX:IN 2");
        if (a == nullptr) a = rack.spotFor ("ORIGAMI#1/OUT:OUT R");
        expect (a != nullptr && b != nullptr, "rear jacks are hit targets");
        if (a != nullptr && b != nullptr)
        {
            const auto from = a->id;
            drag (layer, a->p, b->p);
            expect (cableIndex (proc, from, "RONIN#1/MIX:IN 2") >= 0, "drag on the back patches a cable");
        }
    }
    snapshot (rack, out.getChildFile ("rack_back.png"));

    // Cable gestures (jidai/ui/CableEdit.h), on the BACK where every jack is shown: reroute, the stack modifier,
    // drag-off, click to front (saved), palette dimming, the cable menu's Remove, undo / redo of each.
    {
        auto cablesNow = [&proc] { return proc.rack().cables(); };
        auto spotId = [&rack] (const std::string& id) { return rack.spotFor (id); };
        // A free input on RONIN that `from` may feed (and not `avoid`).
        auto freeInput = [&] (const std::string& from, const std::string& avoid) -> const RackView::JackSpot*
        {
            for (auto& sp : rack.jackSpots())
                if (! sp.out && sp.id.rfind ("RONIN#1/", 0) == 0 && sp.id != avoid && proc.rack().check (from, sp.id) == jidai::Rack::Check::Ok
                    && jidai::cable::cablesAt (proc.rack().cables(), sp.id) == 0 && sp.role == jidai::jcs::Role::CV)
                    return &sp;
            return nullptr;
        };
        // A point at least 40 px from every jack and 12 px from every cable, near p.
        auto emptyNear = [&] (juce::Point<float> p)
        {
            for (float dy = 0.0f; dy < 600.0f; dy += 7.0f)
                for (float dx : { 0.0f, 37.0f, -37.0f, 81.0f, -81.0f })
                {
                    const auto q = p.translated (dx, -40.0f - dy);
                    bool clear = rack.getLocalBounds().toFloat().reduced (20.0f).contains (q);
                    for (auto& sp : rack.jackSpots())
                        clear = clear && sp.p.getDistanceFrom (q) > 40.0f;
                    for (auto& d : layer.drawn())
                    {
                        juce::Point<float> on;
                        if (! d.path.isEmpty())
                        {
                            d.path.getNearestPoint (q, on);
                            clear = clear && on.getDistanceFrom (q) > 12.0f;
                        }
                    }
                    if (clear)
                        return q;
                }
            return p.translated (0.0f, -300.0f);
        };
        const std::string cvA = "BUSHIDO#1/OUTPUTS:CV A", vOct = "RONIN#1/VCO:V/OCT";
        const int i = cableIndex (proc, cvA, vOct);
        expect (i >= 0, "cable CV A > V/OCT is patched");
        layer.setColour (i, 2);
        const auto start = cablesNow();
        const auto spec = start[(size_t) i];
        const auto* targetPtr = freeInput (cvA, vOct);
        const auto* plugPtr = spotId (vOct);
        expect (targetPtr != nullptr && plugPtr != nullptr, "a free CV input on RONIN for the reroute");
        if (targetPtr != nullptr && plugPtr != nullptr)
        {
            const auto targetSpot = *targetPtr, plugSpot = *plugPtr;      // copies: the spot list may be rebuilt
            const auto* target = &targetSpot;
            const auto* plug = &plugSpot;
            const auto targetId = target->id;
            const auto depth = proc.cableHistory().undoDepth();
            // 1. Reroute: grab the plug on V/OCT (off-centre, on the plug body) and drop it on the free input.
            drag (layer, plug->p.translated (plug->r * 1.1f, 0.0f), target->p);
            auto now = cablesNow();
            expect (now.size() == start.size(), "reroute: no cable created or lost (" + juce::String ((int) now.size()) + ")");
            const auto& moved = now[(size_t) i];
            expect ((moved.a == cvA && moved.b == targetId) || (moved.b == cvA && moved.a == targetId),
                    "reroute: the same cable (same slot) now runs CV A > " + juce::String (targetId));
            expect (moved.color == 2 && moved.age == spec.age, "reroute keeps the cable's colour and age");
            expect (cableIndex (proc, cvA, vOct) < 0, "nothing left on V/OCT from CV A");
            expect (proc.cableHistory().undoDepth() == depth + 1 && proc.cableHistory().undoLabel() == "Move cable", "reroute is one undo step, 'Move cable'");
            key (*editor, 'Z', juce::ModifierKeys::commandModifier, 'z');
            expect (cablesNow() == start, "Ctrl/Cmd+Z puts the cable back on V/OCT, exactly");
            key (*editor, 'Y', juce::ModifierKeys::commandModifier, 'y');
            expect (cablesNow() == now, "Ctrl+Y redoes the move");
            key (*editor, 'Z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 'Z');
            expect (cablesNow() == now, "Shift+Cmd+Z redoes too (nothing left to redo: unchanged)");
            key (*editor, 'Z', juce::ModifierKeys::commandModifier, 'z');
            expect (cablesNow() == start, "undo again: back to the start");

            // 2. Stack: Option/Alt-drag from the used V/OCT jack adds a second cable there; plain drag would reroute.
            const auto srcP = spotId (cvA)->p;
            drag (layer, srcP, target->p, true, juce::ModifierKeys::altModifier);
            now = cablesNow();
            expect (now.size() == start.size() + 1 && cableIndex (proc, cvA, vOct) == i && cableIndex (proc, cvA, targetId) >= 0,
                    "Alt-drag from a used jack stacks a new cable; the old one stays");
            expect (jidai::cable::topCableAt (now, cvA) == (int) now.size() - 1, "the stacked cable is the top plug");
            key (*editor, 'Z', juce::ModifierKeys::commandModifier, 'z');
            expect (cablesNow() == start, "undo removes the stacked cable");

            // 3. Drag a plug off into empty space: removed; undo restores it in the same slot.
            const auto off = emptyNear (plug->p);
            drag (layer, plug->p, off);
            expect (cablesNow().size() == start.size() - 1 && cableIndex (proc, cvA, vOct) < 0, "dragging a plug into empty space removes its cable");
            key (*editor, 'Z', juce::ModifierKeys::commandModifier, 'z');
            expect (cablesNow() == start, "undo: the removed cable is back, same slot, colour and age");

            // 4. A click on a jack (no drag) changes nothing.
            click (layer, plug->p);
            expect (cablesNow() == start, "a click on a plug only selects the jack (no new cable, no move)");
        }

        // 5. Click a cable: it comes to the front, and z is saved with the rack.
        {
            int raised = -1;
            for (auto& d : layer.drawn())
            {
                if (d.shown != RackCableLayer::Shown::Rope || d.alpha < 0.9f || jidai::cable::isOnTop (cablesNow(), d.index))
                    continue;
                for (float t : { 0.5f, 0.35f, 0.65f, 0.2f, 0.8f })
                {
                    const auto at = d.path.getPointAlongPath (d.path.getLength() * t);
                    click (layer, at);
                    if (layer.selectedCable() >= 0 && jidai::cable::isOnTop (cablesNow(), layer.selectedCable()) && cablesNow()[(size_t) layer.selectedCable()].z > 0)
                    {
                        raised = layer.selectedCable();
                        break;
                    }
                }
                if (raised >= 0)
                    break;
            }
            expect (raised >= 0, "a click on a cable brings it to the front (z " + juce::String (raised >= 0 ? cablesNow()[(size_t) raised].z : -1) + ")");
            const auto order = jidai::cable::drawOrder (cablesNow());
            expect (! order.empty() && order.back() == raised, "it draws last");
            expect (! layer.drawn().empty() && layer.drawn().back().index == raised, "the layer draws it last too");
            juce::MemoryBlock st;
            proc.getStateInformation (st);
            JidaiProcessor copy;
            copy.prepareToPlay (48000.0, 256);
            copy.setStateInformation (st.getData(), (int) st.getSize());
            copy.applyPendingState();
            expect (copy.rack().cables() == cablesNow(), "z-order survives a save and load (every cable, z included)");
            expect (proc.cableHistory().undoLabel() == "Bring cable to front", "bring to front is undoable");
            key (*editor, 'Z', juce::ModifierKeys::commandModifier, 'z');
            expect (cablesNow() == start, "undo: z back to 0");
        }

        // 6. Palette: CV picked dims the jacks that cannot take a CV cable, and they refuse new cables.
        {
            auto& bar = editor->cableBar();
            expect (bar.chipCount() == 7 && bar.paletteForChip (0) == jidai::cable::kAnyRole
                        && bar.paletteForChip (1) == (int) jidai::jcs::Role::Audio && bar.paletteForChip (2) == (int) jidai::jcs::Role::CV
                        && bar.paletteForChip (6) == (int) jidai::jcs::Role::STrig,
                    "palette: ANY, AUDIO, CV, GATE/CLK, V/OCT, HZ/V LIN, S-TRIG");
            clickAt (*editor, bar.chipBounds (2).getCentre().toInt() + bar.getPosition());
            pump();
            expect (proc.cablePalette == (int) jidai::jcs::Role::CV, "clicking the CV chip picks CV");
            auto dimmed = [&] (const std::string& id) { const auto* sp = spotId (id); return sp != nullptr && layer.jackDimmed (*sp); };
            // Gate-type inputs are dimmed (CV into a gate input is refused); RONIN's S-trig EG TRIG inputs are CV-type
            // jacks that take CV, so they stay lit.
            int gateInputs = 0, gateInputsDimmed = 0;
            for (auto& sp : rack.jackSpots())
            {
                jidai::Device* d = nullptr;
                int jj = -1;
                if (! sp.out && proc.rack().resolve (sp.id, d, jj) && d->jacks()[(size_t) jj].desc.type == PortType::Gate)
                {
                    ++gateInputs;
                    gateInputsDimmed += layer.jackDimmed (sp) ? 1 : 0;
                }
            }
            expect (gateInputsDimmed == gateInputs, "CV picked: every gate input is dimmed (" + juce::String (gateInputsDimmed) + " of " + juce::String (gateInputs) + ")");
            expect (! dimmed ("RONIN#1/EG 1:TRIG"), "CV picked: RONIN EG 1 TRIG (takes CV) stays lit");
            expect (! dimmed ("RONIN#1/VCF:CUTOFF") && ! dimmed (vOct), "CV picked: CV inputs (CUTOFF, V/OCT) are lit");
            expect (dimmed ("RONIN#1/VCO:SAW") && dimmed ("BUSHIDO#1/OUTPUTS:GATE A"), "CV picked: audio and gate outputs are dimmed");
            expect (! dimmed ("BUSHIDO#1/OUTPUTS:CV C") && dimmed ("BUSHIDO#1/OUTPUTS:CV B"), "CV picked: CV outputs (CV C) lit, V/OCT outputs (CV B) dimmed");
            int dimCount = 0;
            for (auto& sp : rack.jackSpots())
                dimCount += layer.jackDimmed (sp) ? 1 : 0;
            expect (dimCount > 0 && dimCount < (int) rack.jackSpots().size(), "some jacks dimmed (" + juce::String (dimCount) + " of " + juce::String ((int) rack.jackSpots().size()) + ")");
            // Refused: a new cable from a dimmed free jack, and a new CV cable dropped on a dimmed gate input.
            const auto before = cablesNow();
            // A free dimmed output (audio) and a free lit CV input it could otherwise feed.
            int freeDimOut = -1;
            for (int k = 0; k < (int) rack.jackSpots().size(); ++k)
            {
                const auto& sp = rack.jackSpots()[(size_t) k];
                if (sp.out && sp.role == jidai::jcs::Role::Audio && layer.jackDimmed (sp) && jidai::cable::cablesAt (before, sp.id) == 0)
                    freeDimOut = k;
            }
            const auto* cvBPtr = spotId ("BUSHIDO#1/OUTPUTS:CV C");
            const RackView::JackSpot* litIn = freeDimOut >= 0 ? freeInput (rack.jackSpots()[(size_t) freeDimOut].id, vOct) : nullptr;
            expect (freeDimOut >= 0 && litIn != nullptr && cvBPtr != nullptr, "a free dimmed audio output and a free lit input to try");
            if (freeDimOut >= 0 && litIn != nullptr && cvBPtr != nullptr)
            {
                const auto cvBSpot = *cvBPtr, outSpot = rack.jackSpots()[(size_t) freeDimOut], inSpot = *litIn;
                const auto* cvB = &cvBSpot;
                const int freeGateIndex = freeDimOut;
                expect (proc.rack().check (outSpot.id, inSpot.id) == jidai::Rack::Check::Ok, "(audio into that CV input is allowed by the rack)");
                drag (layer, outSpot.p, inSpot.p);
                expect (cablesNow() == before, "a new cable cannot start on a dimmed jack (" + juce::String (outSpot.id) + ")");
                drag (layer, inSpot.p, outSpot.p);
                expect (cablesNow() == before && layer.lastMessage().contains ("can't take a CV cable"), "a new cable dropped on a dimmed jack is refused and says why");
                // Mid-drag: the jacks it cannot go to are dimmed (screenshot).
                drag (layer, cvB->p, cvB->p.translated (140.0f, -60.0f), false);
                expect (layer.isDragging(), "dragging a new CV cable");
                expect (! layer.jackTakesDrag (freeGateIndex), "while dragging, the dimmed audio output does not take it");
                snapshot (*editor, out.getChildFile ("cables_v2_drag_cv.png"));
                layer.cancelDrag();
            }
            snapshot (*editor, out.getChildFile ("cables_v2_back_cv.png"));
            clickAt (*editor, bar.chipBounds (3).getCentre().toInt() + bar.getPosition());
            pump();
            expect (proc.cablePalette == (int) jidai::jcs::Role::GateClk, "GATE/CLK chip");
            expect (! dimmed ("RONIN#1/EG 1:TRIG") && dimmed ("BUSHIDO#1/OUTPUTS:CV B") && dimmed ("RONIN#1/VCO:SAW") && ! dimmed ("BUSHIDO#1/OUTPUTS:GATE A"),
                    "GATE/CLK picked: gate outputs and every input lit, CV and audio outputs dimmed");
            clickAt (*editor, bar.chipBounds (3).getCentre().toInt() + bar.getPosition());
            pump();
            expect (proc.cablePalette == jidai::cable::kAnyRole, "clicking the picked chip again clears it");
            int none = 0;
            for (auto& sp : rack.jackSpots())
                none += layer.jackDimmed (sp) ? 1 : 0;
            expect (none == 0, "ANY: nothing dimmed");
            // The help lines on the bar, and the hover zoom.
            const auto rows = bar.helpRows().joinIntoString (" ");
            expect (rows.contains (juce::String (jidai::cable::stackModifierName()) + "-drag") && rows.contains ("reroute") && rows.contains ("Remove")
                        && rows.contains ("front") && rows.contains ("dim") && rows.contains ("empty jack"),
                    "the bar spells out every gesture: " + rows);
            expect (CableBar::helpFontPx (1.0f) >= 12.0f, "help text at least 12 px (about 9 pt) at 100 %");
            bar.showZoom (true);
            pump();
            expect (bar.zoomShowing(), "hovering the help shows it larger");
            snapshot (*editor, out.getChildFile ("cables_v2_help_zoom.png"));
            bar.showZoom (false);
        }

        // 7. Right-click a cable: the menu has Remove; choosing it removes that cable; undo brings it back.
        {
            const int j = cableIndex (proc, cvA, vOct);
            bool hasRemove = false;
            for (juce::PopupMenu::MenuItemIterator it (layer.cableMenu (j)); it.next();)
                hasRemove = hasRemove || (it.getItem().itemID == RackCableLayer::CableMenuRemove && it.getItem().text == "Remove");
            expect (hasRemove, "the cable menu has Remove");
            const auto before = cablesNow();
            expect (layer.cableMenuResult (before[(size_t) j], RackCableLayer::CableMenuRemove) && cableIndex (proc, cvA, vOct) < 0, "Remove removes it");
            expect (! layer.cableMenuResult (before[(size_t) j], RackCableLayer::CableMenuRemove), "a stale menu result does nothing");
            key (*editor, 'Z', juce::ModifierKeys::commandModifier, 'z');
            expect (cablesNow() == before, "undo brings it back");
        }
        layer.setColour (i, -1);
        key (*editor, 'Z', juce::ModifierKeys::commandModifier, 'z');      // the colour change
        expect (cablesNow()[(size_t) i].color == 2, "colour changes are undoable");
        key (*editor, 'Z', juce::ModifierKeys::commandModifier, 'z');
        expect (cablesNow()[(size_t) i].color == -1, "back to the role colour");
    }

    // Selection, Delete, colour override.
    {
        const int i = cableIndex (proc, "BUSHIDO#1/OUTPUTS:CV B", "RONIN#1/VCF:CUTOFF");
        layer.setColour (i, 4);
        expect (proc.rack().cables()[(size_t) i].color == 4, "colour override (white) stored on the cable");
        layer.selectCable (i);
        const auto n = proc.rack().cables().size();
        key (*editor, juce::KeyPress::deleteKey);
        expect (proc.rack().cables().size() == n - 1 && cableIndex (proc, "BUSHIDO#1/OUTPUTS:CV B", "RONIN#1/VCF:CUTOFF") < 0, "Delete removes the selected cable");
    }

    // Header: FRONT button, mode buttons; state round trip of the view.
    {
        clickAt (*editor, editor->headerButtonBounds (JidaiEditor::BtnFront).getCentre());
        pump();
        expect (! proc.showBack, "header FRONT button flips back to the front");
        clickAt (*editor, editor->headerButtonBounds (JidaiEditor::BtnModeAll).getCentre());
        pump();
        expect (proc.cableModeFront == JidaiProcessor::CablesAll, "header ALL sets the front cable mode");
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        proc.cableModeFront = JidaiProcessor::CablesHide;
        origami->closed = false;
        proc.setStateInformation (state.getData(), (int) state.getSize());
        pump();
        expect (proc.cableModeFront == JidaiProcessor::CablesAll && proc.rack().findDevice ("ORIGAMI#1")->closed, "view state and CLOSED are saved with the rack");
        rack.setCableMode (JidaiProcessor::CablesHidePassThru);
    }

    // Remove a device with the strip's x; move one with a drop.
    {
        jidai::Device* o = proc.rack().findDevice ("ORIGAMI#1");
        const int at = rack.indexOfSlotFor (*o);
        clickAt (rack, rack.removeButtonBounds (at).getCentre());
        pump();
        expect (proc.rack().findDevice ("ORIGAMI#1") == nullptr && cableIndex (proc, "RACK#1/HOST:IN L", "ORIGAMI#1/HOST:IN L") < 0,
                "the strip's x removes ORIGAMI and its cables");
        juce::DragAndDropTarget::SourceDetails move ("move:RONIN#1", &rack, { 10, 2 });
        rack.itemDropped (move);
        pump();
        expect (proc.rack().device (0)->kind() == jidai::DeviceKind::RackIO && proc.rack().device (1)->rackId() == "RONIN#1",
                "a device dropped at the top lands under RACK I/O");
    }

    // The window at its default size, with the demo rack (front).
    {
        juce::DragAndDropTarget::SourceDetails drop ("add:ORIGAMI", &editor->deviceList(), { 10, rack.getHeight() - 5 });
        rack.itemDropped (drop);
        pump();
        editor->viewport().setViewPosition (0, 0);
        snapshot (*editor, out.getChildFile ("window_front_1200x672.png"));
        editor->setSize (JidaiEditor::kMinWidth, JidaiEditor::kMinHeight);
        pump();
        snapshot (*editor, out.getChildFile ("window_front_960x540.png"));
        expect (editor->viewport().getWidth() > 0 && rack.getWidth() > 0, "the window still lays out at 960 x 540");

        // Large renders: the rack at about 1:1 design scale (1740 wide), front and back.
        editor->setSize (1964, 1100);
        pump();
        rack.setCableMode (JidaiProcessor::CablesAll);
        rack.setCableMode (JidaiProcessor::CablesHidePassThru);
        snapshot (rack, out.getChildFile ("rack_front_large.png"));
        rack.setShowBack (true);
        pump();
        snapshot (rack, out.getChildFile ("rack_back_large.png"));
        rack.setShowBack (false);
    }

    // Starter racks: the RACKS menu, then every rack loaded from it, front and back. On the back (CABLES ALL)
    // every cable of the rack is drawn as a rope between two rear jacks: the back is the whole patch.
    {
        const auto menu = editor->starterRackMenu();
        juce::StringArray tops;
        int items = 0;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            tops.add (it.getItem().text);
            if (auto* sub = it.getItem().subMenu.get())
                for (juce::PopupMenu::MenuItemIterator si (*sub); si.next();) ++items;
            else
                ++items;
        }
        expect (tops == juce::StringArray ({ "INIT", "ACID", "EDM", "FX" }) && items == proc.getNumPrograms(),
                "RACKS menu: INIT, then ACID, EDM and FX holding every starter rack (" + tops.joinIntoString (", ") + ")");
        expect (! editor->headerButtonBounds (JidaiEditor::BtnRacks).isEmpty(), "the header has a RACKS button");
        editor->setSize (1964, 1100);
        pump();
        const auto& racks = starterRacks();
        for (int i = 0; i < (int) racks.size(); ++i)
        {
            proc.setCurrentProgram (i);
            pump (60);
            const auto slug = racks[(size_t) i].name.toLowerCase().replaceCharacter (' ', '_').retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789_");
            const auto stem = juce::String ("starter_") + juce::String (i).paddedLeft ('0', 2) + "_" + slug;
            rack.setShowBack (true);
            rack.setCableMode (JidaiProcessor::CablesAll);
            pump (60);
            int ropes = 0, onRear = 0;
            for (auto& d : layer.drawn())
                ropes += d.shown == RackCableLayer::Shown::Rope ? 1 : 0;
            for (auto& c : proc.rack().cables())
                onRear += rack.spotFor (c.a) != nullptr && rack.spotFor (c.b) != nullptr ? 1 : 0;
            const int n = (int) proc.rack().cables().size();
            expect (proc.getCurrentProgram() == i && ropes == n && onRear == n,
                    "starter rack " + racks[(size_t) i].name + ": back view draws all " + juce::String (n) + " cables as ropes between rear jacks (ropes "
                    + juce::String (ropes) + ", both ends on the back " + juce::String (onRear) + ")");
            snapshot (rack, out.getChildFile (stem + "_back.png"));
            {
                // Where each cable's jacks sit in that PNG (rack pixels), for the cookbook's numbered diagrams.
                juce::DynamicObject::Ptr jacks = new juce::DynamicObject();
                for (auto& c : proc.rack().cables())
                    for (const auto& id : { c.a, c.b })
                        if (const auto* s = rack.spotFor (id))
                            jacks->setProperty (juce::Identifier (juce::String (id)), juce::Array<juce::var> { s->p.x, s->p.y, s->r });
                out.getChildFile (stem + "_back_jacks.json").replaceWithText (juce::JSON::toString (juce::var (jacks.get())));
            }
            rack.setShowBack (false);
            rack.setCableMode (JidaiProcessor::CablesHidePassThru);
            pump (60);
            snapshot (rack, out.getChildFile (stem + "_front.png"));
        }
        proc.setCurrentProgram (0);
        pump();
        expect (proc.rack().deviceCount() == 1 && proc.rack().device (0)->kind() == jidai::DeviceKind::RackIO, "INIT starter rack: RACK I/O only");
    }

    // SHOGUN: from its browser card (auto-routed MIX -> MAIN OUT), the MAIN face open (5.8 U), CLOSED (1 U), and its
    // 151 jacks (SHOGUN's port table, = its ROUTE bay) on a 4 U rear bay, every jack placed, none overlapping.
    {
        proc.setCurrentProgram (0);
        pump();
        auto& b = editor->browser();
        click (b, b.rowBounds (2).toFloat().getCentre());
        pump (30);
        auto* sg = proc.rack().deviceCount() == 2 ? dynamic_cast<jidai::ShogunDevice*> (proc.rack().device (1)) : nullptr;
        expect (sg != nullptr, "SHOGUN card adds SHOGUN#1");
        if (sg != nullptr)
        {
            int routed = 0;
            for (auto& c : proc.rack().cables())
                routed += c.autoRouted && c.a.rfind ("SHOGUN#1/MIX:", 0) == 0 ? 1 : 0;
            expect (routed == 2, "SHOGUN auto-routes MIX L/R to MAIN OUT");
            expect (rack.slotBounds (1).getHeight() == juce::roundToInt (5.8f * RackView::kUnit * rack.scale()), "SHOGUN open front is 5.8 U");
            auto* face = dynamic_cast<ShogunFace*> (rack.faceComponent (1));
            expect (face != nullptr && face->paramKnobCount() >= 50,
                    "SHOGUN MAIN face from SHOGUN's own op table: " + juce::String (face != nullptr ? face->paramKnobCount() : 0) + " parameter controls");
            if (face != nullptr)
            {
                // Nothing on the face is inert: every key SHOGUN's MAIN page draws is bound (SRC, the KIT/PATTERN
                // arrows and search, A/B, undo/redo); the other page tabs are not drawn.
                expect (face->inertControlCount() == 0, "SHOGUN face: no inert knobs, keys or toggles ("
                                                            + juce::String (face->inertControlCount()) + ")");
                const int src = shogun::findParam ("CLOCK:SOURCE");
                const auto srcAt = [&] { return shogun::stepIndex (sg->param (src), 3); };
                expect (srcAt() == shogun::SRC_HOST, "a new rack SHOGUN starts on SRC HOST");
                clickAt (*face, face->sourceKeyBounds().getCentre());
                pump (5);
                const int afterOne = srcAt();
                clickAt (*face, face->sourceKeyBounds().getCentre());
                pump (5);
                const int afterTwo = srcAt();
                clickAt (*face, face->sourceKeyBounds().getCentre());
                pump (5);
                expect (afterOne == shogun::SRC_INT && afterTwo == shogun::SRC_EXT && srcAt() == shogun::SRC_HOST,
                        "SRC key cycles HOST -> INT -> EXT -> HOST");
                clickAt (*face, face->programArrowBounds (1).getCentre());
                pump (5);
                const int next = sg->program();
                clickAt (*face, face->programArrowBounds (0).getCentre());
                pump (5);
                expect (next == 1 && sg->program() == 0, "KIT arrows step the factory list (next "
                                                              + juce::String (next) + ", back " + juce::String (sg->program()) + ")");
                // PATTERN arrows step the same list.
                clickAt (*face, face->programArrowBounds (3).getCentre());
                pump (5);
                const int pNext = sg->program();
                clickAt (*face, face->programArrowBounds (2).getCentre());
                pump (5);
                expect (pNext == 1 && sg->program() == 0, "PATTERN arrows step the factory list");

                // Undo / redo over knob moves, step edits and program loads (the SHOGUN plugin's laws), kept with the
                // device. The two program loads above are on the stack.
                const int decay = shogun::findParam ("BD1:DECAY");
                auto& hist = sg->history();
                const int depth0 = (int) hist.undo.size();
                jidai::shogunstate::beginUndoStep (*sg);
                sg->setParam (decay, 0.9);
                jidai::shogunstate::settleUndoStep (*sg);
                const double setTo = sg->param (decay);
                clickAt (*face, face->keyBounds ("undo").getCentre());
                pump (5);
                const double undone = sg->param (decay);
                clickAt (*face, face->keyBounds ("redo").getCentre());
                pump (5);
                expect (depth0 >= 2 && std::abs (setTo - (double) (float) 0.9) < 1e-12 && std::abs (undone - setTo) > 0.01
                            && std::abs (sg->param (decay) - setTo) < 1e-12,
                        "undo / redo: BD1 DECAY " + juce::String (setTo, 3) + " -> undo " + juce::String (undone, 3) + " -> redo "
                            + juce::String (sg->param (decay), 3) + " (" + juce::String (depth0) + " program loads on the stack)");
                // A no-op click keeps redo.
                clickAt (*face, face->keyBounds ("undo").getCentre());
                pump (5);
                const auto redoBefore = hist.redo.size();
                jidai::shogunstate::beginUndoStep (*sg);
                jidai::shogunstate::settleUndoStep (*sg);
                expect (hist.redo.size() == redoBefore && redoBefore > 0, "an edit that changes nothing keeps redo");
                clickAt (*face, face->keyBounds ("redo").getCentre());
                pump (5);

                // A/B: B starts as a copy of A; edits on B; A recalls exactly; right-click A copies B onto A.
                const double aVal = sg->param (decay);
                clickAt (*face, face->keyBounds ("ab:1").getCentre());
                pump (5);
                const bool bCopy = std::abs (sg->param (decay) - aVal) < 1e-12 && hist.abSlot == 1;
                sg->setParam (decay, 0.2);
                const double bVal = sg->param (decay);
                clickAt (*face, face->keyBounds ("ab:0").getCentre());
                pump (5);
                const bool aBack = std::abs (sg->param (decay) - aVal) < 1e-12 && hist.abSlot == 0;
                clickAt (*face, face->keyBounds ("ab:1").getCentre());
                pump (5);
                const bool bBack = std::abs (sg->param (decay) - bVal) < 1e-12;
                clickAt (*face, face->keyBounds ("ab:0").getCentre());
                pump (5);
                clickAt (*face, face->keyBounds ("ab:0").getCentre(), true);     // right-click A: copy B onto A
                pump (5);
                expect (bCopy && aBack && bBack && std::abs (sg->param (decay) - bVal) < 1e-12,
                        "A/B: B starts as a copy, A and B recall exactly, right-click A copies B onto A");
                clickAt (*face, face->keyBounds ("undo").getCentre());
                pump (5);
                expect (std::abs (sg->param (decay) - aVal) < 1e-12, "the A/B copy is undoable");
                face->setSelectedVoice (shogun::SD);
            }
            editor->setSize (1964, 1100);
            pump (60);
            snapshot (rack, out.getChildFile ("shogun_front_open.png"));
            rack.setClosed (sg, true);
            pump (30);
            expect (rack.slotBounds (1).getHeight() == juce::roundToInt (RackView::kUnit * rack.scale()), "SHOGUN CLOSED is 1 U");
            snapshot (rack, out.getChildFile ("shogun_front_closed.png"));
            rack.setClosed (sg, false);
            rack.setShowBack (true);
            rack.setCableMode (JidaiProcessor::CablesAll);
            pump (60);
            auto* rear = dynamic_cast<RearPanel*> (rack.faceComponent (1));
            int inside = 0, overlaps = 0;
            if (rear != nullptr)
            {
                const auto area = rear->getLocalBounds().toFloat();
                for (int j = 0; j < rear->jackCount(); ++j)
                {
                    const auto c = rear->jackCentre (j);
                    inside += area.contains (c) && c != juce::Point<float>() ? 1 : 0;
                    for (int k = j + 1; k < rear->jackCount(); ++k)
                        overlaps += c.getDistanceFrom (rear->jackCentre (k)) < 2.0f * rear->jackRadius() ? 1 : 0;
                }
            }
            expect (rear != nullptr && rear->jackCount() == 151 && inside == 151 && overlaps == 0,
                    "SHOGUN rear bay: 151 jacks placed on the plate (" + juce::String (inside) + "), " + juce::String (overlaps) + " overlapping");
            expect (rack.slotBounds (1).getHeight() == juce::roundToInt (4.0f * RackView::kUnit * rack.scale()), "SHOGUN back is 4 U");
            int spots = 0;
            for (int j = 0; j < shogun::kPorts; ++j)
                spots += rack.spotFor ("SHOGUN#1/" + std::string (shogun::kPortTable[j].id)) != nullptr ? 1 : 0;
            expect (spots == 151, "every SHOGUN jack is a cable spot on the back, and only those (" + juce::String (spots) + ")");
            snapshot (rack, out.getChildFile ("shogun_back.png"));
            rack.setShowBack (false);
            pump();
        }
    }

    // Block sizes: a rack plays the same whatever blocks the host cuts the audio into, larger than prepareToPlay's
    // (processBlock hands the rack at most that much at once) or 1 sample.
    {
        int same = 0, sounding = 0, close = 0;
        double worst = 0.0;
        const int racks = proc.getNumPrograms();
        for (int prog = 0; prog < racks; ++prog)
        {
            const int total = 48000;
            const auto ref = render (prog, total, [] { return 256; });
            const auto big = render (prog, total, [] { return 512; });
            juce::Random r ((juce::int64) prog);
            const auto mixed = render (prog, total, [&r] { const int k = r.nextInt (6); return k == 0 ? 1 : k == 1 ? 512 : 1 + r.nextInt (512); });
            // A 512 block is two 256 blocks to the rack: identical. Blocks of any size are not bit-identical (devices
            // read the host transport and some controls once per block), but must stay finite and close.
            same += ref == big ? 1 : 0;
            double sum = 0.0, maxDiff = 0.0, peak = 0.0;
            bool finiteOut = true;
            for (size_t i = 0; i < ref.size(); ++i)
            {
                sum += (double) ref[i] * (double) ref[i];
                maxDiff = std::max (maxDiff, (double) std::abs (ref[i] - mixed[i]));
                peak = std::max (peak, (double) std::abs (ref[i]));
                finiteOut = finiteOut && std::isfinite (mixed[i]) && std::isfinite (big[i]);
            }
            sounding += sum > 1e-6 ? 1 : 0;
            close += finiteOut && maxDiff <= 0.05 * std::max (peak, 1e-3) ? 1 : 0;
            worst = std::max (worst, peak > 0.0 ? maxDiff / peak : maxDiff);
            if (ref != big)
            {
                size_t at = 0;
                while (at < ref.size() && jidai::exactlyEqual (ref[at], big[at]))
                    ++at;
                std::printf ("  rack %d: 512-sample blocks change the output from sample %d\n", prog + 1, (int) (at / 2));
            }
        }
        expect (same == racks && sounding > 0, "every starter rack plays sample-identical in blocks of 256 and of 512, twice the prepared size ("
                                                  + juce::String (same) + " of " + juce::String (racks) + ", " + juce::String (sounding) + " sounding)");
        expect (close == racks, "in blocks of 1 to 512 samples every starter rack stays finite and within 5% of peak of the 256-block render (worst "
                                   + juce::String (worst * 100.0, 2) + "%)");
    }

    // A host changing programs and loading states from other threads while the editor is open and the audio runs (a
    // VST3 host's program parameter; pluginval's editor automation). The rack changes on the message thread only, the
    // view never paints a device the rack dropped, the dropped devices are freed, and a state saved meanwhile is always
    // a whole rack: the one live or the one requested.
    {
        rack.setShowBack (false);
        editor->setSize (1200, 672);
        pump();
        const int programs = proc.getNumPrograms();
        std::vector<juce::MemoryBlock> states;           // states[i] = program i, saved on the message thread
        for (int i = 0; i < programs; ++i)
        {
            proc.setCurrentProgram (i);
            states.emplace_back();
            proc.getStateInformation (states.back());
        }
        bool repeatable = true;
        for (int i = 0; i < programs; ++i)
        {
            proc.setStateInformation (states[(size_t) i].getData(), (int) states[(size_t) i].getSize());
            juce::MemoryBlock again;
            proc.getStateInformation (again);
            repeatable = repeatable && again == states[(size_t) i];
        }
        expect (programs >= 2 && repeatable, "each starter rack saves the same state after it is loaded back (" + juce::String (programs) + " racks)");
        const auto stateIndex = [&states] (const juce::MemoryBlock& m)
        {
            for (size_t i = 0; i < states.size(); ++i)
                if (states[i] == m)
                    return (int) i;
            return -1;
        };

        const int rounds = juce::SystemStats::getEnvironmentVariable ("JIDAI_STRESS_ROUNDS", "").getIntValue() > 0
                               ? juce::SystemStats::getEnvironmentVariable ("JIDAI_STRESS_ROUNDS", "").getIntValue() : 3000;
        std::atomic<bool> hostDone { false }, stopAudio { false };
        std::atomic<int> badSaves { 0 }, wrongProgram { 0 }, blocks { 0 }, expected { -1 }, maxBlock { 0 }, oneSample { 0 };
        std::atomic<bool> finite { true };
        std::vector<std::pair<juce::MemoryBlock, int>> stagedSaves;     // saved while a program change was staged
        std::atomic<int> exactSaves { 0 };
        std::thread audio ([&]
        {
            // As some hosts do: any block size from 1 sample to twice prepareToPlay's 256, MIDI in most blocks.
            const int channels = juce::jmax (2, proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels());
            juce::AudioBuffer<float> buffer (channels, 512);
            juce::MidiBuffer midi;
            midi.ensureSize (4096);
            juce::Random r (48000);
            while (! stopAudio.load())
            {
                const int pick = r.nextInt (8);
                const int n = pick == 0 ? 1 : pick == 1 ? 512 : pick == 2 ? 257 + r.nextInt (256) : 1 + r.nextInt (512);
                buffer.setSize (channels, n, false, false, true);
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < n; ++i)
                        buffer.setSample (ch, i, r.nextFloat() * 0.2f - 0.1f);
                midi.clear();
                for (int k = r.nextInt (4); k > 0; --k)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 36 + r.nextInt (48), (juce::uint8) (1 + r.nextInt (127))), r.nextInt (n));
                if (r.nextBool())
                    midi.addEvent (juce::MidiMessage::allNotesOff (1), n - 1);
                proc.processBlock (buffer, midi);
                ++blocks;
                maxBlock = juce::jmax (maxBlock.load(), n);
                oneSample += n == 1 ? 1 : 0;
                for (int i = 0; i < n; ++i)
                    finite = finite && std::isfinite (buffer.getSample (0, i)) && std::isfinite (buffer.getSample (1, i));
                std::this_thread::yield();
            }
        });
        std::thread host ([&]
        {
            juce::Random r (2026);
            for (int i = 0; i < rounds; ++i)
            {
                const int k = r.nextInt (programs);
                const int what = r.nextInt (10);
                if (what < 4)
                {
                    proc.setCurrentProgram (k);
                    expected = k;
                    wrongProgram += proc.getCurrentProgram() == k ? 0 : 1;   // reported at once, applied later
                }
                else if (what < 7)
                {
                    proc.setStateInformation (states[(size_t) k].getData(), (int) states[(size_t) k].getSize());
                    expected = k;
                }
                else
                {
                    juce::MemoryBlock saved;
                    proc.getStateInformation (saved);
                    const int e = expected.load();
                    // Only this thread makes requests, so the save is the requested rack (or, before any request, a
                    // starter rack). While a program change is staged it is that starter rack's own text (checked
                    // below by loading it).
                    const int got = stateIndex (saved);
                    if (e < 0 ? got >= 0 : got == e)
                        ++exactSaves;
                    else if (got < 0 && e >= 0)
                        stagedSaves.emplace_back (saved, e);
                    else
                    {
                        if (badSaves.load() < 4)
                        {
                            out.getChildFile ("bad-save-" + juce::String (badSaves.load()) + "-got" + juce::String (got) + "-want" + juce::String (e) + ".xml")
                                .replaceWithText (juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize())->toString());
                        }
                        ++badSaves;
                    }
                }
                if (r.nextInt (3) == 0)       // sometimes in bursts, sometimes in step with the message thread's paints
                    std::this_thread::sleep_for (std::chrono::microseconds (r.nextInt (3000)));
            }
            hostDone = true;
        });

        int mismatches = 0, unfreed = 0, paints = 0, rebuildsSeen = 0;
        const auto viewMatchesRack = [&]
        {
            if (rack.slotCount() != proc.rack().deviceCount())
                return false;
            for (int i = 0; i < rack.slotCount(); ++i)
                if (rack.slotDevice (i) != proc.rack().device (i))
                    return false;
            return true;
        };
        const auto* firstSeen = proc.rack().deviceCount() > 0 ? proc.rack().device (0) : nullptr;
        for (int n = 0; ! hostDone.load() || proc.hasPendingState(); ++n)
        {
            pump (1);
            mismatches += viewMatchesRack() ? 0 : 1;
            unfreed += proc.rack().retiredCount() == 0 ? 0 : 1;
            if (proc.rack().deviceCount() > 0 && proc.rack().device (0) != firstSeen)
            {
                firstSeen = proc.rack().device (0);
                ++rebuildsSeen;
            }
            if (n % 2 == 0)
            {
                editor->repaint();
                (void) editor->createComponentSnapshot (editor->getLocalBounds(), true, 0.5f);     // paints every Mount and face
                ++paints;
            }
        }
        host.join();
        pump (20);
        stopAudio = true;
        audio.join();

        juce::MemoryBlock final;
        proc.getStateInformation (final);
        const int last = expected.load();
        std::printf ("background host: %d requests, %d message-loop paints, %d rack swaps seen, %d audio blocks (largest %d, %d of 1 sample)\n",
                     rounds, paints, rebuildsSeen, blocks.load(), maxBlock.load(), oneSample.load());
        int stagedWrong = 0;
        for (auto& [saved, e] : stagedSaves)
        {
            proc.setStateInformation (saved.getData(), (int) saved.getSize());
            juce::MemoryBlock loaded;
            proc.getStateInformation (loaded);
            if (loaded != states[(size_t) e] && stagedWrong < 4)
            {
                out.getChildFile ("staged-save-" + juce::String (stagedWrong) + "-want" + juce::String (e) + ".xml")
                    .replaceWithText (juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize())->toString());
                out.getChildFile ("staged-load-" + juce::String (stagedWrong) + ".xml")
                    .replaceWithText (juce::AudioProcessor::getXmlFromBinary (loaded.getData(), (int) loaded.getSize())->toString());
            }
            stagedWrong += loaded == states[(size_t) e] ? 0 : 1;
        }
        proc.setStateInformation (final.getData(), (int) final.getSize());
        pump();
        std::printf ("host-thread saves: %d exact, %d during a staged program change\n", exactSaves.load(), (int) stagedSaves.size());
        expect (badSaves.load() == 0 && stagedWrong == 0 && exactSaves.load() > 0,
                "a state saved on the host thread is always the requested rack (" + juce::String (badSaves.load() + stagedWrong) + " wrong)");
        expect (wrongProgram.load() == 0, "getCurrentProgram reports a program change at once");
        expect (mismatches == 0, "the rack view always shows the rack's own devices (" + juce::String (mismatches) + " mismatches)");
        expect (unfreed == 0 && proc.rack().retiredCount() == 0, "dropped devices are freed once the view rebuilt");
        expect (! proc.hasPendingState() && last >= 0 && final == states[(size_t) last], "after the host stops, the rack is the last one it asked for");
        expect (blocks.load() > 0 && maxBlock.load() == 512 && oneSample.load() > 0 && finite.load(),
                "the audio thread ran throughout, blocks of 1 to 512 samples (prepared for 256), output finite");
        snapshot (*editor, out.getChildFile ("window_after_host_thread.png"));
        expect (viewMatchesRack(), "the window shows the final rack");
    }

    editor.reset();
    std::printf ("%d checks, %d failed\n%s\n", checks, failures, failures == 0 ? "RACK UI PROBE PASS" : "RACK UI PROBE FAIL");
    return failures == 0 ? 0 : 1;
}

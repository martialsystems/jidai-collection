// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
//
// Rack window probe (no host, no audio device, no window): builds the JIDAI RACK editor offscreen and drives it the
// way a user would, with mouse events on the real components and the editor's keys. It checks the processor after
// each step and writes PNGs of the rack (front, back, ORIGAMI OPEN and CLOSED in the rack, the 1200 x 672 window).
//   JidaiRackProbe <out-dir>      (registered with ctest as rack_ui)

#include "plugin/JidaiEditor.h"
#include "plugin/RackCableLayer.h"
#include "plugin/StarterRacks.h"

#include <juce_gui_basics/juce_gui_basics.h>
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

void click (juce::Component& c, juce::Point<float> p)
{
    c.mouseDown (mouse (c, p, p, false));
    c.mouseUp (mouse (c, p, p, false));
    pump();
}

// Click whatever child of `root` is under p (root coordinates), as the real mouse would.
void clickAt (juce::Component& root, juce::Point<int> p)
{
    if (auto* c = root.getComponentAt (p))
        click (*c, c->getLocalPoint (&root, p).toFloat());
}

void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to, bool release = true)
{
    c.mouseDown (mouse (c, from, from, false));
    for (int i = 1; i <= 10; ++i)
        c.mouseDrag (mouse (c, from + (to - from) * ((float) i / 10.0f), from, true));
    if (release)
        c.mouseUp (mouse (c, to, from, true));
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
        expect (b.rowCount() == 5, "browser: BUSHIDO, RONIN, RONIN FX, ORIGAMI, RACK I/O");
        expect (juce::String (b.row (3).group) == "EFFECT" && b.row (3).kind == jidai::DeviceKind::Origami
                && juce::String (b.row (4).group) == "UTILITY" && b.row (4).kind == jidai::DeviceKind::RackIO, "ORIGAMI under EFFECT, RACK I/O under UTILITY");
        expect (! b.available (b.row (4)), "RACK I/O card is used up (one per rack)");
        b.setSearch ("ori");
        expect (b.rowCount() == 1 && b.row (0).kind == jidai::DeviceKind::Origami, "search 'ori' shows ORIGAMI");
        b.setSearch ("");
        click (b, b.rowBounds (4).toFloat().getCentre());
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
            rack.setShowBack (false);
            rack.setCableMode (JidaiProcessor::CablesHidePassThru);
            pump (60);
            snapshot (rack, out.getChildFile (stem + "_front.png"));
        }
        proc.setCurrentProgram (0);
        pump();
        expect (proc.rack().deviceCount() == 1 && proc.rack().device (0)->kind() == jidai::DeviceKind::RackIO, "INIT starter rack: RACK I/O only");
    }

    editor.reset();
    std::printf ("%d checks, %d failed\n%s\n", checks, failures, failures == 0 ? "RACK UI PROBE PASS" : "RACK UI PROBE FAIL");
    return failures == 0 ? 0 : 1;
}

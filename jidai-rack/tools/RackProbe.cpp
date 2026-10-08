// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
//
// Window probe (no host, no audio device): builds the JIDAI RACK editor offscreen, then drives it the way a user
// would, with mouse events on the real components: drops a RONIN onto the rack, patches BUSHIDO CV A to a RONIN
// HZ/V by dragging a cable, removes a device with its x, and checks the processor after each step.
// Writes PNGs of the window. The window goes on screen only for the wheel step (wheel events need a peer); on Linux
// run under xvfb-run:  xvfb-run -a JidaiRackProbe <out-dir>

#define JUCE_GUI_BASICS_INCLUDE_XHEADERS 1      // the wheel step needs a window; see main()
#include "plugin/JidaiEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace {

int failures = 0;

void expect (bool ok, const juce::String& what)
{
    std::printf ("%s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (! ok)
        ++failures;
}

void pump (int ms = 80)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

juce::MouseEvent mouse (juce::Component& c, juce::Point<float> p, juce::Point<float> down, bool dragged)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent (source, p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             &c, &c, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
}

void click (juce::Component& c, juce::Point<float> p)
{
    c.mouseDown (mouse (c, p, p, false));
    c.mouseUp (mouse (c, p, p, false));
}

void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to)
{
    c.mouseDown (mouse (c, from, from, false));
    for (int i = 1; i <= 10; ++i)
        c.mouseDrag (mouse (c, from + (to - from) * ((float) i / 10.0f), from, true));
    c.mouseUp (mouse (c, to, from, true));
}

void snapshot (juce::Component& c, const juce::File& file, juce::Rectangle<int> area = {}, float scale = 1.0f)
{
    const auto image = c.createComponentSnapshot (area.isEmpty() ? c.getLocalBounds() : area, true, scale);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (image, out);
    std::printf ("wrote %s (%d x %d)\n", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
}

bool hasCable (JidaiProcessor& p, const std::string& a, const std::string& b)
{
    for (auto& c : p.rack().cables())
        if ((c.a == a && c.b == b) || (c.a == b && c.b == a))
            return true;
    return false;
}

}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
   #if JUCE_LINUX
    // A console app gets Xlib's default error handler, which exits on the first error. A bare X server (xvfb, no window
    // manager) rejects a few window-manager properties JUCE sets on a new window, so ignore X errors as a JUCE app does.
    juce::XWindowSystem::getInstance();                                       // loads the Xlib symbols
    juce::X11Symbols::getInstance()->xSetErrorHandler ([] (::Display*, ::XErrorEvent*) { return 0; });
   #endif
    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "probe");
    out.createDirectory();

    JidaiProcessor proc;
    proc.prepareToPlay (48000.0, 256);
    std::unique_ptr<JidaiEditor> editor (static_cast<JidaiEditor*> (proc.createEditor()));
    editor->setSize (1320, 860);
    editor->setVisible (true);
    pump();

    auto& rack = editor->rackView();
    expect (proc.rack().deviceCount() == 2, "fresh instance: two devices");
    expect (proc.rack().device (0)->kind() == jidai::DeviceKind::Bushido && proc.rack().device (1)->kind() == jidai::DeviceKind::Ronin,
            "fresh instance: BUSHIDO above RONIN");
    expect (rack.slotCount() == 2, "window shows two panels");
    snapshot (*editor, out.getChildFile ("1_default_rack.png"));

    // The device browser: groups, counts, search.
    {
        auto& b = editor->browser();
        expect (b.rowCount() == 3, "browser lists BUSHIDO, RONIN and RONIN");
        expect (juce::String (b.row (0).group) == "SEQUENCER" && b.row (0).kind == jidai::DeviceKind::Bushido
                && juce::String (b.row (1).group) == "VOICE" && b.row (1).kind == jidai::DeviceKind::Ronin
                && juce::String (b.row (2).group) == "EFFECT" && b.row (2).kind == jidai::DeviceKind::Ronin,
                "groups: BUSHIDO in Sequencer, RONIN in Voice and Effect");
        expect (b.countOnRack (jidai::DeviceKind::Bushido) == 1 && b.countOnRack (jidai::DeviceKind::Ronin) == 1, "counts: one of each on the rack");
        b.searchBox().setText ("bush", true);      // as if typed: the box tells the browser
        pump();
        expect (b.rowCount() == 1 && b.row (0).kind == jidai::DeviceKind::Bushido, "search 'bush' shows only BUSHIDO");
        b.setSearch ("RON");
        expect (b.rowCount() == 2, "search 'RON' shows RONIN under Voice and Effect");
        b.setSearch ("drum");
        expect (b.rowCount() == 0, "search for a device that is not here shows nothing");
        snapshot (*editor, out.getChildFile ("0_search_no_match.png"));
        b.setSearch ("");
        expect (b.rowCount() == 3, "clearing the search shows every row");

        // A click on a row adds one at the bottom (the way in when a drop misses); then take it out again.
        const auto r = b.rowBounds (2).toFloat().getCentre();
        click (b, r);
        pump();
        expect (proc.rack().deviceCount() == 3 && proc.rack().device (2)->rackId() == "RONIN#2", "clicking RONIN (Effect) adds RONIN 2 at the bottom");
        expect (b.countOnRack (jidai::DeviceKind::Ronin) == 2, "the RONIN count reads 2");
        rack.removeDevice (proc.rack().device (2));
        pump();
        expect (proc.rack().deviceCount() == 2, "back to one of each");
    }

    // The browser's close/open button: closed, it is a thin strip and the rack takes the width; a click on the strip opens
    // it again; the state is saved with the rack.
    {
        auto& b = editor->browser();
        const int rackWidth = editor->viewport().getWidth();
        click (b, b.toggleBounds().toFloat().getCentre());
        pump();
        expect (! proc.browserOpen && b.isClosed() && b.getWidth() == DeviceBrowser::kClosedWidth, "close button folds the browser to a strip");
        expect (editor->viewport().getWidth() > rackWidth, "the rack takes the width");
        snapshot (*editor, out.getChildFile ("0b_browser_closed.png"));
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        proc.browserOpen = true;
        proc.setStateInformation (state.getData(), (int) state.getSize());
        pump();
        expect (! proc.browserOpen && b.isClosed() && b.getWidth() == DeviceBrowser::kClosedWidth, "a closed browser is saved with the rack");
        click (b, { 15.0f, 300.0f });
        pump();
        expect (proc.browserOpen && ! b.isClosed() && b.getWidth() == JidaiEditor::kListWidth && editor->viewport().getWidth() == rackWidth,
                "a click on the strip opens it again");
    }

    // Drop a RONIN from the device list onto the bottom of the rack.
    juce::DragAndDropTarget::SourceDetails drop ("add:RONIN", &editor->deviceList(), { 10, rack.getHeight() - 5 });
    rack.itemDropped (drop);
    pump();
    expect (proc.rack().deviceCount() == 3 && proc.rack().device (2)->rackId() == "RONIN#2", "drop RONIN at the bottom: RONIN 2 added");
    expect (rack.slotCount() == 3, "window shows three panels");

    // Drop a BUSHIDO at the top.
    juce::DragAndDropTarget::SourceDetails dropTop ("add:BUSHIDO", &editor->deviceList(), { 10, 2 });
    rack.itemDropped (dropTop);
    pump();
    expect (proc.rack().device (0)->rackId() == "BUSHIDO#2", "drop BUSHIDO at the top: BUSHIDO 2 is first");

    // Patch BUSHIDO 1 CV A to RONIN 2 HZ/V by dragging a cable between the two jacks on the cable layer.
    auto* layer = rack.cableLayer();
    const int a = layer->jackIndex ("BUSHIDO#1/OUTPUTS:CV A");
    const int b = layer->jackIndex ("RONIN#2/VCO:HZ/V");
    expect (a >= 0 && b >= 0, "both jacks are on the one cable layer");
    const float s = layer->getWidth() / RackView::kPanelWidth;
    drag (*layer, layer->jackPosDesign (a) * s, layer->jackPosDesign (b) * s);
    pump();
    expect (hasCable (proc, "BUSHIDO#1/OUTPUTS:CV A", "RONIN#2/VCO:HZ/V"), "dragged cable: BUSHIDO 1 CV A -> RONIN 2 HZ/V is in the rack");

    // And back: RONIN 1 MG TRI into BUSHIDO 1 CLOCK.
    const int mg = layer->jackIndex ("RONIN#1/MG:TRI");
    const int clk = layer->jackIndex ("BUSHIDO#1/CLOCK:CLOCK");
    drag (*layer, layer->jackPosDesign (mg) * s, layer->jackPosDesign (clk) * s);
    pump();
    expect (hasCable (proc, "RONIN#1/MG:TRI", "BUSHIDO#1/CLOCK:CLOCK"), "dragged cable: RONIN 1 MG TRI -> BUSHIDO 1 CLOCK is in the rack");

    // Run the audio side once so the needle and lamps have something to show, then picture it.
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    for (int i = 0; i < 200; ++i)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);
    }
    editor->viewport().setViewPosition (0, 0);
    snapshot (*editor, out.getChildFile ("2_four_devices_patched.png"));

    // Turn a RONIN knob through its panel: drag VCF CUTOFF up.
    {
        auto* ronin = dynamic_cast<jidai::RoninDevice*> (proc.rack().findDevice ("RONIN#1"));
        const int cutoff = panelKnobIndex ("VCF", "CUTOFF");
        const float before = ronin->knob (cutoff);
        for (int i = 0; i < rack.getNumChildComponents(); ++i)
            if (auto* rp = dynamic_cast<RoninPanel*> (rack.getChildComponent (i)); rp != nullptr && &rp->device() == ronin)
            {
                const float ps = (float) rp->getWidth() / RoninPanel::kWidth;
                const auto k = juce::Point<float> (kPanelKnobs[cutoff].cx * ps, kPanelKnobs[cutoff].cy * ps);
                drag (*rp, k, k - juce::Point<float> (0, 40));
            }
        expect (ronin->knob (cutoff) > before, "RONIN panel: dragging VCF CUTOFF up raises it");
    }

    // The wheel, through JUCE's real dispatch (a peer, so mouse listeners hear it too): over an open preset list it
    // moves the list and leaves the rack where it is; over a panel's bare metal it still scrolls the rack.
    {
        editor->addToDesktop (0);
        editor->setVisible (true);
        pump();
        auto* peer = editor->getPeer();
        expect (peer != nullptr, "the probe window has a peer for wheel events");
        PatternScreen* screen = nullptr;
        const int ronin1 = rack.indexOfSlotFor (*proc.rack().findDevice ("RONIN#1"));
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (auto* ps = dynamic_cast<PatternScreen*> (child); ps != nullptr && ! ps->isOpen() && rack.slotBounds (ronin1).intersects (rack.getLocalArea (&c, ps->getBoundsInParent())))
                    screen = ps;
                find (*child);
            }
        };
        find (rack);
        juce::int64 t = juce::Time::currentTimeMillis();
        auto wheelAt = [&] (juce::Point<int> inRack)
        {
            const auto p = editor->getLocalPoint (&rack, inRack).toFloat();
            for (int i = 0; i < 6; ++i)
                peer->handleMouseWheel (juce::MouseInputSource::InputSourceType::mouse, p, t += 50, { 0.0f, -0.25f, false, false, false });
            pump (20);
        };
        if (peer != nullptr && screen != nullptr)
        {
            editor->viewport().setViewPosition (0, 0);
            const auto closed = rack.getLocalArea (screen->getParentComponent(), screen->getBoundsInParent());
            click (*screen, { 8.0f, (float) screen->getHeight() / 2 });
            pump();
            expect (screen->isOpen(), "clicking RONIN 1's PRESET screen opens its list");
            const int y0 = editor->viewport().getViewPositionY();
            wheelAt ({ closed.getX() + 40, closed.getBottom() + juce::roundToInt (60 * rack.scale()) });
            expect (editor->viewport().getViewPositionY() == y0, "wheel over the open preset list leaves the rack where it is");
            snapshot (*editor, out.getChildFile ("2a_preset_list_wheel.png"));
            const auto list = editor->getLocalArea (&rack, closed.withHeight (juce::roundToInt (260 * rack.scale())).expanded (8));
            snapshot (*editor, out.getChildFile ("2a_preset_list_2x.png"), list, 2.0f);   // as on a Retina screen
            screen->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            pump();
            const auto metal = rack.slotBounds (ronin1);
            wheelAt ({ metal.getX() + 6, metal.getCentreY() });
            expect (editor->viewport().getViewPositionY() > y0, "wheel over a panel's bare metal still scrolls the rack");
            editor->viewport().setViewPosition (0, 0);
        }
        else
            expect (false, "found RONIN 1's PRESET screen");
        editor->removeFromDesktop();
        pump();
    }

    // Fold RONIN 2 with the arrow on its ear: it becomes a strip, its cables stay in the rack, and opening it shows them again.
    {
        auto* r2 = proc.rack().findDevice ("RONIN#2");
        const int slot = rack.indexOfSlotFor (*r2);
        const int tall = rack.slotBounds (slot).getHeight();
        const auto box = rack.foldButtonBounds (slot).getCentre();
        if (auto* c = rack.getComponentAt (box))
            click (*c, (box - c->getPosition()).toFloat());
        pump();
        expect (r2->folded && rack.slotBounds (rack.indexOfSlotFor (*r2)).getHeight() < tall / 5, "fold arrow folds RONIN 2 to a strip");
        expect (rack.cableLayer()->jackIndex ("RONIN#2/VCO:HZ/V") < 0, "a folded device's jacks are off the cable layer");
        // Patch something else while it is folded: the folded device's cable must survive the layer's commit.
        const int m1 = rack.cableLayer()->jackIndex ("RONIN#1/MG:SAW");
        const int n1 = rack.cableLayer()->jackIndex ("RONIN#1/VCA 2:IN");
        const float ls = rack.cableLayer()->getWidth() / RackView::kPanelWidth;
        drag (*rack.cableLayer(), rack.cableLayer()->jackPosDesign (m1) * ls, rack.cableLayer()->jackPosDesign (n1) * ls);
        pump();
        expect (hasCable (proc, "BUSHIDO#1/OUTPUTS:CV A", "RONIN#2/VCO:HZ/V"), "its cable stays in the rack while folded");
        editor->viewport().setViewPosition (0, juce::jmax (0, rack.slotBounds (rack.indexOfSlotFor (*r2)).getBottom() - editor->viewport().getHeight() + 200));
        snapshot (*editor, out.getChildFile ("2b_folded.png"));
        editor->viewport().setViewPosition (0, 0);
        const auto open = rack.foldButtonBounds (rack.indexOfSlotFor (*r2)).getCentre();
        if (auto* c = rack.getComponentAt (open))
            click (*c, (open - c->getPosition()).toFloat());
        pump();
        expect (! r2->folded && rack.cableLayer()->jackIndex ("RONIN#2/VCO:HZ/V") >= 0, "the arrow opens it again, jacks back on the layer");
    }

    // Remove RONIN 2 with the x on its ear; its cables go with it.
    {
        const int slot = rack.indexOfSlotFor (*proc.rack().findDevice ("RONIN#2"));
        const auto box = rack.removeButtonBounds (slot).getCentre();
        auto* c = rack.getComponentAt (box);
        expect (c != nullptr && c != rack.cableLayer(), "the remove button on the ear is not under the cable layer");
        if (c != nullptr)
            click (*c, (box - c->getPosition()).toFloat());
        pump();
        expect (proc.rack().findDevice ("RONIN#2") == nullptr, "x removes RONIN 2");
        expect (! hasCable (proc, "BUSHIDO#1/OUTPUTS:CV A", "RONIN#2/VCO:HZ/V"), "its cable went with it");
        expect (hasCable (proc, "RONIN#1/MG:TRI", "BUSHIDO#1/CLOCK:CLOCK"), "other cables stay");
    }

    // Drag a device off onto the device list: the list takes it and removes it.
    {
        juce::DragAndDropTarget::SourceDetails off ("move:BUSHIDO#2", &rack, { 10, 10 });
        auto* list = dynamic_cast<juce::DragAndDropTarget*> (&editor->deviceList());
        expect (list != nullptr && list->isInterestedInDragSource (off), "the device list takes a device dragged off the rack");
        list->itemDropped (off);
        pump();
        expect (proc.rack().findDevice ("BUSHIDO#2") == nullptr, "dragged onto the list: BUSHIDO 2 removed");
    }

    // State round trip.
    {
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        JidaiProcessor other;
        other.setStateInformation (state.getData(), (int) state.getSize());
        expect (other.rack().deviceCount() == proc.rack().deviceCount(), "saved rack reloads with the same devices");
        expect (other.rack().cables().size() == proc.rack().cables().size(), "saved rack reloads with the same cables");
    }

    // Empty rack is valid.
    while (proc.rack().deviceCount() > 0)
        rack.removeDevice (proc.rack().device (0));
    pump();
    expect (proc.rack().deviceCount() == 0 && rack.slotCount() == 0, "empty rack");
    buffer.clear();
    proc.processBlock (buffer, midi);
    snapshot (*editor, out.getChildFile ("3_empty_rack.png"));

    editor.reset();
    std::printf (failures == 0 ? "PROBE PASS\n" : "PROBE FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}

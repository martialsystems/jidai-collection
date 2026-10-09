// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// The rack's cables (JIDAI_RACK_Redesign 3.4-3.9), drawn over every device in the current view (FRONT or BACK).
// Ropes hang with sag = 18 + 0.14|dx| + 0.06|dy| design units, a 5-unit core in the source jack's role colour (JCS
// R14) or the cable's override swatch. Modes:
//   ALL             every cable as a rope (on the back, a device's internal cables at 28 %);
//   HIDE PASS-THRU  ropes only between jacks on one device or on adjacent devices; others are short stubs with a tag
//                   naming the far end ("#3 -> RONIN 1 . VCO:HZ/V");
//   SELECTED        ropes on the selected device at full strength, the rest faint;
//   HIDE            a plug in each patched jack, no ropes.
// A cable whose other end is not on show (a back-only jack on the FRONT, a CLOSED or folded device, another tab) is a
// stub with a tag saying where the far end is. Badges: +n comp (R11) on MAIN OUT cables, delta-n skew, the R4.3 / 3.9
// warnings, and a z^-1 mark on feedback cables (R9).
// Editing (the shared gestures of jidai/ui/CableEdit.h, the same on every unit's back panel):
//   drag a plugged end to another jack: reroute (the same cable keeps its colour and age; one undo step);
//   drag from an empty jack: a new cable (jacks that take it ring green, the rest are dimmed; refusals say why);
//   Option-drag (Mac) / Alt-drag from a used jack: stack another cable on it;
//   click a cable: select it and bring it to the front (z, saved with the rack); Delete removes it;
//   palette colour (proc.cablePalette): jacks that cannot take a cable of that colour are dimmed and refuse new cables;
//   drag an end into empty space, or right-click a cable > Remove: remove it;
//   right-click a jack: Connect to..., Bring to front, Disconnect, Cable colour.
// Every edit goes through JidaiProcessor::editCables (undo / redo). The layer only takes the mouse on jacks and
// cables, so the panels under it keep theirs.

#include "RackView.h"

class RackCableLayer : public juce::Component, private juce::Timer
{
public:
    RackCableLayer (RackView&, JidaiProcessor&);
    ~RackCableLayer() override;

    void refresh();                      // the patch, the view or the selection changed

    enum class Shown { None, Rope, Stub, Dot };
    struct Drawn {
        int index = -1;
        Shown shown = Shown::None;
        float alpha = 1.0f;
        bool aVisible = false, bVisible = false;
        juce::Point<float> a, b;         // visible ends (pixels)
        juce::Path path;                 // rope, or both stubs
        juce::Colour colour;
        juce::String tagA, tagB;         // stub tags at end a / end b
    };
    const std::vector<Drawn>& drawn() const { return cablesDrawn; }
    static float sagFor (float dx, float dy, float scale);       // pixels

    // Editing, also called by the menus, the keys and the probe.
    void selectCable (int index);
    int selectedCable() const { return selected; }
    bool deleteSelected();
    bool bringToFront (int index);       // false if it already was in front
    bool undo();
    bool redo();
    bool jackDimmed (const RackView::JackSpot&) const;     // by the palette colour
    bool jackTakesDrag (int spot) const;                   // during a drag: the cable being drawn can go there
    // Right-click on a cable: Remove, Bring to front, Colour. The result acts on that cable if it is still patched.
    enum { CableMenuRemove = 1, CableMenuFront = 2 };
    juce::PopupMenu cableMenu (int index) const;
    bool cableMenuResult (const jidai::CableSpec& cable, int result);
    bool cancelDrag();
    bool isDragging() const { return dragging; }
    jidai::Rack::Check connect (const std::string& from, const std::string& to);
    int disconnectAll (const std::string& jack);
    void setColour (int cableIndex, int colour);      // -1 = role colour
    juce::String lastMessage() const { return message; }
    void clearMessage() { message.clear(); repaint(); }

    bool hitTest (int x, int y) override;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void compute();
    int spotAt (juce::Point<float>) const;
    int cableAt (juce::Point<float>) const;
    juce::String farName (const std::string& id) const;
    void commit (const std::vector<jidai::CableSpec>&, const std::string& label);
    jidai::cable::JackFacts factsFor (const RackView::JackSpot&) const;
    void showJackMenu (int spot);
    void showCableMenu (int index);
    void say (const juce::String& text, juce::Point<float> where);
    juce::Path rope (juce::Point<float> a, juce::Point<float> b) const;
    juce::Path stub (juce::Point<float> a) const;
    float s() const { return view.scale(); }

    RackView& view;
    JidaiProcessor& proc;
    std::vector<Drawn> cablesDrawn;
    int selected = -1, hoverSpot = -1, hoverCable = -1;
    bool dragging = false, pressedOnJack = false;
    std::string dragFrom;                // the fixed end of the cable being drawn
    std::string pressJack;               // the jack the press started on
    jidai::cable::PressPlan press;       // what the press does (pick up, new cable, refused)
    int moving = -1;                     // cable whose end is picked up, or -1 for a new cable
    juce::Point<float> dragOrigin, dragPos;
    int dragTarget = -1;
    juce::String message;
    juce::Point<float> messageAt;
    juce::uint32 messageTime = 0;
};

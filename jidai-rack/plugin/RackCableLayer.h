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
// Editing: drag from a jack to patch (compatible jacks ring green, refusals say why); drag a plug to move a cable end
// (off every jack to unplug it); Shift-drag stacks a new cable on a patched jack; click a cable to select it (Delete
// removes it); right-click a jack or cable for Connect to..., Disconnect, Colour. The layer only takes the mouse on
// jacks and cables, so the panels under it keep theirs.

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
    bool cancelDrag();
    bool isDragging() const { return dragging; }
    jidai::Rack::Check connect (const std::string& from, const std::string& to);
    int disconnectAll (const std::string& jack);
    void setColour (int cableIndex, int colour);      // -1 = role colour
    juce::String lastMessage() const { return message; }

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
    void commit (const std::vector<jidai::CableSpec>&);
    void showJackMenu (int spot);
    void showCableMenu (int cable);
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
    int moving = -1;                     // cable whose end is picked up, or -1 for a new cable
    juce::Point<float> dragOrigin, dragPos;
    int dragTarget = -1;
    juce::String message;
    juce::Point<float> messageAt;
    juce::uint32 messageTime = 0;
};

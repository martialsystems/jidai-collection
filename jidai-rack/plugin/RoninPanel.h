// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// RONIN's own panel in the rack: RONIN's vector plate (panel/assets/panel_bg.svg) with RONIN's knob caps, EFFECT
// rocker, HOLD key and lamp, bank sockets, SAVE key and VU needle, painted by the same code as RONIN's PatchBayView.
// The PRESET screen itself is a PatternScreen above this panel. Cables are not drawn here: the rack's one cable
// layer draws every cable, across devices. It is a RackPanel so that layer can read its jacks and label rectangles.

#include "core/RoninDevice.h"

#include "ui/RackPanel.h"
#include "UI/PatchBayLogic.h"

#include <functional>

class RoninPanel : public RackPanel
{
public:
    RoninPanel (jidai::RoninDevice& device, std::function<int()> getColour, std::function<void (int)> setColour);
    ~RoninPanel() override;

    static PanelLayout makeLayout (const juce::String& rackId);
    static constexpr float kWidth = kPanelW;
    static constexpr float kHeight = kPanelH;       // RONIN's plate, which stops 26 units under its module frame

    jidai::RoninDevice& device() { return ronin; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    float scale() const { return (float) getWidth() / kWidth; }
    juce::Point<float> design (juce::Point<float> local) const { return local / scale(); }
    int knobAt (juce::Point<float> design) const;
    void setKnobValue (int index, float value);

    jidai::RoninDevice& ronin;
    std::function<int()> colour;
    std::function<void (int)> setColour;
    std::unique_ptr<juce::Drawable> plate;
    juce::String readout;
    bool effectPress = false, holdPress = false;
    int knobDrag = -1;
    bool knobMoved = false, suppressSwitchStep = false;
    float dragStartY = 0.0f, dragStartValue = 0.0f;
};

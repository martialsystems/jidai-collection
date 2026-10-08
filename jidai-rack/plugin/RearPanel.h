// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// A device's rear plate (JIDAI_RACK_Redesign 3.3, mockup/rack_back.png), generated from Device::jackGroups(): a cream
// sticker with the name, the role legend and a LAT plate along the top, then one framed box per group, every jack with
// a role-coloured ring and its label (outputs on a light label). 1 U plates (RACK I/O, ORIGAMI) put the sticker on the
// left and the groups in one row. Every jack of the device is here, front jacks and back-only jacks alike.
// Layout in design units: 1600 wide, the plate height given by the slot. The plate scales to its width.

#include "core/Device.h"

#include <juce_gui_basics/juce_gui_basics.h>

class RearPanel : public juce::Component
{
public:
    RearPanel (jidai::Device& device, float designHeight, std::function<int()> pathLatency);

    void paint (juce::Graphics&) override;
    float scale() const { return (float) getWidth() / 1600.0f; }

    // Jack centre in local pixels (every jack of the device is on the plate) and its socket radius.
    juce::Point<float> jackCentre (int jack) const;
    float jackRadius() const { return socketR * scale(); }
    int jackCount() const { return (int) centres.size(); }

    static const char* kindLine (jidai::DeviceKind);    // "12-STEP SEQUENCER" etc.

private:
    struct Box { juce::String title; juce::Rectangle<float> r; std::vector<int> jacks; };
    void layout();
    bool layoutPass (float cellW, float cellH, bool place);

    jidai::Device& device;
    float designH;
    std::function<int()> latency;
    std::vector<Box> boxes;
    std::vector<juce::Point<float>> centres;      // design units, by jack index
    juce::Rectangle<float> sticker, legend, latPlate;
    float socketR = 11.0f, labelSize = 10.5f;
    bool compact = false;
};

// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// A 1 U face built from a short list of controls: the RACK I/O front (transport LCD, meters, MAIN level, LAT) and the
// CLOSED fronts of BUSHIDO and RONIN (JIDAI_RACK_Redesign 3.2: the essentials only, no jacks; open the device or flip
// to the back to patch). Controls are laid out left to right in design units (1600 wide) and centred, so the face is
// mirror-balanced. Vector-drawn; it repaints itself at 15 Hz for meters and lamps.

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

class CompactFace : public juce::Component, private juce::Timer
{
public:
    struct Item
    {
        enum Type { Title, Knob, Toggle, Momentary, Meter, Lcd, Lamps, Space };
        Item (Type t = Space, juce::String l = {}, float w = 80.0f) : type (t), label (std::move (l)), width (w) {}
        static Item title (juce::String name, float w, std::function<juce::String()> sub)
        {
            Item it (Title, std::move (name), w);
            it.text = std::move (sub);
            return it;
        }
        Type type = Space;
        juce::String label;
        float width = 80.0f;                                  // design units
        std::function<double()> get;                          // Knob, Toggle: 0..1; Meter: volts
        std::function<void (double)> set;                     // Knob, Toggle; Momentary: 1 down, 0 up
        std::function<juce::String()> text;                   // Title subline, Lcd text, Knob readout
        std::function<float (int)> lamp;                      // Lamps: 0..1 per lamp
        int lamps = 0;
        double defaultValue = 0.5;
    };
    struct Style { juce::Colour top, bottom, ink, accent; };

    CompactFace (std::vector<Item> items, Style style);
    ~CompactFace() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    float scale() const { return (float) getWidth() / 1600.0f; }
    juce::Rectangle<float> itemBounds (int i) const;          // local pixels
    int itemCount() const { return (int) items.size(); }

private:
    void timerCallback() override { repaint(); }
    int itemAt (juce::Point<float>) const;

    std::vector<Item> items;
    Style style;
    int drag = -1;
    double dragStart = 0.0;
    float dragY = 0.0f;
};

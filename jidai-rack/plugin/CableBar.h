// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// The cable bar under the header: the cable colour palette (ANY, then the six role colours: audio, CV, gate/clock,
// V/oct, Hz/V linear, S-trig) and the help lines that spell out every cable gesture (jidai::cable::helpLines, with
// the platform's modifier names). Picking a colour dims the jacks that cannot take a cable of that colour (on FRONT and
// BACK); picking it again, or ANY, clears it. Help text is at least 12 px (about 9 pt) at 100 %; resting the pointer
// on it for half a second shows it larger.

#include "JidaiProcessor.h"
#include "RackStyle.h"

#include <juce_gui_basics/juce_gui_basics.h>

class CableBar : public juce::Component, private juce::Timer
{
public:
    explicit CableBar (JidaiProcessor&);
    ~CableBar() override;

    static constexpr int kHeight = 64;               // design units (scaled with the UI scale)
    std::function<void()> onPaletteChanged;          // the rack repaints its jacks

    int chipCount() const { return 1 + jidai::jcs::kRoleCount; }
    juce::Rectangle<float> chipBounds (int chip) const;      // chip 0 = ANY, 1..6 = kPaletteOrder
    juce::Rectangle<float> helpBounds() const;
    int paletteForChip (int chip) const;                       // kAnyRole or a Role index
    void pickChip (int chip);
    bool zoomShowing() const;
    void showZoom (bool show);
    juce::StringArray helpRows() const;                        // the two rows shown on the bar
    static float helpFontPx (float k) { return juce::jmax (rackstyle::kHelpTextPx, 12.5f) * k; }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    class Zoom;
    void timerCallback() override;
    float u() const { return (float) getHeight() / (float) kHeight; }

    JidaiProcessor& proc;
    int hover = -1;
    bool overHelp = false;
    juce::uint32 helpSince = 0;
    std::unique_ptr<Zoom> zoom;
};

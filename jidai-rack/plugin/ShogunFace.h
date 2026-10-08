// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// SHOGUN's MAIN tab in the rack: drawn from SHOGUN's own generated op table (plugin/Source/PanelLayout.inc at the
// pinned SHOGUN commit, 1200 x 672 design units), bound to a ShogunDevice instead of the SHOGUN plugin's processor.
// The primitives and the MAIN-tab bindings follow the SHOGUN editor (PluginEditor.cpp) so the face looks and reads
// the same. The other seven tabs are SHOGUN-plugin pages; in the rack they are shown dimmed (patching is on the rear
// panel, and the rack has no per-device editor windows). Scales to its width.
//
// MAIN-tab controls: voice select keys (click selects and auditions), 16 step keys of the selected track (click
// toggles, shift-click = accent, right-click selects the step), the selected step's knobs, page 1-16 / 17-32,
// track < >, COPY / PASTE / CLEAR / RANDOM, RUN / RST, the p:<param> knobs, keys and LCDs (drag, double-click =
// default, wheel), the MAIN meters and the bar:step position.

#include "core/ShogunDevice.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

class ShogunFace : public juce::Component, private juce::Timer
{
public:
    static constexpr float kW = 1200.0f, kH = 672.0f;

    explicit ShogunFace (jidai::ShogunDevice& d);
    ~ShogunFace() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    int selectedVoice() const { return selVoice_; }
    void setSelectedVoice (int v) { selVoice_ = juce::jlimit (0, shogun::kVoices - 1, v); repaint(); }
    static int opCount();
    int boundCount() const { return (int) bounds_.size(); }     // MAIN-tab bindings (tests)
    int paramKnobCount() const;                                 // MAIN-tab p:<param> controls bound to a parameter
    bool loadProgram (int program);      // a SHOGUN factory program (0 = INIT, then the kits), as the KIT menu does
    void showProgramMenu();
    bool skDimmed (int field) const;     // a step field that does not apply to the selected voice (drawn dim, no input)
    // Tests: knobs, keys and toggles drawn on the face that do nothing (must be 0), and a drawn key's bounds by its
    // label ("SRC" for the clock source key; the top-bar arrows by index: 0/1 KIT ◀ ▶, 2/3 PATTERN ◀ ▶).
    int inertControlCount() const;
    juce::Rectangle<int> sourceKeyBounds() const;
    juce::Rectangle<int> programArrowBounds (int index) const;

private:
    struct Bound { int op, kind, a, b; };
    void timerCallback() override;
    void buildBindings();
    juce::AffineTransform toPanel() const;
    void paintOp (juce::Graphics& g, int opIndex, const Bound* b);
    int findBound (juce::Point<float> p) const;
    void commit();
    shogun::Step& selStep();

    jidai::ShogunDevice& device_;
    jidai::ShogunDevice::Edits ui_;
    std::vector<Bound> bounds_;
    std::vector<int> staticOps_;
    int selVoice_ = 0, selStep_ = 0, page_ = 0;
    int dragBound_ = -1;
    float dragStartU_ = 0.0f;
    juce::Point<float> dragStart_;
    float meterL_ = 0.0f, meterR_ = 0.0f;
    std::array<shogun::Step, shogun::kMaxSteps> clipboard_ {};
    int clipLen_ = 0;
    juce::Image cache_;
    int cacheW_ = 0;
};

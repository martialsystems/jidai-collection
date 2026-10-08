// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// ORIGAMI's native panel (ORIGAMI_Proposal.md 5; mockup/folder_fx_main.png): dark plate, bronze section frames,
// indigo accents, mirror-symmetric blocks INPUT | WAVE | OUTPUT over an 8-jack row. Vector-drawn, so it is sharp
// at every scale. One component for every place ORIGAMI appears:
//   Mode::Plugin     1200 x 360 design: ears, its own tab strip (MAIN STAGES DYNAMICS SETUP) and the face.
//   Mode::RackOpen   1136 x 332 design: the face only; the rack's device strip carries the tabs (setPage).
//   Mode::RackClosed 1136 x 87 design: the 1 U essentials strip, no jacks.
// The panel scales uniformly to fit and centres. It knows nothing about the processor or the rack: an Access
// binding reads and writes parameters in natural units (origami/dsp/OrigamiParams.h) and reads the meters.

#include "origami/dsp/OrigamiParams.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

class OrigamiPanel : public juce::Component, public juce::TooltipClient, private juce::Timer
{
public:
    struct Access
    {
        virtual ~Access() = default;
        virtual double get (int param) const = 0;
        virtual void set (int param, double value) = 0;
        virtual void gesture (int param, bool begin) { juce::ignoreUnused (param, begin); }
        virtual float inPeak (int ch) const = 0;        // volts
        virtual float outPeak (int ch) const = 0;
        virtual float follower() const = 0;             // 0..1
        virtual bool over() const = 0;                  // JCS R15 OVER LED
        virtual int latencySamples() const = 0;
        virtual double sampleRate() const { return 48000.0; }
        virtual double stageCurve (int stage, double x) const = 0;
    };

    enum class Mode { Plugin, RackOpen, RackClosed };
    enum Jack { InL, InR, VcaCv, Vc1, Vc2, Vc3, OutL, OutR, kJackCount };

    static constexpr float kPluginW = 1200.0f, kPluginH = 360.0f;
    static constexpr float kFaceW = 1136.0f, kFaceH = 332.0f, kClosedH = 87.0f;

    OrigamiPanel (Access& access, Mode mode);
    ~OrigamiPanel() override;

    void setMode (Mode m);
    Mode mode() const { return mode_; }
    void setPage (origami::Page p);
    origami::Page page() const { return page_; }
    static const char* pageName (origami::Page p);

    // Plugin mode only: SETUP's UI SCALE buttons call this; the editor resizes. uiScale highlights the button.
    std::function<void (float)> onScale;
    float uiScale = 1.0f;
    std::function<void (origami::Page)> onPageChange;

    juce::Point<float> designSize() const;
    // Jack centres in local coordinates (open modes; empty point when CLOSED). Jack ids are SECTION:LABEL.
    juce::Point<float> jackCentre (int jack) const;
    float jackRadius() const;
    static const char* jackId (int jack);
    static bool jackIsAudio (int jack) { return jack == InL || jack == InR || jack == OutL || jack == OutR; }
    static bool jackIsOutput (int jack) { return jack == OutL || jack == OutR; }
    int jackAt (juce::Point<float> local) const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String getTooltip() override;

    // Controls, for tests and the probe: design-space centre of the control bound to a parameter on this page.
    juce::Point<float> controlCentre (int param) const;     // local coordinates; (-1,-1) when not shown
    juce::Point<float> tabCentre (origami::Page p) const;   // plugin mode
    juce::Point<float> toLocal (juce::Point<float> design) const;

private:
    enum class Kind { Knob, Toggle, Selector, Button };
    struct Control
    {
        int param = -1;
        Kind kind = Kind::Knob;
        juce::Point<float> c;      // design coordinates of the current mode
        float r = 15.0f;           // knob radius, or half-width for buttons
        juce::String label, left, right;
        bool bigTicks = false;
    };

    void rebuild();
    void addFaceControls (juce::Point<float> o);
    float scale() const;
    juce::Point<float> origin() const;
    juce::Point<float> toDesign (juce::Point<float> local) const;
    juce::Point<float> faceOrigin() const;
    juce::Rectangle<float> hitBox (const Control&) const;
    int controlAt (juce::Point<float> design) const;
    int tabAt (juce::Point<float> design) const;
    int scaleButtonAt (juce::Point<float> design) const;

    void paintPluginChrome (juce::Graphics&);
    void paintFace (juce::Graphics&, juce::Point<float> o);
    void paintMain (juce::Graphics&, juce::Point<float> o);
    void paintStages (juce::Graphics&, juce::Point<float> o);
    void paintDynamics (juce::Graphics&, juce::Point<float> o);
    void paintSetup (juce::Graphics&, juce::Point<float> o);
    void paintJackRow (juce::Graphics&, juce::Point<float> o);
    void paintClosed (juce::Graphics&);
    void paintControl (juce::Graphics&, const Control&);
    void paintMeter (juce::Graphics&, juce::Rectangle<float> r, float volts);
    juce::String statusText() const;
    double normal (int p) const;
    void setNormal (int p, double n);
    void timerCallback() override { repaint(); }

    Access& access_;
    Mode mode_;
    origami::Page page_ = origami::Page::Main;
    std::vector<Control> controls_;
    int drag_ = -1;
    double dragStartNormal_ = 0.0;
    float dragStartY_ = 0.0f;
};

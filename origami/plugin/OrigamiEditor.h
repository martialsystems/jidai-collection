// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// ORIGAMI editor: the native panel in plugin mode. 1200 x 360 at 100 %, scaling 75-200 % (900 x 270 to
// 2400 x 720) with a fixed aspect ratio, from SETUP's UI SCALE buttons or the corner.

#include "OrigamiPanel.h"
#include "OrigamiProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

class OrigamiEditor : public juce::AudioProcessorEditor, private OrigamiPanel::Access
{
public:
    explicit OrigamiEditor (OrigamiProcessor&);
    ~OrigamiEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
    OrigamiPanel& panel() { return panel_; }

private:
    double get (int p) const override { return proc_.value (p); }
    void set (int p, double v) override { proc_.setValue (p, v); }
    void gesture (int p, bool begin) override { begin ? proc_.beginGesture (p) : proc_.endGesture (p); }
    float inPeak (int ch) const override { return proc_.core().inPeak (ch); }
    float outPeak (int ch) const override { return proc_.core().outPeak (ch); }
    float follower() const override { return proc_.core().follower(); }
    bool over() const override { return proc_.core().overLit(); }
    int latencySamples() const override { return proc_.value (origami::kQuality) >= 0.5 ? origami::kLatency2x : 0; }
    double sampleRate() const override { return proc_.currentSampleRate(); }
    double stageCurve (int stage, double x) const override;

    OrigamiProcessor& proc_;
    OrigamiPanel panel_;
    juce::TooltipWindow tips_ { this, 500 };
};

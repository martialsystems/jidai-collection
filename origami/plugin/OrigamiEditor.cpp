// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "OrigamiEditor.h"

using namespace origami;

OrigamiEditor::OrigamiEditor (OrigamiProcessor& p) : juce::AudioProcessorEditor (p), proc_ (p), panel_ (*this, OrigamiPanel::Mode::Plugin)
{
    const float startScale = proc_.uiScale;     // setResizeLimits resizes (and resized() writes uiScale)
    addAndMakeVisible (panel_);
    panel_.uiScale = startScale;
    panel_.onScale = [this] (float s) {
        proc_.uiScale = s;
        setSize (juce::roundToInt (OrigamiPanel::kPluginW * s), juce::roundToInt (OrigamiPanel::kPluginH * s));
    };
    setResizable (true, true);
    setResizeLimits (900, 270, 2400, 720);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (OrigamiPanel::kPluginW / OrigamiPanel::kPluginH);
    setSize (juce::roundToInt (OrigamiPanel::kPluginW * startScale), juce::roundToInt (OrigamiPanel::kPluginH * startScale));
}

OrigamiEditor::~OrigamiEditor() = default;

void OrigamiEditor::resized()
{
    panel_.setBounds (getLocalBounds());
    proc_.uiScale = (float) getWidth() / OrigamiPanel::kPluginW;
    panel_.uiScale = proc_.uiScale;
}

double OrigamiEditor::stageCurve (int stage, double x) const
{
    double v[kParamCount];
    for (int p = 0; p < kParamCount; ++p) v[p] = proc_.value (p);
    return OrigamiCore::stageCurve (v, stage, x);
}

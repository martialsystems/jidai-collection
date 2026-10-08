// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// The JIDAI RACK window (JIDAI_RACK_Redesign 3.1, mockup rack_front.png): a header (FRONT / BACK, CABLES modes,
// FOLD ALL, total LAT, UI scale), the device browser on the left and the rack on the right. 1200 x 672 by default,
// 960 x 540 at the smallest; the rack scrolls.
// Keys: Tab flips FRONT/BACK; K cycles the cable mode; F folds the selected device, Shift+F folds or opens every
// device; C opens or closes the selected device; Esc cancels a cable drag (or clears the selection); Delete removes
// the selected cable.

#include "JidaiProcessor.h"
#include "DeviceBrowser.h"
#include "RackView.h"

class JidaiEditor : public juce::AudioProcessorEditor,
                    public juce::DragAndDropContainer,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    explicit JidaiEditor (JidaiProcessor&);
    ~JidaiEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

    RackView& rackView() { return rack; }
    juce::Viewport& viewport() { return view; }
    juce::Component& deviceList();
    DeviceBrowser& browser() { return *list; }
    juce::Component& header();
    void setBrowserOpen (bool);
    void setScalePercent (int);
    float uiScale() const { return (float) proc.scalePercent / 100.0f; }

    static constexpr int kDefaultWidth = 1200, kDefaultHeight = 672, kMinWidth = 960, kMinHeight = 540;
    static constexpr int kListWidth = 184, kHeaderHeight = 40;

    // Header buttons (probe): FRONT, BACK, the four cable modes, FOLD ALL, scale.
    enum HeaderButton { BtnFront, BtnBack, BtnModeAll, BtnModeHidePass, BtnModeSelected, BtnModeHide, BtnFoldAll, BtnScale, BtnNotice, kHeaderButtons };
    juce::Rectangle<int> headerButtonBounds (int button) const;      // in the editor

protected:
    void dragOperationStarted (const juce::DragAndDropTarget::SourceDetails&) override;
    void dragOperationEnded (const juce::DragAndDropTarget::SourceDetails&) override;

private:
    class Header;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void layoutRack();
    void removeLater (const juce::String& description);

    JidaiProcessor& proc;
    std::unique_ptr<DeviceBrowser> list;
    std::unique_ptr<Header> head;
    juce::Viewport view;
    RackView rack;
    bool dropLanded = false;
    int lastLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JidaiEditor)
};

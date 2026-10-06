// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// The JIDAI RACK window: the device browser on the left, the rack on the right.
// Drag a row from the browser onto the rack to add one there, or click it to add one at the bottom. Grab a device by
// its ear to move it, or drag it off the rack (onto the browser, or out of the window) to remove it; the x on its
// ear removes it too. The button at the browser's top right closes it to a strip and opens it again.

#include "JidaiProcessor.h"
#include "DeviceBrowser.h"
#include "RackView.h"

class JidaiEditor : public juce::AudioProcessorEditor,
                    public juce::DragAndDropContainer,
                    private juce::ChangeListener
{
public:
    explicit JidaiEditor (JidaiProcessor&);
    ~JidaiEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    RackView& rackView() { return rack; }
    juce::Viewport& viewport() { return view; }
    juce::Component& deviceList();
    DeviceBrowser& browser() { return *list; }
    void setBrowserOpen (bool);      // the browser's close/open button

    static constexpr int kListWidth = 212;

protected:
    void dragOperationStarted (const juce::DragAndDropTarget::SourceDetails&) override;
    void dragOperationEnded (const juce::DragAndDropTarget::SourceDetails&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void layoutRack();
    void removeLater (const juce::String& description);     // "move:<rack id>", after the drag has finished

    JidaiProcessor& proc;
    std::unique_ptr<DeviceBrowser> list;
    juce::Viewport view;
    RackView rack;
    bool dropLanded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JidaiEditor)
};

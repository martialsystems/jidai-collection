// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// The rack: a cabinet with two rails, and every device screwed in between them, flush, top to bottom, on its own
// panel. Empty rack space runs on below the last device. A device's ears carry its screws, its fold arrow, its name
// and its remove button; grab an ear to move the device. A folded device is a thin strip. One cable layer lies over
// every panel, so a cable can join any two jacks in the rack, and cables hang down into the empty space.
// The rack is the drop target for new devices and for devices being moved; the editor's device list is where they
// come from and where they go to be removed.

#include "JidaiProcessor.h"
#include "RoninPanel.h"

#include "ui/CableLayer.h"
#include "ui/PatternScreen.h"
#include "ui/RackPanel.h"

class RackView : public juce::Component,
                 public juce::DragAndDropTarget
{
public:
    // Design units. Panels are 1600 wide; the cabinet adds a side and a rail each side of them.
    static constexpr float kPanelWidth = 1600.0f;
    static constexpr float kSide = 18.0f;                       // cabinet wall outside each rail
    static constexpr float kRail = 52.0f;                       // rail width; a device's ear covers it
    static constexpr float kMargin = kSide + kRail;             // panel's left edge
    static constexpr float kDesignWidth = kPanelWidth + 2.0f * kMargin;
    static constexpr float kUnit = 160.0f;                      // one rack unit at this width (19 in : 1.75 in)
    static constexpr float kFoldHeight = 54.0f;                 // a folded device
    static constexpr float kEmptySpace = 2.0f * kUnit;          // empty rack always shown under the last device

    explicit RackView (JidaiProcessor&);
    ~RackView() override;

    void rebuild();                  // the device list changed
    void reloadCables();             // the patch changed outside the cable layer (a program or a pattern)
    int heightForWidth (int width, int minimum) const;
    float scale() const { return (float) getWidth() / kDesignWidth; }

    std::function<void (const juce::String& description)> onDropped;    // the editor learns a drop landed
    std::function<void()> onLayoutChanged;                             // devices came or went: the height changed
    int slotCount() const { return (int) slots.size(); }
    int indexOfSlotFor (const jidai::Device&) const;
    juce::Rectangle<int> slotBounds (int index) const;                 // one device, ears included, in this component
    juce::Rectangle<int> removeButtonBounds (int index) const;
    juce::Rectangle<int> foldButtonBounds (int index) const;
    CableLayer* cableLayer() { return cables.get(); }
    void removeDevice (jidai::Device*);
    void setFolded (jidai::Device*, bool);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

private:
    class Mount;
    class Shade;
    class BushidoBinding;
    class Swatches;
    struct Slot;

    float devicesHeight() const;     // design units, devices only
    int insertionIndex (float localY) const;

    JidaiProcessor& proc;
    std::vector<std::unique_ptr<Slot>> slots;
    std::unique_ptr<CableLayer> cables;
    int colour = 0;
    int insertAt = -1;
};

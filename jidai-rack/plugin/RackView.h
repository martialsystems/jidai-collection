// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// The rack (JIDAI_RACK_Redesign 3, mockups rack_front.png / rack_back.png): a cabinet with two rails and every
// device screwed in between them, flush, top to bottom, in whole rack units. Each device has a 30-unit tab strip
// (fold, name, id, OPEN/CLOSED, its tabs, LAT, BYPASS, remove) above its face.
//   FRONT: the native panels. OPEN shows the full panel (BUSHIDO 3 U, RONIN 4 U, ORIGAMI 3 U); CLOSED shows a 1 U
//          strip of essentials with no jacks. RACK I/O is a 1 U front with no front jacks.
//   BACK:  generated rear plates (RearPanel) with every jack, the back-only ones included.
// One cable layer (RackCableLayer) lies over everything; it draws and edits the rack's cables in four modes.
// The rack is the drop target for new devices ("add:KIND", "add:RONIN FX") and for devices being moved ("move:ID").

#include "JidaiProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

class RackCableLayer;

class RackView : public juce::Component,
                 public juce::DragAndDropTarget,
                 public juce::TooltipClient
{
public:
    // Design units. Panels are 1600 wide; the cabinet adds a side and a rail each side of them.
    static constexpr float kPanelWidth = 1600.0f;
    static constexpr float kSide = 18.0f;
    static constexpr float kRail = 52.0f;
    static constexpr float kMargin = kSide + kRail;
    static constexpr float kDesignWidth = kPanelWidth + 2.0f * kMargin;     // 1740
    static constexpr float kUnit = 160.0f;
    static constexpr float kStripHeight = 30.0f;                           // the device tab strip
    static constexpr float kFoldHeight = 54.0f;
    static constexpr float kEmptySpace = kUnit;

    // A jack as shown in the current view: where it is and what it is.
    struct JackSpot {
        std::string id;                 // global "RONIN#1/VCO:HZ/V"
        juce::Point<float> p;           // this component's pixels
        float r = 8.0f;
        int slot = -1, jack = -1;
        jidai::jcs::Role role = jidai::jcs::Role::CV;
        bool out = false;
    };
    // Strip parts (probe and tests).
    enum Part { PartNone = 0, PartFold, PartRemove, PartOpenClose, PartBypass, PartBackBadge, PartName, PartTab0 = 10 };

    explicit RackView (JidaiProcessor&);
    ~RackView() override;

    void rebuild();                     // devices, view, open/closed, fold or tab changed
    void reloadCables();                // the patch changed outside the cable layer
    int heightForWidth (int width, int minimum) const;
    float scale() const { return (float) getWidth() / kDesignWidth; }

    // View state (saved with the rack by the processor).
    bool showBack() const { return proc.showBack; }
    void setShowBack (bool back);
    void flip() { setShowBack (! proc.showBack); }
    int cableMode() const { return proc.showBack ? proc.cableModeBack : proc.cableModeFront; }
    void setCableMode (int mode);
    void cycleCableMode() { setCableMode ((cableMode() + 1) % 4); }
    static const char* cableModeName (int mode);
    void setClosed (jidai::Device*, bool closed);
    void setFolded (jidai::Device*, bool folded);
    void foldAll (bool folded);
    bool allFolded() const;
    void setTab (jidai::Device*, int tab);
    int tabOf (const jidai::Device*) const;
    static juce::StringArray tabNames (jidai::DeviceKind);
    void removeDevice (jidai::Device*);
    void renameDevice (jidai::Device*, const juce::String&);
    jidai::Device* selectedDevice() const { return selected; }
    void selectDevice (jidai::Device*);
    void scrollTo (jidai::Device*);
    std::function<void (juce::Rectangle<int>)> onScrollTo;     // the editor scrolls its viewport
    std::function<void (const juce::String&)> onDropped;
    std::function<void()> onLayoutChanged;
    std::function<void()> onViewChanged;                       // flip or cable mode: the header repaints

    // Geometry.
    int slotCount() const { return (int) slots.size(); }
    jidai::Device* slotDevice (int i) const;
    int indexOfSlotFor (const jidai::Device&) const;
    float unitsFor (const jidai::Device&) const;               // rack units of the device in the current view
    juce::Rectangle<int> slotBounds (int index) const;         // ears included
    juce::Rectangle<int> stripBounds (int index) const;
    juce::Rectangle<int> faceBounds (int index) const;         // under the strip
    juce::Rectangle<int> partBounds (int index, int part) const;
    juce::Rectangle<int> foldButtonBounds (int i) const { return partBounds (i, PartFold); }
    juce::Rectangle<int> removeButtonBounds (int i) const { return partBounds (i, PartRemove); }
    juce::Rectangle<int> openCloseBounds (int i) const { return partBounds (i, PartOpenClose); }
    juce::Rectangle<int> tabBounds (int i, int tab) const { return partBounds (i, PartTab0 + tab); }

    // Jacks in the current view, and why a jack is not shown ("back", "closed", "folded", "tab", "").
    const std::vector<JackSpot>& jackSpots() const { return spots; }
    const JackSpot* spotFor (const std::string& globalId) const;
    juce::String whyHidden (const std::string& globalId) const;
    RackCableLayer& cableLayer() { return *cables; }
    juce::Component* faceComponent (int index) const;          // the panel, page, strip or plate on show

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    juce::String getTooltip() override;      // the empty-space hint, enlarged
    juce::String emptyHint() const;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

private:
    class Mount;
    class Strip;
    class Shade;
    class BushidoBinding;
    class BushidoTabHost;
    class OrigamiAccess;
    struct Slot;

    float devicesHeight() const;
    int insertionIndex (float localY) const;
    void buildFace (Slot&);
    void updateSpots();
    void rebuildLater();

    JidaiProcessor& proc;
    std::vector<std::unique_ptr<Slot>> slots;
    std::unique_ptr<RackCableLayer> cables;
    std::vector<JackSpot> spots;
    std::map<const jidai::Device*, int> tabs;
    jidai::Device* selected = nullptr;
    int insertAt = -1;
};

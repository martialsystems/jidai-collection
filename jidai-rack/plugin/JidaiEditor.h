// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// The JIDAI RACK window (JIDAI_RACK_Redesign 3.1, mockup rack_front.png): a header (FRONT / BACK, CABLES modes,
// FOLD ALL, RACKS (the starter racks), total LAT, UI scale), the device browser on the left and the rack on the right. 1200 x 672 by default,
// 960 x 540 at the smallest; the rack scrolls.
// Keys: Tab flips FRONT/BACK; K cycles the cable mode; F folds the selected device, Shift+F folds or opens every
// device; C opens or closes the selected device; Esc cancels a cable drag (or clears the selection); Delete removes
// the selected cable; Cmd+Z / Shift+Cmd+Z (Mac) or Ctrl+Z / Ctrl+Y (Windows) undo and redo cable edits.
// Under the header, the cable bar (CableBar): the cable colour palette and the cable gestures in plain words.

#include "JidaiProcessor.h"
#include "RackLookAndFeel.h"
#include "DeviceBrowser.h"
#include "RackView.h"
#include "CableBar.h"

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
    CableBar& cableBar() { return *bar; }
    void setBrowserOpen (bool);
    void setScalePercent (int);
    float uiScale() const { return (float) proc.scalePercent / 100.0f; }

    static constexpr int kDefaultWidth = 1200, kDefaultHeight = 672, kMinWidth = 960, kMinHeight = 540;
    static constexpr int kListWidth = 184, kHeaderHeight = 40;

    // Header buttons (probe): FRONT, BACK, the four cable modes, FOLD ALL, scale.
    enum HeaderButton { BtnFront, BtnBack, BtnModeAll, BtnModeHidePass, BtnModeSelected, BtnModeHide, BtnFoldAll, BtnScale, BtnNotice, BtnRacks, kHeaderButtons };
    juce::Rectangle<int> headerButtonBounds (int button) const;      // in the editor
    // RACKS: the starter racks, INIT first, then one submenu per category. Item id = program index + 1.
    juce::PopupMenu starterRackMenu() const;
    // The UI scale pill is a list control: click = next step, Shift-click = previous, right-click = this menu
    // (75 % .. 200 %, the current one ticked; item id = step index + 1).
    juce::PopupMenu scaleMenu() const;
    // How the scale menu is shown: empty = juce::PopupMenu::showMenuAsync. The probe sets it to capture and answer it.
    std::function<void (const juce::PopupMenu&, const juce::PopupMenu::Options&, std::function<void (int)>)> showMenu;

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
    RackLookAndFeel laf;                     // sans menus, enlarged tooltips; outlives every child
    std::unique_ptr<DeviceBrowser> list;
    std::unique_ptr<Header> head;
    std::unique_ptr<CableBar> bar;
    juce::Viewport view;
    RackView rack;
    bool dropLanded = false;
    int lastLatency = -1;
    juce::TooltipWindow tips { this, 500 };  // hover 0.5 s

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JidaiEditor)
};

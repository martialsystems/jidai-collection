// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// The device browser on the left of the rack window (JIDAI_RACK_Redesign 3.7). A search box filters the devices by
// name. Groups: SEQUENCER (BUSHIDO), VOICE (RONIN), EFFECT (RONIN FX: a RONIN fed by HOST IN at its EXT IN; ORIGAMI),
// UTILITY (RACK I/O, at most one). Each card shows the device's name, one short line, and how many are on the rack.
// Drag a card onto the rack to add one there; click a card to add one at the bottom. New devices are auto-routed
// (Shift: not). A device dragged back onto the browser is removed.
// The button at its top right closes it to a thin strip down the left edge; a click on the strip opens it again.

#include "JidaiProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

class DeviceBrowser : public juce::Component,
                      public juce::DragAndDropTarget,
                      public juce::TooltipClient
{
public:
    struct Entry {
        const char* group;      // "SEQUENCER", "VOICE", "EFFECT", "UTILITY"
        jidai::DeviceKind kind;
        const char* name;       // card name; "add:<name>" is the drag description
        const char* line;       // one short line under the name
        bool effect = false;    // RONIN FX: auto-route HOST IN to its EXT IN as well
    };
    static const std::vector<Entry>& catalogue();

    explicit DeviceBrowser (JidaiProcessor&);
    ~DeviceBrowser() override;

    std::function<void (const Entry&, bool skipAutoRoute)> onAdd;    // a card was clicked (Shift: skip auto-route)
    std::function<void (const juce::String& description)> onRemoveDrop; // "move:<rack id>" dropped here
    std::function<void()> onToggle;                                    // the close/open button (or the closed strip) was clicked

    static constexpr int kClosedWidth = 28;                            // the closed browser: a strip down the left edge
    void setClosed (bool);
    bool isClosed() const { return closed; }
    juce::Rectangle<int> toggleBounds() const;

    void setSearch (const juce::String&);
    juce::TextEditor& searchBox() { return search; }
    int rowCount() const { return (int) rows.size(); }
    const Entry& row (int i) const { return *rows[(size_t) i]; }
    juce::Rectangle<int> rowBounds (int i) const;
    int countOnRack (jidai::DeviceKind) const;
    bool available (const Entry&) const;     // RACK I/O: only while there is none

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::String getTooltip() override;      // the help lines and card notes, enlarged
    juce::String helpTextAt (juce::Point<float>) const;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

private:
    struct Header { const char* group; float y; };
    void layoutRows();
    int rowAt (juce::Point<float>) const;

    JidaiProcessor& proc;
    juce::TextEditor search;
    std::vector<const Entry*> rows;
    std::vector<juce::Rectangle<float>> rowRects;
    std::vector<Header> headers;
    float listBottom = 0.0f;
    juce::Rectangle<float> helpRect;
    int hover = -1, pressed = -1;
    bool dragged = false, dropHover = false, closed = false, toggleHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceBrowser)
};

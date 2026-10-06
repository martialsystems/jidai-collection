// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// The device browser on the left of the rack window. A search box filters the devices by name. Devices are grouped
// Sequencer (BUSHIDO), Voice (RONIN) and Effect (RONIN again: it processes audio at its EXT IN). Each row shows the
// device's name, one short line, and how many are already on the rack. Drag a row onto the rack to add one there;
// click a row to add one at the bottom. A device dragged back onto the browser is removed.

#include "JidaiProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

class DeviceBrowser : public juce::Component,
                      public juce::DragAndDropTarget
{
public:
    struct Entry {
        const char* group;      // "SEQUENCER", "VOICE", "EFFECT"
        jidai::DeviceKind kind;
        const char* line;       // one short line under the name
    };
    static const std::vector<Entry>& catalogue();

    explicit DeviceBrowser (JidaiProcessor&);
    ~DeviceBrowser() override;

    std::function<void (jidai::DeviceKind)> onAdd;                     // a row was clicked
    std::function<void (const juce::String& description)> onRemoveDrop; // "move:<rack id>" dropped here

    void setSearch (const juce::String&);
    juce::TextEditor& searchBox() { return search; }
    int rowCount() const { return (int) rows.size(); }
    const Entry& row (int i) const { return *rows[(size_t) i]; }
    juce::Rectangle<int> rowBounds (int i) const;
    int countOnRack (jidai::DeviceKind) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

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
    int hover = -1, pressed = -1;
    bool dragged = false, dropHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceBrowser)
};

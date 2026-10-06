// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "JidaiEditor.h"

using namespace jidai;

// ---------------- editor ----------------

JidaiEditor::JidaiEditor (JidaiProcessor& p)
    : AudioProcessorEditor (p), proc (p), rack (p)
{
    list = std::make_unique<DeviceBrowser> (p);
    list->onAdd = [this] (DeviceKind kind)
    {
        proc.addDevice (kind);              // a click adds one at the bottom
        rack.rebuild();
        view.setViewPosition (0, juce::jmax (0, rack.getHeight() - view.getHeight()));
    };
    list->onRemoveDrop = [this] (const juce::String& description)
    {
        dropLanded = true;
        removeLater (description);
    };
    addAndMakeVisible (*list);
    view.setViewedComponent (&rack, false);
    view.setScrollBarsShown (true, false);
    view.setScrollBarThickness (10);
    addAndMakeVisible (view);
    rack.onLayoutChanged = [this] { layoutRack(); list->repaint(); };     // the browser's counts
    rack.onDropped = [this] (const juce::String&) { dropLanded = true; };
    proc.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (900, 480, 2600, 1800);
    setSize (1320, 860);
}

JidaiEditor::~JidaiEditor()
{
    proc.removeChangeListener (this);
}

juce::Component& JidaiEditor::deviceList() { return *list; }

void JidaiEditor::removeLater (const juce::String& description)
{
    const auto id = description.fromFirstOccurrenceOf (":", false, false).toStdString();
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<JidaiEditor> (this), id]
    {
        if (sp == nullptr)
            return;
        if (Device* d = sp->proc.rack().findDevice (id))
            sp->rack.removeDevice (d);
    });
}

void JidaiEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d0d0f));
}

void JidaiEditor::resized()
{
    auto r = getLocalBounds();
    list->setBounds (r.removeFromLeft (kListWidth));
    view.setBounds (r);
    layoutRack();
}

void JidaiEditor::layoutRack()
{
    const int w = juce::jmax (1, view.getWidth() - view.getScrollBarThickness());
    rack.setSize (w, rack.heightForWidth (w, view.getHeight()));
}

void JidaiEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rack.rebuild();         // a saved rack was loaded
}

void JidaiEditor::dragOperationStarted (const juce::DragAndDropTarget::SourceDetails&)
{
    dropLanded = false;
}

void JidaiEditor::dragOperationEnded (const juce::DragAndDropTarget::SourceDetails& d)
{
    // A device dragged off the rack and let go where nothing takes it (out of the window, say) is removed.
    const auto s = d.description.toString();
    if (s.startsWith ("move:") && ! dropLanded)
        removeLater (s);
    dropLanded = false;
}

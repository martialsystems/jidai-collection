// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "DeviceBrowser.h"
#include "RackStyle.h"

using namespace jidai;

namespace {

const juce::Colour kInk { 0xffe8e2cf };
const juce::Colour kDim { 0x99dcd6c2 };
constexpr float kTop = 96.0f, kHeaderH = 24.0f, kRowH = 48.0f, kRowGap = 5.0f, kPad = 8.0f;

juce::String helpText()
{
    return juce::String::fromUTF8 ("Drag into the rack \xc2\xb7 Shift = no auto-route. Click a card to add one at the bottom.\n\n"
                                   "Grab a device by its ear or strip to move it; drag it back here to remove it.");
}

}

const std::vector<DeviceBrowser::Entry>& DeviceBrowser::catalogue()
{
    static const std::vector<Entry> entries = {
        { "SEQUENCER", DeviceKind::Bushido, "BUSHIDO", "12-step sequencer \xc2\xb7 CV/gate", false },
        { "VOICE", DeviceKind::Ronin, "RONIN", "semi-modular voice", false },
        { "DRUMS", DeviceKind::Shogun, "SHOGUN", "drum machine \xc2\xb7 14 drums + 2 synths", false },
        { "EFFECT", DeviceKind::Ronin, "RONIN FX", "RONIN at EXT IN", true },
        { "EFFECT", DeviceKind::Origami, "ORIGAMI", "triple wave shaper FX", false },
        { "UTILITY", DeviceKind::RackIO, "RACK I/O", "host audio \xc2\xb7 MIDI \xc2\xb7 clock", false },
    };
    return entries;
}

DeviceBrowser::DeviceBrowser (JidaiProcessor& p) : proc (p)
{
    search.setTextToShowWhenEmpty ("Search devices", kDim);
    search.setFont (juce::FontOptions (14.0f));
    search.setIndents (8, 5);
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d0d0f));
    search.setColour (juce::TextEditor::textColourId, kInk);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff3a3a3e));
    search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xffc29f4c));
    search.setColour (juce::CaretComponent::caretColourId, kInk);
    search.onTextChange = [this] { layoutRows(); repaint(); };
    search.onEscapeKey = [this] { search.clear(); layoutRows(); repaint(); };
    addAndMakeVisible (search);
    layoutRows();
}

DeviceBrowser::~DeviceBrowser() = default;

void DeviceBrowser::setClosed (bool c)
{
    closed = c;
    search.setVisible (! closed);
    hover = pressed = -1;
    toggleHover = false;
    repaint();
}

juce::Rectangle<int> DeviceBrowser::toggleBounds() const
{
    return closed ? juce::Rectangle<int> (3, 10, kClosedWidth - 6, 26) : juce::Rectangle<int> (getWidth() - 40, 12, 28, 26);
}

void DeviceBrowser::setSearch (const juce::String& text)
{
    search.setText (text, false);
    layoutRows();
    repaint();
}

int DeviceBrowser::countOnRack (DeviceKind kind) const
{
    int n = 0;
    for (int i = 0; i < proc.rack().deviceCount(); ++i)
        if (proc.rack().device (i)->kind() == kind)
            ++n;
    return n;
}

bool DeviceBrowser::available (const Entry& e) const
{
    return e.kind != DeviceKind::RackIO || countOnRack (DeviceKind::RackIO) == 0;
}

void DeviceBrowser::layoutRows()
{
    rows.clear();
    rowRects.clear();
    headers.clear();
    const auto needle = search.getText().trim();
    float y = kTop;
    const char* group = nullptr;
    for (auto& e : catalogue())
    {
        if (needle.isNotEmpty() && ! juce::String::fromUTF8 (e.name).containsIgnoreCase (needle))
            continue;
        if (group == nullptr || juce::String (group) != e.group)
        {
            group = e.group;
            if (! headers.empty())
                y += 6.0f;
            headers.push_back ({ e.group, y });
            y += kHeaderH;
        }
        rows.push_back (&e);
        rowRects.push_back ({ kPad, y, (float) getWidth() - 2.0f * kPad, kRowH });
        y += kRowH + kRowGap;
    }
    listBottom = y;
    hover = pressed = -1;
}

juce::Rectangle<int> DeviceBrowser::rowBounds (int i) const
{
    return i >= 0 && i < (int) rowRects.size() ? rowRects[(size_t) i].toNearestInt() : juce::Rectangle<int>();
}

int DeviceBrowser::rowAt (juce::Point<float> p) const
{
    for (size_t i = 0; i < rowRects.size(); ++i)
        if (rowRects[i].contains (p))
            return (int) i;
    return -1;
}

void DeviceBrowser::resized()
{
    search.setBounds ((int) kPad, 60, getWidth() - 2 * (int) kPad, 28);
    layoutRows();
}

namespace {

// A chevron pointing left (close) or right (open), in a small rounded button.
void paintToggle (juce::Graphics& g, juce::Rectangle<float> r, bool pointRight, bool lit)
{
    g.setColour (juce::Colour (lit ? 0xff34332f : 0xff222224));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colour (lit ? 0xff5a564c : 0xff38383c));
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    const auto c = r.getCentre();
    const float k = 4.5f;
    juce::Path p;
    for (float dx : { -3.5f, 3.5f })
    {
        const float x = c.x + dx;
        if (pointRight) { p.startNewSubPath (x - k * 0.55f, c.y - k); p.lineTo (x + k * 0.55f, c.y); p.lineTo (x - k * 0.55f, c.y + k); }
        else            { p.startNewSubPath (x + k * 0.55f, c.y - k); p.lineTo (x - k * 0.55f, c.y); p.lineTo (x + k * 0.55f, c.y + k); }
    }
    g.setColour (lit ? kInk : kInk.withAlpha (0.75f));
    g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

}

void DeviceBrowser::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff151517));
    if (closed)
    {
        paintToggle (g, toggleBounds().toFloat(), true, toggleHover || dropHover);
        {
            const auto area = getLocalBounds().toFloat().withTrimmedTop (48.0f).withHeight (200.0f);
            juce::Graphics::ScopedSaveState state (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, area.getCentreX(), area.getCentreY()));
            g.setColour (dropHover ? juce::Colour (0xffe0675e) : kDim);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold).withKerningFactor (0.08f));
            g.drawText (dropHover ? "DROP TO REMOVE" : "DEVICE BROWSER", juce::Rectangle<float> (area.getHeight(), area.getWidth()).withCentre (area.getCentre()),
                        juce::Justification::centred);
        }
        if (dropHover)
        {
            g.setColour (juce::Colour (0xffc8322a));
            g.drawRect (getLocalBounds().reduced (1), 2);
        }
        g.setColour (juce::Colour (0xff000000));
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
        return;
    }
    paintToggle (g, toggleBounds().toFloat(), false, toggleHover);
    g.setColour (kInk);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("DEVICES", 14, 14, getWidth() - 28, 22, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (rackstyle::kHelpTextPx));
    g.drawText ("drag into the rack", 14, 36, getWidth() - 28, 16, juce::Justification::centredLeft);

    for (auto& h : headers)
    {
        g.setColour (juce::Colour (0xffc29f4c));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold).withKerningFactor (0.08f));
        g.drawText (h.group, juce::Rectangle<float> (kPad + 2.0f, h.y + 4.0f, (float) getWidth() - 2.0f * kPad, kHeaderH - 6.0f),
                    juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff2a2a2e));
        g.drawHorizontalLine ((int) (h.y + kHeaderH - 2.0f), kPad, (float) getWidth() - kPad);
    }

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& e = *rows[i];
        const auto r = rowRects[i];
        const bool lit = (int) i == hover || (int) i == pressed;
        g.setGradientFill (juce::ColourGradient (juce::Colour (lit ? 0xff34332f : 0xff2a2927), r.getX(), r.getY(),
                                                 juce::Colour (lit ? 0xff1f1f21 : 0xff1a1a1c), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colour (lit ? 0xff5a564c : 0xff38383c));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (juce::Colour (0x77dcd6c2));                                    // grip
        for (int k = 0; k < 3; ++k)
            g.fillRect (r.getX() + 9.0f, r.getY() + 19.0f + 6.0f * (float) k, 10.0f, 2.5f);

        const auto text = r.withTrimmedLeft (28.0f).withTrimmedRight (6.0f);
        const int count = countOnRack (e.kind);
        const juce::String countText = e.kind == DeviceKind::RackIO ? juce::String (count) + "/1" : juce::String (count);
        const juce::FontOptions countFont (10.5f);
        const float pillW = juce::GlyphArrangement::getStringWidth (juce::Font (countFont), countText) + 12.0f;
        const auto pill = juce::Rectangle<float> (text.getRight() - pillW, r.getY() + 8.0f, pillW, 17.0f);
        g.setColour (count > 0 ? juce::Colour (0xff3d3622) : juce::Colour (0xff222224));
        g.fillRoundedRectangle (pill, 8.5f);
        g.setColour (count > 0 ? juce::Colour (0xffe0bd62) : kDim.withAlpha (0.45f));
        g.setFont (countFont);
        g.drawText (countText, pill, juce::Justification::centred);

        g.setColour (available (e) ? kInk : kInk.withAlpha (0.4f));
        g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        g.drawText (juce::String::fromUTF8 (e.name), text.withHeight (30.0f).withRight (pill.getX() - 4.0f), juce::Justification::centredLeft);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (rackstyle::kHelpTextPx));
        g.drawFittedText (juce::String::fromUTF8 (e.line), text.withTrimmedTop (27.0f).withHeight (15.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }

    float y = listBottom + 6.0f;
    g.setFont (juce::FontOptions (rackstyle::kHelpTextPx));
    if (rows.empty())
    {
        g.setColour (kDim);
        g.drawText ("No device matches.", juce::Rectangle<float> (14.0f, kTop, (float) getWidth() - 28.0f, 20.0f), juce::Justification::centredLeft);
        y = kTop + 30.0f;
    }
    g.setColour (dropHover ? juce::Colour (0xffe0675e) : juce::Colour (0x77dcd6c2));
    helpRect = juce::Rectangle<float> (14.0f, y, (float) getWidth() - 28.0f, 140.0f);
    g.drawFittedText (dropHover ? juce::String ("Drop here to remove it.") : helpText(), helpRect.toNearestInt(), juce::Justification::topLeft, 10);

    if (dropHover)
    {
        g.setColour (juce::Colour (0xffc8322a));
        g.drawRect (getLocalBounds().reduced (1), 2);
    }
    g.setColour (juce::Colour (0xff000000));
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void DeviceBrowser::mouseMove (const juce::MouseEvent& e)
{
    const bool onButton = closed || toggleBounds().contains (e.getPosition());
    if (onButton != toggleHover)
    {
        toggleHover = onButton;
        repaint();
    }
    if (onButton)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        if (hover >= 0) { hover = -1; repaint(); }
        return;
    }
    const int at = rowAt (e.position);
    setMouseCursor (at >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    if (at != hover)
    {
        hover = at;
        repaint();
    }
}

void DeviceBrowser::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    toggleHover = false;
    repaint();
}

void DeviceBrowser::mouseDown (const juce::MouseEvent& e)
{
    if (closed || toggleBounds().contains (e.getPosition()))
    {
        pressed = -1;
        if (onToggle)
            onToggle();
        return;
    }
    pressed = rowAt (e.position);
    dragged = false;
    repaint();
}

void DeviceBrowser::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed < 0 || dragged || e.getDistanceFromDragStart() < 5)
        return;
    auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this);
    if (container == nullptr || container->isDragAndDropActive())
        return;
    dragged = true;
    auto image = createComponentSnapshot (rowRects[(size_t) pressed].toNearestInt(), true, 1.0f);
    image.multiplyAllAlphas (0.85f);
    if (! available (*rows[(size_t) pressed]))
        return;
    container->startDragging (juce::String ("add:") + juce::String::fromUTF8 (rows[(size_t) pressed]->name), this,
                              juce::ScaledImage (image), false, nullptr, &e.source);
}

void DeviceBrowser::mouseUp (const juce::MouseEvent& e)
{
    // A click (no drag) on a row adds one at the bottom: the way in when a drop misses.
    if (pressed >= 0 && ! dragged && rowAt (e.position) == pressed && onAdd && available (*rows[(size_t) pressed]))
        onAdd (*rows[(size_t) pressed], e.mods.isShiftDown());
    pressed = -1;
    dragged = false;
    repaint();
}

juce::String DeviceBrowser::getTooltip() { return helpTextAt (getMouseXYRelative().toFloat()); }

juce::String DeviceBrowser::helpTextAt (juce::Point<float> p) const
{
    if (closed)
        return {};
    if (juce::Rectangle<float> (14.0f, 36.0f, (float) getWidth() - 28.0f, 16.0f).contains (p))
        return "Drag a device into the rack, or click its card to add one at the bottom";
    for (size_t i = 0; i < rows.size(); ++i)
        if (rowRects[i].withTrimmedTop (27.0f).withHeight (15.0f).contains (p))
            return juce::String::fromUTF8 (rows[i]->name) + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String::fromUTF8 (rows[i]->line);
    if (helpRect.contains (p))
        return helpText().replace ("\n\n", " ");
    return {};
}

bool DeviceBrowser::isInterestedInDragSource (const SourceDetails& d) { return d.description.toString().startsWith ("move:"); }
void DeviceBrowser::itemDragEnter (const SourceDetails&) { dropHover = true; repaint(); }
void DeviceBrowser::itemDragExit (const SourceDetails&) { dropHover = false; repaint(); }

void DeviceBrowser::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    repaint();
    if (onRemoveDrop)
        onRemoveDrop (d.description.toString());
}

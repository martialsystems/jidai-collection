// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "RoninPanel.h"
#include "BinaryData.h"
#include "UI/Meter.h"

#include <cmath>

namespace {

// ---- Painted exactly as RONIN paints them: copied from RONIN Source/UI/PatchBayView.cpp (0c9cf8a), ----
// ---- paintExtInHold through loadPanelBackground, with the plate read from this binary's data.      ----

struct CablePaint {
    juce::Colour base;
    juce::Colour dark;
    juce::Colour light;
};

CablePaint paintFor (int color)
{
    switch (color)
    {
        case 1: return { juce::Colour (0xffece8da), juce::Colour (0xff86836f), juce::Colour (0xffffffff) };
        case 2: return { juce::Colour (0xffeabd2c), juce::Colour (0xff80600a), juce::Colour (0xfffff2a8) };
        case 3: return { juce::Colour (0xff33a352), juce::Colour (0xff10521f), juce::Colour (0xff98e6aa) };
        default: return { juce::Colour (0xffd23a30), juce::Colour (0xff6e100c), juce::Colour (0xffff9a8a) };
    }
}

// Momentary square key. The HOLD legend and the lamp bezel are already drawn on the plate.
// Mouse down holds the gate. Mouse up releases it. The cap sinks and the red lamp above it lights while down,
// the same lamp BUSHIDO shows under START.
void paintExtInHold (juce::Graphics& g, juce::Point<float> origin, float scale, bool held)
{
    const float lx = origin.x + kHoldLampCx * scale;
    const float ly = origin.y + kHoldLampCy * scale;
    const float lr = kHoldLampR * scale;
    if (held)
    {
        g.setColour (juce::Colour (0x55ff3b2b));
        g.fillEllipse (lx - lr * 2.2f, ly - lr * 2.2f, lr * 4.4f, lr * 4.4f);
    }
    g.setColour (held ? juce::Colour (0xffff4a36) : juce::Colour (0xff4a0c08));
    g.fillEllipse (lx - lr, ly - lr, 2.0f * lr, 2.0f * lr);

    const float sink = held ? 1.6f * scale : 0.0f;
    const float cx = origin.x + kHoldCx * scale;
    const float cy = origin.y + kHoldCy * scale + sink;

    const float cap = 26.0f * scale;
    const float x = cx - 13.0f * scale;
    const float y = cy - 13.0f * scale;
    juce::ColourGradient shell (juce::Colour (0xfff4eedc), x, y, juce::Colour (0xffb3ab94), x + cap, y + cap, false);
    g.setGradientFill (shell);
    g.fillRoundedRectangle (x, y, cap, cap, 3.0f * scale);
    g.setColour (juce::Colour (0xff6f6a5a));
    g.drawRoundedRectangle (x, y, cap, cap, 3.0f * scale, 0.9f * scale);

    g.setColour (juce::Colour (0xff7e7764).withAlpha (0.55f));
    g.fillRoundedRectangle (x, cy + 9.0f * scale, cap, 4.0f * scale, 2.0f * scale);

    const float faceX = cx - 9.5f * scale;
    const float faceY = cy - 10.0f * scale;
    const float faceW = 19.0f * scale;
    const float faceH = 17.0f * scale;
    const float gx = faceX + faceW * 0.45f;
    const float gy = faceY + faceH * 0.40f;
    juce::ColourGradient face (juce::Colour (0xfff8f3e4), gx, gy, juce::Colour (0xffd0c8b2), gx + 0.8f * faceW, gy, true);
    g.setGradientFill (face);
    g.fillRoundedRectangle (faceX, faceY, faceW, faceH, 2.5f * scale);
}

void paintEffectRocker (juce::Graphics& g, juce::Point<float> origin, float scale, bool on)
{
    const float x = origin.x + kPowerX * scale;
    const float y = origin.y + kPowerY * scale;
    const float w = kPowerW * scale;
    const float h = kPowerH * scale;
    const float half = w * 0.5f;
    const auto raised = juce::Colour (0xff3a3a3e);
    const auto pressed = juce::Colour (0xff101012);
    // EFFECT rocker, not power. Off is dry, on is wet. The graph keeps running either way.
    // Printed OFF is left of the rocker and ON is right of it.
    // The raised end points at the active word. Right half stays the ON click.
    const bool raisedOnRight = on;
    g.setColour (raisedOnRight ? pressed : raised);
    g.fillRoundedRectangle (x, y, half, h, 2.5f * scale);
    g.setColour (raisedOnRight ? raised : pressed);
    g.fillRoundedRectangle (x + half, y, half, h, 2.5f * scale);

    g.setColour (juce::Colour (0xff77777c));
    const float lineLeft = raisedOnRight ? x + half + 2.0f * scale : x + 2.0f * scale;
    const float lineRight = raisedOnRight ? x + w - 2.0f * scale : x + half - 2.0f * scale;
    g.drawLine (lineLeft, y + 1.2f * scale, lineRight, y + 1.2f * scale, 1.0f * scale);
    g.setColour (juce::Colours::black);
    g.drawLine (x + half, y + scale, x + half, y + h - scale, 1.0f * scale);
}

void paintKnobCap (juce::Graphics& g, juce::Point<float> centre, float radius, float scale, bool isSwitch, float value)
{
    // Ticks stay on the plate. The cap, its shadow, and the pointer turn as one piece.
    const float angleDeg = panelKnobAngleDegrees (isSwitch, value);
    const float angle = juce::degreesToRadians (angleDeg);
    const auto turn = juce::AffineTransform::rotation (angle, centre.x, centre.y);

    const float shadowR = radius + 4.0f * scale;
    juce::Point<float> shadowCentre (centre.x + scale, centre.y + 2.0f * scale);
    shadowCentre.applyTransform (turn);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (shadowCentre.x - shadowR, shadowCentre.y - shadowR, shadowR * 2.0f, shadowR * 2.0f);

    const float skirt = radius + 3.0f * scale;
    g.setColour (juce::Colour (0xff08080a));
    g.fillEllipse (centre.x - skirt, centre.y - skirt, skirt * 2.0f, skirt * 2.0f);
    g.setColour (juce::Colours::black);
    g.drawEllipse (centre.x - skirt, centre.y - skirt, skirt * 2.0f, skirt * 2.0f, 0.8f * scale);

    juce::Path ring;
    const float ringRadius = radius + 1.2f * scale;
    ring.addEllipse (centre.x - ringRadius, centre.y - ringRadius, ringRadius * 2.0f, ringRadius * 2.0f);
    const float dashes[] = { 0.8f * scale, 1.6f * scale };
    juce::Path dashed;
    juce::PathStrokeType (2.4f * scale).createDashedStroke (dashed, ring, dashes, 2);
    g.setColour (juce::Colour (0xff3c3c3f).withAlpha (0.75f));
    g.fillPath (dashed);

    const float body = radius - 0.3f * scale;
    juce::Point<float> metalHi (centre.x - body, centre.y - body);
    juce::Point<float> metalLo (centre.x + body, centre.y + body);
    metalHi.applyTransform (turn);
    metalLo.applyTransform (turn);
    juce::ColourGradient metal (juce::Colour (0xff4b4b4e), metalHi.x, metalHi.y,
                                juce::Colour (0xff060607), metalLo.x, metalLo.y, false);
    metal.addColour (0.5, juce::Colour (0xff1a1a1b));
    g.setGradientFill (metal);
    g.fillEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f);
    g.setColour (juce::Colours::black);
    g.drawEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f, 0.8f * scale);

    const float cap = radius * 0.8f;
    juce::Point<float> capHi (centre.x - cap, centre.y - cap);
    juce::Point<float> capLo (centre.x + cap, centre.y + cap);
    capHi.applyTransform (turn);
    capLo.applyTransform (turn);
    juce::ColourGradient top (juce::Colour (0xff2a2a2c), capHi.x, capHi.y,
                              juce::Colour (0xff131314), capLo.x, capLo.y, false);
    g.setGradientFill (top);
    g.fillEllipse (centre.x - cap, centre.y - cap, cap * 2.0f, cap * 2.0f);

    {
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path capDisc;
        capDisc.addEllipse (centre.x - cap, centre.y - cap, cap * 2.0f, cap * 2.0f);
        g.reduceClipRegion (capDisc);
        const float blobR = cap * 0.85f;
        juce::Point<float> blob (centre.x, centre.y + cap * 0.62f);
        blob.applyTransform (turn);
        g.setColour (juce::Colours::black.withAlpha (0.42f));
        g.fillEllipse (blob.x - blobR, blob.y - blobR, blobR * 2.0f, blobR * 2.0f);
    }

    g.setColour (juce::Colour (0xff050505));
    g.drawEllipse (centre.x - cap, centre.y - cap, cap * 2.0f, cap * 2.0f, 0.8f * scale);

    juce::Path shine;
    shine.addEllipse (-radius * 0.45f, -radius * 0.28f, radius * 0.90f, radius * 0.56f);
    shine.applyTransform (juce::AffineTransform::translation (centre.x - radius * 0.28f, centre.y - radius * 0.32f)
                              .followedBy (juce::AffineTransform::rotation (juce::degreesToRadians (-35.0f + angleDeg),
                                                                             centre.x, centre.y)));
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.fillPath (shine);

    const auto spin = juce::AffineTransform::rotation (angle, centre.x, centre.y);
    juce::Line<float> shade (centre.x + 1.3f * scale, centre.y - radius * 0.1f,
                             centre.x + 1.3f * scale, centre.y - (radius - 1.5f * scale));
    shade.applyTransform (spin);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawLine (shade, 3.0f * scale);

    g.setColour (juce::Colour (0xfff1ede0));
    juce::Line<float> pointer (centre.x, centre.y - radius * 0.1f,
                               centre.x, centre.y - (radius - 1.5f * scale));
    pointer.applyTransform (spin);
    g.drawLine (pointer, 2.4f * scale);
}

std::unique_ptr<juce::Drawable> loadPanelBackground()
{
    if (auto drawn = juce::Drawable::createFromImageData (BinaryData::panel_bg_svg, BinaryData::panel_bg_svgSize))
        return drawn;

    // The grain filter is optional. Vector wear stays if the filter rejects the parse.
    juce::String svg (reinterpret_cast<const char*> (BinaryData::panel_bg_svg),
                      static_cast<size_t> (BinaryData::panel_bg_svgSize));
    for (;;)
    {
        const int open = svg.indexOfIgnoreCase ("<filter");
        if (open < 0)
            break;
        const int close = svg.indexOfIgnoreCase (open, "</filter>");
        if (close < 0)
            break;
        svg = svg.substring (0, open) + svg.substring (close + 9);
    }

    if (auto xml = juce::parseXML (svg))
        return juce::Drawable::createFromSVG (*xml);
    return {};
}

// ---- end of RONIN's paint code ----

struct NullBinding : RackPanel::Binding {
    float get (const juce::String&) override { return 0.0f; }
    void set (const juce::String&, float) override {}
    void press (const juce::String&, bool) override {}
    float indicator (const juce::String&) override { return 0.0f; }
};

NullBinding& nullBinding()
{
    static NullBinding binding;
    return binding;
}

constexpr int kSwatches = 4;

}

PanelLayout RoninPanel::makeLayout (const juce::String& rackId)
{
    PanelLayout L;
    L.rack = rackId;
    L.width = kWidth;
    L.height = kHeight;
    for (int i = 0; i < kPanelJackCount; ++i)
    {
        const PanelJackRec& j = kPanelJacks[i];
        PanelLayout::Jack jack;
        jack.id = juce::String::fromUTF8 (j.section) + ":" + juce::String::fromUTF8 (j.label);
        jack.dir = j.dir == 1 ? "out" : "in";
        jack.x = j.x;
        jack.y = j.y;
        jack.r = 9.0f;
        jack.hit = { j.x - 11.0f, j.y - 11.0f, 22.0f, 22.0f };
        L.jacks.push_back (jack);
    }
    for (int i = 0; i < kPanelLabelCount; ++i)
        L.labels.push_back ({ kPanelLabels[i].x, kPanelLabels[i].y, kPanelLabels[i].w, kPanelLabels[i].h });
    return L;
}

RoninPanel::RoninPanel (jidai::RoninDevice& d, std::function<int()> getColour, std::function<void (int)> set)
    : RackPanel (makeLayout (juce::String (d.rackId())), nullptr, nullBinding()),
      ronin (d), colour (std::move (getColour)), setColour (std::move (set)), plate (loadPanelBackground())
{
}

RoninPanel::~RoninPanel() = default;

// RONIN's plate has the PRESET glass and its dropdown key, and no bank lamps or SAVE key.
// These sit where BUSHIDO's do: both screens share one x and y on their own panel.
// PatternScreen, above this panel, paints the lamp bulbs and the LCD text.
static void paintBankFurniture (juce::Graphics& g, float s)
{
    auto socket = [&] (float cx, float cy)
    {
        const float r = 6.5f * s;
        const float x = (cx - 6.5f) * s;
        const float y = (cy - 6.5f) * s;
        g.setColour (juce::Colour (0xff050505));
        g.fillEllipse (x, y, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colour (0xff2a2a2c));
        g.drawEllipse (x, y, r * 2.0f, r * 2.0f, juce::jmax (0.6f, s));
    };
    socket (968.0f, 27.0f);
    socket (1000.0f, 27.0f);

    g.setColour (juce::Colour (0xffdcd6c2));
    g.setFont (juce::Font (juce::FontOptions (8.0f * s)));
    g.drawText ("A", juce::Rectangle<float> (976.0f * s, 22.0f * s, 10.0f * s, 10.0f * s), juce::Justification::centred, false);
    g.drawText ("B", juce::Rectangle<float> (1008.0f * s, 22.0f * s, 10.0f * s, 10.0f * s), juce::Justification::centred, false);
    g.drawText ("SAVE", juce::Rectangle<float> (1062.0f * s, 19.0f * s, 40.0f * s, 12.0f * s), juce::Justification::centredLeft, false);

    const float x = 1032.0f * s, y = 15.0f * s, w = 26.0f * s, h = 24.0f * s;
    juce::ColourGradient shell (juce::Colour (0xfff4eedc), x, y, juce::Colour (0xffa9a18a), x + w, y + h, false);
    g.setGradientFill (shell);
    g.fillRoundedRectangle (x, y, w, h, 3.0f * s);
    g.setColour (juce::Colour (0xff6f6a5a));
    g.drawRoundedRectangle (x, y, w, h, 3.0f * s, 0.9f * s);
    g.setColour (juce::Colour (0xff7e7764).withAlpha (0.55f));
    g.fillRoundedRectangle (x, y + 20.0f * s, w, 4.0f * s, 2.0f * s);
    g.setColour (juce::Colour (0xff2a2620));
    g.fillRect (1044.0f * s, 21.0f * s, 2.0f * s, 10.0f * s);
    g.fillRect (1040.0f * s, 25.0f * s, 10.0f * s, 2.0f * s);
}

// RONIN's PatchBayView::paint, without the cables (the rack's cable layer draws those).
void RoninPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff101012));
    const juce::Point<float> origin;
    const float s = scale();
    const auto area = juce::Rectangle<float> (0.0f, 0.0f, kPanelW * s, kPanelH * s);
    if (plate != nullptr)
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area.toNearestInt());
        plate->draw (g, 1.0f, juce::AffineTransform::scale (s, s));
    }

    auto screenPoint = [s] (float x, float y) { return juce::Point<float> (x * s, y * s); };

    {
        // Printed ticks are the VU face, from -20 to +3. The needle is the selected jack: ±5 V across that arc.
        const float unit = Meter::needle (ronin.meterVolts());
        const float angleDeg = kMeterMinAngle + (kMeterMaxAngle - kMeterMinAngle) * (unit + 1.0f) * 0.5f;
        const float angle = juce::degreesToRadians (angleDeg);
        const float hub = 17.0f * kMeterScale;
        const float reach = kMeterRadius + 5.0f * kMeterScale;
        const auto face = juce::Rectangle<float> (kMeterFaceX * s, kMeterFaceY * s, kMeterFaceW * s, kMeterFaceH * s);
        auto polar = [&] (float radius) { return screenPoint (kMeterPivotX + radius * std::sin (angle), kMeterPivotY - radius * std::cos (angle)); };
        const auto start = polar (hub);
        const auto tip = polar (reach);
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (face.toNearestInt());
        g.setColour (juce::Colour (0xff141414));
        g.drawLine (start.x, start.y, tip.x, tip.y, 1.3f * kMeterScale * s);
        juce::ColourGradient glass (juce::Colour (0xfffff8e0).withAlpha (0.18f), face.getX(), face.getY(),
                                    juce::Colour (0xffe8c870).withAlpha (0.04f), face.getRight(), face.getBottom(), false);
        g.setGradientFill (glass);
        g.fillRect (face);
    }

    for (int i = 0; i < kPanelKnobCount; ++i)
    {
        const PanelKnobRec& knob = kPanelKnobs[i];
        paintKnobCap (g, screenPoint (knob.cx, knob.cy), knob.radius * s, s, knob.kind == 1, ronin.knob (i));
    }

    paintExtInHold (g, origin, s, ronin.holdHeld());

    const int current = colour ? colour() : 0;
    for (int i = 0; i < kSwatches; ++i)
    {
        const auto centre = screenPoint (200.0f + (float) i * 22.0f, 22.0f);
        const float radius = 8.0f * s;
        const CablePaint colors = paintFor (i);
        g.setColour (colors.base);
        g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
        if (i == current)
        {
            g.setColour (juce::Colours::white);
            g.drawEllipse (centre.x - radius - 2.0f * s, centre.y - radius - 2.0f * s, (radius + 2.0f * s) * 2.0f, (radius + 2.0f * s) * 2.0f, 1.5f * s);
        }
    }

    paintEffectRocker (g, origin, s, ronin.effectOn());
    paintBankFurniture (g, s);

    if (readout.isNotEmpty())
    {
        g.setColour (juce::Colour (0xffd8d2bd));
        g.setFont (juce::Font (juce::FontOptions (13.0f * s)));
        const auto textOrigin = screenPoint (24.0f, 600.0f);
        g.drawText (readout, juce::Rectangle<float> (textOrigin.x, textOrigin.y, 1000.0f * s, 20.0f * s), juce::Justification::centredLeft, true);
    }
}

int RoninPanel::knobAt (juce::Point<float> p) const
{
    for (int i = 0; i < kPanelKnobCount; ++i)
    {
        const PanelKnobRec& knob = kPanelKnobs[i];
        if (p.x >= knob.hitX && p.x <= knob.hitX + knob.hitW && p.y >= knob.hitY && p.y <= knob.hitY + knob.hitH)
            return i;
    }
    return -1;
}

void RoninPanel::setKnobValue (int index, float value)
{
    if (index < 0 || index >= kPanelKnobCount)
        return;
    const PanelKnobRec& knob = kPanelKnobs[index];
    ronin.setKnob (index, value);
    const float shown = ronin.knob (index);
    if (knob.kind == 1)
        readout = juce::String (knob.section) + juce::String::fromUTF8 (" \xc2\xb7 \xc3\xb7 ") + (shown < 0.5f ? "2" : "4");
    else
        readout = juce::String (knob.section) + juce::String::fromUTF8 (" \xc2\xb7 ") + knob.label + " " + juce::String (shown, 2);
    repaint();
}

// RONIN's PatchBayView mouse rules for the controls: swatches, EFFECT rocker, HOLD key, knobs.
// The PRESET screen, its key, the bank lamps and SAVE belong to the PatternScreen above this panel.
void RoninPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto p = design (e.position);
    for (int i = 0; i < kSwatches; ++i)
    {
        if (std::hypot (p.x - (200.0f + (float) i * 22.0f), p.y - 22.0f) <= 11.0f)
        {
            if (setColour)
                setColour (i);
            repaint();
            return;
        }
    }

    const float pad = 8.0f;
    if (p.x >= kPowerX - pad && p.x <= kPowerX + kPowerW + pad && p.y >= kPowerY - pad && p.y <= kPowerY + kPowerH + pad)
    {
        // Left half is printed OFF (dry). Right half is printed ON (wet).
        effectPress = true;
        ronin.setEffectOn (p.x >= kPowerX + kPowerW * 0.5f);
        repaint();
        return;
    }

    if (p.x >= kHoldHitX - 4.0f && p.x <= kHoldHitX + kHoldHitW + 4.0f && p.y >= kHoldHitY - 4.0f && p.y <= kHoldHitY + kHoldHitH + 4.0f)
    {
        holdPress = true;
        ronin.setHold (true);
        repaint();
        return;
    }

    const int knob = knobAt (p);
    if (knob >= 0)
    {
        knobDrag = knob;
        knobMoved = false;
        suppressSwitchStep = false;
        dragStartY = e.position.y;
        dragStartValue = ronin.knob (knob);
    }
}

void RoninPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (knobDrag < 0)
        return;
    const float deltaUp = dragStartY - e.position.y;
    if (std::fabs (deltaUp) > 3.0f)
        knobMoved = true;
    setKnobValue (knobDrag, panelKnobDrag (dragStartValue, deltaUp, e.mods.isShiftDown(), kPanelKnobs[knobDrag].kind == 1));
}

void RoninPanel::mouseUp (const juce::MouseEvent&)
{
    if (knobDrag >= 0)
    {
        if (! suppressSwitchStep && ! knobMoved && kPanelKnobs[knobDrag].kind == 1)
            setKnobValue (knobDrag, panelKnobSwitchClick (ronin.knob (knobDrag)));
        knobDrag = -1;
        return;
    }
    if (holdPress)
    {
        holdPress = false;
        ronin.setHold (false);
        repaint();
    }
    effectPress = false;
}

void RoninPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int index = knobAt (design (e.position));
    if (index < 0)
        return;
    suppressSwitchStep = true;
    setKnobValue (index, kPanelKnobs[index].valueDefault);
}

void RoninPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (knobDrag >= 0)
        return;
    const int index = knobAt (design (e.position));
    if (index < 0)
    {
        Component::mouseWheelMove (e, wheel);      // let the rack scroll
        return;
    }
    setKnobValue (index, panelKnobFromWheel (ronin.knob (index), wheel.deltaY, wheel.isReversed, e.mods.isShiftDown(), kPanelKnobs[index].kind == 1));
}

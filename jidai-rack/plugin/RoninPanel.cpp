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

const int* lcdRows (juce::juce_wchar ch)
{
    struct Glyph
    {
        char ch;
        int row[7];
    };
    static constexpr int kBlank[7] = { 0, 0, 0, 0, 0, 0, 0 };
    static constexpr Glyph kFont[] = {
        { ' ', { 0, 0, 0, 0, 0, 0, 0 } },
        { '0', { 14, 17, 19, 21, 25, 17, 14 } },
        { '1', { 4, 12, 4, 4, 4, 4, 14 } },
        { '2', { 14, 17, 1, 2, 4, 8, 31 } },
        { '3', { 31, 2, 4, 2, 1, 17, 14 } },
        { '4', { 2, 6, 10, 18, 31, 2, 2 } },
        { '5', { 31, 16, 30, 1, 1, 17, 14 } },
        { '6', { 6, 8, 16, 30, 17, 17, 14 } },
        { '7', { 31, 1, 2, 4, 8, 8, 8 } },
        { '8', { 14, 17, 17, 14, 17, 17, 14 } },
        { '9', { 14, 17, 17, 15, 1, 2, 12 } },
        { 'A', { 14, 17, 17, 17, 31, 17, 17 } },
        { 'B', { 30, 17, 17, 30, 17, 17, 30 } },
        { 'C', { 14, 17, 16, 16, 16, 17, 14 } },
        { 'D', { 28, 18, 17, 17, 17, 18, 28 } },
        { 'E', { 31, 16, 16, 30, 16, 16, 31 } },
        { 'F', { 31, 16, 16, 30, 16, 16, 16 } },
        { 'G', { 14, 17, 16, 23, 17, 17, 15 } },
        { 'H', { 17, 17, 17, 31, 17, 17, 17 } },
        { 'I', { 14, 4, 4, 4, 4, 4, 14 } },
        { 'J', { 7, 2, 2, 2, 2, 18, 12 } },
        { 'K', { 17, 18, 20, 24, 20, 18, 17 } },
        { 'L', { 16, 16, 16, 16, 16, 16, 31 } },
        { 'M', { 17, 27, 21, 21, 17, 17, 17 } },
        { 'N', { 17, 17, 25, 21, 19, 17, 17 } },
        { 'O', { 14, 17, 17, 17, 17, 17, 14 } },
        { 'P', { 30, 17, 17, 30, 16, 16, 16 } },
        { 'Q', { 14, 17, 17, 17, 21, 18, 13 } },
        { 'R', { 30, 17, 17, 30, 20, 18, 17 } },
        { 'S', { 15, 16, 16, 14, 1, 1, 30 } },
        { 'T', { 31, 4, 4, 4, 4, 4, 4 } },
        { 'U', { 17, 17, 17, 17, 17, 17, 14 } },
        { 'V', { 17, 17, 17, 17, 17, 10, 4 } },
        { 'W', { 17, 17, 17, 21, 21, 21, 10 } },
        { 'X', { 17, 17, 10, 4, 10, 17, 17 } },
        { 'Y', { 17, 17, 17, 10, 4, 4, 4 } },
        { 'Z', { 31, 1, 2, 4, 8, 16, 31 } },
        { '&', { 12, 18, 20, 8, 21, 18, 13 } },
        { '-', { 0, 0, 0, 31, 0, 0, 0 } },
        { '+', { 0, 4, 4, 31, 4, 4, 0 } },
        { '/', { 0, 1, 2, 4, 8, 16, 0 } },
        { '.', { 0, 0, 0, 0, 0, 12, 12 } },
        { '>', { 8, 4, 2, 1, 2, 4, 8 } },
    };

    const char ascii = (ch >= 32 && ch < 127) ? static_cast<char> (ch) : ' ';
    for (const auto& glyph : kFont)
        if (glyph.ch == ascii)
            return glyph.row;
    return kBlank;
}

juce::String presetScreenLine (int index, const juce::String& hostName)
{
    static const char* kShort[] = {
        "DRY", "NOISE MIXER", "VOICE", "RING", "S&H", "FEEDBACK", "HOLD",
        "FILTER LOOP", "MG FILTER", "STEP CUTOFF", "RING DRONE", "DELAY BOUNCE", "SELF RING"
    };
    const int shorts = static_cast<int> (sizeof (kShort) / sizeof (kShort[0]));
    const juce::String name = (index >= 0 && index < shorts) ? juce::String (kShort[index]) : hostName.toUpperCase();
    return (juce::String (index + 1).paddedLeft ('0', 2) + " " + name).substring (0, kPresetChars);
}

void paintLcdDots (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text,
                   juce::Colour ink, float ghostAlpha)
{
    const float pitch = juce::jmin (area.getWidth() / (static_cast<float> (kPresetChars) * 6.0f),
                                     area.getHeight() / 8.0f);
    const float dot = pitch * 0.86f;
    const float ox = area.getX() + (area.getWidth() - static_cast<float> (kPresetChars) * 6.0f * pitch) * 0.5f
                     + pitch * 0.5f;
    const float oy = area.getY() + (area.getHeight() - 7.0f * pitch) * 0.5f;
    for (int column = 0; column < kPresetChars; ++column)
    {
        const juce::juce_wchar ch = column < text.length() ? text[column] : static_cast<juce::juce_wchar> (' ');
        const int* rows = lcdRows (ch);
        for (int row = 0; row < 7; ++row)
        {
            for (int bit = 0; bit < 5; ++bit)
            {
                const bool on = ((rows[row] >> (4 - bit)) & 1) != 0;
                g.setColour (ink.withAlpha (on ? 0.9f : ghostAlpha));
                g.fillRect (ox + (static_cast<float> (column) * 6.0f + static_cast<float> (bit)) * pitch,
                            oy + static_cast<float> (row) * pitch,
                            dot, dot);
            }
        }
    }
}

juce::Rectangle<float> presetMenuDesign (int count)
{
    return { kPresetBezelX,
             kPresetBezelY + kPresetBezelH + 3.0f,
             (kPresetKeyX + kPresetKeyW) - kPresetBezelX,
             10.0f + static_cast<float> (count) * 21.0f - 3.0f };
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

void RoninPanel::paintLcd (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, juce::Colour ink, float ghostAlpha)
{
    paintLcdDots (g, area, text, ink, ghostAlpha);
}

juce::String RoninPanel::programLine (int index)
{
    return presetScreenLine (index, factoryPresetName (index));
}

juce::Rectangle<float> RoninPanel::programMenuDesign (int count)
{
    return presetMenuDesign (count);
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
    const int program = ronin.program();
    const auto lcd = juce::Rectangle<float> ((kPresetLcdX + 2.0f) * s, (kPresetLcdY + 1.0f) * s, (kPresetLcdW - 4.0f) * s, (kPresetLcdH - 2.0f) * s);
    paintLcdDots (g, lcd, presetScreenLine (program, factoryPresetName (program)), juce::Colour (0xff1e2419), 0.09f);

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

// RONIN's PatchBayView mouse rules for the controls: swatches, PRESET screen, EFFECT rocker, HOLD key, knobs.
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

    const float left = std::min (kPresetBezelX, kPresetKeyX) - 6.0f;
    const float top = std::min (kPresetBezelY, kPresetKeyY) - 6.0f;
    const float right = std::max (kPresetBezelX + kPresetBezelW, kPresetKeyX + kPresetKeyW) + 6.0f;
    const float bottom = std::max (kPresetBezelY + kPresetBezelH, kPresetKeyY + kPresetKeyH) + 8.0f;
    if (p.x >= left && p.x <= right && p.y >= top && p.y <= bottom)
    {
        if (onOpenPrograms)
            onOpenPrograms();
        return;
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

// ---------------- PRESET list ----------------

RoninProgramList::RoninProgramList (RoninPanel& p, std::function<void (int)> c, std::function<void()> x)
    : panel (p), choose (std::move (c)), close (std::move (x))
{
    hi = panel.device().program();
    setWantsKeyboardFocus (true);
}

juce::Rectangle<float> RoninProgramList::menuBounds() const
{
    // Design units of the panel, placed where the panel sits inside this component's parent.
    const float s = (float) panel.getWidth() / RoninPanel::kWidth;
    const auto box = RoninPanel::programMenuDesign (kFactoryPresetCount);
    const auto origin = panel.getBounds().getPosition().toFloat() - getPosition().toFloat();
    return { origin.x + box.getX() * s, origin.y + box.getY() * s, box.getWidth() * s, box.getHeight() * s };
}

int RoninProgramList::rowAt (juce::Point<float> p) const
{
    const auto m = menuBounds();
    if (! m.contains (p))
        return -1;
    const float s = (float) panel.getWidth() / RoninPanel::kWidth;
    const int row = (int) std::floor ((p.y - m.getY() - 5.0f * s) / (21.0f * s));
    return row >= 0 && row < kFactoryPresetCount ? row : -1;
}

void RoninProgramList::paint (juce::Graphics& g)
{
    const float s = (float) panel.getWidth() / RoninPanel::kWidth;
    const auto menu = menuBounds();
    g.setColour (juce::Colour (0xff0a0a0b));
    g.fillRoundedRectangle (menu, 4.0f * s);
    g.setColour (juce::Colour (0xffc29f4c));
    g.drawRoundedRectangle (menu, 4.0f * s, 1.2f * s);
    const int current = panel.device().program();
    for (int row = 0; row < kFactoryPresetCount; ++row)
    {
        const bool isHi = row == hi;
        const auto rowRect = juce::Rectangle<float> (menu.getX() + 5.0f * s, menu.getY() + (5.0f + (float) row * 21.0f) * s, menu.getWidth() - 10.0f * s, 18.0f * s);
        if (isHi)
        {
            g.setColour (juce::Colour (0xff1e2419));
            g.fillRoundedRectangle (rowRect, 1.5f * s);
        }
        else
        {
            juce::ColourGradient glass (juce::Colour (0xff8f9a7c), rowRect.getX(), rowRect.getY(), juce::Colour (0xff94a083), rowRect.getX(), rowRect.getBottom(), false);
            glass.addColour (0.5, juce::Colour (0xffa6b192));
            g.setGradientFill (glass);
            g.fillRoundedRectangle (rowRect, 1.5f * s);
        }
        const juce::String shown = (juce::String (row == current ? ">" : " ") + RoninPanel::programLine (row)).substring (0, kPresetChars);
        RoninPanel::paintLcd (g, rowRect.reduced (3.0f * s, 1.0f * s), shown, isHi ? juce::Colour (0xffa6b192) : juce::Colour (0xff1e2419), isHi ? 0.08f : 0.09f);
    }
}

void RoninProgramList::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAt (e.position);
    if (row >= 0 && row != hi)
    {
        hi = row;
        repaint();
    }
}

void RoninProgramList::mouseDown (const juce::MouseEvent& e)
{
    const int row = rowAt (e.position);
    if (row >= 0)
        choose (row);
    else
        close();
}

bool RoninProgramList::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
        close();
    else if (key == juce::KeyPress::upKey && hi > 0)
        --hi;
    else if (key == juce::KeyPress::downKey && hi + 1 < kFactoryPresetCount)
        ++hi;
    else if (key == juce::KeyPress::returnKey)
        choose (hi);
    repaint();
    return true;
}

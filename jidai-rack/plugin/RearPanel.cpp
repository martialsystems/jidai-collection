// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "RearPanel.h"
#include "RackStyle.h"

using namespace jidai;
using namespace rackstyle;

namespace {
constexpr float kW = 1600.0f, kPad = 14.0f, kGap = 10.0f, kTitleH = 20.0f;
constexpr int kMaxCols = 8;

juce::String jackLabel (const std::string& id)
{
    const auto s = juce::String::fromUTF8 (id.c_str());
    return s.containsChar (':') ? s.fromFirstOccurrenceOf (":", false, false) : s;
}
}

const char* RearPanel::kindLine (DeviceKind k)
{
    switch (k)
    {
        case DeviceKind::Bushido: return "12-STEP SEQUENCER";
        case DeviceKind::Ronin: return "SEMI-MODULAR VOICE";
        case DeviceKind::Origami: return "TRIPLE WAVE SHAPER FX";
        case DeviceKind::RackIO: return "HOST AUDIO \xc2\xb7 MIDI \xc2\xb7 TRANSPORT";
        case DeviceKind::Shogun: return "DRUM MACHINE \xc2\xb7 14 DRUMS + 2 SYNTHS";
    }
    return "";
}

RearPanel::RearPanel (Device& d, float h, std::function<int()> lat) : device (d), designH (h), latency (std::move (lat))
{
    setInterceptsMouseClicks (false, false);
    layout();
}

void RearPanel::layout()
{
    boxes.clear();
    for (auto& g : device.jackGroups())
        if (! g.jacks.empty())
            boxes.push_back ({ juce::String (g.title), {}, g.jacks });
    centres.assign (device.jacks().size(), {});
    compact = designH < 200.0f;
    if (compact)
    {
        sticker = { kPad, 12.0f, 230.0f, designH - 24.0f };
        legend = latPlate = {};
    }
    else
    {
        sticker = { kPad, 10.0f, 380.0f, 50.0f };
        latPlate = { kW - kPad - 170.0f, 10.0f, 170.0f, 50.0f };
        legend = { sticker.getRight() + 20.0f, 10.0f, latPlate.getX() - sticker.getRight() - 40.0f, 50.0f };
    }
    // Shrink the cells until every box fits on the plate.
    for (float f = 1.0f; f > 0.3f; f -= 0.04f)
        if (layoutPass (64.0f * f, 56.0f * f, false))
        {
            socketR = 11.0f * juce::jmin (1.0f, f * 1.05f);
            labelSize = 10.5f * juce::jmin (1.0f, f * 1.1f);
            layoutPass (64.0f * f, 56.0f * f, true);
            return;
        }
    socketR = 5.0f;
    labelSize = 7.0f;
    layoutPass (64.0f * 0.3f, 56.0f * 0.3f, true);
}

bool RearPanel::layoutPass (float cw, float ch, bool place)
{
    const float left = compact ? sticker.getRight() + 16.0f : kPad;
    const float top = compact ? 10.0f : 72.0f;
    const float bottom = designH - 8.0f;
    float x = left, y = top, rowH = 0.0f;
    for (auto& b : boxes)
    {
        const int n = (int) b.jacks.size();
        const int cols = juce::jmin (n, kMaxCols);
        const int rows = (n + cols - 1) / cols;
        const float bw = juce::jmax ((float) cols * cw + 12.0f, 9.0f * (float) b.title.length() + 16.0f);
        const float bh = compact ? bottom - top : kTitleH + (float) rows * ch + 6.0f;
        if (x + bw > kW - kPad && x > left)
        {
            if (compact)
                return false;
            x = left;
            y += rowH + kGap;
            rowH = 0.0f;
        }
        if (x + bw > kW - kPad || y + bh > bottom)
            return false;
        if (place)
        {
            b.r = { x, y, bw, bh };
            const float gridW = (float) cols * cw;
            const float x0 = x + (bw - gridW) * 0.5f;
            const float y0 = compact ? y + kTitleH + (bh - kTitleH - (float) rows * ch) * 0.5f : y + kTitleH;
            for (int i = 0; i < n; ++i)
            {
                const int c = i % cols, r = i / cols;
                const int j = b.jacks[(size_t) i];
                if (j >= 0 && j < (int) centres.size())
                    centres[(size_t) j] = { x0 + ((float) c + 0.5f) * cw, y0 + (float) r * ch + ch * 0.36f };
            }
        }
        x += bw + kGap;
        rowH = juce::jmax (rowH, bh);
    }
    return true;
}

juce::Point<float> RearPanel::jackCentre (int jack) const
{
    if (jack < 0 || jack >= (int) centres.size())
        return {};
    return centres[(size_t) jack] * scale();
}

void RearPanel::paint (juce::Graphics& g)
{
    const float s = scale();
    const auto R = [s] (juce::Rectangle<float> r) { return juce::Rectangle<float> (r.getX() * s, r.getY() * s, r.getWidth() * s, r.getHeight() * s); };
    const auto bounds = getLocalBounds().toFloat();

    // The plate: dark powder-coated steel.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff232327), 0, 0, juce::Colour (0xff17171a), 0, bounds.getBottom(), false));
    g.fillRect (bounds);
    juce::Random grain (device.number * 31 + (int) device.kind());
    for (int i = 0; i < 260; ++i)
    {
        g.setColour (juce::Colour (grain.nextBool() ? 0x07ffffff : 0x0a000000));
        const float gx = grain.nextFloat() * bounds.getWidth();      // sequenced: argument order is unspecified
        const float gy = grain.nextFloat() * bounds.getHeight();
        g.fillRect (gx, gy, 1.5f * s, 1.5f * s);
    }

    // Sticker.
    {
        const auto r = R (sticker);
        g.setColour (juce::Colour (0xffeae4cf));
        g.fillRoundedRectangle (r, 3.0f * s);
        g.setColour (juce::Colour (0x40000000));
        g.drawRoundedRectangle (r, 3.0f * s, 1.0f);
        g.setColour (juce::Colour (0xff1c1a16));
        g.setFont (font ((compact ? 17.0f : 19.0f) * s, true));
        auto t = r.reduced (10.0f * s, 6.0f * s);
        g.drawText (juce::String (deviceKindName (device.kind())), t.removeFromTop (22.0f * s), juce::Justification::centredLeft);
        g.setFont (font (10.0f * s));
        g.setColour (juce::Colour (0xff4a463c));
        const int n = (int) device.jacks().size();
        juce::String line = u8 (kindLine (device.kind())) + u8 (" \xc2\xb7 REAR PATCH BAY \xc2\xb7 ") + juce::String (n) + " JACKS";
        if (! device.name.empty())
            line = u8 (device.displayName().c_str()) + u8 (" \xc2\xb7 ") + line;
        g.drawFittedText (line, t.toNearestInt(), juce::Justification::topLeft, compact ? 3 : 1, 0.8f);
        if (compact)
        {
            g.setColour (juce::Colour (0xff8a6d1f));
            g.setFont (font (10.0f * s, true));
            g.drawText ("LAT " + juce::String (latency ? latency() : 0) + " smp", t, juce::Justification::bottomLeft);
        }
    }

    // Role legend and LAT plate (3 U and taller).
    if (! compact)
    {
        const auto r = R (legend);
        const float w = r.getWidth() / (float) jcs::kRoleCount;
        const jcs::Role order[] = { jcs::Role::Audio, jcs::Role::CV, jcs::Role::GateClk, jcs::Role::VOct, jcs::Role::HzvLin, jcs::Role::STrig };
        for (int i = 0; i < jcs::kRoleCount; ++i)
        {
            const auto cell = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight());
            const auto dot = juce::Rectangle<float> (16.0f * s, 16.0f * s).withCentre ({ cell.getX() + 12.0f * s, cell.getCentreY() });
            g.setColour (roleColour (order[i]));
            g.fillEllipse (dot);
            g.setColour (juce::Colour (0xff101010));
            g.setFont (font (10.0f * s, true));
            g.drawText (roleGlyph (order[i]), dot, juce::Justification::centred);
            g.setColour (kInk.withAlpha (0.85f));
            g.setFont (font (11.0f * s, true));
            g.drawText (roleName (order[i]), cell.withTrimmedLeft (24.0f * s), juce::Justification::centredLeft);
        }
        const auto lp = R (latPlate);
        g.setColour (juce::Colour (0xff0d0c08));
        g.fillRoundedRectangle (lp, 3.0f * s);
        g.setColour (kGold.withAlpha (0.6f));
        g.drawRoundedRectangle (lp, 3.0f * s, 1.0f);
        g.setColour (kAmber);
        g.setFont (font (15.0f * s, true));
        g.drawText ("LAT " + juce::String (latency ? latency() : 0) + " smp", lp.withTrimmedBottom (lp.getHeight() * 0.45f), juce::Justification::centredBottom);
        g.setFont (font (10.0f * s));
        g.setColour (kAmber.withAlpha (0.7f));
        g.drawText (u8 ("JCS v1.1 \xc2\xb7 0/5 V gates"), lp.withTrimmedTop (lp.getHeight() * 0.58f), juce::Justification::centredTop);
    }

    // Group boxes and sockets.
    for (auto& b : boxes)
    {
        const auto r = R (b.r);
        g.setColour (juce::Colour (0xff111113));
        g.fillRoundedRectangle (r, 4.0f * s);
        g.setColour (juce::Colour (0x30ffffff));
        g.drawRoundedRectangle (r, 4.0f * s, 1.0f);
        g.setColour (kInk.withAlpha (0.8f));
        g.setFont (font (11.0f * s, true));
        g.drawText (b.title.toUpperCase(), r.reduced (7.0f * s, 3.0f * s).withHeight (14.0f * s), juce::Justification::centredLeft);
        for (int j : b.jacks)
        {
            if (j < 0 || j >= (int) centres.size())
                continue;
            const auto c = centres[(size_t) j] * s;
            const float rr = socketR * s;
            const auto role = device.jackRole (j);
            paintSocket (g, c, rr, roleColour (role));
            const auto& desc = device.jacks()[(size_t) j];
            const auto label = jackLabel (desc.id);
            const auto lr = juce::Rectangle<float> (58.0f * s * juce::jmax (0.6f, socketR / 11.0f), (labelSize + 3.0f) * s)
                                .withCentre ({ c.x, c.y + rr + (labelSize * 0.5f + 5.0f) * s });
            const bool out = desc.desc.dir == PortDir::Out;
            g.setFont (font (labelSize * s, true));
            if (out)
            {
                const float tw = juce::jmin (lr.getWidth(), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 6.0f * s);
                g.setColour (juce::Colour (0xffe8e2cf));
                g.fillRoundedRectangle (lr.withSizeKeepingCentre (tw, lr.getHeight()), 2.0f * s);
                g.setColour (juce::Colour (0xff141414));
            }
            else
                g.setColour (kInk.withAlpha (0.82f));
            g.drawFittedText (label, lr.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
    }
}

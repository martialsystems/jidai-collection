// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "CompactFace.h"
#include "RackStyle.h"

using namespace rackstyle;

CompactFace::CompactFace (std::vector<Item> i, Style s) : items (std::move (i)), style (s)
{
    startTimerHz (15);
}

CompactFace::~CompactFace() = default;

juce::Rectangle<float> CompactFace::itemBounds (int index) const
{
    float total = 0.0f;
    for (auto& it : items)
        total += it.width;
    const float s = scale();
    float x = (1600.0f - total) * 0.5f;
    for (int i = 0; i < (int) items.size(); ++i)
    {
        if (i == index)
            return { x * s, 0.0f, items[(size_t) i].width * s, (float) getHeight() };
        x += items[(size_t) i].width;
    }
    return {};
}

int CompactFace::itemAt (juce::Point<float> p) const
{
    for (int i = 0; i < (int) items.size(); ++i)
        if (itemBounds (i).contains (p))
            return i;
    return -1;
}

void CompactFace::paint (juce::Graphics& g)
{
    const float s = scale();
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (style.top, 0, 0, style.bottom, 0, b.getBottom(), false));
    g.fillRect (b);
    g.setColour (juce::Colour (0x18ffffff));
    g.drawHorizontalLine (0, 0.0f, b.getRight());

    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto& it = items[(size_t) i];
        const auto r = itemBounds (i).reduced (6.0f * s, 10.0f * s);
        switch (it.type)
        {
            case Item::Title:
            {
                g.setColour (style.ink);
                g.setFont (font (22.0f * s, true));
                g.drawText (it.label, r.withHeight (r.getHeight() * 0.55f), juce::Justification::bottomLeft);
                g.setColour (style.ink.withAlpha (0.6f));
                g.setFont (font (11.0f * s));
                g.drawText (it.text ? it.text() : juce::String(), r.withTrimmedTop (r.getHeight() * 0.58f), juce::Justification::topLeft);
                break;
            }
            case Item::Knob:
            {
                const double v = it.get ? it.get() : 0.0;
                const float rad = juce::jmin (r.getWidth(), r.getHeight() - 26.0f * s) * 0.42f;
                const juce::Point<float> c (r.getCentreX(), r.getY() + rad + 8.0f * s);
                const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, rad + 4.0f * s, rad + 4.0f * s, 0.0f, a0, a1, true);
                g.setColour (juce::Colour (0x40ffffff));
                g.strokePath (arc, juce::PathStrokeType (2.0f * s));
                juce::Path val;
                val.addCentredArc (c.x, c.y, rad + 4.0f * s, rad + 4.0f * s, 0.0f, a0, a0 + (a1 - a0) * (float) juce::jlimit (0.0, 1.0, v), true);
                g.setColour (style.accent);
                g.strokePath (val, juce::PathStrokeType (2.5f * s));
                juce::ColourGradient cap (juce::Colour (0xff4a4a4e), c.x, c.y - rad, juce::Colour (0xff151517), c.x, c.y + rad, false);
                g.setGradientFill (cap);
                g.fillEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c));
                g.setColour (juce::Colour (0xff000000));
                g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c), 1.0f);
                const float a = a0 + (a1 - a0) * (float) juce::jlimit (0.0, 1.0, v) - juce::MathConstants<float>::halfPi;
                g.setColour (style.ink);
                g.drawLine ({ c, c + juce::Point<float> (std::cos (a), std::sin (a)) * rad * 0.85f }, 2.0f * s);
                g.setColour (style.ink.withAlpha (0.85f));
                g.setFont (font (10.5f * s, true));
                g.drawText (it.text ? it.text() : it.label, r.withTop (r.getBottom() - 14.0f * s), juce::Justification::centred);
                break;
            }
            case Item::Toggle:
            case Item::Momentary:
            {
                const bool on = it.get && it.get() > 0.5;
                const auto pill = juce::Rectangle<float> (juce::jmin (r.getWidth(), 86.0f * s), 26.0f * s).withCentre ({ r.getCentreX(), r.getCentreY() - 6.0f * s });
                g.setColour (on ? style.accent : juce::Colour (0xff2a2a2e));
                g.fillRoundedRectangle (pill, 4.0f * s);
                g.setColour (juce::Colour (0x50ffffff));
                g.drawRoundedRectangle (pill, 4.0f * s, 1.0f);
                g.setColour (on ? juce::Colour (0xff111111) : style.ink);
                g.setFont (font (11.0f * s, true));
                g.drawText (it.label, pill, juce::Justification::centred);
                if (it.text)
                {
                    g.setColour (style.ink.withAlpha (0.6f));
                    g.setFont (font (10.0f * s));
                    g.drawText (it.text(), r.withTop (pill.getBottom() + 4.0f * s).withHeight (14.0f * s), juce::Justification::centred);
                }
                break;
            }
            case Item::Meter:
            {
                const double v = it.get ? std::abs (it.get()) : 0.0;
                const auto bar = juce::Rectangle<float> (r.getWidth() * 0.86f, 8.0f * s).withCentre ({ r.getCentreX(), r.getCentreY() - 4.0f * s });
                g.setColour (juce::Colour (0xff0a0a0b));
                g.fillRoundedRectangle (bar.expanded (2.0f * s), 2.0f * s);
                const float f = (float) juce::jlimit (0.0, 1.0, v / 5.0);
                g.setColour (v > 5.5 ? kRefuse : juce::Colour (0xff3fcf5e));
                g.fillRect (bar.withWidth (bar.getWidth() * f));
                g.setColour (style.ink.withAlpha (0.85f));
                g.setFont (font (10.5f * s, true));
                g.drawText (it.label, bar.translated (0.0f, 14.0f * s).withHeight (14.0f * s), juce::Justification::centred);
                break;
            }
            case Item::Lcd:
            {
                const auto lcd = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 40.0f * s));
                g.setColour (juce::Colour (0xff0b0f08));
                g.fillRoundedRectangle (lcd, 3.0f * s);
                g.setColour (juce::Colour (0x40a8d860));
                g.drawRoundedRectangle (lcd, 3.0f * s, 1.0f);
                g.setColour (juce::Colour (0xffb8e070));
                g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 15.0f * s, juce::Font::bold)));
                g.drawFittedText (it.text ? it.text() : it.label, lcd.reduced (8.0f * s, 2.0f * s).toNearestInt(), juce::Justification::centredLeft, 2, 0.7f);
                break;
            }
            case Item::Lamps:
            {
                const float cell = r.getWidth() / (float) juce::jmax (1, it.lamps);
                for (int k = 0; k < it.lamps; ++k)
                {
                    const float v = it.lamp ? it.lamp (k) : 0.0f;
                    const auto c = juce::Point<float> (r.getX() + cell * ((float) k + 0.5f), r.getCentreY() - 6.0f * s);
                    g.setColour (juce::Colour (0xff200808));
                    g.fillEllipse (juce::Rectangle<float> (12.0f * s, 12.0f * s).withCentre (c));
                    g.setColour (juce::Colour (0xffff4a2e).withAlpha (juce::jlimit (0.0f, 1.0f, v)));
                    g.fillEllipse (juce::Rectangle<float> (10.0f * s, 10.0f * s).withCentre (c));
                    g.setColour (style.ink.withAlpha (0.55f));
                    g.setFont (font (9.0f * s));
                    g.drawText (juce::String (k + 1), juce::Rectangle<float> (cell, 12.0f * s).withCentre (c.translated (0.0f, 14.0f * s)), juce::Justification::centred);
                }
                g.setColour (style.ink.withAlpha (0.8f));
                g.setFont (font (10.5f * s, true));
                g.drawText (it.label, r.withTop (r.getBottom() - 14.0f * s), juce::Justification::centred);
                break;
            }
            case Item::Space:
                break;
        }
    }
}

void CompactFace::mouseDown (const juce::MouseEvent& e)
{
    drag = itemAt (e.position);
    if (drag < 0)
        return;
    auto& it = items[(size_t) drag];
    if (it.type == Item::Knob && it.get)
    {
        dragStart = it.get();
        dragY = e.position.y;
    }
    else if (it.type == Item::Toggle && it.get && it.set)
        it.set (it.get() > 0.5 ? 0.0 : 1.0);
    else if (it.type == Item::Momentary && it.set)
        it.set (1.0);
    repaint();
}

void CompactFace::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0)
        return;
    auto& it = items[(size_t) drag];
    if (it.type == Item::Knob && it.set)
    {
        const float range = 200.0f * scale() * (e.mods.isShiftDown() ? 5.0f : 1.0f);
        it.set (juce::jlimit (0.0, 1.0, dragStart + (double) (dragY - e.position.y) / juce::jmax (1.0f, range)));
        repaint();
    }
}

void CompactFace::mouseUp (const juce::MouseEvent&)
{
    if (drag >= 0 && items[(size_t) drag].type == Item::Momentary && items[(size_t) drag].set)
        items[(size_t) drag].set (0.0);
    drag = -1;
    repaint();
}

void CompactFace::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = itemAt (e.position);
    if (i >= 0 && items[(size_t) i].type == Item::Knob && items[(size_t) i].set)
    {
        items[(size_t) i].set (items[(size_t) i].defaultValue);
        repaint();
    }
}

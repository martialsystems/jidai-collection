// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// Shared look of the rack window (JIDAI_RACK_Redesign 3): ink colours, the JCS R14 role colours and glyphs, the six
// override swatches, and the rack hardware (screws, ears, sockets) drawn by the rack, the rear plates and the cables.

#include "jidai/jcs/Roles.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace rackstyle {

inline const juce::Colour kInk { 0xffe8e2cf };
inline const juce::Colour kDim { 0x99dcd6c2 };
inline const juce::Colour kGold { 0xffd9b24a };
inline const juce::Colour kAmber { 0xffeabd2c };
inline const juce::Colour kRefuse { 0xffe5483b };
inline const juce::Colour kStrip { 0xff1b1b1e };
inline const juce::Colour kPlate { 0xff1e1e21 };

inline juce::String u8 (const char* utf8) { return juce::String::fromUTF8 (utf8); }

inline juce::Colour roleColour (jidai::jcs::Role r) { return juce::Colour (jidai::jcs::roleArgb (r)); }
inline juce::String roleGlyph (jidai::jcs::Role r) { return juce::String::fromUTF8 (jidai::jcs::roleInfo (r).glyph); }
inline juce::String roleName (jidai::jcs::Role r) { return juce::String (jidai::jcs::roleInfo (r).name); }

// User override swatches (JIDAI_RACK_Redesign 3.4): red, yellow, green, blue, white, orange.
inline constexpr int kSwatchCount = 6;
inline juce::Colour swatchColour (int i)
{
    static const juce::uint32 c[kSwatchCount] = { 0xffe0322b, 0xfff2d43c, 0xff35c45a, 0xff3f7fe8, 0xffeeeeee, 0xfff08a24 };
    return juce::Colour (c[juce::jlimit (0, kSwatchCount - 1, i)]);
}
inline const char* swatchName (int i)
{
    static const char* n[kSwatchCount] = { "Red", "Yellow", "Green", "Blue", "White", "Orange" };
    return n[juce::jlimit (0, kSwatchCount - 1, i)];
}
// The colour a cable is drawn in: its override swatch, or the role colour of its source jack.
inline juce::Colour cableColour (int color, jidai::jcs::Role role) { return color >= 0 ? swatchColour (color) : roleColour (role); }

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::jmax (6.0f, size), bold ? juce::Font::bold : juce::Font::plain));
}

inline void paintScrew (juce::Graphics& g, juce::Point<float> c, float r, float angle)
{
    g.setColour (juce::Colour (0xaa000000));                                  // countersink shadow
    g.fillEllipse (juce::Rectangle<float> (r * 2.5f, r * 2.5f).withCentre (c.translated (0.0f, r * 0.15f)));
    juce::ColourGradient metal (juce::Colour (0xffe6e4dc), c.x - r * 0.6f, c.y - r * 0.7f,
                                juce::Colour (0xff5d5c58), c.x + r * 0.7f, c.y + r * 0.8f, true);
    g.setGradientFill (metal);
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    g.setColour (juce::Colour (0xff2a2a28));
    g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), juce::jmax (0.6f, r * 0.12f));
    const float len = r * 0.62f, wdt = juce::jmax (1.0f, r * 0.26f);
    for (int k = 0; k < 2; ++k)
    {
        const float a = angle + (float) k * juce::MathConstants<float>::halfPi;
        const juce::Point<float> d (std::cos (a) * len, std::sin (a) * len);
        g.setColour (juce::Colour (0xff3a3936));
        g.drawLine ({ c - d, c + d }, wdt);
    }
}

// A device's ear: dark brushed steel, with a bevel.
inline void paintEar (juce::Graphics& g, juce::Rectangle<float> r, bool left, float s)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff302e2a), left ? r.getX() : r.getRight(), r.getY(),
                                             juce::Colour (0xff191816), left ? r.getRight() : r.getX(), r.getY(), false));
    g.fillRect (r);
    juce::Random grain (left ? 7 : 11);
    for (float y = r.getY() + 1.0f; y < r.getBottom(); y += juce::jmax (1.5f, 2.2f * s))
    {
        g.setColour (juce::Colour (grain.nextBool() ? 0x0cffffff : 0x10000000));
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
    }
    g.setColour (juce::Colour (0x26ffffff));
    g.drawVerticalLine ((int) (left ? r.getX() : r.getRight() - 1.0f), r.getY(), r.getBottom());
    g.drawHorizontalLine ((int) r.getY(), r.getX(), r.getRight());
    g.setColour (juce::Colour (0xcc000000));
    g.drawHorizontalLine ((int) (r.getBottom() - 1.0f), r.getX(), r.getRight());
    g.drawVerticalLine ((int) (left ? r.getRight() - 1.0f : r.getX()), r.getY(), r.getBottom());
}

// A 3.5 mm socket with a role ring (rear plates; JCS R14: the ring colour and glyph name the role).
inline void paintSocket (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour ring)
{
    g.setColour (juce::Colour (0x99000000));
    g.fillEllipse (juce::Rectangle<float> (r * 2.3f, r * 2.3f).withCentre (c.translated (0.0f, r * 0.12f)));
    g.setColour (ring);
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    juce::ColourGradient nut (juce::Colour (0xffd8d6cf), c.x - r * 0.5f, c.y - r * 0.5f, juce::Colour (0xff6c6b67), c.x + r * 0.5f, c.y + r * 0.6f, true);
    g.setGradientFill (nut);
    g.fillEllipse (juce::Rectangle<float> (r * 1.42f, r * 1.42f).withCentre (c));
    g.setColour (juce::Colour (0xff050505));
    g.fillEllipse (juce::Rectangle<float> (r * 0.78f, r * 0.78f).withCentre (c));
}

// A small rounded label box (badges, tags, LAT plates).
inline void paintTag (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, juce::Colour text, const juce::String& s, float fontSize,
                      juce::Colour outline = juce::Colours::transparentBlack)
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, r.getHeight() * 0.3f);
    if (! outline.isTransparent())
    {
        g.setColour (outline);
        g.drawRoundedRectangle (r, r.getHeight() * 0.3f, 1.0f);
    }
    g.setColour (text);
    g.setFont (font (fontSize, true));
    g.drawFittedText (s, r.reduced (r.getHeight() * 0.25f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
}

} // namespace rackstyle

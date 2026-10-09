// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// The rack window's look for JUCE-drawn parts: popup menus and lists in a plain, readable sans font (the dot-matrix style
// stays on the small LCD readouts only), and hover tooltips that show help text enlarged (hover 0.5 s).
// Every device face in the rack inherits it, so menus and tooltips look the same across the units.

#include <juce_gui_basics/juce_gui_basics.h>

class RackLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static constexpr float kMenuTextHeight = 16.0f;      // px at 100 %
    static constexpr float kTooltipTextHeight = 20.0f;   // px: the hovered text, enlarged

    RackLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff16161b));
        setColour (juce::PopupMenu::textColourId, juce::Colour (0xffefece2));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff8f9be8));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff0b0b12));
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf00b0b14));
        setColour (juce::TooltipWindow::textColourId, juce::Colour (0xffefece2));
        setColour (juce::TooltipWindow::outlineColourId, juce::Colour (0xff8f9be8));
    }

    static juce::Font sans (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), height, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::Font getPopupMenuFont() override { return sans (kMenuTextHeight); }

    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parentArea) override
    {
        const auto tl = layout (tip);
        const int w = (int) std::ceil (tl.getWidth() + 20.0f), h = (int) std::ceil (tl.getHeight() + 12.0f);
        return juce::Rectangle<int> (pos.x > parentArea.getCentreX() ? pos.x - (w + 12) : pos.x + 24,
                                     pos.y > parentArea.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h)
            .constrainedWithin (parentArea);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& tip, int width, int height) override
    {
        const juce::Rectangle<float> r (0.0f, 0.0f, (float) width, (float) height);
        g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (findColour (juce::TooltipWindow::outlineColourId));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        layout (tip).draw (g, r.reduced (10.0f, 6.0f));
    }

private:
    juce::TextLayout layout (const juce::String& tip) const
    {
        juce::AttributedString s;
        s.setJustification (juce::Justification::centredLeft);
        s.append (tip, sans (kTooltipTextHeight, true), findColour (juce::TooltipWindow::textColourId));
        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, 640.0f);
        return tl;
    }
};

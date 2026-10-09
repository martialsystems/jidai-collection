// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "CableBar.h"
#include "RackStyle.h"

using namespace rackstyle;
namespace jc = jidai::cable;

namespace {

juce::String helpLine (size_t i)
{
    const auto lines = jc::helpLines();
    return i < lines.size() ? juce::String::fromUTF8 (lines[i].c_str()) : juce::String();
}

juce::Colour textOn (juce::Colour fill)
{
    return fill.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff111111) : juce::Colour (0xfff4f0e4);
}

}

// The larger copy of the help text, shown over the rack while the pointer rests on the help lines.
class CableBar::Zoom : public juce::Component
{
public:
    Zoom() { setInterceptsMouseClicks (false, false); }

    static float fontPx (float k) { return 18.0f * k; }

    void layout (float k, int maxWidth)
    {
        scale = k;
        const auto f = font (fontPx (k));
        float w = 0.0f;
        for (const auto& l : jc::helpLines())
            w = juce::jmax (w, juce::GlyphArrangement::getStringWidth (f, juce::String::fromUTF8 (l.c_str())));
        const int width = juce::jmin (maxWidth, (int) std::ceil (w + 40.0f * k));
        const int height = (int) std::ceil ((float) jc::helpLines().size() * fontPx (k) * 1.45f + 22.0f * k);
        setSize (width, height);
    }

    void paint (juce::Graphics& g) override
    {
        const float k = scale;
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xf4121214));
        g.fillRoundedRectangle (r, 5.0f * k);
        g.setColour (kGold.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f * k, 1.2f);
        g.setFont (font (fontPx (k)));
        auto row = r.reduced (20.0f * k, 11.0f * k);
        for (const auto& l : jc::helpLines())
        {
            g.setColour (kInk);
            g.drawFittedText (juce::String::fromUTF8 ("\xe2\x80\xa2  ") + juce::String::fromUTF8 (l.c_str()),
                              row.removeFromTop (fontPx (k) * 1.45f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
        }
    }

    float scale = 1.0f;
};

CableBar::CableBar (JidaiProcessor& p) : proc (p)
{
    setRepaintsOnMouseActivity (false);
}

CableBar::~CableBar()
{
    if (zoom != nullptr && zoom->getParentComponent() != nullptr)
        zoom->getParentComponent()->removeChildComponent (zoom.get());
}

juce::Rectangle<float> CableBar::chipBounds (int chip) const
{
    const float k = u(), y = 5.0f * k, h = 24.0f * k;
    if (chip == 0)
        return { 112.0f * k, y, 48.0f * k, h };
    if (chip < 0 || chip >= chipCount())
        return {};
    return { (164.0f + 90.0f * (float) (chip - 1)) * k, y, 86.0f * k, h };
}

juce::Rectangle<float> CableBar::helpBounds() const
{
    const float k = u();
    return { 14.0f * k, 32.0f * k, (float) getWidth() - 28.0f * k, 30.0f * k };
}

int CableBar::paletteForChip (int chip) const
{
    if (chip <= 0 || chip >= chipCount())
        return jc::kAnyRole;
    return (int) jc::kPaletteOrder[chip - 1];
}

void CableBar::pickChip (int chip)
{
    if (chip < 0 || chip >= chipCount())
        return;
    const int role = paletteForChip (chip);
    proc.cablePalette = proc.cablePalette == role ? jc::kAnyRole : role;      // a second click clears it
    repaint();
    if (onPaletteChanged)
        onPaletteChanged();
}

juce::StringArray CableBar::helpRows() const
{
    const auto dot = juce::String::fromUTF8 ("   \xc2\xb7   ");
    juce::StringArray rows;
    rows.add (helpLine (0) + dot + helpLine (1) + dot + helpLine (2) + dot + helpLine (3));
    rows.add (helpLine (4) + dot + helpLine (5) + dot + helpLine (6));
    return rows;
}

void CableBar::paint (juce::Graphics& g)
{
    const float k = u();
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff161618));
    g.fillRect (r);
    g.setColour (juce::Colour (0xff000000));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, r.getRight());

    g.setColour (kInk.withAlpha (0.8f));
    g.setFont (font (11.5f * k, true));
    g.drawText ("CABLE COLOR", juce::Rectangle<float> (14.0f * k, chipBounds (0).getY(), 96.0f * k, chipBounds (0).getHeight()),
                juce::Justification::centredLeft);

    for (int chip = 0; chip < chipCount(); ++chip)
    {
        const auto cr = chipBounds (chip);
        const int role = paletteForChip (chip);
        const bool on = proc.cablePalette == role;
        const auto col = role == jc::kAnyRole ? juce::Colour (0xff8a8a90) : roleColour ((jidai::jcs::Role) role);
        g.setColour (on ? col : (hover == chip ? juce::Colour (0xff36363b) : juce::Colour (0xff28282c)));
        g.fillRoundedRectangle (cr, 3.0f * k);
        g.setColour (on ? kGold : juce::Colour (0x40ffffff));
        g.drawRoundedRectangle (cr, 3.0f * k, on ? 2.0f : 1.0f);
        juce::String label = "ANY";
        if (role != jc::kAnyRole)
        {
            label = roleGlyph ((jidai::jcs::Role) role) + " " + roleName ((jidai::jcs::Role) role);
            if (! on)
            {
                g.setColour (col);
                g.fillRoundedRectangle (cr.withWidth (5.0f * k).reduced (0.0f, 3.0f * k).translated (3.0f * k, 0.0f), 2.0f * k);
            }
        }
        g.setColour (on ? textOn (col) : kInk);
        g.setFont (font (11.0f * k, true));
        g.drawFittedText (label, cr.reduced (8.0f * k, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.75f);
    }

    // What the pick does, right of the chips.
    const float x0 = chipBounds (chipCount() - 1).getRight() + 14.0f * k;
    const auto status = juce::Rectangle<float> (x0, chipBounds (0).getY(), juce::jmax (0.0f, r.getRight() - x0 - 14.0f * k), chipBounds (0).getHeight());
    juce::String s;
    if (proc.cablePalette == jc::kAnyRole)
        s = "No color picked: every jack is lit.";
    else
        s = roleName ((jidai::jcs::Role) proc.cablePalette) + " picked: dimmed jacks can't take a " + roleName ((jidai::jcs::Role) proc.cablePalette)
            + " cable. Click it again or ANY to clear.";
    g.setColour (proc.cablePalette == jc::kAnyRole ? kDim : kInk);
    g.setFont (font (helpFontPx (k)));
    g.drawFittedText (s, status.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

    // The gestures, two rows of help text.
    const auto rows = helpRows();
    auto hb = helpBounds();
    g.setFont (font (helpFontPx (k)));
    for (int i = 0; i < rows.size(); ++i)
    {
        g.setColour (overHelp ? kInk : kInk.withAlpha (0.82f));
        g.drawFittedText (rows[i], hb.removeFromTop (hb.getHeight() / (float) (rows.size() - i)).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }
}

void CableBar::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int c = 0; c < chipCount(); ++c)
        if (chipBounds (c).contains (e.position))
            h = c;
    const bool help = helpBounds().contains (e.position);
    if (h != hover)
    {
        hover = h;
        repaint();
    }
    setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (help != overHelp)
    {
        overHelp = help;
        helpSince = juce::Time::getMillisecondCounter();
        if (help)
            startTimer (100);
        else
        {
            stopTimer();
            showZoom (false);
        }
        repaint();
    }
}

void CableBar::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    overHelp = false;
    stopTimer();
    showZoom (false);
    repaint();
}

void CableBar::mouseUp (const juce::MouseEvent& e)
{
    for (int c = 0; c < chipCount(); ++c)
        if (chipBounds (c).contains (e.position))
        {
            pickChip (c);
            return;
        }
}

void CableBar::timerCallback()
{
    if (overHelp && juce::Time::getMillisecondCounter() - helpSince >= 500)
    {
        stopTimer();
        showZoom (true);
    }
}

void CableBar::showZoom (bool show)
{
    auto* top = getTopLevelComponent();
    if (! show || top == nullptr || top == this)
    {
        if (zoom != nullptr)
            zoom->setVisible (false);
        return;
    }
    if (zoom == nullptr)
        zoom = std::make_unique<Zoom>();
    if (zoom->getParentComponent() != top)
        top->addChildComponent (*zoom);
    const float k = u();
    zoom->layout (k, top->getWidth() - 16);
    const auto at = top->getLocalPoint (this, juce::Point<int> ((int) (14.0f * k), getHeight() + 4));
    zoom->setTopLeftPosition (juce::jlimit (8, juce::jmax (8, top->getWidth() - zoom->getWidth() - 8), at.x), at.y);
    zoom->setVisible (true);
    zoom->toFront (false);
}

bool CableBar::zoomShowing() const
{
    return zoom != nullptr && zoom->isVisible();
}

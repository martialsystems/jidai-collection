// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "RackCableLayer.h"
#include "RackStyle.h"
#include "RoninPanel.h"

using namespace jidai;
using namespace rackstyle;

namespace {

juce::String jackLabel (const std::string& id)
{
    const auto s = juce::String::fromUTF8 (id.c_str());
    return s.fromFirstOccurrenceOf ("/", false, false);
}

int maxAge (const std::vector<CableSpec>& cables)
{
    int a = 0;
    for (auto& c : cables)
        a = std::max (a, c.age);
    return a;
}

juce::Colour textOn (juce::Colour fill)
{
    return fill.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff111111) : juce::Colour (0xfff4f0e4);
}

}

RackCableLayer::RackCableLayer (RackView& v, JidaiProcessor& p) : view (v), proc (p)
{
    setInterceptsMouseClicks (true, false);
    setRepaintsOnMouseActivity (false);
}

RackCableLayer::~RackCableLayer() = default;

float RackCableLayer::sagFor (float dx, float dy, float scale)
{
    return 18.0f * scale + 0.14f * std::abs (dx) + 0.06f * std::abs (dy);
}

juce::Path RackCableLayer::rope (juce::Point<float> a, juce::Point<float> b) const
{
    // A quadratic through the hang point: the control sits at twice the sag, so the middle hangs by exactly sag.
    const float sag = sagFor (b.x - a.x, b.y - a.y, s());
    const auto mid = (a + b) * 0.5f;
    juce::Path p;
    p.startNewSubPath (a);
    p.quadraticTo (mid.translated (0.0f, 2.0f * sag), b);
    return p;
}

juce::Path RackCableLayer::stub (juce::Point<float> a) const
{
    juce::Path p;
    p.startNewSubPath (a);
    p.quadraticTo (a.translated (0.0f, 30.0f * s()), a.translated (12.0f * s(), 42.0f * s()));
    return p;
}

juce::String RackCableLayer::farName (const std::string& id) const
{
    Device* d = nullptr;
    int j = -1;
    if (! proc.rack().resolve (id, d, j))
        return juce::String::fromUTF8 (id.c_str());
    juce::String s = juce::String (d->displayName()) + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String::fromUTF8 (d->jacks()[(size_t) j].id.c_str());
    const auto why = view.whyHidden (id);
    if (why.isNotEmpty())
        s << " (" << why << ")";
    return s;
}

void RackCableLayer::refresh()
{
    compute();
    repaint();
}

void RackCableLayer::compute()
{
    cablesDrawn.clear();
    const auto& cables = proc.rack().cables();
    const auto& info = proc.rack().cableInfo();
    const int mode = view.cableMode();
    const bool back = view.showBack();
    Device* sel = view.selectedDevice();
    const std::string selPrefix = sel != nullptr ? sel->rackId() + "/" : std::string();
    for (int i = 0; i < (int) cables.size(); ++i)
    {
        if (i == moving)
            continue;
        const auto& c = cables[(size_t) i];
        Drawn d;
        d.index = i;
        const auto* A = view.spotFor (c.a);
        const auto* B = view.spotFor (c.b);
        d.aVisible = A != nullptr;
        d.bVisible = B != nullptr;
        if (A != nullptr) d.a = A->p;
        if (B != nullptr) d.b = B->p;
        const auto role = i < (int) info.size() ? info[(size_t) i].role : proc.rack().jackRole (c.a);
        d.colour = cableColour (c.color, role);
        if (! d.aVisible && ! d.bVisible)
        {
            cablesDrawn.push_back (d);
            continue;
        }
        const bool touchesSel = sel != nullptr && (c.a.rfind (selPrefix, 0) == 0 || c.b.rfind (selPrefix, 0) == 0);
        if (mode == JidaiProcessor::CablesSelected && ! touchesSel)
            d.alpha = 0.12f;
        if (mode == JidaiProcessor::CablesHide)
        {
            d.shown = Shown::Dot;
        }
        else if (d.aVisible && d.bVisible)
        {
            const bool near = std::abs (A->slot - B->slot) <= 1;
            if (mode == JidaiProcessor::CablesHidePassThru && ! near)
                d.shown = Shown::Stub;
            else
                d.shown = Shown::Rope;
            if (back && A->slot == B->slot && mode == JidaiProcessor::CablesAll)
                d.alpha = 0.28f;
        }
        else
            d.shown = Shown::Stub;

        const auto num = "#" + juce::String (i + 1) + juce::String::fromUTF8 (" \xe2\x86\x92 ");
        if (d.shown == Shown::Rope)
            d.path = rope (d.a, d.b);
        else if (d.shown == Shown::Stub)
        {
            if (d.aVisible)
            {
                d.path.addPath (stub (d.a));
                d.tagA = num + farName (c.b);
            }
            if (d.bVisible)
            {
                d.path.addPath (stub (d.b));
                d.tagB = num + farName (c.a);
            }
        }
        cablesDrawn.push_back (d);
    }
}

int RackCableLayer::spotAt (juce::Point<float> p) const
{
    const auto& spots = view.jackSpots();
    int best = -1;
    float bestD = 1.0e9f;
    for (int i = 0; i < (int) spots.size(); ++i)
    {
        const float d = spots[(size_t) i].p.getDistanceFrom (p);
        if (d <= spots[(size_t) i].r + 4.0f && d < bestD)
        {
            best = i;
            bestD = d;
        }
    }
    return best;
}

int RackCableLayer::cableAt (juce::Point<float> p) const
{
    const float tol = juce::jmax (4.0f, 6.0f * s());
    for (auto it = cablesDrawn.rbegin(); it != cablesDrawn.rend(); ++it)
    {
        if ((it->shown != Shown::Rope && it->shown != Shown::Stub) || it->alpha < 0.2f)
            continue;
        if (! it->path.getBounds().expanded (tol).contains (p))
            continue;
        juce::Point<float> on;
        it->path.getNearestPoint (p, on);
        if (on.getDistanceFrom (p) <= tol)
            return it->index;
    }
    return -1;
}

bool RackCableLayer::hitTest (int x, int y)
{
    if (dragging)
        return true;
    const auto p = juce::Point<float> ((float) x, (float) y);
    return spotAt (p) >= 0 || cableAt (p) >= 0;
}

void RackCableLayer::say (const juce::String& text, juce::Point<float> where)
{
    message = text;
    messageAt = where;
    messageTime = juce::Time::getMillisecondCounter();
    startTimerHz (10);
    repaint();
}

void RackCableLayer::timerCallback()
{
    const bool showingMessage = message.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime < 2500;
    if (! showingMessage)
        message.clear();
    if (hoverSpot < 0 && ! showingMessage)
        stopTimer();
    repaint();
}

void RackCableLayer::paint (juce::Graphics& g)
{
    const float sc = s();
    const float core = juce::jmax (2.0f, 5.0f * sc);
    const auto& cables = proc.rack().cables();
    const auto& info = proc.rack().cableInfo();

    auto strokeRope = [&] (const juce::Path& p, juce::Colour c, float alpha, bool hi)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
        g.strokePath (p, juce::PathStrokeType (core + 2.0f * sc), juce::AffineTransform::translation (2.0f * sc, 3.0f * sc));
        g.setColour (c.darker (0.7f).withAlpha (alpha));
        g.strokePath (p, juce::PathStrokeType (core + 1.6f * sc, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withAlpha (alpha));
        g.strokePath (p, juce::PathStrokeType (core, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.22f * alpha));
        g.strokePath (p, juce::PathStrokeType (juce::jmax (0.8f, 1.4f * sc)), juce::AffineTransform::translation (-0.6f * sc, -1.0f * sc));
        if (hi)
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.strokePath (p, juce::PathStrokeType (core + 4.0f * sc), {});
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (core, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    };
    auto plug = [&] (juce::Point<float> p, juce::Colour c, float alpha, float r)
    {
        const float pr = juce::jmax (4.0f, r * 1.15f);
        g.setColour (juce::Colour (0xff141414).withAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (pr * 2.0f, pr * 2.0f).withCentre (p));
        g.setColour (c.withAlpha (alpha));
        g.drawEllipse (juce::Rectangle<float> (pr * 1.6f, pr * 1.6f).withCentre (p), juce::jmax (1.5f, pr * 0.32f));
        g.setColour (juce::Colours::white.withAlpha (0.25f * alpha));
        g.fillEllipse (juce::Rectangle<float> (pr * 0.6f, pr * 0.6f).withCentre (p.translated (-pr * 0.25f, -pr * 0.25f)));
    };
    auto radiusOf = [&] (const std::string& id) { const auto* sp = view.spotFor (id); return sp != nullptr ? sp->r : 8.0f * sc; };

    // Ropes and stubs, the selected one last.
    for (int pass = 0; pass < 2; ++pass)
        for (auto& d : cablesDrawn)
        {
            const bool hi = d.index == selected || d.index == hoverCable;
            if ((pass == 1) != hi || d.path.isEmpty())
                continue;
            strokeRope (d.path, d.colour, d.alpha, d.index == selected);
        }
    // Plugs and dots.
    for (auto& d : cablesDrawn)
    {
        if (d.shown == Shown::None)
            continue;
        const auto& c = cables[(size_t) d.index];
        if (d.aVisible) plug (d.a, d.colour, d.alpha, radiusOf (c.a));
        if (d.bVisible) plug (d.b, d.colour, d.alpha, radiusOf (c.b));
    }

    // Badges.
    const float fs = juce::jmax (9.0f, 11.0f * sc);
    std::vector<juce::Rectangle<float>> placed;
    auto place = [&] (juce::Rectangle<float> r)
    {
        for (int tries = 0; tries < 12; ++tries)
        {
            bool clash = false;
            for (auto& q : placed)
                if (q.intersects (r)) { clash = true; break; }
            if (! clash)
                break;
            r = r.translated (0.0f, r.getHeight() + 2.0f);
        }
        placed.push_back (r);
        return r;
    };
    auto tagWidth = [&] (const juce::String& t) { return juce::GlyphArrangement::getStringWidth (font (fs, true), t) + fs * 1.2f; };

    for (auto& d : cablesDrawn)
    {
        if (d.shown == Shown::None || d.index >= (int) info.size() || d.alpha < 0.2f)
            continue;
        const auto& in = info[(size_t) d.index];
        const auto& c = cables[(size_t) d.index];
        // Which end is the destination (an input)?
        Device* da = nullptr;
        int ja = -1;
        const bool aIsIn = proc.rack().resolve (c.a, da, ja) && da->jacks()[(size_t) ja].desc.dir == PortDir::In;
        const bool destVisible = aIsIn ? d.aVisible : d.bVisible;
        const auto destP = aIsIn ? d.a : d.b;
        const auto srcP = aIsIn ? d.b : d.a;
        const auto anchor = destVisible ? destP : srcP;
        const auto mid = d.shown == Shown::Rope ? d.path.getPointAlongPath (d.path.getLength() * 0.5f) : anchor.translated (14.0f * sc, 24.0f * sc);
        if (in.comp > 0)
        {
            const auto t = "+" + juce::String (in.comp) + " comp (R11)";
            const auto r = place (juce::Rectangle<float> (tagWidth (t), fs * 1.6f).withCentre (anchor.translated (0.0f, -fs * 2.2f)));
            paintTag (g, r, juce::Colour (0xee1a1608), kAmber, t, fs, kAmber.withAlpha (0.7f));
        }
        if (in.skew > 0)
        {
            const auto t = juce::String::fromUTF8 ("\xce\x94") + juce::String (in.skew);
            const auto r = place (juce::Rectangle<float> (tagWidth (t), fs * 1.5f).withCentre (anchor.translated (fs * 2.6f, -fs * 1.0f)));
            paintTag (g, r, juce::Colour (0xee101820), juce::Colour (0xff8fd0ff), t, fs, juce::Colour (0x998fd0ff));
        }
        if (in.badge != jcs::Badge::None && in.badge != jcs::Badge::GateToStrig)
        {
            const auto r = juce::Rectangle<float> (fs * 1.7f, fs * 1.7f).withCentre (mid);
            g.setColour (kRefuse);
            g.fillEllipse (r);
            g.setColour (juce::Colours::white);
            g.setFont (font (fs * 1.1f, true));
            g.drawText (juce::String::fromUTF8 ("\xe2\x89\xa0"), r, juce::Justification::centred);
        }
        if (in.feedback)
        {
            const auto t = juce::String::fromUTF8 ("z\xe2\x81\xbb\xc2\xb9");
            const auto r = juce::Rectangle<float> (tagWidth (t), fs * 1.4f).withCentre (mid.translated (0.0f, fs * 1.7f));
            paintTag (g, r, juce::Colour (0xdd202020), kInk, t, fs * 0.9f);
        }
    }
    // Stub tags.
    for (auto& d : cablesDrawn)
    {
        if (d.shown != Shown::Stub)
            continue;
        for (int end = 0; end < 2; ++end)
        {
            const auto& t = end == 0 ? d.tagA : d.tagB;
            if (t.isEmpty())
                continue;
            const auto at = (end == 0 ? d.a : d.b).translated (12.0f * sc, 42.0f * sc);
            const auto r = place (juce::Rectangle<float> (tagWidth (t), fs * 1.6f).withPosition (at.x - 6.0f * sc, at.y));
            paintTag (g, r, d.colour.withAlpha (0.92f * d.alpha), textOn (d.colour).withAlpha (d.alpha), t, fs);
        }
    }

    // The cable being drawn, and which jacks take it.
    if (dragging)
    {
        const auto& spots = view.jackSpots();
        for (int i = 0; i < (int) spots.size(); ++i)
        {
            const auto& sp = spots[(size_t) i];
            if (sp.id == dragFrom)
                continue;
            const bool ok = proc.rack().check (dragFrom, sp.id) == Rack::Check::Ok;
            if (! ok && i != dragTarget)
                continue;
            g.setColour (ok ? juce::Colour (0xff3fe06a) : kRefuse);
            g.drawEllipse (juce::Rectangle<float> (sp.r * 2.6f, sp.r * 2.6f).withCentre (sp.p), i == dragTarget ? 3.0f : 1.6f);
        }
        const auto role = proc.rack().jackRole (dragFrom);
        const auto end = dragTarget >= 0 ? spots[(size_t) dragTarget].p : dragPos;
        strokeRope (rope (dragOrigin, end), roleColour (role), 1.0f, false);
        plug (dragOrigin, roleColour (role), 1.0f, radiusOf (dragFrom));
        plug (end, roleColour (role), 1.0f, 8.0f * sc);
        if (dragTarget >= 0)
        {
            const auto c = proc.rack().check (dragFrom, spots[(size_t) dragTarget].id);
            if (c != Rack::Check::Ok)
            {
                const auto t = juce::String (Rack::checkText (c));
                paintTag (g, juce::Rectangle<float> (tagWidth (t), fs * 1.7f).withPosition (end.translated (14.0f * sc, -fs * 2.4f)),
                          juce::Colour (0xee2a0c0a), juce::Colour (0xffffb0a8), t, fs, kRefuse);
            }
        }
    }

    // Hover: the jack, its role, its live voltage and where its cables go.
    if (hoverSpot >= 0 && hoverSpot < (int) view.jackSpots().size() && ! dragging)
    {
        const auto& sp = view.jackSpots()[(size_t) hoverSpot];
        const float v = proc.rack().jackVolts (sp.id);
        juce::StringArray lines;
        lines.add (jackLabel (sp.id) + "   " + roleGlyph (sp.role) + " " + roleName (sp.role) + (sp.out ? "  OUT" : "  IN"));
        lines.add (juce::String (v, 2) + " V" + (std::abs (v) > 5.5f ? "  OVER RANGE" : ""));
        for (int i = 0; i < (int) cables.size(); ++i)
        {
            const auto& c = cables[(size_t) i];
            if (c.a == sp.id || c.b == sp.id)
            {
                juce::String l = "#" + juce::String (i + 1) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + farName (c.a == sp.id ? c.b : c.a);
                if (i < (int) info.size() && info[(size_t) i].badge != jcs::Badge::None)
                    l << "  " << juce::String::fromUTF8 (jcs::badgeText (info[(size_t) i].badge));
                lines.add (l);
            }
        }
        float w = 0.0f;
        for (auto& l : lines)
            w = juce::jmax (w, juce::GlyphArrangement::getStringWidth (font (fs, false), l));
        auto box = juce::Rectangle<float> (w + fs * 1.6f, (float) lines.size() * fs * 1.45f + fs * 0.8f).withPosition (sp.p.translated (sp.r + 10.0f, -sp.r - 6.0f));
        box = box.constrainedWithin (getLocalBounds().toFloat());
        g.setColour (juce::Colour (0xf0101012));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (roleColour (sp.role).withAlpha (0.8f));
        g.drawRoundedRectangle (box, 4.0f, 1.2f);
        g.setFont (font (fs, false));
        for (int k = 0; k < lines.size(); ++k)
        {
            g.setColour (k == 1 && std::abs (v) > 5.5f ? kRefuse : kInk);
            g.drawText (lines[k], box.reduced (fs * 0.8f, fs * 0.4f).withHeight (fs * 1.45f).translated (0.0f, (float) k * fs * 1.45f), juce::Justification::centredLeft);
        }
    }

    if (message.isNotEmpty())
        paintTag (g, juce::Rectangle<float> (tagWidth (message), fs * 1.8f).withPosition (messageAt.translated (12.0f, -fs * 2.5f)),
                  juce::Colour (0xee2a0c0a), juce::Colour (0xffffc0b8), message, fs, kRefuse);
}

// ---------------- editing ----------------

void RackCableLayer::commit (const std::vector<CableSpec>& next)
{
    proc.setCables (next);
    view.reloadCables();
}

Rack::Check RackCableLayer::connect (const std::string& from, const std::string& to)
{
    const auto c = proc.rack().check (from, to);
    if (c != Rack::Check::Ok)
        return c;
    auto next = proc.rack().cables();
    CableSpec spec;
    spec.a = from;
    spec.b = to;
    spec.age = maxAge (next) + 1;
    next.push_back (spec);
    commit (next);
    return c;
}

int RackCableLayer::disconnectAll (const std::string& jack)
{
    auto next = proc.rack().cables();
    const auto before = next.size();
    next.erase (std::remove_if (next.begin(), next.end(), [&jack] (const CableSpec& c) { return c.a == jack || c.b == jack; }), next.end());
    const int n = (int) (before - next.size());
    if (n > 0)
    {
        selected = -1;
        commit (next);
    }
    return n;
}

void RackCableLayer::setColour (int index, int colour)
{
    if (proc.rack().setCableColor (index, colour))
        refresh();
}

void RackCableLayer::selectCable (int index)
{
    selected = index >= 0 && index < (int) proc.rack().cables().size() ? index : -1;
    refresh();
}

bool RackCableLayer::deleteSelected()
{
    if (selected < 0 || selected >= (int) proc.rack().cables().size())
        return false;
    auto next = proc.rack().cables();
    next.erase (next.begin() + selected);
    selected = -1;
    commit (next);
    return true;
}

bool RackCableLayer::cancelDrag()
{
    if (! dragging)
        return false;
    dragging = false;
    moving = -1;
    dragTarget = -1;
    refresh();
    return true;
}

void RackCableLayer::mouseMove (const juce::MouseEvent& e)
{
    const int sp = spotAt (e.position);
    const int cb = sp < 0 ? cableAt (e.position) : -1;
    if (sp != hoverSpot || cb != hoverCable)
    {
        hoverSpot = sp;
        hoverCable = cb;
        if (hoverSpot >= 0)
            startTimerHz (10);
        setMouseCursor (sp >= 0 || cb >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void RackCableLayer::mouseExit (const juce::MouseEvent&)
{
    hoverSpot = hoverCable = -1;
    repaint();
}

void RackCableLayer::mouseDown (const juce::MouseEvent& e)
{
    const int sp = spotAt (e.position);
    pressedOnJack = sp >= 0;
    if (e.mods.isPopupMenu())
    {
        if (sp >= 0)
            showJackMenu (sp);
        else if (const int cb = cableAt (e.position); cb >= 0)
            showCableMenu (cb);
        return;
    }
    if (sp < 0)
    {
        selectCable (cableAt (e.position));
        return;
    }
    const auto& spot = view.jackSpots()[(size_t) sp];
    // RONIN's meter reads the jack last pressed (as on its own patch bay).
    if (auto* r = dynamic_cast<RoninDevice*> (view.slotDevice (spot.slot)))
        if (spot.jack < kPanelJackCount)
            r->setMeterJack (spot.jack);

    // Pick up the newest cable on this jack, unless Shift (stack a new one) or the jack is free.
    const auto& cables = proc.rack().cables();
    int newest = -1;
    for (int i = 0; i < (int) cables.size(); ++i)
        if ((cables[(size_t) i].a == spot.id || cables[(size_t) i].b == spot.id) && (newest < 0 || cables[(size_t) i].age >= cables[(size_t) newest].age))
            newest = i;
    if (newest >= 0 && ! e.mods.isShiftDown())
    {
        moving = newest;
        const auto& c = cables[(size_t) newest];
        dragFrom = c.a == spot.id ? c.b : c.a;
        const auto* fixed = view.spotFor (dragFrom);
        dragOrigin = fixed != nullptr ? fixed->p : spot.p.translated (0.0f, 40.0f * s());
    }
    else
    {
        moving = -1;
        dragFrom = spot.id;
        dragOrigin = spot.p;
    }
    dragPos = e.position;
    dragTarget = -1;
    dragging = false;     // starts after a few pixels, so a click on a jack only meters it
}

void RackCableLayer::mouseDrag (const juce::MouseEvent& e)
{
    if (! pressedOnJack || dragFrom.empty())
        return;
    if (! dragging && e.getDistanceFromDragStart() < 4)
        return;
    if (! dragging)
    {
        dragging = true;
        compute();
    }
    dragPos = e.position;
    dragTarget = spotAt (e.position);
    if (dragTarget >= 0 && view.jackSpots()[(size_t) dragTarget].id == dragFrom)
        dragTarget = -1;
    repaint();
}

void RackCableLayer::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging)
    {
        pressedOnJack = false;
        if (moving >= 0)
        {
            moving = -1;
            refresh();
        }
        return;
    }
    dragging = false;
    pressedOnJack = false;
    const int target = spotAt (e.position);
    auto next = proc.rack().cables();
    const int was = moving;
    moving = -1;
    dragTarget = -1;
    if (target < 0)
    {
        if (was >= 0)                                          // a plug pulled off every jack: unplugged
        {
            next.erase (next.begin() + was);
            selected = -1;
            commit (next);
        }
        else
            refresh();
        return;
    }
    const auto& to = view.jackSpots()[(size_t) target].id;
    const auto c = proc.rack().check (dragFrom, to);
    if (c != Rack::Check::Ok)
    {
        say (Rack::checkText (c), e.position);
        refresh();
        return;
    }
    if (was >= 0)
    {
        auto& spec = next[(size_t) was];
        if (spec.a == dragFrom) spec.b = to; else spec.a = to;
        spec.autoRouted = false;
        spec.legacyInvert = proc.rack().legacyInversionDiffers (spec.a, spec.b) && spec.legacyInvert;
        commit (next);
    }
    else
        connect (dragFrom, to);
}

// ---------------- menus ----------------

void RackCableLayer::showJackMenu (int spotIndex)
{
    const auto spot = view.jackSpots()[(size_t) spotIndex];
    auto& rack = proc.rack();
    juce::PopupMenu menu;
    menu.addSectionHeader (jackLabel (spot.id) + "  " + roleName (spot.role));

    juce::PopupMenu connectTo;
    std::vector<std::string> targets;
    for (int i = 0; i < rack.deviceCount(); ++i)
    {
        Device* d = rack.device (i);
        juce::PopupMenu sub;
        for (int j = 0; j < (int) d->jacks().size(); ++j)
        {
            const auto id = Rack::jackId (*d, j);
            if (rack.check (spot.id, id) != Rack::Check::Ok)
                continue;
            targets.push_back (id);
            sub.addItem (1000 + (int) targets.size() - 1, juce::String::fromUTF8 (d->jacks()[(size_t) j].id.c_str())
                                                              + (d->jacks()[(size_t) j].backOnly ? "  (back)" : ""));
        }
        if (sub.getNumItems() > 0)
            connectTo.addSubMenu (juce::String (d->displayName()), sub);
    }
    menu.addSubMenu (juce::String::fromUTF8 ("Connect to\xe2\x80\xa6"), connectTo, connectTo.getNumItems() > 0);

    std::vector<int> mine;
    for (int i = 0; i < (int) rack.cables().size(); ++i)
        if (rack.cables()[(size_t) i].a == spot.id || rack.cables()[(size_t) i].b == spot.id)
            mine.push_back (i);
    menu.addItem (1, "Disconnect" + (mine.size() > 1 ? " all (" + juce::String ((int) mine.size()) + ")" : juce::String()), ! mine.empty());
    juce::PopupMenu colours;
    colours.addItem (100, "Role colour");
    for (int k = 0; k < kSwatchCount; ++k)
        colours.addColouredItem (101 + k, swatchName (k), swatchColour (k));
    menu.addSubMenu ("Cable colour", colours, ! mine.empty());
    for (int i : mine)
    {
        const auto& c = rack.cables()[(size_t) i];
        menu.addItem (2000 + i, juce::String::fromUTF8 ("Go to \xe2\x86\x92 ") + farName (c.a == spot.id ? c.b : c.a));
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (
                            localAreaToGlobal (juce::Rectangle<float> (4.0f, 4.0f).withCentre (spot.p)).toNearestInt()),
                        [sp = juce::Component::SafePointer<RackCableLayer> (this), spot, targets, mine] (int r)
    {
        if (sp == nullptr || r == 0)
            return;
        if (r == 1)
            sp->disconnectAll (spot.id);
        else if (r >= 100 && r < 100 + 1 + kSwatchCount)
            for (int i : mine)
                sp->setColour (i, r - 101);
        else if (r >= 1000 && r < 2000 && r - 1000 < (int) targets.size())
            sp->connect (spot.id, targets[(size_t) (r - 1000)]);
        else if (r >= 2000)
        {
            const int i = r - 2000;
            if (i < (int) sp->proc.rack().cables().size())
            {
                const auto& c = sp->proc.rack().cables()[(size_t) i];
                Device* d = nullptr;
                int j = -1;
                if (sp->proc.rack().resolve (c.a == spot.id ? c.b : c.a, d, j))
                    sp->view.scrollTo (d);
            }
        }
    });
}

void RackCableLayer::showCableMenu (int cable)
{
    selectCable (cable);
    const auto c = proc.rack().cables()[(size_t) cable];
    juce::PopupMenu menu;
    menu.addSectionHeader ("#" + juce::String (cable + 1) + "  " + jackLabel (c.a) + juce::String::fromUTF8 (" \xe2\x86\x94 ") + jackLabel (c.b));
    juce::PopupMenu colours;
    colours.addItem (100, "Role colour", true, c.color < 0);
    for (int k = 0; k < kSwatchCount; ++k)
        colours.addColouredItem (101 + k, swatchName (k), swatchColour (k), true, c.color == k);
    menu.addSubMenu ("Colour", colours);
    menu.addItem (1, "Delete cable");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [sp = juce::Component::SafePointer<RackCableLayer> (this), cable] (int r)
    {
        if (sp == nullptr || r == 0)
            return;
        if (r == 1)
        {
            sp->selected = cable;
            sp->deleteSelected();
        }
        else if (r >= 100)
            sp->setColour (cable, r - 101);
    });
}

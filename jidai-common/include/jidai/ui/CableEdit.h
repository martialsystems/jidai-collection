// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
#pragma once
// Cable editing, the same in the rack and on every unit's back panel. Header-only C++17, no framework, no allocation
// except in History. The UI draws; this decides.
//
// Gestures (helpText() spells them out for the user, with the right modifier name for the platform):
//   press on a jack that holds cables      picks up the TOP cable's end there (reroute), the far end stays put
//   press with the stack modifier (Option on Mac, Alt elsewhere), or on an empty jack
//                                          starts a NEW cable from that jack
//   release on another jack                reroute: the same cable (index, colour, age) with that end moved;
//                                          new: connect
//   release on the jack it came from       nothing changes (no undo step)
//   release in empty space                 a picked-up end removes its cable; a new cable is dropped
//   release on a jack that cannot take it  refused: nothing changes
//   click a cable                          brings it to the front (z, saved with the patch)
//   right-click a cable                    menu with Remove
//   pick a colour in the palette           jacks that cannot take a cable of that colour are dimmed and refuse it
//
//   classOf(Role), classCanFeed(from, to)  the signal classes behind the cable rule (audio, CV, gate)
//   jackTakesRole(role, JackFacts)         can a cable of this role plug into this jack?
//   jackDimmed(palette, JackFacts)         palette = kAnyRole (-1) or a Role index
//   kPaletteOrder                          the palette chips, left to right
//   plugHitRadius(r), jackAt(...)          hit test of a jack or a plug sitting on it
//   topCableAt(cables, jack)               the cable whose plug is on top at a jack (highest z, then last in the list)
//   planPress(...), planDrop(...)          what a press and a release do
//   drawOrder(cables), bringToFront(...)   z-order
//   History<C>                             undo / redo by whole-patch snapshots; one step per gesture
//
// The cable type C needs members a, b (jack ids, any type with ==), int z, and operator== (History only).

#include "../jcs/Roles.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace jidai::cable {

using jcs::Role;

// ---------------- roles and dimming ----------------

enum class SignalClass : std::uint8_t { Audio, CV, Gate };

// The electrical class a cable of this role carries: S-TRIG and GATE/CLK are gates; AUDIO is audio; the rest are CV.
constexpr SignalClass classOf (Role r) noexcept
{
    return r == Role::Audio ? SignalClass::Audio
         : (r == Role::GateClk || r == Role::STrig) ? SignalClass::Gate
         : SignalClass::CV;
}

// Audio and CV feed audio and CV inputs; a gate feeds anything. Audio or CV into a gate input is refused.
constexpr bool classCanFeed (SignalClass from, SignalClass to) noexcept
{
    return to != SignalClass::Gate || from == SignalClass::Gate;
}

// What the editor knows about a jack: its role (colour), the class of its port, and its direction.
struct JackFacts
{
    Role role = Role::CV;
    SignalClass cls = SignalClass::CV;
    bool output = false;
};

// A cable is coloured by its source (output) role. An output takes a cable of its own role; an input takes any
// role whose class may feed it (pitch-law and audio-into-clock cables are allowed, with their warning badge).
constexpr bool jackTakesRole (Role cableRole, const JackFacts& j) noexcept
{
    return j.output ? j.role == cableRole : classCanFeed (classOf (cableRole), j.cls);
}

inline constexpr int kAnyRole = -1;     // no colour picked: nothing is dimmed

constexpr bool jackDimmed (int paletteRole, const JackFacts& j) noexcept
{
    return paletteRole >= 0 && paletteRole < jcs::kRoleCount && ! jackTakesRole ((Role) paletteRole, j);
}

// The palette, left to right: audio, CV, gate/clock, V/oct, Hz/V linear, S-trig.
inline constexpr Role kPaletteOrder[jcs::kRoleCount] = { Role::Audio, Role::CV, Role::GateClk, Role::VOct, Role::HzvLin, Role::STrig };

// ---------------- hit testing ----------------

struct Pt
{
    float x = 0.0f, y = 0.0f;
};

inline float distance (Pt a, Pt b) noexcept
{
    const float dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt (dx * dx + dy * dy);
}

// A plug drawn on a jack of radius r is 1.15 r across its body; it is grabbed anywhere on it plus a 4 px margin,
// and never with less than 10 px, so a small jack is still easy to hit.
inline float plugHitRadius (float jackRadius) noexcept
{
    return std::max (10.0f, jackRadius * 1.15f + 4.0f);
}

// The nearest jack whose plug hit area holds p, or -1. centres and radii are index-aligned.
inline int jackAt (const std::vector<Pt>& centres, const std::vector<float>& radii, Pt p) noexcept
{
    int best = -1;
    float bestD = 0.0f;
    for (std::size_t i = 0; i < centres.size() && i < radii.size(); ++i)
    {
        const float d = distance (centres[i], p);
        if (d <= plugHitRadius (radii[i]) && (best < 0 || d < bestD))
        {
            best = (int) i;
            bestD = d;
        }
    }
    return best;
}

// ---------------- z-order ----------------

template <class C>
int maxZ (const std::vector<C>& cables) noexcept
{
    int z = 0;
    for (const auto& c : cables)
        z = std::max (z, c.z);
    return z;
}

// Back to front: by z, then by position in the list (later is higher). With every z at 0 this is the list order.
template <class C>
std::vector<int> drawOrder (const std::vector<C>& cables)
{
    std::vector<int> order (cables.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = (int) i;
    std::stable_sort (order.begin(), order.end(), [&cables] (int x, int y) { return cables[(std::size_t) x].z < cables[(std::size_t) y].z; });
    return order;
}

template <class C>
bool isOnTop (const std::vector<C>& cables, int index) noexcept
{
    if (index < 0 || index >= (int) cables.size())
        return false;
    const int z = cables[(std::size_t) index].z;
    for (int i = 0; i < (int) cables.size(); ++i)
        if (i != index && (cables[(std::size_t) i].z > z || (cables[(std::size_t) i].z == z && i > index)))
            return false;
    return true;
}

// Raises one cable above all others. False when it already was on top (nothing changes, no undo step).
template <class C>
bool bringToFront (std::vector<C>& cables, int index) noexcept
{
    if (index < 0 || index >= (int) cables.size() || isOnTop (cables, index))
        return false;
    cables[(std::size_t) index].z = maxZ (cables) + 1;
    return true;
}

// The z a new cable gets: level with the top, and last in the list, so it draws on top.
template <class C>
int zForNewCable (const std::vector<C>& cables) noexcept
{
    return maxZ (cables);
}

// ---------------- press and drop ----------------

// The cable whose plug is on top at this jack: highest z, then last in the list. -1 if the jack is free.
template <class C, class J>
int topCableAt (const std::vector<C>& cables, const J& jack) noexcept
{
    int top = -1;
    for (int i = 0; i < (int) cables.size(); ++i)
    {
        const auto& c = cables[(std::size_t) i];
        if (! (c.a == jack || c.b == jack))
            continue;
        if (top < 0 || c.z >= cables[(std::size_t) top].z)
            top = i;
    }
    return top;
}

template <class C, class J>
int cablesAt (const std::vector<C>& cables, const J& jack) noexcept
{
    int n = 0;
    for (const auto& c : cables)
        n += (c.a == jack || c.b == jack) ? 1 : 0;
    return n;
}

enum class Press : std::uint8_t {
    PickUp,      // the top cable's end at this jack follows the pointer (reroute or remove)
    NewCable,    // a new cable from this jack
    Refused      // a new cable from a dimmed jack
};

struct PressPlan
{
    Press what = Press::NewCable;
    int cable = -1;      // PickUp: the cable
    int end = 0;         // PickUp: 0 = its a end moves, 1 = its b end moves
};

// stackModifier: Option (Mac) / Alt held. jackIsDimmed: the palette colour cannot use this jack.
// A cable already on a dimmed jack can still be picked up: dimming only refuses new plugs.
template <class C, class J>
PressPlan planPress (const std::vector<C>& cables, const J& jack, bool stackModifier, bool jackIsDimmed) noexcept
{
    PressPlan p;
    const int top = topCableAt (cables, jack);
    if (top >= 0 && ! stackModifier)
    {
        p.what = Press::PickUp;
        p.cable = top;
        p.end = cables[(std::size_t) top].a == jack ? 0 : 1;
        return p;
    }
    p.what = jackIsDimmed ? Press::Refused : Press::NewCable;
    return p;
}

enum class Drop : std::uint8_t {
    Nothing,     // a new cable released in empty space, or a refused press
    Return,      // a picked-up end released on the jack it came from: no change
    Refused,     // the target cannot take it (wrong direction, type or dimmed): no change
    Connect,     // add the new cable
    Reroute,     // move the picked-up end of the same cable to the target
    Remove       // a picked-up end released in empty space: remove the cable
};

// targetJack: true when released over a jack; sameJack: that jack is where the drag started;
// targetTakes: the full check passed (direction, type, palette) for the cable that would result.
inline Drop planDrop (const PressPlan& press, bool targetJack, bool sameJack, bool targetTakes) noexcept
{
    if (press.what == Press::Refused)
        return Drop::Nothing;
    const bool pickUp = press.what == Press::PickUp;
    if (! targetJack)
        return pickUp ? Drop::Remove : Drop::Nothing;
    if (sameJack)
        return pickUp ? Drop::Return : Drop::Nothing;
    if (! targetTakes)
        return Drop::Refused;
    return pickUp ? Drop::Reroute : Drop::Connect;
}

// Applies a Reroute: the same cable object (colour, age, flags) with the moving end on the new jack, raised to the front.
template <class C, class J>
bool reroute (std::vector<C>& cables, int index, int end, const J& toJack)
{
    if (index < 0 || index >= (int) cables.size())
        return false;
    auto& c = cables[(std::size_t) index];
    (end == 0 ? c.a : c.b) = toJack;
    bringToFront (cables, index);
    return true;
}

// ---------------- undo ----------------

// Undo / redo by whole-patch snapshots: one step per gesture (a reroute is one "Move cable" step, never a remove plus
// an add). A step only applies while the patch is still the one it left behind; if anything else changed the patch
// (a preset load, a device removed), the history is cleared instead of guessing.
template <class C>
class History
{
public:
    explicit History (std::size_t limit = 200) : limit_ (limit) {}

    void record (const std::vector<C>& before, const std::vector<C>& after, std::string label)
    {
        if (before == after)
            return;
        undo_.push_back ({ before, after, std::move (label) });
        if (undo_.size() > limit_)
            undo_.erase (undo_.begin());
        redo_.clear();
    }

    bool canUndo (const std::vector<C>& current) const { return ! undo_.empty() && undo_.back().after == current; }
    bool canRedo (const std::vector<C>& current) const { return ! redo_.empty() && redo_.back().before == current; }
    const std::string& undoLabel() const { static const std::string none; return undo_.empty() ? none : undo_.back().label; }
    const std::string& redoLabel() const { static const std::string none; return redo_.empty() ? none : redo_.back().label; }
    std::size_t undoDepth() const noexcept { return undo_.size(); }
    std::size_t redoDepth() const noexcept { return redo_.size(); }
    void clear() noexcept { undo_.clear(); redo_.clear(); }

    // On success `patch` becomes the patch before the last step. False (and the history cleared) if the patch
    // changed some other way since.
    bool undo (std::vector<C>& patch)
    {
        if (undo_.empty())
            return false;
        if (! (undo_.back().after == patch))
        {
            clear();
            return false;
        }
        patch = undo_.back().before;
        redo_.push_back (std::move (undo_.back()));
        undo_.pop_back();
        return true;
    }

    bool redo (std::vector<C>& patch)
    {
        if (redo_.empty())
            return false;
        if (! (redo_.back().before == patch))
        {
            clear();
            return false;
        }
        patch = redo_.back().after;
        undo_.push_back (std::move (redo_.back()));
        redo_.pop_back();
        return true;
    }

private:
    struct Step
    {
        std::vector<C> before, after;
        std::string label;
    };
    std::vector<Step> undo_, redo_;
    std::size_t limit_;
};

// ---------------- help text ----------------

// The stacking modifier as the user's keyboard names it.
inline const char* stackModifierName() noexcept
{
#if defined(__APPLE__)
    return "Option";
#else
    return "Alt";
#endif
}

inline const char* undoKeysName() noexcept
{
#if defined(__APPLE__)
    return "Cmd+Z undoes a cable change, Shift+Cmd+Z redoes it.";
#else
    return "Ctrl+Z undoes a cable change, Ctrl+Y redoes it.";
#endif
}

// One line per gesture, in the order the user meets them. UTF-8.
inline std::vector<std::string> helpLines()
{
    const std::string mod = stackModifierName();
    return {
        "Drag a plugged cable end to another jack to reroute it.",
        "Drag from an empty jack to start a new cable.",
        mod + "-drag from a used jack to stack another cable on it.",
        "Click a cable to bring it to the front.",
        "Pick a color to dim the jacks that can't take it.",
        "To remove a cable, drag its end into empty space, or right-click it and choose Remove.",
        undoKeysName(),
    };
}

inline std::string helpText (const char* separator = "  ")
{
    std::string s;
    for (const auto& l : helpLines())
        s += (s.empty() ? "" : separator) + l;
    return s;
}

} // namespace jidai::cable

// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
// Tests for jidai/ui/CableEdit.h: the cable gestures every unit and the rack share. No framework. Built as C++17.

#include "jidai/ui/CableEdit.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace jidai;
using namespace jidai::cable;

namespace {

int checks = 0, failures = 0;
std::string group;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("FAIL [%s] %s\n", group.c_str(), what.c_str());
    }
}

// A unit-style cable: int jack ids, an id the UI keeps, a colour, an age (feedback order) and z.
struct TestCable
{
    int a = -1, b = -1;
    int id = 0, color = -1, age = 0, z = 0;
    bool operator== (const TestCable& o) const { return a == o.a && b == o.b && id == o.id && color == o.color && age == o.age && z == o.z; }
};

// A rack-style cable: string jack ids.
struct NamedCable
{
    std::string a, b;
    int z = 0;
    bool operator== (const NamedCable& o) const { return a == o.a && b == o.b && z == o.z; }
};

void testRoles()
{
    group = "roles";
    check (classOf (Role::Audio) == SignalClass::Audio, "AUDIO is audio");
    check (classOf (Role::GateClk) == SignalClass::Gate && classOf (Role::STrig) == SignalClass::Gate, "GATE/CLK and S-TRIG are gates");
    check (classOf (Role::CV) == SignalClass::CV && classOf (Role::VOct) == SignalClass::CV && classOf (Role::HzvLin) == SignalClass::CV,
           "CV, V/OCT and HZ/V LIN are CV");
    // The class matrix is the rack's: audio/CV feed audio/CV; gate feeds everything; audio/CV into gate refused.
    const SignalClass all[3] = { SignalClass::Audio, SignalClass::CV, SignalClass::Gate };
    const bool expected[3][3] = { { true, true, false }, { true, true, false }, { true, true, true } };
    for (int f = 0; f < 3; ++f)
        for (int t = 0; t < 3; ++t)
            check (classCanFeed (all[f], all[t]) == expected[f][t], "class matrix " + std::to_string (f) + "->" + std::to_string (t));

    // Full dimming matrix: palette role x (jack role, class, direction).
    int dimmedOutputs = 0, dimmedInputs = 0;
    for (int p = 0; p < jcs::kRoleCount; ++p)
        for (int r = 0; r < jcs::kRoleCount; ++r)
            for (int out = 0; out < 2; ++out)
            {
                const JackFacts j { (Role) r, classOf ((Role) r), out != 0 };
                const bool dim = jackDimmed (p, j);
                const bool expect = out != 0 ? p != r : (classOf ((Role) r) == SignalClass::Gate && classOf ((Role) p) != SignalClass::Gate);
                check (dim == expect, "dim palette " + std::to_string (p) + " jack " + std::to_string (r) + (out ? " out" : " in"));
                (out != 0 ? dimmedOutputs : dimmedInputs) += dim ? 1 : 0;
                check (! jackDimmed (kAnyRole, j), "ANY dims nothing");
            }
    check (dimmedOutputs == 30, "every output of another role is dimmed (6 x 5)");
    check (dimmedInputs == 8, "gate inputs dimmed for the 4 non-gate colours (4 x 2)");
    // Pitch-law and audio-into-clock cables are warned, not refused: those inputs stay lit.
    check (! jackDimmed ((int) Role::HzvLin, { Role::VOct, SignalClass::CV, false }), "HZ/V LIN into V/OCT input: lit (warning badge)");
    check (! jackDimmed ((int) Role::Audio, { Role::CV, SignalClass::CV, false }), "AUDIO into a CV input: lit");
    check (jackDimmed ((int) Role::CV, { Role::GateClk, SignalClass::Gate, false }), "CV into a gate input: dimmed");
    // A GATE/CLK-role input of CV class takes CV.
    check (! jackDimmed ((int) Role::CV, { Role::GateClk, SignalClass::CV, false }), "CV into a CV-class clock input: lit");
    check (jackDimmed (99, { Role::CV, SignalClass::CV, true }) == false, "out-of-range palette dims nothing");
    // Palette order: audio, CV, gate/clock, V/oct, Hz/V linear, S-trig, each role once.
    const Role order[6] = { Role::Audio, Role::CV, Role::GateClk, Role::VOct, Role::HzvLin, Role::STrig };
    for (int i = 0; i < 6; ++i)
        check (kPaletteOrder[i] == order[i], "palette chip " + std::to_string (i));
}

void testHit()
{
    group = "hit";
    const std::vector<Pt> c { { 100.0f, 100.0f }, { 130.0f, 100.0f }, { 300.0f, 300.0f } };
    const std::vector<float> r { 8.0f, 8.0f, 4.0f };
    check (jackAt (c, r, { 100.0f, 100.0f }) == 0, "on the jack centre");
    check (jackAt (c, r, { 112.0f, 100.0f }) == 0, "on the plug body edge (12 px from an 8 px jack: 1.15 r + 4 = 13.2)");
    check (jackAt (c, r, { 114.0f, 100.0f }) == -1, "14 px away: off the plug");
    check (jackAt (c, r, { 117.0f, 100.0f }) == 1, "nearer jack wins");
    check (jackAt (c, r, { 309.0f, 300.0f }) == 2, "a 4 px jack is still hit 9 px out (10 px minimum)");
    check (jackAt (c, r, { 311.0f, 300.0f }) == -1, "... and not at 11 px");
    check (plugHitRadius (8.0f) > 8.0f * 1.15f, "the whole drawn plug is grabbable");
}

void testPressDrop()
{
    group = "gestures";
    // Jacks 1, 2 outputs; 10, 11, 12 inputs. Two cables stacked on jack 1.
    std::vector<TestCable> cs {
        { 1, 10, 501, -1, 0, 0 },
        { 1, 11, 502, 3, 1, 0 },
        { 2, 12, 503, -1, 2, 0 },
    };
    // Press on a plugged jack: pick up the top cable there (last in the list at equal z).
    auto p = planPress (cs, 1, false, false);
    check (p.what == Press::PickUp && p.cable == 1 && p.end == 0, "press on jack 1 picks up the top cable's a end");
    p = planPress (cs, 11, false, false);
    check (p.what == Press::PickUp && p.cable == 1 && p.end == 1, "press on input 11 picks up its b end");
    // Even a dimmed jack lets you pick up what is on it.
    check (planPress (cs, 11, false, true).what == Press::PickUp, "dimmed jack: its cable can still be picked up");
    // Stack modifier: a new cable.
    p = planPress (cs, 1, true, false);
    check (p.what == Press::NewCable, "Option/Alt on a used jack starts a new cable (stack)");
    check (planPress (cs, 7, false, false).what == Press::NewCable, "empty jack: new cable");
    check (planPress (cs, 7, false, true).what == Press::Refused, "empty dimmed jack: refused");
    check (planPress (cs, 1, true, true).what == Press::Refused, "Option/Alt on a dimmed jack: refused");

    // z decides the top plug.
    cs[0].z = 5;
    check (topCableAt (cs, 1) == 0, "raised cable is the top plug at its jack");
    cs[0].z = 0;

    // Drops.
    const PressPlan pick { Press::PickUp, 1, 1 };
    const PressPlan fresh { Press::NewCable, -1, 0 };
    check (planDrop (pick, true, false, true) == Drop::Reroute, "picked-up end on another jack: reroute");
    check (planDrop (fresh, true, false, true) == Drop::Connect, "new cable on a jack: connect");
    check (planDrop (pick, false, false, false) == Drop::Remove, "picked-up end into empty space: remove");
    check (planDrop (fresh, false, false, false) == Drop::Nothing, "new cable into empty space: nothing");
    check (planDrop (pick, true, true, true) == Drop::Return, "back on its own jack: no change");
    check (planDrop (pick, true, false, false) == Drop::Refused, "incompatible or dimmed target: refused, cable stays");
    check (planDrop (fresh, true, false, false) == Drop::Refused, "new cable on an incompatible jack: refused");
    check (planDrop ({ Press::Refused, -1, 0 }, true, false, true) == Drop::Nothing, "refused press never patches");

    // Reroute keeps the cable: id, colour, age, list position.
    auto before = cs;
    check (reroute (cs, 1, 1, 12), "reroute applied");
    check (cs.size() == before.size() && cs[1].a == 1 && cs[1].b == 12 && cs[1].id == 502 && cs[1].color == 3 && cs[1].age == 1,
           "reroute: same id, colour and age, a end fixed, b end moved");
    check (cs[0] == before[0] && cs[2] == before[2], "reroute touches no other cable");
    check (isOnTop (cs, 1), "a rerouted cable comes to the front");
    check (! reroute (cs, 9, 0, 3), "reroute of a missing cable fails");

    // Stack: Option/Alt-drag from jack 1 to 10 adds a second cable on jack 1 (the plan says NewCable; the UI appends).
    cs.push_back ({ 1, 10, 504, -1, 3, zForNewCable (cs) });
    check (cablesAt (cs, 1) == 3, "three plugs stacked on jack 1");
    check (topCableAt (cs, 1) == 3, "the new stacked cable is the top plug");
}

void testZ()
{
    group = "z-order";
    std::vector<NamedCable> cs { { "A/OUT", "B/IN", 0 }, { "A/OUT2", "B/IN2", 0 }, { "C/OUT", "D/IN", 0 } };
    auto o = drawOrder (cs);
    check (o == std::vector<int> { 0, 1, 2 }, "all z 0: list order");
    check (bringToFront (cs, 0) && cs[0].z == 1, "click raises cable 0 above the rest");
    o = drawOrder (cs);
    check (o == std::vector<int> { 1, 2, 0 }, "draw order after raising 0");
    check (! bringToFront (cs, 0), "raising the top cable again changes nothing");
    check (bringToFront (cs, 1) && drawOrder (cs) == std::vector<int> { 2, 0, 1 }, "raise 1: 2, 0, 1");
    check (topCableAt (cs, std::string ("A/OUT2")) == 1, "string jack ids");
    check (zForNewCable (cs) == 2, "a new cable is level with the top (and last, so drawn on top)");
}

void testHistory()
{
    group = "undo";
    std::vector<TestCable> patch { { 1, 10, 1, -1, 0, 0 }, { 2, 11, 2, 4, 1, 0 } };
    History<TestCable> h;
    // Reroute = one step.
    auto before = patch;
    reroute (patch, 0, 1, 12);
    h.record (before, patch, "Move cable");
    check (h.undoDepth() == 1 && h.undoLabel() == "Move cable", "reroute is one undo step");
    check (h.canUndo (patch), "can undo");
    check (h.undo (patch) && patch == before, "undo restores the exact patch (same cable back on its old jack)");
    check (patch.size() == 2, "no cable created or lost by undo");
    check (h.canRedo (patch) && h.redo (patch) && patch[0].b == 12 && patch[0].id == 1, "redo moves it again");
    // A no-op gesture records nothing.
    h.record (patch, patch, "Nothing");
    check (h.undoDepth() == 1, "no-op not recorded");
    // Remove, then undo puts it back in the same list slot (feedback order unchanged).
    before = patch;
    patch.erase (patch.begin());
    h.record (before, patch, "Remove cable");
    check (h.undo (patch) && patch == before && patch[0].id == 1, "undo of remove restores slot, colour and age");
    // New edit clears redo.
    check (h.redoDepth() == 1, "redo available");
    before = patch;
    patch[1].z = 7;
    h.record (before, patch, "Bring to front");
    check (h.redoDepth() == 0, "a new step clears redo");
    // Patch changed elsewhere: undo refuses and clears.
    patch.push_back ({ 5, 6, 9, -1, 9, 0 });
    auto changed = patch;
    check (! h.undo (patch) && patch == changed && h.undoDepth() == 0, "patch changed by something else: undo does nothing, history cleared");
    // Limit.
    History<TestCable> small (3);
    for (int i = 0; i < 5; ++i)
    {
        before = patch;
        patch[0].z = 100 + i;
        small.record (before, patch, "z");
    }
    check (small.undoDepth() == 3, "history keeps the last 3 steps");
}

void testHelp()
{
    group = "help";
    const auto lines = helpLines();
    check (lines.size() == 7, "seven help lines");
    const std::string all = helpText();
#if defined(__APPLE__)
    check (all.find ("Option-drag") != std::string::npos && all.find ("Alt-drag") == std::string::npos, "Mac says Option-drag");
    check (all.find ("Cmd+Z") != std::string::npos, "Mac says Cmd+Z");
#else
    check (all.find ("Alt-drag") != std::string::npos && all.find ("Option-drag") == std::string::npos, "Windows/Linux says Alt-drag");
    check (all.find ("Ctrl+Z") != std::string::npos, "Windows/Linux says Ctrl+Z");
#endif
    for (const char* must : { "reroute", "empty jack", "stack", "front", "dim", "empty space", "Remove" })
        check (all.find (must) != std::string::npos, std::string ("help mentions ") + must);
}

} // namespace

int main()
{
    testRoles();
    testHit();
    testPressDrop();
    testZ();
    testHistory();
    testHelp();
    std::printf ("%d checks, %d failed\n", checks, failures);
    std::printf (failures == 0 ? "JIDAI CABLE EDIT TESTS PASS\n" : "JIDAI CABLE EDIT TESTS FAIL\n");
    return failures == 0 ? 0 : 1;
}

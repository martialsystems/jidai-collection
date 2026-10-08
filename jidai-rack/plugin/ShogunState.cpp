// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "ShogunState.h"

#include "factory.h"
#include "patch.h"

namespace jidai::shogunstate {

std::string patchJson (const ShogunDevice& d)
{
    const auto e = d.edits();
    shogun::Patch pt;
    std::snprintf (pt.name, sizeof pt.name, "%s", e.pattern.name);
    for (int i = 0; i < shogun::kParamCount; ++i)
        pt.u[i] = d.param (i);
    int n = 0;
    for (const auto& r : e.rows)
        if (r.src != shogun::mod::SRC_NONE && r.dst >= 0)
            pt.rows[n++] = r;
    pt.nCables = 0;
    for (int i = 0; i < shogun::kPorts; ++i)
    {
        pt.cvAmt[i] = e.cvAmt[(size_t) i];
        pt.inLaw[i] = e.inLaw[(size_t) i];
    }
    pt.pattern = e.pattern;
    return shogun::patchToJson (pt, false);
}

std::vector<std::pair<std::string, std::string>> loadPatchJson (ShogunDevice& d, const std::string& json, bool* ok)
{
    std::vector<std::pair<std::string, std::string>> cables;
    shogun::Patch pt;
    const bool parsed = shogun::parsePatch (json.c_str(), pt);
    if (ok != nullptr)
        *ok = parsed;
    if (! parsed)
        return cables;
    for (int i = 0; i < shogun::kParamCount; ++i)
        d.setParam (i, pt.u[i]);
    ShogunDevice::Edits e;
    for (int r = 0; r < shogun::mod::kRows; ++r)
        e.rows[(size_t) r] = pt.rows[r];
    for (int i = 0; i < shogun::kPorts; ++i)
    {
        e.cvAmt[(size_t) i] = pt.cvAmt[i];
        e.inLaw[(size_t) i] = pt.inLaw[i];
    }
    e.pattern = pt.pattern;
    d.setEdits (e);
    for (int c = 0; c < pt.nCables; ++c)
        cables.push_back ({ shogun::kPortTable[pt.cableFrom[c]].id, shogun::kPortTable[pt.cableTo[c]].id });
    return cables;
}

int programCount() { return shogun::factory::kPrograms; }

std::string programName (int program) { return shogun::factory::programName (program); }

bool loadProgram (ShogunDevice& d, int program)
{
    shogun::Patch pt;
    if (! shogun::factory::loadProgram (program, pt))
        return false;
    const double clockSource = d.param (shogun::P_CLOCK_SOURCE);
    for (int i = 0; i < shogun::kParamCount; ++i)
        d.setParam (i, pt.u[i]);
    d.setParam (shogun::P_CLOCK_SOURCE, clockSource);
    ShogunDevice::Edits e;
    for (int r = 0; r < shogun::mod::kRows; ++r)
        e.rows[(size_t) r] = pt.rows[r];
    for (int i = 0; i < shogun::kPorts; ++i)
    {
        e.cvAmt[(size_t) i] = pt.cvAmt[i];
        e.inLaw[(size_t) i] = pt.inLaw[i];
    }
    e.pattern = pt.pattern;
    d.setEdits (e);
    d.setProgram (program);
    return true;
}

ShogunDevice::Snapshot captureState (const ShogunDevice& d)
{
    ShogunDevice::Snapshot s;
    s.json = patchJson (d);
    s.program = d.program();
    return s;
}

void restoreState (ShogunDevice& d, const ShogunDevice::Snapshot& s)
{
    if (s.empty())
        return;
    loadPatchJson (d, s.json);
    d.setProgram (juce::jlimit (0, programCount() - 1, s.program));
}

namespace {
void pushBounded (std::deque<ShogunDevice::Snapshot>& q, ShogunDevice::Snapshot s)
{
    q.push_back (std::move (s));
    while ((int) q.size() > ShogunDevice::History::kUndoLevels)
        q.pop_front();
}
}

void beginUndoStep (ShogunDevice& d)
{
    settleUndoStep (d);     // an earlier step still open is recorded first
    d.history().pending = captureState (d);
}

bool settleUndoStep (ShogunDevice& d)
{
    auto& h = d.history();
    if (h.pending.empty())
        return false;
    ShogunDevice::Snapshot pre = std::move (h.pending);
    h.pending = {};
    if (pre.sameAs (captureState (d)))
        return false;
    pushBounded (h.undo, std::move (pre));
    h.redo.clear();
    return true;
}

bool undo (ShogunDevice& d)
{
    settleUndoStep (d);
    auto& h = d.history();
    const auto cur = captureState (d);
    while (! h.undo.empty() && h.undo.back().sameAs (cur))
        h.undo.pop_back();
    if (h.undo.empty())
        return false;
    pushBounded (h.redo, cur);
    restoreState (d, h.undo.back());
    h.undo.pop_back();
    return true;
}

bool redo (ShogunDevice& d)
{
    settleUndoStep (d);
    auto& h = d.history();
    const auto cur = captureState (d);
    while (! h.redo.empty() && h.redo.back().sameAs (cur))
        h.redo.pop_back();
    if (h.redo.empty())
        return false;
    pushBounded (h.undo, cur);
    restoreState (d, h.redo.back());
    h.redo.pop_back();
    return true;
}

void selectAB (ShogunDevice& d, int slot)
{
    slot &= 1;
    auto& h = d.history();
    if (slot == h.abSlot)
        return;
    beginUndoStep (d);
    h.ab[(size_t) h.abSlot] = captureState (d);
    if (h.ab[(size_t) slot].empty())
        h.ab[(size_t) slot] = h.ab[(size_t) h.abSlot];      // first visit: B starts as a copy of A
    else
        restoreState (d, h.ab[(size_t) slot]);
    h.abSlot = slot;
    settleUndoStep (d);
}

void copyAB (ShogunDevice& d, int from, int to)
{
    from &= 1;
    to &= 1;
    auto& h = d.history();
    if (from == to)
        return;
    const auto src = from == h.abSlot ? captureState (d) : h.ab[(size_t) from];
    if (src.empty())
        return;
    if (to == h.abSlot)
    {
        beginUndoStep (d);
        restoreState (d, src);
        settleUndoStep (d);
    }
    else
        h.ab[(size_t) to] = src;
}

bool abFilled (const ShogunDevice& d, int slot)
{
    const auto& h = d.history();
    return (slot & 1) == h.abSlot || ! h.ab[(size_t) (slot & 1)].empty();
}

std::unique_ptr<juce::XmlElement> toXml (const ShogunDevice& d)
{
    auto x = std::make_unique<juce::XmlElement> (kTag);
    x->setAttribute ("version", kVersion);
    x->setAttribute ("program", d.program());
    x->setAttribute ("running", d.running() || d.runRequested() ? 1 : 0);
    x->addTextElement (juce::String::fromUTF8 (patchJson (d).c_str()));
    return x;
}

bool fromXml (ShogunDevice& d, const juce::XmlElement& x, std::vector<std::pair<std::string, std::string>>* cables)
{
    if (! x.hasTagName (kTag) || x.getIntAttribute ("version", 1) < 2)
        return false;    // v1 plugin states (old parameter ids) are not migrated, as in the SHOGUN plugin
    bool ok = false;
    auto c = loadPatchJson (d, x.getAllSubText().toStdString(), &ok);
    if (! ok)
        return false;
    if (cables != nullptr)
        *cables = std::move (c);
    d.setProgram (juce::jlimit (0, programCount() - 1, x.getIntAttribute ("program", 0)));
    d.requestRun (x.getIntAttribute ("running") != 0);
    return true;
}

} // namespace jidai::shogunstate

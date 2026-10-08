// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// ORIGAMI state, one format for the plugin and the rack device (JCS R7):
//   <ORIGAMI format="1" unit="ORIGAMI"><PARAM id="wave" value="0.5"/> ... </ORIGAMI>
// Values are natural units keyed by the stable param ids. Unknown ids are ignored, missing ids take their
// default, a newer format is refused (the caller keeps its current state) and older formats run through the
// migration chain (none yet: format 1 is the first).

#include "origami/dsp/OrigamiParams.h"
#include "jidai/jcs/State.h"

#include <juce_core/juce_core.h>

#include <memory>

namespace origami {

inline std::unique_ptr<juce::XmlElement> stateToXml (const double (&values)[kParamCount])
{
    auto x = std::make_unique<juce::XmlElement> (kStateUnit);
    x->setAttribute ("format", kStateFormat);
    x->setAttribute ("unit", kStateUnit);
    for (int p = 0; p < kParamCount; ++p)
    {
        auto* e = x->createNewChildElement ("PARAM");
        e->setAttribute ("id", paramInfo (p).id);
        e->setAttribute ("value", values[p]);
    }
    return x;
}

struct StateLoad
{
    bool ok = false;
    juce::String message;
};

inline StateLoad stateFromXml (const juce::XmlElement& x, double (&values)[kParamCount])
{
    if (! x.hasTagName (kStateUnit) || x.getStringAttribute ("unit", kStateUnit) != kStateUnit)
        return { false, "not an ORIGAMI state" };
    const int format = x.getIntAttribute ("format", 1);
    if (format > kStateFormat)
        return { false, "made by a newer ORIGAMI (format " + juce::String (format) + ")" };
    double next[kParamCount];
    for (int p = 0; p < kParamCount; ++p)
        next[p] = paramInfo (p).def;
    for (auto* e : x.getChildWithTagNameIterator ("PARAM"))
    {
        const int p = paramIndex (e->getStringAttribute ("id").toStdString());
        if (p >= 0)
            next[p] = clampParam (p, e->getDoubleAttribute ("value", paramInfo (p).def));
    }
    // Older formats would migrate here (jidai::jcs::MigrationChain); format 1 is the first.
    for (int p = 0; p < kParamCount; ++p)
        values[p] = next[p];
    return { true, {} };
}

} // namespace origami

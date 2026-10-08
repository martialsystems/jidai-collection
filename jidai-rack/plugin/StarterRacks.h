// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// The starter racks: complete rack states (JIDAIRACK version 3) compiled in from racks/starter_racks.xml (written by
// tools/make_starter_racks.py), so they need no files on disk on any platform. INIT, the empty rack (RACK I/O only),
// is always first. The host sees them as the plugin's programs ("ACID: Acid Line"); the header's RACKS menu groups
// them by category.

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

struct StarterRack
{
    juce::String name, category, about;
    std::shared_ptr<const juce::XmlElement> state;      // the JIDAIRACK element
    // "INIT", or "CATEGORY: Name" (the host's program list).
    juce::String displayName() const { return category.isEmpty() || category == "INIT" ? name : category + ": " + name; }
};

// Parses a starter rack list (<JIDAI_STARTER_RACKS><RACK name category about><JIDAIRACK .../></RACK>...). Entries
// without a name or a JIDAIRACK are skipped and reported in `errors`.
std::vector<StarterRack> parseStarterRacks (const juce::XmlElement& list, juce::StringArray* errors = nullptr);

// The compiled list, parsed once. Never empty: if the data is damaged it holds INIT alone.
const std::vector<StarterRack>& starterRacks();
juce::String starterRacksXmlText();

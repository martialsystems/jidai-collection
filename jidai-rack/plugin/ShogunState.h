// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// SHOGUN's device state in a rack: the SHOGUN plugin's own state (<SHOGUN version="2" running> holding the patch JSON
// as its text), read and written with SHOGUN's shared patch format (engine/patch.h: parsePatch / patchToJson, the same
// code the SHOGUN plugin, its factory bank and its web page use), so a SHOGUN patch moves between the plugin and the
// rack unchanged. Jack ids in a patch resolve through SHOGUN's own alias shim (shogun::resolvePort), which stays local.
//
// The patch's "cables" (SHOGUN's internal ROUTE bay) are not part of a rack device: a rack keeps every cable in its own
// CABLE list. On load they are returned as (from, to) "SECTION:LABEL" pairs for the caller to patch as rack cables.

#include "core/ShogunDevice.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace jidai::shogunstate {

constexpr int kVersion = 2;
constexpr const char* kTag = "SHOGUN";

std::string patchJson (const ShogunDevice& d);      // sparse: parameters and step fields that differ from INIT
std::vector<std::pair<std::string, std::string>> loadPatchJson (ShogunDevice& d, const std::string& json, bool* ok = nullptr);

// SHOGUN's factory programs (engine/factory.h): 0 = INIT, then the kits, each with its pattern.
int programCount();
std::string programName (int program);
// Loads a factory program the way the SHOGUN plugin does (INIT, then the sparse kit document). The device keeps its
// CLOCK:SOURCE, which belongs to the rack's wiring (a new SHOGUN follows the host). Factory kits carry no bay cables.
bool loadProgram (ShogunDevice& d, int program);

std::unique_ptr<juce::XmlElement> toXml (const ShogunDevice& d);
bool fromXml (ShogunDevice& d, const juce::XmlElement& x, std::vector<std::pair<std::string, std::string>>* cables = nullptr);

} // namespace jidai::shogunstate

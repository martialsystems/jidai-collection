// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "StarterRacks.h"

#include "BinaryData.h"

std::vector<StarterRack> parseStarterRacks (const juce::XmlElement& list, juce::StringArray* errors)
{
    std::vector<StarterRack> out;
    if (! list.hasTagName ("JIDAI_STARTER_RACKS"))
    {
        if (errors != nullptr) errors->add ("not a starter rack list");
        return out;
    }
    for (auto* e : list.getChildWithTagNameIterator ("RACK"))
    {
        StarterRack r;
        r.name = e->getStringAttribute ("name");
        r.category = e->getStringAttribute ("category");
        r.about = e->getStringAttribute ("about");
        auto* state = e->getChildByName ("JIDAIRACK");
        if (r.name.isEmpty() || state == nullptr)
        {
            if (errors != nullptr) errors->add ("skipped a rack without a name or a JIDAIRACK state: " + r.name);
            continue;
        }
        r.state = std::make_shared<const juce::XmlElement> (*state);
        out.push_back (std::move (r));
    }
    return out;
}

juce::String starterRacksXmlText()
{
    return juce::String::fromUTF8 (BinaryData::starter_racks_xml, BinaryData::starter_racks_xmlSize);
}

const std::vector<StarterRack>& starterRacks()
{
    static const std::vector<StarterRack> list = []
    {
        std::vector<StarterRack> racks;
        if (auto xml = juce::parseXML (starterRacksXmlText()))
            racks = parseStarterRacks (*xml);
        if (racks.empty() || racks.front().name != "INIT")
        {
            StarterRack init;
            init.name = init.category = "INIT";
            auto state = std::make_shared<juce::XmlElement> ("JIDAIRACK");
            state->setAttribute ("version", 3);
            state->createNewChildElement ("DEVICE")->setAttribute ("kind", "RACK I/O");
            init.state = state;
            racks.insert (racks.begin(), std::move (init));
        }
        return racks;
    }();
    return list;
}

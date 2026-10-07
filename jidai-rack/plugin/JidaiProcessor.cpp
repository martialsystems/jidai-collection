// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "JidaiProcessor.h"
#include "JidaiEditor.h"
#include "BinaryData.h"

using namespace jidai;

namespace {

const char* kColourNames[] = { "red", "white", "yellow", "green" };

juce::File userPatternFile()
{
    // The BUSHIDO plugin's own file: patterns saved in either one show up in both.
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("BUSHIDO").getChildFile ("user_patterns.json");
}

JidaiProcessor::Pattern patternFromVar (const juce::var& pv)
{
    JidaiProcessor::Pattern pat;
    pat.name = pv["name"].toString();
    if (auto* o = pv["params"].getDynamicObject())
        for (auto& kv : o->getProperties())
            pat.params.push_back ({ kv.name.toString(), (float) kv.value });
    if (auto* cl = pv["cables"].getArray())
        for (auto& c : *cl)
        {
            int col = 0;
            for (int k = 0; k < 4; ++k)
                if (c[2].toString() == kColourNames[k])
                    col = k;
            pat.cables.push_back ({ c[0].toString(), c[1].toString() });
            pat.colors.push_back (col);
        }
    return pat;
}

juce::var patternToVar (const JidaiProcessor::Pattern& pat)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", pat.name);
    auto* params = new juce::DynamicObject();
    for (auto& [id, v] : pat.params)
        params->setProperty (id, v);
    o->setProperty ("params", juce::var (params));
    juce::Array<juce::var> cl;
    for (size_t i = 0; i < pat.cables.size(); ++i)
        cl.add (juce::var (juce::Array<juce::var> { pat.cables[i][0], pat.cables[i][1], kColourNames[juce::jlimit (0, 3, pat.colors[i])] }));
    o->setProperty ("cables", cl);
    return juce::var (o);
}

}

JidaiProcessor::JidaiProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    const auto d = juce::JSON::parse (juce::String::fromUTF8 (BinaryData::sq10_patterns_json, BinaryData::sq10_patterns_jsonSize));
    if (auto* list = d["patterns"].getArray())
        for (auto& pv : *list)
            banks_[0].push_back (patternFromVar (pv));
    factoryCount_ = (int) banks_[0].size();
    const auto u = juce::JSON::parse (userPatternFile());
    for (int b = 0; b < 2; ++b)
        if (auto* list = u[b == 0 ? "A" : "B"].getArray())
            for (auto& pv : *list)
                if ((int) banks_[b].size() < kBankSize)
                    banks_[b].push_back (patternFromVar (pv));

    resetToDefaultRack();
}

JidaiProcessor::~JidaiProcessor() = default;

void JidaiProcessor::resetToDefaultRack()
{
    rack_.clear();
    loaded_.clear();
    addDevice (DeviceKind::Bushido);
    addDevice (DeviceKind::Ronin);
    sendChangeMessage();
}

// ---------------- devices ----------------

Device* JidaiProcessor::addDevice (DeviceKind kind, int position)
{
    Device* d = rack_.addDevice (kind, position);
    if (auto* b = dynamic_cast<BushidoDevice*> (d))
        loadPattern (b, 0, 0);
    return d;
}

void JidaiProcessor::removeDevice (Device* device)
{
    loaded_.erase (device);
    rack_.removeDevice (device);
}

void JidaiProcessor::moveDevice (Device* device, int position)
{
    rack_.moveDevice (device, position);
}

void JidaiProcessor::setCables (const std::vector<jidai::CableSpec>& cables)
{
    rack_.setCables (cables);
}

void JidaiProcessor::loadRoninProgram (RoninDevice* ronin, int index)
{
    rack_.loadRoninProgram (ronin, index);
}

// ---------------- BUSHIDO patterns ----------------

juce::StringArray JidaiProcessor::patternNames (int bank) const
{
    juce::StringArray names;
    if (bank == 0 || bank == 1)
        for (auto& p : banks_[bank])
            names.add (p.name);
    return names;
}

std::pair<int, int> JidaiProcessor::loadedPattern (const Device* bushido) const
{
    const auto it = loaded_.find (bushido);
    return it == loaded_.end() ? std::pair<int, int> { -1, -1 } : it->second;
}

// Pattern cables are BUSHIDO-only ("OUTPUTS:CV A" to "INPUTS:RESET"): they replace this BUSHIDO's own cables,
// and cables to other devices stay.
void JidaiProcessor::loadPattern (BushidoDevice* bushido, int bank, int index)
{
    if (bushido == nullptr || (bank != 0 && bank != 1) || ! juce::isPositiveAndBelow (index, (int) banks_[bank].size()))
        return;
    const Pattern& pat = banks_[bank][(size_t) index];
    for (auto& [id, v] : pat.params)
        bushido->setParam (id.toStdString(), v);
    std::vector<jidai::CableSpec> own;
    const std::string prefix = bushido->rackId() + "/";
    for (size_t i = 0; i < pat.cables.size(); ++i)
        own.push_back ({ prefix + pat.cables[i][0].toStdString(), prefix + pat.cables[i][1].toStdString(), pat.colors[i], 0 });
    rack_.replaceInternalCables (bushido, own);
    loaded_[bushido] = { bank, index };
}

int JidaiProcessor::savePattern (BushidoDevice* bushido, int bank, const juce::String& name)
{
    if (bushido == nullptr || (bank != 0 && bank != 1) || (int) banks_[bank].size() >= kBankSize)
        return -1;
    Pattern pat;
    pat.name = name.trim().isEmpty() ? juce::String ("PATTERN") : name.trim();
    for (auto& p : bushido->engine().params())
        if (p.positions != -1)
            pat.params.push_back ({ juce::String (p.id), bushido->param (p.id) });
    const std::string prefix = bushido->rackId() + "/";
    for (auto& c : rack_.cables())
    {
        if (c.a.rfind (prefix, 0) != 0 || c.b.rfind (prefix, 0) != 0)
            continue;
        pat.cables.push_back ({ juce::String (c.a.substr (prefix.size())), juce::String (c.b.substr (prefix.size())) });
        pat.colors.push_back (c.color);
    }
    banks_[bank].push_back (std::move (pat));
    writeUserPatterns();
    const int i = (int) banks_[bank].size() - 1;
    loaded_[bushido] = { bank, i };
    return i;
}

void JidaiProcessor::writeUserPatterns() const
{
    auto* o = new juce::DynamicObject();
    for (int b = 0; b < 2; ++b)
    {
        juce::Array<juce::var> l;
        for (size_t i = b == 0 ? (size_t) factoryCount_ : 0; i < banks_[b].size(); ++i)
            l.add (patternToVar (banks_[b][i]));
        o->setProperty (b == 0 ? "A" : "B", l);
    }
    const auto f = userPatternFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::JSON::toString (juce::var (o)));
}

// ---------------- audio ----------------

bool JidaiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void JidaiProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    rack_.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (0);
}

void JidaiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();    // Effect: no plugin MIDI port. The rack does not read this buffer.
    const int n = buffer.getNumSamples();
    const int ins = getTotalNumInputChannels();
    const int outs = buffer.getNumChannels();
    if (n == 0 || outs < 2)
    {
        buffer.clear();
        return;
    }
    // Host audio, if any, is read sample by sample before that sample of output is written, so in place is fine.
    const float* inL = ins > 0 ? buffer.getReadPointer (0) : nullptr;
    const float* inR = ins > 1 ? buffer.getReadPointer (1) : inL;
    rack_.process (inL, inR, buffer.getWritePointer (0), buffer.getWritePointer (1), n);
    for (int ch = 2; ch < outs; ++ch)
        buffer.clear (ch, 0, n);
}

// ---------------- state ----------------

void JidaiProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("JIDAIRACK");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("browser", browserOpen ? 1 : 0);
    for (int i = 0; i < rack_.deviceCount(); ++i)
    {
        Device* d = rack_.device (i);
        auto* e = xml.createNewChildElement ("DEVICE");
        e->setAttribute ("kind", deviceKindName (d->kind()));
        e->setAttribute ("number", d->number);
        if (d->folded)
            e->setAttribute ("folded", 1);
        if (auto* r = dynamic_cast<RoninDevice*> (d))
        {
            e->setAttribute ("program", r->program());
            e->setAttribute ("effect", r->effectOn() ? 1 : 0);
            juce::StringArray knobs;
            for (int k = 0; k < kPanelKnobCount; ++k)
                knobs.add (juce::String (r->knob (k), 6));
            e->setAttribute ("knobs", knobs.joinIntoString (","));
        }
        if (auto* b = dynamic_cast<BushidoDevice*> (d))
        {
            e->setAttribute ("bypass", b->bypassed() ? 1 : 0);
            const auto p = loadedPattern (b);
            e->setAttribute ("bank", p.first);
            e->setAttribute ("pattern", p.second);
            for (auto& param : b->engine().params())
            {
                if (param.positions == -1)
                    continue;
                auto* pe = e->createNewChildElement ("PARAM");
                pe->setAttribute ("id", juce::String (param.id));
                pe->setAttribute ("value", (double) b->param (param.id));
            }
        }
    }
    for (auto& c : rack_.cables())
    {
        auto* e = xml.createNewChildElement ("CABLE");
        e->setAttribute ("a", juce::String (c.a));
        e->setAttribute ("b", juce::String (c.b));
        e->setAttribute ("color", c.color);
        e->setAttribute ("age", c.age);
    }
    copyXmlToBinary (xml, dest);
}

void JidaiProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("JIDAIRACK"))
        return;
    restoreFromXml (*xml);
    sendChangeMessage();
}

void JidaiProcessor::restoreFromXml (const juce::XmlElement& xml)
{
    rack_.clear();
    loaded_.clear();
    browserOpen = xml.getIntAttribute ("browser", 1) != 0;
    for (auto* e : xml.getChildWithTagNameIterator ("DEVICE"))
    {
        const auto kind = e->getStringAttribute ("kind") == "RONIN" ? DeviceKind::Ronin : DeviceKind::Bushido;
        Device* d = rack_.addDevice (kind, -1, e->getIntAttribute ("number", 0));
        if (d == nullptr)
            continue;
        d->folded = e->getIntAttribute ("folded") != 0;
        if (auto* r = dynamic_cast<RoninDevice*> (d))
        {
            rack_.loadRoninProgram (r, e->getIntAttribute ("program", kDefaultFactoryPreset));
            r->setEffectOn (e->getIntAttribute ("effect", 1) != 0);
            juce::StringArray knobs;
            knobs.addTokens (e->getStringAttribute ("knobs"), ",", "");
            for (int k = 0; k < knobs.size() && k < kPanelKnobCount; ++k)
                r->setKnob (k, knobs[k].getFloatValue());
        }
        if (auto* b = dynamic_cast<BushidoDevice*> (d))
        {
            b->setBypassed (e->getIntAttribute ("bypass") != 0);
            for (auto* pe : e->getChildWithTagNameIterator ("PARAM"))
                b->setParam (pe->getStringAttribute ("id").toStdString(), (float) pe->getDoubleAttribute ("value"));
            const int bank = e->getIntAttribute ("bank", -1), pattern = e->getIntAttribute ("pattern", -1);
            if (bank >= 0)
                loaded_[b] = { bank, pattern };
        }
    }
    std::vector<jidai::CableSpec> cables;
    for (auto* e : xml.getChildWithTagNameIterator ("CABLE"))
        cables.push_back ({ e->getStringAttribute ("a").toStdString(), e->getStringAttribute ("b").toStdString(),
                            e->getIntAttribute ("color"), e->getIntAttribute ("age", (int) cables.size()) });
    rack_.setCables (cables);
}

juce::AudioProcessorEditor* JidaiProcessor::createEditor()
{
    return new JidaiEditor (*this);
}

#if ! JIDAI_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JidaiProcessor();
}
#endif

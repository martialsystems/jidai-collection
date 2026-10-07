// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "JidaiProcessor.h"
#include "JidaiEditor.h"
#include "BinaryData.h"

#include "UI/PatchBayLogic.h"

#include <algorithm>
#include <utility>

using namespace jidai;

namespace {

const char* kColourNames[] = { "red", "white", "yellow", "green" };

// The PRESET screen's short names, in factory order. The host program names are the longer set.
const char* kRoninScreen[] = {
    "DRY", "NOISE MIXER", "VOICE", "RING", "S&H", "FEEDBACK", "HOLD",
    "FILTER LOOP", "MG FILTER", "STEP CUTOFF", "RING DRONE", "DELAY BOUNCE", "SELF RING"
};
static_assert (sizeof (kRoninScreen) / sizeof (kRoninScreen[0]) == kFactoryPresetCount, "screen names match the factory list");

juce::File& userStoreOverride()
{
    static juce::File root;
    return root;
}

juce::File userFile (const char* folder, const char* name)
{
    auto root = userStoreOverride();
    if (root == juce::File())
        root = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    return root.getChildFile (folder).getChildFile (name);
}

int colourIndex (const juce::String& name)
{
    for (int k = 0; k < 4; ++k)
        if (name == kColourNames[k])
            return k;
    return 0;
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
            pat.cables.push_back ({ c[0].toString(), c[1].toString() });
            pat.colors.push_back (colourIndex (c[2].toString()));
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

void readPairs (const juce::var& obj, std::vector<std::pair<juce::String, float>>& out)
{
    if (auto* o = obj.getDynamicObject())
        for (auto& kv : o->getProperties())
            out.push_back ({ kv.name.toString(), (float) kv.value });
}

void setRoninKnob (RoninDevice* ronin, const juce::String& id, float value)
{
    const auto section = id.upToFirstOccurrenceOf (":", false, false);
    const auto label = id.fromFirstOccurrenceOf (":", false, false);
    if (section.isEmpty() || label.isEmpty())
        return;
    const int index = panelKnobIndex (section.toRawUTF8(), label.toRawUTF8());
    if (index >= 0)
        ronin->setKnob (index, value);
}

// File endpoints are "MS-50/VCO:SAW" and "SQ-10/OUTPUTS:CV A", with no instance number.
// A user preset stores the jack id alone ("VCO:SAW") so it rebinds to the RONIN that saved it.
std::string mapEndpoint (const juce::String& raw, RoninDevice* ronin, BushidoDevice* bushido)
{
    const auto s = raw.trim();
    if (s.startsWith ("MS-50#") || s.startsWith ("SQ-10#"))
        return s.toStdString();
    if (s.startsWith ("MS-50/"))
        return ronin == nullptr ? std::string() : ronin->rackId() + "/" + s.substring (6).toStdString();
    if (s.startsWith ("SQ-10/"))
        return bushido == nullptr ? std::string() : bushido->rackId() + "/" + s.substring (6).toStdString();
    if (ronin != nullptr && s.contains (":"))
        return ronin->rackId() + "/" + s.toStdString();
    return {};
}

bool touches (const jidai::CableSpec& cable, const Device* device)
{
    if (device == nullptr)
        return false;
    const auto prefix = device->rackId() + "/";
    return cable.a.rfind (prefix, 0) == 0 || cable.b.rfind (prefix, 0) == 0;
}

// Same colours the web rack uses: a cable into OUTPUT is green, otherwise the source type.
int cableColor (const JackDesc& a, const JackDesc& b)
{
    const bool aOut = a.desc.dir == PortDir::Out;
    const JackDesc& out = aOut ? a : b;
    const JackDesc& in = aOut ? b : a;
    if (in.id.find ("OUTPUT") != std::string::npos)
        return 3;
    if (out.desc.type == PortType::Audio)
        return 0;
    if (out.desc.type == PortType::CV)
        return 2;
    return 1;
}

}

JidaiProcessor::RoninStored JidaiProcessor::roninFromVar (const juce::var& pv)
{
    RoninStored u;
    u.name = pv["name"].toString();
    u.base = (int) pv["base"];
    u.power = pv.hasProperty ("power") ? (bool) pv["power"] : true;
    readPairs (pv["knobs"], u.knobs);
    if (auto* cl = pv["cables"].getArray())
        for (auto& c : *cl)
        {
            u.cables.push_back ({ c[0].toString(), c[1].toString() });
            u.colors.push_back (colourIndex (c[2].toString()));
        }
    return u;
}

juce::var JidaiProcessor::roninToVar (const RoninStored& u)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", u.name);
    o->setProperty ("base", u.base);
    o->setProperty ("power", u.power);
    auto* knobs = new juce::DynamicObject();
    for (auto& [id, v] : u.knobs)
        knobs->setProperty (id, v);
    o->setProperty ("knobs", juce::var (knobs));
    juce::Array<juce::var> cl;
    for (size_t i = 0; i < u.cables.size(); ++i)
        cl.add (juce::var (juce::Array<juce::var> { u.cables[i][0], u.cables[i][1], kColourNames[juce::jlimit (0, 3, u.colors[i])] }));
    o->setProperty ("cables", cl);
    return juce::var (o);
}

void JidaiProcessor::setUserStoreRootForTest (const juce::File& root)
{
    userStoreOverride() = root;
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

    const auto patches = juce::JSON::parse (juce::String::fromUTF8 (BinaryData::rack_patches_json, BinaryData::rack_patches_jsonSize));
    if (auto* list = patches["patches"].getArray())
        for (auto& pv : *list)
        {
            RackPatch p;
            p.name = pv["name"].toString();
            const auto rack = pv["rack"];
            p.preset = (int) rack["preset"];
            p.power = rack.hasProperty ("power") ? (bool) rack["power"] : true;
            readPairs (rack["knobs"], p.knobs);
            readPairs (rack["params"], p.params);
            if (auto* cl = rack["cables"].getArray())
                for (auto& c : *cl)
                    if (c.isArray() && c.size() >= 2)
                        p.cables.push_back ({ c[0].toString(), c[1].toString() });
            rackPatches_.push_back (std::move (p));
        }

    const auto layout = juce::JSON::parse (juce::String::fromUTF8 (BinaryData::sq10_layout_json, BinaryData::sq10_layout_jsonSize));
    if (auto* controls = layout["controls"].getArray())
        for (auto& c : *controls)
        {
            const auto kind = c["kind"].toString();
            const auto id = c["id"].toString();
            if (kind == "button" || kind == "readout" || id == "TOP:BYPASS")
                continue;
            bushidoDefaults_.push_back ({ id, (float) c["default"] });
        }

    const auto u = juce::JSON::parse (userFile ("BUSHIDO", "user_patterns.json"));
    for (int b = 0; b < 2; ++b)
        if (auto* list = u[b == 0 ? "A" : "B"].getArray())
            for (auto& pv : *list)
                if ((int) banks_[b].size() + (b == 1 ? (int) rackPatches_.size() : 0) < kBankSize)
                    banks_[b].push_back (patternFromVar (pv));

    const auto ru = juce::JSON::parse (userFile ("RONIN", "user_presets.json"));
    for (int b = 0; b < 2; ++b)
    {
        const int front = b == 0 ? kFactoryPresetCount : (int) rackPatches_.size();
        if (auto* list = ru[b == 0 ? "A" : "B"].getArray())
            for (auto& pv : *list)
                if (front + (int) roninUser_[b].size() < kBankSize)
                    roninUser_[b].push_back (roninFromVar (pv));
    }

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
    if (auto* r = dynamic_cast<RoninDevice*> (d))
        loaded_[r] = { 0, r->program() };
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
    if (rack_.loadRoninProgram (ronin, index))
        loaded_[ronin] = { 0, index };
}

RoninDevice* JidaiProcessor::firstRonin() const
{
    for (int i = 0; i < rack_.deviceCount(); ++i)
        if (auto* r = dynamic_cast<RoninDevice*> (rack_.device (i)))
            return r;
    return nullptr;
}

BushidoDevice* JidaiProcessor::firstBushido() const
{
    for (int i = 0; i < rack_.deviceCount(); ++i)
        if (auto* b = dynamic_cast<BushidoDevice*> (rack_.device (i)))
            return b;
    return nullptr;
}

// A rack patch names both instruments with no instance number. It is applied to the device whose screen
// was used and to the first device of the other kind. Cables that name a missing partner are skipped.
// Cables on other devices stay. The file lists cables oldest first, and that order is the age order.
void JidaiProcessor::loadRackPatch (BushidoDevice* bushido, RoninDevice* ronin, int index)
{
    if (! juce::isPositiveAndBelow (index, (int) rackPatches_.size()))
        return;
    if (bushido != nullptr && rack_.indexOf (bushido) < 0)
        bushido = nullptr;
    if (ronin != nullptr && rack_.indexOf (ronin) < 0)
        ronin = nullptr;
    if (bushido == nullptr && ronin == nullptr)
        return;

    const RackPatch& p = rackPatches_[(size_t) index];
    if (ronin != nullptr)
    {
        rack_.loadRoninProgram (ronin, p.preset);
        for (auto& [id, v] : p.knobs)
            setRoninKnob (ronin, id, v);
        ronin->setEffectOn (p.power);
        loaded_[ronin] = { 1, index };
    }
    if (bushido != nullptr)
    {
        for (auto& [id, v] : bushidoDefaults_)
            bushido->setParam (id.toStdString(), v);
        for (auto& [id, v] : p.params)
            bushido->setParam (id.toStdString(), v);
        loaded_[bushido] = { 1, index };
    }

    std::vector<jidai::CableSpec> next;
    int age = 0;
    for (auto& c : rack_.cables())
    {
        if (touches (c, bushido) || touches (c, ronin))
            continue;
        next.push_back (c);
        age = std::max (age, c.age + 1);
    }
    for (auto& pair : p.cables)
    {
        const auto a = mapEndpoint (pair[0], ronin, bushido);
        const auto b = mapEndpoint (pair[1], ronin, bushido);
        if (a.empty() || b.empty() || rack_.check (a, b) != Rack::Check::Ok)
            continue;
        Device* da = nullptr;
        Device* db = nullptr;
        int ja = -1, jb = -1;
        if (! rack_.resolve (a, da, ja) || ! rack_.resolve (b, db, jb))
            continue;
        next.push_back ({ a, b, cableColor (da->jacks()[(size_t) ja], db->jacks()[(size_t) jb]), age++ });
    }
    rack_.setCables (next);
}

// ---------------- BUSHIDO patterns ----------------

juce::StringArray JidaiProcessor::patternNames (int bank) const
{
    juce::StringArray names;
    if (bank == 1)
        for (auto& p : rackPatches_)
            names.add (p.name);
    if (bank == 0 || bank == 1)
        for (auto& p : banks_[bank])
            names.add (p.name);
    return names;
}

std::pair<int, int> JidaiProcessor::loadedPattern (const Device* device) const
{
    const auto it = loaded_.find (device);
    return it == loaded_.end() ? std::pair<int, int> { -1, -1 } : it->second;
}

// Pattern cables are BUSHIDO-only ("OUTPUTS:CV A" to "INPUTS:RESET"): they replace this BUSHIDO's own cables,
// and cables to other devices stay. A bank B index below the rack patches loads that rack patch instead.
void JidaiProcessor::loadPattern (BushidoDevice* bushido, int bank, int index)
{
    if (bushido == nullptr || (bank != 0 && bank != 1))
        return;
    if (bank == 1 && index < (int) rackPatches_.size())
    {
        loadRackPatch (bushido, firstRonin(), index);
        return;
    }
    const int local = bank == 1 ? index - (int) rackPatches_.size() : index;
    if (! juce::isPositiveAndBelow (local, (int) banks_[bank].size()))
        return;
    const Pattern& pat = banks_[bank][(size_t) local];
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
    if (bushido == nullptr || (bank != 0 && bank != 1) || patternNames (bank).size() >= kBankSize)
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
    const int shown = (int) patternNames (bank).size() - 1;
    loaded_[bushido] = { bank, shown };
    return shown;
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
    const auto f = userFile ("BUSHIDO", "user_patterns.json");
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::JSON::toString (juce::var (o)));
}

// ---------------- RONIN presets ----------------

juce::StringArray JidaiProcessor::roninPresetNames (int bank) const
{
    juce::StringArray names;
    if (bank == 0)
        for (int i = 0; i < kFactoryPresetCount; ++i)
            names.add (kRoninScreen[i]);
    else if (bank == 1)
        for (auto& p : rackPatches_)
            names.add (p.name);
    else
        return names;
    for (auto& u : roninUser_[bank])
        names.add (u.name);
    return names;
}

void JidaiProcessor::loadRoninPreset (RoninDevice* ronin, int bank, int index)
{
    if (ronin == nullptr || rack_.indexOf (ronin) < 0 || (bank != 0 && bank != 1))
        return;
    const int front = bank == 0 ? kFactoryPresetCount : (int) rackPatches_.size();
    if (index < front)
    {
        if (bank == 0)
            loadRoninProgram (ronin, index);
        else
            loadRackPatch (firstBushido(), ronin, index);
        return;
    }
    const int local = index - front;
    if (! juce::isPositiveAndBelow (local, (int) roninUser_[bank].size()))
        return;
    const RoninStored& u = roninUser_[bank][(size_t) local];
    rack_.loadRoninProgram (ronin, u.base);
    for (auto& [id, v] : u.knobs)
        setRoninKnob (ronin, id, v);
    ronin->setEffectOn (u.power);
    std::vector<jidai::CableSpec> own;
    for (size_t i = 0; i < u.cables.size(); ++i)
    {
        const auto a = mapEndpoint (u.cables[i][0], ronin, nullptr);
        const auto b = mapEndpoint (u.cables[i][1], ronin, nullptr);
        if (! a.empty() && ! b.empty())
            own.push_back ({ a, b, u.colors[i], 0 });
    }
    rack_.replaceInternalCables (ronin, own);
    loaded_[ronin] = { bank, index };
}

int JidaiProcessor::saveRoninPreset (RoninDevice* ronin, int bank, const juce::String& name)
{
    if (ronin == nullptr || rack_.indexOf (ronin) < 0 || (bank != 0 && bank != 1) || roninPresetNames (bank).size() >= kBankSize)
        return -1;
    RoninStored u;
    u.name = name.trim().isEmpty() ? juce::String ("PRESET") : name.trim();
    u.base = ronin->program();
    u.power = ronin->effectOn();
    for (int i = 0; i < kPanelKnobCount; ++i)
        u.knobs.push_back ({ juce::String::fromUTF8 (kPanelKnobs[i].section) + ":" + juce::String::fromUTF8 (kPanelKnobs[i].label), ronin->knob (i) });
    const std::string prefix = ronin->rackId() + "/";
    for (auto& c : rack_.cables())
    {
        if (c.a.rfind (prefix, 0) != 0 || c.b.rfind (prefix, 0) != 0)
            continue;
        u.cables.push_back ({ juce::String (c.a.substr (prefix.size())), juce::String (c.b.substr (prefix.size())) });
        u.colors.push_back (c.color);
    }
    roninUser_[bank].push_back (std::move (u));
    writeUserRonin();
    const int shown = (int) roninPresetNames (bank).size() - 1;
    loaded_[ronin] = { bank, shown };
    return shown;
}

void JidaiProcessor::writeUserRonin() const
{
    auto* o = new juce::DynamicObject();
    for (int b = 0; b < 2; ++b)
    {
        juce::Array<juce::var> l;
        for (auto& u : roninUser_[b])
            l.add (roninToVar (u));
        o->setProperty (b == 0 ? "A" : "B", l);
    }
    const auto f = userFile ("RONIN", "user_presets.json");
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
    xml.setAttribute ("version", 2);
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
            const auto screen = loadedPattern (r);
            if (screen.first >= 0)
            {
                e->setAttribute ("screenBank", screen.first);
                e->setAttribute ("screenIndex", screen.second);
            }
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
    const int version = xml.getIntAttribute ("version", 1);
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
            if (e->hasAttribute ("screenBank"))
                loaded_[r] = { e->getIntAttribute ("screenBank"), e->getIntAttribute ("screenIndex") };
            else
                loaded_[r] = { 0, r->program() };
        }
        if (auto* b = dynamic_cast<BushidoDevice*> (d))
        {
            b->setBypassed (e->getIntAttribute ("bypass") != 0);
            for (auto* pe : e->getChildWithTagNameIterator ("PARAM"))
                b->setParam (pe->getStringAttribute ("id").toStdString(), (float) pe->getDoubleAttribute ("value"));
            int bank = e->getIntAttribute ("bank", -1), pattern = e->getIntAttribute ("pattern", -1);
            // Version 1 stored bank B as user entries only. The rack patches now sit in front of them.
            if (version < 2 && bank == 1 && pattern >= 0)
                pattern += (int) rackPatches_.size();
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

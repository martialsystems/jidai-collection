// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "Rack.h"

#include "jidai/jcs/Graph.h"

#include <algorithm>
#include <cmath>

namespace jidai {

const char* deviceKindName (DeviceKind kind)
{
    switch (kind)
    {
        case DeviceKind::Bushido: return "BUSHIDO";
        case DeviceKind::Ronin: return "RONIN";
        case DeviceKind::Origami: return "ORIGAMI";
        case DeviceKind::RackIO: return "RACK I/O";
    }
    return "";
}
const char* deviceKindPrefix (DeviceKind kind) { return kind == DeviceKind::RackIO ? "RACK" : deviceKindName (kind); }

const char* deviceKindFromName (const std::string& n, DeviceKind& kind)
{
    for (DeviceKind k : { DeviceKind::Bushido, DeviceKind::Ronin, DeviceKind::Origami, DeviceKind::RackIO })
        if (n == deviceKindName (k) || n == deviceKindPrefix (k))
        {
            kind = k;
            return deviceKindName (k);
        }
    return nullptr;
}

std::vector<JackGroup> Device::jackGroups() const
{
    std::vector<JackGroup> groups;
    for (int i = 0; i < (int) jacks_.size(); ++i)
    {
        if (jacks_[(size_t) i].unit == nullptr)
            continue;
        const std::string section = jacks_[(size_t) i].id.substr (0, jacks_[(size_t) i].id.find (':'));
        auto it = std::find_if (groups.begin(), groups.end(), [&] (const JackGroup& g) { return g.title == section; });
        if (it == groups.end())
            groups.push_back ({ section, { i } });
        else
            it->jacks.push_back (i);
    }
    return groups;
}

jidai::jcs::Role Device::jackRole (int jack) const
{
    using jidai::jcs::Role;
    if (jack < 0 || jack >= (int) jacks_.size() || jacks_[(size_t) jack].unit == nullptr)
        return Role::CV;
    const JackDesc& j = jacks_[(size_t) jack];
    const std::string label = j.id.substr (j.id.find (':') + 1);
    if (j.desc.type == PortType::Audio)
        return Role::Audio;
    if (j.desc.type == PortType::Gate)
        return j.desc.strigVolts ? Role::STrig : Role::GateClk;
    if (j.desc.dir == PortDir::In && j.unit->strigInput (j.port))
        return Role::STrig;
    if (label.find ("HZ/V") != std::string::npos)
        return Role::HzvLin;
    if (label.find ("V/OCT") != std::string::npos)
        return Role::VOct;
    return Role::CV;
}

int Device::findJack (const std::string& id) const
{
    for (size_t i = 0; i < jacks_.size(); ++i)
        if (jacks_[i].id == id)
            return (int) i;
    return -1;
}

const char* Rack::checkText (Check c)
{
    switch (c)
    {
        case Check::Ok: return "";
        case Check::UnknownJack: return "that jack is not in the rack";
        case Check::TwoOutputs: return "two outputs: nothing flows";
        case Check::TwoInputs: return "two inputs: nothing flows";
        case Check::SameJack: return "both ends on one jack";
        case Check::BadType: return "that jack does not take this cable";
    }
    return "";
}

Rack::Rack() = default;

Rack::~Rack()
{
    std::lock_guard<std::mutex> g (lock_);
    graph_.install (nullptr);
}

void Rack::prepare (double sampleRate, int maxBlock)
{
    std::lock_guard<std::mutex> g (lock_);
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock_ = maxBlock;
    for (auto& d : devices_)
        d->prepare (sampleRate_);
}

Device* Rack::device (int index) const
{
    if (index < 0 || index >= (int) devices_.size())
        return nullptr;
    return devices_[(size_t) index].get();
}

int Rack::indexOf (const Device* device) const
{
    for (size_t i = 0; i < devices_.size(); ++i)
        if (devices_[i].get() == device)
            return (int) i;
    return -1;
}

Device* Rack::findDevice (const std::string& rackId) const
{
    for (auto& d : devices_)
        if (d->rackId() == rackId)
            return d.get();
    return nullptr;
}

std::string Rack::jackId (const Device& device, int jack)
{
    if (jack < 0 || jack >= (int) device.jacks().size())
        return {};
    return device.rackId() + "/" + device.jacks()[(size_t) jack].id;
}

bool Rack::resolve (const std::string& id, Device*& device, int& jack) const
{
    // The rack id never holds a '/', jack labels may ("HZ/V", "START/STOP"), so split at the first one.
    const auto slash = id.find ('/');
    if (slash == std::string::npos)
        return false;
    device = findDevice (id.substr (0, slash));
    if (device == nullptr)
        return false;
    jack = device->findJack (id.substr (slash + 1));
    return jack >= 0;
}

float Rack::jackVolts (const std::string& id) const
{
    Device* d = nullptr;
    int j = -1;
    if (! resolve (id, d, j))
        return 0.0f;
    const auto& jack = d->jacks()[(size_t) j];
    if (jack.unit == nullptr)
        return 0.0f;
    return jack.unit->values()[jack.port];
}

int Rack::nextNumber (DeviceKind kind) const
{
    for (int n = 1;; ++n)
    {
        bool used = false;
        for (auto& d : devices_)
            used = used || (d->kind() == kind && d->number == n);
        if (! used)
            return n;
    }
}

Device* Rack::addDevice (DeviceKind kind, int position, int number)
{
    if (kind == DeviceKind::RackIO && rackIO_ != nullptr)
        return nullptr;
    if (number > 0)
        for (auto& d : devices_)
            if (d->kind() == kind && d->number == number)
                return nullptr;
    std::unique_ptr<Device> made;
    if (kind == DeviceKind::Ronin)
        made = std::make_unique<RoninDevice>();
    else if (kind == DeviceKind::Origami)
        made = std::make_unique<OrigamiDevice>();
    else if (kind == DeviceKind::RackIO)
        made = std::make_unique<RackIODevice>();
    else
        made = std::make_unique<BushidoDevice>();
    made->number = number > 0 ? number : nextNumber (kind);

    std::vector<CableSpec> internal;
    if (auto* ronin = dynamic_cast<RoninDevice*> (made.get()))
    {
        std::vector<std::pair<int, int>> pairs;
        ronin->loadProgram (ronin->program(), pairs);
        for (const auto& pr : pairs)
            internal.push_back ({ jackId (*ronin, pr.first), jackId (*ronin, pr.second) });
    }
    return place (std::move (made), position, std::move (internal));
}

Device* Rack::adoptDevice (std::unique_ptr<Device> device, int position)
{
    if (device == nullptr || (device->kind() == DeviceKind::RackIO && rackIO_ != nullptr))
        return nullptr;
    for (auto& d : devices_)
        if (d->kind() == device->kind() && d->number == device->number)
            device->number = nextNumber (device->kind());
    return place (std::move (device), position, {});
}

Device* Rack::place (std::unique_ptr<Device> made, int position, std::vector<CableSpec> internal)
{
    made->prepare (sampleRate_);
    Device* d = made.get();
    std::lock_guard<std::mutex> g (lock_);
    if (d->kind() == DeviceKind::RackIO)
    {
        position = 0;
        rackIO_ = static_cast<RackIODevice*> (d);
    }
    else if (position < 0 || position > (int) devices_.size())
        position = (int) devices_.size();
    else
        position = std::max (position, firstPosition());
    devices_.insert (devices_.begin() + position, std::move (made));
    for (auto& c : internal)
    {
        c.age = nextAge_++;
        cables_.push_back (c);
    }
    rebuild();
    return d;
}

Device* Rack::insertNew (DeviceKind kind, int position, bool route)
{
    Device* d = addDevice (kind, position);
    if (d == nullptr)
        return nullptr;
    if (auto* b = dynamic_cast<BushidoDevice*> (d))
        b->engine().applyNewInstanceDefaults (true, hostPlaying());
    if (route)
        autoRoute (d);
    return d;
}

bool Rack::hasCable (const std::string& a, const std::string& b) const
{
    for (const auto& c : cables_)
        if ((c.a == a && c.b == b) || (c.a == b && c.b == a))
            return true;
    return false;
}

void Rack::addCableLocked (const std::string& a, const std::string& b, bool autoRouted)
{
    CableSpec c { a, b };
    c.age = nextAge_++;
    c.autoRouted = autoRouted;
    cables_.push_back (c);
}

int Rack::autoRoute (Device* device)
{
    const int index = indexOf (device);
    if (index < 0 || rackIO_ == nullptr || device == rackIO_)
        return 0;
    const std::string io = rackIO_->rackId() + "/";
    const std::string me = device->rackId() + "/";
    std::vector<std::pair<std::string, std::string>> want;
    auto add = [&] (const std::string& from, const std::string& to)
    {
        if (check (from, to) == Check::Ok && ! hasCable (from, to))
            want.push_back ({ from, to });
    };
    switch (device->kind())
    {
        case DeviceKind::Ronin:
            add (me + RoninDevice::kHostOutL, io + "MAIN:OUT L");
            add (me + RoninDevice::kHostOutR, io + "MAIN:OUT R");
            if (index > 0)
                if (auto* above = dynamic_cast<BushidoDevice*> (devices_[(size_t) index - 1].get()))
                {
                    const std::string seq = above->rackId() + "/";
                    const bool lin = above->engine().law (0) == rack::pitch::Law::HzvLin;
                    add (seq + "OUTPUTS:CV A", me + (lin ? "VCO:HZ/V" : "VCO:V/OCT"));
                    add (seq + "OUTPUTS:GATE A", me + "EG 1:TRIG");
                }
            break;
        case DeviceKind::Origami:
            add (io + "HOST:IN L", me + "HOST:IN L");
            add (io + "HOST:IN R", me + "HOST:IN R");
            add (me + "HOST:OUT L", io + "MAIN:OUT L");
            add (me + "HOST:OUT R", io + "MAIN:OUT R");
            break;
        case DeviceKind::Bushido:      // no audio out to the host by default (MIXER:OUT is a mixer for patching)
        case DeviceKind::RackIO:
            break;
    }
    if (want.empty())
        return 0;
    std::lock_guard<std::mutex> g (lock_);
    for (const auto& [a, b] : want)
        addCableLocked (a, b, true);
    rebuild();
    return (int) want.size();
}

void Rack::applyLegacyHostRouting()
{
    if (rackIO_ == nullptr)
        addDevice (DeviceKind::RackIO);
    const std::string io = rackIO_->rackId() + "/";
    std::lock_guard<std::mutex> g (lock_);
    bool first = true;
    for (auto& d : devices_)
    {
        if (d->kind() != DeviceKind::Ronin)
            continue;
        const std::string me = d->rackId() + "/";
        if (first)
        {
            if (! hasCable (io + "HOST:IN L", me + RoninDevice::kHostInL)) addCableLocked (io + "HOST:IN L", me + RoninDevice::kHostInL, false);
            if (! hasCable (io + "HOST:IN R", me + RoninDevice::kHostInR)) addCableLocked (io + "HOST:IN R", me + RoninDevice::kHostInR, false);
            first = false;
        }
        if (! hasCable (me + RoninDevice::kHostOutL, io + "MAIN:OUT L")) addCableLocked (me + RoninDevice::kHostOutL, io + "MAIN:OUT L", false);
        if (! hasCable (me + RoninDevice::kHostOutR, io + "MAIN:OUT R")) addCableLocked (me + RoninDevice::kHostOutR, io + "MAIN:OUT R", false);
    }
    rebuild();
}

bool Rack::removeDevice (Device* device)
{
    if (device != nullptr && device == rackIO_)
        return false;
    std::unique_ptr<Device> removed;
    {
        std::lock_guard<std::mutex> g (lock_);
        const int index = indexOf (device);
        if (index < 0)
            return false;
        const std::string prefix = device->rackId() + "/";
        cables_.erase (std::remove_if (cables_.begin(), cables_.end(), [&prefix] (const CableSpec& c)
                                       { return c.a.rfind (prefix, 0) == 0 || c.b.rfind (prefix, 0) == 0; }),
                       cables_.end());
        removed = std::move (devices_[(size_t) index]);
        devices_.erase (devices_.begin() + index);
        rebuild();
    }
    return true;    // `removed` is freed here, after the audio thread has the routing without it
}

bool Rack::moveDevice (Device* device, int position)
{
    if (device != nullptr && device == rackIO_)
        return false;
    std::lock_guard<std::mutex> g (lock_);
    const int index = indexOf (device);
    if (index < 0)
        return false;
    auto keep = std::move (devices_[(size_t) index]);
    devices_.erase (devices_.begin() + index);
    position = std::clamp (position, firstPosition(), (int) devices_.size());
    devices_.insert (devices_.begin() + position, std::move (keep));
    rebuild();
    return true;
}

void Rack::clear()
{
    std::vector<std::unique_ptr<Device>> removed;
    {
        std::lock_guard<std::mutex> g (lock_);
        removed.swap (devices_);
        rackIO_ = nullptr;
        cables_.clear();
        rebuild();
    }
}

void Rack::setCables (const std::vector<CableSpec>& cables)
{
    std::lock_guard<std::mutex> g (lock_);
    cables_ = cables;
    for (auto& c : cables_)
        nextAge_ = std::max (nextAge_, c.age + 1);
    rebuild();
}

Rack::Check Rack::check (const std::string& a, const std::string& b) const
{
    Device* da = nullptr;
    Device* db = nullptr;
    int ja = -1, jb = -1;
    if (! resolve (a, da, ja) || ! resolve (b, db, jb))
        return Check::UnknownJack;
    if (da == db && ja == jb)
        return Check::SameJack;
    const JackDesc& A = da->jacks()[(size_t) ja];
    const JackDesc& B = db->jacks()[(size_t) jb];
    if (A.unit == nullptr || B.unit == nullptr)
        return Check::UnknownJack;
    if (A.desc.dir == B.desc.dir)
        return A.desc.dir == PortDir::Out ? Check::TwoOutputs : Check::TwoInputs;
    const JackDesc& out = A.desc.dir == PortDir::Out ? A : B;
    const JackDesc& in = A.desc.dir == PortDir::Out ? B : A;
    return portTypesAllowed (out.desc.type, in.desc.type) ? Check::Ok : Check::BadType;
}

Rack::Check Rack::connect (const std::string& a, const std::string& b, int color)
{
    const Check c = check (a, b);
    if (c != Check::Ok)
        return c;
    std::lock_guard<std::mutex> g (lock_);
    CableSpec spec { a, b, color, nextAge_++ };
    cables_.push_back (spec);
    rebuild();
    return Check::Ok;
}

bool Rack::setCableColor (int index, int color)
{
    std::lock_guard<std::mutex> g (lock_);
    if (index < 0 || index >= (int) cables_.size())
        return false;
    cables_[(size_t) index].color = color;      // colour never changes sound: no rebuild
    return true;
}

bool Rack::legacyInversionDiffers (const std::string& a, const std::string& b) const
{
    Device* da = nullptr;
    Device* db = nullptr;
    int ja = -1, jb = -1;
    if (! resolve (a, da, ja) || ! resolve (b, db, jb))
        return false;
    const JackDesc& A = da->jacks()[(size_t) ja];
    const JackDesc& B = db->jacks()[(size_t) jb];
    if (A.unit == nullptr || B.unit == nullptr || A.desc.dir == B.desc.dir)
        return false;
    const JackDesc& out = A.desc.dir == PortDir::Out ? A : B;
    const JackDesc& in = A.desc.dir == PortDir::Out ? B : A;
    if (out.desc.type != PortType::Gate)
        return false;
    const bool destPlain = in.unit->plainVoltGates();
    const bool destStrig = in.unit->strigInput (in.port);
    if (! destPlain)
        // v2 inverted a non-S-trig gate into any non-Gate RONIN input; JCS inverts only into S-trig inputs.
        return ! out.desc.strigVolts && in.desc.type != PortType::Gate && ! destStrig;
    // v2 turned an S-trig source into a plain input positive (held = 5 V); JCS passes it raw (held = 0 V).
    return out.desc.strigVolts && ! out.unit->plainVoltGates();
}

jidai::jcs::Role Rack::jackRole (const std::string& id) const
{
    Device* d = nullptr;
    int j = -1;
    return resolve (id, d, j) ? d->jackRole (j) : jidai::jcs::Role::CV;
}

bool Rack::disconnect (const std::string& a, const std::string& b)
{
    std::lock_guard<std::mutex> g (lock_);
    for (auto it = cables_.rbegin(); it != cables_.rend(); ++it)
    {
        if ((it->a == a && it->b == b) || (it->a == b && it->b == a))
        {
            cables_.erase (std::next (it).base());
            rebuild();
            return true;
        }
    }
    return false;
}

void Rack::replaceInternalCables (Device* device, const std::vector<CableSpec>& cables)
{
    std::lock_guard<std::mutex> g (lock_);
    const std::string prefix = device->rackId() + "/";
    cables_.erase (std::remove_if (cables_.begin(), cables_.end(), [&prefix] (const CableSpec& c)
                                   { return c.a.rfind (prefix, 0) == 0 && c.b.rfind (prefix, 0) == 0; }),
                   cables_.end());
    for (auto c : cables)
    {
        c.age = nextAge_++;
        cables_.push_back (c);
    }
    rebuild();
}

bool Rack::loadRoninProgram (RoninDevice* ronin, int index)
{
    if (indexOf (ronin) < 0)
        return false;
    std::vector<CableSpec> internal;
    {
        std::lock_guard<std::mutex> g (lock_);
        std::vector<std::pair<int, int>> pairs;
        if (! ronin->loadProgram (index, pairs))
            return false;
        for (size_t i = 0; i < pairs.size(); ++i)
            internal.push_back ({ jackId (*ronin, pairs[i].first), jackId (*ronin, pairs[i].second) });
    }
    replaceInternalCables (ronin, internal);
    return true;
}

int Rack::pathLatency (const Device* device) const
{
    const int i = indexOf (device);
    return i >= 0 && i < (int) devicePath_.size() ? devicePath_[(size_t) i] : 0;
}

bool Rack::updateLatency()
{
    std::lock_guard<std::mutex> g (lock_);
    bool changed = deviceLatency_.size() != devices_.size();
    for (size_t i = 0; ! changed && i < devices_.size(); ++i)
        changed = deviceLatency_[i] != devices_[i]->latencySamples();
    if (changed)
        rebuild();
    return changed;
}

// Caller holds lock_. Allocates (message thread), then swaps the routing in; the old one is freed here too.
void Rack::rebuild()
{
    std::vector<Unit*> units;
    std::vector<int> unitDevice;
    std::vector<OrderEdge> order;
    bushidos_.clear();
    deviceLatency_.assign (devices_.size(), 0);
    for (size_t di = 0; di < devices_.size(); ++di)
    {
        auto& d = devices_[di];
        deviceLatency_[di] = std::max (0, d->latencySamples());
        for (Unit* u : d->units())
        {
            units.push_back (u);
            unitDevice.push_back ((int) di);
        }
        for (const auto& o : d->orderEdges())
            order.push_back (o);
        if (auto* b = dynamic_cast<BushidoDevice*> (d.get()))
            bushidos_.push_back (b);
    }
    auto unitIndex = [&units] (const Unit* u)
    {
        const auto it = std::find (units.begin(), units.end(), u);
        return it == units.end() ? -1 : (int) (it - units.begin());
    };

    // Oldest first; graphCables[k] came from cables_[from[k]].
    std::vector<size_t> byAge (cables_.size());
    for (size_t i = 0; i < byAge.size(); ++i)
        byAge[i] = i;
    std::stable_sort (byAge.begin(), byAge.end(), [this] (size_t x, size_t y) { return cables_[x].age < cables_[y].age; });
    std::vector<GraphCable> graphCables;
    std::vector<size_t> from;
    std::vector<char> sourceAudio, intoMain;
    info_.assign (cables_.size(), CableInfo {});
    for (size_t idx : byAge)
    {
        const auto& c = cables_[idx];
        Device* da = nullptr;
        Device* db = nullptr;
        int ja = -1, jb = -1;
        if (! resolve (c.a, da, ja) || ! resolve (c.b, db, jb))
            continue;
        const JackDesc& A = da->jacks()[(size_t) ja];
        const JackDesc& B = db->jacks()[(size_t) jb];
        if (A.unit == nullptr || B.unit == nullptr || A.desc.dir == B.desc.dir)
            continue;    // output to output or input to input carries nothing
        const bool aOut = A.desc.dir == PortDir::Out;
        const JackDesc& out = aOut ? A : B;
        const JackDesc& in = aOut ? B : A;
        const Device* outDev = aOut ? da : db;
        const Device* inDev = aOut ? db : da;
        const int outJack = aOut ? ja : jb, inJack = aOut ? jb : ja;
        info_[idx].role = outDev->jackRole (outJack);
        info_[idx].badge = jidai::jcs::cableBadge (info_[idx].role, inDev->jackRole (inJack));
        GraphCable gc { out.unit, out.port, in.unit, in.port };
        gc.legacyInvert = c.legacyInvert;
        graphCables.push_back (gc);
        from.push_back (idx);
        sourceAudio.push_back (out.desc.type == PortType::Audio ? 1 : 0);
        intoMain.push_back (inDev == rackIO_ ? 1 : 0);
    }

    // First pass: R9 classification. Then R11 path latency over the zero-delay audio cables and the device-internal
    // order edges (signal paths inside a device), with L on the units that have audio outputs.
    std::vector<signed char> status;
    (void) RackGraph::build (units, graphCables, order, &status);
    const int n = (int) units.size();
    std::vector<int> L ((size_t) n, 0);
    for (int u = 0; u < n; ++u)
    {
        const int lat = deviceLatency_[(size_t) unitDevice[(size_t) u]];
        if (lat <= 0)
            continue;
        for (int p = 0; p < units[(size_t) u]->numPorts(); ++p)
        {
            const PortDesc pd = units[(size_t) u]->port (p);
            if (pd.dir == PortDir::Out && pd.type == PortType::Audio)
            {
                L[(size_t) u] = lat;
                break;
            }
        }
    }
    std::vector<jidai::jcs::GraphEdge> audioEdges;
    std::vector<int> audioEdgeCable;      // index into graphCables, -1 for order edges
    for (const auto& o : order)
    {
        const int x = unitIndex (o.before), y = unitIndex (o.after);
        if (x >= 0 && y >= 0)
        {
            audioEdges.push_back ({ x, y });
            audioEdgeCable.push_back (-1);
        }
    }
    for (size_t k = 0; k < graphCables.size(); ++k)
        if (status[k] == 0 && sourceAudio[k])
        {
            audioEdges.push_back ({ unitIndex (graphCables[k].source), unitIndex (graphCables[k].dest) });
            audioEdgeCable.push_back ((int) k);
        }
    const auto P = jidai::jcs::pathLatency (L, audioEdges);
    const auto skew = jidai::jcs::arrivalSkew (P, audioEdges);

    int maxP = 0;
    for (size_t k = 0; k < graphCables.size(); ++k)
        if (status[k] >= 0 && intoMain[k] && sourceAudio[k])
            maxP = std::max (maxP, P[(size_t) unitIndex (graphCables[k].source)]);
    for (size_t k = 0; k < graphCables.size(); ++k)
    {
        CableInfo& ci = info_[from[k]];
        ci.live = status[k] >= 0;
        ci.feedback = status[k] == 1;
        if (ci.live && intoMain[k])
        {
            const int p = sourceAudio[k] ? P[(size_t) unitIndex (graphCables[k].source)] : 0;
            ci.comp = maxP - p;
            graphCables[k].delay = ci.comp;
        }
    }
    for (size_t e = 0; e < audioEdges.size(); ++e)
        if (audioEdgeCable[e] >= 0)
            info_[from[(size_t) audioEdgeCable[e]]].skew = skew[e];

    devicePath_.assign (devices_.size(), 0);
    for (int u = 0; u < n; ++u)
        devicePath_[(size_t) unitDevice[(size_t) u]] = std::max (devicePath_[(size_t) unitDevice[(size_t) u]], P[(size_t) u]);
    latency_.store (maxP);

    graph_.install (RackGraph::build (units, graphCables, order));
}

bool Rack::process (const float* inL, const float* inR, float* outL, float* outR, int n, const MidiNote* midi, int numMidi)
{
    std::unique_lock<std::mutex> g (lock_, std::try_to_lock);
    if (! g.owns_lock())
    {
        for (int i = 0; i < n; ++i)
        {
            if (outL != nullptr) outL[i] = 0.0f;
            if (outR != nullptr) outR[i] = 0.0f;
        }
        return false;
    }

    for (auto& d : devices_)
    {
        d->setTransport (transport_);
        d->beginBlock();
    }
    if (rackIO_ != nullptr)
        rackIO_->setBlockMidi (midi, numMidi);

    float pk[4] {};
    for (int i = 0; i < n; ++i)
    {
        const float l = inL != nullptr ? inL[i] : 0.0f;
        const float r = inR != nullptr ? inR[i] : l;
        if (rackIO_ != nullptr)
            rackIO_->setHostSample (i, l * 5.0f, r * 5.0f);

        graph_.process();

        for (auto* b : bushidos_)
            b->collectMidi (i);
        float ol = 0.0f, orr = 0.0f;
        if (rackIO_ != nullptr)
        {
            ol = rackIO_->mainLeftVolts() * 0.2f;
            orr = rackIO_->mainRightVolts() * 0.2f;
        }
        if (outL != nullptr) outL[i] = ol;
        if (outR != nullptr) outR[i] = orr;
        pk[0] = std::max (pk[0], std::fabs (l));
        pk[1] = std::max (pk[1], std::fabs (r));
        pk[2] = std::max (pk[2], std::fabs (ol));
        pk[3] = std::max (pk[3], std::fabs (orr));
    }
    if (rackIO_ != nullptr)
        rackIO_->updateMeters (pk[0], pk[1], pk[2], pk[3], n);
    if (transport_.valid && transport_.playing)
        transport_.ppq += transport_.bpm / 60.0 * (double) n / sampleRate_;   // in case the host does not update it
    return true;
}

} // namespace jidai

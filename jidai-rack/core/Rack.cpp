// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "Rack.h"

#include <algorithm>

namespace jidai {

const char* deviceKindName (DeviceKind kind)
{
    switch (kind)
    {
        case DeviceKind::Bushido: return "BUSHIDO";
        case DeviceKind::Ronin: return "RONIN";
        case DeviceKind::Origami: return "ORIGAMI";
    }
    return "";
}
const char* deviceKindPrefix (DeviceKind kind) { return deviceKindName (kind); }

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
    if (number > 0)
        for (auto& d : devices_)
            if (d->kind() == kind && d->number == number)
                return nullptr;
    std::unique_ptr<Device> made;
    if (kind == DeviceKind::Ronin)
        made = std::make_unique<RoninDevice>();
    else if (kind == DeviceKind::Origami)
        made = std::make_unique<OrigamiDevice>();
    else
        made = std::make_unique<BushidoDevice>();
    made->number = number > 0 ? number : nextNumber (kind);
    made->prepare (sampleRate_);
    Device* d = made.get();

    std::vector<CableSpec> internal;
    if (auto* ronin = dynamic_cast<RoninDevice*> (d))
    {
        std::vector<std::pair<int, int>> pairs;
        ronin->loadProgram (ronin->program(), pairs);
        for (size_t i = 0; i < pairs.size(); ++i)
            internal.push_back ({ jackId (*d, pairs[i].first), jackId (*d, pairs[i].second), (int) (i % 4), 0 });
    }

    {
        std::lock_guard<std::mutex> g (lock_);
        if (position < 0 || position > (int) devices_.size())
            position = (int) devices_.size();
        devices_.insert (devices_.begin() + position, std::move (made));
        for (auto& c : internal)
        {
            c.age = nextAge_++;
            cables_.push_back (c);
        }
        rebuild();
    }
    return d;
}

bool Rack::removeDevice (Device* device)
{
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
    std::lock_guard<std::mutex> g (lock_);
    const int index = indexOf (device);
    if (index < 0)
        return false;
    auto keep = std::move (devices_[(size_t) index]);
    devices_.erase (devices_.begin() + index);
    position = std::clamp (position, 0, (int) devices_.size());
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
    cables_.push_back ({ a, b, color, nextAge_++ });
    rebuild();
    return Check::Ok;
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
            internal.push_back ({ jackId (*ronin, pairs[i].first), jackId (*ronin, pairs[i].second), (int) (i % 4), 0 });
    }
    replaceInternalCables (ronin, internal);
    return true;
}

// Caller holds lock_. Allocates (message thread), then swaps the routing in; the old one is freed here too.
void Rack::rebuild()
{
    std::vector<Unit*> units;
    ronins_.clear();
    bushidos_.clear();
    for (auto& d : devices_)
    {
        for (Unit* u : d->units())
            units.push_back (u);
        if (auto* r = dynamic_cast<RoninDevice*> (d.get()))
            ronins_.push_back (r);
        if (auto* b = dynamic_cast<BushidoDevice*> (d.get()))
            bushidos_.push_back (b);
    }

    auto byAge = cables_;
    std::stable_sort (byAge.begin(), byAge.end(), [] (const CableSpec& x, const CableSpec& y) { return x.age < y.age; });
    std::vector<GraphCable> graphCables;
    for (const auto& c : byAge)
    {
        Device* da = nullptr;
        Device* db = nullptr;
        int ja = -1, jb = -1;
        if (! resolve (c.a, da, ja) || ! resolve (c.b, db, jb))
            continue;
        const JackDesc& A = da->jacks()[(size_t) ja];
        const JackDesc& B = db->jacks()[(size_t) jb];
        if (A.unit == nullptr || B.unit == nullptr || A.desc.dir == B.desc.dir)
            continue;    // output to output or input to input carries nothing
        const JackDesc& out = A.desc.dir == PortDir::Out ? A : B;
        const JackDesc& in = A.desc.dir == PortDir::Out ? B : A;
        graphCables.push_back ({ out.unit, out.port, in.unit, in.port });
    }
    graph_.install (RackGraph::build (units, graphCables));
}

bool Rack::process (const float* inL, const float* inR, float* outL, float* outR, int n)
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
        d->beginBlock();

    for (int i = 0; i < n; ++i)
    {
        const float l = inL != nullptr ? inL[i] : 0.0f;
        const float r = inR != nullptr ? inR[i] : l;
        for (size_t k = 0; k < ronins_.size(); ++k)
        {
            if (k == 0)
                ronins_[k]->setHostSample (l, r);
            else
                ronins_[k]->setHostSample (0.0f, 0.0f);
        }

        graph_.process();

        for (auto* b : bushidos_)
            b->collectMidi (i);
        float sumL = 0.0f, sumR = 0.0f;
        for (auto* ronin : ronins_)
        {
            sumL += ronin->hostLeft();
            sumR += ronin->hostRight();
        }
        if (outL != nullptr) outL[i] = sumL;
        if (outR != nullptr) outR[i] = sumR;
    }
    return true;
}

} // namespace jidai

// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "OrigamiDevice.h"

namespace jidai {

namespace {
struct JackInfo { const char* id; PortType type; PortDir dir; };
constexpr JackInfo kJacks[OrigamiDevice::kJackCount] = {
    { "IN:IN L", PortType::Audio, PortDir::In },
    { "IN:IN R", PortType::Audio, PortDir::In },
    { "VCA:VCA CV", PortType::CV, PortDir::In },
    { "VC:VC 1", PortType::CV, PortDir::In },
    { "VC:VC 2", PortType::CV, PortDir::In },
    { "VC:VC 3", PortType::CV, PortDir::In },
    { "OUT:OUT L", PortType::Audio, PortDir::Out },
    { "OUT:OUT R", PortType::Audio, PortDir::Out },
    { "HOST:IN L", PortType::Audio, PortDir::In },
    { "HOST:IN R", PortType::Audio, PortDir::In },
    { "HOST:OUT L", PortType::Audio, PortDir::Out },
    { "HOST:OUT R", PortType::Audio, PortDir::Out },
};
}

class OrigamiDevice::CoreUnit : public Unit {
public:
    explicit CoreUnit (origami::OrigamiCore& c) : core (c) {}
    int numPorts() const override { return kJackCount; }
    PortDesc port (int i) const override { return { kJacks[i].id, kJacks[i].type, kJacks[i].dir, 0.0f, false }; }
    float* values() override { return value; }
    bool* connected() override { return isConnected; }
    // Plain volts on every input (no logic-gate promotion into ORIGAMI).
    bool plainVoltGates() const override { return true; }

    void processSample() override
    {
        origami::OrigamiCore::Inputs in;
        // Normals: HOST IN R <- HOST IN L; IN L <- HOST IN L; IN R <- IN L if patched, else HOST IN R.
        const float hostL = value[HostInL];
        const float hostR = isConnected[HostInR] ? value[HostInR] : hostL;
        in.inL = isConnected[InL] ? value[InL] : hostL;
        in.inR = isConnected[InR] ? value[InR] : (isConnected[InL] ? value[InL] : hostR);
        in.vcaCv = value[VcaCv];
        in.vcaPatched = isConnected[VcaCv];
        for (int i = 0; i < 3; ++i)
        {
            in.vc[i] = value[Vc1 + i];
            in.vcPatched[i] = isConnected[Vc1 + i];
        }
        double l = 0.0, r = 0.0;
        core.process (in, l, r);
        value[OutL] = value[HostOutL] = (float) l;
        value[OutR] = value[HostOutR] = (float) r;
    }

    origami::OrigamiCore& core;
    float value[kJackCount] {};
    bool isConnected[kJackCount] {};
};

OrigamiDevice::OrigamiDevice()
{
    for (int p = 0; p < origami::kParamCount; ++p)
        params_[(size_t) p].store (origami::paramInfo (p).def);
    unit_ = std::make_unique<CoreUnit> (core_);
    units_.push_back (unit_.get());
    for (int i = 0; i < kJackCount; ++i)
    {
        JackDesc j;
        j.id = kJacks[i].id;
        j.unit = unit_.get();
        j.port = i;
        j.desc = unit_->port (i);
        j.backOnly = i >= kFrontJacks;
        jacks_.push_back (j);
    }
}

OrigamiDevice::~OrigamiDevice() = default;

std::vector<JackGroup> OrigamiDevice::jackGroups() const
{
    return { { "INPUT", { InL, InR, VcaCv } }, { "VC (AUDIO RATE OK)", { Vc1, Vc2, Vc3 } },
             { "OUTPUT", { OutL, OutR } }, { "HOST (NORMALS)", { HostInL, HostInR, HostOutL, HostOutR } } };
}

void OrigamiDevice::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    for (int p = 0; p < origami::kParamCount; ++p)
        core_.setParam (p, params_[(size_t) p].load());
    core_.prepare (sampleRate);
    latency_.store (core_.latencySamples());
    dirty_.store (false);
}

void OrigamiDevice::beginBlock()
{
    if (! dirty_.exchange (false))
        return;
    for (int p = 0; p < origami::kParamCount; ++p)
    {
        const double v = params_[(size_t) p].load (std::memory_order_relaxed);
        if (v != core_.param (p))
            core_.setParam (p, v);
    }
    latency_.store (core_.latencySamples());
}

int OrigamiDevice::latencySamples() const
{
    // The QUALITY setting decides the latency; report the pending value so the rack's compensation can be
    // rebuilt on the message thread right after the change.
    return params_[(size_t) origami::kQuality].load() >= 0.5 ? origami::kLatency2x : 0;
}

double OrigamiDevice::param (int p) const
{
    return p >= 0 && p < origami::kParamCount ? params_[(size_t) p].load() : 0.0;
}

void OrigamiDevice::setParam (int p, double value)
{
    if (p < 0 || p >= origami::kParamCount)
        return;
    params_[(size_t) p].store (origami::clampParam (p, value));
    dirty_.store (true);
}

void OrigamiDevice::setParam (const std::string& id, double value)
{
    setParam (origami::paramIndex (id), value);
}

double OrigamiDevice::stageCurve (int stage, double x) const
{
    double v[origami::kParamCount];
    for (int p = 0; p < origami::kParamCount; ++p) v[p] = params_[(size_t) p].load();
    return origami::OrigamiCore::stageCurve (v, stage, x);
}

float OrigamiDevice::jackVolts (int jack) const
{
    return jack >= 0 && jack < kJackCount ? unit_->value[jack] : 0.0f;
}

} // namespace jidai

// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "RoninDevice.h"

#include "Modular/EffectSwitch.h"

#include <algorithm>
#include <cmath>

namespace jidai {

// A RONIN module as a unit of the rack graph. RONIN modules already keep one float and one flag per port.
class RoninDevice::ModuleUnit : public Unit {
public:
    ModuleUnit (Module& m, int strigPort) : module (m), strig (strigPort) {}
    int numPorts() const override { return module.numPorts(); }
    PortDesc port (int index) const override { return module.port (index); }
    float* values() override { return module.portValue; }
    bool* connected() override { return module.inputConnected; }
    void processSample() override { module.processSample(); }
    bool strigInput (int p) const override { return p == strig; }    // EG 1 / EG 2 TRIG (JCS R3s)
    Module& module;
    int strig = -1;
};

// HOST:IN L/R -> EXT IN's host input (volts / 5 = host units). IN R is normalled to IN L.
class RoninDevice::HostInUnit : public Unit {
public:
    explicit HostInUnit (ExtIn& e) : ext (e) {}
    int numPorts() const override { return 2; }
    PortDesc port (int i) const override { return { i == 0 ? kHostInL : kHostInR, PortType::Audio, PortDir::In }; }
    float* values() override { return value; }
    bool* connected() override { return isConnected; }
    void processSample() override
    {
        const float l = value[0], r = isConnected[1] ? value[1] : value[0];
        ext.setHostSample (l * 0.2f, r * 0.2f);
    }
    ExtIn& ext;
    float value[2] {};
    bool isConnected[2] {};
};

// OUTPUT's host buffer -> HOST:OUT L/R (host units x 5 = volts).
class RoninDevice::HostOutUnit : public Unit {
public:
    explicit HostOutUnit (OutputModule& o) : out (o) {}
    int numPorts() const override { return 2; }
    PortDesc port (int i) const override { return { i == 0 ? kHostOutL : kHostOutR, PortType::Audio, PortDir::Out }; }
    float* values() override { return value; }
    bool* connected() override { return isConnected; }
    void processSample() override
    {
        value[0] = out.hostLeft() * 5.0f;
        value[1] = out.hostRight() * 5.0f;
    }
    OutputModule& out;
    float value[2] {};
    bool isConnected[2] {};
};

RoninDevice::RoninDevice()
{
    // RONIN's addModule order. PanelGeometry.inc numbers these 1..16.
    Module* order[kModules] = { &extIn, &output, &noise, &vcf, &vca1, &vca2, &eg1, &mg,
                                &vco, &eg2, &ring, &divider, &inverter, &integrator, &mixer, &sampleHold };
    for (Module* m : order)
    {
        programGraph.addModule (*m);
        const int strig = m == &eg1 ? Eg1::kTrig : (m == &eg2 ? Eg2::kTrig : -1);
        moduleUnits_.push_back (std::make_unique<ModuleUnit> (*m, strig));
        units_.push_back (moduleUnits_.back().get());
    }
    hostIn_ = std::make_unique<HostInUnit> (extIn);
    hostOut_ = std::make_unique<HostOutUnit> (output);
    units_.insert (units_.begin(), hostIn_.get());      // first, so a free choice of order keeps it before EXT IN
    units_.push_back (hostOut_.get());

    for (int i = 0; i < kPanelJackCount; ++i)
    {
        const PanelJackRec& rec = kPanelJacks[i];
        JackDesc j;
        j.id = std::string (rec.section) + ":" + rec.label;
        if (rec.module >= 1 && rec.module <= kModules && rec.dir >= 0)
        {
            j.unit = moduleUnits_[(size_t) (rec.module - 1)].get();
            j.port = rec.port;
            j.desc = j.unit->port (rec.port);
        }
        else
        {
            j.desc.dir = rec.dir == 1 ? PortDir::Out : PortDir::In;
        }
        jacks_.push_back (j);
    }

    // The back-only HOST jacks, after the panel jacks (panel jack indices stay RONIN's own).
    for (int i = 0; i < 4; ++i)
    {
        JackDesc j;
        j.unit = i < 2 ? (Unit*) hostIn_.get() : (Unit*) hostOut_.get();
        j.port = i % 2;
        j.desc = j.unit->port (j.port);
        j.id = j.desc.name;
        j.backOnly = true;
        jacks_.push_back (j);
    }

    for (int i = 0; i < kPanelKnobCount; ++i)
    {
        face_[(size_t) i] = faceKnobBinding (kPanelKnobs[i].section, kPanelKnobs[i].label).knob;
        knobs_[(size_t) i].store (kPanelKnobs[i].valueDefault);
    }

    // A fresh RONIN is the INIT program: its cables, the default table, and Effect on.
    std::vector<std::pair<int, int>> ignored;
    loadProgram (kDefaultFactoryPreset, ignored);
}

RoninDevice::~RoninDevice() = default;

std::vector<const Unit*> RoninDevice::latencyUnits() const
{
    return { moduleUnits_[1].get() };
}

std::vector<OrderEdge> RoninDevice::orderEdges() const
{
    return { { hostIn_.get(), moduleUnits_[0].get() },        // HOST IN before EXT IN
             { moduleUnits_[1].get(), hostOut_.get() } };     // OUTPUT before HOST OUT
}


Module* RoninDevice::moduleAt (int index)
{
    if (index < 0 || index >= kModules)
        return nullptr;
    return &moduleUnits_[(size_t) index]->module;
}

float RoninDevice::knob (int i) const
{
    if (i < 0 || i >= kPanelKnobCount)
        return 0.0f;
    return knobs_[(size_t) i].load();
}

void RoninDevice::setKnob (int i, float value)
{
    if (i < 0 || i >= kPanelKnobCount)
        return;
    knobs_[(size_t) i].store (panelKnobClamp (value, kPanelKnobs[i].kind == 1));
}

void RoninDevice::setFace (FaceKnob face, float value)
{
    for (int i = 0; i < kPanelKnobCount; ++i)
        if (face_[(size_t) i] == face)
            knobs_[(size_t) i].store (value);
}

void RoninDevice::prepare (double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    programGraph.prepare (sampleRate_);
    forceApply_.store (true);
    beginBlock();
}

// RONIN's applyHostControls: each bound knob into its module, then the Output blend and level.
void RoninDevice::beginBlock()
{
    const bool force = forceApply_.exchange (false);
    for (int i = 0; i < kPanelKnobCount; ++i)
    {
        const float v = knobs_[(size_t) i].load();
        const FaceKnob f = face_[(size_t) i];
        if (! force && v == applied_[(size_t) i] && f != FaceKnob::OutputMix)
            continue;
        applied_[(size_t) i] = v;
        switch (f)
        {
            case FaceKnob::VcfCutoff:      vcf.setKnob (Vcf::kKnobCutoff, v); break;
            case FaceKnob::VcfPeak:        vcf.setKnob (Vcf::kKnobPeak, v); break;
            case FaceKnob::VcfAmount:      vcf.setKnob (Vcf::kKnobAmount, v); break;
            case FaceKnob::Vca1Initial:    vca1.setKnob (Vca1::kKnobInitial, v); break;
            case FaceKnob::Vca1Mod:        vca1.setKnob (Vca1::kKnobIntensity, v); break;
            case FaceKnob::Vca1LowCut:     vca1.setKnob (Vca1::kKnobLowCut, v); break;
            case FaceKnob::Vca2Initial:    vca2.setKnob (Vca2::kKnobInitial, v); break;
            case FaceKnob::Vca2Mod:        vca2.setKnob (Vca2::kKnobMod, v); break;
            case FaceKnob::Eg1Attack:      eg1.setKnob (Eg1::kKnobAttack, v); break;
            case FaceKnob::Eg1Decay:       eg1.setKnob (Eg1::kKnobDecay, v); break;
            case FaceKnob::Eg1Sustain:     eg1.setKnob (Eg1::kKnobSustain, v); break;
            case FaceKnob::Eg1Release:     eg1.setKnob (Eg1::kKnobRelease, v); break;
            case FaceKnob::MgRate:         mg.setKnob (MgModule::kKnobFrequency, v); break;
            case FaceKnob::MgPw:           mg.setKnob (MgModule::kKnobPw, v); break;
            case FaceKnob::VcoRange:       vco.setKnob (Vco::kKnobScale, v); break;
            case FaceKnob::VcoFine:        vco.setKnob (Vco::kKnobFine, v); break;
            case FaceKnob::VcoPw:          vco.setKnob (Vco::kKnobPw, v); break;
            case FaceKnob::VcoFm1:         vco.setKnob (Vco::kKnobAmountA, v); break;
            case FaceKnob::VcoFm2:         vco.setKnob (Vco::kKnobAmountB, v); break;
            case FaceKnob::Eg2Hold:        eg2.setKnob (Eg2::kKnobHold, v); break;
            case FaceKnob::Eg2Delay:       eg2.setKnob (Eg2::kKnobDelay, v); break;
            case FaceKnob::Eg2Attack:      eg2.setKnob (Eg2::kKnobAttack, v); break;
            case FaceKnob::Eg2Release:     eg2.setKnob (Eg2::kKnobRelease, v); break;
            case FaceKnob::IntegratorTime: integrator.setKnob (Integrator::kKnobTime, v); break;
            case FaceKnob::MixerLevel1:    mixer.setKnob (Mixer::kKnobLevel1, v); break;
            case FaceKnob::MixerLevel2:    mixer.setKnob (Mixer::kKnobLevel2, v); break;
            case FaceKnob::MixerLevel3:    mixer.setKnob (Mixer::kKnobLevel3, v); break;
            case FaceKnob::SampleHoldRate: sampleHold.setKnob (SampleHold::kKnobRate, v); break;
            case FaceKnob::ExtInThreshold: extIn.setKnob (ExtIn::kKnobThreshold, v); break;
            case FaceKnob::ExtInRelease:   extIn.setKnob (ExtIn::kKnobRelease, v); break;
            case FaceKnob::OutputMix:      // effect off is dry; on blends dry * (1 - mix) + wet * mix
                output.setMix (outputMixAfterSwitch (effectOn_.load(), v));
                break;
            case FaceKnob::OutputLevel:    output.setOutputLevel (v); break;
            case FaceKnob::DividerRatio:   // a saved setting: both /2 and /4 always run
            case FaceKnob::None:
                break;
        }
    }
}

bool RoninDevice::loadProgram (int index, std::vector<std::pair<int, int>>& cables)
{
    if (! loadFactoryPreset (programGraph, index))
        return false;
    program_.store (index);

    // RONIN's applyProgramParameters: one default table, with each program's overrides.
    effectOn_.store (factoryPresetEffect (index));
    const FactoryProgramKnobs k = factoryProgramKnobs (index);
    setFace (FaceKnob::VcfCutoff, k.vcfCutoff);
    setFace (FaceKnob::VcfPeak, k.vcfPeak);
    setFace (FaceKnob::VcfAmount, PanelDefault::kVcfMod);
    setFace (FaceKnob::Vca1Initial, factoryVca1Initial (index));
    setFace (FaceKnob::Vca1Mod, PanelDefault::kVca1Mod);
    setFace (FaceKnob::Vca1LowCut, PanelDefault::kVca1LowCut);
    setFace (FaceKnob::Vca2Initial, PanelDefault::kVca2Initial);
    setFace (FaceKnob::Vca2Mod, PanelDefault::kVca2Mod);
    setFace (FaceKnob::Eg1Attack, k.eg1Attack);
    setFace (FaceKnob::Eg1Decay, k.eg1Decay);
    setFace (FaceKnob::Eg1Sustain, k.eg1Sustain);
    setFace (FaceKnob::Eg1Release, k.eg1Release);
    setFace (FaceKnob::MgRate, factoryMgRate (index));
    setFace (FaceKnob::MgPw, PanelDefault::kMgPw);
    setFace (FaceKnob::VcoRange, k.vcoRange);
    setFace (FaceKnob::VcoFine, PanelDefault::kVcoFine);
    setFace (FaceKnob::VcoPw, PanelDefault::kVcoPw);
    setFace (FaceKnob::VcoFm1, PanelDefault::kVcoFm1);
    setFace (FaceKnob::VcoFm2, PanelDefault::kVcoFm2);
    setFace (FaceKnob::Eg2Hold, PanelDefault::kEg2Hold);
    setFace (FaceKnob::Eg2Delay, PanelDefault::kEg2Delay);
    setFace (FaceKnob::Eg2Attack, PanelDefault::kEg2Attack);
    setFace (FaceKnob::Eg2Release, PanelDefault::kEg2Release);
    setFace (FaceKnob::IntegratorTime, factoryIntegratorTime (index));
    setFace (FaceKnob::MixerLevel1, PanelDefault::kMixerLevel);
    setFace (FaceKnob::MixerLevel2, PanelDefault::kMixerLevel);
    setFace (FaceKnob::MixerLevel3, PanelDefault::kMixerLevel);
    setFace (FaceKnob::SampleHoldRate, factorySampleHoldRate (index));
    setFace (FaceKnob::OutputLevel, PanelDefault::kOutputLevel);
    setFace (FaceKnob::OutputMix, PanelDefault::kOutputMix);
    setFace (FaceKnob::ExtInThreshold, PanelDefault::kExtInThreshold);
    setFace (FaceKnob::ExtInRelease, PanelDefault::kExtInRelease);
    setFace (FaceKnob::DividerRatio, PanelDefault::kDividerRatio);
    output.setLevel (1.0f);

    // Knobs first, then prepare, so filter and envelope memory restart on the new program (setCurrentProgram's order).
    forceApply_.store (true);
    beginBlock();
    programGraph.prepare (sampleRate_);

    cables.clear();
    Cable published[PatchGraph::kMaxCables] {};
    const int count = programGraph.copyPublishedCables (published, PatchGraph::kMaxCables);
    auto jackFor = [] (int module, int port)
    {
        for (int i = 0; i < kPanelJackCount; ++i)
            if (kPanelJacks[i].module == module + 1 && kPanelJacks[i].port == port && kPanelJacks[i].dir >= 0)
                return i;
        return -1;
    };
    for (int i = 0; i < count; ++i)
    {
        const int out = jackFor (published[i].sourceModule, published[i].sourcePort);
        const int in = jackFor (published[i].destModule, published[i].destPort);
        if (out >= 0 && in >= 0)
            cables.emplace_back (out, in);
    }
    return true;
}

void RoninDevice::setMeterJack (int jack)
{
    if (jack < 0 || jack >= (int) jacks_.size() || jacks_[(size_t) jack].unit == nullptr)
        return;
    const PanelJackRec& rec = kPanelJacks[jack];
    meterModule_.store (rec.module - 1);
    meterPort_.store (rec.port);
}

float RoninDevice::meterVolts() const
{
    int module = meterModule_.load();
    int port = meterPort_.load();
    if (module < 0)
    {
        module = 1;    // OUTPUT
        port = 2;      // WET
    }
    const float v = moduleUnits_[(size_t) module]->module.portValue[port];
    return std::isfinite (v) ? v : 0.0f;
}

float RoninDevice::jackVolts (int jack) const
{
    if (jack < 0 || jack >= (int) jacks_.size() || jacks_[(size_t) jack].unit == nullptr)
        return 0.0f;
    const auto& j = jacks_[(size_t) jack];
    return j.unit->values()[j.port];
}

} // namespace jidai

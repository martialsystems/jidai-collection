// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "ShogunDevice.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace jidai {

namespace {
PortType rackType (shogun::PortType t)
{
    switch (t)
    {
        case shogun::PortType::Audio: return PortType::Audio;
        case shogun::PortType::Gate:  return PortType::Gate;
        case shogun::PortType::CV:    break;
    }
    return PortType::CV;
}
PortDir rackDir (shogun::PortDir d) { return d == shogun::PortDir::Out ? PortDir::Out : PortDir::In; }

int osFromParam (double u) { return 1 << shogun::stepIndex (u, 3); }   // 1X / 2X / 4X (the SHOGUN plugin's law)

// SHOGUN's generated panel table (plugin/Source/PanelLayout.inc at the pinned commit): only its "jack:<id>" binds are
// read here, the ports SHOGUN's ROUTE bay offers.
struct LayoutOp {
    int kind, tab, flags;
    float x, y, w, h, r, z, v;
    std::uint32_t fill, stroke;
    float sw, opacity;
    const char* text;
    const char* text2;
    const char* bind;
    int steps, ticks;
};
[[maybe_unused]] constexpr float kNoRing = 0.0f;   // referenced by the generated table
const LayoutOp kOps[] = {
#include "PanelLayout.inc"
};

const std::array<bool, shogun::kPorts>& offeredPorts()
{
    static const std::array<bool, shogun::kPorts> t = [] {
        std::array<bool, shogun::kPorts> a {};
        for (const auto& o : kOps)
            if (std::strncmp (o.bind, "jack:", 5) == 0)
            {
                const int p = shogun::findPort (o.bind + 5);
                if (p >= 0)
                    a[(size_t) p] = true;
            }
        return a;
    }();
    return t;
}
}

bool ShogunDevice::offered (int port) { return port >= 0 && port < shogun::kPorts && offeredPorts()[(size_t) port]; }

int ShogunDevice::offeredJackCount()
{
    int n = 0;
    for (bool b : offeredPorts())
        n += b ? 1 : 0;
    return n;
}

int ShogunDevice::enginePort (int jack) const
{
    return jack >= 0 && jack < (int) portOfJack_.size() ? portOfJack_[(size_t) jack] : -1;
}

int ShogunDevice::jackOfPort (int port) const
{
    return port >= 0 && port < shogun::kPorts ? jackOfPort_[(size_t) port] : -1;
}

class ShogunDevice::EngineUnit : public Unit {
public:
    explicit EngineUnit (shogun::Engine& e) : engine (e)
    {
        for (int i = 0; i < kPortCount; ++i)
            value[i] = shogun::kPortTable[i].dir == shogun::PortDir::In ? shogun::kPortTable[i].rest : 0.0f;
    }
    int numPorts() const override { return kPortCount; }
    PortDesc port (int i) const override
    {
        const auto& p = shogun::kPortTable[i];
        return { p.id, rackType (p.type), rackDir (p.dir), p.rest, false };
    }
    float* values() override { return value; }
    bool* connected() override { return isConnected; }
    bool plainVoltGates() const override { return true; }   // SHOGUN gates are 0/5 V (kPlainVoltGates)
    void processSample() override;

    shogun::Engine& engine;
    const RetUnit* ret = nullptr;    // the RET input stage, read at the start of every sample
    float value[kPortCount] {};
    bool isConnected[kPortCount] {};
};

// The 16 RET inputs (one per voice), run before the engine. The engine unit reads their values and patch state at the
// start of its sample (the graph resets the engine unit's own, unpatched RET ports first). A separate unit so the rack
// can give RET paths their extra latency (upsampler + decimator).
class ShogunDevice::RetUnit : public Unit {
public:
    explicit RetUnit (EngineUnit& e) : engineUnit (e)
    {
        for (int v = 0; v < shogun::kVoices; ++v)
        {
            port_[v] = shogun::isDrum (v) ? shogun::drumPort (v, shogun::DJ_RET) : shogun::synthPort (v - shogun::LEAD, shogun::SJ_RET);
            value[v] = shogun::kPortTable[port_[v]].rest;
        }
    }
    int numPorts() const override { return shogun::kVoices; }
    PortDesc port (int i) const override { return engineUnit.port (port_[i]); }
    float* values() override { return value; }
    bool* connected() override { return isConnected; }
    bool plainVoltGates() const override { return true; }
    void processSample() override {}
    int enginePort (int i) const { return port_[i]; }

    EngineUnit& engineUnit;
    float value[shogun::kVoices] {};
    bool isConnected[shogun::kVoices] {};

private:
    int port_[shogun::kVoices] {};
};

void ShogunDevice::EngineUnit::processSample()
{
    if (ret != nullptr)
        for (int v = 0; v < shogun::kVoices; ++v)
        {
            value[ret->enginePort (v)] = ret->value[v];
            isConnected[ret->enginePort (v)] = ret->isConnected[v];
        }
    engine.processSample (value, isConnected);
}

ShogunDevice::ShogunDevice()
{
    engine_.loadInit();
    for (int p = 0; p < shogun::kParamCount; ++p)
        params_[(size_t) p].store (engine_.param (p));
    edits_.pattern = engine_.pattern();
    for (int r = 0; r < shogun::mod::kRows; ++r)
        edits_.rows[(size_t) r] = engine_.modulation().rows[r];
    for (int i = 0; i < kPortCount; ++i)
    {
        edits_.cvAmt[(size_t) i] = engine_.cvAmt (i);
        edits_.inLaw[(size_t) i] = (std::uint8_t) engine_.inputLaw (i);
    }
    unit_ = std::make_unique<EngineUnit> (engine_);
    ret_ = std::make_unique<RetUnit> (*unit_);
    unit_->ret = ret_.get();
    units_.push_back (ret_.get());
    units_.push_back (unit_.get());
    int retIndex[kPortCount];
    for (int i = 0; i < kPortCount; ++i)
        retIndex[i] = -1;
    for (int v = 0; v < shogun::kVoices; ++v)
        retIndex[ret_->enginePort (v)] = v;
    for (int i = 0; i < kPortCount; ++i)
    {
        jackOfPort_[(size_t) i] = -1;
        if (! offered (i))
            continue;     // in the engine's table, not on SHOGUN's bay: the engine neither reads nor drives it
        jackOfPort_[(size_t) i] = (int) jacks_.size();
        portOfJack_.push_back (i);
        JackDesc j;
        j.id = shogun::kPortTable[i].id;
        j.unit = retIndex[i] >= 0 ? static_cast<Unit*> (ret_.get()) : static_cast<Unit*> (unit_.get());
        j.port = retIndex[i] >= 0 ? retIndex[i] : i;
        j.desc = unit_->port (i);
        j.backOnly = false;     // every SHOGUN jack is on its rear bay; the MAIN face has no jacks
        jacks_.push_back (j);
    }
    os_.store (wantedOs());
    latency_.store (shogun::osLatency (os_.load()));
}

ShogunDevice::~ShogunDevice() = default;

std::vector<const Unit*> ShogunDevice::latencyUnits() const { return { ret_.get(), unit_.get() }; }

std::vector<OrderEdge> ShogunDevice::orderEdges() const { return { { ret_.get(), unit_.get() } }; }

std::vector<JackGroup> ShogunDevice::jackGroups() const
{
    // One group per voice (its 8 jacks, plus WAVE / FOLD VC on the WAVE voices), then the synth voices, CLOCK, MOD
    // and MIX: the same rows as SHOGUN's own patch bay (§13.2). Built from port numbers, kept as jack indices.
    std::vector<JackGroup> g;
    for (int v = 0; v < shogun::kDrumVoices; ++v)
    {
        JackGroup grp { shogun::kVoiceNames[v], {} };
        for (int j = 0; j < shogun::kDrumJacks; ++j)
            grp.jacks.push_back (shogun::drumPort (v, j));
        g.push_back (grp);
    }
    // WAVE / FOLD VC belong to their voices.
    auto addTo = [&] (const char* name, int port) {
        for (auto& grp : g)
            if (grp.title == name) { grp.jacks.push_back (port); return; }
    };
    addTo ("BD1", shogun::PORT_BD1_WAVE);
    addTo ("BD1", shogun::PORT_FOLD_VC_BD1);
    addTo ("BD2", shogun::PORT_BD2_WAVE);
    addTo ("BD2", shogun::PORT_FOLD_VC_BD2);
    addTo ("LTC", shogun::PORT_FOLD_VC_LTC);
    addTo ("MTC", shogun::PORT_FOLD_VC_MTC);
    addTo ("HTC", shogun::PORT_FOLD_VC_HTC);
    for (int s = 0; s < 2; ++s)
    {
        JackGroup grp { s == 0 ? "LEAD" : "BASS", {} };
        for (int j = 0; j < shogun::kSynthJacks; ++j)
            grp.jacks.push_back (shogun::synthPort (s, j));
        grp.jacks.push_back (s == 0 ? shogun::PORT_LD_GATE : shogun::PORT_BS_GATE);
        g.push_back (grp);
    }
    g.push_back ({ "CLOCK", { shogun::PORT_CLK_IN, shogun::PORT_RST_IN, shogun::PORT_RUN_IN, shogun::PORT_FILL_IN,
                              shogun::PORT_CLK_OUT, shogun::PORT_RST_OUT, shogun::PORT_RUN_OUT, shogun::PORT_ACC_OUT } });
    g.push_back ({ "MOD", { shogun::PORT_LFO1, shogun::PORT_LFO2, shogun::PORT_LFO3, shogun::PORT_LFO4, shogun::PORT_RND,
                            shogun::PORT_LANE_A } });
    g.push_back ({ "MIX", { shogun::PORT_MIX_L, shogun::PORT_MIX_R } });
    for (auto& grp : g)
    {
        std::vector<int> jacks;
        for (int port : grp.jacks)
            if (jackOfPort (port) >= 0)
                jacks.push_back (jackOfPort (port));
        grp.jacks = jacks;
    }
    return g;
}

jidai::jcs::Role ShogunDevice::jackRole (int jack) const
{
    const int port = enginePort (jack);
    return port >= 0 ? shogun::kPortTable[port].role : Device::jackRole (jack);
}

int ShogunDevice::wantedOs() const
{
    return osFromParam (params_[(size_t) shogun::P_GLOBAL_OS].load());
}

void ShogunDevice::prepare (double sampleRate)
{
    // Message thread, with the rack's audio stopped (Rack holds its lock): allocates.
    sampleRate_ = sampleRate;
    const int os = wantedOs();
    engine_.prepare (sampleRate, os);
    for (int p = 0; p < shogun::kParamCount; ++p)
    {
        applied_[(size_t) p] = params_[(size_t) p].load();
        engine_.setParamNow (p, applied_[(size_t) p]);
    }
    {
        std::lock_guard<std::mutex> l (editLock_);
        engine_.setPattern (edits_.pattern);
        for (int r = 0; r < shogun::mod::kRows; ++r)
            engine_.modulation().rows[r] = edits_.rows[(size_t) r];
        for (int i = 0; i < kPortCount; ++i)
        {
            engine_.setCvAmt (i, edits_.cvAmt[(size_t) i]);
            engine_.setInputLaw (i, edits_.inLaw[(size_t) i]);
        }
        appliedEpoch_ = editEpoch_.load();
    }
    if (runRequest_.load() > 0)
        engine_.setRunning (true);
    os_.store (os);
    latency_.store (engine_.latencySamples());
    dirty_.store (false);
}

void ShogunDevice::beginBlock()
{
    if (dirty_.exchange (false))
        for (int p = 0; p < shogun::kParamCount; ++p)
        {
            const double v = params_[(size_t) p].load (std::memory_order_relaxed);
            if (v != applied_[(size_t) p])
            {
                applied_[(size_t) p] = v;
                engine_.setParam (p, v);
            }
        }
    if (editEpoch_.load() != appliedEpoch_)
    {
        std::unique_lock<std::mutex> l (editLock_, std::try_to_lock);
        if (l.owns_lock())
        {
            engine_.setPattern (edits_.pattern);
            for (int r = 0; r < shogun::mod::kRows; ++r)
                engine_.modulation().rows[r] = edits_.rows[(size_t) r];
            for (int i = 0; i < kPortCount; ++i)
            {
                engine_.setCvAmt (i, edits_.cvAmt[(size_t) i]);
                engine_.setInputLaw (i, edits_.inLaw[(size_t) i]);
            }
            appliedEpoch_ = editEpoch_.load();
        }
    }
    const int run = runRequest_.exchange (-1);
    if (run >= 0)
        engine_.setRunning (run > 0);
    if (restartRequest_.exchange (false))
        engine_.restart();
    const std::uint32_t pads = padMask_.exchange (0);
    for (int v = 0; v < shogun::kVoices; ++v)
        if (pads & (1u << (unsigned) v))
            engine_.trigger (v);

    // Meters of the previous block (relaxed; the UI decays them).
    meters_[0].store (std::abs (unit_->value[shogun::PORT_MIX_L]) / 5.0f, std::memory_order_relaxed);
    meters_[1].store (std::abs (unit_->value[shogun::PORT_MIX_R]) / 5.0f, std::memory_order_relaxed);
    for (int v = 0; v < shogun::kVoices; ++v)
        voiceMeters_[(size_t) v].store (engine_.voiceActive (v) ? 1.0f : 0.0f, std::memory_order_relaxed);
    // On CLOCK:SOURCE HOST the engine follows the host transport without its own RUN latch.
    const bool hostClock = shogun::stepIndex (applied_[(size_t) shogun::P_CLOCK_SOURCE], 3) == shogun::SRC_HOST;
    running_.store (engine_.running() || (hostClock && hostPlaying_), std::memory_order_relaxed);
    step_.store (engine_.displayStep(), std::memory_order_relaxed);
    globalStep_.store (engine_.globalStep(), std::memory_order_relaxed);
}

void ShogunDevice::setTransport (const Transport& t)
{
    shogun::HostTransport h;
    h.valid = t.valid;
    h.playing = t.playing;
    h.ppq = t.ppq;
    h.bpm = t.bpm;
    hostPlaying_ = t.valid && t.playing;
    engine_.setHostTransport (h);
}

double ShogunDevice::param (int id) const
{
    return id >= 0 && id < shogun::kParamCount ? params_[(size_t) id].load() : 0.0;
}

void ShogunDevice::setParam (int id, double u)
{
    if (id < 0 || id >= shogun::kParamCount)
        return;
    // u through float, as the SHOGUN plugin's host parameters hold it (engine/patch.h applyPatch does the same), so a
    // patch loads bit-identical here and in the plugin. Stepped controls read stepIndex (u, N) in the engine.
    const double v = (double) (float) (u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u));
    params_[(size_t) id].store (v);
    dirty_.store (true);
}

ShogunDevice::Edits ShogunDevice::edits() const
{
    std::lock_guard<std::mutex> l (editLock_);
    return edits_;
}

void ShogunDevice::setEdits (const Edits& e)
{
    {
        std::lock_guard<std::mutex> l (editLock_);
        edits_ = e;
    }
    editEpoch_.fetch_add (1);
}

void ShogunDevice::loadInit()
{
    // Engine::loadInit: every parameter at its default, CV AMT 1, plain input laws, empty matrix and pattern.
    for (int p = 0; p < shogun::kParamCount; ++p)
        setParam (p, shogun::kParams[p].def);
    Edits e;
    e.pattern = shogun::Pattern {};
    e.rows.fill (shogun::mod::Row {});
    e.cvAmt.fill (1.0);
    e.inLaw.fill (0);
    setEdits (e);
}

float ShogunDevice::jackVolts (int jack) const
{
    const int port = enginePort (jack);
    return port >= 0 ? unit_->value[port] : 0.0f;
}

} // namespace jidai

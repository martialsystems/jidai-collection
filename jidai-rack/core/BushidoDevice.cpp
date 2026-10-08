// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "BushidoDevice.h"

#include "rack/HzPerVolt.h"

namespace jidai {

// The BUSHIDO engine as one graph unit. Port types follow the web rack (web/bushido_dsp.js):
// inputs read raw volts, so they are CV (MIXER IN 1/2 are Audio); GATE and TRIG outputs are 0/5 V logic, typed Gate,
// so RONIN's S-15 law turns them into S-trig volts when they feed a RONIN CV or audio input.
class BushidoDevice::EngineUnit : public Unit {
public:
    static constexpr int kMaxJacks = 32;

    EngineUnit (BushidoModule& engine, const std::atomic<bool>& bypass) : sq (engine), bypassed (bypass)
    {
        const auto& jacks = sq.jacks();
        count = (int) jacks.size();
        for (int i = 0; i < count && i < kMaxJacks; ++i)
        {
            const bool in = jacks[(size_t) i].dir == rack::Dir::In;
            inPtr[i] = in ? &value[i] : &zero;
            outPtr[i] = in ? &scratch[i] : &value[i];
        }
    }

    int numPorts() const override { return count; }

    PortDesc port (int i) const override
    {
        const char* name = sq.jacks()[(size_t) i].id.c_str();
        const PortDir dir = i < BushidoModule::CV_A ? PortDir::In : PortDir::Out;
        PortType type = PortType::CV;
        if (i == BushidoModule::MIX_IN1 || i == BushidoModule::MIX_IN2 || i == BushidoModule::MIX_OUT)
            type = PortType::Audio;
        if (i == BushidoModule::GATE_A || i == BushidoModule::GATE_B || i >= BushidoModule::TRIG1)
            type = PortType::Gate;
        return { name, type, dir, 0.0f, false };
    }

    float* values() override { return value; }
    bool* connected() override { return isConnected; }
    bool plainVoltGates() const override { return true; }

    void processSample() override
    {
        zero = 0.0f;
        sq.process (inPtr, outPtr, 1);
        if (bypassed.load (std::memory_order_relaxed))
        {
            value[BushidoModule::GATE_A] = 0.0f;
            value[BushidoModule::GATE_B] = 0.0f;
        }
    }

    BushidoModule& sq;
    const std::atomic<bool>& bypassed;
    int count = 0;
    float value[kMaxJacks] {};
    float scratch[kMaxJacks] {};
    bool isConnected[kMaxJacks] {};
    float zero = 0.0f;
    const float* inPtr[kMaxJacks] {};
    float* outPtr[kMaxJacks] {};
};

BushidoDevice::BushidoDevice()
{
    unit_ = std::make_unique<EngineUnit> (sq, bypass_);
    units_.push_back (unit_.get());
    const auto& jacks = sq.jacks();
    for (int i = 0; i < (int) jacks.size(); ++i)
    {
        JackDesc j;
        j.id = jacks[(size_t) i].id;
        j.unit = unit_.get();
        j.port = i;
        j.desc = unit_->port (i);
        jacks_.push_back (j);
    }
    sq.prepare (48000.0, 1);
}

BushidoDevice::~BushidoDevice() = default;

void BushidoDevice::prepare (double sampleRate)
{
    sq.prepare (sampleRate > 0.0 ? sampleRate : 48000.0, 1);
}

void BushidoDevice::beginBlock()
{
    noteCount_ = 0;
}

void BushidoDevice::collectMidi (int sampleInBlock)
{
    auto add = [this, sampleInBlock] (int channel, int note, bool on)
    {
        if (noteCount_ < kMaxNoteEvents)
            notes_[(size_t) noteCount_++] = { sampleInBlock, channel, note, on };
    };
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* v = unit_->value;
        const bool high = ! bypass_.load (std::memory_order_relaxed) && v[ch == 0 ? BushidoModule::GATE_A : BushidoModule::GATE_B] > 1.0f;
        if (high && ! gatePrev_[ch])
        {
            if (midiNote_[ch] >= 0)
                add (ch + 1, midiNote_[ch], false);
            midiNote_[ch] = rack::hzv::midiNote (v[ch == 0 ? BushidoModule::CV_A : BushidoModule::CV_B]);
            if (midiNote_[ch] >= 0)
                add (ch + 1, midiNote_[ch], true);
        }
        else if (! high && gatePrev_[ch] && midiNote_[ch] >= 0)
        {
            add (ch + 1, midiNote_[ch], false);
            midiNote_[ch] = -1;
        }
        gatePrev_[ch] = high;
    }
}

int BushidoDevice::paramIndex (const std::string& id) const
{
    const auto& params = sq.params();
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].id == id)
            return (int) i;
    return -1;
}

void BushidoDevice::setParam (const std::string& id, float value)
{
    const int i = paramIndex (id);
    if (i >= 0)
        sq.setParam (i, value);
}

float BushidoDevice::param (const std::string& id) const
{
    const int i = paramIndex (id);
    return i >= 0 ? sq.getParam (i) : 0.0f;
}

void BushidoDevice::press (const std::string& id, bool down)
{
    const int i = paramIndex (id);
    if (i >= 0)
        sq.setParam (i, down ? 1.0f : 0.0f);
}

float BushidoDevice::indicator (const std::string& id) const
{
    const auto& list = sq.indicators();
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].id == id)
            return sq.indicator ((int) i);
    return 0.0f;
}

float BushidoDevice::jackVolts (int jack) const
{
    if (jack < 0 || jack >= unit_->count)
        return 0.0f;
    return unit_->value[jack];
}

} // namespace jidai

// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "RackIODevice.h"

#include "jidai/jcs/Detect.h"
#include "jidai/jcs/Pitch.h"

#include <cmath>

namespace jidai {

namespace {
struct JackInfo { const char* id; PortType type; PortDir dir; };
constexpr JackInfo kJacks[RackIODevice::kJackCount] = {
    { "HOST:IN L", PortType::Audio, PortDir::Out },
    { "HOST:IN R", PortType::Audio, PortDir::Out },
    { "MAIN:OUT L", PortType::Audio, PortDir::In },
    { "MAIN:OUT R", PortType::Audio, PortDir::In },
    { "MIDI:NOTE", PortType::CV, PortDir::Out },
    { "MIDI:HZ/V LIN", PortType::CV, PortDir::Out },
    { "MIDI:GATE", PortType::Gate, PortDir::Out },
    { "MIDI:VEL", PortType::CV, PortDir::Out },
    { "TRANSPORT:CLK 1/16", PortType::Gate, PortDir::Out },
    { "TRANSPORT:RUN", PortType::Gate, PortDir::Out },
    { "TRANSPORT:RESET", PortType::Gate, PortDir::Out },
};
constexpr int kOutJacks[] = { RackIODevice::HostInL, RackIODevice::HostInR, RackIODevice::MidiNoteJack, RackIODevice::MidiHzv,
                              RackIODevice::MidiGate, RackIODevice::MidiVel, RackIODevice::Clk16, RackIODevice::Run, RackIODevice::Reset };
constexpr int kOutCount = (int) (sizeof (kOutJacks) / sizeof (kOutJacks[0]));
}

// Host input, MIDI -> CV and transport: every output jack.
class RackIODevice::InUnit : public Unit {
public:
    int numPorts() const override { return kOutCount; }
    PortDesc port (int i) const override { const auto& j = kJacks[kOutJacks[i]]; return { j.id, j.type, j.dir, 0.0f, false }; }
    float* values() override { return value; }
    bool* connected() override { return isConnected; }
    bool plainVoltGates() const override { return true; }

    void processSample() override
    {
        value[0] = hostL;
        value[1] = hostR;
        // MIDI events due on this sample (last-note priority, mono).
        while (midiPos < midiCount && midi[midiPos].sample <= sampleIndex)
        {
            const MidiNote& m = midi[midiPos++];
            if (m.on && m.velocity > 0)
            {
                pushNote (m.note);
                velocity = (float) m.velocity / 127.0f * 5.0f;
            }
            else
                popNote (m.note);
        }
        if (heldCount > 0)
        {
            const int note = held[heldCount - 1];
            namespace pitch = jidai::jcs::pitch;
            bool over = false;
            value[2] = (float) pitch::clampPitch (pitch::voltsForNote (pitch::Law::VOct, (double) note), over);
            value[3] = (float) pitch::clampPitch (pitch::voltsForNote (pitch::Law::HzvLin, (double) note), over);
        }
        value[4] = heldCount > 0 ? jidai::jcs::kGateHigh : jidai::jcs::kGateLow;
        value[5] = velocity;

        // Transport.
        if (transport.valid && transport.playing)
        {
            const long long step = (long long) std::floor (ppq * 4.0 + 1e-9);
            if (! wasPlaying)
            {
                resetLeft = pulse;
                clkLeft = pulse;
                lastStep = step;
            }
            else if (step != lastStep)
            {
                clkLeft = pulse;
                lastStep = step;
            }
            ppq += transport.bpm / (60.0 * sampleRate);
        }
        wasPlaying = transport.valid && transport.playing;
        value[6] = clkLeft > 0 ? jidai::jcs::kGateHigh : jidai::jcs::kGateLow;
        value[7] = wasPlaying ? jidai::jcs::kGateHigh : jidai::jcs::kGateLow;
        value[8] = resetLeft > 0 ? jidai::jcs::kGateHigh : jidai::jcs::kGateLow;
        if (clkLeft > 0) --clkLeft;
        if (resetLeft > 0) --resetLeft;
        ++sampleIndex;
    }

    void pushNote (int n)
    {
        popNote (n);
        if (heldCount < (int) (sizeof (held) / sizeof (held[0])))
            held[heldCount++] = n;
    }
    void popNote (int n)
    {
        for (int i = 0; i < heldCount; ++i)
            if (held[i] == n)
            {
                for (int k = i; k + 1 < heldCount; ++k) held[k] = held[k + 1];
                --heldCount;
                return;
            }
    }

    float value[kOutCount] {};
    bool isConnected[kOutCount] {};
    float hostL = 0.0f, hostR = 0.0f;
    const MidiNote* midi = nullptr;
    int midiCount = 0, midiPos = 0, sampleIndex = 0;
    int held[16] {};
    int heldCount = 0;
    float velocity = 0.0f;
    Transport transport;
    double ppq = 0.0, sampleRate = 48000.0;
    long long lastStep = 0;
    bool wasPlaying = false;
    int clkLeft = 0, resetLeft = 0, pulse = 48;
};

// MAIN OUT: the summing inputs.
class RackIODevice::OutUnit : public Unit {
public:
    int numPorts() const override { return 2; }
    PortDesc port (int i) const override { const auto& j = kJacks[MainOutL + i]; return { j.id, j.type, j.dir, 0.0f, false }; }
    float* values() override { return value; }
    bool* connected() override { return isConnected; }
    bool plainVoltGates() const override { return true; }
    void processSample() override
    {
        left = value[0];
        right = isConnected[1] ? value[1] : value[0];
    }
    float value[2] {};
    bool isConnected[2] {};
    float left = 0.0f, right = 0.0f;
};

RackIODevice::RackIODevice()
{
    in_ = std::make_unique<InUnit>();
    out_ = std::make_unique<OutUnit>();
    units_.push_back (in_.get());
    units_.push_back (out_.get());
    for (int i = 0; i < kJackCount; ++i)
    {
        JackDesc j;
        j.id = kJacks[i].id;
        if (kJacks[i].dir == PortDir::In)
        {
            j.unit = out_.get();
            j.port = i - MainOutL;
        }
        else
        {
            j.unit = in_.get();
            for (int k = 0; k < kOutCount; ++k)
                if (kOutJacks[k] == i) j.port = k;
        }
        j.desc = j.unit->port (j.port);
        j.backOnly = true;
        jacks_.push_back (j);
    }
}

RackIODevice::~RackIODevice() = default;

std::vector<JackGroup> RackIODevice::jackGroups() const
{
    return { { "HOST IN", { HostInL, HostInR } },
             { "MAIN OUT (SUM)", { MainOutL, MainOutR } },
             { "MIDI > CV", { MidiNoteJack, MidiHzv, MidiGate, MidiVel } },
             { "TRANSPORT", { Clk16, Run, Reset } } };
}

jidai::jcs::Role RackIODevice::jackRole (int jack) const
{
    if (jack == MidiNoteJack)
        return jidai::jcs::Role::VOct;
    if (jack == MidiHzv)
        return jidai::jcs::Role::HzvLin;
    return Device::jackRole (jack);
}

void RackIODevice::prepare (double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    in_->sampleRate = sampleRate_;
    in_->pulse = jidai::jcs::triggerPulseSamples (sampleRate_);
}

void RackIODevice::setTransport (const Transport& t)
{
    transport_ = t;
    in_->transport = t;
    if (t.valid)
        in_->ppq = t.ppq;
}

void RackIODevice::setBlockMidi (const MidiNote* events, int count)
{
    in_->midi = events;
    in_->midiCount = events != nullptr ? count : 0;
    in_->midiPos = 0;
    in_->sampleIndex = 0;
}

void RackIODevice::setHostSample (int sampleInBlock, float l, float r)
{
    in_->sampleIndex = sampleInBlock;
    in_->hostL = l;
    in_->hostR = r;
}

float RackIODevice::mainLeftVolts() const { return out_->left * level_.load (std::memory_order_relaxed); }
float RackIODevice::mainRightVolts() const { return out_->right * level_.load (std::memory_order_relaxed); }

void RackIODevice::updateMeters (float inL, float inR, float outL, float outR, int numSamples)
{
    const float fall = (float) std::exp (-(double) numSamples / (0.3 * sampleRate_));    // 300 ms release
    const float v[4] { std::fabs (inL), std::fabs (inR), std::fabs (outL), std::fabs (outR) };
    for (size_t k = 0; k < 4; ++k)
        meters_[k].store (std::fmax (v[k], meters_[k].load (std::memory_order_relaxed) * fall), std::memory_order_relaxed);
}

float RackIODevice::jackVolts (int jack) const
{
    if (jack < 0 || jack >= kJackCount)
        return 0.0f;
    const auto& j = jacks_[(size_t) jack];
    return const_cast<Unit*> (j.unit)->values()[j.port];
}

} // namespace jidai

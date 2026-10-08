// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// RONIN in the rack: the sixteen RONIN modules, compiled from the RONIN sources, as units of the rack graph.
// The DSP is RONIN's own (Source/Modular); this file only binds its knobs and programs, the way RONIN's
// processor does (applyHostControls, applyProgramParameters, setCurrentProgram).

#include "Device.h"

#include "jidai/dsp/Halfband.h"

#include "Modular/Divider.h"
#include "Modular/Eg1.h"
#include "Modular/Eg2.h"
#include "Modular/ExtIn.h"
#include "Modular/FactoryPresets.h"
#include "Modular/Integrator.h"
#include "Modular/Inverter.h"
#include "Modular/Mg.h"
#include "Modular/Mixer.h"
#include "Modular/Noise.h"
#include "Modular/OutputModule.h"
#include "Modular/PatchGraph.h"
#include "Modular/Ring.h"
#include "Modular/SampleHold.h"
#include "Modular/Vca1.h"
#include "Modular/Vca2.h"
#include "Modular/Vcf.h"
#include "Modular/Vco.h"
#include "UI/FaceKnobs.h"
#include "UI/PatchBayLogic.h"

#include <array>
#include <string>
#include <vector>
#include <atomic>
#include <utility>

namespace jidai {

class RoninDevice : public Device {
public:
    static constexpr int kModules = 16;

    RoninDevice();
    ~RoninDevice() override;
    DeviceKind kind() const override { return DeviceKind::Ronin; }

    void prepare (double sampleRate) override;
    void beginBlock() override;
    std::vector<OrderEdge> orderEdges() const override;

    // Latency on RONIN's audio outputs (JCS R11 L_d). RONIN's HQ 2x mode (RONIN_Redesign 3.2, default OFF) runs
    // RONIN's WHOLE graph at 2fs and decimates each host output with the shared halfband (jidai::dsp::Halfband93,
    // 2x -> 1x only): kHqLatency = 23 samples, the value RONIN's getLatencySamples() reports. In the rack RONIN's
    // modules are units of the rack graph, cabled sample by sample to other devices at the base rate, so the rack
    // runs RONIN at 1x (HQ OFF, the shipped default) and reports 0. The hook stays: a RONIN that renders at 2fs
    // passes kHqLatency to setReportedLatency() (message thread, then Rack::updateLatency()) and the per-path rule
    // compensates it on MAIN OUT like any other device latency (tested).
    static constexpr int kHqLatency = jidai::dsp::Halfband93::kLatencyPerDirection;
    int latencySamples() const override { return reportedLatency_.load(); }
    void setReportedLatency (int samples) { reportedLatency_.store (samples < 0 ? 0 : samples); }
    std::vector<const Unit*> latencyUnits() const override;     // the OUTPUT stage (then HOST OUT through the order edge)

    // Back-only HOST jacks (JIDAI_RACK_Redesign 3.4: the implicit host routing made explicit):
    // HOST:IN L/R (audio in) feed EXT IN's host input; HOST:OUT L/R (audio out) carry what OUTPUT sends to the host.
    static constexpr const char* kHostInL = "HOST:IN L";
    static constexpr const char* kHostInR = "HOST:IN R";
    static constexpr const char* kHostOutL = "HOST:OUT L";
    static constexpr const char* kHostOutR = "HOST:OUT R";

    // Panel knobs, indexed like kPanelKnobs in RONIN's PanelGeometry.inc. 0..1, any thread.
    float knob (int panelIndex) const;
    void setKnob (int panelIndex, float value);
    bool effectOn() const { return effectOn_.load(); }
    // VCO TRI SHAPE (RONIN M-R2): 0 TRIANGLE (new patches, INIT), 1 PARABOLA (legacy; format-1 patches load on it).
    int triShape() const { return triShape_.load(); }
    void setTriShape (int shape) { triShape_.store (shape == 1 ? 1 : 0); }

    // RONIN state format (JCS R7) the rack writes for this device. Format 1 is the pre-redesign RONIN: knobs on the
    // old EG law, parabola triangle, legacy drive pull.
    static constexpr int kStateFormat = 2;
    // RONIN's own format-1 migration (RONIN_Redesign 6) for a RONIN loaded from a format-1 rack state, after its
    // knobs are set: M-R1 EG knobs to the real-time law, M-R2 PARABOLA, M-R5 VCF CUTOFF compensation when VCF IN
    // has exactly one cable and it is this RONIN's VCO SAW or PULSE. vcfInCables: every cable into this RONIN's
    // VCF:IN as (source, VCF:IN), the source as a bare jack id when it is this RONIN's own jack, else the full id.
    // M-R3 (legacyInvert) is the rack's M3 and is applied to the cables by the rack.
    struct Format1Report
    {
        int egKnobs = 0;
        bool attackWasStalled = false;
        bool cutoffCompensated = false, cutoffUnknown = false;
        double cutoffBefore = 0.0, cutoffAfter = 0.0;
    };
    Format1Report migrateFormat1 (const std::vector<std::pair<std::string, std::string>>& vcfInCables);
    void setEffectOn (bool on) { effectOn_.store (on); }
    void setHold (bool held) { extIn.setButtonHeld (held); }
    bool holdHeld() const { return extIn.buttonHeld(); }

    // Factory program, as RONIN's setCurrentProgram. Writes this RONIN's own cables to `cables`
    // as (output jack, input jack) pairs of this device. Call while the audio thread is not processing.
    bool loadProgram (int index, std::vector<std::pair<int, int>>& cables);
    int program() const { return program_.load(); }

    // Meter: the needle reads the jack last pressed, or OUTPUT WET until one is.
    void setMeterJack (int jack);
    float meterVolts() const;

    // Host audio for EXT IN, in host units (±1 = ±5 V), and the buffer RONIN's OUTPUT sends to the host.
    // In the rack these are driven by the HOST jacks; tests may call them directly.
    void setHostSample (float left, float right) { extIn.setHostSample (left, right); }
    float hostLeft() const { return output.hostLeft(); }
    float hostRight() const { return output.hostRight(); }

    float jackVolts (int jack) const;

private:
    class ModuleUnit;
    class HostInUnit;
    class HostOutUnit;
    std::unique_ptr<HostInUnit> hostIn_;
    std::unique_ptr<HostOutUnit> hostOut_;
    void setFace (FaceKnob face, float value);
    Module* moduleAt (int index);

    ExtIn extIn;
    OutputModule output;
    NoiseModule noise;
    Vcf vcf;
    Vca1 vca1;
    Vca2 vca2;
    Eg1 eg1;
    MgModule mg;
    Vco vco;
    Eg2 eg2;
    Ring ring;
    Divider divider;
    Inverter inverter;
    Integrator integrator;
    Mixer mixer;
    SampleHold sampleHold;
    PatchGraph programGraph;     // never processed: RONIN's program loader writes into it

    std::vector<std::unique_ptr<ModuleUnit>> moduleUnits_;
    std::array<FaceKnob, kPanelKnobCount> face_ {};
    std::array<std::atomic<float>, kPanelKnobCount> knobs_ {};
    std::array<float, kPanelKnobCount> applied_ {};
    std::atomic<bool> effectOn_ { true };
    std::atomic<int> triShape_ { 0 };
    std::atomic<int> reportedLatency_ { 0 };
    std::atomic<bool> forceApply_ { true };
    std::atomic<int> program_ { kDefaultFactoryPreset };
    std::atomic<int> meterModule_ { -1 }, meterPort_ { -1 };
    double sampleRate_ = 48000.0;
};

} // namespace jidai

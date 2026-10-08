// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// ORIGAMI in the rack: the same OrigamiCore the standalone effect runs (origami/dsp/OrigamiCore.h, which folds
// with the shared jidai/dsp/TripleShaper.h), as one unit of the rack graph. One state format for both
// (format 1, unit ORIGAMI, param ids from OrigamiParams.h), so a plugin state loads here and the reverse.
//
// Jacks (JCS R6 ids ORIGAMI#N/SECTION:LABEL; R14 roles):
//   IN:IN L, IN:IN R            AUDIO in   (IN R normalled to IN L when only IN L is patched)
//   VCA:VCA CV                  CV in      0..5 V sets the pre-fold VCA when VCA SOURCE = CV
//   VC:VC 1, VC:VC 2, VC:VC 3   CV in      audio rate OK (unsmoothed); a patched jack wins over the internal source
//   OUT:OUT L, OUT:OUT R        AUDIO out
//   HOST:IN L, HOST:IN R        AUDIO in, back only: normalled into IN L/R while those are unpatched
//   HOST:OUT L, HOST:OUT R      AUDIO out, back only: the same signal as OUT L/R (JIDAI_RACK_Redesign.md 3.4)

#include "Device.h"

#include "origami/dsp/OrigamiCore.h"

#include <array>
#include <atomic>
#include <memory>

namespace jidai {

class OrigamiDevice : public Device {
public:
    enum Jack { InL, InR, VcaCv, Vc1, Vc2, Vc3, OutL, OutR, HostInL, HostInR, HostOutL, HostOutR, kJackCount };
    static constexpr int kFrontJacks = 8;

    OrigamiDevice();
    ~OrigamiDevice() override;
    DeviceKind kind() const override { return DeviceKind::Origami; }

    void prepare (double sampleRate) override;
    void beginBlock() override;
    void syncParams();   // audio thread: copy changed params into the core
    int latencySamples() const override;
    std::vector<JackGroup> jackGroups() const override;

    // Parameters in natural units (OrigamiParams.h), any thread; applied at the next block.
    double param (int p) const;
    void setParam (int p, double value);
    void setParam (const std::string& id, double value);

    // Meters and the R15 OVER LED, any thread.
    float inPeak() const { return core_.inPeak(); }
    float outPeak() const { return core_.outPeak(); }
    bool overLit() const { return core_.overLit(); }
    float follower() const { return core_.follower(); }
    double stageCurve (int stage, double x) const;      // from the UI-side parameter values

    float jackVolts (int jack) const;
    const origami::OrigamiCore& core() const { return core_; }

private:
    class CoreUnit;
    origami::OrigamiCore core_;
    std::unique_ptr<CoreUnit> unit_;
    std::array<std::atomic<double>, origami::kParamCount> params_ {};
    std::atomic<bool> dirty_ { true };
    std::atomic<int> latency_ { 0 };
    double sampleRate_ = 48000.0;
};

} // namespace jidai

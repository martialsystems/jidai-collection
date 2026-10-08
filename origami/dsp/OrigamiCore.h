// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
#pragma once
// ORIGAMI DSP core: the stereo triple wave shaper effect (ORIGAMI_Proposal.md 2-4). Framework-free, header-only.
// The standalone plugin and the rack device both run this class; the folder itself is jidai/dsp/TripleShaper.h,
// the same module SHOGUN's WAVE uses.
//
// Signal flow per channel (L and R share controls; the follower and LEVEL COMP detectors are stereo-linked):
//   IN -> GAIN -> pre-fold VCA -> EMPH pre -> STAGE 1 -> 2 -> 3 -> EMPH post -> DC block 8 Hz -> LEVEL COMP
//      -> MIX (dry aligned) -> LEVEL -> OUT
// Units: volts in and out (+-5 V Jidai audio). The standalone converts host floats with x5 / /5 (exact in double).
// True bypass: WAVE 0 with every trim, SYM and EMPH at 0 (and no VC depth on a live source) skips the stages, EMPH,
// the DC block and LEVEL COMP; with GAIN 0 dB, VCA DEPTH 0 and LEVEL 0 dB the output is bit-identical to the input
// (delayed by the reported latency when 2x is on).
// Latency: 0 at 1x; 46 samples at 2x (93-tap halfband up 23 + down 23; the dry path is delayed to match).

#include "OrigamiParams.h"
#include "jidai/dsp/Halfband.h"
#include "jidai/dsp/TripleShaper.h"
#include "jidai/jcs/Volts.h"

#include <atomic>
#include <cmath>

namespace origami {

inline constexpr int kLatency2x = 2 * jidai::dsp::Halfband93::kLatencyPerDirection;   // 46

// First-order high shelf (+E dB at 1 kHz) and its exact inverse, bilinear with prewarp k = tan(pi*fc/fs):
//   H(z) = ((G+k) + (k-G) z^-1) / ((1+k) + (k-1) z^-1);  H^-1 swaps numerator and denominator.
class Shelf
{
public:
    void setup (double k, double G, bool inverse) noexcept
    {
        double n0 = G + k, n1 = k - G, d0 = 1.0 + k, d1 = k - 1.0;
        if (inverse) { double t0 = n0, t1 = n1; n0 = d0; n1 = d1; d0 = t0; d1 = t1; }
        b0_ = n0 / d0; b1_ = n1 / d0; a1_ = d1 / d0;
    }
    void reset() noexcept { x1_ = y1_ = 0.0; }
    double process (double x) noexcept
    {
        const double y = b0_ * x + b1_ * x1_ - a1_ * y1_;
        x1_ = x;
        y1_ = std::fabs (y) < 1e-30 ? 0.0 : y;
        return y;
    }

private:
    double b0_ = 1, b1_ = 0, a1_ = 0, x1_ = 0, y1_ = 0;
};

// One-pole control smoother with an exact snap, so a settled control equals its target bit for bit.
struct Smoothed
{
    double value = 0.0, target = 0.0, coef = 1.0;
    void setTime (double fs, double seconds) noexcept { coef = 1.0 - std::exp (-1.0 / (seconds * fs)); }
    void jump (double v) noexcept { value = target = v; }
    double next() noexcept
    {
        if (! jidai::dsp::same (value, target))
        {
            value += (target - value) * coef;
            if (std::fabs (target - value) < 1e-9) value = target;
        }
        return value;
    }
};

class OrigamiCore
{
public:
    struct Inputs
    {
        double inL = 0.0, inR = 0.0;       // volts
        double vcaCv = 0.0;                // volts, 0..5 nominal
        bool vcaPatched = false;
        double vc[3] { 0.0, 0.0, 0.0 };    // volts, audio rate OK
        bool vcPatched[3] { false, false, false };
        double sidechain = 0.0;            // volts (standalone sidechain bus, mono)
        bool sidechainLive = false;
    };

    OrigamiCore()
    {
        for (int p = 0; p < kParamCount; ++p)
            params_[p] = paramInfo (p).def;
        prepare (48000.0);
    }

    // Not real-time safe only in the sense that it resets state; no allocation anywhere in this class.
    void prepare (double sampleRate) noexcept
    {
        fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (auto* s : { &gain_, &vcaDepth_, &mix_, &level_, &wave_, &emph_, &symG_ })
            s->setTime (fs_, 0.005);
        for (int i = 0; i < 3; ++i) { trim_[i].setTime (fs_, 0.005); sym_[i].setTime (fs_, 0.005); }
        led_.prepare (fs_);
        quality_ = params_[kQuality] >= 0.5 ? 1 : 0;
        resetState();
        syncSmoothers (true);
    }

    void setParam (int p, double v) noexcept
    {
        if (p < 0 || p >= kParamCount)
            return;
        params_[p] = clampParam (p, v);
        if (p == kQuality)
        {
            const int q = params_[kQuality] >= 0.5 ? 1 : 0;
            if (q != quality_)
            {
                quality_ = q;
                resetState();     // the latency changes: the owner reports it to the host
            }
        }
    }
    double param (int p) const noexcept { return p >= 0 && p < kParamCount ? params_[p] : 0.0; }
    int latencySamples() const noexcept { return quality_ ? kLatency2x : 0; }
    bool oversampled() const noexcept { return quality_ != 0; }

    void process (const Inputs& in, double& outL, double& outR) noexcept
    {
        syncSmoothers (false);
        const double gain = gain_.next(), depth = vcaDepth_.next(), mix = mix_.next(), level = level_.next();
        jidai::dsp::ShaperControls ctl;
        ctl.macro = wave_.next();
        const double symG = symG_.next();
        for (int i = 0; i < 3; ++i)
        {
            ctl.trim[i] = trim_[i].next();
            ctl.sym[i] = sym_[i].next() + symG;
            ctl.vcToAmt[i] = params_[kVc1Amt + i];
            ctl.vcToSym[i] = params_[kVc1Sym + i];
        }
        const double emphDb = emph_.next();

        // GAIN and the stereo-linked follower (peak, attack/release, normalised to 5 V).
        const double xL = in.inL * gain, xR = in.inR * gain;
        const double peak = std::fmax (std::fabs (xL), std::fabs (xR)) / 5.0;
        follow_ += (peak - follow_) * (peak > follow_ ? atkCoef() : relCoef());
        if (follow_ < 1e-30) follow_ = 0.0;

        // Pre-fold VCA.
        double E = 0.0;
        if (params_[kVcaSource] < 0.5)
            E = follow_;
        else if (in.vcaPatched)
            E = jidai::dsp::clamp01 (in.vcaCv / 5.0 * params_[kCvScale]);
        if (params_[kVcaLaw] >= 0.5)
            E *= E;
        const double vca = 1.0 - depth + depth * E;
        const double xvL = xL * vca, xvR = xR * vca;

        // VC sources, in shaper units (volts/5). A patched jack wins over the internal source.
        double vcL[3], vcR[3];
        bool vcLive = false;
        for (int i = 0; i < 3; ++i)
        {
            vcL[i] = vcR[i] = 0.0;
            const int src = (int) params_[kVc1Src + i];
            if (in.vcPatched[i]) { vcL[i] = vcR[i] = in.vc[i] / 5.0; vcLive = true; }
            else if (src == 1) { vcL[i] = xvL / 5.0; vcR[i] = xvR / 5.0; vcLive = true; }
            else if (src == 2) { vcL[i] = vcR[i] = follow_; vcLive = true; }
            else if (src == 3 && in.sidechainLive) { vcL[i] = vcR[i] = in.sidechain / 5.0; vcLive = true; }
        }
        const bool active = ! ctl.isBypass (vcLive) || ! jidai::dsp::same (emphDb, 0.0);
        if (active && ! jidai::dsp::same (emphDb, emphApplied_))
            setEmphasis (emphDb);

        double wetL, wetR, refL = xvL, refR = xvR;
        bool activeOut = active;
        if (! quality_)
        {
            wetL = active ? shape (0, xvL, ctl, vcL, ! jidai::dsp::same (emphDb, 0.0)) : passThrough (0, xvL);
            wetR = active ? shape (1, xvR, ctl, vcR, ! jidai::dsp::same (emphDb, 0.0)) : passThrough (1, xvR);
        }
        else
        {
            // 2x: everything between the halfbands runs at 2fs; the inactive flag and the exact signal are
            // delayed by the same 46 samples, so a bypassed stretch is still bit-exact.
            double w[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                const double x = ch == 0 ? xvL : xvR;
                const double* vc = ch == 0 ? vcL : vcR;
                double x0, x1, v0[3], v1[3];
                up_[ch].process (x, x0, x1);
                for (int i = 0; i < 3; ++i) vcUp_[ch][i].process (vc[i], v0[i], v1[i]);
                const double y0 = active ? shape (ch, x0, ctl, v0, ! jidai::dsp::same (emphDb, 0.0)) : passThrough (ch, x0);
                const double y1 = active ? shape (ch, x1, ctl, v1, ! jidai::dsp::same (emphDb, 0.0)) : passThrough (ch, x1);
                w[ch] = down_[ch].process (y0, y1);
            }
            exactL_[pos_] = xvL; exactR_[pos_] = xvR; activeHist_[pos_] = active;
            const int back = (pos_ + kRing - kLatency2x) % kRing;
            refL = exactL_[back]; refR = exactR_[back]; activeOut = activeHist_[back];
            wetL = activeOut ? w[0] : refL;
            wetR = activeOut ? w[1] : refR;
        }

        // DC block and LEVEL COMP (stereo-linked), at the base rate after the folder.
        if (activeOut)
        {
            wetL = dc_[0].process (wetL);
            wetR = dc_[1].process (wetR);
            if (params_[kLevelComp] >= 0.5)
            {
                const double g = lc_.gainFor (0.5 * (refL * refL + refR * refR), 0.5 * (wetL * wetL + wetR * wetR));
                wetL *= g;
                wetR *= g;
            }
        }
        else
        {
            dc_[0].process (refL);
            dc_[1].process (refR);
            lc_.track (std::sqrt (0.5 * (refL * refL + refR * refR)));
        }

        // Dry path aligned to the wet path.
        double dryL = in.inL, dryR = in.inR;
        if (quality_)
        {
            dryRingL_[pos_] = in.inL; dryRingR_[pos_] = in.inR;
            const int back = (pos_ + kRing - kLatency2x) % kRing;
            dryL = dryRingL_[back]; dryR = dryRingR_[back];
            pos_ = (pos_ + 1) % kRing;
        }
        if (params_[kBypass] >= 0.5)
        {
            outL = dryL;
            outR = dryR;
        }
        else
        {
            outL = (dryL + mix * (wetL - dryL)) * level;
            outR = (dryR + mix * (wetR - dryR)) * level;
        }
        const double peaks[4] { std::fabs (in.inL), std::fabs (in.inR), std::fabs (outL), std::fabs (outR) };
        for (int k = 0; k < 4; ++k)
            peak_[k].store ((float) std::fmax (peaks[k], (double) peak_[k].load (std::memory_order_relaxed) * meterFall_), std::memory_order_relaxed);
        over_.store (led_.process ((float) std::fmax (peaks[2], peaks[3])), std::memory_order_relaxed);
        followMeter_.store ((float) follow_, std::memory_order_relaxed);
    }

    // Meters for the UI (any thread): peak volts with a 300 ms fall, the R15 OVER LED and the follower (0..1).
    float inPeak (int ch) const noexcept { return peak_[ch & 1].load (std::memory_order_relaxed); }
    float outPeak (int ch) const noexcept { return peak_[2 + (ch & 1)].load (std::memory_order_relaxed); }
    float inPeak() const noexcept { return std::fmax (inPeak (0), inPeak (1)); }
    float outPeak() const noexcept { return std::fmax (outPeak (0), outPeak (1)); }
    bool overLit() const noexcept { return over_.load (std::memory_order_relaxed); }
    float follower() const noexcept { return followMeter_.load (std::memory_order_relaxed); }

    // Transfer curve of one stage for a parameter set (natural units, indexed by Param), for the STAGES page:
    // static, no ADAA, x in shaper units. UI threads call this with their own copy of the parameters.
    static double stageCurve (const double* params, int stage, double x) noexcept
    {
        jidai::dsp::ShaperControls c;
        c.macro = params[kWave];
        for (int i = 0; i < 3; ++i) { c.trim[i] = params[kStage1 + i]; c.sym[i] = params[kSym1 + i] + params[kSym]; }
        double a[3], b[3];
        jidai::dsp::TripleShaper::stageParams (c, 0.0, a, b);
        return jidai::dsp::ShaperStage::f (x, a[stage], b[stage], jidai::dsp::kShaperK[stage]);
    }

private:
    static constexpr int kRing = 64;

    double processRate() const noexcept { return quality_ ? 2.0 * fs_ : fs_; }
    double atkCoef() const noexcept { return 1.0 - std::exp (-1.0 / (params_[kAttack] * 0.001 * fs_)); }
    double relCoef() const noexcept { return 1.0 - std::exp (-1.0 / (params_[kRelease] * 0.001 * fs_)); }

    void resetState() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            shaper_[ch].reset();
            pre_[ch].reset(); post_[ch].reset();
            up_[ch].reset(); down_[ch].reset();
            for (auto& u : vcUp_[ch]) u.reset();
            dc_[ch].prepare (fs_, 8.0);
        }
        lc_.prepare (fs_);
        follow_ = 0.0;
        for (int i = 0; i < kRing; ++i) { exactL_[i] = exactR_[i] = dryRingL_[i] = dryRingR_[i] = 0.0; activeHist_[i] = false; }
        pos_ = 0;
        emphK_ = std::tan (jidai::dsp::kPi * 1000.0 / processRate());
        emphApplied_ = -1.0;
        setEmphasis (params_[kEmph]);
        meterFall_ = std::exp (-1.0 / (0.3 * fs_));
    }

    void setEmphasis (double db) noexcept
    {
        const double G = std::pow (10.0, db / 20.0);
        for (int ch = 0; ch < 2; ++ch)
        {
            pre_[ch].setup (emphK_, G, false);
            post_[ch].setup (emphK_, G, true);
        }
        emphApplied_ = db;
    }

    void syncSmoothers (bool jump) noexcept
    {
        auto set = [jump] (Smoothed& s, double v) { if (jump) s.jump (v); else s.target = v; };
        set (gain_, std::pow (10.0, params_[kGain] / 20.0));
        set (vcaDepth_, params_[kVcaDepth]);
        set (mix_, params_[kMix]);
        set (level_, std::pow (10.0, params_[kLevel] / 20.0));
        set (wave_, params_[kWave]);
        set (emph_, params_[kEmph]);
        set (symG_, params_[kSym]);
        for (int i = 0; i < 3; ++i) { set (trim_[i], params_[kStage1 + i]); set (sym_[i], params_[kSym1 + i]); }
    }

    double shape (int ch, double xVolts, const jidai::dsp::ShaperControls& ctl, const double vc[3], bool emph) noexcept
    {
        double u = jidai::dsp::toUnits (xVolts);
        if (emph) u = pre_[ch].process (u);
        u = shaper_[ch].process (u, ctl, vc);
        if (emph) u = post_[ch].process (u);
        return jidai::dsp::toVolts (u);
    }
    // Bypassed stretch: keep the shaper's ADAA history current so a later activation starts clean.
    double passThrough (int ch, double xVolts) noexcept
    {
        static const double zero[3] { 0.0, 0.0, 0.0 };
        shaper_[ch].processStages (jidai::dsp::toUnits (xVolts), zero, zero);
        return xVolts;
    }

    double params_[kParamCount] {};
    double fs_ = 48000.0;
    int quality_ = 0;
    Smoothed gain_, vcaDepth_, mix_, level_, wave_, emph_, symG_, trim_[3], sym_[3];
    jidai::dsp::TripleShaper shaper_[2];
    Shelf pre_[2], post_[2];
    double emphK_ = 0.0, emphApplied_ = -1.0;
    jidai::dsp::Upsampler2x up_[2], vcUp_[2][3];
    jidai::dsp::Downsampler2x down_[2];
    jidai::dsp::DcBlocker dc_[2];
    jidai::dsp::LevelComp lc_;
    double follow_ = 0.0;
    double exactL_[kRing] {}, exactR_[kRing] {}, dryRingL_[kRing] {}, dryRingR_[kRing] {};
    bool activeHist_[kRing] {};
    int pos_ = 0;
    jidai::jcs::OverRangeLed led_;
    double meterFall_ = 0.9999;
    std::atomic<float> peak_[4] { { 0.0f }, { 0.0f }, { 0.0f }, { 0.0f } }, followMeter_ { 0.0f };   // in L, in R, out L, out R
    std::atomic<bool> over_ { false };
};

} // namespace origami

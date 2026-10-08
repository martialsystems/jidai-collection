// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
// ORIGAMI DSP core tests (no JUCE): ORIGAMI_Proposal.md 7 (testFxBypass, testFxLatency, testFxStereoLink,
// testEmphInverse) plus level, symmetry, VC, aliasing at 1x/2x and NaN/denormal hygiene.

#include "../dsp/OrigamiCore.h"
#include "../../jidai-common/tests/TestFft.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace origami;

namespace {

int checks = 0, failures = 0;
void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok) { ++failures; std::printf ("FAIL %s\n", what.c_str()); }
}

struct Rng
{
    std::uint32_t s = 12345u;
    double uni() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (double) s / 4294967296.0; }
};

constexpr double kPi = 3.14159265358979323846;

std::unique_ptr<OrigamiCore> make (double fs = 48000.0)
{
    auto c = std::make_unique<OrigamiCore>();
    c->prepare (fs);
    return c;
}

// Runs n samples of a mono/stereo generator; returns L (and R through rOut).
std::vector<double> run (OrigamiCore& c, int n, const std::function<void (int, OrigamiCore::Inputs&)>& gen, std::vector<double>* rOut = nullptr)
{
    std::vector<double> l ((size_t) n);
    if (rOut) rOut->assign ((size_t) n, 0.0);
    for (int i = 0; i < n; ++i)
    {
        OrigamiCore::Inputs in;
        gen (i, in);
        if (i % 64 == 0)
            c.planBlock (in.vcPatched, in.sidechainLive);     // as the plugin and the rack do, once per 64-sample block
        double a, b;
        c.process (in, a, b);
        l[(size_t) i] = a;
        if (rOut) (*rOut)[(size_t) i] = b;
    }
    return l;
}

void testBypass()
{
    // Random float input (as the rack and plugin pass it), all defaults: WAVE 0 -> bit-identical.
    for (int q = 0; q < 2; ++q)
        for (double mix : { 1.0, 0.5, 0.0 })
        {
            auto c = make();
            c->setParam (kQuality, q);
            c->setParam (kMix, mix);
            c->prepare (48000.0);
            Rng r;
            std::vector<float> x (6000);
            for (auto& v : x) v = (float) ((r.uni() * 2.0 - 1.0) * 6.0);
            x[100] = 1.0e-38f; x[101] = -3.3e-39f;
            std::vector<double> R;
            auto L = run (*c, (int) x.size(), [&] (int i, OrigamiCore::Inputs& in) { in.inL = x[(size_t) i]; in.inR = -x[(size_t) i]; }, &R);
            const int lat = q ? 46 : 0;
            bool exact = true;
            for (size_t i = 0; i < x.size(); ++i)
            {
                const float want = i >= (size_t) lat ? x[i - (size_t) lat] : 0.0f;
                exact = exact && (float) L[i] == want && (float) R[i] == -want;
            }
            check (exact, std::string ("testFxBypass: WAVE 0 is bit-identical at ") + (q ? "2x (46 late)" : "1x") + ", MIX " + std::to_string (mix));
        }
    // The BYPASS switch is bit-exact too, at any WAVE.
    auto c = make();
    c->setParam (kWave, 0.8);
    c->setParam (kBypass, 1.0);
    bool exact = true;
    auto L = run (*c, 3000, [] (int i, OrigamiCore::Inputs& in) { in.inL = in.inR = std::sin (i * 0.031) * 4.0; });
    for (int i = 0; i < 3000; ++i) exact = exact && L[(size_t) i] == std::sin (i * 0.031) * 4.0;
    check (exact, "BYPASS switch: dry passes bit-exact");
}

void testLatency()
{
    auto c = make();
    check (c->latencySamples() == 0, "testFxLatency: 1x reports 0");
    c->setParam (kQuality, 1.0);
    check (c->latencySamples() == 46, "testFxLatency: 2x reports 46 (93-tap halfband up 23 + down 23)");
    check (paramInfo (kQuality).def == 0.0, "2x is OFF by default");
    // Wet impulse response peaks at the reported latency (plus the ADAA stage half-samples), and the dry path
    // lines up with it: MIX 0.5 at a tiny WAVE equals the delayed input almost exactly.
    for (int q = 0; q < 2; ++q)
    {
        auto d = make();
        d->setParam (kQuality, q);
        d->setParam (kWave, 0.02);
        d->setParam (kLevelComp, 0.0);
        d->setParam (kMix, 0.5);
        d->prepare (48000.0);
        const int lat = d->latencySamples();
        auto y = run (*d, 4000, [] (int i, OrigamiCore::Inputs& in) { in.inL = in.inR = 2.0 * std::sin (2.0 * kPi * 200.0 * i / 48000.0); });
        double err = 0.0, mis = 0.0;
        for (int i = 2000; i < 4000; ++i)
        {
            err = std::fmax (err, std::fabs (y[(size_t) i] - 2.0 * std::sin (2.0 * kPi * 200.0 * (i - lat) / 48000.0)));
            mis = std::fmax (mis, std::fabs (y[(size_t) i] - 2.0 * std::sin (2.0 * kPi * 200.0 * (i - lat - 3) / 48000.0)));
        }
        check (err < 0.05 && err < mis, std::string ("dry is aligned to wet at ") + (q ? "2x" : "1x") + ": err " + std::to_string (err) + " vs 3-sample misalignment " + std::to_string (mis));
    }
}

void testStereoLink()
{
    auto c = make();
    c->setParam (kWave, 0.7);
    c->setParam (kVcaDepth, 0.6);
    c->setParam (kSym, 0.3);
    c->setParam (kVc1Src, 1.0);
    c->setParam (kVc1Amt, 0.4);
    c->prepare (48000.0);
    std::vector<double> R;
    auto L = run (*c, 5000, [] (int i, OrigamiCore::Inputs& in) { in.inL = in.inR = 3.0 * std::sin (i * 0.02) * std::exp (-i / 3000.0); }, &R);
    bool same = true;
    for (int i = 0; i < 5000; ++i) same = same && L[(size_t) i] == R[(size_t) i];
    check (same, "testFxStereoLink: identical input gives identical L and R");
}

void testEmphInverse()
{
    for (int q = 0; q < 2; ++q)
    {
        auto c = make();
        c->setParam (kQuality, q);
        c->setParam (kEmph, 12.0);
        c->setParam (kLevelComp, 0.0);
        c->prepare (48000.0);
        Rng r;
        std::vector<double> x (8000);
        for (auto& v : x) v = (r.uni() * 2.0 - 1.0) * 4.0;
        auto y = run (*c, 8000, [&] (int i, OrigamiCore::Inputs& in) { in.inL = in.inR = x[(size_t) i]; });
        // The 8 Hz DC block still runs when EMPH is on (the folder path is active), so compare after it settles
        // against the input through an identical DC blocker.
        jidai::dsp::DcBlocker dc;
        dc.prepare (48000.0);
        const int lat = c->latencySamples();
        double worst = 0.0;
        std::vector<double> ref (8000);
        // At 2x the active path is band-limited by the halfband pair, so the reference goes through the same pair
        // (the core outputs the exact delayed signal for its first 46 samples, before any wet sample exists).
        jidai::dsp::Upsampler2x up; jidai::dsp::Downsampler2x dn;
        for (int i = 0; i < 8000; ++i)
        {
            double base = i >= lat ? x[(size_t) (i - lat)] : 0.0;
            if (q) { double u0, u1; up.process (x[(size_t) i], u0, u1); const double d = dn.process (u0, u1); base = i >= lat ? d : 0.0; }
            ref[(size_t) i] = dc.process (base);
        }
        for (int i = 200; i < 8000; ++i) worst = std::fmax (worst, std::fabs (y[(size_t) i] - ref[(size_t) i]));
        check (worst < 1e-6, std::string ("testEmphInverse at ") + (q ? "2x" : "1x") + ": EMPH 12 dB with WAVE 0 returns the input, worst " + std::to_string (worst));
    }
}

void testLevel()
{
    // LEVEL COMP on: the macro sweep keeps loudness (spec: 0.00..+0.03 dB at 110 Hz).
    double lo = 1e9, hi = -1e9, offLo = 1e9, offHi = -1e9;
    for (int k = 1; k <= 10; ++k)
        for (int comp = 0; comp < 2; ++comp)
        {
            auto c = make (96000.0);
            c->setParam (kWave, k / 10.0);
            c->setParam (kLevelComp, comp);
            c->prepare (96000.0);
            const int n = 24000;
            auto y = run (*c, n, [] (int i, OrigamiCore::Inputs& in) { in.inL = in.inR = 4.0 * std::sin (2.0 * kPi * 110.0 * i / 96000.0); });
            double sx = 0, sy = 0;
            for (int i = 14400; i < n; ++i) { const double x = 4.0 * std::sin (2.0 * kPi * 110.0 * i / 96000.0); sx += x * x; sy += y[(size_t) i] * y[(size_t) i]; }
            const double db = 10.0 * std::log10 (sy / sx);
            if (comp) { lo = std::fmin (lo, db); hi = std::fmax (hi, db); }
            else { offLo = std::fmin (offLo, db); offHi = std::fmax (offHi, db); }
        }
    check (lo > -0.5 && hi < 0.5, "LEVEL COMP ON: 110 Hz macro sweep within +-0.5 dB, got " + std::to_string (lo) + " .. " + std::to_string (hi));
    check (offHi - offLo > 1.0, "LEVEL COMP OFF: loudness moves with WAVE (" + std::to_string (offLo) + " .. " + std::to_string (offHi) + " dB)");
    // GAIN and LEVEL are plain dB gains.
    auto c = make();
    c->setParam (kLevel, -6.0);
    c->prepare (48000.0);
    auto y = run (*c, 10, [] (int, OrigamiCore::Inputs& in) { in.inL = in.inR = 2.0; });
    check (std::fabs (y[9] - 2.0 * std::pow (10.0, -6.0 / 20.0)) < 1e-12, "LEVEL -6 dB");
}

void testSilenceAndHygiene()
{
    auto c = make();
    c->setParam (kWave, 0.6);
    c->setParam (kSym, 0.7);
    c->setParam (kVc2Src, 2.0);
    c->setParam (kVc2Sym, 1.0);
    c->setParam (kEmph, 6.0);
    c->setParam (kVcaDepth, 1.0);
    c->prepare (48000.0);
    auto y = run (*c, 4000, [] (int, OrigamiCore::Inputs& in) { in.inL = in.inR = 0.0; });
    bool silent = true;
    for (double v : y) silent = silent && v == 0.0;
    check (silent, "silence stays silent with SYM, VC -> SYM, EMPH and the VCA on (static bias removed)");
    // Loud, then 10 s of silence: no NaN, no denormal ever reaches the output.
    for (int q = 0; q < 2; ++q)
    {
        auto d = make();
        d->setParam (kQuality, q);
        d->setParam (kWave, 1.0);
        d->setParam (kSym, -0.4);
        d->setParam (kEmph, 12.0);
        d->setParam (kVc1Src, 1.0);
        d->setParam (kVc1Amt, 1.0);
        d->prepare (48000.0);
        bool finite = true, noDenormal = true;
        auto z = run (*d, 48000 * 10, [] (int i, OrigamiCore::Inputs& in) { in.inL = in.inR = i < 4800 ? 1.0e6 * std::sin (i * 0.3) : (i < 9600 ? 5.0 * std::sin (i * 0.01) : 0.0); });
        for (double v : z)
        {
            finite = finite && std::isfinite (v);
            noDenormal = noDenormal && std::fpclassify ((float) v) != FP_SUBNORMAL && std::fpclassify (v) != FP_SUBNORMAL;
        }
        check (finite, std::string ("no NaN or Inf with extreme input at ") + (q ? "2x" : "1x"));
        check (noDenormal, std::string ("no denormals in 10 s of decaying silence at ") + (q ? "2x" : "1x"));
    }
}

void testSymmetry()
{
    // SYM 0: odd folds, so an odd-symmetric input has only odd harmonics. SYM +0.6: even harmonics appear.
    const int N = 16384, B = 64;
    auto harm = [&] (double sym) {
        auto c = make();
        c->setParam (kWave, 0.5); c->setParam (kSym, sym); c->setParam (kLevelComp, 0.0);
        c->prepare (48000.0);
        auto y = run (*c, 2 * N, [&] (int i, OrigamiCore::Inputs& in) { in.inL = in.inR = 4.0 * std::sin (2.0 * kPi * B * i / N); });
        std::vector<double> t (y.begin() + N, y.end());
        auto p = jidai::test::powerSpectrum (t);
        double even = 0, odd = 0;
        for (int h = 1; h * B < N / 2 && h <= 20; ++h) (h % 2 ? odd : even) += p[(size_t) (h * B)];
        return 10.0 * std::log10 (even / odd + 1e-30);
    };
    const double e0 = harm (0.0), e6 = harm (0.6);
    check (e0 < -100.0, "SYM 0: even harmonics absent (" + std::to_string (e0) + " dB re odd)");
    check (e6 > -20.0, "SYM 0.6: even harmonics present (" + std::to_string (e6) + " dB re odd)");
}

void testAudioRateVc()
{
    // VC 1 -> AMT with a 1353.5 Hz sine on a 257.8 Hz body (bins 4*44 and 21*44 of 32768): sidebands at
    // multiples of 44 bins prove the VC is read per sample (a smoothed control would leave only the harmonics).
    const int N = 32768, q = 44;
    auto c = make();
    c->setParam (kWave, 0.5); c->setParam (kVc1Amt, 0.5); c->setParam (kLevelComp, 0.0);
    c->prepare (48000.0);
    auto y = run (*c, 2 * N, [&] (int i, OrigamiCore::Inputs& in) {
        in.inL = in.inR = 4.0 * std::sin (2.0 * kPi * 4 * q * i / N);
        in.vc[0] = 5.0 * std::sin (2.0 * kPi * 21 * q * i / N);
        in.vcPatched[0] = true;
    });
    std::vector<double> t (y.begin() + N, y.end());
    auto p = jidai::test::powerSpectrum (t);
    double sb = 0, h = 0;
    for (size_t k = 1; k < p.size(); ++k)
    {
        if (k % q) continue;
        if (k % (4 * q) == 0) h += p[k]; else sb += p[k];
    }
    const double db = 10.0 * std::log10 (sb / h);
    check (db > -10.0, "audio-rate VC: sideband energy " + std::to_string (db) + " dB re harmonics (spec +1.0 unsmoothed vs -35.8 smoothed)");
    check (c->latencySamples() == 0, "VC jacks add no latency at 1x");
}

void testAliasing()
{
    // ORIGAMI inherits SHOGUN's static table: 1031 Hz at +-5 V, WAVE 0.5 -> 1x ADAA about -45 dB, 2x about -122 dB.
    const int N = 32768, B0 = 704;
    double res[2];
    for (int qq = 0; qq < 2; ++qq)
    {
        auto c = make();
        c->setParam (kQuality, qq); c->setParam (kWave, 0.5); c->setParam (kLevelComp, 0.0);
        c->prepare (48000.0);
        auto y = run (*c, 3 * N, [&] (int i, OrigamiCore::Inputs& in) { in.inL = in.inR = 5.0 * std::sin (2.0 * kPi * B0 * i / N); });
        std::vector<double> t (y.begin() + 2 * N, y.end());
        res[qq] = jidai::test::aliasDb (t, 48000.0, B0);
    }
    std::printf ("  ORIGAMI alias 1031 Hz WAVE 0.5: 1x %.1f dB, 2x %.1f dB\n", res[0], res[1]);
    check (res[0] < -40.0 && res[0] > -50.0, "1x ADAA alias about -45 dB (spec -45.3)");
    check (res[1] < -100.0, "2x alias below -100 dB (spec -121.9 with an ideal decimator)");
}

void testParams()
{
    check (paramIndex ("wave") == kWave && paramIndex ("quality") == kQuality && paramIndex ("nope") == -1, "param ids");
    check (paramInfo (kWave).def == 0.0 && paramInfo (kLevelComp).def == 1.0 && paramInfo (kQuality).def == 0.0, "INIT: WAVE 0, LEVEL COMP ON, 2x OFF");
    bool unique = true;
    for (int i = 0; i < kParamCount; ++i)
        for (int j = i + 1; j < kParamCount; ++j)
            unique = unique && std::string (paramInfo (i).id) != paramInfo (j).id;
    check (unique, "param ids are unique");
    bool round = true;
    for (int i = 0; i < kParamCount; ++i)
        for (double n : { 0.0, 0.25, 0.5, 1.0 })
            round = round && std::fabs (toNormal (i, fromNormal (i, n)) - n) < (paramInfo (i).choices ? 0.51 : 1e-9);
    check (round, "normalised round trip");
    check (clampParam (kWave, 3.0) == 1.0 && clampParam (kVc1Src, 2.4) == 2.0, "clamping and choices");
}

}

// jidai-common 1.1.1: a stage is a wire only for a whole block, decided by planBlock.
void testPlanBlock()
{
    auto c = make();
    c->setParam (kWave, 0.5);                              // stage 3 amount 2m - 1 = 0, SYM 0
    const bool none[3] { false, false, false };
    c->planBlock (none, false);
    check (! c->stageIsWire (2), "a WAVE change still ramping (smoothing) plans no wire");
    run (*c, 48000, [] (int, OrigamiCore::Inputs&) {});     // let the 5 ms smoothers settle (exact snap)
    c->planBlock (none, false);
    check (c->stageIsWire (2) && ! c->stageIsWire (0) && ! c->stageIsWire (1), "settled WAVE 0.5: stage 3 is a wire, stages 1-2 are not");
    c->setParam (kVc3Amt, 0.5);
    const bool vc3[3] { false, false, true };
    c->planBlock (vc3, false);
    check (! c->stageIsWire (2), "a patched VC 3 with depth keeps stage 3's ADAA for the block");
    c->planBlock (none, false);
    check (c->stageIsWire (2), "VC 3 depth without a live source: stage 3 is a wire again");
    // Under VC modulation that crosses 0, stage 3 keeps its ADAA all the way: planned output = never-skipping output.
    auto planned = make(), unplanned = make();
    for (auto* m : { planned.get(), unplanned.get() }) { m->setParam (kWave, 0.5); m->setParam (kVc3Amt, 0.5); }
    double worst = 0.0;
    for (int i = 0; i < 9600; ++i)
    {
        OrigamiCore::Inputs in;
        in.inL = in.inR = 2.0 * std::sin (2.0 * kPi * 110.0 * i / 48000.0);
        in.vc[2] = 5.0 * std::sin (2.0 * kPi * 3.0 * i / 48000.0);
        in.vcPatched[2] = true;
        if (i % 64 == 0) planned->planBlock (in.vcPatched, false);
        double a, b, a2, b2;
        planned->process (in, a, b);
        unplanned->process (in, a2, b2);
        worst = std::fmax (worst, std::fabs (a - a2));
    }
    check (worst == 0.0, "VC crossing 0 on stage 3: planned output identical to the never-skipping output (diff " + std::to_string (worst) + ")");
}

int main()
{
    testParams();
    testBypass();
    testLatency();
    testStereoLink();
    testEmphInverse();
    testLevel();
    testSilenceAndHygiene();
    testSymmetry();
    testAudioRateVc();
    testAliasing();
    testPlanBlock();
    std::printf ("%d checks, %d failed\n%s\n", checks, failures, failures == 0 ? "ORIGAMI TESTS PASS" : "ORIGAMI TESTS FAIL");
    return failures == 0 ? 0 : 1;
}

// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
// Tests for the shared DSP blocks: jidai/dsp/TripleShaper.h (SHOGUN WAVE 4.6 testWave* set) and jidai/dsp/Halfband.h.

#include "TestFft.h"
#include "jidai/dsp/Halfband.h"
#include "jidai/dsp/TripleShaper.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace jidai::dsp;

namespace {

int checks = 0, failures = 0;
void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok) { ++failures; std::printf ("FAIL %s\n", what.c_str()); }
}

struct Rng
{
    std::uint32_t s = 0xA341316Cu;
    double uni() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (double) s / 4294967296.0; }
};

double specStage (double x, double a, double b, double K)   // the spec formula, written out independently
{
    const double g = 1.0 + K * a;
    return (1.0 - a) * x + a * (std::sin (kPi / 2.0 * g * (x + b)) - std::sin (kPi / 2.0 * g * b));
}

void testStageLaw()
{
    Rng r;
    double worst = 0.0, worstDeriv = 0.0;
    for (int i = 0; i < 20000; ++i)
    {
        const double x = (r.uni() * 2.0 - 1.0) * 1.5, a = r.uni(), b = r.uni() * 2.0 - 1.0;
        const int k = i % 3;
        worst = std::fmax (worst, std::fabs (ShaperStage::f (x, a, b, kShaperK[k]) - specStage (x, a, b, kShaperK[k])));
        const double h = 1e-5;
        const double dF = (ShaperStage::F (x + h, a, b, kShaperK[k]) - ShaperStage::F (x - h, a, b, kShaperK[k])) / (2.0 * h);
        worstDeriv = std::fmax (worstDeriv, std::fabs (dF - ShaperStage::f (x, a, b, kShaperK[k])));
    }
    check (worst == 0.0, "stage law matches y=(1-a)x+a(sin(pi/2 g(x+b)) - sin(pi/2 g b)), g=1+Ka, exactly");
    check (worstDeriv < 1e-6, "F is the antiderivative of f (dF/dx = f), worst " + std::to_string (worstDeriv));
    check (kShaperK[0] == 4.0 && kShaperK[1] == 2.0 && kShaperK[2] == 2.0, "K = (4, 2, 2)");
    bool zeroAtZero = true, wire = true;
    for (int i = 0; i < 2000; ++i)
    {
        const double a = r.uni(), b = r.uni() * 2.0 - 1.0, x = r.uni() * 2.0 - 1.0;
        zeroAtZero = zeroAtZero && ShaperStage::f (0.0, a, b, 4.0) == 0.0;
        wire = wire && ShaperStage::f (x, 0.0, 0.0, 4.0) == x;
    }
    check (zeroAtZero, "s(0) = 0 for every a, b: silence stays silent, no CV bleed");
    check (wire, "a = 0, b = 0: the stage is a wire");
    // Symmetry: b = 0 is odd; b = +1 is even where g is an odd integer (g = 1, 3, 5), the rectifying core.
    bool odd = true, even = true;
    for (int i = 0; i < 2000; ++i)
    {
        const double x = r.uni() * 2.0 - 1.0, a = r.uni();
        odd = odd && std::fabs (ShaperStage::f (-x, a, 0.0, 4.0) + ShaperStage::f (x, a, 0.0, 4.0)) < 1e-12;
        const double core3 = ShaperStage::f (x, 1.0, 1.0, 2.0) - (1.0 - 1.0) * x;   // g = 3
        const double core5 = ShaperStage::f (x, 1.0, 1.0, 4.0);                     // g = 5
        even = even && std::fabs (core3 - ShaperStage::f (-x, 1.0, 1.0, 2.0)) < 1e-12 && std::fabs (core5 - ShaperStage::f (-x, 1.0, 1.0, 4.0)) < 1e-12;
    }
    check (odd, "SYM 0: odd folds, f(-x) = -f(x)");
    check (even, "SYM +1 at full amount: even (rectifying) core");
}

void testAdaa()
{
    AdaaStage s;
    double y = 0.0;
    for (int i = 0; i < 10; ++i) y = s.process (0.3, 0.7, 0.2, 4.0);
    check (std::fabs (y - ShaperStage::f (0.3, 0.7, 0.2, 4.0)) < 1e-12, "ADAA on a constant equals f(x)");
    s.reset();
    s.process (0.0, 0.5, 0.0, 4.0);
    y = s.process (0.4, 0.5, 0.0, 4.0);
    const double expect = (ShaperStage::F (0.4, 0.5, 0.0, 4.0) - ShaperStage::F (0.0, 0.5, 0.0, 4.0)) / 0.4;
    check (std::fabs (y - expect) < 1e-15, "ADAA difference quotient");
    // Audio-rate parameters: the cached F(x[n-1]) must only be reused when (a, b) did not move.
    AdaaStage c, ref;
    Rng r;
    double worst = 0.0, xp = 0.0;
    for (int i = 0; i < 5000; ++i)
    {
        const double x = std::sin (i * 0.05) * 0.9, a = (i / 7) % 2 ? 0.6 : 0.3 + 0.2 * r.uni(), b = 0.1;
        const double yc = c.process (x, a, b, 2.0);
        double yr;
        if (std::fabs (x - xp) < 1e-6) yr = ShaperStage::f (0.5 * (x + xp), a, b, 2.0);
        else yr = (ShaperStage::F (x, a, b, 2.0) - ShaperStage::F (xp, a, b, 2.0)) / (x - xp);
        xp = x;
        worst = std::fmax (worst, std::fabs (yc - yr));
    }
    (void) ref;
    check (worst < 1e-12, "ADAA uses sample-n parameters for both terms (cache is exact), worst " + std::to_string (worst));
    AdaaStage w;
    w.process (0.5, 0.0, 0.0, 4.0);
    check (w.process (0.25, 0.0, 0.0, 4.0) == 0.25, "a skipped stage has no ADAA delay");
}

void testMacro()
{
    double c[3];
    macroAmounts (0.0, c);   check (c[0] == 0 && c[1] == 0 && c[2] == 0, "WAVE 0: all stages off");
    macroAmounts (0.25, c);  check (c[0] == 0.5 && c[1] == 0.0 && c[2] == 0.0, "WAVE 0.25: stage 1 half");
    macroAmounts (0.5, c);   check (c[0] == 1.0 && c[1] == 0.5 && c[2] == 0.0, "noon: stage 1 full, stage 2 half, stage 3 off");
    macroAmounts (0.75, c);  check (c[0] == 1.0 && c[1] == 1.0 && c[2] == 0.5, "WAVE 0.75");
    macroAmounts (1.0, c);   check (c[0] == 1.0 && c[1] == 1.0 && c[2] == 1.0, "WAVE 1: all full");
    ShaperControls k;
    check (k.isBypass (true), "defaults are a true bypass");
    k.vcToAmt[1] = 0.3;
    check (k.isBypass (false) && ! k.isBypass (true), "a VC depth only counts when a VC source is live");
    k.vcToAmt[1] = 0.0; k.sym[2] = 0.01;
    check (! k.isBypass (false), "any SYM leaves bypass");
    // Exact migration from the single-stage WAVE (SHOGUN 4.6): m = w/2, WAVE 2 = -(w - 0.5) for w > 0.5.
    double worst = 0.0;
    for (int v = 0; v <= 127; ++v)
    {
        const double w = v / 127.0;
        ShaperControls mc;
        mc.macro = w / 2.0;
        double cc[3];
        macroAmounts (mc.macro, cc);
        mc.trim[1] = -cc[1];
        TripleShaper ts;
        ts.setAdaa (false);
        for (int i = 0; i <= 400; ++i)
        {
            const double x = -1.2 + 2.4 * i / 400.0;
            const double old = (1.0 - w) * x + w * std::sin (kPi / 2.0 * (1.0 + 4.0 * w) * x);
            worst = std::fmax (worst, std::fabs (ts.process (x, mc, 0.0) - old));
        }
    }
    check (worst < 1e-12, "single-stage WAVE migration is exact, worst " + std::to_string (worst));
}

void testLevelComp()
{
    const double fs = 96000.0;
    const int n = (int) (0.25 * fs), k0 = (int) (0.15 * fs);
    double lo = 1e9, hi = -1e9;
    Rng r;
    for (int t = 0; t < 200; ++t)
    {
        ShaperControls c;
        for (int i = 0; i < 3; ++i) { c.trim[i] = r.uni(); c.sym[i] = r.uni() < 0.5 ? (r.uni() * 2.0 - 1.0) : 0.0; }
        const double amps[] = { 1.0, 0.6, 0.3 };
        const double amp = amps[t % 3], f = 60.0 + 340.0 * r.uni();
        TripleShaper ts; DcBlocker dc; LevelComp lc;
        dc.prepare (fs); lc.prepare (fs);
        double sx = 0.0, sy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double x = amp * std::sin (2.0 * kPi * f * i / fs);
            const double y = lc.process (x, dc.process (ts.process (x, c, 0.0)));
            if (i >= k0) { sx += x * x; sy += y * y; }
        }
        const double db = 10.0 * std::log10 (sy / sx);
        lo = std::fmin (lo, db); hi = std::fmax (hi, db);
    }
    check (lo > -2.5 && hi < 0.5, "LEVEL COMP: 200 random settings within -2.5..+0.5 dB (spec -1.9..+0.15), got " + std::to_string (lo) + " .. " + std::to_string (hi));
    LevelComp lc;
    lc.prepare (48000.0);
    for (int i = 0; i < 48000; ++i) lc.process (1.0, 1e-6);
    check (std::fabs (lc.gain() - LevelComp::kMax) < 1e-6, "LEVEL COMP clamps at +12 dB");
}

void testHalfband()
{
    const double* a = Halfband93::odd();
    std::vector<double> h (Halfband93::kTaps, 0.0);
    h[46] = 0.5;
    for (int j = 0; j < 23; ++j) h[(size_t) (46 + 2 * j + 1)] = h[(size_t) (46 - 2 * j - 1)] = a[j];
    double worstStop = 0.0, pmax = 0.0, pmin = 1e9;
    for (int i = 0; i <= 4000; ++i)
    {
        const double f = 0.5 * i / 4000.0;
        double re = 0.0, im = 0.0;
        for (int k = 0; k < 93; ++k) { re += h[(size_t) k] * std::cos (2 * kPi * f * k); im -= h[(size_t) k] * std::sin (2 * kPi * f * k); }
        const double mag = std::sqrt (re * re + im * im);
        if (f >= 0.2875) worstStop = std::fmax (worstStop, mag);
        if (f <= 0.2125) { pmax = std::fmax (pmax, mag); pmin = std::fmin (pmin, mag); }
    }
    check (20.0 * std::log10 (worstStop) < -110.0, "93-tap halfband stopband >= 110 dB, got " + std::to_string (-20.0 * std::log10 (worstStop)));
    check (20.0 * std::log10 (pmax / pmin) < 1e-4, "passband ripple <= 1e-4 dB");
    // up -> down chain: an impulse comes out at exactly 46 base samples, DC gain 1.
    Upsampler2x up; Downsampler2x down;
    int peakAt = -1; double peak = 0.0;
    for (int n = 0; n < 200; ++n)
    {
        double u0, u1;
        up.process (n == 10 ? 1.0 : 0.0, u0, u1);
        const double y = down.process (u0, u1);
        if (std::fabs (y) > peak) { peak = std::fabs (y); peakAt = n - 10; }
    }
    check (peakAt == 46, "up + down latency = 46 base samples (23 each way), got " + std::to_string (peakAt));
    Upsampler2x up2; Downsampler2x down2;
    double y = 0.0, e0 = 0.0, e1 = 0.0;
    for (int n = 0; n < 400; ++n) { up2.process (1.0, e0, e1); y = down2.process (e0, e1); }
    check (std::fabs (y - 1.0) < 1e-4, "chain DC gain 1");
    check (e0 == 1.0, "even phase of the upsampler is the input itself (delayed 23)");
}

void testAliasing()
{
    // SHOGUN 4.6 static aliasing: 1031.25 Hz (bin 704 of 32768 at 48 kHz) at +-5 V (1 unit), WAVE 0.5.
    const int N = 32768, B0 = 704;
    auto run = [&] (int M, bool adaa) {
        TripleShaper ts; ts.setAdaa (adaa);
        ShaperControls c; c.macro = 0.5;
        std::vector<double> y ((size_t) N);
        Upsampler2x up; Downsampler2x dn;
        for (int n = 0; n < 2 * N; ++n)
        {
            const double x = std::sin (2.0 * kPi * B0 * n / N);
            double out;
            if (M == 1) out = ts.process (x, c, 0.0);
            else { double u0, u1; up.process (x, u0, u1); out = dn.process (ts.process (u0, c, 0.0), ts.process (u1, c, 0.0)); }
            if (n >= N) y[(size_t) (n - N)] = out;
        }
        return jidai::test::aliasDb (y, 48000.0, B0);
    };
    const double n1 = run (1, false), a1 = run (1, true), a2 = run (2, true);
    std::printf ("  alias 1031 Hz WAVE 0.5: 1x naive %.1f, 1x ADAA %.1f, 2x ADAA %.1f dB (spec -27.3 / -45.3 / -121.9 ideal decimator)\n", n1, a1, a2);
    check (std::fabs (n1 - (-27.3)) < 1.5, "1x naive alias matches the spec (-27.3 dB)");
    check (std::fabs (a1 - (-45.3)) < 1.5, "1x ADAA alias matches the spec (-45.3 dB)");
    check (a2 < -100.0, "2x ADAA alias below -100 dB with the real 93-tap pair");
}

}

int main()
{
    testStageLaw();
    testAdaa();
    testMacro();
    testLevelComp();
    testHalfband();
    testAliasing();
    std::printf ("%d checks, %d failed\n%s\n", checks, failures, failures == 0 ? "JIDAI DSP TESTS PASS" : "JIDAI DSP TESTS FAIL");
    return failures == 0 ? 0 : 1;
}

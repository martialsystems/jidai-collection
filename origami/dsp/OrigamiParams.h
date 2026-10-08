// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
#pragma once
// ORIGAMI parameters: one table for the standalone plugin and the rack device (state format 1, unit ORIGAMI, JCS R7).
// Framework-free. Values are stored in their natural units (dB, ms, 0..1, -1..1, choice index).

#include <cmath>
#include <string>

namespace origami {

enum Param : int {
    // MAIN
    kGain, kVcaDepth, kVcaSource, kWave, kEmph, kSym, kStage1, kStage2, kStage3, kMix, kLevel, kLevelComp, kBypass,
    // STAGES
    kSym1, kSym2, kSym3, kVc1Amt, kVc2Amt, kVc3Amt, kVc1Sym, kVc2Sym, kVc3Sym, kVc1Src, kVc2Src, kVc3Src,
    // DYNAMICS
    kAttack, kRelease, kCvScale, kVcaLaw,
    // SETUP
    kQuality,
    kParamCount
};

enum class Page { Main, Stages, Dynamics, Setup };

struct ParamInfo
{
    const char* id;        // state / automation id (never reused, JCS R7)
    const char* name;      // panel label
    double min, max, def;
    int choices;           // 0 = continuous, n = n-way switch
    const char* const* choiceNames;
    const char* unit;
    Page page;
};

inline constexpr const char* kVcaSourceNames[] = { "FOLLOW", "CV" };
inline constexpr const char* kVcSrcNames[] = { "JACK", "INPUT", "FOLLOW", "SIDECHAIN" };
inline constexpr const char* kLawNames[] = { "LIN", "EXP" };
inline constexpr const char* kQualityNames[] = { "1x", "2x" };
inline constexpr const char* kOffOn[] = { "OFF", "ON" };

inline const ParamInfo& paramInfo (int p)
{
    static const ParamInfo t[kParamCount] = {
        { "gain",       "GAIN",       -24.0, 12.0,  0.0, 0, nullptr, "dB", Page::Main },
        { "vca_depth",  "VCA DEPTH",    0.0,  1.0,  0.0, 0, nullptr, "",   Page::Main },
        { "vca_source", "VCA SOURCE",   0.0,  1.0,  0.0, 2, kVcaSourceNames, "", Page::Main },
        { "wave",       "WAVE",         0.0,  1.0,  0.0, 0, nullptr, "",   Page::Main },
        { "emph",       "EMPH",         0.0, 12.0,  0.0, 0, nullptr, "dB", Page::Main },
        { "sym",        "SYM",         -1.0,  1.0,  0.0, 0, nullptr, "",   Page::Main },
        { "stage1",     "STAGE 1",     -1.0,  1.0,  0.0, 0, nullptr, "",   Page::Main },
        { "stage2",     "STAGE 2",     -1.0,  1.0,  0.0, 0, nullptr, "",   Page::Main },
        { "stage3",     "STAGE 3",     -1.0,  1.0,  0.0, 0, nullptr, "",   Page::Main },
        { "mix",        "MIX",          0.0,  1.0,  1.0, 0, nullptr, "",   Page::Main },
        { "level",      "LEVEL",      -36.0,  6.0,  0.0, 0, nullptr, "dB", Page::Main },
        { "level_comp", "LEVEL COMP",   0.0,  1.0,  1.0, 2, kOffOn, "",   Page::Main },
        { "bypass",     "BYPASS",       0.0,  1.0,  0.0, 2, kOffOn, "",   Page::Main },
        { "sym1",       "SYM 1",       -1.0,  1.0,  0.0, 0, nullptr, "",   Page::Stages },
        { "sym2",       "SYM 2",       -1.0,  1.0,  0.0, 0, nullptr, "",   Page::Stages },
        { "sym3",       "SYM 3",       -1.0,  1.0,  0.0, 0, nullptr, "",   Page::Stages },
        { "vc1_amt",    "VC 1 > AMT",  -1.0,  1.0,  0.0, 0, nullptr, "",   Page::Stages },
        { "vc2_amt",    "VC 2 > AMT",  -1.0,  1.0,  0.0, 0, nullptr, "",   Page::Stages },
        { "vc3_amt",    "VC 3 > AMT",  -1.0,  1.0,  0.0, 0, nullptr, "",   Page::Stages },
        { "vc1_sym",    "VC 1 > SYM",  -1.0,  1.0,  0.0, 0, nullptr, "",   Page::Stages },
        { "vc2_sym",    "VC 2 > SYM",  -1.0,  1.0,  0.0, 0, nullptr, "",   Page::Stages },
        { "vc3_sym",    "VC 3 > SYM",  -1.0,  1.0,  0.0, 0, nullptr, "",   Page::Stages },
        { "vc1_src",    "VC 1 SOURCE",  0.0,  3.0,  0.0, 4, kVcSrcNames, "", Page::Stages },
        { "vc2_src",    "VC 2 SOURCE",  0.0,  3.0,  0.0, 4, kVcSrcNames, "", Page::Stages },
        { "vc3_src",    "VC 3 SOURCE",  0.0,  3.0,  0.0, 4, kVcSrcNames, "", Page::Stages },
        { "attack",     "ATTACK",       0.1, 50.0,  1.0, 0, nullptr, "ms", Page::Dynamics },
        { "release",    "RELEASE",      5.0, 1000.0, 80.0, 0, nullptr, "ms", Page::Dynamics },
        { "cv_scale",   "CV SCALE",     0.0,  2.0,  1.0, 0, nullptr, "x",  Page::Dynamics },
        { "vca_law",    "VCA LAW",      0.0,  1.0,  0.0, 2, kLawNames, "", Page::Dynamics },
        { "quality",    "QUALITY",      0.0,  1.0,  0.0, 2, kQualityNames, "", Page::Setup },
    };
    return t[p >= 0 && p < kParamCount ? p : 0];
}

inline int paramIndex (const std::string& id)
{
    for (int i = 0; i < kParamCount; ++i)
        if (id == paramInfo (i).id)
            return i;
    return -1;
}

inline double clampParam (int p, double v)
{
    const auto& i = paramInfo (p);
    if (v != v) v = i.def;
    v = v < i.min ? i.min : (v > i.max ? i.max : v);
    if (i.choices > 0) v = (double) (long) (v + 0.5);
    return v;
}

// Normalised 0..1 <-> natural units (linear; ATTACK and RELEASE are log-skewed for the knobs).
inline double toNormal (int p, double v)
{
    const auto& i = paramInfo (p);
    v = clampParam (p, v);
    if (p == kAttack || p == kRelease)
        return (std::log (v) - std::log (i.min)) / (std::log (i.max) - std::log (i.min));
    return (v - i.min) / (i.max - i.min);
}
inline double fromNormal (int p, double n)
{
    const auto& i = paramInfo (p);
    n = n < 0.0 ? 0.0 : (n > 1.0 ? 1.0 : n);
    if (p == kAttack || p == kRelease)
        return clampParam (p, std::exp (std::log (i.min) + n * (std::log (i.max) - std::log (i.min))));
    return clampParam (p, i.min + n * (i.max - i.min));
}

inline constexpr int kStateFormat = 1;
inline constexpr const char* kStateUnit = "ORIGAMI";

} // namespace origami

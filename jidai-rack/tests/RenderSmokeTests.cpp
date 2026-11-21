// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
//
// Offline render smoke test (target: about 1 s wall). No GUI, no audio device.
// Feeds 1 s at 48 kHz (impulse, white noise, and a 0.1 s silent tail) through
// JidaiProcessor for an unpatched default rack, a HOST IN -> MAIN OUT through
// patch, and a RONIN saw voice, at several block sizes. Checks that the output
// is finite, has no subnormal floats, stays within a peak bound, is silent when
// nothing is patched to MAIN OUT or the input is silence, and is not silent
// when the through patch or the RONIN voice should make sound.
// JIDAI_SMOKE_WAV_DIR=<dir> also writes each render as a 32-bit float stereo WAV.
// These are sanity bounds, not a substitute for the rack and preset suites.

#include "plugin/JidaiProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace jidai;

namespace {

int* gChecks = nullptr;
int* gFailures = nullptr;

void check (bool ok, const std::string& what)
{
    ++*gChecks;
    if (! ok)
    {
        ++*gFailures;
        std::printf ("FAIL %s\n", what.c_str());
    }
}

struct Scan
{
    bool finite = true;
    bool subnormal = false;
    double peak = 0.0;
};

Scan scanBuffer (const juce::AudioBuffer<float>& buffer)
{
    Scan s;
    const int n = buffer.getNumSamples();
    const int ch = buffer.getNumChannels();
    for (int c = 0; c < ch; ++c)
        for (int i = 0; i < n; ++i)
        {
            const float v = buffer.getSample (c, i);
            if (! std::isfinite (v))
            {
                s.finite = false;
                continue;
            }
            s.peak = std::fmax (s.peak, (double) std::fabs (v));
            if (v != 0.0f && std::fabs (v) < std::numeric_limits<float>::min())
                s.subnormal = true;
        }
    return s;
}

void fillImpulse (juce::AudioBuffer<float>& buffer, int signalSamples)
{
    buffer.clear();
    const int n = buffer.getNumSamples();
    const int mid = std::min (std::max (signalSamples / 100, 1), n - 1);
    if (n > 0 && buffer.getNumChannels() > 0)
        buffer.setSample (0, mid, 0.9f);
    if (n > 0 && buffer.getNumChannels() > 1)
        buffer.setSample (1, mid, 0.9f);
}

void fillNoise (juce::AudioBuffer<float>& buffer, int signalSamples, unsigned seed)
{
    buffer.clear();
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
    const int n = std::min (signalSamples, buffer.getNumSamples());
    for (int i = 0; i < n; ++i)
    {
        const float v = dist (rng);
        if (buffer.getNumChannels() > 0)
            buffer.setSample (0, i, v);
        if (buffer.getNumChannels() > 1)
            buffer.setSample (1, i, v * 0.8f);
    }
}

void writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double fs)
{
    file.getParentDirectory().createDirectory();
    FILE* f = std::fopen (file.getFullPathName().toRawUTF8(), "wb");
    if (f == nullptr)
        return;
    const int ch = buffer.getNumChannels();
    const int n = buffer.getNumSamples();
    const uint32_t bytes = (uint32_t) n * (uint32_t) ch * 4u;
    const uint32_t rate = (uint32_t) fs;
    const uint32_t byteRate = rate * (uint32_t) ch * 4u;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f);
    w32 (36 + bytes);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16);
    w16 (3);   // IEEE float
    w16 ((uint16_t) ch);
    w32 (rate);
    w32 (byteRate);
    w16 ((uint16_t) (ch * 4));
    w16 (32);
    std::fwrite ("data", 1, 4, f);
    w32 (bytes);
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < ch; ++c)
        {
            const float v = buffer.getSample (c, i);
            std::fwrite (&v, 4, 1, f);
        }
    std::fclose (f);
}

void patchThrough (JidaiProcessor& p)
{
    p.rack().connect ("RACK#1/HOST:IN L", "RACK#1/MAIN:OUT L");
    p.rack().connect ("RACK#1/HOST:IN R", "RACK#1/MAIN:OUT R");
}

void patchRoninSaw (JidaiProcessor& p)
{
    // Same voice as the v2 audio check in RackStateTests: RONIN#2's VCO SAW into its WET,
    // RONIN#1 passing the host dry. The host hears the saw.
    auto xml = std::make_unique<juce::XmlElement> ("JIDAIRACK");
    xml->setAttribute ("version", 2);
    for (int n = 1; n <= 2; ++n)
    {
        auto* ronin = xml->createNewChildElement ("DEVICE");
        ronin->setAttribute ("kind", "RONIN");
        ronin->setAttribute ("number", n);
        ronin->setAttribute ("program", 0);
        ronin->setAttribute ("effect", n == 1 ? 0 : 1);
    }
    auto* saw = xml->createNewChildElement ("CABLE");
    saw->setAttribute ("a", "RONIN#2/VCO:SAW");
    saw->setAttribute ("b", "RONIN#2/OUTPUT:WET");
    saw->setAttribute ("color", 0);
    saw->setAttribute ("age", 50);
    p.testRestore (*xml);
}

struct Case
{
    const char* rack;
    const char* signal;
    int block;
    void (*setup) (JidaiProcessor&);
    bool inputSilent;
    bool expectSilent;
    double peakLimit;
};

void renderCase (JidaiProcessor& p, const Case& c, int fs, int signalSamples, int tailSamples, const juce::File& wavDir)
{
    const int total = signalSamples + tailSamples;
    p.prepareToPlay ((double) fs, c.block);

    juce::AudioBuffer<float> buffer (2, total);
    if (c.inputSilent || std::strcmp (c.signal, "silence") == 0)
        buffer.clear();
    else if (std::strcmp (c.signal, "impulse") == 0)
        fillImpulse (buffer, signalSamples);
    else
        fillNoise (buffer, signalSamples, 12345);

    const double inPeak = (double) buffer.getMagnitude (0, total);
    juce::MidiBuffer midi;
    for (int start = 0; start < total; start += c.block)
    {
        const int n = std::min (c.block, total - start);
        juce::AudioBuffer<float> block (buffer.getNumChannels(), n);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            block.copyFrom (ch, 0, buffer, ch, start, n);
        juce::MidiBuffer slice;
        p.processBlock (block, slice);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.copyFrom (ch, start, block, ch, 0, n);
    }

    const Scan s = scanBuffer (buffer);
    char tag[160];
    std::snprintf (tag, sizeof tag, "%s %s block %d (%d samples)", c.rack, c.signal, c.block, total);

    check (s.finite, std::string ("NaN/Inf: ") + tag);
    check (! s.subnormal, std::string ("subnormal sample: ") + tag);
    check (s.peak <= c.peakLimit, std::string ("sample peak ") + std::to_string (s.peak) + " exceeds " + std::to_string (c.peakLimit) + ": " + tag);
    if (c.expectSilent)
        check (s.peak < 1e-6, std::string ("expected silence, peak ") + std::to_string (s.peak) + ": " + tag);
    else
        check (s.peak > 1e-4, std::string ("silent output when sound was expected (in-peak ") + std::to_string (inPeak) + "): " + tag);

    std::printf ("INFO smoke %s: in-peak %.4f out-peak %.4f%s%s\n",
                 tag, inPeak, s.peak, s.finite ? "" : " NaN/Inf", s.subnormal ? " subnormal" : "");

    if (wavDir != juce::File())
        writeWav (wavDir.getChildFile (juce::String (tag).replaceCharacter (' ', '_') + ".wav"), buffer, (double) fs);
}

} // namespace

void runRenderSmokeTests (int& checks, int& failures)
{
    gChecks = &checks;
    gFailures = &failures;

    const int fs = 48000;
    // One full 1 s render (plus a 0.1 s tail) on the through path; other cases use 100 ms so the
    // whole test stays near one second of wall time after the processor is up.
    const int oneSec = fs;
    const int shortSig = fs / 10;
    const int tail = fs / 10;
    const int shortTail = fs / 20;

    const char* wavEnv = std::getenv ("JIDAI_SMOKE_WAV_DIR");
    juce::File wavDir = (wavEnv != nullptr && wavEnv[0] != '\0') ? juce::File (juce::String::fromUTF8 (wavEnv)) : juce::File();

    JidaiProcessor def;
    JidaiProcessor thru;
    patchThrough (thru);
    JidaiProcessor ronin;
    patchRoninSaw (ronin);

    const Case oneSecondThrough { "through", "noise", 512, patchThrough, false, false, 1.0 };
    renderCase (thru, oneSecondThrough, fs, oneSec, tail, wavDir);

    const Case shortCases[] = {
        { "default", "impulse", 512, nullptr, false, true, 1e-6 },
        { "default", "noise", 64, nullptr, false, true, 1e-6 },
        { "default", "silence", 512, nullptr, true, true, 1e-6 },
        { "through", "impulse", 512, nullptr, false, false, 1.0 },
        { "through", "noise", 37, nullptr, false, false, 1.0 },
        { "through", "silence", 512, nullptr, true, true, 1e-6 },
        { "ronin", "silence", 512, nullptr, true, false, 8.0 },
        { "ronin", "noise", 256, nullptr, false, false, 8.0 },
        { "ronin", "impulse", 64, nullptr, false, false, 8.0 },
    };
    for (const auto& c : shortCases)
    {
        JidaiProcessor* p = &def;
        if (std::strcmp (c.rack, "through") == 0)
            p = &thru;
        else if (std::strcmp (c.rack, "ronin") == 0)
            p = &ronin;
        renderCase (*p, c, fs, shortSig, shortTail, wavDir);
    }
}

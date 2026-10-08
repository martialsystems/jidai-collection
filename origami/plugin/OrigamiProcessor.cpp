// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "OrigamiProcessor.h"
#include "OrigamiEditor.h"
#include "OrigamiState.h"

using namespace origami;

namespace {
juce::NormalisableRange<float> rangeFor (int p)
{
    const auto& i = paramInfo (p);
    if (p == kAttack || p == kRelease)
        return { (float) i.min, (float) i.max,
                 [p] (float, float, float n) { return (float) fromNormal (p, n); },
                 [p] (float, float, float v) { return (float) toNormal (p, v); } };
    return { (float) i.min, (float) i.max };
}
}

juce::AudioProcessorValueTreeState::ParameterLayout OrigamiProcessor::makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int p = 0; p < kParamCount; ++p)
    {
        const auto& i = paramInfo (p);
        const juce::ParameterID id { i.id, 1 };
        if (i.choices > 0)
        {
            juce::StringArray names;
            for (int k = 0; k < i.choices; ++k) names.add (i.choiceNames[k]);
            layout.add (std::make_unique<juce::AudioParameterChoice> (id, i.name, names, (int) i.def));
        }
        else
        {
            const juce::String unit (i.unit);
            layout.add (std::make_unique<juce::AudioParameterFloat> (id, i.name, rangeFor (p), (float) i.def,
                juce::AudioParameterFloatAttributes().withLabel (unit)
                    .withStringFromValueFunction ([unit] (float v, int) { return juce::String (v, unit == "ms" ? 1 : 2); })));
        }
    }
    return layout;
}

OrigamiProcessor::OrigamiProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts_ (*this, nullptr, "ORIGAMI_PARAMS", makeLayout())
{
    for (int p = 0; p < kParamCount; ++p)
    {
        params_[(size_t) p] = apvts_.getParameter (paramInfo (p).id);
        raw_[(size_t) p] = apvts_.getRawParameterValue (paramInfo (p).id);
    }
    pullParams (true);
}

OrigamiProcessor::~OrigamiProcessor() = default;

bool OrigamiProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (in != out) return false;
    if (l.inputBuses.size() > 1)
    {
        const auto sc = l.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

double OrigamiProcessor::value (int p) const { return (double) raw_[(size_t) p]->load(); }

void OrigamiProcessor::setValue (int p, double v)
{
    auto* prm = params_[(size_t) p];
    prm->setValueNotifyingHost (prm->convertTo0to1 ((float) clampParam (p, v)));
}

void OrigamiProcessor::beginGesture (int p) { params_[(size_t) p]->beginChangeGesture(); }
void OrigamiProcessor::endGesture (int p) { params_[(size_t) p]->endChangeGesture(); }

juce::AudioProcessorParameter* OrigamiProcessor::getBypassParameter() const { return params_[(size_t) kBypass]; }

void OrigamiProcessor::pullParams (bool force)
{
    for (int p = 0; p < kParamCount; ++p)
    {
        const double v = clampParam (p, (double) raw_[(size_t) p]->load (std::memory_order_relaxed));
        if (force || ! jidai::dsp::same (v, core_.param (p)))
            core_.setParam (p, v);
    }
}

void OrigamiProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRate_ = sampleRate;
    pullParams (true);
    core_.prepare (sampleRate);
    scFollow_ = 0.0;
    setLatencySamples (core_.latencySamples());
}

void OrigamiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    const int lat = core_.latencySamples();
    pullParams (false);
    if (core_.latencySamples() != lat)
        setLatencySamples (core_.latencySamples());

    auto main = getBusBuffer (buffer, true, 0);
    auto out = getBusBuffer (buffer, false, 0);
    const bool scLive = getBusCount (true) > 1 && getBus (true, 1)->isEnabled() && getBusBuffer (buffer, true, 1).getNumChannels() > 0;
    juce::AudioBuffer<float> sc;
    if (scLive) sc = getBusBuffer (buffer, true, 1);

    const int n = buffer.getNumSamples();
    const int inCh = main.getNumChannels(), outCh = out.getNumChannels();
    const double atk = 1.0 - std::exp (-1.0 / (core_.param (kAttack) * 0.001 * sampleRate_));
    const double rel = 1.0 - std::exp (-1.0 / (core_.param (kRelease) * 0.001 * sampleRate_));
    {
        const bool noVcJacks[3] { false, false, false };     // the plugin has no VC jacks; VC comes from SRC
        core_.planBlock (noVcJacks, scLive);
    }
    for (int i = 0; i < n; ++i)
    {
        OrigamiCore::Inputs in;
        in.inL = inCh > 0 ? (double) main.getSample (0, i) * 5.0 : 0.0;
        in.inR = inCh > 1 ? (double) main.getSample (1, i) * 5.0 : in.inL;
        if (scLive)
        {
            double s = 0.0;
            for (int c = 0; c < sc.getNumChannels(); ++c) s += (double) sc.getSample (c, i);
            s = s / (double) sc.getNumChannels() * 5.0;
            in.sidechain = s;
            in.sidechainLive = true;
            const double pk = std::fabs (s);
            scFollow_ += (pk - scFollow_) * (pk > scFollow_ ? atk : rel);
            if (scFollow_ < 1e-30) scFollow_ = 0.0;
            in.vcaCv = juce::jmin (5.0, scFollow_);
            in.vcaPatched = true;
        }
        double l, r;
        core_.process (in, l, r);
        if (outCh > 0) out.setSample (0, i, (float) (l / 5.0));
        if (outCh > 1) out.setSample (1, i, (float) (r / 5.0));
    }
    for (int c = 2; c < outCh; ++c) out.clear (c, 0, n);
}

juce::AudioProcessorEditor* OrigamiProcessor::createEditor() { return new OrigamiEditor (*this); }

void OrigamiProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    double v[kParamCount];
    for (int p = 0; p < kParamCount; ++p) v[p] = value (p);
    auto x = stateToXml (v);
    x->setAttribute ("ui_scale", (double) uiScale);
    copyXmlToBinary (*x, dest);
}

void OrigamiProcessor::setStateInformation (const void* data, int size)
{
    auto x = getXmlFromBinary (data, size);
    if (x == nullptr) return;
    double v[kParamCount];
    if (! stateFromXml (*x, v).ok) return;
    for (int p = 0; p < kParamCount; ++p) setValue (p, v[p]);
    uiScale = (float) juce::jlimit (0.75, 2.0, x->getDoubleAttribute ("ui_scale", 1.0));
}

#if ! ORIGAMI_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OrigamiProcessor(); }
#endif

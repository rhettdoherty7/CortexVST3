#include "PluginProcessor.h"

#include "Params.h"
#include "PluginEditor.h"
#include "Version.h"

namespace cortex
{

CortexProcessor::CortexProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CortexParams", params::createLayout())
{
    sourceType       = apvts.getRawParameterValue (params::id::sourceType);
    kickSensitivity  = apvts.getRawParameterValue (params::id::kickSensitivity);
    snareSensitivity = apvts.getRawParameterValue (params::id::snareSensitivity);
    hatSensitivity   = apvts.getRawParameterValue (params::id::hatSensitivity);
    kickRelease      = apvts.getRawParameterValue (params::id::kickRelease);
    snareRelease     = apvts.getRawParameterValue (params::id::snareRelease);
    hatRelease       = apvts.getRawParameterValue (params::id::hatRelease);
}

void CortexProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    analysisRunning = false;
    setLatencySamples (0);
}

bool CortexProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void CortexProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Real-time rules (CLAUDE.md 4.3): no allocation, locks, I/O or logging in here, and the
    // audio in `buffer` is never written. Input and output share the buffer, so leaving it
    // alone is a bit-identical passthrough.
    juce::ScopedNoDenormals noDenormals;

    const int numInputs  = getTotalNumInputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = numInputs; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const bool wanted = analysisRequested.load (std::memory_order_acquire);
    if (wanted && ! analysisRunning)
        engine.reset();   // start clean rather than from whatever was in the buffers last time
    analysisRunning = wanted;

    if (! analysisRunning || numInputs < 1 || numSamples < 1)
        return;

    HostTime host;
    if (auto* head = getPlayHead())
    {
        if (const auto position = head->getPosition())
        {
            host.playing = position->getIsPlaying();
            if (const auto bpm = position->getBpm())                       host.bpm = *bpm;
            if (const auto ppq = position->getPpqPosition())               host.ppq = *ppq;
            if (const auto bar = position->getPpqPositionOfLastBarStart()) host.barStartPpq = *bar;
            if (const auto sig = position->getTimeSignature())
            {
                host.timeSigNum = sig->numerator;
                host.timeSigDen = sig->denominator;
            }
        }
    }

    AnalysisSettings settings;
    settings.source           = (SourceType) juce::jlimit (0, 6, juce::roundToInt (sourceType->load (std::memory_order_relaxed)));
    settings.kickSensitivity  = kickSensitivity->load (std::memory_order_relaxed);
    settings.snareSensitivity = snareSensitivity->load (std::memory_order_relaxed);
    settings.hatSensitivity   = hatSensitivity->load (std::memory_order_relaxed);
    settings.kickRelease      = kickRelease->load (std::memory_order_relaxed);
    settings.snareRelease     = snareRelease->load (std::memory_order_relaxed);
    settings.hatRelease       = hatRelease->load (std::memory_order_relaxed);

    engine.process (buffer.getArrayOfReadPointers(), juce::jmin (numInputs, 2), numSamples, host, settings, *this);
}

juce::AudioProcessorEditor* CortexProcessor::createEditor()
{
    return new CortexEditor (*this);
}

void CortexProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", stateVersion, nullptr);
    state.setProperty ("pluginVersion", versionString, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void CortexProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace cortex

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cortex::CortexProcessor();
}

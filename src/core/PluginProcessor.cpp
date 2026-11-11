#include "PluginProcessor.h"

#include "Params.h"
#include "PluginEditor.h"
#include "Version.h"

#include <engine/Scene.h>

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

    for (int i = 0; i < numLearnSlots; ++i)
    {
        midiMap[(size_t) i].store (-1);
        pendingValue[(size_t) i].store (0.0f);
        pendingFlag[(size_t) i].store (false);
    }

    startTimerHz (30);
}

// --- MIDI learn ----------------------------------------------------------------------------------

int CortexProcessor::midiControllerFor (int slot) const noexcept
{
    return slot >= 0 && slot < numLearnSlots ? midiMap[(size_t) slot].load (std::memory_order_acquire) : -1;
}

void CortexProcessor::clearMidiController (int slot) noexcept
{
    if (slot >= 0 && slot < numLearnSlots)
        midiMap[(size_t) slot].store (-1, std::memory_order_release);
}

void CortexProcessor::handleMidi (const juce::MidiBuffer& midi) noexcept
{
    // Audio thread: only atomics here. The timer below turns values into parameter changes.
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (! message.isController())
            continue;

        const int controller = message.getControllerNumber();
        const int learning = learnSlot.load (std::memory_order_acquire);
        if (learning >= 0 && learning < numLearnSlots)
        {
            for (auto& m : midiMap)   // one controller drives one slot
                if (m.load (std::memory_order_relaxed) == controller)
                    m.store (-1, std::memory_order_relaxed);
            midiMap[(size_t) learning].store (controller, std::memory_order_release);
            learnSlot.store (-1, std::memory_order_release);
        }

        for (int slot = 0; slot < numLearnSlots; ++slot)
        {
            if (midiMap[(size_t) slot].load (std::memory_order_acquire) == controller)
            {
                pendingValue[(size_t) slot].store ((float) message.getControllerValue() / 127.0f, std::memory_order_relaxed);
                pendingFlag[(size_t) slot].store (true, std::memory_order_release);
            }
        }
    }
}

juce::RangedAudioParameter* CortexProcessor::learnTarget (int slot) const
{
    if (slot == sceneLearnSlot)
        return apvts.getParameter (params::id::sceneSelect);
    return apvts.getParameter (params::id::macro (slot));
}

void CortexProcessor::timerCallback()
{
    for (int slot = 0; slot < numLearnSlots; ++slot)
    {
        if (! pendingFlag[(size_t) slot].exchange (false, std::memory_order_acq_rel))
            continue;

        auto* param = learnTarget (slot);
        if (param == nullptr)
            continue;

        float normalised = pendingValue[(size_t) slot].load (std::memory_order_relaxed);
        if (slot == sceneLearnSlot)
        {
            // Spread the controller's range over the factory scenes.
            const int scene = juce::roundToInt (normalised * (float) (numFactoryScenes() - 1)) + 1;
            normalised = param->convertTo0to1 ((float) scene);
        }

        param->beginChangeGesture();
        param->setValueNotifyingHost (normalised);
        param->endChangeGesture();
    }
}

// --- Cover art -----------------------------------------------------------------------------------

void CortexProcessor::setCoverPalette (std::optional<Palette> palette)
{
    {
        const juce::SpinLock::ScopedLockType lock (coverLock);
        cover = palette;
    }
    coverVersion.fetch_add (1, std::memory_order_acq_rel);
}

std::optional<Palette> CortexProcessor::coverPalette() const
{
    const juce::SpinLock::ScopedLockType lock (coverLock);
    return cover;
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

void CortexProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Real-time rules (CLAUDE.md 4.3): no allocation, locks, I/O or logging in here, and the
    // audio in `buffer` is never written. Input and output share the buffer, so leaving it
    // alone is a bit-identical passthrough.
    juce::ScopedNoDenormals noDenormals;

    const int numInputs  = getTotalNumInputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = numInputs; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    handleMidi (midi);   // MIDI learn works whether or not the editor is open

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

    juce::ValueTree midiTree ("MidiLearn");
    for (int slot = 0; slot < numLearnSlots; ++slot)
        if (const int cc = midiControllerFor (slot); cc >= 0)
            midiTree.setProperty ("slot" + juce::String (slot), cc, nullptr);
    state.removeChild (state.getChildWithName ("MidiLearn"), nullptr);
    state.appendChild (midiTree, nullptr);

    state.removeChild (state.getChildWithName ("CoverPalette"), nullptr);
    if (const auto palette = coverPalette())
    {
        juce::StringArray hex;
        for (auto c : palette->srgbHex)
            hex.add (juce::String::toHexString ((int) c).paddedLeft ('0', 6));
        juce::ValueTree coverTree ("CoverPalette");
        coverTree.setProperty ("colours", hex.joinIntoString (" "), nullptr);
        state.appendChild (coverTree, nullptr);
    }

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void CortexProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    const auto midiTree = state.getChildWithName ("MidiLearn");
    for (int slot = 0; slot < numLearnSlots; ++slot)
        midiMap[(size_t) slot].store ((int) midiTree.getProperty ("slot" + juce::String (slot), -1));

    std::optional<Palette> palette;
    const auto coverTree = state.getChildWithName ("CoverPalette");
    const auto hex = juce::StringArray::fromTokens (coverTree.getProperty ("colours").toString(), " ", "");
    if (hex.size() == 6)
    {
        Palette p;
        p.name = "Cover Art";
        for (int i = 0; i < 6; ++i)
            p.srgbHex[(size_t) i] = (uint32_t) hex[i].getHexValue32();
        palette = p;
    }
    setCoverPalette (palette);

    apvts.replaceState (state);
}

} // namespace cortex

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cortex::CortexProcessor();
}

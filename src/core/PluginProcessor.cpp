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

    slots[0] = slots[1] = factoryPreset (0);
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
    // Scene select (automation, MIDI learn) loads a factory scene. Look select changes the Look.
    if (auto* sceneParam = apvts.getParameter (params::id::sceneSelect))
    {
        const int value = juce::roundToInt (sceneParam->convertFrom0to1 (sceneParam->getValue()));
        if (value != lastSceneParam)
        {
            lastSceneParam = value;
            if (value >= 1 && value <= numFactoryScenes())
                loadPreset (factoryPreset (value - 1));
        }
    }
    syncLookFromParameter();

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
    editScene ([&palette] (ScenePreset& p) { p.cover = palette; });
}

// --- Scene ---------------------------------------------------------------------------------------

ScenePreset CortexProcessor::scene() const
{
    const juce::SpinLock::ScopedLockType lock (sceneLock);
    return slots[(size_t) active];
}

void CortexProcessor::loadPreset (ScenePreset preset)
{
    const int look = preset.look;
    {
        const juce::SpinLock::ScopedLockType lock (sceneLock);
        slots[(size_t) active] = std::move (preset);
    }
    modified[(size_t) active] = false;
    version.fetch_add (1, std::memory_order_acq_rel);
    setLookParameter (look);
}

void CortexProcessor::selectSlot (int slot)
{
    if (slot == active || slot < 0 || slot > 1)
        return;
    {
        const juce::SpinLock::ScopedLockType lock (sceneLock);
        active = slot;
    }
    version.fetch_add (1, std::memory_order_acq_rel);
    setLookParameter (scene().look);
}

void CortexProcessor::copyActiveToOther()
{
    const juce::SpinLock::ScopedLockType lock (sceneLock);
    slots[(size_t) (1 - active)] = slots[(size_t) active];
    modified[(size_t) (1 - active)] = modified[(size_t) active];
}

void CortexProcessor::markSaved (const juce::String& name)
{
    editScene ([&name] (ScenePreset& p) { p.scene.name = name.toStdString(); });
    modified[(size_t) active] = false;
}

void CortexProcessor::setLook (int look)
{
    look = juce::jlimit (0, numFactoryLooks() - 1, look);
    editScene ([look] (ScenePreset& p) { p.look = look; });
    setLookParameter (look);
}

void CortexProcessor::setLookParameter (int look)
{
    lastLookParam = look;
    if (auto* param = apvts.getParameter (params::id::lookSelect))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) look));
        param->endChangeGesture();
    }
}

void CortexProcessor::syncLookFromParameter()
{
    auto* param = apvts.getParameter (params::id::lookSelect);
    if (param == nullptr)
        return;

    const int value = juce::jlimit (0, numFactoryLooks() - 1, juce::roundToInt (param->convertFrom0to1 (param->getValue())));
    if (value != lastLookParam)
    {
        lastLookParam = value;
        editScene ([value] (ScenePreset& p) { p.look = value; });
    }
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

    // The scene itself, as preset JSON (section 11): both A/B slots and which one is showing.
    state.removeChild (state.getChildWithName ("Scenes"), nullptr);
    juce::ValueTree scenes ("Scenes");
    {
        const juce::SpinLock::ScopedLockType lock (sceneLock);
        scenes.setProperty ("active", active, nullptr);
        scenes.setProperty ("a", presetToJson (slots[0]), nullptr);
        scenes.setProperty ("b", presetToJson (slots[1]), nullptr);
    }
    state.appendChild (scenes, nullptr);

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

    apvts.replaceState (state);

    // Restore the scenes. The parameters were restored above; note their values so the timer does
    // not mistake them for new changes and reload a factory scene over the saved one.
    const auto scenes = state.getChildWithName ("Scenes");
    {
        const juce::SpinLock::ScopedLockType lock (sceneLock);
        juce::String error;
        if (auto a = presetFromJson (scenes.getProperty ("a").toString(), error)) slots[0] = std::move (*a);
        if (auto b = presetFromJson (scenes.getProperty ("b").toString(), error)) slots[1] = std::move (*b);
        active = juce::jlimit (0, 1, (int) scenes.getProperty ("active", 0));
        modified = { false, false };
    }

    if (auto* sceneParam = apvts.getParameter (params::id::sceneSelect))
        lastSceneParam = juce::roundToInt (sceneParam->convertFrom0to1 (sceneParam->getValue()));
    if (auto* lookParam = apvts.getParameter (params::id::lookSelect))
    {
        lastLookParam = juce::roundToInt (lookParam->convertFrom0to1 (lookParam->getValue()));
        const juce::SpinLock::ScopedLockType lock (sceneLock);
        slots[(size_t) active].look = lastLookParam;
    }
    version.fetch_add (1, std::memory_order_acq_rel);
}

} // namespace cortex

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cortex::CortexProcessor();
}

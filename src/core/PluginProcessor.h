#pragma once

#include <analysis/AnalysisEngine.h>
#include <analysis/FrameFifo.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace cortex
{

/** The plugin. Audio passes through untouched; a copy is analysed and the results are queued
    for whoever is drawing (section 3). */
class CortexProcessor final : public juce::AudioProcessor,
                              private FrameSink
{
public:
    CortexProcessor();
    ~CortexProcessor() override = default;

    // --- Analysis consumer API (one consumer at a time: the editor / render thread) ------------
    /** Analysis only runs while someone is reading it. Call with true when a consumer attaches. */
    void setAnalysisActive (bool shouldRun) noexcept   { analysisRequested.store (shouldRun, std::memory_order_release); }
    bool popFrame (AnalysisFrame& out) noexcept        { return frames.pop (out); }

    juce::AudioProcessorValueTreeState apvts;

    // --- juce::AudioProcessor ------------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return "Cortex"; }
    bool acceptsMidi() const override                     { return false; }
    bool producesMidi() const override                    { return false; }
    bool isMidiEffect() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 0.0; }

    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    void onFrame (const AnalysisFrame& frame) noexcept override   { frames.push (frame); }

    AnalysisEngine engine;
    SpscFifo<AnalysisFrame> frames { 64 };
    std::atomic<bool> analysisRequested { false };
    bool analysisRunning = false;   // audio thread only

    std::atomic<float>* sourceType = nullptr;
    std::atomic<float>* kickSensitivity = nullptr;
    std::atomic<float>* snareSensitivity = nullptr;
    std::atomic<float>* hatSensitivity = nullptr;
    std::atomic<float>* kickRelease = nullptr;
    std::atomic<float>* snareRelease = nullptr;
    std::atomic<float>* hatRelease = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CortexProcessor)
};

} // namespace cortex

#pragma once

#include <analysis/AnalysisEngine.h>
#include <analysis/FrameFifo.h>
#include <engine/Palette.h>
#include <presets/PresetFormat.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <optional>

namespace cortex
{

/** The plugin. Audio passes through untouched; a copy is analysed and the results are queued
    for whoever is drawing (section 3). */
class CortexProcessor final : public juce::AudioProcessor,
                              private FrameSink,
                              private juce::Timer
{
public:
    // --- MIDI learn (section 6): slots 0..7 are the macros, slot 8 is scene select -------------
    static constexpr int numLearnSlots = 9;
    static constexpr int sceneLearnSlot = 8;

    void startMidiLearn (int slot) noexcept    { learnSlot.store (slot, std::memory_order_release); }
    void cancelMidiLearn() noexcept            { learnSlot.store (-1, std::memory_order_release); }
    int  learningSlot() const noexcept         { return learnSlot.load (std::memory_order_acquire); }
    int  midiControllerFor (int slot) const noexcept;   // -1 when none
    void clearMidiController (int slot) noexcept;

    // --- The scene on screen (sections 5.1, 11) ------------------------------------------------
    // Two slots for A/B comparison. The message thread edits; the GL thread copies the active slot
    // whenever sceneVersion() changes. Saved with the project as preset JSON.

    ScenePreset scene() const;
    int sceneVersion() const noexcept   { return version.load (std::memory_order_acquire); }

    /** Loads a preset into the active slot (and sets the Look parameter to match). */
    void loadPreset (ScenePreset);

    /** Changes the active slot in place. Marks the scene as modified. */
    template <typename Fn>
    void editScene (Fn&& fn)
    {
        {
            const juce::SpinLock::ScopedLockType lock (sceneLock);
            fn (slots[(size_t) active]);
        }
        modified[(size_t) active] = true;
        version.fetch_add (1, std::memory_order_acq_rel);
    }

    int activeSlot() const noexcept     { return active; }
    void selectSlot (int slot);
    void copyActiveToOther();
    bool isModified() const noexcept    { return modified[(size_t) active]; }
    void markSaved (const juce::String& name);
    void setLook (int look);   // the scene's Look and the Look parameter together

    // Cover-art palette (section 5.6): part of the scene, so it travels with presets.
    void setCoverPalette (std::optional<Palette>);
    std::optional<Palette> coverPalette() const   { return scene().cover; }

    CortexProcessor();
    ~CortexProcessor() override = default;

    // --- Analysis consumer API (one consumer at a time: the editor / render thread) ------------
    /** Analysis only runs while someone is reading it. Call with true when a consumer attaches. */
    void setAnalysisActive (bool shouldRun) noexcept   { analysisRequested.store (shouldRun, std::memory_order_release); }

    /** Hands every queued frame to `fn`, oldest first. Safe to call from more than one thread
        (the editor's timer and the GL thread): consumers take turns; the audio thread never waits. */
    template <typename Fn>
    void drainFrames (Fn&& fn)
    {
        const juce::SpinLock::ScopedLockType lock (consumerLock);
        AnalysisFrame frame;
        while (frames.pop (frame))
            fn (frame);
    }

    juce::AudioProcessorValueTreeState apvts;

    // --- juce::AudioProcessor ------------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return "Cortex"; }
    bool acceptsMidi() const override                     { return true; }
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
    void timerCallback() override;
    void handleMidi (const juce::MidiBuffer&) noexcept;
    juce::RangedAudioParameter* learnTarget (int slot) const;

    // MIDI learn state: the audio thread writes, the message-thread timer applies.
    std::atomic<int> learnSlot { -1 };
    std::array<std::atomic<int>, numLearnSlots> midiMap;
    std::array<std::atomic<float>, numLearnSlots> pendingValue;
    std::array<std::atomic<bool>, numLearnSlots> pendingFlag;

    void syncLookFromParameter();
    void setLookParameter (int look);

    mutable juce::SpinLock sceneLock;
    std::array<ScenePreset, 2> slots;
    std::array<bool, 2> modified { false, false };
    int active = 0;                       // message thread
    std::atomic<int> version { 0 };
    int lastSceneParam = 1, lastLookParam = 0;

    AnalysisEngine engine;
    SpscFifo<AnalysisFrame> frames { 64 };
    std::atomic<bool> analysisRequested { false };
    juce::SpinLock consumerLock;   // consumers only; the audio thread never takes it
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

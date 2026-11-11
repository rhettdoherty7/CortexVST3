#pragma once

#include "PluginProcessor.h"

#include <ui/CanvasComponent.h>

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <memory>

namespace cortex
{

/** Phase 2 editor: the visual canvas with a scene picker, plus the Phase 1 hit lights, band meters
    and detector controls along the bottom. Replaced by the final UI (section 10) in Phase 3. */
class CortexEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit CortexEditor (CortexProcessor&);
    ~CortexEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void drawHitLight (juce::Graphics&, juce::Rectangle<float>, const juce::String& label, juce::Colour, float envelope, float flash) const;
    void drawBands (juce::Graphics&, juce::Rectangle<float>) const;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    CortexProcessor& processor;
    CanvasComponent canvas;

    AnalysisFrame frame {};
    bool hasFrame = false;
    float kickFlash = 0.0f, snareFlash = 0.0f, hatFlash = 0.0f;
    juce::String statusText, errorText;
    juce::File statsLog;          // set by the CORTEX_STATS_LOG environment variable (diagnostics)
    int statsTicks = 0;

    std::array<Knob, 6> knobs;
    juce::ComboBox sourceBox, sceneBox;
    juce::Label sourceLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment;
    juce::RangedAudioParameter* sceneParameter = nullptr;

    juce::Rectangle<int> titleArea, stripArea, lightsArea, bandsArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CortexEditor)
};

} // namespace cortex

#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <memory>

namespace cortex
{

/** Phase 1 debug editor: hit lights, band meters, spectrum, scope and the detector controls,
    drawn with plain JUCE graphics. Replaced by the real UI (section 10) in phase 3. */
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
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>) const;
    void drawBands (juce::Graphics&, juce::Rectangle<float>) const;
    void drawScope (juce::Graphics&, juce::Rectangle<float>) const;
    void drawReadout (juce::Graphics&, juce::Rectangle<float>) const;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    CortexProcessor& processor;
    AnalysisFrame frame {};
    bool hasFrame = false;
    float kickFlash = 0.0f, snareFlash = 0.0f, hatFlash = 0.0f;

    std::array<Knob, 6> knobs;
    juce::ComboBox sourceBox;
    juce::Label sourceLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment;

    juce::Rectangle<int> lightsArea, spectrumArea, bandsArea, scopeArea, readoutArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CortexEditor)
};

} // namespace cortex

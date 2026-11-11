#pragma once

#include "PluginProcessor.h"

#include <ui/CanvasComponent.h>

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <functional>
#include <memory>

namespace cortex
{

/** Phase 3 working editor: canvas, scene and Look pickers, the 8 macro knobs with MIDI learn,
    cover-art drop, and the analysis meters and detector controls along the bottom.
    The final layout of section 10 (module rail, inspector, preset browser) replaces it later. */
class CortexEditor final : public juce::AudioProcessorEditor,
                           public juce::FileDragAndDropTarget,
                           private juce::Timer
{
public:
    explicit CortexEditor (CortexProcessor&);
    ~CortexEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override   { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void showLearnMenu (int slot, juce::Component& anchor);
    void loadCoverArt (const juce::File&);

    void drawHitLight (juce::Graphics&, juce::Rectangle<float>, const juce::String& label, juce::Colour, float envelope, float flash) const;
    void drawBands (juce::Graphics&, juce::Rectangle<float>) const;

    /** A rotary slider that reports right-clicks instead of dragging on them. */
    struct LearnableSlider : juce::Slider
    {
        std::function<void()> onRightClick;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && onRightClick != nullptr) onRightClick();
            else juce::Slider::mouseDown (e);
        }
    };

    struct LearnableCombo : juce::ComboBox
    {
        std::function<void()> onRightClick;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && onRightClick != nullptr) onRightClick();
            else juce::ComboBox::mouseDown (e);
        }
    };

    struct Knob
    {
        LearnableSlider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    CortexProcessor& processor;
    CanvasComponent canvas;

    AnalysisFrame frame {};
    bool hasFrame = false, dragHover = false;
    float kickFlash = 0.0f, snareFlash = 0.0f, hatFlash = 0.0f;
    juce::String statusText, errorText;
    juce::File statsLog;          // set by the CORTEX_STATS_LOG environment variable (diagnostics)
    int statsTicks = 0;

    std::array<Knob, 6> detectorKnobs;
    std::array<Knob, 8> macroKnobs;
    juce::ComboBox sourceBox, lookBox;
    LearnableCombo sceneBox;
    juce::Label sourceLabel, sceneLearnLabel;
    juce::TextButton clearCoverButton { "x" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment;
    juce::RangedAudioParameter* sceneParameter = nullptr;
    juce::RangedAudioParameter* lookParameter = nullptr;
    std::optional<Palette> coverShown;

    juce::Rectangle<int> titleArea, stripArea, lightsArea, bandsArea, macroArea, coverArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CortexEditor)
};

} // namespace cortex

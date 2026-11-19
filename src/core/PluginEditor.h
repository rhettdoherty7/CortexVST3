#pragma once

#include "PluginProcessor.h"

#include <presets/PresetManager.h>
#include <ui/CanvasComponent.h>
#include <ui/Inspector.h>
#include <ui/ModuleRail.h>
#include <ui/PresetBrowser.h>
#include <ui/Theme.h>

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <memory>

namespace cortex
{

/** The Cortex window (section 10): top bar with presets and A/B, module rail on the left, canvas in
    the middle (or the preset browser while it is open), inspector on the right, analysis meters,
    tempo and macros along the bottom. Tab hides everything but the picture. */
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
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override   { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    /** A small drawn icon button: previous, next, close, expand. */
    struct IconButton final : juce::Button
    {
        enum class Icon { previous, next, close, expand };
        explicit IconButton (Icon i) : juce::Button ({}), icon (i) {}
        void paintButton (juce::Graphics&, bool over, bool down) override;
        Icon icon;
    };

    struct Knob
    {
        ui::CortexSlider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void select (int widget);
    void stepPreset (int direction);
    void saveCurrent();
    void setBrowserOpen (bool open);
    void setCleanView (bool clean);
    void showLearnMenu (int slot, juce::Component& anchor);
    void loadCoverArt (const juce::File&);
    void updateTopBar();

    void drawHitLight (juce::Graphics&, juce::Rectangle<float>, const juce::String& label, juce::Colour, float envelope, float flash) const;
    void drawBands (juce::Graphics&, juce::Rectangle<float>) const;

    CortexProcessor& processor;
    juce::SharedResourcePointer<ui::CortexLookAndFeel> lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };
    PresetManager presets;

    CanvasComponent canvas;
    ui::ModuleRail rail;
    ui::Inspector inspector;
    ui::PresetBrowser browser;

    // Top bar
    IconButton previousButton { IconButton::Icon::previous }, nextButton { IconButton::Icon::next };
    juce::TextButton presetButton, saveButton { "Save" }, copyButton;
    ui::Chip slotA { "A" }, slotB { "B" };
    IconButton clearCoverButton { IconButton::Icon::close }, cleanViewButton { IconButton::Icon::expand };
    juce::TextButton showInterfaceButton { "Show interface  (Tab)" };

    std::array<Knob, 8> macros;

    AnalysisFrame frame {};
    float kickFlash = 0.0f, snareFlash = 0.0f, hatFlash = 0.0f;
    juce::String statusText, errorText;
    juce::File statsLog;          // CORTEX_STATS_LOG=<file> (diagnostics)
    int statsTicks = 0, ticks = 0;
    int selectedWidget = -1;
    bool browserOpen = false, cleanView = false, dragHover = false;
    std::optional<Palette> coverShown;

    juce::Rectangle<int> topArea, wordmarkArea, coverArea, statusArea, bottomArea, lightsArea, bandsArea, tempoArea, macroArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CortexEditor)
};

} // namespace cortex

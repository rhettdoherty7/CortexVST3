#pragma once

#include "Theme.h"

#include <core/PluginProcessor.h>

#include <functional>
#include <memory>
#include <vector>

namespace cortex::ui
{

/** Right rail (section 10): everything about the scene that is not a host parameter.
    Widget: the widget list, the selected widget's settings, and scene settings.
    Effects: the selected widget's own layer stack.
    Motion: modulation routes (section 6).
    Audio: detector and output parameters.

    Rows are rebuilt only when the scene's shape changes (widgets, modules, layers, routes); on every
    other refresh the controls just follow the scene, so a value moved by automation or another
    control shows up without the panel jumping. */
class Inspector final : public juce::Component
{
public:
    explicit Inspector (CortexProcessor&);
    ~Inspector() override;

    std::function<int()> getSelectedWidget;
    std::function<void (int)> selectWidget;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Tab { widget = 0, effects, motion, audio };

    struct Row
    {
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<juce::Component> label;
        int height = 30;
        std::function<void (const ScenePreset&)> update;
    };

    void rebuild (const ScenePreset&);
    juce::String shapeKey (const ScenePreset&) const;
    void layoutRows();

    void buildWidgetTab (const ScenePreset&);
    void buildEffectsTab (const ScenePreset&);
    void buildMotionTab (const ScenePreset&);
    void buildAudioTab();

    // Row builders. Getters read the scene; setters change it through processor.editScene.
    void section (const juce::String& title);
    void note (const juce::String& text, int height = 34);
    void slider (const juce::String& name, double min, double max,
                 std::function<double (const ScenePreset&)> get, std::function<void (ScenePreset&, double)> set,
                 std::function<juce::String (double)> format = {});
    void combo (const juce::String& name, const juce::StringArray& items,
                std::function<int (const ScenePreset&)> get, std::function<void (ScenePreset&, int)> set);
    void buttons (std::vector<std::pair<juce::String, std::function<void()>>>);
    void widgetRow (int index, const WidgetSpec&);
    void paramSlider (const juce::String& name, const char* paramId);
    void paramCombo (const juce::String& name, const char* paramId);
    void paramToggle (const juce::String& name, const char* paramId);

    CortexProcessor& processor;
    std::array<std::unique_ptr<Chip>, 4> tabs;
    Tab tab = Tab::widget;

    juce::Viewport viewport;
    juce::Component content;
    std::vector<Row> rows;
    juce::String builtFor;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
};

} // namespace cortex::ui

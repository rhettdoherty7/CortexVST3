#pragma once

#include "Theme.h"

#include <core/PluginProcessor.h>
#include <presets/PresetManager.h>

#include <functional>
#include <memory>
#include <vector>

namespace cortex::ui
{

/** The preset browser (section 10): search, tag chips, favourites, factory and user presets, and
    the Save As form. Shown in place of the canvas while open. */
class PresetBrowser final : public juce::Component,
                            private juce::ListBoxModel
{
public:
    PresetBrowser (CortexProcessor&, PresetManager&);

    std::function<void()> onClose;

    void showList();
    void showSaveForm();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    void rebuildTags();
    void applyFilter();
    void load (int row, bool close);
    void save();

    CortexProcessor& processor;
    PresetManager& presets;

    juce::TextEditor search;
    Chip favouritesOnly { "Favourites" };
    juce::TextButton closeButton { "Close" }, saveAsButton { "Save as..." };
    std::vector<std::unique_ptr<Chip>> tagChips;
    juce::ListBox list;
    std::vector<int> visible;   // indices into presets.entries()

    // Save form
    bool saving = false;
    juce::TextEditor nameField, tagsField;
    juce::TextButton confirmSave { "Save" }, cancelSave { "Cancel" };
    juce::String message;
};

} // namespace cortex::ui

#pragma once

#include "Theme.h"

#include <core/PluginProcessor.h>

#include <functional>
#include <vector>

namespace cortex::ui
{

/** Left rail (section 10): module thumbnails, then the Looks.
    Clicking a module swaps the selected widget's module, or adds a widget when none is selected;
    right-click offers both. Clicking a Look applies it to the whole scene. */
class ModuleRail final : public juce::Component
{
public:
    explicit ModuleRail (CortexProcessor&);

    std::function<int()> getSelectedWidget;
    std::function<void (int)> selectWidget;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Called by the editor's timer: repaints when the Look changes. */
    void refresh();

private:
    void useModule (int moduleIndex, bool addNew);

    CortexProcessor& processor;
    std::vector<juce::Image> thumbnails;
    std::vector<juce::Rectangle<int>> tiles, lookRows;
    juce::Rectangle<int> modulesHeader, looksHeader;
    int hoverTile = -1, hoverLook = -1, shownLook = -1;
};

} // namespace cortex::ui

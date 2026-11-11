#include "ModuleRail.h"

#include <CortexUiData.h>

#include <engine/Layers.h>
#include <engine/ModuleInfo.h>

namespace cortex::ui
{

namespace
{
    juce::Image embeddedImage (const juce::String& fileName)
    {
        for (int i = 0; i < CortexUiData::namedResourceListSize; ++i)
        {
            const auto* resource = CortexUiData::namedResourceList[i];
            if (fileName == CortexUiData::getNamedResourceOriginalFilename (resource))
            {
                int size = 0;
                const auto* data = CortexUiData::getNamedResource (resource, size);
                return juce::ImageCache::getFromMemory (data, size);
            }
        }
        return {};
    }

    constexpr int lookRowHeight = 22;
}

ModuleRail::ModuleRail (CortexProcessor& p) : processor (p)
{
    for (const auto& info : moduleInfos())
        thumbnails.push_back (embeddedImage (juce::String (info.id) + ".png"));
}

void ModuleRail::resized()
{
    auto area = getLocalBounds().reduced (10, 8);

    modulesHeader = area.removeFromTop (20);
    area.removeFromTop (4);

    tiles.clear();
    const int columns = 2, gap = 6;
    const int tileW = (area.getWidth() - gap) / columns;
    const int tileH = tileW * 9 / 16 + 18;   // picture plus name
    const int count = (int) moduleInfos().size();
    for (int i = 0; i < count; ++i)
        tiles.push_back ({ area.getX() + (i % columns) * (tileW + gap), area.getY() + (i / columns) * (tileH + gap), tileW, tileH });
    area.removeFromTop (((count + columns - 1) / columns) * (tileH + gap) + 6);

    looksHeader = area.removeFromTop (20);
    area.removeFromTop (2);
    lookRows.clear();
    for (int i = 0; i < numFactoryLooks() && area.getHeight() >= lookRowHeight; ++i)
        lookRows.push_back (area.removeFromTop (lookRowHeight));
}

void ModuleRail::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    drawSectionLabel (g, "Modules", modulesHeader.toFloat());
    drawSectionLabel (g, "Looks", looksHeader.toFloat());

    const auto& infos = moduleInfos();
    const auto scene = processor.scene();
    const int selected = getSelectedWidget != nullptr ? getSelectedWidget() : -1;
    const std::string selectedModule = selected >= 0 && selected < (int) scene.scene.widgets.size()
                                     ? scene.scene.widgets[(size_t) selected].module : std::string();

    for (size_t i = 0; i < tiles.size(); ++i)
    {
        const auto tile = tiles[i].toFloat();
        auto picture = tile.withHeight (tile.getHeight() - 18.0f);
        const bool isCurrent = infos[i].id == selectedModule;
        const bool hot = (int) i == hoverTile;

        juce::Path clip;
        clip.addRoundedRectangle (picture, 6.0f);
        g.saveState();
        g.reduceClipRegion (clip);
        if (thumbnails[i].isValid())
            g.drawImage (thumbnails[i], picture, juce::RectanglePlacement::fillDestination);
        else
            g.fillAll (col::raised);
        if (! hot && ! isCurrent)
        {
            g.setColour (col::background.withAlpha (0.25f));
            g.fillRect (picture);
        }
        g.restoreState();

        g.setColour (isCurrent ? col::accent : (hot ? col::hairlineHot : col::hairline));
        g.drawRoundedRectangle (picture.reduced (0.5f), 6.0f, isCurrent ? 1.5f : 1.0f);

        g.setColour (isCurrent || hot ? col::text : col::dim);
        g.setFont (Fonts::text (11.5f));
        g.drawText (infos[i].name, tile.withTop (picture.getBottom() + 2.0f), juce::Justification::centredLeft, true);
    }

    for (size_t i = 0; i < lookRows.size(); ++i)
    {
        const auto row = lookRows[i].toFloat();
        const bool current = (int) i == scene.look;
        if (current || (int) i == hoverLook)
        {
            g.setColour (current ? col::accent.withAlpha (0.18f) : col::raised);
            g.fillRoundedRectangle (row, 5.0f);
        }
        if (current)
        {
            g.setColour (col::accent);
            g.fillRoundedRectangle (row.withWidth (3.0f).reduced (0.0f, 5.0f), 1.5f);
        }
        g.setColour (current ? col::text : col::dim);
        g.setFont (Fonts::text (12.5f));
        g.drawText (i == 0 ? juce::String ("No look") : juce::String (factoryLook ((int) i).name),
                    row.reduced (10.0f, 0.0f), juce::Justification::centredLeft);
    }
}

void ModuleRail::refresh()
{
    const int look = processor.scene().look;
    if (look != shownLook)
    {
        shownLook = look;
        repaint();
    }
}

void ModuleRail::mouseMove (const juce::MouseEvent& e)
{
    int tile = -1, look = -1;
    for (size_t i = 0; i < tiles.size(); ++i)
        if (tiles[i].contains (e.getPosition())) tile = (int) i;
    for (size_t i = 0; i < lookRows.size(); ++i)
        if (lookRows[i].contains (e.getPosition())) look = (int) i;

    if (tile != hoverTile || look != hoverLook)
    {
        hoverTile = tile;
        hoverLook = look;
        setMouseCursor (tile >= 0 || look >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ModuleRail::mouseExit (const juce::MouseEvent&)
{
    hoverTile = hoverLook = -1;
    repaint();
}

void ModuleRail::mouseDown (const juce::MouseEvent& e)
{
    for (size_t i = 0; i < lookRows.size(); ++i)
    {
        if (lookRows[i].contains (e.getPosition()))
        {
            processor.setLook ((int) i);
            repaint();
            return;
        }
    }

    for (size_t i = 0; i < tiles.size(); ++i)
    {
        if (! tiles[i].contains (e.getPosition()))
            continue;

        const int index = (int) i;
        const bool hasSelection = getSelectedWidget != nullptr && getSelectedWidget() >= 0;
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addSectionHeader (moduleInfos()[i].name);
            menu.addItem (1, "Replace the selected widget", hasSelection);
            menu.addItem (2, "Add as a new widget");
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                                [this, index] (int choice) { if (choice > 0) useModule (index, choice == 2); });
        }
        else
        {
            useModule (index, ! hasSelection);
        }
        return;
    }
}

void ModuleRail::useModule (int moduleIndex, bool addNew)
{
    const auto& info = moduleInfos()[(size_t) moduleIndex];
    const int selected = getSelectedWidget != nullptr ? getSelectedWidget() : -1;
    int newSelection = selected;

    processor.editScene ([&] (ScenePreset& p)
    {
        auto& widgets = p.scene.widgets;
        if (addNew || selected < 0 || selected >= (int) widgets.size())
        {
            auto w = defaultWidget (info.id, p.scene.backgroundPalette);
            if (! widgets.empty())
                w.blend = BlendMode::add;   // layered on top of what is there
            widgets.push_back (w);
            newSelection = (int) widgets.size() - 1;
        }
        else
        {
            // Keep where it sits and how it blends; start the new module from its defaults.
            const auto old = widgets[(size_t) selected];
            auto w = defaultWidget (info.id, old.settings.palette);
            w.x = old.x; w.y = old.y; w.w = old.w; w.h = old.h;
            w.blend = old.blend;
            w.opacity = old.opacity;
            w.layers = old.layers;
            widgets[(size_t) selected] = w;
        }
    });

    if (selectWidget != nullptr)
        selectWidget (newSelection);
    repaint();
}

} // namespace cortex::ui

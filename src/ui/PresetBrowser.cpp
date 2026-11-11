#include "PresetBrowser.h"

namespace cortex::ui
{

namespace
{
    constexpr int rowHeight = 40;

    void styleField (juce::TextEditor& t, const juce::String& hint)
    {
        t.setFont (Fonts::text (13.5f));
        t.setTextToShowWhenEmpty (hint, col::faint);
        t.setIndents (10, 7);
        t.setColour (juce::TextEditor::backgroundColourId, col::raised);
        t.setColour (juce::TextEditor::outlineColourId, col::hairline);
        t.setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
    }

    juce::Path star (juce::Rectangle<float> r)
    {
        juce::Path p;
        p.addStar (r.getCentre(), 5, r.getWidth() * 0.22f, r.getWidth() * 0.5f, -juce::MathConstants<float>::halfPi);
        return p;
    }
}

PresetBrowser::PresetBrowser (CortexProcessor& p, PresetManager& m) : processor (p), presets (m)
{
    styleField (search, "Search names and tags");
    search.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible (search);

    favouritesOnly.onClick = [this] { applyFilter(); };
    addAndMakeVisible (favouritesOnly);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);
    saveAsButton.onClick = [this] { showSaveForm(); };
    addAndMakeVisible (saveAsButton);

    list.setModel (this);
    list.setRowHeight (rowHeight);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);

    styleField (nameField, "Preset name");
    styleField (tagsField, "Tags, separated by commas (e.g. Dark, Trap, Flowfield)");
    nameField.onReturnKey = [this] { save(); };
    confirmSave.getProperties().set ("primary", true);
    confirmSave.onClick = [this] { save(); };
    cancelSave.onClick = [this] { showList(); };
    addChildComponent (nameField);
    addChildComponent (tagsField);
    addChildComponent (confirmSave);
    addChildComponent (cancelSave);

    setWantsKeyboardFocus (true);
    rebuildTags();
    applyFilter();
}

void PresetBrowser::showList()
{
    saving = false;
    message.clear();
    presets.refresh();
    rebuildTags();
    applyFilter();
    for (auto* c : { (juce::Component*) &nameField, (juce::Component*) &tagsField, (juce::Component*) &confirmSave, (juce::Component*) &cancelSave })
        c->setVisible (false);
    for (auto* c : { (juce::Component*) &search, (juce::Component*) &favouritesOnly, (juce::Component*) &list, (juce::Component*) &saveAsButton })
        c->setVisible (true);
    for (auto& chip : tagChips)
        chip->setVisible (true);
    resized();
    repaint();
    search.grabKeyboardFocus();
}

void PresetBrowser::showSaveForm()
{
    saving = true;
    message.clear();
    const auto current = processor.scene();
    nameField.setText (current.scene.name, false);
    juce::StringArray tags;
    for (const auto& t : current.scene.tags)
        tags.add (t);
    tagsField.setText (tags.joinIntoString (", "), false);

    for (auto* c : { (juce::Component*) &search, (juce::Component*) &favouritesOnly, (juce::Component*) &list, (juce::Component*) &saveAsButton })
        c->setVisible (false);
    for (auto& chip : tagChips)
        chip->setVisible (false);
    for (auto* c : { (juce::Component*) &nameField, (juce::Component*) &tagsField, (juce::Component*) &confirmSave, (juce::Component*) &cancelSave })
        c->setVisible (true);
    resized();
    repaint();
    nameField.grabKeyboardFocus();
    nameField.selectAll();
}

void PresetBrowser::rebuildTags()
{
    juce::StringArray active;
    for (auto& chip : tagChips)
        if (chip->getToggleState())
            active.add (chip->getButtonText());

    for (auto& chip : tagChips)
        removeChildComponent (chip.get());
    tagChips.clear();

    for (const auto& tag : presets.allTags())
    {
        auto chip = std::make_unique<Chip> (tag);
        chip->setToggleState (active.contains (tag), juce::dontSendNotification);
        chip->onClick = [this] { applyFilter(); };
        addAndMakeVisible (*chip);
        tagChips.push_back (std::move (chip));
    }
}

void PresetBrowser::applyFilter()
{
    juce::StringArray tags;
    for (auto& chip : tagChips)
        if (chip->getToggleState())
            tags.add (chip->getButtonText());

    visible = presets.filter (search.getText(), tags, favouritesOnly.getToggleState());
    list.updateContent();
    list.repaint();
}

void PresetBrowser::resized()
{
    auto area = getLocalBounds().reduced (20, 16);
    auto header = area.removeFromTop (28);
    closeButton.setBounds (header.removeFromRight (80));

    if (saving)
    {
        area.removeFromTop (24);
        auto form = area.removeFromTop (220).withSizeKeepingCentre (juce::jmin (460, area.getWidth()), 220);
        form.removeFromTop (24);
        nameField.setBounds (form.removeFromTop (36));
        form.removeFromTop (28);
        tagsField.setBounds (form.removeFromTop (36));
        form.removeFromTop (20);
        auto buttonsRow = form.removeFromTop (34);
        confirmSave.setBounds (buttonsRow.removeFromRight (110));
        buttonsRow.removeFromRight (8);
        cancelSave.setBounds (buttonsRow.removeFromRight (100));
        return;
    }

    header.removeFromRight (8);
    saveAsButton.setBounds (header.removeFromRight (100));
    area.removeFromTop (12);

    auto searchRow = area.removeFromTop (34);
    favouritesOnly.setBounds (searchRow.removeFromRight (110).reduced (0, 3));
    searchRow.removeFromRight (8);
    search.setBounds (searchRow);
    area.removeFromTop (10);

    // Tag chips flow onto as many lines as they need.
    int x = area.getX(), y = area.getY();
    for (auto& chip : tagChips)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (Fonts::textBold (11.5f), chip->getButtonText()) + 24;
        if (x + w > area.getRight())
        {
            x = area.getX();
            y += 30;
        }
        chip->setBounds (x, y, w, 24);
        x += w + 6;
    }
    area.setTop (y + (tagChips.empty() ? 0 : 34));

    list.setBounds (area);
}

void PresetBrowser::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    auto header = getLocalBounds().reduced (20, 16).removeFromTop (28).toFloat();
    g.setColour (col::text);
    g.setFont (Fonts::textBold (16.0f));
    g.drawText (saving ? "Save preset" : "Presets", header, juce::Justification::centredLeft);

    if (saving)
    {
        auto form = getLocalBounds().reduced (20, 16).withTrimmedTop (52);
        form = form.removeFromTop (220).withSizeKeepingCentre (juce::jmin (460, form.getWidth()), 220);
        g.setColour (col::dim);
        g.setFont (Fonts::text (12.0f));
        g.drawText ("Name", form.removeFromTop (22).toFloat(), juce::Justification::bottomLeft);
        form.removeFromTop (36 + 6);
        g.drawText ("Tags", form.removeFromTop (22).toFloat(), juce::Justification::bottomLeft);
        g.setColour (col::faint);
        g.drawText ("Saved to " + presets.folder().getFullPathName(),
                    getLocalBounds().reduced (20, 16).removeFromBottom (20).toFloat(), juce::Justification::centredLeft);
    }

    if (message.isNotEmpty())
    {
        g.setColour (col::warning);
        g.setFont (Fonts::text (12.5f));
        g.drawText (message, getLocalBounds().reduced (20, 16).removeFromBottom (44).removeFromTop (20).toFloat(),
                    juce::Justification::centredLeft);
    }
}

bool PresetBrowser::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (saving) showList();
        else if (onClose) onClose();
        return true;
    }
    return false;
}

// --- List ----------------------------------------------------------------------------------------

int PresetBrowser::getNumRows()
{
    return (int) visible.size();
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
{
    if (row < 0 || row >= (int) visible.size())
        return;

    const auto& e = presets.entries()[(size_t) visible[(size_t) row]];
    const bool current = processor.scene().scene.name == e.name.toStdString();
    auto area = juce::Rectangle<int> (width, height).toFloat().reduced (0.0f, 2.0f);

    if (current)
    {
        g.setColour (col::accent.withAlpha (0.16f));
        g.fillRoundedRectangle (area, 6.0f);
    }

    auto starArea = area.removeFromLeft (36.0f).withSizeKeepingCentre (16.0f, 16.0f);
    const auto starPath = star (starArea);
    if (e.favourite)
    {
        g.setColour (col::snare);
        g.fillPath (starPath);
    }
    else
    {
        g.setColour (col::faint);
        g.strokePath (starPath, juce::PathStrokeType (1.2f));
    }

    auto right = area.removeFromRight (90.0f);
    g.setColour (col::faint);
    g.setFont (Fonts::text (11.0f));
    g.drawText (e.factory ? "Factory" : "Yours", right.reduced (8.0f, 0.0f), juce::Justification::centredRight);

    g.setColour (current ? col::text : col::text.withAlpha (0.9f));
    g.setFont (Fonts::textBold (13.5f));
    g.drawText (e.name, area.removeFromTop (area.getHeight() * 0.55f), juce::Justification::bottomLeft);
    g.setColour (col::dim);
    g.setFont (Fonts::text (11.5f));
    g.drawText (e.tags.joinIntoString ("  /  "), area, juce::Justification::topLeft);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) visible.size())
        return;

    const auto entry = presets.entries()[(size_t) visible[(size_t) row]];

    if (e.x < 36)   // the star
    {
        presets.setFavourite (entry, ! entry.favourite);
        applyFilter();
        return;
    }

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (entry.name);
        menu.addItem (1, "Load");
        menu.addItem (2, entry.favourite ? "Remove from favourites" : "Add to favourites");
        if (! entry.factory)
        {
            juce::PopupMenu confirm;
            confirm.addItem (3, "Yes, move it to the Recycle Bin");
            menu.addSubMenu ("Delete preset", confirm);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, row, entry] (int choice)
        {
            if (choice == 1) load (row, false);
            if (choice == 2) { presets.setFavourite (entry, ! entry.favourite); applyFilter(); }
            if (choice == 3) { presets.remove (entry); rebuildTags(); resized(); applyFilter(); }
        });
        return;
    }

    load (row, false);   // a single click previews; the browser stays open
}

void PresetBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    load (row, true);
}

void PresetBrowser::load (int row, bool close)
{
    if (row < 0 || row >= (int) visible.size())
        return;

    juce::String error;
    if (auto preset = presets.load (presets.entries()[(size_t) visible[(size_t) row]], error))
    {
        processor.loadPreset (std::move (*preset));
        message.clear();
        list.repaint();
        if (close && onClose)
            onClose();
    }
    else
    {
        message = "Could not load: " + error;
    }
    repaint();
}

void PresetBrowser::save()
{
    const auto name = nameField.getText().trim();
    auto preset = processor.scene();
    preset.scene.tags.clear();
    for (const auto& t : juce::StringArray::fromTokens (tagsField.getText(), ",", ""))
        if (t.trim().isNotEmpty())
            preset.scene.tags.push_back (t.trim().toStdString());
    processor.editScene ([&preset] (ScenePreset& p) { p.scene.tags = preset.scene.tags; });

    juce::String error;
    if (presets.save (preset, name, error) < 0)
    {
        message = "Not saved: " + error;
        repaint();
        return;
    }

    processor.markSaved (name);
    showList();
    if (onClose)
        onClose();
}

} // namespace cortex::ui

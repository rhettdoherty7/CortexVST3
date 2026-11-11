#include "PluginEditor.h"

#include "Params.h"
#include "Version.h"

#include <engine/ModRouter.h>
#include <presets/CoverArtPalette.h>

namespace cortex
{

namespace col = ui::col;
using ui::Fonts;

namespace
{
    constexpr int topHeight = 40;
    constexpr int bottomHeight = 118;
    constexpr int railWidth = 204;
    constexpr int inspectorWidth = 304;

    bool isImage (const juce::File& f)   { return f.hasFileExtension ("png;jpg;jpeg"); }

    // Open Cortex windows in this process; the shared look and feel stays the default while any is open.
    int openEditors = 0;
}

// --- Icon button ---------------------------------------------------------------------------------

void CortexEditor::IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto area = getLocalBounds().toFloat();
    if (over || down)
    {
        g.setColour (col::raised.brighter (down ? 0.12f : 0.04f));
        g.fillRoundedRectangle (area, 6.0f);
    }

    const auto c = area.getCentre();
    juce::Path p;
    switch (icon)
    {
        case Icon::previous: p.startNewSubPath (c.x + 2.5f, c.y - 5.0f); p.lineTo (c.x - 2.5f, c.y); p.lineTo (c.x + 2.5f, c.y + 5.0f); break;
        case Icon::next:     p.startNewSubPath (c.x - 2.5f, c.y - 5.0f); p.lineTo (c.x + 2.5f, c.y); p.lineTo (c.x - 2.5f, c.y + 5.0f); break;
        case Icon::close:
            p.startNewSubPath (c.x - 4.0f, c.y - 4.0f); p.lineTo (c.x + 4.0f, c.y + 4.0f);
            p.startNewSubPath (c.x + 4.0f, c.y - 4.0f); p.lineTo (c.x - 4.0f, c.y + 4.0f);
            break;
        case Icon::expand:
            for (int sx : { -1, 1 })
                for (int sy : { -1, 1 })
                {
                    const float x = c.x + (float) sx * 6.0f, y = c.y + (float) sy * 5.0f;
                    p.startNewSubPath (x - (float) sx * 3.5f, y);
                    p.lineTo (x, y);
                    p.lineTo (x, y - (float) sy * 3.5f);
                }
            break;
    }
    g.setColour (over ? col::text : col::dim);
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// --- Construction --------------------------------------------------------------------------------

CortexEditor::CortexEditor (CortexProcessor& p)
    : AudioProcessorEditor (p), processor (p), canvas (p), rail (p), inspector (p), browser (p, presets)
{
    ++openEditors;
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel.get());   // menus and tooltips too
    setLookAndFeel (&lookAndFeel.get());

    addAndMakeVisible (canvas);
    addAndMakeVisible (rail);
    addAndMakeVisible (inspector);
    addChildComponent (browser);

    // Selection is shared by the canvas, the rail and the inspector.
    canvas.onSelectionChanged = [this] (int w) { select (w); };
    rail.getSelectedWidget = [this] { return selectedWidget; };
    rail.selectWidget = [this] (int w) { select (w); };
    inspector.getSelectedWidget = [this] { return selectedWidget; };
    inspector.selectWidget = [this] (int w) { select (w); };
    browser.onClose = [this] { setBrowserOpen (false); };

    // --- Top bar ---------------------------------------------------------------------------------
    previousButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    previousButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };

    presetButton.setTooltip ("Browse presets. Right-click for MIDI learn of scene select.");
    presetButton.onClick = [this] { setBrowserOpen (! browserOpen); };
    presetButton.addMouseListener (this, false);

    saveButton.setTooltip ("Save this preset (Save as... is in the browser)");
    saveButton.onClick = [this] { saveCurrent(); };

    for (auto* chip : { &slotA, &slotB })
    {
        chip->setRadioGroupId (2);
        chip->setTooltip ("A/B: two versions of the scene to compare");
    }
    slotA.onClick = [this] { processor.selectSlot (0); };
    slotB.onClick = [this] { processor.selectSlot (1); };
    copyButton.setTooltip ("Copy this version to the other slot");
    copyButton.onClick = [this] { processor.copyActiveToOther(); };

    clearCoverButton.setTooltip ("Clear the cover-art palette");
    clearCoverButton.onClick = [this] { processor.setCoverPalette (std::nullopt); };
    cleanViewButton.setTooltip ("Clean view: only the picture (Tab)");
    cleanViewButton.onClick = [this] { setCleanView (true); };
    showInterfaceButton.onClick = [this] { setCleanView (false); };

    for (juce::Component* c : std::initializer_list<juce::Component*> { &previousButton, &nextButton, &presetButton, &saveButton,
                                                                         &slotA, &slotB, &copyButton, &cleanViewButton })
        addAndMakeVisible (c);
    addChildComponent (clearCoverButton);
    addChildComponent (showInterfaceButton);

    // --- Macros (bottom strip), right-click for MIDI learn ----------------------------------------
    for (int i = 0; i < (int) macros.size(); ++i)
    {
        auto& k = macros[(size_t) i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 14);
        k.slider.setTooltip (juce::String ("Macro ") + juce::String (i + 1) + ": " + macroLabel (i)
                             + ". Double-click resets, Ctrl-drag for fine moves, right-click for MIDI learn.");
        k.slider.onRightClick = [this, i] { showLearnMenu (i, macros[(size_t) i].slider); };
        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, params::id::macro (i), k.slider);
        k.slider.setDoubleClickReturnValue (true, 0.0);
        k.label.setText (macroLabel (i), juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setFont (Fonts::text (11.0f));
        k.label.setColour (juce::Label::textColourId, col::dim);
        addAndMakeVisible (k.slider);
        addAndMakeVisible (k.label);
    }

    const auto logPath = juce::SystemStats::getEnvironmentVariable ("CORTEX_STATS_LOG", {});
    if (juce::File::isAbsolutePath (logPath))
        statsLog = juce::File (logPath);

    setWantsKeyboardFocus (true);
    setOpaque (true);
    setResizable (true, true);
    setResizeLimits (1080, 680, 2560, 1600);
    setSize (1320, 820);

    updateTopBar();
    processor.setAnalysisActive (true);
    startTimerHz (60);
}

CortexEditor::~CortexEditor()
{
    stopTimer();
    processor.cancelMidiLearn();
    processor.setAnalysisActive (false);   // editor closed -> analysis stops (section 13)
    setLookAndFeel (nullptr);
    if (--openEditors == 0)   // the last Cortex window: the shared look and feel is about to go
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

// --- Actions -------------------------------------------------------------------------------------

void CortexEditor::select (int widget)
{
    selectedWidget = widget;
    canvas.setSelectedWidget (widget);
    rail.repaint();
    inspector.refresh();
}

void CortexEditor::stepPreset (int direction)
{
    const auto& entries = presets.entries();
    if (entries.empty())
        return;

    const auto name = juce::String (processor.scene().scene.name);
    int index = -1;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].name == name)
            index = i;

    index = (index + direction + (int) entries.size()) % (int) entries.size();
    juce::String error;
    if (auto preset = presets.load (entries[(size_t) index], error))
    {
        processor.loadPreset (std::move (*preset));
        select (-1);
    }
    else
    {
        errorText = "Could not load: " + error;
    }
}

void CortexEditor::saveCurrent()
{
    // Save over a user preset of the same name; otherwise ask for a name.
    const auto current = processor.scene();
    for (const auto& e : presets.entries())
    {
        if (! e.factory && e.name == juce::String (current.scene.name))
        {
            juce::String error;
            if (presets.save (current, e.name, error) >= 0)
                processor.markSaved (e.name);
            else
                errorText = "Not saved: " + error;
            return;
        }
    }
    setBrowserOpen (true);
    browser.showSaveForm();
}

void CortexEditor::setBrowserOpen (bool open)
{
    browserOpen = open;
    browser.setVisible (open);
    canvas.setVisible (! open);   // the canvas is a native GL view: nothing can be drawn over it
    if (open)
        browser.showList();
    else
        grabKeyboardFocus();
}

void CortexEditor::setCleanView (bool clean)
{
    cleanView = clean;
    if (clean && browserOpen)
        setBrowserOpen (false);

    for (juce::Component* c : std::initializer_list<juce::Component*> { &rail, &inspector, &previousButton, &nextButton, &presetButton,
                                                                        &saveButton, &slotA, &slotB, &copyButton, &cleanViewButton })
        c->setVisible (! clean);
    for (auto& k : macros)
    {
        k.slider.setVisible (! clean);
        k.label.setVisible (! clean);
    }
    clearCoverButton.setVisible (! clean && coverShown.has_value());
    showInterfaceButton.setVisible (clean);
    resized();
    repaint();
    grabKeyboardFocus();
}

bool CortexEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::tabKey)
    {
        setCleanView (! cleanView);
        return true;
    }
    if (key == juce::KeyPress::escapeKey && cleanView)
    {
        setCleanView (false);
        return true;
    }
    return false;
}

void CortexEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent == &presetButton && e.mods.isPopupMenu())
        showLearnMenu (CortexProcessor::sceneLearnSlot, presetButton);
}

void CortexEditor::showLearnMenu (int slot, juce::Component& anchor)
{
    const int cc = processor.midiControllerFor (slot);
    const bool learning = processor.learningSlot() == slot;

    juce::PopupMenu menu;
    menu.addSectionHeader (slot == CortexProcessor::sceneLearnSlot ? juce::String ("Scene select") : juce::String (macroLabel (slot)));
    menu.addItem (1, learning ? "Cancel MIDI learn" : "MIDI learn: move a controller");
    menu.addItem (2, cc >= 0 ? "Forget CC " + juce::String (cc) : juce::String ("No controller assigned"), cc >= 0);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor), [this, slot, learning] (int choice)
    {
        if (choice == 1) { if (learning) processor.cancelMidiLearn(); else processor.startMidiLearn (slot); }
        if (choice == 2) processor.clearMidiController (slot);
    });
}

// --- Cover art -----------------------------------------------------------------------------------

bool CortexEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isImage (juce::File (f)))
            return true;
    return false;
}

void CortexEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    for (const auto& f : files)
        if (isImage (juce::File (f)))
        {
            loadCoverArt (juce::File (f));
            break;
        }
    repaint();
}

void CortexEditor::loadCoverArt (const juce::File& file)
{
    const auto image = juce::ImageFileFormat::loadFrom (file);
    if (! image.isValid())
    {
        errorText = "Could not read " + file.getFileName() + " as an image.";
        return;
    }

    const auto small = image.rescaled (96, 96, juce::Graphics::mediumResamplingQuality);
    std::vector<Rgb> pixels;
    pixels.reserve (96 * 96);
    for (int y = 0; y < small.getHeight(); ++y)
        for (int x = 0; x < small.getWidth(); ++x)
        {
            const auto c = small.getPixelAt (x, y);
            if (c.getAlpha() > 128)
                pixels.push_back ({ c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() });
        }

    processor.setCoverPalette (paletteFromPixels (pixels));
    errorText.clear();
}

// --- Timer ---------------------------------------------------------------------------------------

void CortexEditor::updateTopBar()
{
    const auto scene = processor.scene();
    const auto name = juce::String (scene.scene.name.empty() ? "Untitled" : scene.scene.name);
    const auto text = name + (processor.isModified() ? "  (edited)" : "");
    if (presetButton.getButtonText() != text)
        presetButton.setButtonText (text);

    slotA.setToggleState (processor.activeSlot() == 0, juce::dontSendNotification);
    slotB.setToggleState (processor.activeSlot() == 1, juce::dontSendNotification);
    const auto copyText = processor.activeSlot() == 0 ? "Copy to B" : "Copy to A";
    if (copyButton.getButtonText() != copyText)
        copyButton.setButtonText (copyText);

    const bool changed = scene.cover.has_value() != coverShown.has_value()
                      || (scene.cover && coverShown && scene.cover->srgbHex != coverShown->srgbHex);
    if (changed)
    {
        coverShown = scene.cover;
        clearCoverButton.setVisible (! cleanView && coverShown.has_value());
        repaint (coverArea);
    }

    // Keep the selection valid when widgets disappear.
    if (selectedWidget >= (int) scene.scene.widgets.size())
        select (-1);
}

void CortexEditor::timerCallback()
{
    bool kick = false, snare = false, hat = false, any = false;

    if (canvas.isRendering() && canvas.isVisible())
    {
        const auto snap = canvas.takeSnapshot();
        if (snap.gotFrame) { frame = snap.frame; any = true; }
        kick = snap.kickHit; snare = snap.snareHit; hat = snap.hatHit;
        if (snap.error.isNotEmpty())
            errorText = snap.error;

        statusText = juce::String (juce::roundToInt (snap.framesPerSecond)) + " fps   "
                   + juce::String (snap.stats.gpuMs, 1) + " ms";
        if (snap.stats.renderScale < 0.99f)
            statusText << "   " << juce::roundToInt (snap.stats.renderScale * 100.0f) << "% res";
        if (snap.stats.limiterGain < 0.99f)
            statusText << "   flash limit";

        if (statsLog != juce::File() && ++statsTicks >= 60)
        {
            statsTicks = 0;
            statsLog.appendText (juce::String (snap.framesPerSecond, 1) + " fps, " + juce::String (snap.stats.gpuMs, 3)
                                 + " ms GPU, scale " + juce::String (snap.stats.renderScale, 2) + juce::newLine);
        }
    }
    else
    {
        // Canvas hidden (browser open) or GL not running: keep the meters alive from here.
        processor.drainFrames ([&] (const AnalysisFrame& f)
        {
            kick = kick || f.kickHit; snare = snare || f.snareHit; hat = hat || f.hatHit;
            frame = f;
            any = true;
        });
        statusText = browserOpen ? juce::String() : juce::String ("visuals off");
    }

    const auto step = [] (float& flash, bool hit) { flash = hit ? 1.0f : flash * 0.8f; };
    step (kickFlash, kick);
    step (snareFlash, snare);
    step (hatFlash, hat);
    if (! any)
    {
        frame.kick *= 0.9f; frame.snare *= 0.9f; frame.hat *= 0.9f;
        for (auto& b : frame.bands) b *= 0.95f;
    }

    // MIDI learn state on the macro labels.
    const int learning = processor.learningSlot();
    for (int i = 0; i < (int) macros.size(); ++i)
    {
        const int cc = processor.midiControllerFor (i);
        const juce::String text = learning == i ? juce::String ("move a knob...")
                                : cc >= 0 ? juce::String (macroLabel (i)) + " CC" + juce::String (cc)
                                          : juce::String (macroLabel (i));
        auto& label = macros[(size_t) i].label;
        if (label.getText() != text)
        {
            label.setText (text, juce::dontSendNotification);
            label.setColour (juce::Label::textColourId, learning == i ? col::accent : col::dim);
        }
    }

    // Slower work four times a second... and the panels fifteen times a second.
    if (++ticks % 4 == 0)
    {
        updateTopBar();
        rail.refresh();
        if (! cleanView)
            inspector.refresh();
    }

    repaint (topArea);
    repaint (lightsArea);
    repaint (bandsArea);
    repaint (tempoArea);
}

// --- Layout --------------------------------------------------------------------------------------

void CortexEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    if (cleanView)
    {
        topArea = area.removeFromTop (26);
        showInterfaceButton.setBounds (topArea.removeFromRight (170));
        area.removeFromTop (6);
        canvas.setBounds (area);
        browser.setBounds (area);
        return;
    }

    topArea = area.removeFromTop (topHeight);
    {
        auto bar = topArea.reduced (0, 4);
        wordmarkArea = bar.removeFromLeft (124);
        previousButton.setBounds (bar.removeFromLeft (28));
        presetButton.setBounds (bar.removeFromLeft (260));
        nextButton.setBounds (bar.removeFromLeft (28));
        bar.removeFromLeft (10);
        slotA.setBounds (bar.removeFromLeft (32).reduced (0, 2));
        bar.removeFromLeft (4);
        slotB.setBounds (bar.removeFromLeft (32).reduced (0, 2));
        bar.removeFromLeft (6);
        copyButton.setBounds (bar.removeFromLeft (90));
        bar.removeFromLeft (6);
        saveButton.setBounds (bar.removeFromLeft (64));

        cleanViewButton.setBounds (bar.removeFromRight (32));
        bar.removeFromRight (8);
        coverArea = bar.removeFromRight (150);
        clearCoverButton.setBounds (coverArea.removeFromRight (24));
        bar.removeFromRight (12);
        statusArea = bar;
    }
    area.removeFromTop (8);

    bottomArea = area.removeFromBottom (bottomHeight);
    area.removeFromBottom (8);

    rail.setBounds (area.removeFromLeft (railWidth));
    area.removeFromLeft (8);
    inspector.setBounds (area.removeFromRight (inspectorWidth));
    area.removeFromRight (8);
    canvas.setBounds (area);
    browser.setBounds (area);

    auto strip = bottomArea;
    lightsArea = strip.removeFromLeft (210);
    strip.removeFromLeft (8);
    bandsArea = strip.removeFromLeft (220);
    strip.removeFromLeft (8);
    tempoArea = strip.removeFromLeft (130);
    strip.removeFromLeft (8);
    macroArea = strip;

    auto knobs = macroArea.reduced (10, 8);
    knobs.removeFromTop (16);
    const int w = knobs.getWidth() / (int) macros.size();
    for (auto& k : macros)
    {
        auto cell = knobs.removeFromLeft (w).reduced (3, 0);
        k.label.setBounds (cell.removeFromBottom (14));
        k.slider.setBounds (cell);
    }
}

// --- Painting ------------------------------------------------------------------------------------

void CortexEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::background);

    if (cleanView)
        return;

    // Wordmark
    g.setColour (col::text);
    g.setFont (Fonts::display (17.0f).withExtraKerningFactor (0.06f));
    g.drawText ("CORTEX", wordmarkArea.toFloat(), juce::Justification::centredLeft);

    // Status: frame rate and GPU time, or an error.
    g.setFont (Fonts::mono (11.5f));
    if (errorText.isNotEmpty())
    {
        g.setColour (col::warning);
        g.drawFittedText (errorText.replaceCharacters ("\n", " "), statusArea, juce::Justification::centredRight, 1);
    }
    else
    {
        g.setColour (col::faint);
        g.drawText (statusText, statusArea.toFloat(), juce::Justification::centredRight);
    }

    // Cover art: background swatch first, then the five colours; or a hint.
    auto swatches = coverArea.toFloat().reduced (0.0f, 8.0f);
    if (coverShown)
    {
        const float w = swatches.getWidth() / 6.0f;
        for (int i = 0; i < 6; ++i)
        {
            const int index = i == 0 ? 5 : i - 1;
            g.setColour (juce::Colour ((juce::uint32) (0xff000000u | coverShown->srgbHex[(size_t) index])));
            g.fillRoundedRectangle (swatches.removeFromLeft (w).reduced (1.0f), 3.0f);
        }
    }
    else
    {
        g.setColour (col::faint);
        g.setFont (Fonts::text (11.0f));
        g.drawText ("drop cover art for colours", swatches, juce::Justification::centredRight);
    }

    // Bottom strip
    const auto lights = lightsArea.toFloat();
    ui::drawPanel (g, lights, "Onsets");
    auto row = lights.reduced (10.0f, 24.0f).withTrimmedTop (2.0f);
    const float cell = row.getWidth() / 3.0f;
    drawHitLight (g, row.removeFromLeft (cell), "KICK", col::kick, frame.kick, kickFlash);
    drawHitLight (g, row.removeFromLeft (cell), "SNARE", col::snare, frame.snare, snareFlash);
    drawHitLight (g, row, "HAT", col::hat, frame.hat, hatFlash);

    ui::drawPanel (g, bandsArea.toFloat(), "Bands");
    drawBands (g, bandsArea.toFloat().reduced (10.0f, 26.0f).withTrimmedBottom (-12.0f));

    ui::drawPanel (g, tempoArea.toFloat(), "Tempo");
    {
        auto t = tempoArea.toFloat().reduced (12.0f, 24.0f);
        g.setColour (col::text);
        g.setFont (Fonts::monoBold (28.0f));
        g.drawText (juce::String (frame.bpm, 1), t.removeFromTop (40.0f), juce::Justification::centredLeft);
        g.setColour (col::faint);
        g.setFont (Fonts::text (11.0f));
        g.drawText (frame.ppq >= 0.0 ? "BPM  from the host" : "BPM", t.removeFromTop (16.0f), juce::Justification::centredLeft);

        // Beat dots: four per bar, the current one lit.
        auto dots = t.removeFromTop (14.0f);
        const int beat = juce::jlimit (0, 3, (int) (frame.barPhase * 4.0f));
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (i == beat ? col::accent : col::hairlineHot);
            g.fillEllipse (dots.removeFromLeft (14.0f).withSizeKeepingCentre (7.0f, 7.0f));
        }
    }

    ui::drawPanel (g, macroArea.toFloat(), "Macros");
}

void CortexEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragHover)
        return;

    // The canvas is a native view and stays on top; the hint is drawn over everything else.
    g.setColour (col::accent.withAlpha (0.16f));
    g.fillRect (getLocalBounds());
    g.setColour (col::accent);
    g.drawRect (getLocalBounds(), 2);
    g.setColour (col::text);
    g.setFont (Fonts::textBold (15.0f));
    g.drawText ("Drop cover art to take its colours", topArea, juce::Justification::centred);
}

void CortexEditor::drawHitLight (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& label,
                                 juce::Colour colour, float envelope, float flash) const
{
    const float size = juce::jmin (area.getHeight() - 18.0f, area.getWidth() - 12.0f, 50.0f);
    const auto circle = juce::Rectangle<float> (size, size).withCentre ({ area.getCentreX(), area.getY() + size * 0.5f + 2.0f });

    g.setColour (colour.withAlpha (0.12f));
    g.fillEllipse (circle);
    g.setColour (colour.withAlpha (juce::jlimit (0.0f, 1.0f, envelope)));
    g.fillEllipse (circle.reduced (size * 0.5f * (1.0f - juce::jlimit (0.0f, 1.0f, 0.35f + 0.65f * envelope))));
    if (flash > 0.02f)
    {
        g.setColour (juce::Colours::white.withAlpha (flash));
        g.drawEllipse (circle.reduced (1.0f), 2.0f);
    }

    g.setColour (col::dim);
    g.setFont (Fonts::textBold (10.0f));
    g.drawText (label, area.withTop (circle.getBottom() + 2.0f), juce::Justification::centredTop);
}

void CortexEditor::drawBands (juce::Graphics& g, juce::Rectangle<float> area) const
{
    static const char* const names[8] = { "SUB", "BASS", "LO", "MID", "HI", "PRES", "BRIL", "AIR" };
    const float cellWidth = area.getWidth() / 8.0f;
    auto bars = area.withTrimmedBottom (14.0f);

    g.setFont (Fonts::text (9.0f));
    for (int i = 0; i < 8; ++i)
    {
        const auto column = juce::Rectangle<float> (area.getX() + (float) i * cellWidth, bars.getY(), cellWidth, bars.getHeight()).reduced (3.0f, 0.0f);
        g.setColour (col::hairline);
        g.fillRoundedRectangle (column, 3.0f);
        g.setColour (col::accent);
        g.fillRoundedRectangle (column.withTop (column.getBottom() - column.getHeight() * frame.bands[i]), 3.0f);
        g.setColour (col::faint);
        g.drawText (names[i], juce::Rectangle<float> (column.getX() - 3.0f, bars.getBottom() + 2.0f, cellWidth, 12.0f), juce::Justification::centred);
    }
}

} // namespace cortex

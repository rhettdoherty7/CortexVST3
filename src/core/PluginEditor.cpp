#include "PluginEditor.h"

#include "Params.h"
#include "Version.h"

#include <engine/Layers.h>
#include <engine/ModRouter.h>
#include <engine/Scene.h>
#include <presets/CoverArtPalette.h>

namespace cortex
{

namespace
{
    // Palette from CLAUDE.md section 10.
    const juce::Colour background  { 0xff0b0b0f };
    const juce::Colour panel       { 0xff14141a };
    const juce::Colour hairline    { 0xff26262e };
    const juce::Colour text        { 0xffe8e8ee };
    const juce::Colour dimText     { 0xff8a8a96 };
    const juce::Colour accent      { 0xff7c5cff };
    const juce::Colour warning     { 0xffff8a5c };
    const juce::Colour kickColour  { 0xffff5c7a };
    const juce::Colour snareColour { 0xffffc857 };
    const juce::Colour hatColour   { 0xff5ce1e6 };

    constexpr int stripHeight = 132;
    constexpr int macroRailWidth = 196;

    juce::Font uiFont (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    void drawPanel (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (hairline);
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (dimText);
        g.setFont (uiFont (11.0f));
        g.drawText (title, area.reduced (10.0f, 6.0f).removeFromTop (14.0f), juce::Justification::topLeft);
    }

    void styleKnob (juce::Slider& s, juce::Colour fill)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        s.setNumDecimalPlacesToDisplay (2);
        s.setColour (juce::Slider::rotarySliderFillColourId, fill);
        s.setColour (juce::Slider::rotarySliderOutlineColourId, hairline);
        s.setColour (juce::Slider::thumbColourId, text);
        s.setColour (juce::Slider::textBoxTextColourId, text);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    void styleLabel (juce::Label& l, const juce::String& textToShow)
    {
        l.setText (textToShow, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (uiFont (11.0f));
        l.setColour (juce::Label::textColourId, dimText);
    }

    bool isImage (const juce::File& f)
    {
        return f.hasFileExtension ("png;jpg;jpeg");
    }
}

CortexEditor::CortexEditor (CortexProcessor& p)
    : AudioProcessorEditor (p), processor (p), canvas (p)
{
    addAndMakeVisible (canvas);

    // --- Detector knobs (bottom strip) -----------------------------------------------------------
    struct KnobSpec { const char* id; const char* name; juce::Colour colour; };
    const KnobSpec specs[] = {
        { params::id::kickSensitivity,  "Kick sens",     kickColour },
        { params::id::kickRelease,      "Kick release",  kickColour },
        { params::id::snareSensitivity, "Snare sens",    snareColour },
        { params::id::snareRelease,     "Snare release", snareColour },
        { params::id::hatSensitivity,   "Hat sens",      hatColour },
        { params::id::hatRelease,       "Hat release",   hatColour },
    };

    for (size_t i = 0; i < detectorKnobs.size(); ++i)
    {
        auto& k = detectorKnobs[i];
        styleKnob (k.slider, specs[i].colour);
        styleLabel (k.label, specs[i].name);
        addAndMakeVisible (k.slider);
        addAndMakeVisible (k.label);
        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, specs[i].id, k.slider);
    }

    // --- Macro knobs (right rail), right-click for MIDI learn -------------------------------------
    for (int i = 0; i < (int) macroKnobs.size(); ++i)
    {
        auto& k = macroKnobs[(size_t) i];
        styleKnob (k.slider, accent);
        k.slider.setTooltip (juce::String ("Macro ") + juce::String (i + 1) + ": " + macroLabel (i) + ". Right-click for MIDI learn.");
        styleLabel (k.label, macroLabel (i));
        addAndMakeVisible (k.slider);
        addAndMakeVisible (k.label);
        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, params::id::macro (i), k.slider);
        k.slider.onRightClick = [this, i] { showLearnMenu (i, macroKnobs[(size_t) i].slider); };
    }

    // --- Pickers ---------------------------------------------------------------------------------
    for (juce::ComboBox* box : { &sourceBox, &lookBox, static_cast<juce::ComboBox*> (&sceneBox) })
    {
        box->setColour (juce::ComboBox::backgroundColourId, panel);
        box->setColour (juce::ComboBox::outlineColourId, hairline);
        box->setColour (juce::ComboBox::textColourId, text);
        box->setColour (juce::ComboBox::arrowColourId, dimText);
        addAndMakeVisible (*box);
    }

    sourceBox.addItemList ({ "Full Mix", "Kick", "Snare", "Hats", "Bass", "Vocal", "Synth" }, 1);
    sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, params::id::sourceType, sourceBox);
    sourceLabel.setText ("Source type", juce::dontSendNotification);
    sourceLabel.setFont (uiFont (11.0f));
    sourceLabel.setColour (juce::Label::textColourId, dimText);
    addAndMakeVisible (sourceLabel);

    // Scene and Look are integer parameters; only factory entries are listed.
    sceneParameter = processor.apvts.getParameter (params::id::sceneSelect);
    for (int i = 0; i < numFactoryScenes(); ++i)
        sceneBox.addItem (factoryScene (i).name, i + 1);
    sceneBox.setTooltip ("Scene. Right-click for MIDI learn.");
    sceneBox.onRightClick = [this] { showLearnMenu (CortexProcessor::sceneLearnSlot, sceneBox); };

    lookParameter = processor.apvts.getParameter (params::id::lookSelect);
    for (int i = 0; i < numFactoryLooks(); ++i)
        lookBox.addItem (i == 0 ? juce::String ("No look") : juce::String (factoryLook (i).name), i + 1);
    lookBox.setTooltip ("Look: a one-click stack of post effects.");

    const auto bindPicker = [] (juce::ComboBox& box, juce::RangedAudioParameter* param, int idOffset)
    {
        box.onChange = [&box, param, idOffset]
        {
            if (param != nullptr && box.getSelectedId() > 0)
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->convertTo0to1 ((float) (box.getSelectedId() - idOffset)));
                param->endChangeGesture();
            }
        };
    };
    bindPicker (sceneBox, sceneParameter, 0);   // scene parameter is 1-based, like the item ids
    bindPicker (lookBox, lookParameter, 1);     // look parameter is 0-based

    sceneLearnLabel.setFont (uiFont (10.0f));
    sceneLearnLabel.setColour (juce::Label::textColourId, dimText);
    sceneLearnLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (sceneLearnLabel);

    clearCoverButton.setTooltip ("Clear the cover-art palette");
    clearCoverButton.setColour (juce::TextButton::buttonColourId, panel);
    clearCoverButton.setColour (juce::TextButton::textColourOffId, dimText);
    clearCoverButton.onClick = [this] { processor.setCoverPalette (std::nullopt); };
    addChildComponent (clearCoverButton);

    // Diagnostics: CORTEX_STATS_LOG=<file> appends the frame rate and GPU time once a second.
    const auto logPath = juce::SystemStats::getEnvironmentVariable ("CORTEX_STATS_LOG", {});
    if (juce::File::isAbsolutePath (logPath))
        statsLog = juce::File (logPath);

    setOpaque (true);
    setResizable (true, true);
    setResizeLimits (960, 580, 2400, 1600);
    setSize (1200, 740);

    processor.setAnalysisActive (true);
    startTimerHz (60);
}

CortexEditor::~CortexEditor()
{
    stopTimer();
    processor.cancelMidiLearn();
    processor.setAnalysisActive (false);   // editor closed -> analysis stops (section 13)
}

// --- MIDI learn ----------------------------------------------------------------------------------

void CortexEditor::showLearnMenu (int slot, juce::Component& anchor)
{
    const int cc = processor.midiControllerFor (slot);
    const bool learning = processor.learningSlot() == slot;

    juce::PopupMenu menu;
    menu.addSectionHeader (cc >= 0 ? "MIDI: CC " + juce::String (cc) : juce::String ("MIDI: not assigned"));
    menu.addItem (1, learning ? "Cancel MIDI learn" : "MIDI learn (move a controller)");
    menu.addItem (2, "Clear MIDI assignment", cc >= 0);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor),
                        [this, slot, learning] (int choice)
                        {
                            if (choice == 1)
                            {
                                if (learning) processor.cancelMidiLearn();
                                else          processor.startMidiLearn (slot);
                            }
                            else if (choice == 2)
                            {
                                processor.clearMidiController (slot);
                            }
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
    {
        if (isImage (juce::File (f)))
        {
            loadCoverArt (juce::File (f));
            break;
        }
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

    // A 96 x 96 sample is plenty for six clusters.
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

void CortexEditor::timerCallback()
{
    bool kick = false, snare = false, hat = false, any = false;

    if (canvas.isRendering())
    {
        const auto snap = canvas.takeSnapshot();
        if (snap.gotFrame)
        {
            frame = snap.frame;
            any = true;
        }
        kick = snap.kickHit;
        snare = snap.snareHit;
        hat = snap.hatHit;
        if (snap.error.isNotEmpty())
            errorText = snap.error;

        statusText = juce::String (juce::roundToInt (snap.framesPerSecond)) + " fps  |  "
                   + juce::String (snap.stats.gpuMs, 2) + " ms GPU";
        if (snap.stats.renderScale < 0.99f)
            statusText << "  |  " << juce::roundToInt (snap.stats.renderScale * 100.0f) << "% res";
        if (snap.stats.limiterGain < 0.99f)
            statusText << "  |  flash limiter active";

        if (statsLog != juce::File() && ++statsTicks >= 60)
        {
            statsTicks = 0;
            statsLog.appendText (juce::String (snap.framesPerSecond, 1) + " fps, " + juce::String (snap.stats.gpuMs, 3)
                                 + " ms GPU, scale " + juce::String (snap.stats.renderScale, 2) + juce::newLine);
        }
    }
    else
    {
        processor.drainFrames ([&] (const AnalysisFrame& f)
        {
            kick = kick || f.kickHit;
            snare = snare || f.snareHit;
            hat = hat || f.hatHit;
            frame = f;
            any = true;
        });
        const auto snap = canvas.takeSnapshot();
        if (snap.error.isNotEmpty())
            errorText = snap.error;
        statusText = "visuals off";
    }

    hasFrame = hasFrame || any;

    const auto step = [] (float& flash, bool hit) { flash = hit ? 1.0f : flash * 0.8f; };
    step (kickFlash, kick);
    step (snareFlash, snare);
    step (hatFlash, hat);

    if (! any)
    {
        // Transport stopped in some hosts means no audio callbacks: let the meters fall.
        frame.kick *= 0.9f;
        frame.snare *= 0.9f;
        frame.hat *= 0.9f;
        for (auto& b : frame.bands)
            b *= 0.95f;
    }

    // Keep pickers in step with automation and MIDI.
    if (sceneParameter != nullptr)
    {
        const int id = juce::roundToInt (sceneParameter->convertFrom0to1 (sceneParameter->getValue()));
        if (sceneBox.getSelectedId() != id)
            sceneBox.setSelectedId (id, juce::dontSendNotification);
    }
    if (lookParameter != nullptr)
    {
        const int id = juce::jlimit (0, numFactoryLooks() - 1, juce::roundToInt (lookParameter->convertFrom0to1 (lookParameter->getValue()))) + 1;
        if (lookBox.getSelectedId() != id)
            lookBox.setSelectedId (id, juce::dontSendNotification);
    }

    // MIDI learn state on the labels.
    const int learning = processor.learningSlot();
    for (int i = 0; i < (int) macroKnobs.size(); ++i)
    {
        const int cc = processor.midiControllerFor (i);
        const juce::String label = learning == i ? juce::String ("move a controller...")
                                 : cc >= 0       ? juce::String (macroLabel (i)) + "  CC" + juce::String (cc)
                                                 : juce::String (macroLabel (i));
        auto& l = macroKnobs[(size_t) i].label;
        if (l.getText() != label)
        {
            l.setText (label, juce::dontSendNotification);
            l.setColour (juce::Label::textColourId, learning == i ? accent : dimText);
        }
    }
    {
        const int cc = processor.midiControllerFor (CortexProcessor::sceneLearnSlot);
        const juce::String label = learning == CortexProcessor::sceneLearnSlot ? juce::String ("move a controller...")
                                 : cc >= 0 ? "scene: CC" + juce::String (cc) : juce::String();
        if (sceneLearnLabel.getText() != label)
            sceneLearnLabel.setText (label, juce::dontSendNotification);
    }

    // Cover palette swatches.
    const auto cover = processor.coverPalette();
    const bool changed = cover.has_value() != coverShown.has_value() || (cover && coverShown && cover->srgbHex != coverShown->srgbHex);
    if (changed)
    {
        coverShown = cover;
        clearCoverButton.setVisible (cover.has_value());
        repaint (coverArea);
    }

    repaint (titleArea);
    repaint (lightsArea);
    repaint (bandsArea);
}

// --- Layout and painting -------------------------------------------------------------------------

void CortexEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    titleArea = area.removeFromTop (28);
    {
        auto title = titleArea;
        sceneBox.setBounds (title.removeFromRight (180).reduced (0, 2));
        title.removeFromRight (8);
        lookBox.setBounds (title.removeFromRight (150).reduced (0, 2));
        title.removeFromRight (12);
        coverArea = title.removeFromRight (150);
        clearCoverButton.setBounds (coverArea.withLeft (coverArea.getRight() - 22).reduced (0, 4));
    }
    area.removeFromTop (8);

    stripArea = area.removeFromBottom (stripHeight);
    area.removeFromBottom (8);

    macroArea = area.removeFromRight (macroRailWidth);
    area.removeFromRight (8);
    canvas.setBounds (area);

    {
        auto rail = macroArea.reduced (8, 0);
        rail.removeFromTop (24);
        sceneLearnLabel.setBounds (rail.removeFromBottom (14));
        const int rowHeight = rail.getHeight() / 4;
        for (int i = 0; i < (int) macroKnobs.size(); ++i)
        {
            auto cell = juce::Rectangle<int> (rail.getX() + (i % 2) * rail.getWidth() / 2, rail.getY() + (i / 2) * rowHeight,
                                              rail.getWidth() / 2, rowHeight).reduced (2, 2);
            auto& k = macroKnobs[(size_t) i];
            k.label.setBounds (cell.removeFromTop (14));
            k.slider.setBounds (cell);
        }
    }

    auto strip = stripArea;
    lightsArea = strip.removeFromLeft (230);
    strip.removeFromLeft (8);
    bandsArea = strip.removeFromLeft (250);
    strip.removeFromLeft (8);

    auto controls = strip;
    auto sourceColumn = controls.removeFromLeft (160).reduced (10, 24);
    sourceLabel.setBounds (sourceColumn.removeFromTop (16));
    sourceColumn.removeFromTop (4);
    sourceBox.setBounds (sourceColumn.removeFromTop (26));

    controls.removeFromTop (18);
    const int knobWidth = controls.getWidth() / (int) detectorKnobs.size();
    for (auto& k : detectorKnobs)
    {
        auto cell = controls.removeFromLeft (knobWidth).reduced (2, 4);
        k.label.setBounds (cell.removeFromTop (14));
        k.slider.setBounds (cell);
    }
}

void CortexEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    // Title bar
    auto title = titleArea.toFloat();
    g.setColour (text);
    g.setFont (uiFont (15.0f, true));
    g.drawText ("CORTEX", title.removeFromLeft (90.0f), juce::Justification::centredLeft);
    g.setColour (dimText);
    g.setFont (uiFont (11.0f));
    g.drawText (juce::String ("v") + versionString, title.removeFromLeft (60.0f), juce::Justification::centredLeft);
    title.setRight ((float) coverArea.getX() - 12.0f);
    g.drawText (statusText + "    " + juce::String (frame.bpm, 1) + " BPM", title, juce::Justification::centredRight);

    // Cover art: the six swatches, or a hint.
    auto swatches = coverArea.toFloat().withTrimmedRight (26.0f).reduced (0.0f, 6.0f);
    if (coverShown)
    {
        const float w = swatches.getWidth() / 6.0f;
        for (int i = 0; i < 6; ++i)
        {
            const int index = i == 0 ? 5 : i - 1;   // background first, then dark to bright
            g.setColour (juce::Colour ((juce::uint32) (0xff000000u | coverShown->srgbHex[(size_t) index])));
            g.fillRoundedRectangle (swatches.removeFromLeft (w).reduced (1.0f), 3.0f);
        }
    }
    else
    {
        g.setColour (dimText.withAlpha (0.7f));
        g.setFont (uiFont (10.5f));
        g.drawText ("drop cover art for colours", swatches, juce::Justification::centredRight);
    }

    // Right rail
    drawPanel (g, macroArea.toFloat(), "MACROS");

    // Bottom strip
    const auto lights = lightsArea.toFloat();
    drawPanel (g, lights, "ONSETS");
    auto row = lights.reduced (10.0f, 22.0f);
    const float cell = row.getWidth() / 3.0f;
    drawHitLight (g, row.removeFromLeft (cell), "KICK", kickColour, frame.kick, kickFlash);
    drawHitLight (g, row.removeFromLeft (cell), "SNARE", snareColour, frame.snare, snareFlash);
    drawHitLight (g, row, "HAT", hatColour, frame.hat, hatFlash);

    drawPanel (g, bandsArea.toFloat(), "BANDS");
    drawBands (g, bandsArea.toFloat().reduced (10.0f, 24.0f));

    drawPanel (g, stripArea.toFloat().withLeft ((float) bandsArea.getRight() + 8.0f), "DETECTORS");
}

void CortexEditor::paintOverChildren (juce::Graphics& g)
{
    if (dragHover)
    {
        g.setColour (accent.withAlpha (0.18f));
        g.fillRect (getLocalBounds());
        g.setColour (accent);
        g.drawRect (getLocalBounds(), 2);
        g.setColour (text);
        g.setFont (uiFont (16.0f, true));
        g.drawText ("Drop cover art to take its colours", getLocalBounds(), juce::Justification::centred);
    }

    if (errorText.isNotEmpty())
    {
        auto box = canvas.getBounds().toFloat().reduced (24.0f).removeFromBottom (90.0f);
        g.setColour (panel.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 8.0f);
        g.setColour (warning);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
        g.drawFittedText (errorText, box.reduced (12.0f).toNearestInt(), juce::Justification::topLeft, 6);
    }
}

void CortexEditor::drawHitLight (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& label,
                                 juce::Colour colour, float envelope, float flash) const
{
    const float size = juce::jmin (area.getHeight() - 20.0f, area.getWidth() - 12.0f, 56.0f);
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

    g.setColour (dimText);
    g.setFont (uiFont (10.0f));
    g.drawText (label, area.withTop (circle.getBottom() + 2.0f), juce::Justification::centredTop);
}

void CortexEditor::drawBands (juce::Graphics& g, juce::Rectangle<float> area) const
{
    static const char* const names[8] = { "SUB", "BASS", "LO-M", "MID", "HI-M", "PRES", "BRIL", "AIR" };
    const float cellWidth = area.getWidth() / 8.0f;
    auto bars = area.withTrimmedBottom (14.0f);

    g.setFont (uiFont (9.0f));
    for (int i = 0; i < 8; ++i)
    {
        const auto column = juce::Rectangle<float> (area.getX() + (float) i * cellWidth, bars.getY(), cellWidth, bars.getHeight()).reduced (3.0f, 0.0f);
        g.setColour (hairline);
        g.fillRoundedRectangle (column, 3.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (column.withTop (column.getBottom() - column.getHeight() * frame.bands[i]), 3.0f);
        g.setColour (dimText);
        g.drawText (names[i], juce::Rectangle<float> (column.getX() - 3.0f, bars.getBottom() + 2.0f, cellWidth, 12.0f), juce::Justification::centred);
    }
}

} // namespace cortex

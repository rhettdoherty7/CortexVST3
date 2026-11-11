#include "PluginEditor.h"

#include "Params.h"
#include "Version.h"

#include <engine/Scene.h>

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
}

CortexEditor::CortexEditor (CortexProcessor& p)
    : AudioProcessorEditor (p), processor (p), canvas (p)
{
    addAndMakeVisible (canvas);

    struct KnobSpec { const char* id; const char* name; juce::Colour colour; };
    const KnobSpec specs[] = {
        { params::id::kickSensitivity,  "Kick sens",     kickColour },
        { params::id::kickRelease,      "Kick release",  kickColour },
        { params::id::snareSensitivity, "Snare sens",    snareColour },
        { params::id::snareRelease,     "Snare release", snareColour },
        { params::id::hatSensitivity,   "Hat sens",      hatColour },
        { params::id::hatRelease,       "Hat release",   hatColour },
    };

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        k.slider.setNumDecimalPlacesToDisplay (2);
        k.slider.setColour (juce::Slider::rotarySliderFillColourId, specs[i].colour);
        k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, hairline);
        k.slider.setColour (juce::Slider::thumbColourId, text);
        k.slider.setColour (juce::Slider::textBoxTextColourId, text);
        k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (k.slider);

        k.label.setText (specs[i].name, juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setFont (uiFont (11.0f));
        k.label.setColour (juce::Label::textColourId, dimText);
        addAndMakeVisible (k.label);

        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, specs[i].id, k.slider);
    }

    for (auto* box : { &sourceBox, &sceneBox })
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

    // Scene select is an integer parameter (1..64); only the factory scenes are listed.
    sceneParameter = processor.apvts.getParameter (params::id::sceneSelect);
    for (int i = 0; i < numFactoryScenes(); ++i)
        sceneBox.addItem (factoryScene (i).name, i + 1);
    sceneBox.onChange = [this]
    {
        const int id = sceneBox.getSelectedId();
        if (id > 0 && sceneParameter != nullptr)
        {
            sceneParameter->beginChangeGesture();
            sceneParameter->setValueNotifyingHost (sceneParameter->convertTo0to1 ((float) id));
            sceneParameter->endChangeGesture();
        }
    };

    setOpaque (true);
    setResizable (true, true);
    setResizeLimits (900, 560, 2400, 1600);
    setSize (1100, 700);

    // Diagnostics: CORTEX_STATS_LOG=<file> appends the frame rate and GPU time once a second.
    const auto logPath = juce::SystemStats::getEnvironmentVariable ("CORTEX_STATS_LOG", {});
    if (juce::File::isAbsolutePath (logPath))
        statsLog = juce::File (logPath);

    processor.setAnalysisActive (true);
    startTimerHz (60);
}

CortexEditor::~CortexEditor()
{
    stopTimer();
    processor.setAnalysisActive (false);   // editor closed -> analysis stops (section 13)
}

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
        errorText = snap.error;

        statusText = juce::String (juce::roundToInt (snap.framesPerSecond)) + " fps  |  "
                   + juce::String (snap.stats.gpuMs, 2) + " ms GPU";

        if (statsLog != juce::File() && ++statsTicks >= 60)
        {
            statsTicks = 0;
            statsLog.appendText (juce::String (snap.framesPerSecond, 1) + " fps, " + juce::String (snap.stats.gpuMs, 3)
                                 + " ms GPU, scale " + juce::String (snap.stats.renderScale, 2) + juce::newLine);
        }

        if (snap.stats.renderScale < 0.99f)
            statusText << "  |  " << juce::roundToInt (snap.stats.renderScale * 100.0f) << "% res";
        if (snap.stats.limiterGain < 0.99f)
            statusText << "  |  flash limiter active";
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
        errorText = canvas.takeSnapshot().error;
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

    if (sceneParameter != nullptr)
    {
        const int id = juce::roundToInt (sceneParameter->convertFrom0to1 (sceneParameter->getValue()));
        if (sceneBox.getSelectedId() != id)
            sceneBox.setSelectedId (id, juce::dontSendNotification);
    }

    repaint (titleArea);
    repaint (lightsArea);
    repaint (bandsArea);
}

void CortexEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    titleArea = area.removeFromTop (28);
    {
        auto title = titleArea;
        sceneBox.setBounds (title.removeFromRight (200).reduced (0, 2));
    }
    area.removeFromTop (8);

    stripArea = area.removeFromBottom (stripHeight);
    area.removeFromBottom (8);
    canvas.setBounds (area);

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
    const int knobWidth = controls.getWidth() / (int) knobs.size();
    for (auto& k : knobs)
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
    title.removeFromRight (212.0f);
    g.drawText (statusText + "    " + juce::String (frame.bpm, 1) + " BPM", title, juce::Justification::centredRight);

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

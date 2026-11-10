#include "PluginEditor.h"

#include "Params.h"
#include "Version.h"

namespace cortex
{

namespace
{
    // Palette from CLAUDE.md section 10.
    const juce::Colour background { 0xff0b0b0f };
    const juce::Colour panel      { 0xff14141a };
    const juce::Colour hairline   { 0xff26262e };
    const juce::Colour text       { 0xffe8e8ee };
    const juce::Colour dimText    { 0xff8a8a96 };
    const juce::Colour accent     { 0xff7c5cff };
    const juce::Colour kickColour { 0xffff5c7a };
    const juce::Colour snareColour{ 0xffffc857 };
    const juce::Colour hatColour  { 0xff5ce1e6 };

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
    : AudioProcessorEditor (p), processor (p)
{
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

    sourceBox.addItemList ({ "Full Mix", "Kick", "Snare", "Hats", "Bass", "Vocal", "Synth" }, 1);
    sourceBox.setColour (juce::ComboBox::backgroundColourId, panel);
    sourceBox.setColour (juce::ComboBox::outlineColourId, hairline);
    sourceBox.setColour (juce::ComboBox::textColourId, text);
    addAndMakeVisible (sourceBox);
    sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, params::id::sourceType, sourceBox);

    sourceLabel.setText ("Source type", juce::dontSendNotification);
    sourceLabel.setFont (uiFont (11.0f));
    sourceLabel.setColour (juce::Label::textColourId, dimText);
    addAndMakeVisible (sourceLabel);

    setOpaque (true);
    setResizable (true, true);
    setResizeLimits (900, 560, 2400, 1600);
    setSize (900, 560);

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
    // Drain everything queued since the last tick; keep the newest frame but never lose a hit.
    AnalysisFrame next;
    bool kick = false, snare = false, hat = false, any = false;

    while (processor.popFrame (next))
    {
        kick  = kick  || next.kickHit;
        snare = snare || next.snareHit;
        hat   = hat   || next.hatHit;
        frame = next;
        any = true;
    }

    hasFrame = hasFrame || any;

    const auto step = [] (float& flash, bool hit) { flash = hit ? 1.0f : flash * 0.8f; };
    step (kickFlash, kick);
    step (snareFlash, snare);
    step (hatFlash, hat);

    if (! any)
    {
        // No audio callback is running (transport stopped in some hosts): let the meters fall.
        frame.kick *= 0.9f;
        frame.snare *= 0.9f;
        frame.hat *= 0.9f;
    }

    repaint();
}

void CortexEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    area.removeFromTop (28);   // title bar

    auto controls = area.removeFromBottom (118);
    area.removeFromBottom (10);

    auto top = area.removeFromTop (area.getHeight() * 3 / 5);
    area.removeFromTop (10);

    lightsArea = top.removeFromLeft (150);
    top.removeFromLeft (10);
    spectrumArea = top;

    bandsArea = area.removeFromLeft (area.getWidth() / 3);
    area.removeFromLeft (10);
    readoutArea = area.removeFromRight (190);
    area.removeFromRight (10);
    scopeArea = area;

    auto sourceColumn = controls.removeFromLeft (170).reduced (10, 22);
    sourceLabel.setBounds (sourceColumn.removeFromTop (16));
    sourceColumn.removeFromTop (4);
    sourceBox.setBounds (sourceColumn.removeFromTop (26));

    const int knobWidth = controls.getWidth() / (int) knobs.size();
    for (auto& k : knobs)
    {
        auto cell = controls.removeFromLeft (knobWidth).reduced (4, 6);
        k.label.setBounds (cell.removeFromTop (16));
        k.slider.setBounds (cell);
    }
}

void CortexEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto titleBar = getLocalBounds().reduced (12).removeFromTop (28).toFloat();
    g.setColour (text);
    g.setFont (uiFont (15.0f, true));
    g.drawText ("CORTEX", titleBar, juce::Justification::centredLeft);
    g.setColour (dimText);
    g.setFont (uiFont (11.0f));
    g.drawText (juce::String ("analysis debug view  v") + versionString, titleBar, juce::Justification::centredRight);

    const auto lights = lightsArea.toFloat();
    drawPanel (g, lights, "ONSETS");
    auto lightColumn = lights.reduced (10.0f, 24.0f);
    const float cell = lightColumn.getHeight() / 3.0f;
    const auto kickCell  = lightColumn.removeFromTop (cell);
    const auto snareCell = lightColumn.removeFromTop (cell);
    drawHitLight (g, kickCell, "KICK", kickColour, frame.kick, kickFlash);
    drawHitLight (g, snareCell, "SNARE", snareColour, frame.snare, snareFlash);
    drawHitLight (g, lightColumn, "HAT", hatColour, frame.hat, hatFlash);

    drawPanel (g, spectrumArea.toFloat(), "SPECTRUM  20 Hz - 20 kHz");
    drawSpectrum (g, spectrumArea.toFloat().reduced (10.0f, 24.0f));

    drawPanel (g, bandsArea.toFloat(), "BANDS");
    drawBands (g, bandsArea.toFloat().reduced (10.0f, 24.0f));

    drawPanel (g, scopeArea.toFloat(), "SCOPE");
    drawScope (g, scopeArea.toFloat().reduced (10.0f, 24.0f));

    drawPanel (g, readoutArea.toFloat(), "READOUT");
    drawReadout (g, readoutArea.toFloat().reduced (10.0f, 24.0f));

    auto controls = getLocalBounds().reduced (12).removeFromBottom (118).toFloat();
    drawPanel (g, controls, "DETECTORS");
}

void CortexEditor::drawHitLight (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& label,
                                 juce::Colour colour, float envelope, float flash) const
{
    const float size = juce::jmin (area.getHeight() - 22.0f, area.getWidth() - 20.0f, 64.0f);
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
    g.setFont (uiFont (11.0f));
    g.drawText (label, area.withTop (circle.getBottom() + 2.0f), juce::Justification::centredTop);
}

void CortexEditor::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const float barWidth = area.getWidth() / 128.0f;
    for (int i = 0; i < 128; ++i)
    {
        const float h = frame.spectrum[i] * area.getHeight();
        g.setColour (accent.withAlpha (0.35f + 0.65f * frame.spectrum[i]));
        g.fillRect (area.getX() + (float) i * barWidth, area.getBottom() - h, juce::jmax (1.0f, barWidth - 1.0f), h);
    }
}

void CortexEditor::drawBands (juce::Graphics& g, juce::Rectangle<float> area) const
{
    static const char* const names[8] = { "SUB", "BASS", "LO-MID", "MID", "HI-MID", "PRES", "BRIL", "AIR" };
    const float cellWidth = area.getWidth() / 8.0f;
    auto bars = area.withTrimmedBottom (16.0f);

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

void CortexEditor::drawScope (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto trace = [&] (const float* wave, juce::Colour colour)
    {
        juce::Path path;
        for (int i = 0; i < 256; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / 255.0f;
            const float y = area.getCentreY() - juce::jlimit (-1.0f, 1.0f, wave[i]) * area.getHeight() * 0.5f;
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (1.2f));
    };

    g.setColour (hairline);
    g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
    trace (frame.waveR, hatColour.withAlpha (0.6f));
    trace (frame.waveL, accent);
}

void CortexEditor::drawReadout (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto db = [] (float linear) { return juce::Decibels::gainToDecibels (linear, -100.0f); };

    juce::StringArray lines;
    lines.add ("BPM      " + juce::String (frame.bpm, 1));
    lines.add ("PPQ      " + (frame.ppq >= 0.0 ? juce::String (frame.ppq, 2) : juce::String ("stopped")));
    lines.add ("Beat     " + juce::String (frame.beatPhase, 2) + "   Bar " + juce::String (frame.barPhase, 2));
    lines.add ("RMS      " + juce::String (db (frame.rms), 1) + " dB");
    lines.add ("Peak     " + juce::String (db (frame.peak), 1) + " dB");
    lines.add ("LUFS-S   " + juce::String (frame.lufsShort, 1));
    lines.add ("Flux     " + juce::String (frame.flux, 2));
    lines.add ("Bright   " + juce::String (frame.centroid, 2));
    lines.add ("Width    " + juce::String (frame.stereoWidth, 2) + "   Corr " + juce::String (frame.correlation, 2));
    lines.add ("Pitch    " + (frame.pitchConf > 0.0f ? juce::String (frame.pitchHz, 1) + " Hz" : juce::String ("-")));
    if (! hasFrame)
        lines.add ("waiting for audio...");

    g.setColour (text);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
    const float lineHeight = juce::jmin (16.0f, area.getHeight() / (float) lines.size());
    for (const auto& line : lines)
        g.drawText (line, area.removeFromTop (lineHeight), juce::Justification::centredLeft);
}

} // namespace cortex

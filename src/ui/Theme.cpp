#include "Theme.h"

#include <CortexUiData.h>

#include <map>

namespace cortex::ui
{

namespace
{
    /** One typeface per embedded file for the whole process, found by its original file name. */
    juce::Typeface::Ptr typeface (const char* fileName)
    {
        static std::map<juce::String, juce::Typeface::Ptr> cache;
        auto& entry = cache[fileName];
        if (entry == nullptr)
        {
            for (int i = 0; i < CortexUiData::namedResourceListSize; ++i)
            {
                const auto* resource = CortexUiData::namedResourceList[i];
                if (juce::String (CortexUiData::getNamedResourceOriginalFilename (resource)) == fileName)
                {
                    int size = 0;
                    const auto* data = CortexUiData::getNamedResource (resource, size);
                    entry = juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
                    break;
                }
            }
            jassert (entry != nullptr);   // font missing from resources/fonts
        }
        return entry;
    }

    juce::Font font (const char* fileName, float px)
    {
        juce::FontOptions options (px);
        if (auto tf = typeface (fileName))
            options = options.withTypeface (tf);
        return juce::Font (options);
    }
}

juce::Font Fonts::text (float px)      { return font ("InstrumentSans-Regular.ttf", px); }
juce::Font Fonts::textBold (float px)  { return font ("InstrumentSans-SemiBold.ttf", px); }
juce::Font Fonts::mono (float px)      { return font ("JetBrainsMono-Regular.ttf", px); }
juce::Font Fonts::monoBold (float px)  { return font ("JetBrainsMono-SemiBold.ttf", px); }
juce::Font Fonts::display (float px)   { return font ("Unbounded-Bold.ttf", px); }

void drawPanel (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title)
{
    g.setColour (col::panel);
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (col::hairline);
    g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);
    if (title.isNotEmpty())
        drawSectionLabel (g, title, area.reduced (12.0f, 8.0f).removeFromTop (14.0f));
}

void drawSectionLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area)
{
    g.setColour (col::dim);
    g.setFont (Fonts::textBold (10.5f).withExtraKerningFactor (0.08f));
    g.drawText (text.toUpperCase(), area, juce::Justification::centredLeft);
}

// --- Look and feel -------------------------------------------------------------------------------

CortexLookAndFeel::CortexLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::background);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, col::accent.withAlpha (0.4f));
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::PopupMenu::backgroundColourId, col::panel);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::raised);
    setColour (juce::PopupMenu::highlightedTextColourId, col::text);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::highlightColourId, col::accent.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, col::text);
    setColour (juce::CaretComponent::caretColourId, col::accent);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, col::text);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, col::hairlineHot);
    setColour (juce::TooltipWindow::textColourId, col::text);
}

void CortexLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float start, float end, juce::Slider& s)
{
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto centre = area.getCentre();
    const float angle = start + pos * (end - start);
    const bool hot = s.isMouseOverOrDragging();
    const auto fill = s.findColour (juce::Slider::rotarySliderFillColourId);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, start, end, true);
    g.setColour (col::hairline);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, start, angle, true);
    g.setColour (hot ? fill.brighter (0.25f) : fill);
    g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto tip = centre.getPointOnCircumference (radius - 9.0f, angle);
    g.setColour (col::text.withAlpha (hot ? 1.0f : 0.8f));
    g.drawLine ({ centre.getPointOnCircumference (radius * 0.18f, angle), tip }, 2.0f);
}

void CortexLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                          juce::Slider::SliderStyle, juce::Slider& s)
{
    // A bar with the name on the left and the value on the right, drawn inside it.
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
    const bool hot = s.isMouseOverOrDragging();

    g.setColour (hot ? col::raised.brighter (0.05f) : col::raised);
    g.fillRoundedRectangle (area, 5.0f);

    const auto fillArea = area.withRight (juce::jmax (area.getX(), pos)).reduced (0.0f, 0.0f);
    g.setColour (s.findColour (juce::Slider::trackColourId).withAlpha (hot ? 0.55f : 0.4f));
    g.fillRoundedRectangle (fillArea, 5.0f);

    auto text = area.reduced (9.0f, 0.0f);
    g.setColour (col::dim);
    g.setFont (Fonts::text (12.0f));
    g.drawText (s.getName(), text, juce::Justification::centredLeft);
    g.setColour (col::text);
    g.setFont (Fonts::mono (11.5f));
    g.drawText (s.getTextFromValue (s.getValue()), text, juce::Justification::centredRight);
}

juce::Label* CortexLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (Fonts::mono (11.0f));
    l->setColour (juce::Label::textColourId, col::text);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

void CortexLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto area = juce::Rectangle<int> (w, h).toFloat().reduced (0.5f);
    const bool hot = box.isMouseOver (true) || box.isPopupActive();
    g.setColour (hot ? col::raised.brighter (0.04f) : col::raised);
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (box.hasKeyboardFocus (true) ? col::accent : (hot ? col::hairlineHot : col::hairline));
    g.drawRoundedRectangle (area, 6.0f, 1.0f);

    juce::Path arrow;
    const float cx = (float) w - 14.0f, cy = (float) h * 0.5f;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.0f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (col::dim);
    g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font CortexLookAndFeel::getComboBoxFont (juce::ComboBox&)   { return Fonts::text (13.0f); }

void CortexLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void CortexLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto area = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool primary = b.getProperties().getWithDefault ("primary", false);
    auto fill = primary ? col::accent : col::raised;
    if (down)      fill = fill.brighter (0.15f);
    else if (over) fill = fill.brighter (0.07f);
    if (! b.isEnabled()) fill = fill.withAlpha (0.4f);

    g.setColour (fill);
    g.fillRoundedRectangle (area, 6.0f);
    if (! primary)
    {
        g.setColour (over ? col::hairlineHot : col::hairline);
        g.drawRoundedRectangle (area, 6.0f, 1.0f);
    }
}

void CortexLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.isEnabled() ? col::text : col::faint);
    g.drawText (b.getButtonText(), b.getLocalBounds().reduced (6, 0), juce::Justification::centred);
}

juce::Font CortexLookAndFeel::getTextButtonFont (juce::TextButton&, int)   { return Fonts::textBold (12.5f); }

void CortexLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto area = b.getLocalBounds().toFloat();
    const auto box = area.removeFromLeft (30.0f).withSizeKeepingCentre (26.0f, 14.0f);
    g.setColour (b.getToggleState() ? col::accent : (over ? col::hairlineHot : col::hairline));
    g.fillRoundedRectangle (box, 7.0f);
    g.setColour (col::text);
    const float knobX = b.getToggleState() ? box.getRight() - 12.0f : box.getX() + 2.0f;
    g.fillEllipse (knobX, box.getY() + 2.0f, 10.0f, 10.0f);
    g.setColour (col::dim);
    g.setFont (Fonts::text (12.0f));
    g.drawText (b.getButtonText(), area.withTrimmedLeft (4.0f), juce::Justification::centredLeft);
}

void CortexLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::panel);
    g.setColour (col::hairlineHot);
    g.drawRect (0, 0, w, h, 1);
}

void CortexLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool separator, bool active,
                                           bool highlighted, bool ticked, bool hasSubMenu, const juce::String& text,
                                           const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (separator)
    {
        g.setColour (col::hairline);
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (4, 1);
    if (highlighted && active)
    {
        g.setColour (col::raised);
        g.fillRoundedRectangle (r.toFloat(), 5.0f);
    }

    auto textArea = r.reduced (10, 0);
    if (ticked)
    {
        g.setColour (col::accent);
        g.fillEllipse (textArea.removeFromLeft (8).withSizeKeepingCentre (6, 6).toFloat());
        textArea.removeFromLeft (6);
    }

    g.setColour (active ? col::text : col::faint);
    g.setFont (getPopupMenuFont());
    g.drawText (text, textArea, juce::Justification::centredLeft);

    if (hasSubMenu)
    {
        g.setColour (col::dim);
        g.drawText (">", textArea, juce::Justification::centredRight);
    }
}

void CortexLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    drawSectionLabel (g, name, area.reduced (14, 0).toFloat());
}

juce::Font CortexLookAndFeel::getPopupMenuFont()   { return Fonts::text (13.0f); }

void CortexLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto area = juce::Rectangle<int> (w, h).toFloat();
    g.setColour (col::raised);
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (col::hairlineHot);
    g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (col::text);
    g.setFont (Fonts::text (12.0f));
    g.drawFittedText (text, area.reduced (10.0f, 6.0f).toNearestInt(), juce::Justification::centredLeft, 4);
}

juce::Rectangle<int> CortexLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const int width = juce::jmin (300, juce::GlyphArrangement::getStringWidthInt (Fonts::text (12.0f), text) + 24);
    const int lines = 1 + juce::GlyphArrangement::getStringWidthInt (Fonts::text (12.0f), text) / 280;
    const int height = 14 + lines * 16;
    return juce::Rectangle<int> (pos.x + 12, pos.y + 18, width, height).constrainedWithin (parent);
}

void CortexLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (col::raised);
    g.fillRoundedRectangle (juce::Rectangle<int> (w, h).toFloat(), 6.0f);
}

void CortexLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) ? col::accent : col::hairline);
    g.drawRoundedRectangle (juce::Rectangle<int> (w, h).toFloat().reduced (0.5f), 6.0f, 1.0f);
}

void CortexLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                       int thumbStart, int thumbSize, bool over, bool down)
{
    if (thumbSize <= 0)
        return;
    const auto thumb = vertical ? juce::Rectangle<int> (x + 2, thumbStart, w - 4, thumbSize)
                                : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, h - 4);
    g.setColour (down ? col::dim : (over ? col::faint : col::hairlineHot));
    g.fillRoundedRectangle (thumb.toFloat(), 3.0f);
}

juce::Font CortexLookAndFeel::getLabelFont (juce::Label&)   { return Fonts::text (12.0f); }

// --- Controls ------------------------------------------------------------------------------------

CortexSlider::CortexSlider()
{
    setColour (juce::Slider::rotarySliderFillColourId, col::accent);
    setColour (juce::Slider::trackColourId, col::accent);
    setMouseDragSensitivity ((int) normalSensitivity);
    setVelocityBasedMode (false);
}

void CortexSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick != nullptr)
    {
        onRightClick();
        return;
    }
    // Ctrl-drag (Cmd on Mac) moves five times more slowly, for fine adjustment.
    setMouseDragSensitivity ((int) (e.mods.isCommandDown() ? normalSensitivity * 5.0 : normalSensitivity));
    juce::Slider::mouseDown (e);
}

void CortexSlider::mouseUp (const juce::MouseEvent& e)
{
    juce::Slider::mouseUp (e);
    setMouseDragSensitivity ((int) normalSensitivity);
}

Chip::Chip (const juce::String& text) : juce::Button (text)
{
    setClickingTogglesState (true);
}

void Chip::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto area = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    g.setColour (on ? col::accent.withAlpha (down ? 0.35f : 0.25f) : (over ? col::raised.brighter (0.05f) : col::raised));
    g.fillRoundedRectangle (area, area.getHeight() * 0.5f);
    g.setColour (on ? col::accent : (over ? col::hairlineHot : col::hairline));
    g.drawRoundedRectangle (area, area.getHeight() * 0.5f, 1.0f);
    g.setColour (on ? col::text : col::dim);
    g.setFont (Fonts::textBold (11.5f));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
}

} // namespace cortex::ui

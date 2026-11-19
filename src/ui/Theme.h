#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace cortex::ui
{

// Section 10: near-black, one electric accent, 1 px hairlines, 8 px radius, no gradients on chrome.
namespace col
{
    inline const juce::Colour background  { 0xff0b0b0f };
    inline const juce::Colour panel       { 0xff14141a };
    inline const juce::Colour raised      { 0xff1c1c24 };   // hovered rows, inputs
    inline const juce::Colour hairline    { 0xff26262e };
    inline const juce::Colour hairlineHot { 0xff3a3a48 };
    inline const juce::Colour text        { 0xffe8e8ee };
    inline const juce::Colour dim         { 0xff8a8a96 };
    inline const juce::Colour faint       { 0xff55555f };
    inline const juce::Colour accent      { 0xff7c5cff };
    inline const juce::Colour warning     { 0xffff8a5c };
    inline const juce::Colour kick        { 0xffff5c7a };
    inline const juce::Colour snare       { 0xffffc857 };
    inline const juce::Colour hat         { 0xff5ce1e6 };
}

/** The embedded typefaces (resources/fonts): Instrument Sans for text, JetBrains Mono for numbers,
    Unbounded for the wordmark. Sizes in logical pixels. */
struct Fonts
{
    static juce::Font text (float px);
    static juce::Font textBold (float px);
    static juce::Font mono (float px);
    static juce::Font monoBold (float px);
    static juce::Font display (float px);
};

void drawPanel (juce::Graphics&, juce::Rectangle<float>, const juce::String& title = {});
void drawSectionLabel (juce::Graphics&, const juce::String&, juce::Rectangle<float>);

class CortexLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    CortexLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int height) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;

    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool separator, bool active, bool highlighted,
                            bool ticked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSize() override   { return 6; }

    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;

    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override   { return 8; }

    juce::Font getLabelFont (juce::Label&) override;
};

/** A slider with the section 10 / North Star Q4 behaviour: double-click resets, Ctrl-drag is fine
    adjustment, right-click can be handed to a callback (MIDI learn), value always readable. */
class CortexSlider : public juce::Slider
{
public:
    CortexSlider();
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    double normalSensitivity = 250.0;
};

/** A rounded toggle "chip": tabs, tags, A/B. */
class Chip : public juce::Button
{
public:
    explicit Chip (const juce::String& text);
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

} // namespace cortex::ui

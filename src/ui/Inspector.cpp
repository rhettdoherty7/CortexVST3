#include "Inspector.h"

#include <core/Params.h>
#include <engine/Layers.h>
#include <engine/ModRouter.h>
#include <engine/ModuleInfo.h>

namespace cortex::ui
{

namespace
{
    constexpr int labelWidth = 92;

    juce::StringArray paletteNames()
    {
        juce::StringArray names;
        for (int i = 0; i < numFactoryPalettes(); ++i)
            names.add (factoryPalette (i).name);
        return names;
    }

    juce::String moduleName (const std::string& id)
    {
        const auto* info = findModuleInfo (id);
        return info != nullptr ? juce::String (info->name) : juce::String (id);
    }

    /** A section title. */
    struct SectionHeader final : juce::Component
    {
        explicit SectionHeader (juce::String t) : title (std::move (t)) {}
        void paint (juce::Graphics& g) override
        {
            g.setColour (col::hairline);
            g.fillRect (0, 2, getWidth(), 1);
            drawSectionLabel (g, title, getLocalBounds().withTrimmedTop (8).toFloat());
        }
        juce::String title;
    };

    /** One widget in the list: number, module, blend; click to select. */
    struct WidgetRow final : juce::Component
    {
        std::function<void()> onClick;
        juce::String number, name, detail;
        bool selected = false;

        void paint (juce::Graphics& g) override
        {
            const auto area = getLocalBounds().toFloat();
            if (selected || isMouseOver())
            {
                g.setColour (selected ? col::accent.withAlpha (0.18f) : col::raised);
                g.fillRoundedRectangle (area, 5.0f);
            }
            if (selected)
            {
                g.setColour (col::accent);
                g.fillRoundedRectangle (area.withWidth (3.0f).reduced (0.0f, 6.0f), 1.5f);
            }
            auto text = area.reduced (10.0f, 0.0f);
            g.setColour (col::faint);
            g.setFont (Fonts::mono (11.0f));
            g.drawText (number, text.removeFromLeft (18.0f), juce::Justification::centredLeft);
            g.setColour (selected ? col::text : col::dim);
            g.setFont (Fonts::text (12.5f));
            g.drawText (name, text, juce::Justification::centredLeft);
            g.setColour (col::faint);
            g.setFont (Fonts::text (11.0f));
            g.drawText (detail, text, juce::Justification::centredRight);
        }
        void mouseEnter (const juce::MouseEvent&) override   { repaint(); }
        void mouseExit (const juce::MouseEvent&) override    { repaint(); }
        void mouseDown (const juce::MouseEvent&) override    { if (onClick) onClick(); }
    };

    juce::Label* makeLabel (const juce::String& text)
    {
        auto* l = new juce::Label ({}, text);
        l->setFont (Fonts::text (12.0f));
        l->setColour (juce::Label::textColourId, col::dim);
        l->setJustificationType (juce::Justification::centredLeft);
        return l;
    }

    /** All the places a route can point at, labelled for a menu. */
    std::vector<std::pair<juce::String, ModTarget>> routeTargets (const SceneSpec& scene)
    {
        using K = ModTarget::Kind;
        std::vector<std::pair<juce::String, ModTarget>> out;
        const auto add = [&out] (const juce::String& label, K kind, int widget = -1, int index = 0)
        {
            ModTarget t;
            t.kind = kind;
            t.widget = widget;
            t.index = index;
            out.push_back ({ label, t });
        };

        add ("All widgets: Intensity", K::widgetIntensity);
        add ("All widgets: Speed", K::widgetSpeed);
        add ("All widgets: Scale", K::widgetScale);
        add ("All widgets: Color shift", K::widgetColorShift);

        for (size_t i = 0; i < scene.widgets.size(); ++i)
        {
            const auto prefix = juce::String ((int) i + 1) + " " + moduleName (scene.widgets[i].module) + ": ";
            add (prefix + "Intensity", K::widgetIntensity, (int) i);
            add (prefix + "Speed", K::widgetSpeed, (int) i);
            add (prefix + "Scale", K::widgetScale, (int) i);
            add (prefix + "Color shift", K::widgetColorShift, (int) i);
            if (const auto* info = findModuleInfo (scene.widgets[i].module))
                for (size_t k = 0; k < info->params.size(); ++k)
                    if (info->params[k].choices.empty())
                        add (prefix + info->params[k].name, K::widgetParam, (int) i, (int) k);
        }

        add ("Scene: Bloom", K::sceneBloom);
        add ("Scene: Exposure", K::sceneExposure);
        add ("Effects: Trails", K::fxFeedback);
        add ("Effects: Glitch", K::fxGlitch);
        add ("Effects: Aberration", K::fxChromatic);
        return out;
    }

    bool sameTarget (const ModTarget& a, const ModTarget& b)
    {
        return a.kind == b.kind && a.widget == b.widget && (a.kind != ModTarget::Kind::widgetParam || a.index == b.index);
    }
}

Inspector::Inspector (CortexProcessor& p) : processor (p)
{
    const char* names[] = { "Widget", "Effects", "Motion", "Audio" };
    for (int i = 0; i < 4; ++i)
    {
        tabs[(size_t) i] = std::make_unique<Chip> (names[i]);
        auto& chip = *tabs[(size_t) i];
        chip.setRadioGroupId (1);
        chip.setToggleState (i == 0, juce::dontSendNotification);
        chip.onClick = [this, i]
        {
            tab = (Tab) i;
            builtFor.clear();   // force a rebuild
            refresh();
        };
        addAndMakeVisible (chip);
    }

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

Inspector::~Inspector()
{
    sliderAttachments.clear();
    comboAttachments.clear();
    buttonAttachments.clear();
}

void Inspector::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
}

void Inspector::resized()
{
    auto area = getLocalBounds().reduced (10, 10);
    auto strip = area.removeFromTop (26);
    const int w = strip.getWidth() / 4;
    for (auto& t : tabs)
        t->setBounds (strip.removeFromLeft (w).reduced (2, 0));
    area.removeFromTop (8);
    viewport.setBounds (area);
    layoutRows();
}

// --- Refresh -------------------------------------------------------------------------------------

juce::String Inspector::shapeKey (const ScenePreset& p) const
{
    juce::String key;
    key << (int) tab << "|" << (getSelectedWidget != nullptr ? getSelectedWidget() : -1) << "|";
    for (const auto& w : p.scene.widgets)
    {
        key << w.module << ":";
        for (const auto& l : w.layers)
            key << (int) l.type << ",";
        key << ";";
    }
    key << "|" << (int) p.scene.routes.size();
    return key;
}

void Inspector::refresh()
{
    const auto scene = processor.scene();
    const auto key = shapeKey (scene);
    if (key != builtFor)
    {
        builtFor = key;
        rebuild (scene);
        return;
    }

    for (auto& row : rows)
        if (row.update != nullptr)
            row.update (scene);
}

void Inspector::rebuild (const ScenePreset& scene)
{
    const auto scroll = viewport.getViewPosition();
    sliderAttachments.clear();
    comboAttachments.clear();
    buttonAttachments.clear();
    rows.clear();
    content.removeAllChildren();

    switch (tab)
    {
        case Tab::widget:  buildWidgetTab (scene); break;
        case Tab::effects: buildEffectsTab (scene); break;
        case Tab::motion:  buildMotionTab (scene); break;
        case Tab::audio:   buildAudioTab(); break;
    }

    for (auto& row : rows)
    {
        if (row.label != nullptr) content.addAndMakeVisible (*row.label);
        content.addAndMakeVisible (*row.control);
        if (row.update != nullptr)
            row.update (scene);
    }
    layoutRows();
    viewport.setViewPosition (scroll);
}

void Inspector::layoutRows()
{
    const int width = juce::jmax (100, viewport.getWidth() - 10);
    int y = 0;
    for (auto& row : rows)
    {
        auto r = juce::Rectangle<int> (0, y, width, row.height).reduced (0, 2);
        if (row.label != nullptr)
            row.label->setBounds (r.removeFromLeft (labelWidth));
        row.control->setBounds (r);
        y += row.height;
    }
    content.setSize (width, y + 8);
}

// --- Row builders --------------------------------------------------------------------------------

void Inspector::section (const juce::String& title)
{
    Row row;
    row.control = std::make_unique<SectionHeader> (title);
    row.height = 30;
    rows.push_back (std::move (row));
}

void Inspector::note (const juce::String& text, int height)
{
    auto* label = makeLabel (text);
    label->setFont (Fonts::text (11.5f));
    label->setColour (juce::Label::textColourId, col::faint);
    label->setJustificationType (juce::Justification::topLeft);
    label->setMinimumHorizontalScale (1.0f);
    Row row;
    row.control.reset (label);
    row.height = height;
    rows.push_back (std::move (row));
}

void Inspector::slider (const juce::String& name, double min, double max,
                        std::function<double (const ScenePreset&)> get, std::function<void (ScenePreset&, double)> set,
                        std::function<juce::String (double)> format)
{
    auto s = std::make_unique<CortexSlider>();
    s->setName (name);
    s->setSliderStyle (juce::Slider::LinearBar);
    s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s->setRange (min, max, 0.0);
    s->setNumDecimalPlacesToDisplay (2);
    if (format != nullptr)
        s->textFromValueFunction = format;

    auto* raw = s.get();
    raw->onValueChange = [this, raw, set]
    {
        if (raw->isMouseButtonDown() || raw->hasKeyboardFocus (false))
            processor.editScene ([raw, &set] (ScenePreset& p) { set (p, raw->getValue()); });
    };
    Row row;
    row.control = std::move (s);
    row.height = 30;
    row.update = [raw, get] (const ScenePreset& p)
    {
        if (! raw->isMouseButtonDown())
            raw->setValue (get (p), juce::dontSendNotification);
    };
    rows.push_back (std::move (row));
}

void Inspector::combo (const juce::String& name, const juce::StringArray& items,
                       std::function<int (const ScenePreset&)> get, std::function<void (ScenePreset&, int)> set)
{
    auto c = std::make_unique<juce::ComboBox>();
    c->addItemList (items, 1);
    auto* raw = c.get();
    raw->onChange = [this, raw, set]
    {
        const int index = raw->getSelectedItemIndex();
        if (index >= 0)
            processor.editScene ([index, &set] (ScenePreset& p) { set (p, index); });
    };

    Row row;
    row.label.reset (makeLabel (name));
    row.control = std::move (c);
    row.height = 32;
    row.update = [raw, get] (const ScenePreset& p)
    {
        if (! raw->isPopupActive())
            raw->setSelectedItemIndex (get (p), juce::dontSendNotification);
    };
    rows.push_back (std::move (row));
}

void Inspector::buttons (std::vector<std::pair<juce::String, std::function<void()>>> list)
{
    struct Strip final : juce::Component
    {
        std::vector<std::unique_ptr<juce::TextButton>> items;
        void resized() override
        {
            auto r = getLocalBounds();
            const int w = items.empty() ? 0 : r.getWidth() / (int) items.size();
            for (auto& b : items)
                b->setBounds (r.removeFromLeft (w).reduced (2, 0));
        }
    };

    auto strip = std::make_unique<Strip>();
    for (auto& [text, action] : list)
    {
        auto b = std::make_unique<juce::TextButton> (text);
        b->onClick = action;
        strip->addAndMakeVisible (*b);
        strip->items.push_back (std::move (b));
    }

    Row row;
    row.control = std::move (strip);
    row.height = 32;
    rows.push_back (std::move (row));
}

void Inspector::widgetRow (int index, const WidgetSpec& w)
{
    static const char* const blendLabels[] = { "normal", "add", "screen", "multiply" };
    auto r = std::make_unique<WidgetRow>();
    r->number = juce::String (index + 1);
    r->name = moduleName (w.module);
    r->detail = blendLabels[(size_t) w.blend];
    r->selected = getSelectedWidget != nullptr && getSelectedWidget() == index;
    r->onClick = [this, index] { if (selectWidget) selectWidget (index); };

    Row row;
    row.control = std::move (r);
    row.height = 28;
    rows.push_back (std::move (row));
}

void Inspector::paramSlider (const juce::String& name, const char* paramId)
{
    auto s = std::make_unique<CortexSlider>();
    s->setName (name);
    s->setSliderStyle (juce::Slider::LinearBar);
    s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, *s));
    s->setDoubleClickReturnValue (true, processor.apvts.getParameter (paramId)->convertFrom0to1 (processor.apvts.getParameter (paramId)->getDefaultValue()));

    Row row;
    row.control = std::move (s);
    row.height = 30;
    rows.push_back (std::move (row));
}

void Inspector::paramCombo (const juce::String& name, const char* paramId)
{
    auto c = std::make_unique<juce::ComboBox>();
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (paramId)))
        c->addItemList (choice->choices, 1);
    comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, paramId, *c));

    Row row;
    row.label.reset (makeLabel (name));
    row.control = std::move (c);
    row.height = 32;
    rows.push_back (std::move (row));
}

void Inspector::paramToggle (const juce::String& name, const char* paramId)
{
    auto t = std::make_unique<juce::ToggleButton> (name);
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, paramId, *t));

    Row row;
    row.control = std::move (t);
    row.height = 30;
    rows.push_back (std::move (row));
}

// --- Tabs ----------------------------------------------------------------------------------------

void Inspector::buildWidgetTab (const ScenePreset& preset)
{
    const auto& scene = preset.scene;
    const int selected = getSelectedWidget != nullptr ? getSelectedWidget() : -1;

    section ("Widgets");
    for (size_t i = 0; i < scene.widgets.size(); ++i)
        widgetRow ((int) i, scene.widgets[i]);

    const auto edit = [this] (std::function<void (std::vector<WidgetSpec>&)> fn)
    {
        processor.editScene ([&fn] (ScenePreset& p) { fn (p.scene.widgets); });
    };

    buttons ({
        { "Add", [this, edit, selected]
          {
              int added = -1;
              edit ([&added, selected] (std::vector<WidgetSpec>& ws)
              {
                  const auto module = selected >= 0 && selected < (int) ws.size() ? ws[(size_t) selected].module : std::string ("bloom_orb");
                  auto w = defaultWidget (module, ws.empty() ? 0 : ws.back().settings.palette);
                  w.blend = ws.empty() ? BlendMode::normal : BlendMode::add;
                  ws.push_back (w);
                  added = (int) ws.size() - 1;
              });
              if (selectWidget) selectWidget (added);
          } },
        { "Copy", [this, edit, selected]
          {
              int added = -1;
              edit ([&added, selected] (std::vector<WidgetSpec>& ws)
              {
                  if (selected < 0 || selected >= (int) ws.size()) return;
                  ws.push_back (ws[(size_t) selected]);
                  added = (int) ws.size() - 1;
              });
              if (added >= 0 && selectWidget) selectWidget (added);
          } },
        { "Delete", [this, edit, selected]
          {
              edit ([selected] (std::vector<WidgetSpec>& ws)
              {
                  if (selected >= 0 && selected < (int) ws.size() && ws.size() > 1)
                      ws.erase (ws.begin() + selected);
              });
              if (selectWidget) selectWidget (-1);
          } },
        { "Up", [this, edit, selected]
          {
              bool moved = false;
              edit ([&moved, selected] (std::vector<WidgetSpec>& ws)
              {
                  if (selected >= 0 && selected + 1 < (int) ws.size())   // later = drawn on top
                  {
                      std::swap (ws[(size_t) selected], ws[(size_t) selected + 1]);
                      moved = true;
                  }
              });
              if (moved && selectWidget) selectWidget (selected + 1);
          } },
        { "Down", [this, edit, selected]
          {
              bool moved = false;
              edit ([&moved, selected] (std::vector<WidgetSpec>& ws)
              {
                  if (selected > 0 && selected < (int) ws.size())
                  {
                      std::swap (ws[(size_t) selected], ws[(size_t) selected - 1]);
                      moved = true;
                  }
              });
              if (moved && selectWidget) selectWidget (selected - 1);
          } },
    });

    if (selected >= 0 && selected < (int) scene.widgets.size())
    {
        const auto& w = scene.widgets[(size_t) selected];
        const auto* info = findModuleInfo (w.module);
        const size_t i = (size_t) selected;
        const auto in = [i] (ScenePreset& p) -> WidgetSpec* { return i < p.scene.widgets.size() ? &p.scene.widgets[i] : nullptr; };
        const auto at = [i] (const ScenePreset& p) -> const WidgetSpec& { return p.scene.widgets[std::min (i, p.scene.widgets.size() - 1)]; };

        section (juce::String (selected + 1) + "  " + moduleName (w.module));
        if (info != nullptr)
            note (info->description);

        combo ("Palette", paletteNames(),
               [at] (const ScenePreset& p) { return at (p).settings.palette; },
               [in] (ScenePreset& p, int v) { if (auto* x = in (p)) x->settings.palette = v; });
        combo ("Blend", { "Normal", "Add", "Screen", "Multiply" },
               [at] (const ScenePreset& p) { return (int) at (p).blend; },
               [in] (ScenePreset& p, int v) { if (auto* x = in (p)) x->blend = (BlendMode) v; });

        const auto percent = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
        const auto times = [] (double v) { return juce::String (v, 2) + "x"; };

        slider ("Opacity", 0.0, 1.0, [at] (const ScenePreset& p) { return (double) at (p).opacity; },
                [in] (ScenePreset& p, double v) { if (auto* x = in (p)) x->opacity = (float) v; }, percent);
        slider ("Intensity", 0.0, 2.0, [at] (const ScenePreset& p) { return (double) at (p).settings.intensity; },
                [in] (ScenePreset& p, double v) { if (auto* x = in (p)) x->settings.intensity = (float) v; }, times);
        slider ("Speed", 0.0, 4.0, [at] (const ScenePreset& p) { return (double) at (p).settings.speed; },
                [in] (ScenePreset& p, double v) { if (auto* x = in (p)) x->settings.speed = (float) v; }, times);
        slider ("Scale", 0.25, 3.0, [at] (const ScenePreset& p) { return (double) at (p).settings.scale; },
                [in] (ScenePreset& p, double v) { if (auto* x = in (p)) x->settings.scale = (float) v; }, times);
        slider ("Color shift", 0.0, 1.0, [at] (const ScenePreset& p) { return (double) at (p).settings.colorShift; },
                [in] (ScenePreset& p, double v) { if (auto* x = in (p)) x->settings.colorShift = (float) v; }, percent);

        if (info != nullptr)
        {
            for (size_t k = 0; k < info->params.size(); ++k)
            {
                const auto& param = info->params[k];
                if (! param.choices.empty())
                {
                    juce::StringArray choices;
                    for (auto* c : param.choices)
                        choices.add (c);
                    const int maxIndex = (int) param.choices.size() - 1;
                    combo (param.name, choices,
                           [at, k, maxIndex] (const ScenePreset& p) { return juce::jlimit (0, maxIndex, juce::roundToInt (at (p).settings.p[k])); },
                           [in, k] (ScenePreset& p, int v) { if (auto* x = in (p)) x->settings.p[k] = (float) v; });
                }
                else
                {
                    slider (param.name, 0.0, 1.0, [at, k] (const ScenePreset& p) { return (double) at (p).settings.p[k]; },
                            [in, k] (ScenePreset& p, double v) { if (auto* x = in (p)) x->settings.p[k] = (float) v; }, percent);
                }
            }
        }
    }
    else
    {
        note ("Click a widget on the canvas or in the list to edit it. Click a module on the left to add one.", 48);
    }

    section ("Scene");
    combo ("Background", paletteNames(),
           [] (const ScenePreset& p) { return p.scene.backgroundPalette; },
           [] (ScenePreset& p, int v) { p.scene.backgroundPalette = v; });
    combo ("Tone map", { "Film", "Punchy", "Neutral" },
           [] (const ScenePreset& p) { return (int) p.scene.tonemap; },
           [] (ScenePreset& p, int v) { p.scene.tonemap = (Tonemap) v; });
    slider ("Exposure", 0.25, 4.0, [] (const ScenePreset& p) { return (double) p.scene.exposure; },
            [] (ScenePreset& p, double v) { p.scene.exposure = (float) v; },
            [] (double v) { return juce::String (v, 2) + "x"; });
    slider ("Bloom", 0.0, 0.5, [] (const ScenePreset& p) { return (double) p.scene.bloom; },
            [] (ScenePreset& p, double v) { p.scene.bloom = (float) v; },
            [] (double v) { return juce::String (juce::roundToInt (v * 200.0)) + "%"; });
    slider ("Crossfade", 0.0, 8.0, [] (const ScenePreset& p) { return (double) p.scene.crossfadeBeats; },
            [] (ScenePreset& p, double v) { p.scene.crossfadeBeats = (float) v; },
            [] (double v) { return v < 0.05 ? juce::String ("cut") : juce::String (v, 1) + " beats"; });
}

void Inspector::buildEffectsTab (const ScenePreset& preset)
{
    const int selected = getSelectedWidget != nullptr ? getSelectedWidget() : -1;
    if (selected < 0 || selected >= (int) preset.scene.widgets.size())
    {
        section ("Effects");
        note ("Select a widget to give it its own effects. Effects on the whole scene come from the Look "
              "(left) and from macros 6 to 8.", 64);
        return;
    }

    const size_t i = (size_t) selected;
    const auto& w = preset.scene.widgets[i];
    section ("Effects on " + juce::String (selected + 1) + " " + moduleName (w.module));

    for (size_t k = 0; k < w.layers.size(); ++k)
    {
        const auto type = w.layers[k].type;
        const auto layerAt = [i, k] (const ScenePreset& p) -> const LayerSpec&
        {
            static const LayerSpec none {};
            return i < p.scene.widgets.size() && k < p.scene.widgets[i].layers.size() ? p.scene.widgets[i].layers[k] : none;
        };
        const auto layerIn = [i, k] (ScenePreset& p) -> LayerSpec*
        {
            return i < p.scene.widgets.size() && k < p.scene.widgets[i].layers.size() ? &p.scene.widgets[i].layers[k] : nullptr;
        };

        buttons ({ { juce::String (layerName (type)) + "  (remove)", [this, i, k]
                     {
                         processor.editScene ([i, k] (ScenePreset& p)
                         {
                             if (i < p.scene.widgets.size() && k < p.scene.widgets[i].layers.size())
                                 p.scene.widgets[i].layers.erase (p.scene.widgets[i].layers.begin() + (long) k);
                         });
                     } } });

        const auto percent = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
        slider ("Amount", 0.0, 1.0, [layerAt] (const ScenePreset& p) { return (double) layerAt (p).amount; },
                [layerIn] (ScenePreset& p, double v) { if (auto* l = layerIn (p)) l->amount = (float) v; }, percent);

        const auto names = layerParamNames (type);
        for (size_t n = 0; n < names.size(); ++n)
            slider (names[n], 0.0, 1.0, [layerAt, n] (const ScenePreset& p) { return (double) layerAt (p).p[n]; },
                    [layerIn, n] (ScenePreset& p, double v) { if (auto* l = layerIn (p)) l->p[n] = (float) v; }, percent);
    }

    buttons ({ { "Add effect", [this, i]
                 {
                     juce::PopupMenu menu;
                     for (int t = 1; t < (int) LayerType::count; ++t)   // bloom is scene-wide, so not offered here
                         menu.addItem (t, layerName ((LayerType) t));
                     menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, i] (int choice)
                     {
                         if (choice <= 0)
                             return;
                         processor.editScene ([i, choice] (ScenePreset& p)
                         {
                             if (i >= p.scene.widgets.size())
                                 return;
                             LayerSpec l;
                             l.type = (LayerType) choice;
                             l.amount = 0.5f;
                             p.scene.widgets[i].layers.push_back (l);
                         });
                     });
                 } } });
    note ("Effects run top to bottom on this widget only. The Look on the left covers the whole scene.", 48);
}

void Inspector::buildMotionTab (const ScenePreset& preset)
{
    section ("Routes");
    note ("A route lets the music move a setting: pick what listens and what moves. "
          "Macros 1 to 8 have fixed routes of their own.", 48);

    const auto targets = routeTargets (preset.scene);
    juce::StringArray targetNames, sourceNames;
    for (const auto& t : targets)
        targetNames.add (t.first);
    for (int s = 0; s < (int) ModSource::macro1; ++s)   // macros are routed already; not offered here
        sourceNames.add (modSourceName ((ModSource) s));

    for (size_t r = 0; r < preset.scene.routes.size(); ++r)
    {
        const auto routeAt = [r] (const ScenePreset& p) -> const ModRoute&
        {
            static const ModRoute none {};
            return r < p.scene.routes.size() ? p.scene.routes[r] : none;
        };
        const auto routeIn = [r] (ScenePreset& p) -> ModRoute* { return r < p.scene.routes.size() ? &p.scene.routes[r] : nullptr; };

        section ("Route " + juce::String ((int) r + 1));
        combo ("Listens to", sourceNames,
               [routeAt] (const ScenePreset& p) { return juce::jmin ((int) routeAt (p).source, (int) ModSource::macro1 - 1); },
               [routeIn] (ScenePreset& p, int v) { if (auto* x = routeIn (p)) x->source = (ModSource) v; });
        combo ("Moves", targetNames,
               [routeAt, targets] (const ScenePreset& p)
               {
                   for (size_t t = 0; t < targets.size(); ++t)
                       if (sameTarget (targets[t].second, routeAt (p).target))
                           return (int) t;
                   return 0;
               },
               [routeIn, targets] (ScenePreset& p, int v) { if (auto* x = routeIn (p)) x->target = targets[(size_t) v].second; });
        slider ("Amount", -1.0, 1.0, [routeAt] (const ScenePreset& p) { return (double) routeAt (p).amount; },
                [routeIn] (ScenePreset& p, double v) { if (auto* x = routeIn (p)) x->amount = (float) v; },
                [] (double v) { return (v >= 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0)) + "%"; });
        combo ("Curve", { "Linear", "Exponential", "Logarithmic" },
               [routeAt] (const ScenePreset& p) { return (int) routeAt (p).curve; },
               [routeIn] (ScenePreset& p, int v) { if (auto* x = routeIn (p)) x->curve = (ModCurve) v; });
        slider ("Smoothing", 0.0, 1.0, [routeAt] (const ScenePreset& p) { return (double) routeAt (p).smoothing; },
                [routeIn] (ScenePreset& p, double v) { if (auto* x = routeIn (p)) x->smoothing = (float) v; },
                [] (double v) { return juce::String (juce::roundToInt (v * 1000.0)) + " ms"; });
        buttons ({ { "Remove route", [this, r]
                     {
                         processor.editScene ([r] (ScenePreset& p)
                         {
                             if (r < p.scene.routes.size())
                                 p.scene.routes.erase (p.scene.routes.begin() + (long) r);
                         });
                     } } });
    }

    buttons ({ { "Add route", [this]
                 {
                     processor.editScene ([] (ScenePreset& p)
                     {
                         ModRoute r;
                         r.source = ModSource::kick;
                         r.target.kind = ModTarget::Kind::widgetScale;
                         r.amount = 0.15f;
                         r.smoothing = 0.03f;
                         p.scene.routes.push_back (r);
                     });
                 } } });
}

void Inspector::buildAudioTab()
{
    section ("Detection");
    paramCombo ("Source", params::id::sourceType);
    paramSlider ("Kick sensitivity", params::id::kickSensitivity);
    paramSlider ("Kick release", params::id::kickRelease);
    paramSlider ("Snare sensitivity", params::id::snareSensitivity);
    paramSlider ("Snare release", params::id::snareRelease);
    paramSlider ("Hat sensitivity", params::id::hatSensitivity);
    paramSlider ("Hat release", params::id::hatRelease);
    note ("On a single drum track, set Source to that drum: every hit then counts as it.", 48);

    section ("Output");
    paramSlider ("Master intensity", params::id::intensity);
    paramCombo ("Flash limiter", params::id::flashLimiter);
    paramToggle ("Freeze picture", params::id::freeze);
    note ("The flash limiter holds big flashes to three a second (two in Strict), for viewers with "
          "photosensitive epilepsy. Leave it on for anything you publish.", 64);
}

} // namespace cortex::ui

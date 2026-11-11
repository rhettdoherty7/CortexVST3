#include "CanvasComponent.h"

#include <core/Params.h>

namespace cortex
{

using namespace juce::gl;

namespace
{
    // Behind the letterbox: the editor's background colour (CLAUDE.md section 10), in sRGB.
    constexpr float letterboxGrey = 0x0b / 255.0f;
    constexpr float handleRadius = 9.0f;   // pixels around a corner that grab it for resizing
    constexpr float minimumSize = 0.05f;   // smallest widget, as a fraction of the canvas
}

CanvasComponent::CanvasComponent (CortexProcessor& p) : processor (p)
{
    flashParam     = processor.apvts.getRawParameterValue (params::id::flashLimiter);
    intensityParam = processor.apvts.getRawParameterValue (params::id::intensity);
    freezeParam    = processor.apvts.getRawParameterValue (params::id::freeze);
    for (int i = 0; i < 8; ++i)
        macroParams[(size_t) i] = processor.apvts.getRawParameterValue (params::id::macro (i));

    setOpaque (true);
    context.setOpenGLVersionRequired (juce::OpenGLContext::openGL4_1);
    context.setRenderer (this);
    context.setComponentPaintingEnabled (false);
    context.setContinuousRepainting (false);
    context.attachTo (*this);

    vblank = std::make_unique<juce::VBlankAttachment> (this, [this]
    {
        // Measure the refresh interval, then render on every Nth refresh with N = floor(Hz / 60):
        // 60 fps at 60/120/240 Hz, 72 at 144 Hz, 82 at 165 Hz. Always at least 60, never judders.
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (lastVBlankMs > 0.0)
        {
            const double gap = now - lastVBlankMs;
            if (gap > 0.5 && gap < 100.0)
                vblankIntervalMs = vblankIntervalMs <= 0.0 ? gap : vblankIntervalMs * 0.95 + gap * 0.05;
        }
        lastVBlankMs = now;

        const double refreshHz = vblankIntervalMs > 0.0 ? 1000.0 / vblankIntervalMs : 60.0;
        const int every = juce::jmax (1, (int) std::floor (refreshHz / 60.0 + 0.05));

        if (++vblanksSinceRender >= every)
        {
            vblanksSinceRender = 0;
            if (isShowing())
                context.triggerRepaint();
        }
    });
}

CanvasComponent::~CanvasComponent()
{
    vblank = nullptr;
    context.detach();
}

void CanvasComponent::paint (juce::Graphics& g)
{
    // Only visible if OpenGL could not start.
    g.fillAll (juce::Colour (0xff0b0b0f));
    g.setColour (juce::Colour (0xff8a8a96));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Starting the visual engine...", getLocalBounds(), juce::Justification::centred);
}

void CanvasComponent::resized()
{
    widthPx.store (getWidth());
    heightPx.store (getHeight());
}

void CanvasComponent::setSelectedWidget (int index) noexcept
{
    selected.store (index);
}

CanvasComponent::Snapshot CanvasComponent::takeSnapshot()
{
    const juce::SpinLock::ScopedLockType lock (snapshotLock);
    auto copy = shared;
    shared.kickHit = shared.snareHit = shared.hatHit = shared.gotFrame = false;
    return copy;
}

// --- Mouse ---------------------------------------------------------------------------------------

juce::Rectangle<float> CanvasComponent::pictureBounds() const
{
    const auto area = getLocalBounds().toFloat();
    float w = area.getWidth(), h = w * 9.0f / 16.0f;
    if (h > area.getHeight())
    {
        h = area.getHeight();
        w = h * 16.0f / 9.0f;
    }
    return juce::Rectangle<float> (w, h).withCentre (area.getCentre());
}

juce::Point<float> CanvasComponent::toCanvas (juce::Point<float> p) const
{
    const auto r = pictureBounds();
    return { (p.x - r.getX()) / r.getWidth(), (p.y - r.getY()) / r.getHeight() };
}

int CanvasComponent::hitTest (juce::Point<float> c, const SceneSpec& scene) const
{
    for (int i = (int) scene.widgets.size() - 1; i >= 0; --i)   // topmost first
    {
        const auto& w = scene.widgets[(size_t) i];
        if (juce::Rectangle<float> (w.x, w.y, w.w, w.h).contains (c))
            return i;
    }
    return -1;
}

int CanvasComponent::cornerAt (juce::Point<float> p, const WidgetSpec& w) const
{
    const auto r = pictureBounds();
    const juce::Point<float> corners[4] = {
        { r.getX() + w.x * r.getWidth(),         r.getY() + w.y * r.getHeight() },
        { r.getX() + (w.x + w.w) * r.getWidth(), r.getY() + w.y * r.getHeight() },
        { r.getX() + w.x * r.getWidth(),         r.getY() + (w.y + w.h) * r.getHeight() },
        { r.getX() + (w.x + w.w) * r.getWidth(), r.getY() + (w.y + w.h) * r.getHeight() },
    };
    for (int i = 0; i < 4; ++i)
        if (corners[i].getDistanceFrom (p) <= handleRadius)
            return i;
    return -1;
}

void CanvasComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto scene = processor.scene().scene;
    const int index = selected.load();
    if (index >= 0 && index < (int) scene.widgets.size())
    {
        const int corner = cornerAt (e.position, scene.widgets[(size_t) index]);
        if (corner == 0 || corner == 3) { setMouseCursor (juce::MouseCursor::TopLeftCornerResizeCursor); return; }
        if (corner == 1 || corner == 2) { setMouseCursor (juce::MouseCursor::TopRightCornerResizeCursor); return; }
    }
    setMouseCursor (hitTest (toCanvas (e.position), scene) >= 0 ? juce::MouseCursor::DraggingHandCursor
                                                                 : juce::MouseCursor::NormalCursor);
}

void CanvasComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto scene = processor.scene().scene;
    drag = Drag::none;
    dragStartPoint = toCanvas (e.position);

    int index = selected.load();
    if (index >= 0 && index < (int) scene.widgets.size())
    {
        dragCorner = cornerAt (e.position, scene.widgets[(size_t) index]);
        if (dragCorner >= 0)
            drag = Drag::resize;
    }

    if (drag == Drag::none)
    {
        index = hitTest (dragStartPoint, scene);
        if (index >= 0)
            drag = Drag::move;
    }

    if (index >= 0 && index < (int) scene.widgets.size())
    {
        const auto& w = scene.widgets[(size_t) index];
        dragStartRect = { w.x, w.y, w.w, w.h };
    }

    if (index != selected.load())
    {
        selected.store (index);
        if (onSelectionChanged != nullptr)
            onSelectionChanged (index);
    }
}

void CanvasComponent::mouseDrag (const juce::MouseEvent& e)
{
    const int index = selected.load();
    if (drag == Drag::none || index < 0)
        return;

    const auto delta = toCanvas (e.position) - dragStartPoint;
    auto r = dragStartRect;

    if (drag == Drag::move)
    {
        r.setPosition (juce::jlimit (0.0f, 1.0f - r.getWidth(), r.getX() + delta.x),
                       juce::jlimit (0.0f, 1.0f - r.getHeight(), r.getY() + delta.y));
    }
    else
    {
        float left = r.getX(), top = r.getY(), right = r.getRight(), bottom = r.getBottom();
        if (dragCorner == 0 || dragCorner == 2) left   = juce::jlimit (0.0f, right - minimumSize, left + delta.x);
        else                                    right  = juce::jlimit (left + minimumSize, 1.0f, right + delta.x);
        if (dragCorner == 0 || dragCorner == 1) top    = juce::jlimit (0.0f, bottom - minimumSize, top + delta.y);
        else                                    bottom = juce::jlimit (top + minimumSize, 1.0f, bottom + delta.y);
        r = { left, top, right - left, bottom - top };
    }

    // Snap edges to the canvas edges and centre lines when close, so layouts line up.
    const auto snap = [] (float v) { for (float s : { 0.0f, 0.5f, 1.0f }) if (std::abs (v - s) < 0.012f) return s; return v; };
    if (drag == Drag::move)
        r.setPosition (snap (r.getX()), snap (r.getY()));

    processor.editScene ([index, r] (ScenePreset& p)
    {
        if (index < (int) p.scene.widgets.size())
        {
            auto& w = p.scene.widgets[(size_t) index];
            w.x = r.getX(); w.y = r.getY(); w.w = r.getWidth(); w.h = r.getHeight();
        }
    });
}

void CanvasComponent::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::none;
}

void CanvasComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int index = hitTest (toCanvas (e.position), processor.scene().scene);
    if (index < 0)
        return;

    processor.editScene ([index] (ScenePreset& p)
    {
        auto& w = p.scene.widgets[(size_t) index];
        w.x = w.y = 0.0f;
        w.w = w.h = 1.0f;
    });
}

// --- GL thread -----------------------------------------------------------------------------------

void CanvasComponent::newOpenGLContextCreated()
{
    std::string log;
    const bool ok = renderer.init (log);

    {
        const juce::SpinLock::ScopedLockType lock (snapshotLock);
        shared.error = ok ? juce::String() : juce::String ("Visual engine failed to start:\n") + log;
    }

    sceneVersionSeen = -1;
    sceneLoadedOnce = false;
    lastTime = 0.0;
    rendering.store (ok, std::memory_order_release);
}

void CanvasComponent::openGLContextClosing()
{
    rendering.store (false, std::memory_order_release);
    renderer.shutdown();
}

void CanvasComponent::renderOpenGL()
{
    if (! rendering.load (std::memory_order_acquire))
        return;

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = lastTime > 0.0 ? (float) (now - lastTime) : 1.0f / 60.0f;
    lastTime = now;

    ++fpsFrames;
    if (now - fpsWindowStart >= 1.0)
    {
        framesPerSecond = fpsWindowStart > 0.0 ? (float) (fpsFrames / (now - fpsWindowStart)) : 0.0f;
        fpsWindowStart = now;
        fpsFrames = 0;
    }

    // --- Analysis frames in ----------------------------------------------------------------------
    const bool frozen = freezeParam->load (std::memory_order_relaxed) > 0.5f;
    AnalysisFrame newest {};
    bool gotFrame = false, kick = false, snare = false, hat = false;

    processor.drainFrames ([&] (const AnalysisFrame& f)
    {
        if (! frozen)
            interpolator.push (f);
        newest = f;
        gotFrame = true;
        kick = kick || f.kickHit;
        snare = snare || f.snareHit;
        hat = hat || f.hatHit;
    });

    RenderInputs inputs = frozen ? interpolator.current() : interpolator.update (dt);
    if (frozen)
        inputs.dt = 0.0f;

    // --- Scene: take edits as they happen ---------------------------------------------------------
    std::string error;
    if (const int version = processor.sceneVersion(); version != sceneVersionSeen)
    {
        sceneVersionSeen = version;
        const auto preset = processor.scene();   // copies, only when something changed
        if (! renderer.applyScene (preset.scene, error, sceneLoadedOnce))
            error = "Scene failed to load:\n" + error;
        sceneLoadedOnce = true;
        look = preset.look;
        cover = preset.cover;
    }

   #if JUCE_DEBUG
    reloadTimer += dt;
    if (reloadTimer > 0.5f)
    {
        reloadTimer = 0.0f;
        const auto shaderErrors = renderer.reloadShadersIfChanged();
        if (! shaderErrors.empty())
            error = "Shader reload failed (keeping the last good version):\n" + shaderErrors;
    }
   #endif

    // --- Draw ------------------------------------------------------------------------------------
    const double scale = context.getRenderingScale();
    const int w = juce::roundToInt (scale * widthPx.load());
    const int h = juce::roundToInt (scale * heightPx.load());
    if (w < 16 || h < 16)
        return;

    GLint framebuffer = 0;
    glGetIntegerv (GL_FRAMEBUFFER_BINDING, &framebuffer);
    glViewport (0, 0, w, h);
    glClearColor (letterboxGrey, letterboxGrey, letterboxGrey, 1.0f);
    glClear (GL_COLOR_BUFFER_BIT);

    int vw = w, vh = juce::roundToInt (w * 9.0 / 16.0);
    if (vh > h)
    {
        vh = h;
        vw = juce::roundToInt (h * 16.0 / 9.0);
    }

    FrameSettings settings;
    settings.flashMode = (FlashLimiter::Mode) juce::jlimit (0, 2, juce::roundToInt (flashParam->load()));
    settings.masterIntensity = intensityParam->load();
    settings.look = juce::jlimit (0, numFactoryLooks() - 1, look);
    for (size_t i = 0; i < 8; ++i)
        settings.macros[i] = macroParams[i]->load (std::memory_order_relaxed);
    settings.paletteOverride = cover ? &*cover : nullptr;
    settings.selectedWidget = selected.load();

    renderer.render (inputs, vw, vh, (GLuint) framebuffer, (w - vw) / 2, (h - vh) / 2, vw, vh, settings);

    // --- Report back to the editor ---------------------------------------------------------------
    const juce::SpinLock::ScopedLockType lock (snapshotLock);
    if (gotFrame)
    {
        shared.frame = newest;
        shared.gotFrame = true;
    }
    shared.kickHit = shared.kickHit || kick;
    shared.snareHit = shared.snareHit || snare;
    shared.hatHit = shared.hatHit || hat;
    shared.stats = renderer.stats();
    shared.framesPerSecond = framesPerSecond;
    if (! error.empty())
        shared.error = error;
}

} // namespace cortex

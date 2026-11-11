#include "CanvasComponent.h"

#include <core/Params.h>

namespace cortex
{

using namespace juce::gl;

namespace
{
    // Behind the letterbox: the editor's panel colour (CLAUDE.md section 10), in sRGB.
    constexpr float letterboxGrey = 0x0b / 255.0f;
}

CanvasComponent::CanvasComponent (CortexProcessor& p) : processor (p)
{
    sceneParam     = processor.apvts.getRawParameterValue (params::id::sceneSelect);
    flashParam     = processor.apvts.getRawParameterValue (params::id::flashLimiter);
    intensityParam = processor.apvts.getRawParameterValue (params::id::intensity);
    freezeParam    = processor.apvts.getRawParameterValue (params::id::freeze);

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

CanvasComponent::Snapshot CanvasComponent::takeSnapshot()
{
    const juce::SpinLock::ScopedLockType lock (snapshotLock);
    auto copy = shared;
    shared.kickHit = shared.snareHit = shared.hatHit = shared.gotFrame = false;
    return copy;
}

void CanvasComponent::newOpenGLContextCreated()
{
    std::string log;
    const bool ok = renderer.init (log);

    {
        const juce::SpinLock::ScopedLockType lock (snapshotLock);
        shared.error = ok ? juce::String() : juce::String ("Visual engine failed to start:\n") + log;
    }

    loadedScene = -1;
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

    // --- Scene -----------------------------------------------------------------------------------
    std::string error;
    const int scene = juce::jlimit (0, numFactoryScenes() - 1, juce::roundToInt (sceneParam->load()) - 1);
    if (scene != loadedScene)
    {
        if (! renderer.loadScene (factoryScene (scene), error))
            error = "Scene failed to load:\n" + error;
        loadedScene = scene;
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

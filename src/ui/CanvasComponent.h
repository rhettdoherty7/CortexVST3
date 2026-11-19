#pragma once

#include <core/PluginProcessor.h>
#include <engine/FrameInterpolator.h>
#include <engine/Renderer.h>

#include <juce_opengl/juce_opengl.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace cortex
{

/** The visual canvas: an OpenGL 4.1 view that drains the processor's analysis frames, smooths
    them, and renders the processor's scene letterboxed at 16:9.

    Rendering is driven by the display's vertical blank and only while the component is showing,
    so a closed, hidden or minimised editor costs no GPU time (North Star Q3). On fast displays it
    renders every Nth refresh, aiming for 60 fps with an even cadence (docs/DECISIONS.md D-019).

    Mouse: click selects a widget, drag moves it, drag a corner resizes it, double-click fills the
    canvas with it (section 10). */
class CanvasComponent final : public juce::Component,
                              private juce::OpenGLRenderer
{
public:
    explicit CanvasComponent (CortexProcessor&);
    ~CanvasComponent() override;

    /** True once the GL context exists and the renderer started. Until then the editor drains
        the analysis frames itself. */
    bool isRendering() const noexcept   { return rendering.load (std::memory_order_acquire); }

    /** What the GL thread saw since the last call: the newest frame and any hits in between. */
    struct Snapshot
    {
        AnalysisFrame frame {};
        bool kickHit = false, snareHit = false, hatHit = false, gotFrame = false;
        RenderStats stats;
        float framesPerSecond = 0.0f;
        juce::String error;
    };
    Snapshot takeSnapshot();

    int selectedWidget() const noexcept       { return selected.load(); }
    void setSelectedWidget (int index) noexcept;
    std::function<void (int)> onSelectionChanged;   // message thread

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    /** The 16:9 picture inside this component, in component coordinates. */
    juce::Rectangle<float> pictureBounds() const;
    juce::Point<float> toCanvas (juce::Point<float>) const;   // 0..1, origin top-left
    int hitTest (juce::Point<float> canvasPoint, const SceneSpec&) const;
    int cornerAt (juce::Point<float> componentPoint, const WidgetSpec&) const;   // -1 or 0..3

    CortexProcessor& processor;
    juce::OpenGLContext context;
    std::unique_ptr<juce::VBlankAttachment> vblank;
    double lastVBlankMs = 0.0, vblankIntervalMs = 0.0;   // message thread
    int vblanksSinceRender = 0;

    // Dragging (message thread)
    enum class Drag { none, move, resize };
    Drag drag = Drag::none;
    int dragCorner = -1;
    juce::Rectangle<float> dragStartRect;   // canvas units
    juce::Point<float> dragStartPoint;

    // GL thread only
    Renderer renderer;
    FrameInterpolator interpolator;
    int sceneVersionSeen = -1;
    bool sceneLoadedOnce = false;
    int look = 0;
    std::optional<Palette> cover;
    double lastTime = 0.0;
    float reloadTimer = 0.0f;
    double fpsWindowStart = 0.0;
    int fpsFrames = 0;
    float framesPerSecond = 0.0f;

    std::atomic<bool> rendering { false };
    std::atomic<int> widthPx { 0 }, heightPx { 0 };
    std::atomic<int> selected { -1 };

    juce::SpinLock snapshotLock;
    Snapshot shared;

    std::atomic<float>* flashParam = nullptr;
    std::atomic<float>* intensityParam = nullptr;
    std::atomic<float>* freezeParam = nullptr;
    std::array<std::atomic<float>*, 8> macroParams {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CanvasComponent)
};

} // namespace cortex

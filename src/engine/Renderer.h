#pragma once

#include "FrameInterpolator.h"
#include "Palette.h"
#include "Scene.h"

#include <gfx/Device.h>
#include <safety/FlashLimiter.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cortex
{

class Module;

struct FrameSettings
{
    FlashLimiter::Mode flashMode = FlashLimiter::Mode::standard;
    float masterIntensity = 1.0f;
    bool exact = false;     // offline rendering: full resolution, and the flash limiter measures the same frame it limits
};

struct RenderStats
{
    float gpuMs = 0.0f;         // smoothed GPU time of the whole frame
    float renderScale = 1.0f;   // adaptive internal resolution, 1 = full
    int   flashes = 0;          // flashes let through in the last second
    float limiterGain = 1.0f;
};

/** Draws a scene (section 5.1): every widget's module into its own HDR target, composited onto
    the canvas, then bloom, tone mapping, the flash limiter and a dithered 8-bit output.
    Everything here runs on the GL thread. No heap allocation per frame. */
class Renderer
{
public:
    Renderer();
    ~Renderer();

    bool init (std::string& log);
    void shutdown();

    bool loadScene (const SceneSpec&, std::string& log);

    /** canvasWidth/Height: full-quality internal resolution. Draws into `outputFramebuffer`
        at the given viewport. */
    void render (const RenderInputs&, int canvasWidth, int canvasHeight,
                 GLuint outputFramebuffer, int viewportX, int viewportY, int viewportWidth, int viewportHeight,
                 const FrameSettings&);

    const RenderStats& stats() const noexcept   { return statsOut; }

    /** Debug builds: rebuilds every shader when a file in shaders/ changes. Returns the error log, if any. */
    std::string reloadShadersIfChanged();

    // --- For modules ---------------------------------------------------------------------------
    gfx::Device& device() noexcept                   { return dev; }
    const RenderInputs& inputs() const noexcept      { return *in; }

    /** Compiled once and shared between widgets. Returns nullptr and fills `log` on failure. */
    gfx::Shader* program (const char* vertexFile, const char* fragmentFile, std::string& log);

    /** Sets the shared uniform contract (shaders/common.glsl) on the bound program. */
    void bindCommon (gfx::Shader&, const WidgetSettings&, int targetWidth, int targetHeight) noexcept;

    /** Binds `target` and runs `shader` over all of it. */
    void drawFullscreen (gfx::Shader&, gfx::RenderTarget& target, const WidgetSettings&) noexcept;

    /** Multiplies everything in `target` by `keep` (trails). */
    void fade (const gfx::RenderTarget& target, float keep) noexcept;

    /** Draws `texture` over the bound target with the current blend mode. */
    void drawTexture (GLuint texture, float opacity) noexcept;

private:
    struct LiveWidget;
    struct Program { std::string vertex, fragment; std::unique_ptr<gfx::Shader> shader; };

    void updateDataTextures (const RenderInputs&) noexcept;
    void renderBloom (int width, int height) noexcept;
    void present (int measure, const FrameSettings&) noexcept;
    void measureAndLimit (float dt, const FrameSettings&) noexcept;
    void collectGpuTime() noexcept;
    void adapt (float dt) noexcept;

    static std::string vertexSource (const char* file);
    static std::string fragmentSource (const char* file);

    gfx::Device dev;
    std::map<std::string, Program> programs;
    gfx::Shader *copyShader = nullptr, *downShader = nullptr, *upShader = nullptr, *presentShader = nullptr;

    gfx::DataTexture spectrumTex, waveTex, waveHistoryTex, spectrumHistoryTex, noiseTex;
    std::vector<float> waveStaging, waveHistoryRing, waveHistoryStaging, spectrumHistoryRing, spectrumHistoryStaging;
    int historyHead = 0;
    float historyAccumulator = 0.0f, historyPhase = 0.0f;

    gfx::RenderTarget sceneTarget, measureTarget;
    std::array<gfx::RenderTarget, 6> bloom;

    std::vector<std::unique_ptr<LiveWidget>> widgets;
    Rgb background;
    Tonemap tonemap = Tonemap::film;
    float exposure = 1.0f, bloomStrength = 0.08f;

    FlashLimiter limiter;
    std::array<GLuint, 3> pixelBuffers {};
    std::array<GLsync, 3> fences {};
    int measureWrite = 0;
    float unmeasuredSeconds = 0.0f, limiterGain = 1.0f, redDesaturation = 0.0f;

    std::array<GLuint, 4> queries {};
    std::array<bool, 4> queryPending {};
    int queryWrite = 0;
    bool timing = false;
    float gpuMs = 0.0f, renderScale = 1.0f, overBudget = 0.0f, underBudget = 0.0f;

    const RenderInputs* in = nullptr;
    float masterIntensity = 1.0f;
    uint32_t frameCounter = 0;
    long long shaderStamp = 0;
    RenderStats statsOut;
};

} // namespace cortex

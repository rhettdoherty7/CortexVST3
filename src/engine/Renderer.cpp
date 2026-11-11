#include "Renderer.h"

#include <modules/Module.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cortex
{

using namespace juce::gl;

namespace
{
    constexpr int   historyRows   = 64;
    constexpr int   bloomLevels   = 6;
    constexpr int   measureSize   = 256;
    constexpr int   measureLevel  = 6;          // 256 >> 6 = the limiter's 4 x 4 grid
    constexpr float frameBudgetMs = 8.0f;       // North Star Q3, 1080p60 on a mid-range GPU

    /** 64 x 64 blue noise by void-and-cluster (Ulichney), ranks scaled to 0..1. */
    std::vector<float> makeBlueNoise()
    {
        constexpr int n = 64, count = n * n, mask = n - 1;
        constexpr float sigma = 1.5f;

        std::vector<float> kernel (count);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                const int dx = std::min (x, n - x), dy = std::min (y, n - y);
                kernel[(size_t) (y * n + x)] = std::exp (-(float) (dx * dx + dy * dy) / (2.0f * sigma * sigma));
            }

        std::vector<uint8_t> on (count, 0);
        std::vector<float> energy (count, 0.0f);

        const auto splat = [&] (int index, float sign)
        {
            const int px = index & mask, py = index / n;
            for (int y = 0; y < n; ++y)
            {
                const float* row = kernel.data() + ((y - py) & mask) * n;
                float* out = energy.data() + y * n;
                for (int x = 0; x < n; ++x)
                    out[x] += sign * row[(x - px) & mask];
            }
        };

        const auto tightest = [&] (std::vector<uint8_t>& pattern)
        {
            int best = -1;
            for (int i = 0; i < count; ++i)
                if (pattern[(size_t) i] && (best < 0 || energy[(size_t) i] > energy[(size_t) best]))
                    best = i;
            return best;
        };

        const auto largestVoid = [&] (std::vector<uint8_t>& pattern)
        {
            int best = -1;
            for (int i = 0; i < count; ++i)
                if (! pattern[(size_t) i] && (best < 0 || energy[(size_t) i] < energy[(size_t) best]))
                    best = i;
            return best;
        };

        // Initial pattern: 10% of pixels, from a fixed seed, then relaxed until stable.
        uint32_t seed = 0x2545f491u;
        const int initial = count / 10;
        for (int placed = 0; placed < initial;)
        {
            seed = seed * 1664525u + 1013904223u;
            const int i = (int) ((seed >> 8) % (uint32_t) count);
            if (! on[(size_t) i]) { on[(size_t) i] = 1; splat (i, 1.0f); ++placed; }
        }

        for (int iteration = 0; iteration < count; ++iteration)
        {
            const int cluster = tightest (on);
            on[(size_t) cluster] = 0;
            splat (cluster, -1.0f);
            const int gap = largestVoid (on);
            on[(size_t) gap] = 1;
            splat (gap, 1.0f);
            if (gap == cluster)
                break;
        }

        std::vector<int> rank (count, 0);
        const auto prototype = on;
        const auto prototypeEnergy = energy;

        for (int r = initial - 1; r >= 0; --r)   // phase 1: rank the initial points
        {
            const int cluster = tightest (on);
            on[(size_t) cluster] = 0;
            splat (cluster, -1.0f);
            rank[(size_t) cluster] = r;
        }

        on = prototype;
        energy = prototypeEnergy;
        for (int r = initial; r < count; ++r)    // phases 2 and 3: fill the voids
        {
            const int gap = largestVoid (on);
            on[(size_t) gap] = 1;
            splat (gap, 1.0f);
            rank[(size_t) gap] = r;
        }

        std::vector<float> out (count);
        for (int i = 0; i < count; ++i)
            out[(size_t) i] = ((float) rank[(size_t) i] + 0.5f) / (float) count;
        return out;
    }

    const std::vector<float>& blueNoise()
    {
        static const std::vector<float> table = makeBlueNoise();
        return table;
    }

    gfx::Device::Blend toDeviceBlend (BlendMode mode) noexcept
    {
        switch (mode)
        {
            case BlendMode::add:      return gfx::Device::Blend::additive;
            case BlendMode::screen:   return gfx::Device::Blend::screen;
            case BlendMode::multiply: return gfx::Device::Blend::multiply;
            case BlendMode::normal:   break;
        }
        return gfx::Device::Blend::alpha;
    }
}

struct Renderer::LiveWidget
{
    std::unique_ptr<Module> module;
    WidgetSpec spec;
    gfx::RenderTarget target;

    WidgetSettings current;                  // spec settings after modulation, this frame
    std::vector<LayerSpec> layersCurrent;    // same size as spec.layers
    LayerChain chain;
    GLuint output = 0;                       // what gets composited: the target, or its last layer
    float clock = 0.0f, motion = 0.0f;
};

void Renderer::LayerChain::prepare (size_t layerCount)
{
    history.clear();
    for (size_t i = 0; i < layerCount; ++i)
        history.push_back (std::make_unique<gfx::RenderTarget>());
}

void Renderer::LayerChain::release()
{
    for (auto& t : pingPong)
        t.release();
    history.clear();
}

Renderer::Renderer() = default;

Renderer::~Renderer()
{
    // shutdown() must already have run on the GL thread; nothing here touches GL.
    jassert (widgets.empty() && programs.empty());
}

// --- Shaders -------------------------------------------------------------------------------------

std::string Renderer::vertexSource (const char* file)
{
    return "#version 410 core\n" + gfx::ShaderSource::load (file);
}

std::string Renderer::fragmentSource (const char* file)
{
    return "#version 410 core\n" + gfx::ShaderSource::load ("common.glsl") + "\n#line 1\n" + gfx::ShaderSource::load (file);
}

gfx::Shader* Renderer::program (const char* vertexFile, const char* fragmentFile, std::string& log)
{
    const std::string key = std::string (vertexFile) + "|" + fragmentFile;
    if (const auto found = programs.find (key); found != programs.end())
        return found->second.shader.get();

    auto shader = std::make_unique<gfx::Shader>();
    std::string errors;
    if (! shader->build (vertexSource (vertexFile), fragmentSource (fragmentFile), errors))
    {
        log += std::string (fragmentFile) + ":\n" + errors;
        return nullptr;
    }

    auto* raw = shader.get();
    programs.emplace (key, Program { vertexFile, fragmentFile, std::move (shader) });
    return raw;
}

std::string Renderer::reloadShadersIfChanged()
{
    const auto stamp = gfx::ShaderSource::latestModification();
    if (stamp == 0 || stamp == shaderStamp)
        return {};

    shaderStamp = stamp;
    std::string log;
    for (auto& [key, entry] : programs)
    {
        std::string errors;
        if (! entry.shader->build (vertexSource (entry.vertex.c_str()), fragmentSource (entry.fragment.c_str()), errors))
            log += entry.fragment + ":\n" + errors;
    }
    return log;
}

// --- Lifetime ------------------------------------------------------------------------------------

bool Renderer::init (std::string& log)
{
    dev.init();

    copyShader    = program ("fullscreen.vert.glsl", "copy.frag.glsl", log);
    downShader    = program ("fullscreen.vert.glsl", "bloom_down.frag.glsl", log);
    upShader      = program ("fullscreen.vert.glsl", "bloom_up.frag.glsl", log);
    presentShader = program ("fullscreen.vert.glsl", "present.frag.glsl", log);
    if (copyShader == nullptr || downShader == nullptr || upShader == nullptr || presentShader == nullptr)
        return false;

    for (int t = 0; t < (int) LayerType::count; ++t)
        if (const char* file = layerShaderFile ((LayerType) t))
            if ((layerShaders[(size_t) t] = program ("fullscreen.vert.glsl", file, log)) == nullptr)
                return false;

    spectrumTex.ensure (128, 1);
    waveTex.ensure (256, 2);
    waveHistoryTex.ensure (256, historyRows);
    spectrumHistoryTex.ensure (128, historyRows);
    noiseTex.ensure (64, 64, GL_R32F, GL_RED, false);
    noiseTex.upload (blueNoise().data());

    waveStaging.assign (512, 0.0f);
    waveHistoryRing.assign (256 * historyRows, 0.0f);
    waveHistoryStaging.assign (256 * historyRows, 0.0f);
    spectrumHistoryRing.assign (128 * historyRows, 0.0f);
    spectrumHistoryStaging.assign (128 * historyRows, 0.0f);

    glGenBuffers ((GLsizei) pixelBuffers.size(), pixelBuffers.data());
    for (auto buffer : pixelBuffers)
    {
        glBindBuffer (GL_PIXEL_PACK_BUFFER, buffer);
        glBufferData (GL_PIXEL_PACK_BUFFER, 16 * 4 * sizeof (float), nullptr, GL_STREAM_READ);
    }
    glBindBuffer (GL_PIXEL_PACK_BUFFER, 0);

    glGenQueries ((GLsizei) queries.size(), queries.data());

    shaderStamp = gfx::ShaderSource::latestModification();
    limiter.reset();
    return true;
}

void Renderer::shutdown()
{
    widgets.clear();
    globalChain.release();
    loadedLook = -1;
    programs.clear();
    copyShader = downShader = upShader = presentShader = nullptr;
    layerShaders = {};

    for (auto* t : { &spectrumTex, &waveTex, &waveHistoryTex, &spectrumHistoryTex, &noiseTex })
        t->release();

    sceneTarget.release();
    measureTarget.release();
    for (auto& b : bloom)
        b.release();

    for (auto& fence : fences)
    {
        if (fence != nullptr)
            glDeleteSync (fence);
        fence = nullptr;
    }

    if (pixelBuffers[0] != 0)
        glDeleteBuffers ((GLsizei) pixelBuffers.size(), pixelBuffers.data());
    if (queries[0] != 0)
        glDeleteQueries ((GLsizei) queries.size(), queries.data());
    pixelBuffers = {};
    queries = {};
    queryPending = {};

    dev.shutdown();
}

bool Renderer::loadScene (const SceneSpec& spec, std::string& log)
{
    widgets.clear();

    for (const auto& w : spec.widgets)
    {
        auto live = std::make_unique<LiveWidget>();
        live->module = createModule (w.module);
        live->spec = w;

        if (live->module == nullptr)
        {
            log += std::string ("unknown module: ") + w.module + "\n";
            continue;
        }

        if (! live->module->init (*this, log))
            return false;

        live->current = w.settings;
        live->layersCurrent = w.layers;
        live->chain.prepare (w.layers.size());
        widgets.push_back (std::move (live));
    }

    auto routes = spec.routes;
    routes.insert (routes.end(), macroRoutes().begin(), macroRoutes().end());
    router.setRoutes (std::move (routes));

    backgroundPalette = spec.backgroundPalette;
    tonemap = spec.tonemap;
    exposureBase = exposure = spec.exposure;
    bloomBase = bloomStrength = spec.bloom;
    return true;
}

// --- Helpers for modules -------------------------------------------------------------------------

void Renderer::bindCommon (gfx::Shader& s, const WidgetSettings& w, int width, int height) noexcept
{
    const auto& i = *in;

    s.set ("uTime", w.clock);
    s.set ("uDt", i.dt * w.speed);
    s.set ("uRes", (float) width, (float) height);
    s.set ("uKick", i.kick);
    s.set ("uSnare", i.snare);
    s.set ("uHat", i.hat);
    s.set ("uRms", i.rms);
    s.set ("uFlux", i.flux);
    s.set ("uCentroid", i.centroid);
    s.setFloats ("uBands", i.bands, 8);
    s.set ("uBeatPhase", i.beatPhase);
    s.set ("uBarPhase", i.barPhase);
    s.set ("uIntensity", w.intensity * masterIntensity);
    s.set ("uSpeed", w.speed);
    s.set ("uScale", w.scale);
    s.set ("uColorShift", w.colorShift);
    s.setFloats ("uP", w.p.data(), 16);
    s.set ("uSinceKick", i.sinceKick);
    s.set ("uSinceSnare", i.sinceSnare);
    s.set ("uSinceHat", i.sinceHat);
    s.set ("uKickCount", i.kickCount);
    s.set ("uSnareCount", i.snareCount);
    s.set ("uHatCount", i.hatCount);
    s.set ("uMotion", w.motionClock);
    s.set ("uHistoryPhase", historyPhase);

    const auto& pal = paletteFor (w);
    float linear[18], lab[18];
    for (int c = 0; c < 6; ++c)
    {
        const Rgb l = pal.linear (c), k = linearToOklab (l);
        linear[c * 3] = l.r; linear[c * 3 + 1] = l.g; linear[c * 3 + 2] = l.b;
        lab[c * 3]    = k.r; lab[c * 3 + 1]    = k.g; lab[c * 3 + 2]    = k.b;
    }
    s.setVec3s ("uPalette", linear, 6);
    s.setVec3s ("uPaletteLab", lab, 6);

    s.setTexture ("uSpectrum", 0, spectrumTex.texture());
    s.setTexture ("uWave", 1, waveTex.texture());
    s.setTexture ("uWaveHistory", 2, waveHistoryTex.texture());
    s.setTexture ("uSpectrumHistory", 3, spectrumHistoryTex.texture());
}

void Renderer::drawFullscreen (gfx::Shader& shader, gfx::RenderTarget& target, const WidgetSettings& w) noexcept
{
    target.bind();
    gfx::Device::setBlend (gfx::Device::Blend::none);
    shader.use();
    bindCommon (shader, w, target.width(), target.height());
    dev.drawFullscreen();
}

void Renderer::fade (const gfx::RenderTarget& target, float keep) noexcept
{
    target.bind();
    glEnable (GL_BLEND);
    glBlendEquation (GL_FUNC_ADD);
    glBlendFunc (GL_ZERO, GL_CONSTANT_COLOR);
    glBlendColor (keep, keep, keep, keep);
    drawTexture (noiseTex.texture(), 1.0f);   // source is ignored: the result is destination x keep
    glDisable (GL_BLEND);
}

void Renderer::drawTexture (GLuint texture, float opacity) noexcept
{
    copyShader->use();
    copyShader->setTexture ("uSource", 4, texture);
    copyShader->set ("uOpacity", opacity);
    dev.drawFullscreen();
}

// --- Frame ---------------------------------------------------------------------------------------

void Renderer::updateDataTextures (const RenderInputs& i) noexcept
{
    spectrumTex.upload (i.spectrum);

    std::copy (std::begin (i.waveL), std::end (i.waveL), waveStaging.begin());
    std::copy (std::begin (i.waveR), std::end (i.waveR), waveStaging.begin() + 256);
    waveTex.upload (waveStaging.data());

    // Histories advance eight rows per beat, so stacked visuals scroll in time with the music.
    const float interval = std::clamp (60.0f / (float) std::max (i.bpm, 1.0) / 8.0f, 0.03f, 0.12f);
    historyAccumulator += i.dt;

    for (int pushes = 0; historyAccumulator >= interval && pushes < 4; ++pushes)
    {
        historyAccumulator -= interval;
        historyHead = (historyHead + historyRows - 1) % historyRows;
        std::copy (std::begin (i.waveL), std::end (i.waveL), waveHistoryRing.begin() + historyHead * 256);
        std::copy (std::begin (i.spectrum), std::end (i.spectrum), spectrumHistoryRing.begin() + historyHead * 128);
    }
    historyAccumulator = std::min (historyAccumulator, interval);
    historyPhase = historyAccumulator / interval;

    for (int r = 0; r < historyRows; ++r)
    {
        const int source = (historyHead + r) % historyRows;
        std::copy_n (waveHistoryRing.begin() + source * 256, 256, waveHistoryStaging.begin() + r * 256);
        std::copy_n (spectrumHistoryRing.begin() + source * 128, 128, spectrumHistoryStaging.begin() + r * 128);
    }

    waveHistoryTex.upload (waveHistoryStaging.data());
    spectrumHistoryTex.upload (spectrumHistoryStaging.data());
}

void Renderer::renderBloom (GLuint source, int width, int height) noexcept
{
    int sourceW = width, sourceH = height;

    downShader->use();
    for (int i = 0; i < bloomLevels; ++i)
    {
        const int w = std::max (1, sourceW / 2), h = std::max (1, sourceH / 2);
        bloom[(size_t) i].ensure (w, h);
        bloom[(size_t) i].bind();
        downShader->setTexture ("uSource", 4, source);
        downShader->set ("uTexel", 1.0f / (float) sourceW, 1.0f / (float) sourceH);
        downShader->set ("uFirst", i == 0 ? 1 : 0);
        downShader->set ("uThreshold", bloomThreshold);
        dev.drawFullscreen();
        source = bloom[(size_t) i].texture();
        sourceW = w;
        sourceH = h;
    }

    upShader->use();
    gfx::Device::setBlend (gfx::Device::Blend::additive);
    for (int i = bloomLevels - 1; i > 0; --i)
    {
        const auto& src = bloom[(size_t) i];
        bloom[(size_t) i - 1].bind();
        upShader->setTexture ("uSource", 4, src.texture());
        upShader->set ("uTexel", 1.0f / (float) src.width(), 1.0f / (float) src.height());
        dev.drawFullscreen();
    }
    gfx::Device::setBlend (gfx::Device::Blend::none);
}

void Renderer::present (int measure, const FrameSettings&) noexcept
{
    presentShader->use();
    presentShader->setTexture ("uScene", 4, finalScene);
    presentShader->setTexture ("uBloom", 5, bloom[0].texture());
    presentShader->setTexture ("uBlueNoise", 6, noiseTex.texture());
    presentShader->set ("uBloomStrength", bloomStrength);
    presentShader->set ("uExposure", exposure);
    presentShader->set ("uTonemap", (int) tonemap);
    presentShader->set ("uMeasure", measure);
    presentShader->set ("uGain", limiterGain);
    presentShader->set ("uRedDesaturation", redDesaturation);
    glUniform2i (presentShader->location ("uNoiseOffset"), (int) ((frameCounter * 37u) & 63u), (int) ((frameCounter * 23u) & 63u));
    dev.drawFullscreen();
}

void Renderer::measureAndLimit (float dt, const FrameSettings& settings) noexcept
{
    unmeasuredSeconds += dt;

    // What the viewer would see without the limiter, averaged down to a 4 x 4 grid.
    measureTarget.ensure (measureSize, measureSize, GL_RGBA16F, true, true);
    measureTarget.bind();
    present (1, settings);
    measureTarget.generateMipmaps();

    const int slot = measureWrite;
    measureWrite = (measureWrite + 1) % (int) fences.size();
    if (fences[(size_t) slot] != nullptr)
        glDeleteSync (fences[(size_t) slot]);   // never read in time: drop it

    glBindBuffer (GL_PIXEL_PACK_BUFFER, pixelBuffers[(size_t) slot]);
    glBindTexture (GL_TEXTURE_2D, measureTarget.texture());
    glGetTexImage (GL_TEXTURE_2D, measureLevel, GL_RGBA, GL_FLOAT, nullptr);
    glBindBuffer (GL_PIXEL_PACK_BUFFER, 0);
    fences[(size_t) slot] = glFenceSync (GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    const auto consume = [this, &settings] (int index)
    {
        float luminance[16], redness[16];
        glBindBuffer (GL_PIXEL_PACK_BUFFER, pixelBuffers[(size_t) index]);
        if (const auto* data = static_cast<const float*> (glMapBufferRange (GL_PIXEL_PACK_BUFFER, 0, 16 * 4 * sizeof (float), GL_MAP_READ_BIT)))
        {
            for (int c = 0; c < 16; ++c)
            {
                luminance[c] = data[c * 4];
                redness[c] = data[c * 4 + 1];
            }
            glUnmapBuffer (GL_PIXEL_PACK_BUFFER);

            const auto result = limiter.process (luminance, redness, unmeasuredSeconds, settings.flashMode);
            limiterGain = result.gain;
            redDesaturation = result.redDesaturation;
            unmeasuredSeconds = 0.0f;
        }
        glBindBuffer (GL_PIXEL_PACK_BUFFER, 0);
        glDeleteSync (fences[(size_t) index]);
        fences[(size_t) index] = nullptr;
    };

    if (settings.exact)
    {
        // Offline: wait, so the limit applies to the frame it was measured on.
        glClientWaitSync (fences[(size_t) slot], GL_SYNC_FLUSH_COMMANDS_BIT, 2'000'000'000ull);
        for (int k = 1; k < (int) fences.size(); ++k)   // discard anything older
        {
            const int older = (slot + k) % (int) fences.size();
            if (fences[(size_t) older] != nullptr) { glDeleteSync (fences[(size_t) older]); fences[(size_t) older] = nullptr; }
        }
        consume (slot);
        return;
    }

    // Live: use whatever has finished, oldest first, without ever waiting on the GPU.
    for (int k = 1; k <= (int) fences.size(); ++k)
    {
        const int index = (slot + k) % (int) fences.size();
        if (fences[(size_t) index] == nullptr)
            continue;

        const GLenum status = glClientWaitSync (fences[(size_t) index], 0, 0);
        if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED)
            break;

        consume (index);
    }
}

void Renderer::collectGpuTime() noexcept
{
    for (size_t q = 0; q < queries.size(); ++q)
    {
        if (! queryPending[q])
            continue;

        GLint available = 0;
        glGetQueryObjectiv (queries[q], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == 0)
            continue;

        GLuint64 nanoseconds = 0;
        glGetQueryObjectui64v (queries[q], GL_QUERY_RESULT, &nanoseconds);
        const float ms = (float) ((double) nanoseconds * 1.0e-6);
        gpuMs = gpuMs <= 0.0f ? ms : gpuMs + (ms - gpuMs) * 0.1f;
        queryPending[q] = false;
    }
}

void Renderer::adapt (float dt) noexcept
{
    // Over budget for a second: drop the internal resolution a step. Comfortably under for three
    // seconds: go back up (North Star Q3).
    if (gpuMs > frameBudgetMs)            { overBudget += dt; underBudget = 0.0f; }
    else if (gpuMs < frameBudgetMs * 0.55f) { underBudget += dt; overBudget = 0.0f; }
    else                                  { overBudget = underBudget = 0.0f; }

    if (overBudget > 1.0f && renderScale > 0.5f)
    {
        renderScale = renderScale > 0.8f ? 0.75f : 0.5f;
        overBudget = 0.0f;
    }
    else if (underBudget > 3.0f && renderScale < 1.0f)
    {
        renderScale = renderScale < 0.6f ? 0.75f : 1.0f;
        underBudget = 0.0f;
    }
}

const Palette& Renderer::paletteFor (const WidgetSettings& w) const noexcept
{
    return (paletteOverride != nullptr && ! w.lockPalette) ? *paletteOverride : factoryPalette (w.palette);
}

void Renderer::loadLook (int index)
{
    loadedLook = index;
    const auto& look = factoryLook (index);

    LayerSpec feedback;
    feedback.type = LayerType::feedback;
    feedback.amount = 0.0f;
    feedback.p = { 0.35f, 0.5f, 0.55f, 0.5f };

    LayerSpec glitch;
    glitch.type = LayerType::glitch;
    glitch.amount = 0.0f;
    glitch.p = { 0.4f, 0.8f, 0.5f, 0.5f };

    LayerSpec chromatic;
    chromatic.type = LayerType::chromatic;
    chromatic.amount = 0.0f;
    chromatic.p = { 1.0f, 0.5f, 0.5f, 0.5f };

    globalBase.clear();
    lookBloom = 0.0f;
    bloomThreshold = 0.0f;

    feedbackSlot = globalBase.size();
    globalBase.push_back (feedback);

    for (const auto& l : look.layers)
    {
        if (l.type == LayerType::bloom)
        {
            lookBloom = l.amount * 0.2f;
            bloomThreshold = l.p[0] * 1.5f;
            continue;
        }
        globalBase.push_back (l);
    }

    glitchSlot = globalBase.size();
    globalBase.push_back (glitch);
    chromaticSlot = globalBase.size();
    globalBase.push_back (chromatic);

    globalCurrent = globalBase;
    globalChain.release();
    globalChain.prepare (globalBase.size());
}

void Renderer::applyModulation() noexcept
{
    for (auto& w : widgets)
    {
        w->current = w->spec.settings;
        std::copy (w->spec.layers.begin(), w->spec.layers.end(), w->layersCurrent.begin());
    }
    std::copy (globalBase.begin(), globalBase.end(), globalCurrent.begin());
    bloomStrength = bloomBase + lookBloom;
    exposure = exposureBase;

    router.forEach ([this] (const ModTarget& t, float v)
    {
        using K = ModTarget::Kind;
        const auto bump = [v] (float& value, float lo, float hi) { value = std::clamp (value + v * (hi - lo), lo, hi); };

        switch (t.kind)
        {
            case K::sceneBloom:    bump (bloomStrength, 0.0f, 0.5f); return;
            case K::sceneExposure: bump (exposure, 0.25f, 4.0f); return;
            case K::fxFeedback:    bump (globalCurrent[feedbackSlot].amount, 0.0f, 1.0f); return;
            case K::fxGlitch:      bump (globalCurrent[glitchSlot].amount, 0.0f, 1.0f); return;
            case K::fxChromatic:   bump (globalCurrent[chromaticSlot].amount, 0.0f, 1.0f); return;
            case K::widgetIntensity: case K::widgetSpeed: case K::widgetScale:
            case K::widgetColorShift: case K::widgetParam: break;
        }

        for (size_t i = 0; i < widgets.size(); ++i)
        {
            if (t.widget >= 0 && (size_t) t.widget != i)
                continue;

            auto& s = widgets[i]->current;
            switch (t.kind)
            {
                case K::widgetIntensity:  bump (s.intensity, 0.0f, 2.0f); break;
                case K::widgetSpeed:      bump (s.speed, 0.0f, 4.0f); break;
                case K::widgetScale:      bump (s.scale, 0.25f, 3.0f); break;
                case K::widgetColorShift: s.colorShift = s.colorShift + v - std::floor (s.colorShift + v); break;   // wraps around
                case K::widgetParam:      bump (s.p[(size_t) std::clamp (t.index, 0, 15)], 0.0f, 1.0f); break;
                case K::sceneBloom: case K::sceneExposure: case K::fxFeedback: case K::fxGlitch: case K::fxChromatic: break;
            }
        }
    });
}

GLuint Renderer::applyLayers (LayerChain& chain, const std::vector<LayerSpec>& layers, GLuint input,
                              int width, int height, const WidgetSettings& settings) noexcept
{
    GLuint current = input;
    int flip = 0;

    for (size_t i = 0; i < layers.size(); ++i)
    {
        const auto& l = layers[i];
        auto* shader = layerShaders[(size_t) l.type];
        if (shader == nullptr || l.amount <= 0.001f)
            continue;

        auto& out = chain.pingPong[(size_t) flip];
        out.ensure (width, height);
        out.bind();
        gfx::Device::setBlend (gfx::Device::Blend::none);
        shader->use();

        WidgetSettings s = settings;
        if (l.palette >= 0)
        {
            s.palette = l.palette;
            s.lockPalette = true;
            s.colorShift = 0.0f;
        }
        bindCommon (*shader, s, width, height);
        shader->setTexture ("uSource", 4, current);
        shader->set ("uAmount", l.amount);
        shader->setFloats ("uL", l.p.data(), 4);

        gfx::RenderTarget* history = nullptr;
        if (l.type == LayerType::feedback && i < chain.history.size())
        {
            history = chain.history[i].get();
            history->ensure (width, height);
            shader->setTexture ("uHistory", 5, history->texture());
        }

        dev.drawFullscreen();

        if (history != nullptr)   // keep this frame for the next one
        {
            history->bind();
            drawTexture (out.texture(), 1.0f);
        }

        current = out.texture();
        flip = 1 - flip;
    }

    return current;
}

void Renderer::render (const RenderInputs& inputs, int canvasWidth, int canvasHeight,
                       GLuint outputFramebuffer, int vx, int vy, int vw, int vh, const FrameSettings& settings)
{
    if (presentShader == nullptr)
        return;

    in = &inputs;
    masterIntensity = settings.masterIntensity;
    macros = settings.macros;
    paletteOverride = settings.paletteOverride;
    ++frameCounter;

    collectGpuTime();
    if (settings.exact)
        renderScale = 1.0f;
    else
        adapt (inputs.dt);

    const int cw = std::max (16, (int) std::lround ((float) canvasWidth * renderScale));
    const int ch = std::max (16, (int) std::lround ((float) canvasHeight * renderScale));

    timing = ! queryPending[(size_t) queryWrite];
    if (timing)
        glBeginQuery (GL_TIME_ELAPSED, queries[(size_t) queryWrite]);

    if (settings.look != loadedLook)
        loadLook (settings.look);

    router.update (inputs, macros, inputs.dt);
    applyModulation();
    updateDataTextures (inputs);

    // Each widget's clocks advance at its own (modulated) Speed, so speed changes never jump.
    const float motionStep = std::max (inputs.motion - lastMotion, 0.0f);
    lastMotion = inputs.motion;

    for (auto& w : widgets)
    {
        w->clock += inputs.dt * w->current.speed;
        w->motion += motionStep * w->current.speed;
        w->current.clock = w->clock;
        w->current.motionClock = w->motion;

        w->target.ensure (std::max (8, (int) std::lround (w->spec.w * (float) cw)),
                          std::max (8, (int) std::lround (w->spec.h * (float) ch)));
        w->module->render (*this, w->target, w->current);
        w->output = applyLayers (w->chain, w->layersCurrent, w->target.texture(), w->target.width(), w->target.height(), w->current);
    }

    WidgetSettings global;
    global.palette = backgroundPalette;
    global.clock = inputs.time;
    global.motionClock = inputs.motion;
    const Rgb background = paletteFor (global).linear (5);

    sceneTarget.ensure (cw, ch);
    sceneTarget.clear (background.r, background.g, background.b, 1.0f);

    for (auto& w : widgets)
    {
        glViewport ((int) std::lround (w->spec.x * (float) cw),
                    (int) std::lround ((1.0f - w->spec.y - w->spec.h) * (float) ch),
                    w->target.width(), w->target.height());
        gfx::Device::setBlend (toDeviceBlend (w->spec.blend));
        drawTexture (w->output, w->spec.opacity);
    }
    gfx::Device::setBlend (gfx::Device::Blend::none);

    finalScene = applyLayers (globalChain, globalCurrent, sceneTarget.texture(), cw, ch, global);

    renderBloom (finalScene, cw, ch);
    measureAndLimit (inputs.dt, settings);

    gfx::Device::bindBackbuffer (outputFramebuffer, vx, vy, vw, vh);
    present (0, settings);

    if (timing)
    {
        glEndQuery (GL_TIME_ELAPSED);
        queryPending[(size_t) queryWrite] = true;
        queryWrite = (queryWrite + 1) % (int) queries.size();
    }

    statsOut.gpuMs = gpuMs;
    statsOut.renderScale = renderScale;
    statsOut.flashes = limiter.flashesInLastSecond();
    statsOut.limiterGain = limiterGain;
}

} // namespace cortex

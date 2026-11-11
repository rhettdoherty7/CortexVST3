// Scope (module 2) and Waveform Stack (module 4): waveforms drawn as anti-aliased capsule chains.

#include "Module.h"

#include <algorithm>
#include <cmath>

namespace cortex
{

using namespace juce::gl;

namespace
{
    constexpr int segments = 255;

    /** Scope: phosphor oscilloscope. p0 mode (< 0.5 time, >= 0.5 X/Y), p1 persistence, p2 gain. */
    class Scope final : public Module
    {
    public:
        const char* id() const noexcept override   { return "scope"; }

        bool init (Renderer& r, std::string& log) override
        {
            line = r.program ("line.vert.glsl", "line.frag.glsl", log);
            wave.ensure (256, 2, GL_R32F, GL_RED, false);
            return line != nullptr;
        }

        void render (Renderer& r, gfx::RenderTarget& target, const WidgetSettings& s) override
        {
            const auto& in = r.inputs();
            const bool xy = s.p[0] >= 0.5f;

            if (xy)
            {
                std::copy (std::begin (in.waveL), std::end (in.waveL), staging.begin());
                std::copy (std::begin (in.waveR), std::end (in.waveR), staging.begin() + 256);
            }
            else
            {
                triggered (in);
            }
            wave.upload (staging.data());

            // Phosphor: what was drawn before decays; persistence 40 to 500 ms.
            const float persistence = 0.04f + 0.46f * s.p[1];
            r.fade (target, std::exp (-in.dt * s.speed / persistence));

            const float px = (float) target.height() / 1080.0f;
            target.bind();
            gfx::Device::setBlend (gfx::Device::Blend::maximum);
            line->use();
            r.bindCommon (*line, s, target.width(), target.height());
            line->setTexture ("uWave", 1, wave.texture());
            line->set ("uMode", xy ? 1 : 0);
            line->set ("uThickness", 1.2f * px);
            line->set ("uGlow", 13.0f * px);
            line->set ("uAmp", (0.6f + 1.6f * s.p[2]) * s.scale);
            line->set ("uLayers", 1.0f);
            r.device().drawInstancedQuads (xy ? segments : segments * 2);
            gfx::Device::setBlend (gfx::Device::Blend::none);
        }

    private:
        /** Time mode: start at a rising zero crossing so the trace stands still on steady tones. */
        void triggered (const RenderInputs& in) noexcept
        {
            const auto mono = [&in] (int i) { return 0.5f * (in.waveL[i] + in.waveR[i]); };

            int start = 0;
            float steepest = 0.0f;
            for (int i = 1; i < 96; ++i)
            {
                if (mono (i - 1) < 0.0f && mono (i) >= 0.0f && mono (i) - mono (i - 1) > steepest)
                {
                    steepest = mono (i) - mono (i - 1);
                    start = i;
                }
            }

            constexpr float span = 160.0f;
            for (int k = 0; k < 256; ++k)
            {
                const float x = std::min ((float) start + (float) k * span / 255.0f, 255.0f);
                const int i0 = std::min ((int) x, 254);
                const float f = x - (float) i0;
                staging[(size_t) k]       = in.waveL[i0] + (in.waveL[i0 + 1] - in.waveL[i0]) * f;
                staging[(size_t) k + 256] = in.waveR[i0] + (in.waveR[i0 + 1] - in.waveR[i0]) * f;
            }
        }

        gfx::Shader* line = nullptr;
        gfx::DataTexture wave;
        std::array<float, 512> staging {};
    };

    /** Waveform Stack: recent waveforms receding into the distance. p0 gain. */
    class WaveformStack final : public Module
    {
    public:
        const char* id() const noexcept override   { return "waveform_stack"; }

        bool init (Renderer& r, std::string& log) override
        {
            line = r.program ("line.vert.glsl", "line.frag.glsl", log);
            return line != nullptr;
        }

        void render (Renderer& r, gfx::RenderTarget& target, const WidgetSettings& s) override
        {
            constexpr int layers = 28;
            const float px = (float) target.height() / 1080.0f;

            target.clear();
            gfx::Device::setBlend (gfx::Device::Blend::maximum);
            line->use();
            r.bindCommon (*line, s, target.width(), target.height());
            line->set ("uMode", 2);
            line->set ("uThickness", 1.0f * px);
            line->set ("uGlow", 9.0f * px);
            line->set ("uAmp", (0.5f + 2.0f * s.p[0]) * s.scale);
            line->set ("uLayers", (float) layers);
            r.device().drawInstancedQuads (segments * layers);
            gfx::Device::setBlend (gfx::Device::Blend::none);
        }

    private:
        gfx::Shader* line = nullptr;
    };
}

std::unique_ptr<Module> makeScope()           { return std::make_unique<Scope>(); }
std::unique_ptr<Module> makeWaveformStack()   { return std::make_unique<WaveformStack>(); }

} // namespace cortex

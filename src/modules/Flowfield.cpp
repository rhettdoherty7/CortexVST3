// Flowfield (module 5): about 100k particles carried by a curl-noise field, simulated on the GPU.
// p0 field scale, p1 flow speed, p2 trail length, p3 brightness.

#include "Module.h"

#include <cmath>

namespace cortex
{

using namespace juce::gl;

namespace
{
    class Flowfield final : public Module
    {
    public:
        const char* id() const noexcept override   { return "flowfield"; }

        bool init (Renderer& r, std::string& log) override
        {
            sim    = r.program ("fullscreen.vert.glsl", "flow_sim.frag.glsl", log);
            points = r.program ("flow_points.vert.glsl", "flow_points.frag.glsl", log);
            for (auto& s : state)
                s.ensure (side, side, GL_RGBA32F, false);   // zeroed = every particle dead, respawned on frame one
            return sim != nullptr && points != nullptr;
        }

        void render (Renderer& r, gfx::RenderTarget& target, const WidgetSettings& s) override
        {
            const auto& in = r.inputs();
            const float aspect = (float) target.width() / (float) target.height();
            auto& current = state[(size_t) flip];
            auto& next = state[(size_t) (1 - flip)];

            // 1. Move every particle.
            next.bind();
            gfx::Device::setBlend (gfx::Device::Blend::none);
            sim->use();
            r.bindCommon (*sim, s, side, side);
            sim->setTexture ("uState", 4, current.texture());
            sim->set ("uAspect", aspect);
            sim->set ("uFieldScale", (0.6f + 2.6f * s.p[0]) / s.scale);
            sim->set ("uFlowSpeed", 0.4f + 1.6f * s.p[1]);
            r.device().drawFullscreen();
            flip = 1 - flip;

            // 2. Fade the trails, then add the particles on top.
            const float trail = 0.05f + 0.6f * s.p[2];
            const float keep = std::exp (-in.dt * s.speed / trail);
            r.fade (target, keep);

            target.bind();
            gfx::Device::setBlend (gfx::Device::Blend::additive);
            points->use();
            r.bindCommon (*points, s, target.width(), target.height());
            points->setTexture ("uState", 4, next.texture());
            glUniform2i (points->location ("uStateSize"), side, side);
            points->set ("uAspect", aspect);
            points->set ("uPointSize", 2.4f * (float) target.height() / 1080.0f);
            // Steady-state brightness is (added per frame) / (1 - keep), so scale by (1 - keep).
            points->set ("uBrightness", (0.2f + 1.2f * s.p[3]) * std::max (1.0f - keep, 0.002f));
            r.device().drawPoints (side * side);
            gfx::Device::setBlend (gfx::Device::Blend::none);
        }

    private:
        static constexpr int side = 320;   // 102,400 particles

        gfx::Shader *sim = nullptr, *points = nullptr;
        std::array<gfx::RenderTarget, 2> state;
        int flip = 0;
    };
}

std::unique_ptr<Module> makeFlowfield()   { return std::make_unique<Flowfield>(); }

} // namespace cortex

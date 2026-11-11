// Modules that are a single fragment shader over the whole widget.

#include "Module.h"

namespace cortex
{

namespace
{
    class FullscreenModule : public Module
    {
    public:
        FullscreenModule (const char* moduleId, const char* fragmentFile) : identifier (moduleId), fragment (fragmentFile) {}

        const char* id() const noexcept override   { return identifier; }

        bool init (Renderer& r, std::string& log) override
        {
            shader = r.program ("fullscreen.vert.glsl", fragment, log);
            return shader != nullptr;
        }

        void render (Renderer& r, gfx::RenderTarget& target, const WidgetSettings& s) override
        {
            r.drawFullscreen (*shader, target, s);
        }

    private:
        const char* identifier;
        const char* fragment;
        gfx::Shader* shader = nullptr;
    };
}

std::unique_ptr<Module> makeSpectrumBars()   { return std::make_unique<FullscreenModule> ("spectrum_bars", "spectrum_bars.frag.glsl"); }
std::unique_ptr<Module> makeBloomOrb()       { return std::make_unique<FullscreenModule> ("bloom_orb", "bloom_orb.frag.glsl"); }

} // namespace cortex

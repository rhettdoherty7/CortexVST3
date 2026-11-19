#pragma once

#include <engine/Renderer.h>

#include <memory>
#include <string>

namespace cortex
{

/** A visual (section 5.2). One instance per widget, so a module can keep per-widget state such as
    trails or particles. Shaders come from the renderer's shared cache.

    Departs from the spec's interface: update() and render() are one call, and parameters arrive
    as WidgetSettings each frame (docs/DECISIONS.md D-015). */
class Module
{
public:
    virtual ~Module() = default;

    virtual const char* id() const noexcept = 0;

    /** GL thread, once, before the first render. */
    virtual bool init (Renderer&, std::string& log) = 0;

    /** GL thread, every frame. Draws into `target` (premultiplied linear HDR). The target keeps
        its contents between frames, so a module either covers every pixel, clears, or fades. */
    virtual void render (Renderer&, gfx::RenderTarget& target, const WidgetSettings&) = 0;
};

std::unique_ptr<Module> createModule (const std::string& id);

std::unique_ptr<Module> makeSpectrumBars();
std::unique_ptr<Module> makeBloomOrb();
std::unique_ptr<Module> makePulseGrid();
std::unique_ptr<Module> makeScope();
std::unique_ptr<Module> makeWaveformStack();
std::unique_ptr<Module> makeFlowfield();

} // namespace cortex

#include "Module.h"

namespace cortex
{

std::unique_ptr<Module> createModule (const std::string& id)
{
    if (id == "spectrum_bars")   return makeSpectrumBars();
    if (id == "bloom_orb")       return makeBloomOrb();
    if (id == "pulse_grid")      return makePulseGrid();
    if (id == "scope")           return makeScope();
    if (id == "waveform_stack")  return makeWaveformStack();
    if (id == "flowfield")       return makeFlowfield();
    return nullptr;
}

} // namespace cortex

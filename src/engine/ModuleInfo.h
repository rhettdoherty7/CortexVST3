#pragma once

#include "Scene.h"

#include <string>
#include <vector>

namespace cortex
{

/** What the inspector shows for one module parameter (WidgetSettings::p[index]). A parameter with
    choices stores the chosen index as its value; otherwise it runs 0..1. */
struct ModuleParamInfo
{
    const char* name = "";
    float defaultValue = 0.5f;
    std::vector<const char*> choices;
};

struct ModuleInfo
{
    const char* id = "";
    const char* name = "";
    const char* description = "";
    std::vector<ModuleParamInfo> params;
};

/** Every module, in the order the module rail shows them. No OpenGL: safe on the message thread. */
const std::vector<ModuleInfo>& moduleInfos() noexcept;
const ModuleInfo* findModuleInfo (const std::string& id) noexcept;

/** A full-canvas widget of `moduleId` with that module's default parameters. */
WidgetSpec defaultWidget (const std::string& moduleId, int palette);

} // namespace cortex

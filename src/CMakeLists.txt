# --- Analysis core -----------------------------------------------------------------------------
# An INTERFACE library: the sources compile into whichever JUCE target links it (the plugin and
# the test runner), because JUCE modules are themselves source-only.

add_library(cortex_analysis INTERFACE)
target_sources(cortex_analysis INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/analysis/AnalysisFrame.h
    ${CMAKE_CURRENT_SOURCE_DIR}/analysis/FrameFifo.h
    ${CMAKE_CURRENT_SOURCE_DIR}/analysis/AnalysisEngine.h
    ${CMAKE_CURRENT_SOURCE_DIR}/analysis/AnalysisEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/analysis/BandSplitter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/analysis/BandSplitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/analysis/OnsetDetector.h
    ${CMAKE_CURRENT_SOURCE_DIR}/analysis/OnsetDetector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/analysis/PitchTracker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/analysis/PitchTracker.cpp)
target_include_directories(cortex_analysis INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cortex_analysis INTERFACE juce::juce_dsp)

# --- Engine core (no OpenGL; unit tested) ----------------------------------------------------

add_library(cortex_engine_core INTERFACE)
target_sources(cortex_engine_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/Palette.h
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/Palette.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/FrameInterpolator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/FrameInterpolator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/Scene.h
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/Scene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/Layers.h
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/Layers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ModRouter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ModRouter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/presets/CoverArtPalette.h
    ${CMAKE_CURRENT_SOURCE_DIR}/presets/CoverArtPalette.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/presets/PresetFormat.h
    ${CMAKE_CURRENT_SOURCE_DIR}/presets/PresetFormat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/presets/PresetManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/presets/PresetManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ModuleInfo.h
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ModuleInfo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/safety/FlashLimiter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/safety/FlashLimiter.cpp)
target_link_libraries(cortex_engine_core INTERFACE cortex_analysis)

# --- Shaders, embedded as binary data ---------------------------------------------------------

file(GLOB CORTEX_SHADER_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/shaders/*.glsl)
juce_add_binary_data(cortex_shaders
    HEADER_NAME CortexShaders.h
    NAMESPACE   CortexShaders
    SOURCES     ${CORTEX_SHADER_FILES})

# --- Render engine and modules (OpenGL 4.1) ---------------------------------------------------

add_library(cortex_render INTERFACE)
target_sources(cortex_render INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/gfx/Device.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gfx/Device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/Renderer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/Renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/Module.h
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/ModuleRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/FullscreenModules.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/PulseGrid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/LineModules.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/Flowfield.cpp)
# Debug builds read shaders straight from the source folder so edits show up live.
target_compile_definitions(cortex_render INTERFACE CORTEX_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders")
target_link_libraries(cortex_render INTERFACE cortex_engine_core cortex_shaders juce::juce_opengl)

# --- Interface resources: fonts (resources/fonts, SIL OFL) and module thumbnails ------------------

file(GLOB CORTEX_UI_FILES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/resources/fonts/*.ttf
    ${CMAKE_SOURCE_DIR}/resources/thumbnails/*.png)
juce_add_binary_data(cortex_ui_data
    HEADER_NAME CortexUiData.h
    NAMESPACE   CortexUiData
    SOURCES     ${CORTEX_UI_FILES})

# --- Plugin ------------------------------------------------------------------------------------

set(CORTEX_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND CORTEX_FORMATS AU)
endif()

juce_add_plugin(Cortex
    VERSION                     ${PROJECT_VERSION}
    COMPANY_NAME                "RD Audio"
    BUNDLE_ID                   com.rdaudio.cortex
    PLUGIN_MANUFACTURER_CODE    Rdau
    PLUGIN_CODE                 Crtx
    FORMATS                     ${CORTEX_FORMATS}
    PRODUCT_NAME                "Cortex"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Analyzer
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     ${CORTEX_COPY_AFTER_BUILD}
    VST3_COPY_DIR               "${CORTEX_VST3_DIR}")

target_sources(Cortex PRIVATE
    core/Version.h
    core/Params.h
    core/Params.cpp
    core/PluginProcessor.h
    core/PluginProcessor.cpp
    core/PluginEditor.h
    core/PluginEditor.cpp
    ui/CanvasComponent.h
    ui/CanvasComponent.cpp
    ui/Theme.h
    ui/Theme.cpp
    ui/ModuleRail.h
    ui/ModuleRail.cpp
    ui/Inspector.h
    ui/Inspector.cpp
    ui/PresetBrowser.h
    ui/PresetBrowser.cpp)

target_compile_definitions(Cortex PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    CORTEX_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(Cortex
    PRIVATE
        cortex_analysis
        cortex_render
        cortex_ui_data
        cortex_warnings
        juce::juce_audio_utils
        juce::juce_opengl
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

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
    NEEDS_MIDI_INPUT            FALSE
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
    core/PluginEditor.cpp)

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
        cortex_warnings
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

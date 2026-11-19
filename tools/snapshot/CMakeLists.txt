juce_add_gui_app(cortex_snapshot PRODUCT_NAME "cortex_snapshot")

target_sources(cortex_snapshot PRIVATE Main.cpp)
target_include_directories(cortex_snapshot PRIVATE ${CMAKE_SOURCE_DIR}/tests)

target_compile_definitions(cortex_snapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="cortex_snapshot"
    JUCE_APPLICATION_VERSION_STRING="1.0")

target_link_libraries(cortex_snapshot
    PRIVATE
        cortex_analysis
        cortex_render
        cortex_warnings
        juce::juce_gui_extra
        juce::juce_opengl
        juce::juce_dsp
        juce::juce_recommended_config_flags)

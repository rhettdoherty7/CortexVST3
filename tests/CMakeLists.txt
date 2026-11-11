juce_add_console_app(cortex_tests PRODUCT_NAME "cortex_tests")

target_sources(cortex_tests PRIVATE
    TestSignals.h
    AnalysisTests.cpp
    EngineTests.cpp)

target_compile_definitions(cortex_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(cortex_tests
    PRIVATE
        cortex_analysis
        cortex_engine_core
        cortex_warnings
        Catch2::Catch2WithMain
        juce::juce_dsp
        juce::juce_recommended_config_flags)

add_test(NAME cortex_tests COMMAND cortex_tests)

# Third-party code, fetched at configure time and pinned to exact releases.
# Same pins as Interlock. Decision: docs/DECISIONS.md D-001.
#
# To reuse an existing checkout instead of downloading, configure with
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=<path> -DFETCHCONTENT_SOURCE_DIR_CATCH2=<path>

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    ON
    GIT_PROGRESS   ON)
FetchContent_MakeAvailable(JUCE)

if(CORTEX_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.0
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(Catch2)
endif()

cmake_minimum_required(VERSION 3.22)

project(Cortex VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static MSVC runtime: the plugin DLL does not depend on a separately installed VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(CORTEX_BUILD_TESTS "Build the Catch2 unit tests" ON)
option(CORTEX_COPY_AFTER_BUILD "Copy Cortex.vst3 into the system VST3 folder after each build" OFF)
set(CORTEX_VST3_DIR "$ENV{CommonProgramFiles}/VST3" CACHE PATH "Folder the post-build step copies Cortex.vst3 into")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
include(Warnings)

add_subdirectory(src)
add_subdirectory(tools/snapshot)

if(CORTEX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

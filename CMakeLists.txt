cmake_minimum_required(VERSION 3.25)

project(AnimalEngine
    VERSION 0.7.0
    DESCRIPTION "Animal-inspired 2D exploration game engine"
    LANGUAGES C CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use a preset: cmake --preset <name>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(ANIMAL_BUILD_TESTS "Build unit tests" ON)
option(ANIMAL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in project targets" OFF)
option(ANIMAL_ENABLE_VULKAN "Build the Vulkan RHI backend" ON)
option(ANIMAL_DEV_TOOLS "Build the developer tools (debug UI, hot reload, level editor) into the game" ON)

if(ANIMAL_BUILD_TESTS)
    enable_testing() # before the subdirectories: game/ adds the headless playthrough test
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(AnimalCompileOptions)

# Release binaries must not carry this machine's paths (ADR-0037): __FILE__ in assertions and logs
# (ours and SDL's) is made relative to the source folder. Debug builds keep full paths for debuggers.
if(MSVC)
    file(TO_NATIVE_PATH "${PROJECT_SOURCE_DIR}/" ANIMAL_SOURCE_PREFIX)
    add_compile_options("$<$<CONFIG:Release,MinSizeRel>:/d1trimfile:${ANIMAL_SOURCE_PREFIX}>")
else()
    add_compile_options("$<$<CONFIG:Release,MinSizeRel>:-ffile-prefix-map=${PROJECT_SOURCE_DIR}/=>")
endif()

add_subdirectory(external)

# Single- and multi-config generators share one layout: build/<preset>/bin/<Config>/
set(ANIMAL_OUTPUT_DIR "${PROJECT_BINARY_DIR}/bin/$<CONFIG>")
set(ANIMAL_DATA_DIR "${ANIMAL_OUTPUT_DIR}/data")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ANIMAL_OUTPUT_DIR}")

include(AnimalData)

add_subdirectory(tools/shaderc)
add_subdirectory(shaders)
add_subdirectory(engine)
add_subdirectory(framework)
if(ANIMAL_DEV_TOOLS)
    add_subdirectory(tools/devtools)
endif()
add_subdirectory(game)
add_subdirectory(tools/rhi_conformance)
add_subdirectory(tools/asset_cooker)

if(ANIMAL_BUILD_TESTS)
    add_subdirectory(tests)
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT animal_game)

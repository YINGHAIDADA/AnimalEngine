cmake_minimum_required(VERSION 3.25)

project(AnimalEngine
    VERSION 0.1.0
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

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(AnimalCompileOptions)

add_subdirectory(external)

# Single- and multi-config generators share one layout: build/<preset>/bin/<Config>/
set(ANIMAL_OUTPUT_DIR "${PROJECT_BINARY_DIR}/bin/$<CONFIG>")
set(ANIMAL_DATA_DIR "${ANIMAL_OUTPUT_DIR}/data")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ANIMAL_OUTPUT_DIR}")

include(AnimalData)

add_subdirectory(tools/shaderc)
add_subdirectory(shaders)



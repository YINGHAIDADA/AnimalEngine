# Third-party dependencies. Versions are pinned by git submodules; see external/README.md.

function(animal_require_submodule dir)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt")
        message(FATAL_ERROR
            "Missing submodule external/${dir}. Run: git submodule update --init --depth 1")
    endif()
endfunction()

function(animal_set_folder folder)
    foreach(target IN LISTS ARGN)
        if(TARGET ${target})
            set_target_properties(${target} PROPERTIES FOLDER "${folder}")
        endif()
    endforeach()
endfunction()

# --- SDL3 (static) -----------------------------------------------------------
animal_require_submodule(SDL)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
# Native HID backends are used on macOS / Windows; never pick up a system libusb.
set(SDL_HIDAPI_LIBUSB OFF CACHE BOOL "" FORCE)
add_subdirectory(SDL EXCLUDE_FROM_ALL)
animal_set_folder(external/SDL SDL3-static SDL_uclibc)

# --- glslang (build-time shader compiler only) -------------------------------
animal_require_submodule(glslang)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(glslang EXCLUDE_FROM_ALL)
animal_set_folder(external/glslang
    glslang glslang-default-resource-limits SPIRV GenericCodeGen MachineIndependent OSDependent)

# --- SPIRV-Cross (build-time shader cross-compiler only) ---------------------
animal_require_submodule(SPIRV-Cross)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory(SPIRV-Cross EXCLUDE_FROM_ALL)
animal_set_folder(external/SPIRV-Cross spirv-cross-core spirv-cross-glsl)

# --- Vulkan headers + volk (runtime loader; no Vulkan SDK needed to build) ----
if(ANIMAL_ENABLE_VULKAN)
    animal_require_submodule(Vulkan-Headers)
    animal_require_submodule(volk)
    add_subdirectory(Vulkan-Headers EXCLUDE_FROM_ALL)
    # volk would otherwise take headers from $VULKAN_SDK / FindVulkan (a global install).
    set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "" FORCE)
    set(VOLK_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(volk EXCLUDE_FROM_ALL)
    target_link_libraries(volk PUBLIC Vulkan::Headers)
    animal_set_folder(external/vulkan volk)
    # Vulkan Memory Allocator: header-only; functions come from volk at runtime.
    animal_require_submodule(VulkanMemoryAllocator)
    add_library(animal_vma INTERFACE)
    target_include_directories(animal_vma SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/VulkanMemoryAllocator/include")
    target_compile_definitions(animal_vma INTERFACE VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
    target_link_libraries(animal_vma INTERFACE Vulkan::Headers)
endif()

# --- Header-only / single-file dependencies ----------------------------------
add_library(animal_stb INTERFACE)
target_include_directories(animal_stb SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/stb")

# --- Dear ImGui (developer tools only) -----------------------------------------
if(ANIMAL_DEV_TOOLS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.h")
        message(FATAL_ERROR "Missing submodule external/imgui. Run: git submodule update --init --depth 1")
    endif()
    add_library(animal_imgui STATIC
        imgui/imgui.cpp
        imgui/imgui_demo.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp)
    target_include_directories(animal_imgui SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
    target_compile_definitions(animal_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    target_compile_features(animal_imgui PUBLIC cxx_std_20)
    animal_set_folder(external animal_imgui)
endif()

add_library(animal_json INTERFACE)
target_include_directories(animal_json SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann")

add_library(animal_gl_headers INTERFACE)
target_include_directories(animal_gl_headers SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/gl/include")

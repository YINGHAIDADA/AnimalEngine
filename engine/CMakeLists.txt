set(ANIMAL_ENGINE_HEADERS
    include/animal/app/application.h
    include/animal/app/entry_point.h
    include/animal/core/assert.h
    include/animal/core/file.h
    include/animal/core/handle.h
    include/animal/core/log.h
    include/animal/core/math.h
    include/animal/core/random.h
    include/animal/platform/input.h
    include/animal/platform/platform.h
    include/animal/platform/window.h
    include/animal/renderer/camera2d.h
    include/animal/renderer/lighting.h
    include/animal/renderer/pixel_canvas.h
    include/animal/renderer/renderer2d.h
    include/animal/renderer/sprite_batch.h
    include/animal/resources/loaders.h
    include/animal/rhi/command_list.h
    include/animal/rhi/rhi.h
    include/animal/rhi/shader_package.h)

set(ANIMAL_ENGINE_SOURCES
    src/app/application.cpp
    src/core/assert.cpp
    src/core/file.cpp
    src/core/log.cpp
    src/platform/gamepads.h
    src/platform/input.cpp
    src/platform/platform.cpp
    src/platform/window.cpp
    src/renderer/debug_font.cpp
    src/renderer/debug_font.h
    src/renderer/lighting_pass.cpp
    src/renderer/lighting_pass.h
    src/renderer/pixel_canvas.cpp
    src/renderer/renderer2d.cpp
    src/renderer/sprite_batch.cpp
    src/resources/loaders.cpp
    src/rhi/command_list.cpp
    src/rhi/rhi.cpp
    src/rhi/opengl/gl_device.cpp
    src/rhi/opengl/gl_loader.cpp
    src/rhi/opengl/gl_loader.h)

add_library(animal_engine STATIC ${ANIMAL_ENGINE_HEADERS} ${ANIMAL_ENGINE_SOURCES})
add_library(animal::engine ALIAS animal_engine)
animal_set_compile_options(animal_engine)

target_include_directories(animal_engine
    PUBLIC include
    PRIVATE src)
target_link_libraries(animal_engine
    PUBLIC SDL3::SDL3-static
    PRIVATE animal_stb animal_gl_headers)

if(ANIMAL_ENABLE_VULKAN)
    set(ANIMAL_VULKAN_SOURCES src/rhi/vulkan/vk_device.cpp)
    target_sources(animal_engine PRIVATE ${ANIMAL_VULKAN_SOURCES})
    target_link_libraries(animal_engine PRIVATE volk)
    target_compile_definitions(animal_engine PRIVATE ANIMAL_RHI_VULKAN=1)
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${ANIMAL_VULKAN_SOURCES})
endif()

set_target_properties(animal_engine PROPERTIES FOLDER engine)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${ANIMAL_ENGINE_HEADERS} ${ANIMAL_ENGINE_SOURCES})

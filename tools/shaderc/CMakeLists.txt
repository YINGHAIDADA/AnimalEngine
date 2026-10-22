add_executable(animal_shaderc main.cpp)
animal_set_compile_options(animal_shaderc)
target_include_directories(animal_shaderc PRIVATE "${PROJECT_SOURCE_DIR}/engine/include")
target_link_libraries(animal_shaderc PRIVATE
    glslang
    glslang-default-resource-limits
    spirv-cross-glsl
    spirv-cross-core)
set_target_properties(animal_shaderc PROPERTIES
    FOLDER tools
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/tools/$<CONFIG>")

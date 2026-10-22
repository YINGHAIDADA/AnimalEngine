# RHI conformance (ADR-0025): renders one tile per RHI feature into an offscreen target and saves
# it, so render_regression.py can compare the backends (including features the game never uses).
animal_add_shaders(animal_conformance_shaders
    SOURCES
        conformance.vert
        conformance.frag
        conformance_mrt.frag)

add_executable(animal_rhi_conformance main.cpp)
animal_set_compile_options(animal_rhi_conformance)
target_link_libraries(animal_rhi_conformance PRIVATE animal::engine)
add_dependencies(animal_rhi_conformance animal_conformance_shaders)
set_target_properties(animal_rhi_conformance PROPERTIES OUTPUT_NAME AnimalRhiConformance FOLDER tools)

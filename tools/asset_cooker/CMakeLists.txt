# Asset Cooker (ADR-0031): source assets -> deterministic resource pack + report.
add_library(animal_cooker STATIC cooker.cpp cooker.h)
animal_set_compile_options(animal_cooker)
target_include_directories(animal_cooker PUBLIC .)
target_link_libraries(animal_cooker PUBLIC animal::framework PRIVATE animal_json)
set_target_properties(animal_cooker PROPERTIES FOLDER tools)

add_executable(animal_asset_cooker main.cpp)
animal_set_compile_options(animal_asset_cooker)
target_link_libraries(animal_asset_cooker PRIVATE animal_cooker)
set_target_properties(animal_asset_cooker PROPERTIES OUTPUT_NAME AnimalCooker FOLDER tools)

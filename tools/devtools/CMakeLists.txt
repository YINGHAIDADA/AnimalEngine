# Developer tools (ADR-0026): debug UI, hot reload, editor support. Only built with ANIMAL_DEV_TOOLS.
set(ANIMAL_DEVTOOLS_HEADERS
    include/animal/devtools/console.h
    include/animal/devtools/imgui_layer.h
    include/animal/devtools/json_editor.h
    include/animal/devtools/performance_panel.h
    include/animal/devtools/source_assets.h
    include/animal/devtools/tuning_panel.h)

set(ANIMAL_DEVTOOLS_SOURCES
    src/console.cpp
    src/imgui_layer.cpp
    src/json_editor.cpp
    src/performance_panel.cpp
    src/source_assets.cpp
    src/tuning_panel.cpp)

add_library(animal_devtools STATIC ${ANIMAL_DEVTOOLS_HEADERS} ${ANIMAL_DEVTOOLS_SOURCES})
add_library(animal::devtools ALIAS animal_devtools)
animal_set_compile_options(animal_devtools)
target_include_directories(animal_devtools PUBLIC include)
target_link_libraries(animal_devtools PUBLIC animal::framework animal_imgui animal_json)
# Tools edit the source tree the build came from (SourceAssets).
target_compile_definitions(animal_devtools PRIVATE ANIMAL_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
set_target_properties(animal_devtools PROPERTIES FOLDER tools)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${ANIMAL_DEVTOOLS_HEADERS} ${ANIMAL_DEVTOOLS_SOURCES})

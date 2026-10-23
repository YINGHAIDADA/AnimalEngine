# Developer tools (ADR-0026): debug UI, hot reload, editor support. Only built with ANIMAL_DEV_TOOLS.
set(ANIMAL_DEVTOOLS_HEADERS
    include/animal/devtools/imgui_layer.h)

set(ANIMAL_DEVTOOLS_SOURCES
    src/imgui_layer.cpp)

add_library(animal_devtools STATIC ${ANIMAL_DEVTOOLS_HEADERS} ${ANIMAL_DEVTOOLS_SOURCES})
add_library(animal::devtools ALIAS animal_devtools)
animal_set_compile_options(animal_devtools)
target_include_directories(animal_devtools PUBLIC include)
target_link_libraries(animal_devtools PUBLIC animal::engine animal_imgui)
set_target_properties(animal_devtools PROPERTIES FOLDER tools)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${ANIMAL_DEVTOOLS_HEADERS} ${ANIMAL_DEVTOOLS_SOURCES})

set(ANIMAL_GAME_SOURCES
    src/main.cpp
    src/room_layout.cpp
    src/room_layout.h
    src/sandbox_game.cpp
    src/sandbox_game.h)

add_executable(animal_game ${ANIMAL_GAME_SOURCES})
animal_set_compile_options(animal_game)
target_link_libraries(animal_game PRIVATE animal::framework)

set_target_properties(animal_game PROPERTIES
    OUTPUT_NAME AnimalGame
    FOLDER game
    XCODE_GENERATE_SCHEME ON
    XCODE_SCHEME_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${ANIMAL_GAME_SOURCES})

animal_add_data_files(animal_game_textures
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/assets/source/textures"
    DESTINATION textures
    FILES
        tileset.png
        creature.png)

add_dependencies(animal_game animal_shaders animal_game_textures)

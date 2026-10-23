set(ANIMAL_GAME_SOURCES
    src/cave_room.cpp
    src/cave_room.h
    src/dev/dev_tools.h
    src/main.cpp
    src/sandbox_game.cpp
    src/sandbox_game.h)

if(ANIMAL_DEV_TOOLS)
    list(APPEND ANIMAL_GAME_SOURCES
        src/dev/dev_state.h
        src/dev/dev_tools.cpp
        src/dev/hot_reload.cpp
        src/dev/level_editor.cpp
        src/dev/world_editor.cpp
        src/dev/panels.cpp)
else()
    list(APPEND ANIMAL_GAME_SOURCES src/dev/dev_tools_off.cpp)
endif()

add_executable(animal_game ${ANIMAL_GAME_SOURCES})
animal_set_compile_options(animal_game)
target_include_directories(animal_game PRIVATE src)
if(ANIMAL_DEV_TOOLS)
    target_link_libraries(animal_game PRIVATE animal::devtools) # brings animal::framework
    # Hot reload compiles changed shaders with the build's own compiler.
    target_compile_definitions(animal_game PRIVATE ANIMAL_DEV_TOOLS=1 ANIMAL_SHADERC_PATH="$<TARGET_FILE:animal_shaderc>")
else()
    target_link_libraries(animal_game PRIVATE animal::framework)
endif()

set_target_properties(animal_game PROPERTIES
    OUTPUT_NAME AnimalGame
    FOLDER game
    XCODE_GENERATE_SCHEME ON
    XCODE_SCHEME_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${ANIMAL_GAME_SOURCES})

# Every file under the source folders is deployed (rooms created with the level editor included);
# CONFIGURE_DEPENDS re-runs the glob when files are added or removed.
file(GLOB_RECURSE ANIMAL_GAME_TEXTURE_FILES CONFIGURE_DEPENDS
    RELATIVE "${PROJECT_SOURCE_DIR}/assets/source/textures"
    "${PROJECT_SOURCE_DIR}/assets/source/textures/*.png"
    "${PROJECT_SOURCE_DIR}/assets/source/textures/*.json")
file(GLOB_RECURSE ANIMAL_GAME_WORLD_FILES CONFIGURE_DEPENDS
    RELATIVE "${PROJECT_SOURCE_DIR}/assets/source/world"
    "${PROJECT_SOURCE_DIR}/assets/source/world/*.world"
    "${PROJECT_SOURCE_DIR}/assets/source/world/*.json"
    "${PROJECT_SOURCE_DIR}/assets/source/world/*.tmj"
    "${PROJECT_SOURCE_DIR}/assets/source/world/*.tsj")

animal_add_data_files(animal_game_textures
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/assets/source/textures"
    DESTINATION textures
    FILES ${ANIMAL_GAME_TEXTURE_FILES})

animal_add_data_files(animal_game_world
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/assets/source/world"
    DESTINATION world
    FILES ${ANIMAL_GAME_WORLD_FILES})

add_dependencies(animal_game animal_shaders animal_game_textures animal_game_world)

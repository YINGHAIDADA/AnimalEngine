set(ANIMAL_GAME_SOURCES
    src/cave_room.cpp
    src/cave_room.h
    src/dev/dev_tools.h
    src/main.cpp
    src/map_screen.cpp
    src/mechanisms.cpp
    src/mechanisms.h
    src/progression.cpp
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

file(GLOB_RECURSE ANIMAL_GAME_SOUND_FILES CONFIGURE_DEPENDS
    RELATIVE "${PROJECT_SOURCE_DIR}/assets/source/sounds"
    "${PROJECT_SOURCE_DIR}/assets/source/sounds/*.wav"
    "${PROJECT_SOURCE_DIR}/assets/source/sounds/*.ogg")
animal_add_data_files(animal_game_sounds
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/assets/source/sounds"
    DESTINATION sounds
    FILES ${ANIMAL_GAME_SOUND_FILES})

animal_add_data_files(animal_game_world
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/assets/source/world"
    DESTINATION world
    FILES ${ANIMAL_GAME_WORLD_FILES})

add_dependencies(animal_game animal_shaders animal_game_textures animal_game_world animal_game_sounds)

# Release layout (ADR-0031): cmake --build <dir> --target animal_package
#   build/<preset>/package/<Config>/AnimalGame + data.pak (+ cook report)
set(ANIMAL_PACKAGE_DIR "${PROJECT_BINARY_DIR}/package/$<CONFIG>")
set(ANIMAL_COOK_INPUTS "${PROJECT_SOURCE_DIR}/assets/cook.json")
foreach(rel IN LISTS ANIMAL_GAME_WORLD_FILES)
    list(APPEND ANIMAL_COOK_INPUTS "${PROJECT_SOURCE_DIR}/assets/source/world/${rel}")
endforeach()
foreach(rel IN LISTS ANIMAL_GAME_TEXTURE_FILES)
    list(APPEND ANIMAL_COOK_INPUTS "${PROJECT_SOURCE_DIR}/assets/source/textures/${rel}")
endforeach()
foreach(rel IN LISTS ANIMAL_GAME_SOUND_FILES)
    list(APPEND ANIMAL_COOK_INPUTS "${PROJECT_SOURCE_DIR}/assets/source/sounds/${rel}")
endforeach()
add_custom_command(
    OUTPUT "${ANIMAL_PACKAGE_DIR}/data.pak"
    COMMAND animal_asset_cooker
        "--manifest=${PROJECT_SOURCE_DIR}/assets/cook.json"
        "--source=${PROJECT_SOURCE_DIR}/assets/source"
        "--shaders=${ANIMAL_DATA_DIR}/shaders"
        "--out=${ANIMAL_PACKAGE_DIR}/data.pak"
        "--report=${ANIMAL_PACKAGE_DIR}/cook_report.json"
    COMMAND animal_asset_cooker "--verify=${ANIMAL_PACKAGE_DIR}/data.pak" "--manifest=${PROJECT_SOURCE_DIR}/assets/cook.json"
    DEPENDS animal_asset_cooker animal_shaders ${ANIMAL_COOK_INPUTS}
    COMMENT "Cooking data.pak"
    VERBATIM)
add_custom_target(animal_package
    DEPENDS "${ANIMAL_PACKAGE_DIR}/data.pak"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:animal_game>" "${ANIMAL_PACKAGE_DIR}/"
    COMMENT "Release layout in ${ANIMAL_PACKAGE_DIR}"
    VERBATIM)
add_dependencies(animal_package animal_game)
set_target_properties(animal_package PROPERTIES FOLDER game)

# Headless playthrough (ADR-0032): the scripted demo route must still reach the smoke pit with two
# gems and no fall; in Debug every recorded command list is validated (--strict).
if(ANIMAL_BUILD_TESTS)
    add_test(NAME demo_playthrough
        COMMAND animal_game --headless --demo --deterministic --strict --frames=900
                "--expect=room:smoke_pit,gems:2,visited:3,respawns:0")
    # Crash reports (ADR-0032) for a fault, abort() and an uncaught exception.
    foreach(mode segv abort exception)
        add_test(NAME crash_report_${mode}
            COMMAND "${CMAKE_COMMAND}" "-DGAME=$<TARGET_FILE:animal_game>"
                    "-DDIR=${CMAKE_CURRENT_BINARY_DIR}/crash_test_${mode}" -DMODE=${mode}
                    -P "${PROJECT_SOURCE_DIR}/cmake/CheckCrashReport.cmake")
    endforeach()
endif()

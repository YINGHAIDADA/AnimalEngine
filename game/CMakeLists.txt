set(ANIMAL_GAME_SOURCES
    src/cave_room.cpp
    src/controls.cpp
    src/cave_room.h
    src/dev/dev_tools.h
    src/game_settings.cpp
    src/main.cpp
    src/map_screen.cpp
    src/mechanisms.cpp
    src/mechanisms.h
    src/progression.cpp
    src/sandbox_game.cpp
    src/sandbox_game.h
    src/test_runs.cpp)

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

# Automated tests run on their own data (ADR-0035): tests/data is deployed to bin/<Config>/test_data
# with the compiled shaders, and every game test passes --data, so editing the game's content never
# affects a test.
set(ANIMAL_TEST_DATA_DIR "${ANIMAL_OUTPUT_DIR}/test_data")
if(ANIMAL_BUILD_TESTS)
    file(GLOB_RECURSE ANIMAL_TEST_DATA_FILES CONFIGURE_DEPENDS RELATIVE "${PROJECT_SOURCE_DIR}/tests/data"
        "${PROJECT_SOURCE_DIR}/tests/data/*")
    animal_add_data_files(animal_test_data
        SOURCE_DIR "${PROJECT_SOURCE_DIR}/tests/data"
        DATA_ROOT "${ANIMAL_TEST_DATA_DIR}"
        FILES ${ANIMAL_TEST_DATA_FILES})
    add_custom_target(animal_test_data_shaders ALL
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${ANIMAL_DATA_DIR}/shaders" "${ANIMAL_TEST_DATA_DIR}/shaders"
        COMMENT "Test data: shaders"
        VERBATIM)
    add_dependencies(animal_test_data_shaders animal_shaders)
    set_target_properties(animal_test_data_shaders PROPERTIES FOLDER data)
    add_dependencies(animal_game animal_test_data animal_test_data_shaders)

    set(ANIMAL_TEST_RUN animal_game --headless --deterministic --strict "--data=${ANIMAL_TEST_DATA_DIR}")
    # Headless playthrough (ADR-0032): the scripted demo route through the render rooms must reach the
    # pit with two gems and no fall; in Debug every recorded command list is validated (--strict).
    add_test(NAME demo_playthrough
        COMMAND ${ANIMAL_TEST_RUN} --demo --frames=900 "--expect=room:render_pit,gems:2,visited:3,respawns:0")
    # Crash reports (ADR-0032) for a fault, abort() and an uncaught exception.
    foreach(mode segv abort exception)
        add_test(NAME crash_report_${mode}
            COMMAND "${CMAKE_COMMAND}" "-DGAME=$<TARGET_FILE:animal_game>"
                    "-DDIR=${CMAKE_CURRENT_BINARY_DIR}/crash_test_${mode}" -DMODE=${mode}
                    -P "${PROJECT_SOURCE_DIR}/cmake/CheckCrashReport.cmake")
    endforeach()

    # Capabilities (ADR-0034, ADR-0035): one test room each, trivial input (tests/input), checked
    # where the run ends.
    function(animal_add_capability_test name room input)
        set(script "")
        if(input)
            set(script "--input-script=${PROJECT_SOURCE_DIR}/tests/input/${input}.txt")
        endif()
        add_test(NAME capability_${name} COMMAND ${ANIMAL_TEST_RUN} --start-room=${room} ${script} ${ARGN})
    endfunction()
    animal_add_capability_test(room_transition test_walk hold_right --frames=300 "--expect=room:test_walk_east")
    animal_add_capability_test(platform_carries test_platform "" --frames=360 "--expect=gems:1,respawns:0")
    animal_add_capability_test(platform_crushes test_crush "" --frames=70 "--expect=health:11,respawns:1")
    animal_add_capability_test(platform_falls test_falling "" --frames=180 "--expect=room:test_falling_below")
    animal_add_capability_test(spikes_hurt test_spikes hold_right --frames=60 "--expect=health:11,respawns:1")
    animal_add_capability_test(knocked_out test_spikes hold_right --frames=570 "--expect=health:11,respawns:13")
    animal_add_capability_test(crawler_hits test_crawler "" --frames=420 "--expect=health:11,respawns:0")
    animal_add_capability_test(switch_opens_door test_switch hold_right --frames=300
        "--expect=flag:switch:test_door,gems:1")
    animal_add_capability_test(door_stays_closed test_door_locked hold_right --frames=300 "--expect=gems:0")
    animal_add_capability_test(flag_opens_door test_door_locked hold_right --flags=switch:test_elsewhere --frames=300
        "--expect=gems:1")
    animal_add_capability_test(dive test_dive dive_right --flags=ability:dive --frames=300 "--expect=gems:1")
    animal_add_capability_test(no_dive test_dive dive_right --frames=300 "--expect=gems:0")
    animal_add_capability_test(current_carries test_current step_right --frames=540 "--expect=gems:1")
    animal_add_capability_test(gate_opens test_gate hold_right --gems=3 --frames=300 "--expect=finished:true")
    animal_add_capability_test(gate_stays_closed test_gate hold_right --gems=2 --frames=300 "--expect=finished:false")
    animal_add_capability_test(swim_up test_rise swim_up --flags=ability:dive --frames=240
        "--expect=room:test_rise_above")
    # Controls (ADR-0036): the player's rebinding from a settings file replaces the default keys.
    set(ANIMAL_REBOUND "--settings=${PROJECT_SOURCE_DIR}/tests/input/rebound_settings.json")
    animal_add_capability_test(rebound_key test_walk hold_l ${ANIMAL_REBOUND} --frames=300
        "--expect=room:test_walk_east")
    animal_add_capability_test(rebound_default_unbound test_walk hold_right ${ANIMAL_REBOUND} --frames=300
        "--expect=room:test_walk")
endif()

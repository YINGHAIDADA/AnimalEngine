set(ANIMAL_GAME_SOURCES
    src/cave_room.cpp
    src/cave_room.h
    src/main.cpp
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
        tileset_n.png
        tileset_e.png
        creature.png
        creature_n.png
        props.png
        props_n.png
        props_e.png
        crawler.png
        crawler_n.png
        crawler_e.png)

animal_add_data_files(animal_game_world
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/assets/source/world"
    DESTINATION world
    FILES
        test.world
        tilesets/cave.tsj
        rooms/cave_entrance.tmj
        rooms/crystal_gallery.tmj
        rooms/smoke_pit.tmj
        looks/cave.look.json
        looks/crystal.look.json
        looks/ember.look.json
        effects/dust.effect.json
        effects/firefly.effect.json
        effects/ember.effect.json
        effects/spark.effect.json
        effects/splash.effect.json)

add_dependencies(animal_game animal_shaders animal_game_textures animal_game_world)

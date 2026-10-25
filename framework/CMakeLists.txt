set(ANIMAL_FRAMEWORK_HEADERS
    include/animal/framework/action_map.h
    include/animal/framework/animation.h
    include/animal/framework/climbing.h
    include/animal/framework/effects.h
    include/animal/framework/entity.h
    include/animal/framework/fluid_stirs.h
    include/animal/framework/foliage.h
    include/animal/framework/geometry.h
    include/animal/framework/influence.h
    include/animal/framework/level.h
    include/animal/framework/light_animation.h
    include/animal/framework/loadout.h
    include/animal/framework/look.h
    include/animal/framework/particles.h
    include/animal/framework/patrol.h
    include/animal/framework/pendulum.h
    include/animal/framework/physics.h
    include/animal/framework/platforms.h
    include/animal/framework/projectile.h
    include/animal/framework/platformer.h
    include/animal/framework/save.h
    include/animal/framework/settings.h
    include/animal/framework/sprite_sheet.h
    include/animal/framework/strings.h
    include/animal/framework/tilemap.h
    include/animal/framework/triggers.h
    include/animal/framework/tuning.h
    include/animal/framework/water.h
    include/animal/framework/world_map.h)

set(ANIMAL_FRAMEWORK_SOURCES
    src/action_map.cpp
    src/animation.cpp
    src/climbing.cpp
    src/effects.cpp
    src/entity.cpp
    src/fluid_stirs.cpp
    src/foliage.cpp
    src/level.cpp
    src/light_animation.cpp
    src/loadout.cpp
    src/look.cpp
    src/particles.cpp
    src/patrol.cpp
    src/pendulum.cpp
    src/physics.cpp
    src/platforms.cpp
    src/projectile.cpp
    src/platformer.cpp
    src/save.cpp
    src/settings.cpp
    src/sprite_sheet.cpp
    src/strings.cpp
    src/tilemap.cpp
    src/triggers.cpp
    src/tuning.cpp
    src/water.cpp
    src/world_map.cpp)

add_library(animal_framework STATIC ${ANIMAL_FRAMEWORK_HEADERS} ${ANIMAL_FRAMEWORK_SOURCES})
add_library(animal::framework ALIAS animal_framework)
animal_set_compile_options(animal_framework)
target_include_directories(animal_framework PUBLIC include)
target_link_libraries(animal_framework PUBLIC animal::engine PRIVATE animal_json)
set_target_properties(animal_framework PROPERTIES FOLDER framework)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${ANIMAL_FRAMEWORK_HEADERS} ${ANIMAL_FRAMEWORK_SOURCES})

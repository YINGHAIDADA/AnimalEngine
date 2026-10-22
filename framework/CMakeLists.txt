set(ANIMAL_FRAMEWORK_HEADERS
    include/animal/framework/action_map.h
    include/animal/framework/animation.h
    include/animal/framework/climbing.h
    include/animal/framework/entity.h
    include/animal/framework/foliage.h
    include/animal/framework/geometry.h
    include/animal/framework/influence.h
    include/animal/framework/level.h
    include/animal/framework/particles.h
    include/animal/framework/physics.h
    include/animal/framework/platformer.h
    include/animal/framework/tilemap.h)

set(ANIMAL_FRAMEWORK_SOURCES
    src/action_map.cpp
    src/animation.cpp
    src/climbing.cpp
    src/entity.cpp
    src/foliage.cpp
    src/level.cpp
    src/particles.cpp
    src/physics.cpp
    src/platformer.cpp
    src/tilemap.cpp)

add_library(animal_framework STATIC ${ANIMAL_FRAMEWORK_HEADERS} ${ANIMAL_FRAMEWORK_SOURCES})
add_library(animal::framework ALIAS animal_framework)
animal_set_compile_options(animal_framework)
target_include_directories(animal_framework PUBLIC include)
target_link_libraries(animal_framework PUBLIC animal::engine PRIVATE animal_json)
set_target_properties(animal_framework PROPERTIES FOLDER framework)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${ANIMAL_FRAMEWORK_HEADERS} ${ANIMAL_FRAMEWORK_SOURCES})

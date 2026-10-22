add_executable(animal_tests
    test_harness.h
    test_main.cpp
    engine_tests.cpp
    framework_tests.cpp
    level_tests.cpp
    gameplay_tests.cpp
    look_tests.cpp
    effects_tests.cpp)
animal_set_compile_options(animal_tests)
target_link_libraries(animal_tests PRIVATE animal::framework)
set_target_properties(animal_tests PROPERTIES FOLDER tests)

add_test(NAME animal_unit_tests COMMAND animal_tests)

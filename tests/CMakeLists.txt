add_executable(animal_tests unit_tests.cpp)
animal_set_compile_options(animal_tests)
target_link_libraries(animal_tests PRIVATE animal::engine)
set_target_properties(animal_tests PROPERTIES FOLDER tests)

add_test(NAME animal_unit_tests COMMAND animal_tests)

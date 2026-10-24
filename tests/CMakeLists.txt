add_executable(animal_tests
    test_harness.h
    test_main.cpp
    engine_tests.cpp
    framework_tests.cpp
    level_tests.cpp
    gameplay_tests.cpp
    look_tests.cpp
    effects_tests.cpp
    water_tests.cpp
    wind_tests.cpp
    rhi_validation_tests.cpp
    tuning_tests.cpp
    animation_import_tests.cpp
    pack_tests.cpp
    cooker_tests.cpp
    audio_tests.cpp
    mechanism_tests.cpp
    settings_tests.cpp
    bitmap_font_tests.cpp
    strings_tests.cpp
    swing_and_throw_tests.cpp)
animal_set_compile_options(animal_tests)
if(ANIMAL_DEV_TOOLS)
    target_sources(animal_tests PRIVATE devtools_tests.cpp)
    target_link_libraries(animal_tests PRIVATE animal::devtools) # brings animal::framework
else()
    target_link_libraries(animal_tests PRIVATE animal::framework)
endif()
target_compile_definitions(animal_tests PRIVATE ANIMAL_TEST_ASSETS_DIR="${PROJECT_SOURCE_DIR}/tests/data"
    ANIMAL_TEST_SHADER_DIR="${ANIMAL_DATA_DIR}/shaders")
target_link_libraries(animal_tests PRIVATE animal_cooker)
add_dependencies(animal_tests animal_shaders) # the cooker tests read the compiled shaders
set_target_properties(animal_tests PROPERTIES FOLDER tests)

add_test(NAME animal_unit_tests COMMAND animal_tests)

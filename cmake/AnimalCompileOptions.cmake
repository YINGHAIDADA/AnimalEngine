# Common compiler settings for project-owned targets (third-party targets keep their own flags).
function(animal_set_compile_options target)
    target_compile_features(${target} PUBLIC cxx_std_20)

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor
            $<$<BOOL:${ANIMAL_WARNINGS_AS_ERRORS}>:/WX>)
        target_compile_definitions(${target} PRIVATE
            NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
        if(CMAKE_GENERATOR MATCHES "Visual Studio")
            target_compile_options(${target} PRIVATE /MP)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow
            $<$<BOOL:${ANIMAL_WARNINGS_AS_ERRORS}>:-Werror>)
    endif()

    target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:ANIMAL_DEBUG=1>)
endfunction()

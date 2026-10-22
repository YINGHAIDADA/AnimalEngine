# Runtime data deployment into ${ANIMAL_DATA_DIR} (build/<preset>/bin/<Config>/data).

# Compiles GLSL 450 sources (*.vert / *.frag) into data/shaders/<file>.ashd packages.
function(animal_add_shaders target)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
    set(outputs "")
    foreach(src IN LISTS ARG_SOURCES)
        get_filename_component(src_abs "${src}" ABSOLUTE)
        get_filename_component(src_name "${src}" NAME)
        get_filename_component(src_ext "${src}" LAST_EXT)
        string(SUBSTRING "${src_ext}" 1 -1 stage)
        set(out "${ANIMAL_DATA_DIR}/shaders/${src_name}.ashd")
        add_custom_command(
            OUTPUT "${out}"
            COMMAND animal_shaderc --stage ${stage} -o "${out}" "${src_abs}"
            DEPENDS "${src_abs}" animal_shaderc
            COMMENT "Compiling shader ${src_name}"
            VERBATIM)
        list(APPEND outputs "${out}")
    endforeach()
    add_custom_target(${target} ALL DEPENDS ${outputs} SOURCES ${ARG_SOURCES})
    set_target_properties(${target} PROPERTIES FOLDER data)
endfunction()

# Copies source assets into data/<DESTINATION>/, preserving relative paths.
function(animal_add_data_files target)
    cmake_parse_arguments(ARG "" "SOURCE_DIR;DESTINATION" "FILES" ${ARGN})
    set(outputs "")
    foreach(rel IN LISTS ARG_FILES)
        set(src "${ARG_SOURCE_DIR}/${rel}")
        set(dst "${ANIMAL_DATA_DIR}/${ARG_DESTINATION}/${rel}")
        get_filename_component(dst_dir "${dst}" DIRECTORY)
        add_custom_command(
            OUTPUT "${dst}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${dst_dir}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${src}" "${dst}"
            DEPENDS "${src}"
            COMMENT "Deploying ${ARG_DESTINATION}/${rel}"
            VERBATIM)
        list(APPEND outputs "${dst}")
    endforeach()
    add_custom_target(${target} ALL DEPENDS ${outputs})
    set_target_properties(${target} PROPERTIES FOLDER data)
endfunction()

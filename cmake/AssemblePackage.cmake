# Assembles a release package (M7, ADR-0037), run by the animal_package target:
#   cmake -DPLATFORM=macos|windows -DOUT=<package dir> -DNAME=<folder name> -DEXE=<game> -DPACK=<data.pak>
#         -DREADME=<file> -DNOTICES=<file> -DVERSION=<x.y.z> -DSOURCE_DIR=<repo>
#         [-DPLIST=<Info.plist> -DICNS=<icon> -DCODESIGN_IDENTITY=<identity, "-" = ad hoc>]
#         -P AssemblePackage.cmake
# Produces <OUT>/<NAME>/ (the app or the executable, data.pak, README.txt, LICENSE.txt,
# THIRD_PARTY_NOTICES.txt, manifest.json), <OUT>/<NAME>.zip and a copy of the manifest beside it.

foreach(var PLATFORM OUT NAME EXE PACK README NOTICES VERSION SOURCE_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "AssemblePackage: ${var} is not set")
    endif()
endforeach()

if(DEV_TOOLS)
    message(FATAL_ERROR "This build includes the developer tools (level editor, debug UI, hot reload, and paths of "
                        "this machine), which a release package must not: package from the macos-dist / windows-dist "
                        "preset (cmake --preset macos-dist && cmake --build --preset macos-dist), or configure "
                        "with -DANIMAL_DEV_TOOLS=OFF")
endif()

set(root "${OUT}/${NAME}")
file(REMOVE_RECURSE "${root}" "${OUT}/${NAME}.zip" "${OUT}/${NAME}.manifest.json")
file(MAKE_DIRECTORY "${root}")

if(PLATFORM STREQUAL "macos")
    set(app "${root}/AnimalEngine.app")
    file(MAKE_DIRECTORY "${app}/Contents/MacOS" "${app}/Contents/Resources")
    file(COPY_FILE "${EXE}" "${app}/Contents/MacOS/AnimalGame")
    file(CHMOD "${app}/Contents/MacOS/AnimalGame" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ
         GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    file(COPY_FILE "${PACK}" "${app}/Contents/Resources/data.pak")
    file(COPY_FILE "${ICNS}" "${app}/Contents/Resources/AnimalGame.icns")
    file(COPY_FILE "${PLIST}" "${app}/Contents/Info.plist")
    if(NOT CODESIGN_IDENTITY)
        set(CODESIGN_IDENTITY "-")
    endif()
    set(sign_args --force --sign "${CODESIGN_IDENTITY}")
    if(NOT CODESIGN_IDENTITY STREQUAL "-")
        list(APPEND sign_args --options runtime --timestamp) # Developer ID: hardened runtime for notarization
    endif()
    execute_process(COMMAND codesign ${sign_args} "${app}" RESULT_VARIABLE signed OUTPUT_VARIABLE log ERROR_VARIABLE log)
    if(NOT signed EQUAL 0)
        message(FATAL_ERROR "codesign failed:\n${log}")
    endif()
elseif(PLATFORM STREQUAL "windows")
    file(COPY_FILE "${EXE}" "${root}/AnimalGame.exe")
    file(COPY_FILE "${PACK}" "${root}/data.pak")
else()
    message(FATAL_ERROR "AssemblePackage: unknown PLATFORM '${PLATFORM}'")
endif()
file(COPY_FILE "${README}" "${root}/README.txt")
file(COPY_FILE "${NOTICES}" "${root}/THIRD_PARTY_NOTICES.txt")
file(COPY_FILE "${SOURCE_DIR}/LICENSE" "${root}/LICENSE.txt")

# The manifest: every file with its size and SHA-256, so a package can be audited and checked.
execute_process(COMMAND git rev-parse --short=12 HEAD WORKING_DIRECTORY "${SOURCE_DIR}"
                OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE git_result)
if(NOT git_result EQUAL 0 OR commit STREQUAL "")
    set(commit "unknown")
endif()
file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${root}" "${root}/*")
list(SORT files)
set(entries "")
foreach(rel IN LISTS files)
    file(SIZE "${root}/${rel}" size)
    file(SHA256 "${root}/${rel}" hash)
    list(APPEND entries "    {\"path\": \"${rel}\", \"size\": ${size}, \"sha256\": \"${hash}\"}")
endforeach()
list(JOIN entries ",\n" entries)
file(WRITE "${root}/manifest.json" "{\n  \"product\": \"Animal Engine\",\n  \"version\": \"${VERSION}\",\n  \"platform\": \"${PLATFORM}\",\n  \"commit\": \"${commit}\",\n  \"files\": [\n${entries}\n  ]\n}\n")
file(COPY_FILE "${root}/manifest.json" "${OUT}/${NAME}.manifest.json")

execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${NAME}.zip" --format=zip "${NAME}" WORKING_DIRECTORY "${OUT}"
                RESULT_VARIABLE zipped)
if(NOT zipped EQUAL 0)
    message(FATAL_ERROR "zip failed")
endif()
list(LENGTH files count)
message(STATUS "Package ${OUT}/${NAME}.zip (${count} files, commit ${commit})")

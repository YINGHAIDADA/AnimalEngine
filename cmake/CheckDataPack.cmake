# ctest helper (ADR-0037): the game reading its data from a resource pack, as a release does.
#   cmake -DGAME=<exe> -DCOOKER=<exe> -DSOURCE=<repo> -DDATA=<test_data> -DSHADERS=<dir> -DWORK=<dir>
#         -DMODE=damaged|packed -P CheckDataPack.cmake
#   damaged: a file that is not a pack must stop the game with an error naming the resource pack.
#   packed:  the test data cooked into a pack runs a capability (walking into the next room).
file(MAKE_DIRECTORY "${WORK}")
set(run "${GAME}" --headless --deterministic --strict "--data=${DATA}" --frames=300)
if(MODE STREQUAL "damaged")
    execute_process(COMMAND ${run} "--pack=${SOURCE}/tests/input/hold_right.txt"
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(result EQUAL 0 OR NOT output MATCHES "resource pack")
        message(FATAL_ERROR "a damaged pack must stop the game with an error (exit ${result}):\n${output}")
    endif()
elseif(MODE STREQUAL "packed")
    set(pack "${WORK}/test_data.pak")
    execute_process(COMMAND "${COOKER}" "--manifest=${SOURCE}/tests/data/cook.json" "--source=${SOURCE}/tests/data"
                            "--shaders=${SHADERS}" "--out=${pack}"
                    RESULT_VARIABLE cooked OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(NOT cooked EQUAL 0)
        message(FATAL_ERROR "cooking the test data failed:\n${output}")
    endif()
    execute_process(COMMAND ${run} "--pack=${pack}" --start-room=test_walk
                            "--input-script=${SOURCE}/tests/input/hold_right.txt" "--expect=room:test_walk_east"
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(NOT result EQUAL 0 OR NOT output MATCHES "resource pack")
        message(FATAL_ERROR "the test data from a pack failed (exit ${result}):\n${output}")
    endif()
else()
    message(FATAL_ERROR "CheckDataPack: unknown MODE '${MODE}'")
endif()

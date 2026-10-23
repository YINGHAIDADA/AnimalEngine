# ctest helper (ADR-0032): the game, crashing on purpose, must write a crash report.
#   cmake -DGAME=<exe> -DDIR=<dir> -DMODE=segv|abort|exception -P CheckCrashReport.cmake
file(REMOVE_RECURSE "${DIR}")
execute_process(COMMAND "${GAME}" --headless "--crash-test=${MODE}" "--crash-dir=${DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(result EQUAL 0)
    message(FATAL_ERROR "the game did not crash (--crash-test=${MODE}):\n${output}")
endif()
file(GLOB reports "${DIR}/crash-*.txt")
list(LENGTH reports count)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "expected one crash report in ${DIR}, found ${count} (exit ${result}):\n${output}")
endif()
file(READ "${reports}" report)
if(NOT report MATCHES "Animal Engine crash" OR NOT report MATCHES "call stack")
    message(FATAL_ERROR "incomplete crash report:\n${report}")
endif()
message(STATUS "crash report (${MODE}) written: ${reports}")

# Applies PATCH to the working directory once; a no-op when it is already applied.
# Usage: cmake -DPATCH=<file> -P ApplyPatch.cmake
execute_process(COMMAND git apply --reverse --check "${PATCH}"
    RESULT_VARIABLE alreadyApplied OUTPUT_QUIET ERROR_QUIET)
if(NOT alreadyApplied EQUAL 0)
    execute_process(COMMAND git apply "${PATCH}" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Could not apply ${PATCH}")
    endif()
endif()

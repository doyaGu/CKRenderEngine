# CTest driver: runs spirv-val on every module a shader JIT test emits.
#
# Arguments (-D): TEST, VALIDATOR, OUT_DIR.

get_filename_component(_test "${TEST}" NAME_WE)
file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")
execute_process(COMMAND "${TEST}" "${OUT_DIR}" RESULT_VARIABLE _rc)
if (NOT _rc EQUAL 0)
    message(FATAL_ERROR "${_test} failed (${_rc})")
endif ()

file(GLOB _modules "${OUT_DIR}/*.spv")
if (NOT _modules)
    message(FATAL_ERROR "${_test} wrote no modules to ${OUT_DIR}")
endif ()
foreach (_module IN LISTS _modules)
    execute_process(COMMAND "${VALIDATOR}" --target-env vulkan1.0 "${_module}" RESULT_VARIABLE _rc)
    if (NOT _rc EQUAL 0)
        message(FATAL_ERROR "spirv-val rejected ${_module}")
    endif ()
endforeach ()

# CTest driver: creates a D3D12 pipeline on WARP for every container a
# shader JIT test emits.
#
# Arguments (-D): TEST, VALIDATOR, OUT_DIR.

get_filename_component(_test "${TEST}" NAME_WE)
file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")
execute_process(COMMAND "${TEST}" "${OUT_DIR}" RESULT_VARIABLE _rc)
if (NOT _rc EQUAL 0)
    message(FATAL_ERROR "${_test} failed (${_rc})")
endif ()

file(GLOB _containers "${OUT_DIR}/*.dxbc")
if (NOT _containers)
    message(FATAL_ERROR "${_test} wrote no containers to ${OUT_DIR}")
endif ()
execute_process(COMMAND "${VALIDATOR}" ${_containers} RESULT_VARIABLE _rc)
if (NOT _rc EQUAL 0)
    message(FATAL_ERROR "D3D12 refused a container (${_rc})")
endif ()

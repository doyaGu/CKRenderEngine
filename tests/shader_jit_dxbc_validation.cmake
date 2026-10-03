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

file(GLOB _containers RELATIVE "${OUT_DIR}" "${OUT_DIR}/*.dxbc")
if (NOT _containers)
    message(FATAL_ERROR "${_test} wrote no containers to ${OUT_DIR}")
endif ()
# Windows bounds the process command line. Keep batches small as the frontend
# corpus grows, and avoid repeating the absolute output path for every file.
function(_validate_batch)
    execute_process(COMMAND "${VALIDATOR}" ${ARGN}
                    WORKING_DIRECTORY "${OUT_DIR}" RESULT_VARIABLE _rc)
    if (NOT _rc EQUAL 0)
        message(FATAL_ERROR "D3D12 refused a container in batch ${ARGN} (${_rc})")
    endif ()
endfunction()
set(_batch)
foreach (_container IN LISTS _containers)
    list(APPEND _batch "${_container}")
    list(LENGTH _batch _count)
    if (_count EQUAL 64)
        _validate_batch(${_batch})
        set(_batch)
    endif ()
endforeach ()
if (_batch)
    _validate_batch(${_batch})
endif ()

# CTest driver for persistent-resource stability.  Static scenes must render
# identically before and after RCKMesh promotes them to hardware buffers.
#
# Arguments (-D): TOOL, ENGINE_DIR, OUT_DIR.
# Environment:
#   CKRE_RUN_BGFX_BACKEND_RUNTIME_TESTS=1  required, otherwise the test is skipped

if (NOT "$ENV{CKRE_RUN_BGFX_BACKEND_RUNTIME_TESTS}" STREQUAL "1")
    message(STATUS "SKIPPED: set CKRE_RUN_BGFX_BACKEND_RUNTIME_TESTS=1 to run the multiframe scene gate")
    return()
endif ()

set(_baseline "${OUT_DIR}/frame-1")
set(_candidate "${OUT_DIR}/frame-5")
file(REMOVE_RECURSE "${_baseline}" "${_candidate}")
file(MAKE_DIRECTORY "${_baseline}" "${_candidate}")

set(_common "${TOOL}" --render-engine-dir "${ENGINE_DIR}" --scene all --size 640x480 --skip dump_copy)

execute_process(
    COMMAND ${_common} --frames 1 --out "${_baseline}"
    RESULT_VARIABLE _baseline_rc
)
if (NOT _baseline_rc EQUAL 0)
    message(FATAL_ERROR "ckre_scene_capture failed while capturing frame 1 (${_baseline_rc})")
endif ()

execute_process(
    COMMAND ${_common} --frames 5 --out "${_candidate}"
            --compare "${_baseline}" --threshold 2 --min-pass 0.995 --require-all
    RESULT_VARIABLE _candidate_rc
)
if (NOT _candidate_rc EQUAL 0)
    message(FATAL_ERROR
        "scene_capture_multiframe failed (${_candidate_rc}); frame 1, frame 5 and diff images are in ${OUT_DIR}")
endif ()

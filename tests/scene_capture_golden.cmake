# CTest driver for the scene_capture_golden test (plan task 0.7, spec 7.3).
#
# Arguments (-D): TOOL, ENGINE_DIR, GOLDEN_ROOT, OUT_DIR, SKIP.
# Environment:
#   CKRE_RUN_BGFX_BACKEND_RUNTIME_TESTS=1  required, otherwise the test is skipped
#   CKBGFX_RENDERER_BACKEND=<backend>      golden directory suffix (default "default")
#   CKRE_GOLDEN_RUNNER=<runner>            golden directory prefix (default "local")
#   CKRE_UPDATE_GOLDEN=1                   write the golden frames instead of comparing
# A line containing SKIPPED marks the test as skipped (SKIP_REGULAR_EXPRESSION).

if (NOT "$ENV{CKRE_RUN_BGFX_BACKEND_RUNTIME_TESTS}" STREQUAL "1")
    message(STATUS "SKIPPED: set CKRE_RUN_BGFX_BACKEND_RUNTIME_TESTS=1 to run the scene capture golden gate")
    return()
endif ()

set(_backend "$ENV{CKBGFX_RENDERER_BACKEND}")
if (_backend STREQUAL "")
    set(_backend "default")
endif ()
set(_runner "$ENV{CKRE_GOLDEN_RUNNER}")
if (_runner STREQUAL "")
    set(_runner "local")
endif ()
set(_golden "${GOLDEN_ROOT}/${_runner}-${_backend}")
set(_out "${OUT_DIR}/${_runner}-${_backend}")
file(MAKE_DIRECTORY "${_out}")

set(_common "${TOOL}" --render-engine-dir "${ENGINE_DIR}" --scene all --frames 3 --size 640x480)
if (SKIP)
    list(APPEND _common --skip "${SKIP}")
endif ()

if ("$ENV{CKRE_UPDATE_GOLDEN}" STREQUAL "1")
    file(MAKE_DIRECTORY "${_golden}")
    message(STATUS "Writing golden frames to ${_golden}")
    execute_process(COMMAND ${_common} --out "${_golden}" RESULT_VARIABLE _rc)
    if (NOT _rc EQUAL 0)
        message(FATAL_ERROR "ckre_scene_capture failed while writing golden frames (${_rc})")
    endif ()
    # Remove stale diff images that an earlier compare may have left behind.
    file(GLOB _stale "${_golden}/*.diff.png" "${_golden}/*.CK2_3D.ini")
    if (_stale)
        file(REMOVE ${_stale})
    endif ()
    return()
endif ()

if (NOT EXISTS "${_golden}")
    message(STATUS "SKIPPED: no golden frames for ${_runner}-${_backend} in ${GOLDEN_ROOT}; rerun with CKRE_UPDATE_GOLDEN=1 to create them")
    return()
endif ()

execute_process(COMMAND ${_common} --out "${_out}" --compare "${_golden}" --threshold 2 --min-pass 0.995 --require-all
        RESULT_VARIABLE _rc)
if (NOT _rc EQUAL 0)
    message(FATAL_ERROR "scene_capture_golden failed (${_rc}); captured frames and diff images are in ${_out}")
endif ()

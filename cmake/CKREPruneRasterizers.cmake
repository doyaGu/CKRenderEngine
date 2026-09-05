# Remove only known plugin binaries left by a previous stage configuration.
# Keep user configuration, other plugins and assets. External install prefixes
# are never cleaned automatically.
if (NOT DEFINED BUILD_ROOT OR NOT DEFINED STAGE_ROOT OR
        NOT DEFINED CKRE_BUILD_BGFX_RASTERIZER OR NOT DEFINED CKRE_BUILD_SDL_GPU_RASTERIZER)
    message(FATAL_ERROR "Build/stage roots and both rasterizer options are required")
endif ()
get_filename_component(_build_root "${BUILD_ROOT}" REALPATH)
get_filename_component(_stage_root "${STAGE_ROOT}" REALPATH)
include("${CMAKE_CURRENT_LIST_DIR}/CKRERasterizers.cmake")
ckre_get_rasterizers(_enabled _disabled "${CKRE_STATIC_RUNTIME}")
string(FIND "${_stage_root}/" "${_build_root}/" _stage_prefix)
set(_remove "")
foreach (_name IN LISTS _disabled)
    foreach (_directory IN ITEMS Bin RenderEngines)
        foreach (_filename IN ITEMS "${_name}.dll" "${_name}.so" "lib${_name}.so"
                "${_name}.dylib" "lib${_name}.dylib")
            set(_path "${_stage_root}/${_directory}/${_filename}")
            if (EXISTS "${_path}" OR IS_SYMLINK "${_path}")
                get_filename_component(_resolved "${_path}" REALPATH)
                string(FIND "${_resolved}" "${_stage_root}/" _file_prefix)
                if (NOT _stage_prefix EQUAL 0 OR _stage_root STREQUAL _build_root OR
                        NOT _file_prefix EQUAL 0 OR IS_DIRECTORY "${_path}")
                    message(FATAL_ERROR "Cannot automatically remove stale rasterizer outside an isolated build stage: ${_path}")
                endif ()
                list(APPEND _remove "${_path}")
            endif ()
        endforeach ()
    endforeach ()
endforeach ()
# Validate the entire set before removing any files.
foreach (_path IN LISTS _remove)
    file(REMOVE "${_path}")
    if (EXISTS "${_path}" OR IS_SYMLINK "${_path}")
        message(FATAL_ERROR "Could not remove stale rasterizer (close the Player first): ${_path}")
    endif ()
    message(STATUS "[Stage] Removed disabled rasterizer: ${_path}")
endforeach ()

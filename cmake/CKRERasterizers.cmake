include_guard(GLOBAL)

# Keep script-mode defaults aligned with configure without changing the caller's
# cache. Enabled targets participate in both dynamic and static compositions;
# disabled binaries also include enabled providers when using static registration.
function(ckre_get_rasterizers OUT_ENABLED OUT_DISABLED_BINARIES STATIC_REGISTRATION)
    set(_enabled "")
    set(_disabled "")
    # Target | build option | default
    foreach (_entry IN ITEMS
            "CKBgfxRasterizer|CKRE_BUILD_BGFX_RASTERIZER|OFF"
            "CKSdlGpuRasterizer|CKRE_BUILD_SDL_GPU_RASTERIZER|ON")
        string(REPLACE "|" ";" _fields "${_entry}")
        list(GET _fields 0 _target)
        list(GET _fields 1 _option)
        list(GET _fields 2 _selected)
        if (DEFINED ${_option})
            set(_selected "${${_option}}")
        endif ()
        if (_selected)
            list(APPEND _enabled "${_target}")
        endif ()
        if (NOT _selected OR STATIC_REGISTRATION)
            list(APPEND _disabled "${_target}")
        endif ()
    endforeach ()
    set(${OUT_ENABLED} "${_enabled}" PARENT_SCOPE)
    set(${OUT_DISABLED_BINARIES} "${_disabled}" PARENT_SCOPE)
endfunction()

include_guard(GLOBAL)

# Cover native and MinGW naming when reusing a staging directory across toolchains.
function(ckre_runtime_library_names OUT_VAR NAME)
    set(${OUT_VAR} "${NAME}.dll;lib${NAME}.dll;${NAME}.so;lib${NAME}.so;${NAME}.dylib;lib${NAME}.dylib" PARENT_SCOPE)
endfunction()

# Keep script-mode defaults aligned with configure without changing the caller's
# cache. Enabled targets participate in both dynamic and static compositions;
# disabled binaries also include enabled providers when using static registration.
# Retired providers are always disabled: stages configured before their removal
# may still hold their binaries, which the engine would load.
function(ckre_get_rasterizers OUT_ENABLED OUT_DISABLED_BINARIES STATIC_REGISTRATION)
    set(_enabled "")
    set(_disabled CKBgfxRasterizer)
    # Target | build option | default
    foreach (_entry IN ITEMS
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

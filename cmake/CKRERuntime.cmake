# Runtime ownership stays with the component, including standalone installs.
include(CKRERasterizers)
ckre_get_rasterizers(_ckre_runtime_providers _ckre_unused FALSE)
set_property(GLOBAL PROPERTY CKRE_RUNTIME_TARGETS "CK2_3D;${_ckre_runtime_providers}")
if (_ckre_runtime_providers)
    set_property(GLOBAL PROPERTY CKRE_HAS_RENDER_OUTPUT TRUE)
else ()
    set_property(GLOBAL PROPERTY CKRE_HAS_RENDER_OUTPUT FALSE)
endif ()

set(CKRE_STATIC_RUNTIME FALSE)
if (NOT CKRE_BUILD_SHARED)
    set(CKRE_STATIC_RUNTIME TRUE)
endif ()

if (CKRE_INSTALL)
    set(_ckre_required_files "")
    if (CKRE_BUILD_SHARED)
        foreach (_target IN ITEMS CK2_3D ${_ckre_runtime_providers})
            list(APPEND _ckre_required_files "RenderEngines/$<TARGET_FILE_NAME:${_target}>")
        endforeach ()
        set(_ckre_config_directory RenderEngines)
    else ()
        set(_ckre_config_directory Bin)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/CK2_3D.ini"
                DESTINATION Bin COMPONENT Runtime)
        if (CKRE_BUILD_BGFX_RASTERIZER)
            install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/CKRasterizer/CKBgfxRasterizer/CKBgfxRasterizer.ini"
                    DESTINATION Bin COMPONENT Runtime)
        endif ()
    endif ()
    list(APPEND _ckre_required_files "${_ckre_config_directory}/CK2_3D.ini")
    if (CKRE_BUILD_BGFX_RASTERIZER)
        list(APPEND _ckre_required_files "${_ckre_config_directory}/CKBgfxRasterizer.ini")
    endif ()
    # A data-only contract also lets archive checkers validate the component
    # without knowing any provider names or executing installed scripts.
    ckre_get_rasterizers(_ckre_enabled _ckre_disabled "${CKRE_STATIC_RUNTIME}")
    set(_ckre_forbidden_files "")
    foreach (_name IN LISTS _ckre_disabled)
        foreach (_directory IN ITEMS Bin RenderEngines)
            foreach (_filename IN ITEMS "${_name}.dll" "${_name}.so" "lib${_name}.so"
                    "${_name}.dylib" "lib${_name}.dylib")
                list(APPEND _ckre_forbidden_files "${_directory}/${_filename}")
            endforeach ()
        endforeach ()
    endforeach ()
    set(_ckre_required_json "")
    set(_ckre_forbidden_json "")
    if (_ckre_required_files)
        string(REPLACE ";" "\",\n    \"" _ckre_required_json "${_ckre_required_files}")
        set(_ckre_required_json "\"${_ckre_required_json}\"")
    endif ()
    if (_ckre_forbidden_files)
        string(REPLACE ";" "\",\n    \"" _ckre_forbidden_json "${_ckre_forbidden_files}")
        set(_ckre_forbidden_json "\"${_ckre_forbidden_json}\"")
    endif ()
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/runtime-$<CONFIG>/RenderEngine.json"
        CONTENT "{\n  \"required\": [${_ckre_required_json}],\n  \"forbidden\": [${_ckre_forbidden_json}]\n}\n")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/runtime-$<CONFIG>/RenderEngine.json"
            DESTINATION Bin/RuntimeManifests COMPONENT Runtime)
    configure_file("${CMAKE_CURRENT_LIST_DIR}/CKREInstallRuntime.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/CKREInstallRuntime.cmake.in" @ONLY)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/CKREInstallRuntime-$<CONFIG>.cmake"
            INPUT "${CMAKE_CURRENT_BINARY_DIR}/CKREInstallRuntime.cmake.in")
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/CKREInstallRuntime-$<CONFIG>.cmake" COMPONENT Runtime)
endif ()

if (BUILD_TESTING)
    add_test(NAME RasterizerInstallSelection
            COMMAND "${CMAKE_COMMAND}" "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/install-selection-test"
            -P "${CMAKE_CURRENT_LIST_DIR}/CKRETestRasterizerInstall.cmake")
endif ()

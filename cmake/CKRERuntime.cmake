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

ckre_get_rasterizers(_ckre_unused _ckre_disabled_runtime_providers
        "${CKRE_STATIC_RUNTIME}")
set_property(GLOBAL PROPERTY CKRE_DISABLED_RUNTIME_OUTPUTS
        "${_ckre_disabled_runtime_providers}")
set_property(GLOBAL PROPERTY CKRE_RUNTIME_CONFIGS CK2_3D.ini)

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
    endif ()
    list(APPEND _ckre_required_files "${_ckre_config_directory}/CK2_3D.ini")
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

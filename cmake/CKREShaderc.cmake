function(_ckre_normalize_architecture VALUE OUT_VAR)
    string(TOLOWER "${VALUE}" _ckre_arch)
    if (_ckre_arch MATCHES "^(x86|win32|i[3-6]86)$")
        set(${OUT_VAR} "x86" PARENT_SCOPE)
    elseif (_ckre_arch MATCHES "^(x64|amd64|x86_64)$")
        set(${OUT_VAR} "x64" PARENT_SCOPE)
    elseif (_ckre_arch MATCHES "^(arm64|aarch64)$")
        set(${OUT_VAR} "arm64" PARENT_SCOPE)
    else ()
        set(${OUT_VAR} "${_ckre_arch}" PARENT_SCOPE)
    endif ()
endfunction()

function(_ckre_target_architecture OUT_VAR)
    if (CMAKE_GENERATOR_PLATFORM)
        set(_ckre_arch "${CMAKE_GENERATOR_PLATFORM}")
    elseif (CMAKE_VS_PLATFORM_NAME)
        set(_ckre_arch "${CMAKE_VS_PLATFORM_NAME}")
    elseif (CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
        set(_ckre_arch "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
    elseif (CMAKE_SYSTEM_PROCESSOR)
        set(_ckre_arch "${CMAKE_SYSTEM_PROCESSOR}")
    else ()
        set(_ckre_arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif ()
    _ckre_normalize_architecture("${_ckre_arch}" _ckre_normalized_arch)
    set(${OUT_VAR} "${_ckre_normalized_arch}" PARENT_SCOPE)
endfunction()

function(_ckre_host_visual_studio_platform OUT_VAR)
    _ckre_normalize_architecture("${CMAKE_HOST_SYSTEM_PROCESSOR}" _ckre_host_arch)
    if (_ckre_host_arch STREQUAL "x86")
        set(${OUT_VAR} "Win32" PARENT_SCOPE)
    elseif (_ckre_host_arch STREQUAL "x64")
        set(${OUT_VAR} "x64" PARENT_SCOPE)
    elseif (_ckre_host_arch STREQUAL "arm64")
        set(${OUT_VAR} "ARM64" PARENT_SCOPE)
    else ()
        message(FATAL_ERROR "[CKRenderEngine] Unsupported host architecture for Visual Studio host tools: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif ()
endfunction()

function(_ckre_target_tool_is_runnable OUT_VAR)
    _ckre_target_architecture(_ckre_target_arch)
    _ckre_normalize_architecture("${CMAKE_HOST_SYSTEM_PROCESSOR}" _ckre_host_arch)

    set(_ckre_runnable ON)
    if (WIN32)
        if (_ckre_target_arch STREQUAL _ckre_host_arch)
            set(_ckre_runnable ON)
        elseif (_ckre_host_arch STREQUAL "x64" AND _ckre_target_arch STREQUAL "x86")
            set(_ckre_runnable ON)
        else ()
            set(_ckre_runnable OFF)
        endif ()
    elseif (CMAKE_CROSSCOMPILING)
        set(_ckre_runnable OFF)
    endif ()

    set(${OUT_VAR} "${_ckre_runnable}" PARENT_SCOPE)
endfunction()

function(ckre_prepare_shaderc)
    set(CKRE_SHADERC_EXECUTABLE "" CACHE FILEPATH "Host-compatible bgfx shaderc executable for shader generation")
    mark_as_advanced(CKRE_SHADERC_EXECUTABLE)

    set(CKRE_BUILD_TARGET_SHADERC OFF PARENT_SCOPE)
    set(CKRE_SHADERC_COMMAND "" PARENT_SCOPE)
    set(CKRE_SHADERC_DEPENDS "" PARENT_SCOPE)

    if (NOT CKRE_GENERATE_SHADERS)
        return()
    endif ()

    if (CKRE_SHADERC_EXECUTABLE)
        get_filename_component(_ckre_shaderc "${CKRE_SHADERC_EXECUTABLE}" ABSOLUTE
                BASE_DIR "${CMAKE_BINARY_DIR}")
        if (NOT EXISTS "${_ckre_shaderc}")
            message(FATAL_ERROR "CKRE_SHADERC_EXECUTABLE does not exist: ${_ckre_shaderc}")
        endif ()
        set(CKRE_SHADERC_COMMAND "${_ckre_shaderc}" PARENT_SCOPE)
        return()
    endif ()

    _ckre_target_tool_is_runnable(_ckre_target_shaderc_runnable)
    if (_ckre_target_shaderc_runnable)
        set(CKRE_BUILD_TARGET_SHADERC ON PARENT_SCOPE)
        return()
    endif ()

    include(ExternalProject)

    set(_ckre_host_shaderc_build_dir "${CMAKE_BINARY_DIR}/host-tools/shaderc-build")
    get_property(_ckre_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if (_ckre_multi_config)
        set(_ckre_host_shaderc_path "${_ckre_host_shaderc_build_dir}/cmake/bgfx/$<CONFIG>/shaderc${CMAKE_EXECUTABLE_SUFFIX}")
    else ()
        set(_ckre_host_shaderc_path "${_ckre_host_shaderc_build_dir}/cmake/bgfx/shaderc${CMAKE_EXECUTABLE_SUFFIX}")
    endif ()

    set(_ckre_host_shaderc_generator_args CMAKE_GENERATOR "${CMAKE_GENERATOR}")
    if (CMAKE_GENERATOR MATCHES "Visual Studio")
        _ckre_host_visual_studio_platform(_ckre_host_vs_platform)
        list(APPEND _ckre_host_shaderc_generator_args
                CMAKE_GENERATOR_PLATFORM "${_ckre_host_vs_platform}")
    endif ()
    if (CMAKE_GENERATOR_TOOLSET)
        list(APPEND _ckre_host_shaderc_generator_args
                CMAKE_GENERATOR_TOOLSET "${CMAKE_GENERATOR_TOOLSET}")
    endif ()

    ExternalProject_Add(CKREHostShaderc
            SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/bgfx"
            BINARY_DIR "${_ckre_host_shaderc_build_dir}"
            ${_ckre_host_shaderc_generator_args}
            CMAKE_ARGS
                    -DBGFX_BUILD_TOOLS=ON
                    -DBGFX_BUILD_TOOLS_BIN2C=OFF
                    -DBGFX_BUILD_TOOLS_GEOMETRY=OFF
                    -DBGFX_BUILD_TOOLS_SHADER=ON
                    -DBGFX_BUILD_TOOLS_TEXTURE=OFF
                    -DBGFX_BUILD_EXAMPLES=OFF
                    -DBGFX_BUILD_TESTS=OFF
                    -DBGFX_CUSTOM_TARGETS=OFF
                    -DBGFX_INSTALL=OFF
                    -DBGFX_LIBRARY_TYPE=STATIC
            BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config $<CONFIG> --target shaderc
            INSTALL_COMMAND ""
            BUILD_BYPRODUCTS "${_ckre_host_shaderc_path}"
    )
    set_target_properties(CKREHostShaderc PROPERTIES FOLDER "CKRenderEngine/tools")

    set(CKRE_SHADERC_COMMAND "${_ckre_host_shaderc_path}" PARENT_SCOPE)
    set(CKRE_SHADERC_DEPENDS CKREHostShaderc PARENT_SCOPE)
endfunction()

function(ckre_resolve_shaderc)
    if (NOT CKRE_GENERATE_SHADERS)
        return()
    endif ()

    if (CKRE_SHADERC_COMMAND)
        set(CKRE_SHADERC_COMMAND "${CKRE_SHADERC_COMMAND}" PARENT_SCOPE)
        set(CKRE_SHADERC_DEPENDS "${CKRE_SHADERC_DEPENDS}" PARENT_SCOPE)
        return()
    endif ()

    if (TARGET shaderc)
        set(CKRE_SHADERC_COMMAND "$<TARGET_FILE:shaderc>" PARENT_SCOPE)
        set(CKRE_SHADERC_DEPENDS shaderc PARENT_SCOPE)
    else ()
        message(FATAL_ERROR
                "CKRE_GENERATE_SHADERS requires bgfx shaderc. "
                "Set CKRE_SHADERC_EXECUTABLE to an existing host-compatible shaderc, "
                "or disable CKRE_GENERATE_SHADERS to use checked-in generated shader headers.")
    endif ()
endfunction()

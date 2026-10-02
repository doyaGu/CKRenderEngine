set(CKBGFX_RASTERIZER_SOURCES
    Native/CKBgfxBuffers.cpp
    Native/CKBgfxCallback.cpp
    Native/CKBgfxClear.cpp
    Native/CKBgfxCommands.cpp
    Native/CKBgfxConfig.cpp
    Native/CKBgfxDiagnostics.cpp
    Native/CKBgfxDrawMapTrace.cpp
    Native/CKBgfxInternal.cpp
    Native/CKBgfxPrograms.cpp
    Native/CKBgfxReadback.cpp
    Native/CKBgfxRenderTargets.cpp
    Native/CKBgfxSubmission.cpp
    Native/CKBgfxTextures.cpp
    Rasterizer/CKBgfxPresentStage.cpp
    Rasterizer/CKBgfxRasterizerContext.cpp
    Rasterizer/CKBgfxRasterizerDriver.cpp
    Rasterizer/CKBgfxRasterizerFrame.cpp
    Rasterizer/CKBgfxRasterizerNative.cpp
    Rasterizer/CKBgfxRasterizerReadback.cpp
    Rasterizer/CKBgfxRasterizerResources.cpp
    Rasterizer/CKBgfxRasterizerTargets.cpp
    Rasterizer/CKBgfxRasterizerTransfer.cpp
    Rasterizer/CKBgfxRasterizer.cpp
    Rasterizer/CKBgfxShaders.cpp
)
set(CKBGFX_RASTERIZER_HEADERS
    Native/CKBgfxBorderPalette.h
    Native/CKBgfxConfig.h
    Native/CKBgfxDrawMapTrace.h
    Native/CKBgfxInternal.h
    Native/CKBgfxResources.h
    Rasterizer/CKBgfxPresentStage.h
    Rasterizer/CKBgfxRasterizerContext.h
    Rasterizer/CKBgfxRasterizer.h
)
set(CKBGFX_RASTERIZER_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/CKBgfxRasterizer.ini")

# The rasterizer embeds the shaders of the bgfx renderers of the build, by
# their compile_shaders.py backends.
set(_ckbgfx_shader_definitions)
foreach (_ckbgfx_pair DIRECT3D11:dx11 DIRECT3D12:dx12 VULKAN:spirv OPENGL:glsl OPENGLES:essl METAL:metal)
    string(REPLACE ":" ";" _ckbgfx_pair "${_ckbgfx_pair}")
    list(GET _ckbgfx_pair 0 _ckbgfx_renderer)
    list(GET _ckbgfx_pair 1 _ckbgfx_backend)
    string(TOUPPER "${_ckbgfx_backend}" _ckbgfx_backend)
    if (_ckbgfx_renderer IN_LIST CKRE_BGFX_RENDERERS)
        list(APPEND _ckbgfx_shader_definitions CKBGFX_SHADER_${_ckbgfx_backend}=1)
    else ()
        list(APPEND _ckbgfx_shader_definitions CKBGFX_SHADER_${_ckbgfx_backend}=0)
    endif ()
endforeach ()

# The bgfx rasterizer owns compilation of its shader containers. The fixed-function
# source calculations remain canonical input owned by CKFFPLib.
find_package(Python3 COMPONENTS Interpreter)
set(_ckbgfx_shader_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/varying.def.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/vs_ff_3d.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/vs_ff_positiont.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/fs_ff_stage.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/vs_postprocess.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/fs_postprocess.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/fs_ff_common.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/ff_sampler_common.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/ff_sampler_shader_controlled.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/ff_sampler_hardware.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/ff_texture_ops.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/ff_fog_common.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/ff_fragment_program_layout.sh
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/ff_sampler_shader_state.sh
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/ff_sampler_layout.sh
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/CKFFFragmentProgramLayout.def
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/CKFFSamplerLayout.def
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/CKFFShaderABI.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shader_abi_codegen.py
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/compile_shaders.py
)
if (CKRE_GENERATE_SHADERS AND Python3_Interpreter_FOUND AND CKRE_SHADERC_COMMAND)
    # Every checked-in profile that the host's shaderc compiles stays current;
    # the Direct3D ones need DirectX.
    set(_ckbgfx_generate_args)
    set(_ckbgfx_generate_backends spirv glsl essl metal)
    if (CKRE_ENABLE_DIRECTX)
        list(PREPEND _ckbgfx_generate_backends dx11 dx12)
    endif ()
    foreach (_ckbgfx_backend IN LISTS _ckbgfx_generate_backends)
        list(APPEND _ckbgfx_generate_args --backend ${_ckbgfx_backend})
    endforeach ()
    set(_ckbgfx_shader_stamp "${CMAKE_CURRENT_BINARY_DIR}/CKBgfxShaderArtifacts.stamp")
    add_custom_command(OUTPUT "${_ckbgfx_shader_stamp}"
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/compile_shaders.py
            --shaderc "${CKRE_SHADERC_COMMAND}"
            --bgfx-source "${BGFX_DIR}"
            ${_ckbgfx_generate_args}
        COMMAND "${CMAKE_COMMAND}" -E touch "${_ckbgfx_shader_stamp}"
        DEPENDS ${CKRE_SHADERC_DEPENDS} ${_ckbgfx_shader_sources}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        COMMENT "Generating bgfx fixed-function shader containers"
        VERBATIM
    )
    add_custom_target(CKBgfxShaderArtifacts
        DEPENDS "${_ckbgfx_shader_stamp}"
        SOURCES ${_ckbgfx_shader_sources}
    )
elseif (CKRE_GENERATE_SHADERS)
    message(WARNING "CKBgfxShaderArtifacts target disabled: Python3 or bgfx shaderc target not available")
else ()
    message(STATUS "CKBgfxShaderArtifacts target disabled: using checked-in generated shader headers")
endif ()

if (APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    foreach (_target bx bimg bgfx)
        if (TARGET ${_target})
            set_target_properties(${_target} PROPERTIES
                CXX_COMPILER_LAUNCHER "/bin/sh;${CMAKE_CURRENT_SOURCE_DIR}/CKBgfxAppleClangLauncher.sh"
            )
            target_link_libraries(${_target} PUBLIC c++)
        endif ()
    endforeach ()
endif ()

function(ckre_configure_bgfx_rasterizer TARGET_NAME)
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
    target_compile_definitions(${TARGET_NAME} PRIVATE ${_ckbgfx_shader_definitions})
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Native
        ${CMAKE_CURRENT_SOURCE_DIR}/Rasterizer
    )
    target_link_libraries(${TARGET_NAME} PRIVATE
        CKFFPLib
        bgfx
        bimg
        bx
        SDL3::SDL3
    )
    if (CMAKE_DL_LIBS)
        target_link_libraries(${TARGET_NAME} PRIVATE ${CMAKE_DL_LIBS})
    endif ()
    if (TARGET CKBgfxShaderArtifacts)
        add_dependencies(${TARGET_NAME} CKBgfxShaderArtifacts)
    endif ()
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        FOLDER "Rasterizer"
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
endfunction()

if (CKRE_BUILD_SHARED)
    set(_ckbgfx_version_resource)
    if (WIN32)
        set(CKRE_RC_TARGET CKBgfxRasterizer)
        set(CKRE_RC_DESCRIPTION "bgfx Rasterizer")
        set(CKRE_RC_ORIGINAL_FILENAME "CKBgfxRasterizer.dll")
        set(_ckbgfx_version_resource "${CMAKE_CURRENT_BINARY_DIR}/CKBgfxRasterizerVersion.generated.rc")
        configure_file("${PROJECT_SOURCE_DIR}/src/Version.rc.in" "${_ckbgfx_version_resource}" @ONLY)
    endif ()
    add_library(CKBgfxRasterizer SHARED
        ${CKBGFX_RASTERIZER_SOURCES}
        ${CKBGFX_RASTERIZER_HEADERS}
        ${CKBGFX_RASTERIZER_CONFIG}
        ${_ckbgfx_version_resource}
    )
    ckre_configure_bgfx_rasterizer(CKBgfxRasterizer)
    if (MSVC)
        target_link_options(CKBgfxRasterizer PRIVATE /WHOLEARCHIVE:$<TARGET_FILE:bgfx>)
    endif ()
    add_custom_command(TARGET CKBgfxRasterizer POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CKBGFX_RASTERIZER_CONFIG}"
            "$<TARGET_FILE_DIR:CKBgfxRasterizer>/CKBgfxRasterizer.ini"
        VERBATIM
    )
    if (CKRE_INSTALL)
        install(TARGETS CKBgfxRasterizer
            RUNTIME DESTINATION RenderEngines COMPONENT Runtime
            LIBRARY DESTINATION RenderEngines COMPONENT Runtime
        )
        install(FILES ${CKBGFX_RASTERIZER_CONFIG}
            DESTINATION RenderEngines
            COMPONENT Runtime
        )
    endif ()
endif ()

if (CKRE_BUILD_STATIC)
    add_library(CKBgfxRasterizerStatic STATIC
        ${CKBGFX_RASTERIZER_SOURCES}
        ${CKBGFX_RASTERIZER_HEADERS}
        ${CKBGFX_RASTERIZER_CONFIG}
    )
    target_compile_definitions(CKBgfxRasterizerStatic PRIVATE CK_LIB=1)
    ckre_configure_bgfx_rasterizer(CKBgfxRasterizerStatic)
    if (NOT TARGET CKBgfxRasterizer)
        add_library(CKBgfxRasterizer ALIAS CKBgfxRasterizerStatic)
    endif ()
endif ()

if (CKRE_BUILD_TESTS AND BUILD_TESTING)
    add_executable(test_bgfx_rasterizer
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/rasterizers/test_bgfx_rasterizer.cpp)
    target_include_directories(test_bgfx_rasterizer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Native
        ${CMAKE_CURRENT_SOURCE_DIR}/Rasterizer
    )
    target_link_libraries(test_bgfx_rasterizer PRIVATE CKFFPLib bgfx bimg bx SDL3::SDL3)
    if (TARGET CKBgfxRasterizerStatic)
        target_link_libraries(test_bgfx_rasterizer PRIVATE CKBgfxRasterizerStatic)
    else ()
        target_sources(test_bgfx_rasterizer PRIVATE
            ${CKBGFX_RASTERIZER_SOURCES}
            ${CKBGFX_RASTERIZER_HEADERS}
        )
        target_compile_definitions(test_bgfx_rasterizer PRIVATE CK_LIB=1 ${_ckbgfx_shader_definitions})
    endif ()
    set_target_properties(test_bgfx_rasterizer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        FOLDER "Tests"
    )
    ckre_copy_runtime_dependencies(test_bgfx_rasterizer)
    add_test(NAME BgfxRasterizerTests COMMAND $<TARGET_FILE:test_bgfx_rasterizer>)
    set_tests_properties(BgfxRasterizerTests PROPERTIES TIMEOUT 60)
endif ()

# Fixed-function translation and the provider-neutral modern graphics API
# compatibility layer used by SDL_gpu and bgfx rasterizers.
set(CKFFP_LIB_SOURCES
    CKBackendDisplayCaps.cpp
    CKBackendProgram.cpp
    CKBackendProgramLayout.cpp
    CKDrawStateCache.cpp
    CKFFDebug.cpp
    CKFFDrawProbes.cpp
    CKFFDrawTypes.cpp
    CKFFPipelineGuards.cpp
    CKFFPipelineState.cpp
    CKFFShaderCache.cpp
    CKFFShaderInterface.cpp
    CKFFShaderKey.cpp
    CKFFSpecializationInfo.cpp
    CKFFStageState.cpp
    CKFFStateResolver.cpp
    CKFFStateStore.cpp
    CKFFTextureBinder.cpp
    CKFFUniformEmitter.cpp
    CKFFUniformState.cpp
    CKFixedFunctionPipeline.cpp
    CKPresentStage.cpp
    CKRasterizerPlugin.cpp
    CKTransientGeometry.cpp
    CKTranslatedContext.cpp
    CKTranslatedFrame.cpp
    CKTranslatedRasterizer.cpp
    CKVertexLayoutCache.cpp
)

set(CKFFP_LIB_HEADERS
    CKBackendDrawData.h
    CKBackendProgram.h
    CKBackendProgramLayout.h
    CKBuiltinShaders.h
    CKDrawStateCache.h
    CKFFConstants.h
    CKFFDebug.h
    CKFFDrawProbes.h
    CKFFDrawTypes.h
    CKFFNativeLayout.def
    CKFFShaderABI.h
    CKFFShaderCache.h
    CKFFShaderInterface.h
    CKFFShaderKey.h
    CKFFSpecializationInfo.h
    CKFFSpecLayout.def
    CKFFSpecLayout.h
    CKFFStageState.h
    CKFFStateDesc.h
    CKFFStateResolver.h
    CKFFStateStore.h
    CKFFTextureBinder.h
    CKFFUniformEmitter.h
    CKFFUniformState.h
    CKFixedFunctionPipeline.h
    CKPresentStage.h
    CKRasterizerBackend.h
    CKRasterizerBackendEnums.h
    CKRasterizerBackendTypes.h
    CKRasterizerPlugin.h
    CKRasterizerValidation.h
    CKTransientGeometry.h
    CKTranslatedRasterizer.h
    CKTranslatedRasterizerInternal.h
    CKVertexLayoutCache.h
)

add_library(CKFFPLib STATIC ${CKFFP_LIB_SOURCES} ${CKFFP_LIB_HEADERS})
target_compile_features(CKFFPLib PRIVATE cxx_std_20)
ckre_apply_diagnostic_definitions(CKFFPLib PRIVATE)
target_include_directories(CKFFPLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(CKFFPLib
    PUBLIC CKRasterizerLib
    PRIVATE SDL3::SDL3
)
set_target_properties(CKFFPLib PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    FOLDER "Rasterizer"
    POSITION_INDEPENDENT_CODE ON
)

# Canonical FFP shader calculations are consumed by provider-owned generators.
# Generated shader binaries remain in each concrete rasterizer.
find_package(Python3 COMPONENTS Interpreter)
set(CKRE_SHADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vs_ff_3d.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vs_ff_positiont.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fs_ff_stage.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vs_postprocess.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fs_postprocess.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fs_ff_common.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ff_fog_common.sc
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ff_spec_layout.sh
    ${CMAKE_CURRENT_SOURCE_DIR}/CKFFSpecLayout.def
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/compile_shaders.py
)
if (CKRE_GENERATE_SHADERS AND Python3_Interpreter_FOUND AND CKRE_SHADERC_COMMAND)
    set(CKRE_SHADER_STAMP "${CMAKE_CURRENT_BINARY_DIR}/CK2_3D_shaders.stamp")
    add_custom_command(OUTPUT "${CKRE_SHADER_STAMP}"
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/compile_shaders.py
            --shaderc "${CKRE_SHADERC_COMMAND}"
            --bgfx-source "${BGFX_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${CKRE_SHADER_STAMP}"
        DEPENDS ${CKRE_SHADERC_DEPENDS} ${CKRE_SHADER_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        COMMENT "Generating CK2_3D bgfx fixed-function shaders"
        VERBATIM
    )
    add_custom_target(CK2_3D_shaders
        DEPENDS "${CKRE_SHADER_STAMP}"
        SOURCES ${CKRE_SHADER_SOURCES}
    )
elseif (CKRE_GENERATE_SHADERS)
    message(WARNING "CK2_3D_shaders target disabled: Python3 or bgfx shaderc target not available")
else ()
    message(STATUS "CK2_3D_shaders target disabled: using checked-in generated shader headers")
endif ()

# Fixed-function translation and the provider-neutral modern graphics API
# compatibility layer used by SDL_gpu and bgfx rasterizers.
set(CKFFP_LIB_SOURCES
    Backend/CKBackendProgram.cpp
    Backend/CKBackendProgramLayout.cpp
    FixedFunction/CKDrawStateCache.cpp
    FixedFunction/CKFFDebug.cpp
    FixedFunction/CKFFDrawProbes.cpp
    FixedFunction/CKFFDrawTypes.cpp
    FixedFunction/CKFFPipelineGuards.cpp
    FixedFunction/CKFFPipelineState.cpp
    FixedFunction/CKFFShaderCache.cpp
    FixedFunction/CKFFShaderInterface.cpp
    FixedFunction/CKFFShaderKey.cpp
    FixedFunction/CKFFSpecializationInfo.cpp
    FixedFunction/CKFFStageState.cpp
    FixedFunction/CKFFStateResolver.cpp
    FixedFunction/CKFFStateStore.cpp
    FixedFunction/CKFFTextureBinder.cpp
    FixedFunction/CKFFUniformEmitter.cpp
    FixedFunction/CKFFUniformState.cpp
    FixedFunction/CKFixedFunctionPipeline.cpp
    FixedFunction/CKTransientGeometry.cpp
    FixedFunction/CKVertexLayoutCache.cpp
    Translation/CKBackendDisplayCaps.cpp
    Translation/CKPresentStage.cpp
    Translation/CKRasterizerPlugin.cpp
    Translation/CKTranslatedContext.cpp
    Translation/CKTranslatedFrame.cpp
    Translation/CKTranslatedRasterizer.cpp
)

set(CKFFP_LIB_HEADERS
    Backend/CKBackendDrawData.h
    Backend/CKBackendProgram.h
    Backend/CKBackendProgramLayout.h
    Backend/CKRasterizerBackend.h
    Backend/CKRasterizerBackendEnums.h
    Backend/CKRasterizerBackendTypes.h
    Backend/CKRasterizerValidation.h
    FixedFunction/CKBuiltinShaders.h
    FixedFunction/CKDrawStateCache.h
    FixedFunction/CKFFConstants.h
    FixedFunction/CKFFDebug.h
    FixedFunction/CKFFDrawProbes.h
    FixedFunction/CKFFDrawTypes.h
    FixedFunction/CKFFNativeLayout.def
    FixedFunction/CKFFShaderABI.h
    FixedFunction/CKFFShaderCache.h
    FixedFunction/CKFFShaderInterface.h
    FixedFunction/CKFFShaderKey.h
    FixedFunction/CKFFSpecializationInfo.h
    FixedFunction/CKFFSpecLayout.def
    FixedFunction/CKFFSpecLayout.h
    FixedFunction/CKFFStageState.h
    FixedFunction/CKFFStateDesc.h
    FixedFunction/CKFFStateResolver.h
    FixedFunction/CKFFStateStore.h
    FixedFunction/CKFFTextureBinder.h
    FixedFunction/CKFFUniformEmitter.h
    FixedFunction/CKFFUniformState.h
    FixedFunction/CKFixedFunctionPipeline.h
    FixedFunction/CKTransientGeometry.h
    FixedFunction/CKVertexLayoutCache.h
    Translation/CKPresentStage.h
    Translation/CKRasterizerPlugin.h
    Translation/CKTranslatedRasterizer.h
    Translation/CKTranslatedRasterizerInternal.h
)

add_library(CKFFPLib STATIC ${CKFFP_LIB_SOURCES} ${CKFFP_LIB_HEADERS})
target_compile_features(CKFFPLib PRIVATE cxx_std_20)
ckre_apply_diagnostic_definitions(CKFFPLib PRIVATE)
target_include_directories(CKFFPLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Backend>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/FixedFunction>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Translation>
)
target_link_libraries(CKFFPLib
    PUBLIC CKRasterizerLib
    PRIVATE SDL3::SDL3
)
set_target_properties(CKFFPLib PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    FOLDER "Rasterizer"
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)

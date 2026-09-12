# Reusable fixed-function implementation and modern graphics backend Interface
# used by SDL_gpu and bgfx rasterizers.
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
    ShaderModel/CKFFShaderInterface.cpp
    ShaderModel/CKFFShaderKey.cpp
    ShaderModel/CKFFSpecializationInfo.cpp
    FixedFunction/CKFFStageState.cpp
    FixedFunction/CKFFStateResolver.cpp
    FixedFunction/CKFFStateStore.cpp
    FixedFunction/CKFFTextureBinder.cpp
    FixedFunction/CKFFUniformEmitter.cpp
    FixedFunction/CKFFUniformState.cpp
    FixedFunction/CKFixedFunctionPipeline.cpp
    FixedFunction/CKTransientGeometry.cpp
    FixedFunction/CKVertexLayoutCache.cpp
    Context/CKPresentStage.cpp
    Context/CKTranslatedContext.cpp
    Context/CKTranslatedFrame.cpp
    Context/CKTranslatedReadback.cpp
    Context/CKTranslatedResources.cpp
    Context/CKTranslatedTargets.cpp
    Context/CKTranslatedTransfer.cpp
)

set(CKFFP_LIB_HEADERS
    Interface/CKBackendDrawData.h
    Interface/CKBackendProgram.h
    Interface/CKBackendProgramLayout.h
    Interface/CKBuiltinShaderIdentity.h
    Interface/CKBuiltinShaders.h
    Interface/CKFFRasterizerContext.h
    Interface/CKFFNativeLayout.def
    Interface/CKRasterizerBackend.h
    Interface/CKRasterizerBackendEnums.h
    Interface/CKRasterizerBackendTypes.h
    Interface/CKRasterizerValidation.h
    FixedFunction/CKDrawStateCache.h
    FixedFunction/CKFFConstants.h
    FixedFunction/CKFFDebug.h
    FixedFunction/CKFFDrawProbes.h
    FixedFunction/CKFFDrawTypes.h
    FixedFunction/CKFFShaderCache.h
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
    ShaderModel/CKFFShaderABI.h
    ShaderModel/CKFFShaderInterface.h
    ShaderModel/CKFFShaderKey.h
    ShaderModel/CKFFSpecializationInfo.h
    ShaderModel/CKFFSpecLayout.def
    ShaderModel/CKFFSpecLayout.h
    Context/CKPresentStage.h
    Context/CKFFRasterizerContextInternal.h
)

add_library(CKFFPLib STATIC ${CKFFP_LIB_SOURCES} ${CKFFP_LIB_HEADERS})
target_compile_features(CKFFPLib PRIVATE cxx_std_20)
ckre_apply_diagnostic_definitions(CKFFPLib PRIVATE)
target_include_directories(CKFFPLib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Interface>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Backend
        ${CMAKE_CURRENT_SOURCE_DIR}/FixedFunction
        ${CMAKE_CURRENT_SOURCE_DIR}/ShaderModel
        ${CMAKE_CURRENT_SOURCE_DIR}/Context
)
target_link_libraries(CKFFPLib
    PUBLIC CKRasterizerLib
)
set_target_properties(CKFFPLib PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    FOLDER "Rasterizer"
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)

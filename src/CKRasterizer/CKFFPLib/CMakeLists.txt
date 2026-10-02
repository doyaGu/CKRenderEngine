# Reusable CPU fixed-function implementation shared by the concrete
# SDL_gpu and bgfx rasterizer contexts.
set(CKFFP_LIB_SOURCES
    Core/CKFFProgramDesc.cpp
    Core/CKFFProgramLayout.cpp
    FixedFunction/CKDrawStateCache.cpp
    FixedFunction/CKFFBufferUseTracker.cpp
    FixedFunction/CKFFBufferData.cpp
    FixedFunction/CKFFContextState.cpp
    FixedFunction/CKFFDebug.cpp
    FixedFunction/CKFFDrawProbes.cpp
    FixedFunction/CKFFDrawTypes.cpp
    FixedFunction/CKFFImage.cpp
    FixedFunction/CKFFResourceStore.cpp
    FixedFunction/CKFFPipelineGuards.cpp
    FixedFunction/CKFFPipelineState.cpp
    FixedFunction/CKFFPresentDraw.cpp
    FixedFunction/CKFFShaderCache.cpp
    ShaderModel/CKFFShaderInterface.cpp
    ShaderModel/CKFFShaderKey.cpp
    ShaderModel/CKFFFragmentProgram.cpp
    ShaderModel/CKFFNativeFragmentJit.cpp
    FixedFunction/CKFFStageState.cpp
    FixedFunction/CKFFStateResolver.cpp
    FixedFunction/CKFFStateStore.cpp
    FixedFunction/CKFFTextureBinder.cpp
    FixedFunction/CKFFUniformEmitter.cpp
    FixedFunction/CKFFUniformState.cpp
    FixedFunction/CKFixedFunctionPipeline.cpp
    FixedFunction/CKTransientGeometry.cpp
    FixedFunction/CKVertexLayoutCache.cpp
)

set(CKFFP_LIB_HEADERS
    Interface/CKFFConstantSet.h
    Interface/CKFFPipelineState.h
    Interface/CKFFTextureBindings.h
    Interface/CKFFProgramDesc.h
    Interface/CKFFProgramLayout.h
    Interface/CKBuiltinShaderIdentity.h
    Interface/CKBuiltinShaders.h
    Interface/CKFFNativeLayout.def
    Interface/CKRasterizerContextData.h
    Interface/CKRasterizerContextEnums.h
    Interface/CKRasterizerContextTypes.h
    Interface/CKRasterizerValidation.h
    FixedFunction/CKDrawStateCache.h
    FixedFunction/CKFFBufferUseTracker.h
    FixedFunction/CKFFBufferData.h
    FixedFunction/CKFFContextState.h
    FixedFunction/CKFFConstants.h
    FixedFunction/CKFFDebug.h
    FixedFunction/CKFFDrawProbes.h
    FixedFunction/CKFFDrawTypes.h
    FixedFunction/CKFFImage.h
    FixedFunction/CKFFPresentDraw.h
    FixedFunction/CKFFProgram.h
    FixedFunction/CKFFResourceStore.h
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
    ShaderModel/CKFFSamplerLayout.def
    ShaderModel/CKFFFragmentProgram.h
    ShaderModel/CKFFFragmentProgramLayout.def
    ShaderModel/CKFFFragmentProgramLayout.h
    ShaderModel/CKFFNativeFragmentJit.h
)

add_library(CKFFPLib STATIC ${CKFFP_LIB_SOURCES} ${CKFFP_LIB_HEADERS})
target_compile_features(CKFFPLib PRIVATE cxx_std_20)
ckre_apply_diagnostic_definitions(CKFFPLib PRIVATE)
target_include_directories(CKFFPLib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Interface>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/FixedFunction
        ${CMAKE_CURRENT_SOURCE_DIR}/ShaderModel
)
target_link_libraries(CKFFPLib
    PUBLIC CKRasterizerLib CKShaderJit
)
set_target_properties(CKFFPLib PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    FOLDER "Rasterizer"
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)

# Reusable CPU fixed-function implementation shared by the concrete
# SDL_gpu and bgfx rasterizer contexts. Each layer includes only the layers
# listed before it:
#   Interface      data contracts shared with the rasterizer contexts
#   State          fixed-function state vocabulary and texture stage rules
#   ShaderModel    shader keys, shader ABI and the native fragment JIT front end
#   FixedFunction  the pipeline runtime the contexts own
# Core implements Interface.
set(CKFFP_LIB_SOURCES
    Core/CKFFProgramDesc.cpp
    Core/CKFFProgramLayout.cpp
    State/CKFFStageState.cpp
    ShaderModel/CKFFShaderInterface.cpp
    ShaderModel/CKFFShaderKey.cpp
    ShaderModel/CKFFFragmentProgram.cpp
    ShaderModel/CKFFNativeFragmentJit.cpp
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
    State/CKFFConstants.h
    State/CKFFStageState.h
    State/CKFFStateDesc.h
    State/CKFFVertexFormat.h
    ShaderModel/CKFFShaderABI.h
    ShaderModel/CKFFShaderInterface.h
    ShaderModel/CKFFShaderKey.h
    ShaderModel/CKFFSamplerLayout.h
    ShaderModel/CKFFSamplerLayout.def
    ShaderModel/CKFFFragmentProgram.h
    ShaderModel/CKFFFragmentProgramLayout.def
    ShaderModel/CKFFFragmentProgramLayout.h
    ShaderModel/CKFFNativeFragmentJit.h
    FixedFunction/CKDrawStateCache.h
    FixedFunction/CKFFBufferUseTracker.h
    FixedFunction/CKFFBufferData.h
    FixedFunction/CKFFContextState.h
    FixedFunction/CKFFDebug.h
    FixedFunction/CKFFDrawProbes.h
    FixedFunction/CKFFDrawTypes.h
    FixedFunction/CKFFImage.h
    FixedFunction/CKFFPresentDraw.h
    FixedFunction/CKFFProgram.h
    FixedFunction/CKFFResourceStore.h
    FixedFunction/CKFFShaderCache.h
    FixedFunction/CKFFStateResolver.h
    FixedFunction/CKFFStateStore.h
    FixedFunction/CKFFTextureBinder.h
    FixedFunction/CKFFUniformEmitter.h
    FixedFunction/CKFFUniformState.h
    FixedFunction/CKFixedFunctionPipeline.h
    FixedFunction/CKTransientGeometry.h
    FixedFunction/CKVertexLayoutCache.h
)

add_library(CKFFPLib STATIC ${CKFFP_LIB_SOURCES} ${CKFFP_LIB_HEADERS})
target_compile_features(CKFFPLib PRIVATE cxx_std_20)
ckre_apply_diagnostic_definitions(CKFFPLib PRIVATE)
# Rasterizer contexts own the fixed-function objects and build shaders from the
# shader model, so every layer except Core is part of the library interface.
target_include_directories(CKFFPLib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Interface>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/State>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/FixedFunction>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ShaderModel>
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

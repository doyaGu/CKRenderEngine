# CKRasterizerLib - translation core shared by the rasterizer plugins:
#   * backend interface (CKRasterizerBackend.h) with its NULL implementation
#   * fixed-function pipeline translation (CKFF*, CKFixedFunctionPipeline,
#     draw-state / vertex-layout caches, transient geometry, postprocess pass)
#   * render settings / debug logger / frame-cost stats it depends on
#   * bgfx fixed-function shaders (shaders/, generated headers checked in):
#     one program family (vs_ff_3d[_clip], vs_ff_positiont[_clip], fs_ff_stage)
set(CKRASTERIZER_LIB_SOURCES
        CKRasterizerCapsBaseline.cpp

        CKFixedFunctionPipeline.cpp
        CKFFPipelineState.cpp
        CKFFPipelineGuards.cpp
        CKFFDrawProbes.cpp
        CKFFDrawTypes.cpp
        CKFFDrawPreparer.cpp
        CKFFStateStore.cpp
        CKFFStateResolver.cpp
        CKFFTextureBinder.cpp
        CKFFUniformEmitter.cpp
        CKFFStageState.cpp
        CKFFUniformState.cpp
        CKFFShaderKey.cpp
        CKFFSpecializationInfo.cpp
        CKFFShaderCache.cpp
        CKDrawAnnotation.cpp
        CKDrawAnnotationParser.cpp
        CKDrawStateCache.cpp
        CKVertexLayoutCache.cpp
        CKTransientGeometry.cpp
        CKPresentStage.cpp
        CKRenderSettings.cpp
        CKRasterizerBackend.cpp
        CKNullBackend.cpp

        CKTranslatedRasterizer.cpp
        CKTranslatedContext.cpp
        CKTranslatedFrame.cpp
)

set(CKRASTERIZER_LIB_HEADERS
        CKRasterizerValidation.h
        CKRasterizerCapsBaseline.h
        CKRasterizerCapsBaseline.generated.h
        CKRasterizerBackendEnums.h
        CKRasterizerBackendTypes.h
        CKRasterizerBackend.h
        CKNullBackend.h
        ${CKRE_INCLUDE_DIR}/CKRasterizer.h
        ${CKRE_INCLUDE_DIR}/CKRasterizerEnums.h
        ${CKRE_INCLUDE_DIR}/CKRasterizerTypes.h

        CKFixedFunctionPipeline.h
        CKRenderConfig.h
        CKDebugLogger.h
        CKFFDebug.h
        CKFFDrawProbes.h
        CKFFStageState.h
        CKFFShaderABI.h
        CKFFUniformState.h
        CKFFShaderKey.h
        CKFFSpecializationInfo.h
        CKFFSpecLayout.h
        CKFFSpecLayout.def
        CKFFStateDesc.h
        CKFFShaderCache.h
        CKFFConstants.h
        CKFFDrawTypes.h
        CKFFDrawPreparer.h
        CKFFStateStore.h
        CKFFStateResolver.h
        CKFFTextureBinder.h
        CKFFUniformEmitter.h
        CKDrawAnnotation.h
        CKDrawStateCache.h
        CKVertexLayoutCache.h
        CKTransientGeometry.h
        CKPresentStage.h
        CKRenderSettings.h
        CKTranslatedRasterizer.h
        CKRenderPerfClock.h
        CKRenderFrameCostStats.h
)

if (CKRE_ENABLE_DEBUG_LOGGER)
    list(APPEND CKRASTERIZER_LIB_SOURCES CKDebugLogger.cpp)
endif ()

if (CKRE_ENABLE_RENDER_STATS)
    list(APPEND CKRASTERIZER_LIB_SOURCES CKRenderPerfClock.cpp)
endif ()

if (CKRE_ENABLE_FRAME_COST_STATS)
    list(APPEND CKRASTERIZER_LIB_SOURCES CKRenderFrameCostStats.cpp)
endif ()

if (CKRE_ENABLE_FFP_DIAGNOSTICS)
    list(APPEND CKRASTERIZER_LIB_SOURCES CKFFDebug.cpp)
endif ()

add_library(CKRasterizerLib STATIC ${CKRASTERIZER_LIB_SOURCES} ${CKRASTERIZER_LIB_HEADERS})
target_compile_features(CKRasterizerLib PRIVATE cxx_std_20)
# The diagnostic macros change class layouts (e.g. CKFixedFunctionPipeline under
# CKRE_ENABLE_FFP_DIAGNOSTICS). They must be PUBLIC here so every consumer of
# the translation core sees the same definitions (ODR).
ckre_apply_diagnostic_definitions(CKRasterizerLib PUBLIC)

target_include_directories(CKRasterizerLib
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CKRE_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set(_ckre_rasterizer_ck2_dep CK2)
if (CKRE_BUILD_STATIC AND TARGET CK2Static)
    set(_ckre_rasterizer_ck2_dep CK2Static)
endif ()

set(_ckre_rasterizer_vxmath_dep VxMath)
if (CKRE_BUILD_STATIC AND TARGET VxMathStatic)
    set(_ckre_rasterizer_vxmath_dep VxMathStatic)
endif ()

target_link_libraries(CKRasterizerLib PUBLIC
        ${_ckre_rasterizer_ck2_dep}
        ${_ckre_rasterizer_vxmath_dep}
)

set_target_properties(CKRasterizerLib PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        FOLDER "Rasterizer"
)


# -----------------------------------------------------------------------------
# bgfx fixed-function shaders. The generated headers are checked in; the
# CK2_3D_shaders target regenerates them when CKRE_GENERATE_SHADERS is ON.
# -----------------------------------------------------------------------------
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
    add_dependencies(CKRasterizerLib CK2_3D_shaders)
elseif (CKRE_GENERATE_SHADERS)
    message(WARNING "CK2_3D_shaders target disabled: Python3 or bgfx shaderc target not available")
else ()
    message(STATUS "CK2_3D_shaders target disabled: using checked-in generated shader headers")
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CKRE_BUILD_SHARED)
    set_target_properties(CKRasterizerLib PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()

if (CKRE_INSTALL)
    install(TARGETS CKRasterizerLib
            EXPORT CKRenderEngineTargets
            ARCHIVE DESTINATION lib
    )
endif ()

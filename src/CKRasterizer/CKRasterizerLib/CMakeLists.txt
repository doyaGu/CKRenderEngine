# Fixed-function translation, presentation, and rasterizer/plugin composition.
# Generic device contracts and shared diagnostics are separate dependencies.
set(CKRASTERIZER_LIB_SOURCES
        CKRasterizerCapsBaseline.cpp

        CKFFDebug.cpp
        CKFFShaderInterface.cpp
        CKRasterizerPlugin.cpp
        CKNullRasterizer.cpp
        CKFixedFunctionPipeline.cpp
        CKFFPipelineState.cpp
        CKFFPipelineGuards.cpp
        CKFFDrawProbes.cpp
        CKFFDrawTypes.cpp
        CKFFStateStore.cpp
        CKFFStateResolver.cpp
        CKFFTextureBinder.cpp
        CKFFUniformEmitter.cpp
        CKFFStageState.cpp
        CKFFUniformState.cpp
        CKFFShaderKey.cpp
        CKFFSpecializationInfo.cpp
        CKFFShaderCache.cpp
        CKDrawStateCache.cpp
        CKVertexLayoutCache.cpp
        CKTransientGeometry.cpp
        CKPresentStage.cpp
        CKBackendDisplayCaps.cpp

        CKTranslatedRasterizer.cpp
        CKTranslatedContext.cpp
        CKTranslatedFrame.cpp
)

set(CKRASTERIZER_LIB_HEADERS
        CKRasterizerCapsBaseline.h
        CKRasterizerCapsBaseline.generated.h
        CKBuiltinShaders.h
        CKFFShaderInterface.h
        CKRasterizerPlugin.h
        CKNullRasterizer.h
        ${CKRE_INCLUDE_DIR}/CKRasterizer.h
        ${CKRE_INCLUDE_DIR}/CKRasterizerEnums.h
        ${CKRE_INCLUDE_DIR}/CKRasterizerTypes.h

        CKFixedFunctionPipeline.h
        CKFFDebug.h
        CKFFDrawProbes.h
        CKFFStageState.h
        CKFFShaderABI.h
        CKFFNativeLayout.def
        CKFFUniformState.h
        CKFFShaderKey.h
        CKFFSpecializationInfo.h
        CKFFSpecLayout.h
        CKFFSpecLayout.def
        CKFFStateDesc.h
        CKFFShaderCache.h
        CKFFConstants.h
        CKFFDrawTypes.h
        CKFFStateStore.h
        CKFFStateResolver.h
        CKFFTextureBinder.h
        CKFFUniformEmitter.h
        CKDrawStateCache.h
        CKVertexLayoutCache.h
        CKTransientGeometry.h
        CKPresentStage.h
        CKTranslatedRasterizer.h
        CKTranslatedRasterizerInternal.h
)

add_library(CKRasterizerLib STATIC ${CKRASTERIZER_LIB_SOURCES} ${CKRASTERIZER_LIB_HEADERS})
target_compile_features(CKRasterizerLib PRIVATE cxx_std_20)
# Diagnostics affect translation-core implementation only; the public
# CKRasterizer v3 contract and all internal class layouts stay unchanged.
ckre_apply_diagnostic_definitions(CKRasterizerLib PRIVATE)

target_include_directories(CKRasterizerLib
        PUBLIC
        $<BUILD_INTERFACE:${CKRE_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
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
        CKRasterizerBackend
        CKRenderSupport
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

if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CKRE_BUILD_SHARED)
    set_target_properties(CKRasterizerLib PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()

if (CKRE_INSTALL)
    install(TARGETS CKRasterizerLib
            EXPORT CKRenderEngineTargets
            ARCHIVE DESTINATION lib
    )
endif ()

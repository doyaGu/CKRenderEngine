file(MAKE_DIRECTORY "${CKRE_GENERATED_INCLUDE_DIR}")
configure_file(Version.h.in "${CKRE_VERSION_HEADER}" @ONLY)

set(CKRE_PUBLIC_HEADERS
        ${CKRE_VERSION_HEADER}
        ${CKRE_INCLUDE_DIR}/CKRenderEngineTypes.h
        ${CKRE_INCLUDE_DIR}/CKRenderEngineEnums.h

        ${CKRE_INCLUDE_DIR}/CKRasterizer.h
        ${CKRE_INCLUDE_DIR}/CKRasterizerEnums.h
        ${CKRE_INCLUDE_DIR}/CKRasterizerTypes.h

        ${CKRE_INCLUDE_DIR}/MeshAdjacency.h
        ${CKRE_INCLUDE_DIR}/RadixSort.h
        ${CKRE_INCLUDE_DIR}/MeshStriper.h
        ${CKRE_INCLUDE_DIR}/NvStripifier.h
        ${CKRE_INCLUDE_DIR}/VertexCache.h
        ${CKRE_INCLUDE_DIR}/VertexCacheOptimizer.h
        ${CKRE_INCLUDE_DIR}/NearestPointGrid.h
        ${CKRE_INCLUDE_DIR}/PlaceFitter.h

        ${CKRE_INCLUDE_DIR}/RCKRenderManager.h
        ${CKRE_INCLUDE_DIR}/RCKRenderContext.h
        ${CKRE_INCLUDE_DIR}/RCKMaterial.h
        ${CKRE_INCLUDE_DIR}/RCKTexture.h
        ${CKRE_INCLUDE_DIR}/RCKMesh.h
        ${CKRE_INCLUDE_DIR}/RCKPatchMesh.h
        ${CKRE_INCLUDE_DIR}/RCKAnimation.h
        ${CKRE_INCLUDE_DIR}/RCKKeyedAnimation.h
        ${CKRE_INCLUDE_DIR}/RCKObjectAnimation.h
        ${CKRE_INCLUDE_DIR}/RCKKeyframeData.h
        ${CKRE_INCLUDE_DIR}/RCKKinematicChain.h
        ${CKRE_INCLUDE_DIR}/RCKLayer.h
        ${CKRE_INCLUDE_DIR}/RCKRenderObject.h
        ${CKRE_INCLUDE_DIR}/RCK2dEntity.h
        ${CKRE_INCLUDE_DIR}/RCK3dEntity.h
        ${CKRE_INCLUDE_DIR}/RCKCamera.h
        ${CKRE_INCLUDE_DIR}/RCKLight.h
        ${CKRE_INCLUDE_DIR}/RCKCurvePoint.h
        ${CKRE_INCLUDE_DIR}/RCKCurve.h
        ${CKRE_INCLUDE_DIR}/RCK3dObject.h
        ${CKRE_INCLUDE_DIR}/RCKSprite3D.h
        ${CKRE_INCLUDE_DIR}/RCKCharacter.h
        ${CKRE_INCLUDE_DIR}/RCKPlace.h
        ${CKRE_INCLUDE_DIR}/RCKGrid.h
        ${CKRE_INCLUDE_DIR}/RCKBodyPart.h
        ${CKRE_INCLUDE_DIR}/RCKTargetCamera.h
        ${CKRE_INCLUDE_DIR}/RCKTargetLight.h
        ${CKRE_INCLUDE_DIR}/RCKSprite.h
        ${CKRE_INCLUDE_DIR}/RCKSpriteText.h
        ${CKRE_INCLUDE_DIR}/RCKSkin.h

        ${CKRE_INCLUDE_DIR}/CKRenderedScene.h
        ${CKRE_INCLUDE_DIR}/CKSceneGraph.h
        ${CKRE_INCLUDE_DIR}/RCKVertexBuffer.h
)

set(CKRE_PRIVATE_HEADERS
        CKFixedFunctionPipeline.h
        CKFFDebug.h
        CKFFStageState.h
        CKFFUniformState.h
        CKFFShaderKey.h
        CKFFSpecializationInfo.h
        CKFFSpecializedModuleTable.h
        CKFFStateDesc.h
        CKFFShaderCache.h
        CKFFConstants.h
        CKDrawStateCache.h
        CKVertexLayoutCache.h
        CKTransientGeometry.h
        CKRenderPipeline.h
        CKFrustumCuller.h
        CKRenderDebugEnv.h
        CKRenderPerfStats.h
)

set(CKRE_SOURCES
        CK2_3D.cpp

        CKRenderManager.cpp
        CKRenderContext.cpp
        CKCallbacksContainer.cpp

        CKMaterial.cpp
        CKTexture.cpp
        CKMesh.cpp
        CKMeshUtils.cpp
        CKPatchMesh.cpp
        CKAnimation.cpp
        CKKeyedAnimation.cpp
        CKObjectAnimation.cpp
        CKKeyframeData.cpp
        CKKinematicChain.cpp
        CKLayer.cpp
        CKRenderObject.cpp
        CK2dEntity.cpp
        CK3dEntity.cpp
        CKCamera.cpp
        CKLight.cpp
        CKCurvePoint.cpp
        CKCurve.cpp
        CK3dObject.cpp
        CKSprite3D.cpp
        CKCharacter.cpp
        CKPlace.cpp
        CKGrid.cpp
        CKBodyPart.cpp
        CKTargetCamera.cpp
        CKTargetLight.cpp
        CKSprite.cpp
        CKSpriteText.cpp
        CKSkin.cpp

        MeshAdjacency.cpp
        RadixSort.cpp
        MeshStriper.cpp
        NvStripifier.cpp
        VertexCacheOptimizer.cpp
        NearestPointGrid.cpp
        PlaceFitter.cpp

        CKRenderedScene.cpp
        CKSceneGraph.cpp
        CKVertexBuffer.cpp
        CKRasterizer/CKNULLRasterizer.cpp

        CKFixedFunctionPipeline.cpp
        CKFFDebug.cpp
        CKFFStageState.cpp
        CKFFUniformState.cpp
        CKFFShaderKey.cpp
        CKFFSpecializationInfo.cpp
        CKFFSpecializedModuleTable.cpp
        CKFFShaderCache.cpp
        CKDrawStateCache.cpp
        CKVertexLayoutCache.cpp
        CKTransientGeometry.cpp
        CKRenderPipeline.cpp
        CKFrustumCuller.cpp
        CKRenderDebugEnv.cpp
        CKRenderPerfStats.cpp

        CKDebugLogger.cpp
        CKException.cpp

        CK2_3D.rc
)

# Add the bgfx rasterizer plugin targets when requested.
if (CKRE_BUILD_BGFX_RASTERIZER AND (CKRE_BUILD_SHARED OR CKRE_BUILD_STATIC))
    add_subdirectory(CKRasterizer)
endif ()

find_package(Python3 COMPONENTS Interpreter)
set(CKRE_SHADER_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vs_ff_3d.sc
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vs_ff_positiont.sc
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fs_ff_stage.sc
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fs_ff_common.sc
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ffp_specialized_variants.json
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/compile_shaders.py
)

if (Python3_Interpreter_FOUND AND TARGET shaderc)
    add_custom_target(CK2_3D_shaders
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/compile_shaders.py
                    --shaderc $<TARGET_FILE:shaderc>
            DEPENDS shaderc ${CKRE_SHADER_SOURCES}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shaders
            SOURCES ${CKRE_SHADER_SOURCES}
            COMMENT "Generating CK2_3D bgfx fixed-function shaders"
    )
else ()
    message(WARNING "CK2_3D_shaders target disabled: Python3 or bgfx shaderc target not available")
endif ()

# Function to configure common target settings
function(ckre_configure_target TARGET_NAME)
    target_include_directories(${TARGET_NAME}
            PUBLIC
            $<BUILD_INTERFACE:${CKRE_GENERATED_INCLUDE_DIR}>
            $<BUILD_INTERFACE:${CKRE_INCLUDE_DIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
            PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    get_target_property(_ckre_type ${TARGET_NAME} TYPE)
    if (_ckre_type STREQUAL "STATIC_LIBRARY")
        set(_ckre_ck2_dep CK2)
        if (TARGET CK2Static)
            set(_ckre_ck2_dep CK2Static)
        endif ()
        set(_ckre_vxmath_dep VxMath)
        if (TARGET VxMathStatic)
            set(_ckre_vxmath_dep VxMathStatic)
        endif ()
        target_link_libraries(${TARGET_NAME} PUBLIC ${_ckre_ck2_dep} ${_ckre_vxmath_dep})
    else ()
        target_link_libraries(${TARGET_NAME} PRIVATE CK2 VxMath)
    endif ()

    if (WIN32)
        target_link_libraries(${TARGET_NAME} PRIVATE dbghelp)
    endif ()

    target_compile_definitions(${TARGET_NAME} PRIVATE
            CKRE_DEBUG_OUTPUT_DEFAULT=$<IF:$<BOOL:${CKRE_DEBUG_OUTPUT}>,1,0>
    )

    set_target_properties(${TARGET_NAME} PROPERTIES
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR}
    )
endfunction()

# Track created targets for export
set(CKRE_TARGETS)

if (CKRE_BUILD_SHARED)
    add_library(CK2_3D SHARED ${CKRE_SOURCES} ${CKRE_PUBLIC_HEADERS} ${CKRE_PRIVATE_HEADERS})
    ckre_configure_target(CK2_3D)

    # Shared library specific settings
    set_target_properties(CK2_3D PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )

    # CK2_3D consumer code not yet rewritten for v2 API
    if (TARGET CK2_3D_shaders)
        add_dependencies(CK2_3D CK2_3D_shaders)
    endif ()

    list(APPEND CKRE_TARGETS CK2_3D)
endif ()

if (CKRE_BUILD_STATIC)
    set(CKRE_STATIC_SOURCES ${CKRE_SOURCES})
    list(FILTER CKRE_STATIC_SOURCES EXCLUDE REGEX "\\.rc$")

    add_library(CK2_3DStatic STATIC ${CKRE_STATIC_SOURCES} ${CKRE_PUBLIC_HEADERS} ${CKRE_PRIVATE_HEADERS})
    ckre_configure_target(CK2_3DStatic)
    # CK2_3DStatic consumer code not yet rewritten for v2 API
    if (TARGET CK2_3D_shaders)
        add_dependencies(CK2_3DStatic CK2_3D_shaders)
    endif ()

    # Static library specific settings
    set_target_properties(CK2_3DStatic PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )

    list(APPEND CKRE_TARGETS CK2_3DStatic)
endif ()

# =============================================================================
# Installation
# =============================================================================
if (CKRE_INSTALL)
    install(FILES ${CKRE_PUBLIC_HEADERS}
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # Install CK2_3D to RenderEngines directory (shared) or lib directory (static)
    if (TARGET CK2_3D)
        install(TARGETS CK2_3D
                EXPORT CKRenderEngineTargets
                RUNTIME DESTINATION RenderEngines
                LIBRARY DESTINATION RenderEngines
                ARCHIVE DESTINATION lib
        )
    endif ()

    if (TARGET CK2_3DStatic)
        install(TARGETS CK2_3DStatic
                EXPORT CKRenderEngineTargets
                ARCHIVE DESTINATION lib
        )
    endif ()
endif ()

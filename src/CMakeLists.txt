configure_file(Version.h.in ${CKRE_INCLUDE_DIR}/Version.h)

set(CKRE_PUBLIC_HEADERS
        ${CKRE_INCLUDE_DIR}/Version.h
        ${CKRE_INCLUDE_DIR}/CKRenderEngineTypes.h
        ${CKRE_INCLUDE_DIR}/CKRenderEngineEnums.h
        ${CKRE_INCLUDE_DIR}/CKDebugLogger.h
        ${CKRE_INCLUDE_DIR}/CKException.h

        ${CKRE_INCLUDE_DIR}/RCKRenderManager.h
        ${CKRE_INCLUDE_DIR}/RCKRenderContext.h
        ${CKRE_INCLUDE_DIR}/RCKMaterial.h
        ${CKRE_INCLUDE_DIR}/RCKTexture.h
        ${CKRE_INCLUDE_DIR}/RCKMesh.h
        ${CKRE_INCLUDE_DIR}/RCKPatchMesh.h
        ${CKRE_INCLUDE_DIR}/RCKAnimation.h
        ${CKRE_INCLUDE_DIR}/RCKKeyedAnimation.h
        ${CKRE_INCLUDE_DIR}/RCKObjectAnimation.h
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
        )

set(CKRE_SOURCES
        CK2_3D.cpp

        CKDebugLogger.cpp
        CKException.cpp

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

        CKRenderedScene.cpp
        CKSceneGraph.cpp
        CKVertexBuffer.cpp

        CKRasterizer.cpp
        CKRasterizerDriver.cpp
        CKRasterizerContext.cpp

        CK2_3D.rc
)

add_library(CK2_3D SHARED ${CKRE_SOURCES} ${CKRE_PUBLIC_HEADERS} ${CKRE_PRIVATE_HEADERS})

target_include_directories(CK2_3D PRIVATE
        $<BUILD_INTERFACE:${CKRE_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        )

target_link_libraries(CK2_3D PUBLIC CK2 VxMath)

set_target_properties(CK2_3D PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        )

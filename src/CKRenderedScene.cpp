#include "CKRenderedScene.h"

#include "CKRenderContext.h"
#include "CKRasterizer.h"
#include "CK3dEntity.h"
#include "CKMaterial.h"
#include "CKLight.h"
#include "CKCamera.h"
#include "CKSceneGraph.h"
#include "RCKRenderManager.h"
#include "RCKRenderContext.h"
#include "RCKRasterizerContext.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKCamera.h"
#include "RCKLight.h"
#include "VxMatrix.h"
#include "CKDebugLogger.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>

#define SCENE_DEBUG_LOG(msg) CK_LOG("RenderedScene", msg)
#define SCENE_DEBUG_LOG_FMT(fmt, ...) CK_LOG_FMT("RenderedScene", fmt, __VA_ARGS__)

extern int g_UpdateTransparency;

CKRenderedScene::CKRenderedScene(CKRenderContext *rc) {
    SCENE_DEBUG_LOG_FMT("Constructor: rc=%p", rc);
    m_RenderContext = rc;
    SCENE_DEBUG_LOG_FMT("Constructor: rc->m_Context=%p", rc ? rc->m_Context : nullptr);
    m_Context = rc->m_Context;
    SCENE_DEBUG_LOG_FMT("Constructor: m_Context=%p", m_Context);
    m_AmbientLight = 0xFF0F0F0F;
    m_FogMode = 0;
    m_FogStart = 1.0f;
    m_FogEnd = 100.0f;
    m_FogColor = 0;
    m_FogDensity = 1.0f;
    m_LightCount = 0;
    m_BackgroundMaterial = nullptr;
    m_RootEntity = nullptr;
    m_AttachedCamera = nullptr;

    if (!m_Context) {
        SCENE_DEBUG_LOG("Constructor: ERROR - m_Context is NULL!");
        return;
    }

    SCENE_DEBUG_LOG("Constructor: Creating root entity");
    m_RootEntity = (CK3dEntity *)m_Context->CreateObject(CKCID_3DENTITY);
    SCENE_DEBUG_LOG_FMT("Constructor: m_RootEntity=%p", m_RootEntity);
    if (m_RootEntity) {
        m_RootEntity->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
        m_RenderContext->AddObject(m_RootEntity);
        SCENE_DEBUG_LOG("Constructor: Root entity added");
    }

    SCENE_DEBUG_LOG("Constructor: Creating background material");
    m_BackgroundMaterial = (CKMaterial *)m_Context->CreateObject(CKCID_MATERIAL, (CKSTRING)"Background Material");
    SCENE_DEBUG_LOG_FMT("Constructor: m_BackgroundMaterial=%p", m_BackgroundMaterial);
    if (m_BackgroundMaterial) {
        m_BackgroundMaterial->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
        m_BackgroundMaterial->SetDiffuse(VxColor(0.0f, 0.0f, 0.0f));
        m_BackgroundMaterial->SetAmbient(VxColor(0.0f, 0.0f, 0.0f));
        m_BackgroundMaterial->SetSpecular(VxColor(0.0f, 0.0f, 0.0f));
        m_BackgroundMaterial->SetEmissive(VxColor(0.0f, 0.0f, 0.0f));
    }
    SCENE_DEBUG_LOG("Constructor: Complete");
}

CKRenderedScene::~CKRenderedScene() {
    if (!m_Context->IsInClearAll()) {
        CKDestroyObject(m_BackgroundMaterial);
        CKDestroyObject(m_RootEntity);
    }
}

void CKRenderedScene::AddObject(CKRenderObject *obj) {
    if (!obj) return;
    
    int classId = obj->GetClassID();
    SCENE_DEBUG_LOG_FMT("AddObject: obj=%p classId=%d", obj, classId);
    
    if (CKIsChildClassOf(classId, CKCID_3DENTITY)) {
        // Add to 3D entities array
        m_3DEntities.PushBack((CK3dEntity *)obj);
        SCENE_DEBUG_LOG_FMT("AddObject: Added 3D entity, count=%d", m_3DEntities.Size());
        
        // Check if it's a camera
        if (CKIsChildClassOf(classId, CKCID_CAMERA)) {
            m_Cameras.PushBack((CKCamera *)obj);
            SCENE_DEBUG_LOG_FMT("AddObject: Added camera, count=%d", m_Cameras.Size());
        }
        // Check if it's a light
        else if (CKIsChildClassOf(classId, CKCID_LIGHT)) {
            m_Lights.PushBack((CKLight *)obj);
            SCENE_DEBUG_LOG_FMT("AddObject: Added light, count=%d", m_Lights.Size());
        }
    }
    else if (CKIsChildClassOf(classId, CKCID_2DENTITY)) {
        // Add to 2D entities array
        m_2DEntities.PushBack((CK2dEntity *)obj);
        SCENE_DEBUG_LOG_FMT("AddObject: Added 2D entity, count=%d", m_2DEntities.Size());
        
        // If no parent, set parent to 2D root
        CK2dEntity *ent2d = (CK2dEntity *)obj;
        if (!ent2d->GetParent()) {
            RCKRenderManager *renderManager = ((RCKRenderContext *)m_RenderContext)->m_RenderManager;
            if (renderManager) {
                if (ent2d->IsBackground()) {
                    ent2d->SetParent(renderManager->m_2DRootBack);
                } else {
                    ent2d->SetParent(renderManager->m_2DRootFore);
                }
            }
        }
    }
}

void CKRenderedScene::RemoveObject(CKRenderObject *obj) {
    if (!obj) return;
    
    int classId = obj->GetClassID();
    
    if (CKIsChildClassOf(classId, CKCID_3DENTITY)) {
        // Remove from arrays using RemoveAt
        for (int i = 0; i < m_3DEntities.Size(); ++i) {
            if (m_3DEntities[i] == obj) {
                m_3DEntities.RemoveAt(i);
                break;
            }
        }
        
        if (CKIsChildClassOf(classId, CKCID_CAMERA)) {
            for (int i = 0; i < m_Cameras.Size(); ++i) {
                if (m_Cameras[i] == obj) {
                    m_Cameras.RemoveAt(i);
                    break;
                }
            }
        }
        else if (CKIsChildClassOf(classId, CKCID_LIGHT)) {
            for (int i = 0; i < m_Lights.Size(); ++i) {
                if (m_Lights[i] == obj) {
                    m_Lights.RemoveAt(i);
                    break;
                }
            }
        }
    }
    else if (CKIsChildClassOf(classId, CKCID_2DENTITY)) {
        for (int i = 0; i < m_2DEntities.Size(); ++i) {
            if (m_2DEntities[i] == obj) {
                m_2DEntities.RemoveAt(i);
                break;
            }
        }
    }
}

CKERROR CKRenderedScene::Draw(CK_RENDER_FLAGS Flags) {
    SCENE_DEBUG_LOG_FMT("Draw: Flags=0x%X, rootEntity=%p, camera=%p", 
                        Flags, m_RootEntity, m_RenderContext ? ((RCKRenderContext*)m_RenderContext)->m_Camera : nullptr);
    RCKRenderContext *rc = (RCKRenderContext *)m_RenderContext;
    RCKRenderManager *rm = rc->m_RenderManager;
    CKRasterizerContext *rst = rc->m_RasterizerContext;
    RCK3dEntity *rootEntity = (RCK3dEntity *)m_RootEntity;
    
    // Set default render states
    SetDefaultRenderStates(rst);
    
    // Reset sprite time profiler
    rc->m_SpriteTimeProfiler.Reset();
    
    // Render background 2D sprites
    if ((Flags & CK_RENDER_BACKGROUNDSPRITES) != 0 &&
        rm->m_2DRootBack->GetChildrenCount() > 0) {
        VxRect viewRect;
        rc->GetViewRect(viewRect);
        
        // Set full viewport for 2D rendering
        rc->SetFullViewport(&rst->m_ViewportData, 
                           rc->m_Settings.m_Rect.right, 
                           rc->m_Settings.m_Rect.bottom);
        rst->SetViewport(&rst->m_ViewportData);
        
        // Render background 2D entities
        ((RCK2dEntity *)rm->m_2DRootBack)->Render((CKRenderContext *)rc);
        
        // Restore viewport
        ResizeViewport(viewRect);
    }
    
    // Update sprite time
    rc->m_Stats.SpriteTime += rc->m_SpriteTimeProfiler.Current();
    
    // Render 3D scene
    CKBOOL skip3D = (Flags & CK_RENDER_SKIP3D) != 0;
    SCENE_DEBUG_LOG_FMT("Draw: 3D scene - skip=%d (CK_RENDER_SKIP3D=0x%X), camera=%p", 
                        skip3D, CK_RENDER_SKIP3D, rc->m_Camera);
    if ((Flags & CK_RENDER_SKIP3D) == 0) {
        RCKCamera *camera = rc->m_Camera;
        
        // If camera is attached, set up camera transform
        if (camera) {
            // Copy camera world matrix to root entity
            VxMatrix *camMatrix = (VxMatrix *)&camera->m_WorldMatrix;
            VxMatrix *rootMatrix = (VxMatrix *)&rootEntity->m_WorldMatrix;
            memcpy(rootMatrix, camMatrix, sizeof(VxVector) * 4); // Copy 3x4 part
            rootEntity->WorldMatrixChanged(FALSE, TRUE);
            
            // Compute perspective projection based on camera bounding box
            VxVector camPos;
            VxVector rootPos;
            rootEntity->GetPosition(&rootPos, nullptr);
            camera->InverseTransform(&camPos, &rootPos, nullptr);
            
            float z = rc->m_NearPlane / fabsf(camPos.z);
            float left = (camera->m_LocalBoundingBox.Min.x - camPos.x) * z;
            float right = (camera->m_LocalBoundingBox.Max.x - camPos.x) * z;
            float top = (camera->m_LocalBoundingBox.Max.y - camPos.y) * z;
            float bottom = (camera->m_LocalBoundingBox.Min.y - camPos.y) * z;
            
            rc->m_ProjectionMatrix.PerspectiveRect(left, right, top, bottom, 
                                                   rc->m_NearPlane, rc->m_FarPlane);
            rst->SetTransformMatrix(VXMATRIX_PROJECTION, rc->m_ProjectionMatrix);
            rst->SetViewport(&rc->m_ViewportData);
            
            // Update 2D root rects
            VxRect fullRect(0.0f, 0.0f, 
                           (float)rc->m_Settings.m_Rect.right, 
                           (float)rc->m_Settings.m_Rect.bottom);
            CK2dEntity *root2DBack = rc->Get2dRoot(TRUE);
            CK2dEntity *root2DFore = rc->Get2dRoot(FALSE);
            SCENE_DEBUG_LOG_FMT("2D roots: back=%p fore=%p (rc=%p)", root2DBack, root2DFore, rc);
            if (!root2DBack || !root2DFore) {
                SCENE_DEBUG_LOG_FMT("WARN: missing 2D root (manager=%p)",
                                    rc ? rc->m_RenderManager : nullptr);
            }
            if (root2DBack) root2DBack->SetRect(fullRect);
            if (root2DFore) root2DFore->SetRect(fullRect);
        }
        
        // Build view frustum
        float aspectRatio = (float)rc->m_ViewportData.ViewWidth / (float)rc->m_ViewportData.ViewHeight;
        VxVector *origin = (VxVector *)&rootEntity->m_WorldMatrix[3];
        VxVector *right = (VxVector *)&rootEntity->m_WorldMatrix[0];
        VxVector *up = (VxVector *)&rootEntity->m_WorldMatrix[1];
        VxVector *dir = (VxVector *)&rootEntity->m_WorldMatrix[2];
        
        VxFrustum frustum(*origin, *right, *up, *dir, 
                         rc->m_NearPlane, rc->m_FarPlane, rc->m_Fov, aspectRatio);
        rc->m_Frustum = frustum;
        
        // Set world and view matrices
        rst->SetTransformMatrix(VXMATRIX_WORLD, VxMatrix::Identity());
        rst->SetTransformMatrix(VXMATRIX_VIEW, rootEntity->GetInverseWorldMatrix());
        
        // Setup lights
        SetupLights(rst);
        
        // Update projection if no camera
        if (!camera) {
            rc->UpdateProjection(FALSE);
        }
        
        // Execute pre-render callbacks
        rc->m_DevicePreCallbacksTimeProfiler.Reset();
        if (rc->m_PreRenderCallBacks.m_PreCallBacks.Size() > 0) {
            rst->SetVertexShader(0);
            for (int i = 0; i < rc->m_PreRenderCallBacks.m_PreCallBacks.Size(); i++) {
                VxCallBack &cb = rc->m_PreRenderCallBacks.m_PreCallBacks[i];
                if (cb.callback) {
                    ((CK_RENDERCALLBACK)cb.callback)((CKRenderContext *)rc, cb.argument);
                }
            }
        }
        rc->m_Stats.DevicePreCallbacks += rc->m_DevicePreCallbacksTimeProfiler.Current();
        
        rst->SetVertexShader(0);
        m_Context->ExecuteManagersOnPreRender((CKRenderContext *)rc);
        
        // Compute render flags for scene traversal
        int renderFlags = CK_RENDER_DEFAULTSETTINGS;
        if ((Flags & CK_RENDER_DONOTUPDATEEXTENTS) != 0) {
            renderFlags &= ~CK_RENDER_BACKGROUNDSPRITES;
        }
        
        // Reset transparent objects
        rc->m_ObjectsRenderTimeProfiler.Reset();
        rc->m_TransparentObjects.Resize(0);
        rc->m_Stats.SceneTraversalTime = 0.0f;
        rc->m_SceneTraversalTimeProfiler.Reset();
        
    // Render opaque objects via scene graph
    SCENE_DEBUG_LOG_FMT("Draw: Calling RenderTransparents with flags=0x%X", renderFlags);
    rm->m_CKSceneGraphRootNode.RenderTransparents(rc, renderFlags);
    SCENE_DEBUG_LOG_FMT("Draw: RenderTransparents complete, transparent count=%d", 
                        rc->m_TransparentObjects.Size());
    
    rc->m_Stats.SceneTraversalTime += rc->m_SceneTraversalTimeProfiler.Current();        // Call sprite 3D batches
        rc->CallSprite3DBatches();
        
        // Execute post-render temp callbacks
        rc->m_DevicePostCallbacksTimeProfiler.Reset();
        if (rc->m_PreRenderTempCallBacks.m_PostCallBacks.Size() > 0) {
            rst->SetVertexShader(0);
            for (int i = 0; i < rc->m_PreRenderTempCallBacks.m_PostCallBacks.Size(); i++) {
                VxCallBack &cb = rc->m_PreRenderTempCallBacks.m_PostCallBacks[i];
                if (cb.callback) {
                    ((CK_RENDERCALLBACK)cb.callback)((CKRenderContext *)rc, cb.argument);
                }
            }
        }
        rc->m_Stats.DevicePostCallbacks += rc->m_DevicePostCallbacksTimeProfiler.Current();
        
        // Sort and render transparent objects
        rc->m_SortTransparentObjects = TRUE;
        rm->m_CKSceneGraphRootNode.SortTransparentObjects(rc, renderFlags);
        rc->CallSprite3DBatches();
        rc->m_SortTransparentObjects = FALSE;
        
        rc->m_Stats.ObjectsRenderTime = rc->m_ObjectsRenderTimeProfiler.Current();
        
        // Execute post-render callbacks (pre)
        rc->m_DevicePostCallbacksTimeProfiler.Reset();
        if (rc->m_PreRenderCallBacks.m_PostCallBacks.Size() > 0) {
            rst->SetVertexShader(0);
            for (int i = 0; i < rc->m_PreRenderCallBacks.m_PostCallBacks.Size(); i++) {
                VxCallBack &cb = rc->m_PreRenderCallBacks.m_PostCallBacks[i];
                if (cb.callback) {
                    ((CK_RENDERCALLBACK)cb.callback)((CKRenderContext *)rc, cb.argument);
                }
            }
        }
        rc->m_Stats.DevicePostCallbacks += rc->m_DevicePostCallbacksTimeProfiler.Current();
        
        m_Context->ExecuteManagersOnPostRender((CKRenderContext *)rc);
    }
    
    // Reset sprite time profiler for foreground
    rc->m_SpriteTimeProfiler.Reset();
    
    // Render foreground 2D sprites
    if ((Flags & CK_RENDER_FOREGROUNDSPRITES) != 0 &&
        rm->m_2DRootFore->GetChildrenCount() > 0) {
        VxRect viewRect;
        rc->GetViewRect(viewRect);
        
        // Set full viewport for 2D rendering
        rc->SetFullViewport(&rst->m_ViewportData,
                           rc->m_Settings.m_Rect.right,
                           rc->m_Settings.m_Rect.bottom);
        rst->SetViewport(&rst->m_ViewportData);
        
        // Render foreground 2D entities
        ((RCK2dEntity *)rm->m_2DRootFore)->Render((CKRenderContext *)rc);
        
        // Restore viewport
        ResizeViewport(viewRect);
    }
    
    // Update sprite time
    rc->m_Stats.SpriteTime += rc->m_SpriteTimeProfiler.Current();
    
    rst->SetVertexShader(0);
    m_Context->ExecuteManagersOnPostSpriteRender((CKRenderContext *)rc);
    
    // Execute final post-render callbacks
    rc->m_DevicePostCallbacksTimeProfiler.Reset();
    if (rc->m_PostRenderCallBacks.m_PostCallBacks.Size() > 0) {
        rst->SetVertexShader(0);
        for (int i = 0; i < rc->m_PostRenderCallBacks.m_PostCallBacks.Size(); i++) {
            VxCallBack &cb = rc->m_PostRenderCallBacks.m_PostCallBacks[i];
            if (cb.callback) {
                ((CK_RENDERCALLBACK)cb.callback)((CKRenderContext *)rc, cb.argument);
            }
        }
    }
    rc->m_Stats.DevicePostCallbacks += rc->m_DevicePostCallbacksTimeProfiler.Current();
    
    // Adjust render times
    rc->m_Stats.ObjectsRenderTime -= (rc->m_Stats.SceneTraversalTime +
                                      rc->m_Stats.TransparentObjectsSortTime +
                                      rc->m_Stats.ObjectsCallbacksTime +
                                      rc->m_Stats.SkinTime);
    rc->m_Stats.SpriteTime -= rc->m_Stats.SpriteCallbacksTime;
    
    g_UpdateTransparency = 0;
    
    return CK_OK;
}

void CKRenderedScene::SetupLights(CKRasterizerContext *rst) {
    // Disable all previously enabled lights
    for (CKDWORD i = 0; i < m_LightCount; ++i) {
        rst->EnableLight(i, FALSE);
    }
    m_LightCount = 0;
    
    // Setup each light in the scene
    for (int i = 0; i < m_Lights.Size(); ++i) {
        RCKLight *light = (RCKLight *)m_Lights[i];
        if (light && light->Setup(rst, m_LightCount)) {
            ++m_LightCount;
        }
    }
    
    // Set ambient light
    rst->SetRenderState(VXRENDERSTATE_AMBIENT, m_AmbientLight);
}

void CKRenderedScene::ResizeViewport(const VxRect &rect) {
    CKRasterizerContext *rst = ((RCKRenderContext *)m_RenderContext)->m_RasterizerContext;
    rst->m_ViewportData.ViewX = (int)rect.left;
    rst->m_ViewportData.ViewY = (int)rect.top;
    rst->m_ViewportData.ViewWidth = (int)rect.GetWidth();
    rst->m_ViewportData.ViewHeight = (int)rect.GetHeight();
    rst->SetViewport(&rst->m_ViewportData);
}

void CKRenderedScene::SetDefaultRenderStates(CKRasterizerContext *rst_base) {
    RCKRasterizerContext *rst = (RCKRasterizerContext*)rst_base;
    RCKRenderContext *rc = (RCKRenderContext*)m_RenderContext;
    RCKRenderManager *rm = rc->m_RenderManager;

    rst->ClearRenderStateDefaultValue(VXRENDERSTATE_FOGVERTEXMODE);
    rst->ClearRenderStateDefaultValue(VXRENDERSTATE_FOGENABLE);
    rst->ClearRenderStateDefaultValue(VXRENDERSTATE_FOGPIXELMODE);
    rst->SetRenderState(VXRENDERSTATE_FOGENABLE, m_FogMode != VXFOG_NONE);

    if (m_FogMode) {
        if (rm->m_ForceLinearFog.Value != 0)
            m_FogMode = VXFOG_LINEAR;

        if ((rc->m_RasterizerDriver->m_3DCaps.RasterCaps &
            (CKRST_RASTERCAPS_FOGRANGE | CKRST_RASTERCAPS_FOGPIXEL)) ==
            (CKRST_RASTERCAPS_FOGRANGE | CKRST_RASTERCAPS_FOGPIXEL) ) {
            rst->SetRenderState(VXRENDERSTATE_FOGPIXELMODE, m_FogMode);
        } else {
            m_FogMode = VXFOG_LINEAR;
            rst->SetRenderState(VXRENDERSTATE_FOGVERTEXMODE, m_FogMode);
        }

        // Fog Projection - project fog values through the projection matrix
        // This is needed for correct fog in projected space
        const VxMatrix &projMat = rst->m_ProjectionMatrix;
        
        // Calculate projected fog end values
        float endZ = projMat[2][2] * m_FogEnd + projMat[3][2];
        float endW = projMat[2][3] * m_FogEnd + projMat[3][3];
        
        // Calculate projected fog start values
        float startZ = projMat[2][2] * m_FogStart + projMat[3][2];
        float startW = projMat[2][3] * m_FogStart + projMat[3][3];
        
        // Normalize by w
        float projFogEnd = endZ / endW;
        float projFogStart = startZ / startW;
        float recipEndW = 1.0f / endW;
        float recipStartW = 1.0f / startW;
        
        // Use projection mode based on global config
        // Mode 0: Direct fog values
        // Mode 1: Projected fog values (z/w based)
        // Mode 2: Projected fog values (1/w based)
        // Currently using mode 0 (direct) - most compatible
        rst->SetRenderState(VXRENDERSTATE_FOGEND, *(CKDWORD*)&m_FogEnd);
        rst->SetRenderState(VXRENDERSTATE_FOGSTART, *(CKDWORD*)&m_FogStart);
        rst->SetRenderState(VXRENDERSTATE_FOGDENSITY, *(CKDWORD*)&m_FogDensity);
        rst->SetRenderState(VXRENDERSTATE_FOGCOLOR, (CKDWORD)m_FogColor);
    }

    if (rm->m_DisableSpecular.Value != 0) {
        rst->SetRenderStateFlag(VXRENDERSTATE_SPECULARENABLE, FALSE);
        rst->SetRenderState(VXRENDERSTATE_SPECULARENABLE, FALSE);
        rst->SetRenderStateFlag(VXRENDERSTATE_SPECULARENABLE, TRUE);
    } else {
        rst->SetRenderStateFlag(VXRENDERSTATE_SPECULARENABLE, FALSE);
        rst->SetRenderState(VXRENDERSTATE_SPECULARENABLE, TRUE);
    }

    if (rm->m_DisableDithering.Value != 0) {
        rst->SetRenderStateFlag(VXRENDERSTATE_DITHERENABLE, FALSE);
        rst->SetRenderState(VXRENDERSTATE_DITHERENABLE, FALSE);
        rst->SetRenderStateFlag(VXRENDERSTATE_DITHERENABLE, TRUE);
    } else {
        rst->SetRenderStateFlag(VXRENDERSTATE_DITHERENABLE, FALSE);
        rst->ClearRenderStateDefaultValue(VXRENDERSTATE_DITHERENABLE);
        rst->SetRenderState(VXRENDERSTATE_DITHERENABLE, TRUE);
    }

    if (rm->m_DisablePerspectiveCorrection.Value != 0) {
        rst->SetRenderStateFlag(VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE);
        rst->SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE);
        rst->SetRenderStateFlag(VXRENDERSTATE_TEXTUREPERSPECTIVE, TRUE);
    } else {
        rst->SetRenderStateFlag(VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE);
        rst->SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, TRUE);
    }

    rst->m_PresentInterval = rm->m_DisableFilter.Value;
    rst->m_CurrentPresentInterval = rm->m_DisableMipmap.Value;
    rst->SetRenderState(VXRENDERSTATE_NORMALIZENORMALS, TRUE);
    rst->SetRenderState(VXRENDERSTATE_ZENABLE, TRUE);
    rst->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_CCW);
    rst->SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_LESSEQUAL);

    if (rc->m_Shading == 0) {
        rst->SetRenderStateFlag(VXRENDERSTATE_SHADEMODE, FALSE);
        rst->SetRenderStateFlag(VXRENDERSTATE_FILLMODE, FALSE);
        rst->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_WIREFRAME);
        rst->SetRenderStateFlag(VXRENDERSTATE_FILLMODE, TRUE);
    } else if (rc->m_Shading == 1) {
        rst->SetRenderStateFlag(VXRENDERSTATE_FILLMODE, FALSE);
        rst->SetRenderStateFlag(VXRENDERSTATE_SHADEMODE, FALSE);
        rst->SetRenderState(VXRENDERSTATE_SHADEMODE, rc->m_Shading);
        rst->SetRenderStateFlag(VXRENDERSTATE_SHADEMODE, TRUE);
    } else if (rc->m_Shading == 2) {
        rst->SetRenderStateFlag(VXRENDERSTATE_SHADEMODE, FALSE);
        rst->SetRenderStateFlag(VXRENDERSTATE_FILLMODE, FALSE);
    }
}

void CKRenderedScene::PrepareCameras(CK_RENDER_FLAGS Flags) {
    RCKRenderContext *rc = (RCKRenderContext *)m_RenderContext;
    
    if (!rc || !rc->m_RasterizerContext)
        return;
    
    // Lazily create a fallback camera so we always have a viewpoint
    CKCamera *camera = m_AttachedCamera;
    if (!camera) {
        SCENE_DEBUG_LOG("PrepareCameras: creating fallback camera");
        camera = (CKCamera *)m_Context->CreateObject(CKCID_CAMERA, (CKSTRING)"Fallback Camera");
        if (camera) {
            camera->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
            camera->SetProjectionType(CK_PERSPECTIVEPROJECTION);
            camera->SetFov(PI / 4.0f);          // 45 deg default
            camera->SetFrontPlane(1.0f);
            camera->SetBackPlane(10000.0f);
            // Position the camera a bit back looking at origin
            VxMatrix mat = VxMatrix::Identity();
            mat[3].z = -500.0f;
            camera->SetWorldMatrix(mat);
            // Attach to render context so rc->m_Camera is non-null
            rc->AttachViewpointToCamera(camera);
            // Add camera to render context so it's part of the scene graph
            rc->AddObject(camera);
            m_AttachedCamera = camera;
        } else {
            SCENE_DEBUG_LOG("PrepareCameras: failed to create fallback camera");
            return;
        }
    }
    
    // Set up render context parameters from camera
    RCKCamera *cam = (RCKCamera *)camera;
    
    // Get camera properties
    float fov = cam->GetFov();
    float nearPlane = cam->GetFrontPlane();
    float farPlane = cam->GetBackPlane();
    int projType = cam->GetProjectionType();
    
    // Update render context with camera settings
    rc->m_Fov = fov;
    rc->m_NearPlane = nearPlane;
    rc->m_FarPlane = farPlane;
    rc->m_PerspectiveOrOrthographic = (projType == CK_PERSPECTIVEPROJECTION);
    
    if (!rc->m_PerspectiveOrOrthographic) {
        rc->m_Zoom = cam->GetOrthographicZoom();
    }
    
    // Get the inverse of the camera's world matrix for the view matrix
    const VxMatrix &worldMat = cam->GetWorldMatrix();
    VxMatrix viewMat;
    Vx3DInverseMatrix(viewMat, worldMat);
    
    // Set the view matrix on the rasterizer
    rc->m_RasterizerContext->SetTransformMatrix(VXMATRIX_VIEW, viewMat);
    
    // Mark projection as needing update 
    rc->m_ProjectionUpdated = FALSE;
}
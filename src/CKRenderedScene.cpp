#include "CKRenderedScene.h"

#include "CKDebugLogger.h"

#include "VxMatrix.h"
#include "CKRenderContext.h"
#include "CKRasterizer.h"
#include "CK3dEntity.h"
#include "CKMaterial.h"
#include "CKLight.h"
#include "CKCamera.h"
#include "CKScene.h"
#include "CKSceneGraph.h"
#include "RCKRenderManager.h"
#include "RCKRenderContext.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKCamera.h"
#include "RCKLight.h"

#include <cstdlib>

extern int g_UpdateTransparency;
extern int g_FogProjectionMode;

static bool EnvEnabled(const char *name) {
    const char *value = std::getenv(name);
    return value && value[0] != '\0' && value[0] != '0';
}

static bool RenderedSceneFrameLogEnabled() {
    static const bool enabled = EnvEnabled("CK2_3D_DEBUG_FRAME_LOG");
    return enabled;
}

static bool RenderedSceneCameraLogEnabled() {
    static const bool enabled = EnvEnabled("CK2_3D_DEBUG_LOG_RENDERED_SCENE_CAMERA");
    return enabled;
}

CKRenderedScene::CKRenderedScene(CKRenderContext *rc) {
    // IDA: 0x1006f830
    m_RenderContext = rc;
    m_Context = rc->m_Context;
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

    m_RootEntity = (CK3dEntity *) m_Context->CreateObject(CKCID_3DENTITY, nullptr, CK_OBJECTCREATION_NONAMECHECK);
    if (m_RootEntity) {
        m_RootEntity->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
        AddObject(m_RootEntity);
    }

    m_BackgroundMaterial = (CKMaterial *) m_Context->CreateObject(CKCID_MATERIAL, "Background Material", CK_OBJECTCREATION_NONAMECHECK);
    if (m_BackgroundMaterial) {
        m_BackgroundMaterial->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
        m_BackgroundMaterial->SetDiffuse(VxColor(0.0f, 0.0f, 0.0f));
        m_BackgroundMaterial->SetAmbient(VxColor(0.0f, 0.0f, 0.0f));
        m_BackgroundMaterial->SetSpecular(VxColor(0.0f, 0.0f, 0.0f));
        m_BackgroundMaterial->SetEmissive(VxColor(0.0f, 0.0f, 0.0f));
    }
}

CKRenderedScene::~CKRenderedScene() {
    if (!m_Context->IsInClearAll()) {
        CKDestroyObject(m_BackgroundMaterial);
        CKDestroyObject(m_RootEntity);
    }
}

void CKRenderedScene::AddObject(CKRenderObject *obj) {
    // IDA: 0x1006fae5
    int classId = obj->GetClassID();

    if (CKIsChildClassOf(classId, CKCID_3DENTITY)) {
        m_3DEntities.PushBack((CK3dEntity *) obj);

        if (CKIsChildClassOf(obj->GetClassID(), CKCID_CAMERA)) {
            m_Cameras.PushBack((CKCamera *) obj);
        } else if (CKIsChildClassOf(obj->GetClassID(), CKCID_LIGHT)) {
            m_Lights.PushBack((CKLight *) obj);
        }
    } else if (CKIsChildClassOf(obj->GetClassID(), CKCID_2DENTITY)) {
        m_2DEntities.PushBack((CK2dEntity *) obj);

        CK2dEntity *ent2d = (CK2dEntity *) obj;
        if (!ent2d->GetParent()) {
            RCKRenderManager *renderManager = ((RCKRenderContext *) m_RenderContext)->m_RenderManager;
            if (ent2d->IsBackground()) {
                ent2d->SetParent(renderManager->m_2DRootBack);
            } else {
                ent2d->SetParent(renderManager->m_2DRootFore);
            }
        }
    }
}

void CKRenderedScene::RemoveObject(CKRenderObject *obj) {
    // IDA: 0x1006fc21
    int classId = obj->GetClassID();

    if (CKIsChildClassOf(classId, CKCID_3DENTITY)) {
        m_3DEntities.Remove((CK3dEntity *) obj);

        if (CKIsChildClassOf(obj->GetClassID(), CKCID_CAMERA)) {
            m_Cameras.Remove((CKCamera *) obj);
            if (m_AttachedCamera == (CKCamera *) obj) {
                m_AttachedCamera = nullptr;
            }
        }
        if (CKIsChildClassOf(obj->GetClassID(), CKCID_LIGHT)) {
            m_Lights.Remove((CKLight *) obj);
        }
    } else if (CKIsChildClassOf(obj->GetClassID(), CKCID_2DENTITY)) {
        m_2DEntities.Remove((CK2dEntity *) obj);

        CK2dEntity *ent2d = (CK2dEntity *) obj;
        if (!ent2d->GetParent()) {
            CK2dEntity *root2D = m_RenderContext->Get2dRoot(ent2d->IsBackground());
            ((RCK2dEntity *) root2D)->m_Children.Remove(ent2d);
        }
    }
}

void CKRenderedScene::DetachAll() {
    // IDA: 0x1006fd4f
    RCKRenderContext *rc = (RCKRenderContext *) m_RenderContext;
    RCKRenderManager *rm = rc->m_RenderManager;

    // Clear 2D root children if in ClearAll or if render context not found in list
    if (m_Context->IsInClearAll() || !rm->m_RenderContexts.IsHere(rc->m_ID)) {
        if (rm->m_2DRootBack) {
            ((RCK2dEntity *) rm->m_2DRootBack)->m_Children.Clear();
        }
        if (rm->m_2DRootFore) {
            ((RCK2dEntity *) rm->m_2DRootFore)->m_Children.Clear();
        }
    }

    // Remove from render context if not in ClearAll
    if (!m_Context->IsInClearAll()) {
        for (CK2dEntity **it = m_2DEntities.Begin(); it != m_2DEntities.End(); ++it) {
            RCK2dEntity *entity = (RCK2dEntity *) *it;
            if (entity) {
                entity->RemoveFromRenderContext(rc);
            }
        }

        for (CKObject **it = m_3DEntities.Begin(); it != m_3DEntities.End(); ++it) {
            RCK3dEntity *entity = (RCK3dEntity *) *it;
            if (entity) {
                entity->RemoveFromRenderContext(rc);
            }
        }
    }

    m_2DEntities.Clear();
    m_3DEntities.Clear();
    m_Cameras.Clear();
    m_Lights.Clear();
    m_AttachedCamera = nullptr;
}

CKERROR CKRenderedScene::Draw(CK_RENDER_FLAGS Flags) {
    // IDA: 0x100704ae
    const bool frameLog = RenderedSceneFrameLogEnabled();
    if (frameLog)
        CK_LOG("RenderedScene", "Draw enter");
    RCKRenderContext *rc = (RCKRenderContext *) m_RenderContext;
    RCKRenderManager *rm = rc->m_RenderManager;
    RCK3dEntity *rootEntity = (RCK3dEntity *) m_RootEntity;
    if (!rootEntity) {
        CK_LOG("RenderedScene", "Draw - no rootEntity!");
        return CKERR_INVALIDRENDERCONTEXT;
    }

    // --- Phase 1: begin the frame via the FF pipeline ---
    // Build a clear color from the background material diffuse.
    CKDWORD clearColor = 0xFF000000;
    if (m_BackgroundMaterial) {
        VxColor diff = m_BackgroundMaterial->GetDiffuse();
        CKDWORD r = (CKDWORD)(diff.r * 255.0f) & 0xFF;
        CKDWORD g = (CKDWORD)(diff.g * 255.0f) & 0xFF;
        CKDWORD b = (CKDWORD)(diff.b * 255.0f) & 0xFF;
        clearColor = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
    CKDWORD clearFlags = 0;
    if (Flags & CK_RENDER_CLEARBACK)
        clearFlags |= CKRST_CTXCLEAR_COLOR;
    if (Flags & CK_RENDER_CLEARZ)
        clearFlags |= CKRST_CTXCLEAR_DEPTH;
    if (Flags & CK_RENDER_CLEARSTENCIL)
        clearFlags |= CKRST_CTXCLEAR_STENCIL;

    // Build a viewport CKRECT from current settings.
    CKRECT viewport;
    viewport.left   = 0;
    viewport.top    = 0;
    viewport.right  = rc->m_Settings.m_Rect.right;
    viewport.bottom = rc->m_Settings.m_Rect.bottom;

    RCKCamera *camera = nullptr;
    if (!(Flags & CK_RENDER_SKIP3D)) {
        camera = rc->m_Camera;
        if (!camera)
            camera = (RCKCamera *)m_AttachedCamera;

        if (camera) {
            rootEntity->SetWorldMatrix(camera->GetWorldMatrix(), FALSE);
        } else {
            rc->UpdateProjection(FALSE);
        }

        // Build view frustum
        float aspectRatio = (float) rc->m_ViewportData.ViewWidth / (float) rc->m_ViewportData.ViewHeight;
        VxVector *origin = (VxVector *) &rootEntity->m_WorldMatrix[3];
        VxVector *vright = (VxVector *) &rootEntity->m_WorldMatrix[0];
        VxVector *up = (VxVector *) &rootEntity->m_WorldMatrix[1];
        VxVector *dir = (VxVector *) &rootEntity->m_WorldMatrix[2];

        VxFrustum frustum(*origin, *vright, *up, *dir,
                          rc->m_NearPlane, rc->m_FarPlane, rc->m_Fov, aspectRatio);
        rc->m_Frustum = frustum;

        rc->m_FFPipeline.SetTransform(VXMATRIX_WORLD, VxMatrix::Identity());
        rc->SetViewTransformationMatrix(rootEntity->GetInverseWorldMatrix());

        static int s_camLogCount = 0;
        static int s_attachedCamLogCount = 0;
        const bool cameraLog = RenderedSceneCameraLogEnabled();
        if (cameraLog && s_camLogCount < 60 && (s_camLogCount < 5 || s_camLogCount % 20 == 0)) {
            const VxMatrix &invW = rootEntity->GetInverseWorldMatrix();
            CK_LOG_FMT("RenderedScene", "Frame %d: camera=%p rootEntity worldMat row3: %.2f %.2f %.2f",
                       s_camLogCount, (void*)camera,
                       rootEntity->m_WorldMatrix[3][0], rootEntity->m_WorldMatrix[3][1], rootEntity->m_WorldMatrix[3][2]);
            CK_LOG_FMT("RenderedScene", "  InverseWorld row3: %.3f %.3f %.3f",
                       invW[3][0], invW[3][1], invW[3][2]);
        }
        if (cameraLog && camera && s_attachedCamLogCount < 12) {
            const VxMatrix &camW = camera->GetWorldMatrix();
            const VxMatrix &invW = rootEntity->GetInverseWorldMatrix();
            CK3dEntity *target = camera->GetClassID() == CKCID_TARGETCAMERA ? camera->GetTarget() : nullptr;
            int aspectWidth = 0;
            int aspectHeight = 0;
            camera->GetAspectRatio(aspectWidth, aspectHeight);
            CK_LOG_FMT("RenderedScene", "Attached camera #%d: camera=%p name=%s class=%d target=%p targetName=%s fov=%.3f near=%.3f far=%.3f aspect=%d:%d root row3=(%.3f %.3f %.3f) cam row3=(%.3f %.3f %.3f) inv row3=(%.3f %.3f %.3f)",
                       s_attachedCamLogCount, (void *)camera,
                       camera->GetName() ? camera->GetName() : "",
                       camera->GetClassID(),
                       (void *)target,
                       target && target->GetName() ? target->GetName() : "",
                       camera->GetFov(), camera->GetFrontPlane(), camera->GetBackPlane(),
                       aspectWidth, aspectHeight,
                       rootEntity->m_WorldMatrix[3][0], rootEntity->m_WorldMatrix[3][1], rootEntity->m_WorldMatrix[3][2],
                       camW[3][0], camW[3][1], camW[3][2],
                       invW[3][0], invW[3][1], invW[3][2]);
            CK_LOG_FMT("RenderedScene", "  cam row0=(%.3f %.3f %.3f %.3f) row1=(%.3f %.3f %.3f %.3f) row2=(%.3f %.3f %.3f %.3f)",
                       camW[0][0], camW[0][1], camW[0][2], camW[0][3],
                       camW[1][0], camW[1][1], camW[1][2], camW[1][3],
                       camW[2][0], camW[2][1], camW[2][2], camW[2][3]);
            s_attachedCamLogCount++;
        }
        s_camLogCount++;
    }

    // Obtain current view and projection matrices after camera setup so bgfx
    // receives the same transforms used by draw submission in this frame.
    const VxMatrix &viewMat = rc->m_FFPipeline.GetViewMatrix();
    const VxMatrix &projMat = rc->m_FFPipeline.GetProjectionMatrix();

    if (frameLog)
        CK_LOG("RenderedScene", "Draw - calling BeginFrame");
    rc->m_FFPipeline.BeginDebugFrame();
    rc->m_FFPipeline.GetRenderPipeline().BeginFrame(viewport, clearFlags, clearColor, 1.0f, viewMat, projMat);

    // --- Default render states via the FF pipeline ---
    SetDefaultRenderStates(nullptr);

    rc->m_SpriteTimeProfiler.Reset();

    // Render background 2D sprites
    if ((Flags & CK_RENDER_BACKGROUNDSPRITES) != 0 &&
        rm->m_2DRootBack && rm->m_2DRootBack->GetChildrenCount() > 0) {
        VxRect viewRect;
        rc->GetViewRect(viewRect);

        rc->m_Current2DView = CKRP_VIEW_BACKGROUND2D;
        ((RCK2dEntity *) rm->m_2DRootBack)->Render((CKRenderContext *) rc);
        rc->m_Current2DView = CKRP_VIEW_FOREGROUND2D;

        ResizeViewport(viewRect);
    }

    rc->m_Stats.SpriteTime += rc->m_SpriteTimeProfiler.Current();

    // Render 3D scene
    if (!(Flags & CK_RENDER_SKIP3D)) {
        SetupLights(nullptr);

        // Execute pre-render callbacks (m_PreCallBacks)
        rc->m_DevicePreCallbacksTimeProfiler.Reset();
        if (rc->m_PreRenderCallBacks.m_PreCallBacks.Size() > 0) {
            VxCallBack *it = rc->m_PreRenderCallBacks.m_PreCallBacks.Begin();
            while (it < rc->m_PreRenderCallBacks.m_PreCallBacks.End()) {
                ((CK_RENDERCALLBACK) it->callback)((CKRenderContext *) rc, it->argument);
                ++it;
            }
        }
        rc->m_Stats.DevicePreCallbacks += rc->m_DevicePreCallbacksTimeProfiler.Current();

        m_Context->ExecuteManagersOnPreRender((CKRenderContext *) rc);

        // Compute render flags for scene traversal (matches original CK2_3D.dll behavior)
        // a3 = CK_RENDER_DEFAULTSETTINGS + ((Flags & CK_RENDER_DONOTUPDATEEXTENTS) ? 1 : 0)
        CKDWORD renderFlags = (Flags & CK_RENDER_DONOTUPDATEEXTENTS)
                                  ? CK_RENDER_CLEARVIEWPORT
                                  : CK_RENDER_DEFAULTSETTINGS;

        rc->m_ObjectsRenderTimeProfiler.Reset();
        rc->m_TransparentObjects.Resize(0);
        rc->m_Stats.SceneTraversalTime = 0.0f;
        rc->m_SceneTraversalTimeProfiler.Reset();

        rc->m_Current3DView = CKRP_VIEW_OPAQUE3D;
        rm->m_SceneGraphRootNode.RenderTransparentObjects(rc, renderFlags);

        rc->m_Stats.SceneTraversalTime += rc->m_SceneTraversalTimeProfiler.Current();

        rc->CallSprite3DBatches();

        // Execute post-render temp callbacks (m_PostRenderCallBacks.m_PostCallBacks)
        rc->m_DevicePostCallbacksTimeProfiler.Reset();
        if (rc->m_PostRenderCallBacks.m_PostCallBacks.Size() > 0) {
            VxCallBack *it = rc->m_PostRenderCallBacks.m_PostCallBacks.Begin();
            while (it < rc->m_PostRenderCallBacks.m_PostCallBacks.End()) {
                ((CK_RENDERCALLBACK) it->callback)((CKRenderContext *) rc, it->argument);
                ++it;
            }
        }
        rc->m_Stats.DevicePostCallbacks += rc->m_DevicePostCallbacksTimeProfiler.Current();

        // Sort and render transparent objects
        rc->m_SortTransparentObjects = TRUE;
        rc->m_Current3DView = CKRP_VIEW_TRANSPARENT;
        rm->m_SceneGraphRootNode.SortTransparentObjects(rc, renderFlags);
        rc->CallSprite3DBatches();
        rc->m_SortTransparentObjects = FALSE;
        rc->m_Current3DView = CKRP_VIEW_OPAQUE3D;

        rc->m_Stats.ObjectsRenderTime = rc->m_ObjectsRenderTimeProfiler.Current();

        // Execute post-render callbacks (m_PreRenderCallBacks.m_PostCallBacks)
        rc->m_DevicePostCallbacksTimeProfiler.Reset();
        if (rc->m_PreRenderCallBacks.m_PostCallBacks.Size() > 0) {
            VxCallBack *it = rc->m_PreRenderCallBacks.m_PostCallBacks.Begin();
            while (it < rc->m_PreRenderCallBacks.m_PostCallBacks.End()) {
                ((CK_RENDERCALLBACK) it->callback)((CKRenderContext *) rc, it->argument);
                ++it;
            }
        }
        rc->m_Stats.DevicePostCallbacks += rc->m_DevicePostCallbacksTimeProfiler.Current();

        m_Context->ExecuteManagersOnPostRender(rc);
    }

    rc->m_SpriteTimeProfiler.Reset();

    // Render foreground 2D sprites
    if ((Flags & CK_RENDER_FOREGROUNDSPRITES) != 0 &&
        rm->m_2DRootFore && rm->m_2DRootFore->GetChildrenCount() > 0) {
        VxRect viewRect;
        rc->GetViewRect(viewRect);

        ((RCK2dEntity *) rm->m_2DRootFore)->Render(rc);

        ResizeViewport(viewRect);
    }

    rc->m_Stats.SpriteTime += rc->m_SpriteTimeProfiler.Current();

    m_Context->ExecuteManagersOnPostSpriteRender(rc);

    // Execute final post-render callbacks (m_PostSpriteRenderCallBacks.m_PostCallBacks)
    rc->m_DevicePostCallbacksTimeProfiler.Reset();
    if (rc->m_PostSpriteRenderCallBacks.m_PostCallBacks.Size() > 0) {
        VxCallBack *it = rc->m_PostSpriteRenderCallBacks.m_PostCallBacks.Begin();
        while (it < rc->m_PostSpriteRenderCallBacks.m_PostCallBacks.End()) {
            ((CK_RENDERCALLBACK) it->callback)((CKRenderContext *) rc, it->argument);
            ++it;
        }
    }
    rc->m_Stats.DevicePostCallbacks += rc->m_DevicePostCallbacksTimeProfiler.Current();

    rc->m_Stats.ObjectsRenderTime -= (rc->m_Stats.SceneTraversalTime +
        rc->m_Stats.TransparentObjectsSortTime +
        rc->m_Stats.ObjectsCallbacksTime +
        rc->m_Stats.SkinTime);
    rc->m_Stats.SpriteTime -= rc->m_Stats.SpriteCallbacksTime;

    g_UpdateTransparency = FALSE;

    return CK_OK;
}

void CKRenderedScene::SetupLights(CKRasterizerContext * /*rst*/) {
    // IDA: 0x1006fea0
    // Phase 1: disable all lights that were active during the previous frame.
    RCKRenderContext *rc = (RCKRenderContext *) m_RenderContext;

    for (CKDWORD i = 0; i < m_LightCount; ++i) {
        rc->m_FFPipeline.EnableLight(i, FALSE);
    }
    m_LightCount = 0;

    // Phase 2: iterate the scene light list and submit each active light to
    // the FF pipeline.  RCKLight::Setup populates world-space position/direction
    // from the entity's world matrix, applies power scaling to the diffuse color,
    // and calls SetLight/EnableLight.  The pipeline transforms position and
    // direction to view space internally during uniform upload.
    for (CKObject **it = m_Lights.Begin(); it != m_Lights.End(); ++it) {
        RCKLight *light = static_cast<RCKLight *>(*it);
        if (!light)
            continue;
        if (m_LightCount >= CKFF_MAX_LIGHTS)
            break;
        if (light->Setup(&rc->m_FFPipeline, static_cast<int>(m_LightCount))) {
            ++m_LightCount;
        }
    }

    rc->m_FFPipeline.SetRenderState(VXRENDERSTATE_AMBIENT, m_AmbientLight);
}

void CKRenderedScene::ResizeViewport(const VxRect &rect) {
    // Update the viewport data stored on the render context.
    // The v2 API configures the viewport through BeginFrame/SetViewRect on the
    // render pipeline - this local copy is kept for picks, frustum building,
    // and any code that reads rc->m_ViewportData directly.
    RCKRenderContext *rc = (RCKRenderContext *) m_RenderContext;
    rc->m_ViewportData.ViewX = (int) rect.left;
    rc->m_ViewportData.ViewY = (int) rect.top;
    rc->m_ViewportData.ViewWidth  = (int) rect.GetWidth();
    rc->m_ViewportData.ViewHeight = (int) rect.GetHeight();
}

void CKRenderedScene::SetDefaultRenderStates(CKRasterizerContext * /*rst*/) {
    // Route all render state changes through the FF pipeline.
    RCKRenderContext *rc = (RCKRenderContext *) m_RenderContext;
    RCKRenderManager *rm = rc->m_RenderManager;
    CKFixedFunctionPipeline &ffp = rc->m_FFPipeline;

    CKDWORD fogMode = m_FogMode;
    if (fogMode != VXFOG_NONE && rm->m_ForceLinearFog.Value != 0) {
        fogMode = VXFOG_LINEAR;
    }

    ffp.SetRenderState(VXRENDERSTATE_FOGENABLE, fogMode != VXFOG_NONE);

    if (fogMode != VXFOG_NONE) {
        if ((rc->m_RasterizerDriver->m_3DCaps.RasterCaps & (CKRST_RASTERCAPS_FOGRANGE | CKRST_RASTERCAPS_FOGPIXEL)) == (CKRST_RASTERCAPS_FOGRANGE | CKRST_RASTERCAPS_FOGPIXEL)) {
            ffp.SetRenderState(VXRENDERSTATE_FOGPIXELMODE, fogMode);
        } else {
            fogMode = VXFOG_LINEAR;
            ffp.SetRenderState(VXRENDERSTATE_FOGVERTEXMODE, fogMode);
        }

        const VxMatrix &projMat = ffp.GetProjectionMatrix();

        float endZ = projMat[2][2] * m_FogEnd + projMat[3][2];
        float endW = projMat[2][3] * m_FogEnd + projMat[3][3];

        float startZ = projMat[2][2] * m_FogStart + projMat[3][2];
        float startW = projMat[2][3] * m_FogStart + projMat[3][3];

        float projFogEnd = endZ / endW;
        float projFogStart = startZ / startW;
        float recipStartW = 1.0f / startW;

        if (g_FogProjectionMode == 0) {
            ffp.SetRenderState(VXRENDERSTATE_FOGEND,   *reinterpret_cast<CKDWORD *>(&m_FogEnd));
            ffp.SetRenderState(VXRENDERSTATE_FOGSTART, *reinterpret_cast<CKDWORD *>(&m_FogStart));
        } else if (g_FogProjectionMode == 1) {
            ffp.SetRenderState(VXRENDERSTATE_FOGEND,   *reinterpret_cast<CKDWORD *>(&projFogEnd));
            ffp.SetRenderState(VXRENDERSTATE_FOGSTART, *reinterpret_cast<CKDWORD *>(&projFogStart));
        } else if (g_FogProjectionMode == 2) {
            ffp.SetRenderState(VXRENDERSTATE_FOGEND,   *reinterpret_cast<CKDWORD *>(&projFogStart));
            ffp.SetRenderState(VXRENDERSTATE_FOGSTART, *reinterpret_cast<CKDWORD *>(&recipStartW));
        }

        ffp.SetRenderState(VXRENDERSTATE_FOGDENSITY, *reinterpret_cast<CKDWORD *>(&m_FogDensity));
        ffp.SetRenderState(VXRENDERSTATE_FOGCOLOR, m_FogColor);
    }

    if (rm->m_DisableSpecular.Value != 0) {
        ffp.SetRenderState(VXRENDERSTATE_SPECULARENABLE, FALSE);
    } else {
        ffp.SetRenderState(VXRENDERSTATE_SPECULARENABLE, TRUE);
    }

    if (rm->m_DisableDithering.Value != 0) {
        ffp.SetRenderState(VXRENDERSTATE_DITHERENABLE, FALSE);
    } else {
        ffp.SetRenderState(VXRENDERSTATE_DITHERENABLE, TRUE);
    }

    if (rm->m_DisablePerspectiveCorrection.Value != 0) {
        ffp.SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE);
    } else {
        ffp.SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, TRUE);
    }

    // m_PresentInterval / m_CurrentPresentInterval were v1 fields on
    // CKRasterizerContext; filter/mipmap modes are now managed by the
    // sampler descriptors built inside CKFixedFunctionPipeline.

    ffp.SetRenderState(VXRENDERSTATE_NORMALIZENORMALS, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_ZENABLE,   TRUE);
    ffp.SetRenderState(VXRENDERSTATE_CULLMODE,  VXCULL_CCW);
    ffp.SetRenderState(VXRENDERSTATE_ZFUNC,     VXCMP_LESSEQUAL);

    if (rc->m_Shading < 2) {
        if (rc->m_Shading) {
            ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, rc->m_Shading);
        } else {
            ffp.SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_WIREFRAME);
        }
    }
    // Shading == 2 (Gouraud solid) is the pipeline default; no override needed.
}

void CKRenderedScene::PrepareCameras(CK_RENDER_FLAGS flags) {
    RCKRenderContext *rc = (RCKRenderContext *) m_RenderContext;

    if (!m_AttachedCamera) {
        CKScene *currentScene = m_Context ? m_Context->GetCurrentScene() : nullptr;
        CKCamera *fallbackCamera = currentScene ? currentScene->GetStartingCamera() : nullptr;
        static int s_cameraListLogCount = 0;
        if (s_cameraListLogCount < 3) {
            CK_LOG_FMT("RenderedScene", "Camera list #%d: scene=%s starting=%p cameraCount=%d",
                       s_cameraListLogCount,
                       currentScene && currentScene->GetName() ? currentScene->GetName() : "",
                       (void *)fallbackCamera,
                       m_Cameras.Size());
            int cameraIndex = 0;
            for (CKObject **it = m_Cameras.Begin(); it != m_Cameras.End(); ++it, ++cameraIndex) {
                CKCamera *listedCamera = (CKCamera *) *it;
                if (!listedCamera)
                    continue;
                const VxMatrix &camW = ((RCKCamera *)listedCamera)->GetWorldMatrix();
                CK3dEntity *target = listedCamera->GetClassID() == CKCID_TARGETCAMERA ? listedCamera->GetTarget() : nullptr;
                CK_LOG_FMT("RenderedScene",
                           "  camera[%d]=%p name=%s class=%d target=%p targetName=%s pos=(%.3f %.3f %.3f) dir=(%.3f %.3f %.3f)",
                           cameraIndex,
                           (void *)listedCamera,
                           listedCamera->GetName() ? listedCamera->GetName() : "",
                           listedCamera->GetClassID(),
                           (void *)target,
                           target && target->GetName() ? target->GetName() : "",
                           camW[3][0], camW[3][1], camW[3][2],
                           camW[2][0], camW[2][1], camW[2][2]);
            }
            ++s_cameraListLogCount;
        }
        if (!fallbackCamera && EnvEnabled("CK2_3D_DEBUG_FALLBACK_FIRST_CAMERA")) {
            for (CKObject **it = m_Cameras.Begin(); it != m_Cameras.End(); ++it) {
                fallbackCamera = (CKCamera *) *it;
                if (fallbackCamera)
                    break;
            }
        }
        if (fallbackCamera) {
            fallbackCamera->ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
            m_AttachedCamera = fallbackCamera;
            static int s_fallbackCameraLogCount = 0;
            if (s_fallbackCameraLogCount < 12) {
                const VxMatrix &fallbackWorld = ((RCKCamera *)fallbackCamera)->GetWorldMatrix();
                CK3dEntity *fallbackTarget = fallbackCamera->GetClassID() == CKCID_TARGETCAMERA ? fallbackCamera->GetTarget() : nullptr;
                CK_LOG_FMT("RenderedScene",
                           "Resolved fallback camera #%d: camera=%p name=%s class=%d target=%p targetName=%s targetPos=(%.3f %.3f %.3f) scene=%s cameraCount=%d pos=(%.3f %.3f %.3f) dir=(%.3f %.3f %.3f)",
                           s_fallbackCameraLogCount,
                           (void *)fallbackCamera,
                           fallbackCamera->GetName() ? fallbackCamera->GetName() : "",
                           fallbackCamera->GetClassID(),
                           (void *)fallbackTarget,
                           fallbackTarget && fallbackTarget->GetName() ? fallbackTarget->GetName() : "",
                           fallbackTarget ? ((RCK3dEntity *)fallbackTarget)->GetWorldMatrix()[3][0] : 0.0f,
                           fallbackTarget ? ((RCK3dEntity *)fallbackTarget)->GetWorldMatrix()[3][1] : 0.0f,
                           fallbackTarget ? ((RCK3dEntity *)fallbackTarget)->GetWorldMatrix()[3][2] : 0.0f,
                           currentScene && currentScene->GetName() ? currentScene->GetName() : "",
                           m_Cameras.Size(),
                           fallbackWorld[3][0], fallbackWorld[3][1], fallbackWorld[3][2],
                           fallbackWorld[2][0], fallbackWorld[2][1], fallbackWorld[2][2]);
                ++s_fallbackCameraLogCount;
            }
        }
    }

    for (CKObject **it = m_Cameras.Begin(); it != m_Cameras.End(); ++it) {
        CKCamera *camera = (CKCamera *) *it;
        if (camera && camera->GetClassID() == CKCID_TARGETCAMERA) {
            CK3dEntity *target = camera->GetTarget();
            if (target) {
                static int s_targetCameraLogCount = 0;
                if (s_targetCameraLogCount < 24 && (s_targetCameraLogCount < 8 || s_targetCameraLogCount % 4 == 0)) {
                    const VxMatrix &camW = ((RCKCamera *)camera)->GetWorldMatrix();
                    const VxMatrix &targetW = ((RCK3dEntity *)target)->GetWorldMatrix();
                    CK_LOG_FMT("RenderedScene",
                               "Target camera update #%d: camera=%s pos=(%.3f %.3f %.3f) target=%s targetPos=(%.3f %.3f %.3f) beforeDir=(%.3f %.3f %.3f)",
                               s_targetCameraLogCount,
                               camera->GetName() ? camera->GetName() : "",
                               camW[3][0], camW[3][1], camW[3][2],
                               target->GetName() ? target->GetName() : "",
                               targetW[3][0], targetW[3][1], targetW[3][2],
                               camW[2][0], camW[2][1], camW[2][2]);
                }
                VxVector origin(0.0f, 0.0f, 0.0f);
                camera->LookAt(&origin, target, FALSE);
                if (s_targetCameraLogCount < 24 && (s_targetCameraLogCount < 8 || s_targetCameraLogCount % 4 == 0)) {
                    const VxMatrix &camW = ((RCKCamera *)camera)->GetWorldMatrix();
                    CK_LOG_FMT("RenderedScene",
                               "Target camera update #%d: afterDir=(%.3f %.3f %.3f)",
                               s_targetCameraLogCount,
                               camW[2][0], camW[2][1], camW[2][2]);
                }
                ++s_targetCameraLogCount;
            }
        }
    }

    for (CKObject **it = m_Lights.Begin(); it != m_Lights.End(); ++it) {
        CKLight *light = (CKLight *) *it;
        if (light && light->GetClassID() == CKCID_TARGETLIGHT && light->GetType() != VX_LIGHTPOINT) {
            CK3dEntity *target = light->GetTarget();
            if (target) {
                VxVector origin(0.0f, 0.0f, 0.0f);
                light->LookAt(&origin, target, FALSE);
            }
        }
    }

    if (m_AttachedCamera && m_RootEntity) {
        RCKCamera *cam = (RCKCamera *) m_AttachedCamera;
        RCK3dEntity *rootEntity = (RCK3dEntity *) m_RootEntity;

        memcpy(&rootEntity->m_WorldMatrix, &cam->m_WorldMatrix, sizeof(rootEntity->m_WorldMatrix));
        ((RCK3dEntity *) m_RootEntity)->WorldMatrixChanged(TRUE, FALSE);

        if (!cam->IsUpToDate()) {
            rc->m_Perspective = (cam->GetProjectionType() == CK_PERSPECTIVEPROJECTION);
            rc->m_Zoom = cam->GetOrthographicZoom();
            rc->m_Fov = cam->GetFov();
            rc->m_NearPlane = cam->GetFrontPlane();
            rc->m_FarPlane = cam->GetBackPlane();

            if ((flags & CK_RENDER_USECAMERARATIO) != 0) {
                UpdateViewportSize(TRUE, flags);
            } else {
                rc->UpdateProjection(TRUE);
            }

            cam->ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
        } else {
            rc->UpdateProjection(FALSE);
        }
    } else {
        rc->UpdateProjection(FALSE);
    }
}

void CKRenderedScene::UpdateViewportSize(int forceUpdate, CK_RENDER_FLAGS Flags) {
    // IDA: 0x10071381
    RCKRenderContext *rc = (RCKRenderContext *) m_RenderContext;

    VxRect windowRect;
    rc->GetWindowRect(windowRect, FALSE);

    if (Flags == 0) {
        Flags = (CK_RENDER_FLAGS) rc->GetCurrentRenderOptions();
    }

    int width = 4;
    int height = 3;
    if (m_AttachedCamera) {
        m_AttachedCamera->GetAspectRatio(width, height);
    }
    if (width <= 0)
        width = 1;
    if (height <= 0)
        height = 1;

    int right = rc->m_Settings.m_Rect.right;
    int bottom = rc->m_Settings.m_Rect.bottom;
    if (right <= 0)
        right = 1;
    if (bottom <= 0)
        bottom = 1;

    int viewX = 0;
    int viewY = 0;
    int viewWidth = right;
    int viewHeight = bottom;

    double rcAspect = static_cast<double>(right) / static_cast<double>(bottom);
    double camAspect = static_cast<double>(width) / static_cast<double>(height);

    if ((Flags & CK_RENDER_USECAMERARATIO) != 0) {
        if (rcAspect >= camAspect) {
            viewHeight = bottom;
            viewWidth = width * bottom / height;
        } else {
            viewWidth = right;
            viewHeight = height * right / width;
        }

        viewX = static_cast<int>(static_cast<double>(right - viewWidth) * 0.5);
        viewY = static_cast<int>(static_cast<double>(bottom - viewHeight) * 0.5);
    } else {
        viewX = 0;
        viewY = 0;
        viewWidth = right;
        viewHeight = bottom;
    }

    if ((Flags & CK_RENDER_USECAMERARATIO) != 0) {
        if (m_AttachedCamera) {
            if ((m_AttachedCamera->GetFlags() & CK_3DENTITY_CAMERAIGNOREASPECT) != 0) {
                if (forceUpdate) {
                    rc->UpdateProjection(TRUE);
                }
            } else if (rc->m_ViewportData.ViewHeight == viewHeight &&
                rc->m_ViewportData.ViewWidth == viewWidth &&
                rc->m_ViewportData.ViewX == viewX &&
                rc->m_ViewportData.ViewY == viewY) {
                if (forceUpdate) {
                    rc->UpdateProjection(TRUE);
                }
            } else {
                rc->m_ViewportData.ViewHeight = viewHeight;
                rc->m_ViewportData.ViewWidth = viewWidth;
                rc->m_ViewportData.ViewX = viewX;
                rc->m_ViewportData.ViewY = viewY;
                rc->UpdateProjection(TRUE);
            }
        } else if (forceUpdate) {
            rc->UpdateProjection(TRUE);
        }
    } else if (forceUpdate) {
        rc->UpdateProjection(TRUE);
    }
}

void CKRenderedScene::ForceCameraSettingsUpdate() {
    // IDA: 0x10070ffd
    if (!m_AttachedCamera)
        return;
    if (!m_RootEntity)
        return;

    RCKRenderContext *rc = (RCKRenderContext *) m_RenderContext;
    RCKCamera *cam = (RCKCamera *) m_AttachedCamera;

    // Set root entity's world matrix from camera
    m_RootEntity->SetWorldMatrix(cam->GetWorldMatrix(), FALSE);

    // Update render context settings from camera
    rc->m_Perspective = (cam->GetProjectionType() == 1);
    rc->m_Zoom = cam->GetOrthographicZoom();
    rc->m_Fov = cam->GetFov();
    rc->m_NearPlane = cam->GetFrontPlane();
    rc->m_FarPlane = cam->GetBackPlane();

    // Update viewport size
    UpdateViewportSize(FALSE, CK_RENDER_USECURRENTSETTINGS);
}

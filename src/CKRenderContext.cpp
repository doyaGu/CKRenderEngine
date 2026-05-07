#include "RCKRenderContext.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

#include "CKDebugLogger.h"

#include "VxMath.h"
#include "VxIntersect.h"
#include "CKGlobals.h"
#include "CKTimeManager.h"
#include "CKAttributeManager.h"
#include "CKParameterOut.h"
#include "CKRenderManager.h"
#include "CKMaterial.h"
#include "CKTexture.h"
#include "CK2dEntity.h"
#include "CKSprite.h"
#include "CK3dEntity.h"
#include "CKCamera.h"
#include "CKSceneGraph.h"
#include "CKRasterizer.h"
#include "CKRasterizerTypes.h"
#include "RCKRenderManager.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKMesh.h"
#include "RCKTexture.h"
#include "RCKMaterial.h"
#include "RCKSprite3D.h"
#include "CKFixedFunctionPipeline.h"

#include <cstdlib>

CK_CLASSID RCKRenderContext::m_ClassID = CKCID_RENDERCONTEXT;

static CKBOOL SameCopyPixelFormat(const VxImageDescEx &a, const VxImageDescEx &b) {
    return a.BitsPerPixel == b.BitsPerPixel &&
           a.RedMask == b.RedMask &&
           a.GreenMask == b.GreenMask &&
           a.BlueMask == b.BlueMask &&
           a.AlphaMask == b.AlphaMask;
}

static CKBYTE *ConvertCopyImage(const VxImageDescEx &src, const VxImageDescEx &videoFormat,
                         VxImageDescEx &uploadDesc) {
    if (src.Width <= 0 || src.Height <= 0 || src.BitsPerPixel <= 0 || videoFormat.BitsPerPixel <= 0)
        return nullptr;

    uploadDesc = videoFormat;
    uploadDesc.Width = src.Width;
    uploadDesc.Height = src.Height;
    uploadDesc.BytesPerLine = src.Width * uploadDesc.BitsPerPixel / 8;
    uploadDesc.TotalImageSize = uploadDesc.BytesPerLine * uploadDesc.Height;

    CKBYTE *converted = new CKBYTE[uploadDesc.TotalImageSize];
    uploadDesc.Image = converted;
    VxDoBlit(src, uploadDesc);
    return converted;
}

static bool RenderDebugLogEnabled(const char *name) {
    const char *value = std::getenv(name);
    return value && value[0] != '\0' && value[0] != '0';
}

static bool RenderFrameLogEnabled() {
    static const bool enabled = RenderDebugLogEnabled("CK2_3D_DEBUG_FRAME_LOG");
    return enabled;
}

static bool CameraAttachLogEnabled() {
    static const bool enabled = RenderDebugLogEnabled("CK2_3D_DEBUG_LOG_CAMERA_ATTACH");
    return enabled;
}

static bool PresentSyncLogEnabled() {
    static const bool enabled = RenderDebugLogEnabled("CK2_3D_DEBUG_LOG_PRESENT_SYNC");
    return enabled;
}

CK_CLASSID RCKRenderContext::GetClassID() {
    return m_ClassID;
}

void RCKRenderContext::PreDelete() {
    CKObject::PreDelete();
}

void RCKRenderContext::CheckPreDeletion() {
    // IDA: 0x1006d89b
    CKObject::CheckPreDeletion();

    CheckObjectExtents();

    m_RenderedScene->m_Lights.Check();
    m_RenderedScene->m_Cameras.Check();
    m_RenderedScene->m_3DEntities.Check();

    CKCamera *cam = m_RenderedScene->m_AttachedCamera;
    if (cam && cam->IsToBeDeleted()) {
        m_RenderedScene->m_AttachedCamera = nullptr;
    }
}

int RCKRenderContext::GetMemoryOccupation() {
    int size = CKObject::GetMemoryOccupation() + (sizeof(RCKRenderContext) - sizeof(CKObject));
    size += m_RenderedScene->m_3DEntities.GetMemoryOccupation();
    size += m_RenderedScene->m_Cameras.GetMemoryOccupation();
    size += m_RenderedScene->m_Lights.GetMemoryOccupation();
    if (m_UserDrawPrimitiveData)
        size += sizeof(UserDrawPrimitiveDataClass)
            + m_UserDrawPrimitiveData->m_MaxVertexCount * sizeof(VxDrawPrimitiveData)
            + m_UserDrawPrimitiveData->m_MaxIndexCount * sizeof(CKWORD);
    return size;
}

CKBOOL RCKRenderContext::IsObjectUsed(CKObject *obj, CK_CLASSID cid) {
    if (CKIsChildClassOf(obj, CKCID_LIGHT)) {
        if (m_RenderedScene->m_Lights.FindObject(obj)) {
            return TRUE;
        }
    } else if (CKIsChildClassOf(obj, CKCID_CAMERA)) {
        if (m_RenderedScene->m_Cameras.FindObject(obj)) {
            return TRUE;
        }
    } else if (CKIsChildClassOf(obj, CKCID_3DENTITY)) {
        if (m_RenderedScene->m_3DEntities.FindObject(obj)) {
            return TRUE;
        }
    }

    return CKObject::IsObjectUsed(obj, cid);
}

CKERROR RCKRenderContext::PrepareDependencies(CKDependenciesContext &context) {
    CKERROR err = CKObject::PrepareDependencies(context);
    if (err != CK_OK)
        return err;

    context.GetClassDependencies(CKCID_RENDERCONTEXT);
    if (context.IsInMode(CK_DEPENDENCIES_BUILD)) {
        CK2dEntity *background = Get2dRoot(TRUE);
        background->PrepareDependencies(context);
        CK2dEntity *foreground = Get2dRoot(FALSE);
        foreground->PrepareDependencies(context);
        m_RenderedScene->m_3DEntities.Prepare(context);
        m_RenderedScene->m_Cameras.Prepare(context);
        m_RenderedScene->m_Lights.Prepare(context);
        m_RenderedScene->m_BackgroundMaterial->PrepareDependencies(context);
    }

    return context.FinishPrepareDependencies(this, m_ClassID);
}

void RCKRenderContext::AddObject(CKRenderObject *obj) {
    // Based on IDA at 0x10067c2a
    if (obj && obj->IsRootObject() && !obj->IsInRenderContext(this)) {
        ((RCKRenderObject *) obj)->AddToRenderContext(this);
        m_RenderedScene->AddObject(obj);
    }
}

void RCKRenderContext::AddObjectWithHierarchy(CKRenderObject *obj) {
    // Based on IDA at 0x10067cb5
    if (obj) {
        AddObject(obj);

        if (CKIsChildClassOf(obj, CKCID_3DENTITY)) {
            CK3dEntity *ent = (CK3dEntity *) obj;
            for (int i = 0; i < ent->GetChildrenCount(); ++i) {
                CK3dEntity *child = ent->GetChild(i);
                AddObjectWithHierarchy(child);
            }
        }
    }
}

void RCKRenderContext::RemoveObject(CKRenderObject *obj) {
    // Based on IDA at 0x10067d40
    if (obj && obj->IsInRenderContext(this)) {
        ((RCKRenderObject *) obj)->RemoveFromRenderContext(this);

        // If it's a 3D entity, also clear it from object extents
        if (CKIsChildClassOf(obj, CKCID_3DENTITY)) {
            int count = m_ObjectExtents.Size();
            for (int i = 0; i < count; ++i) {
                if (m_ObjectExtents[i].m_Entity == (CK3dEntity *) obj) {
                    m_ObjectExtents[i].m_Entity = nullptr;
                    break;
                }
            }
        }

        m_RenderedScene->RemoveObject(obj);
    }
}

CKBOOL RCKRenderContext::IsObjectAttached(CKRenderObject *obj) {
    // Based on IDA at 0x10067bf7
    if (!obj)
        return FALSE;
    return obj->IsInRenderContext(this);
}

const XObjectArray &RCKRenderContext::Compute3dRootObjects() {
    // IDA: 0x10067e41
    m_RootObjects.Resize(0);

    // Iterate through scene graph root node children
    CKSceneGraphNode &rootNode = m_RenderManager->m_SceneGraphRootNode;
    for (int i = 0; i < rootNode.m_Children.Size(); ++i) {
        CKSceneGraphNode *child = rootNode.m_Children[i];
        m_RootObjects.PushBack(child->m_Entity->GetID());
    }

    return m_RootObjects;
}

const XObjectArray &RCKRenderContext::Compute2dRootObjects() {
    // IDA: 0x10067ec2
    // Get 2D root entities from both background and foreground
    CK2dEntity *bgRoot = Get2dRoot(TRUE);
    CK2dEntity *fgRoot = Get2dRoot(FALSE);

    int bgCount = bgRoot->GetChildrenCount();
    int fgCount = fgRoot->GetChildrenCount();

    m_RootObjects.Resize(bgCount + fgCount);

    // Add background children
    for (int i = 0; i < bgCount; ++i) {
        CK2dEntity *child = (CK2dEntity *) bgRoot->GetChild(i);
        m_RootObjects[i] = child ? child->GetID() : 0;
    }

    // Add foreground children
    for (int j = 0; j < fgCount; ++j) {
        CK2dEntity *child = (CK2dEntity *) fgRoot->GetChild(j);
        m_RootObjects[bgCount + j] = child ? child->GetID() : 0;
    }

    return m_RootObjects;
}

CK2dEntity *RCKRenderContext::Get2dRoot(CKBOOL background) {
    // IDA: 0x10068053
    RCKRenderManager *rm = (RCKRenderManager *) m_Context->GetRenderManager();
    return background ? rm->m_2DRootBack : rm->m_2DRootFore;
}

void RCKRenderContext::DetachAll() {
    // Based on IDA at 0x10067e01
    m_ObjectExtents.Resize(0);

    m_RenderedScene->DetachAll();
}

void RCKRenderContext::ForceCameraSettingsUpdate() {
    // IDA: 0x10069c2b
    m_RenderedScene->ForceCameraSettingsUpdate();
}

CK_RENDER_FLAGS RCKRenderContext::ResolveRenderFlags(CK_RENDER_FLAGS Flags) const {
    // Original engine treats "use current settings" as "no options in the low 16 bits".
    // This allows passing high-bit flags (e.g. CK_RENDER_PLAYERCONTEXT) without specifying options.
    return ((Flags & CK_RENDER_OPTIONSMASK) == 0)
               ? static_cast<CK_RENDER_FLAGS>(m_RenderFlags)
               : Flags;
}

static CK_RENDER_FLAGS ApplyFrameRateLimitOptions(CK_RENDER_FLAGS Flags, CKTimeManager *TimeManager) {
    if (!TimeManager)
        return Flags;

    const CKDWORD frameRateMode = TimeManager->GetLimitOptions() & CK_FRAMERATE_MASK;
    CK_RENDER_FLAGS resolved = Flags;
    if (frameRateMode & CK_FRAMERATE_SYNC) {
        resolved = static_cast<CK_RENDER_FLAGS>(resolved | CK_RENDER_WAITVBL);
    } else if (frameRateMode & (CK_FRAMERATE_FREE | CK_FRAMERATE_LIMIT)) {
        resolved = static_cast<CK_RENDER_FLAGS>(resolved & ~CK_RENDER_WAITVBL);
    }

    static int s_PresentSyncLogCount = 0;
    if (PresentSyncLogEnabled() && s_PresentSyncLogCount < 64) {
        CK_LOG_FMT("PresentSync",
                   "limitOptions=0x%X frameRateMode=0x%X inputFlags=0x%X resolvedFlags=0x%X waitVbl=%d",
                   TimeManager->GetLimitOptions(), frameRateMode, Flags, resolved,
                   (resolved & CK_RENDER_WAITVBL) ? 1 : 0);
        ++s_PresentSyncLogCount;
    }
    return resolved;
}

static void LogPresentFrameRateContract(const char *stage, CK_RENDER_FLAGS inputFlags, CK_RENDER_FLAGS resolvedFlags,
                                        CKTimeManager *timeManager) {
    if (!PresentSyncLogEnabled())
        return;

    static int s_PresentFrameLogCount = 0;
    if (s_PresentFrameLogCount >= 64)
        return;

    const CKDWORD limitOptions = timeManager ? timeManager->GetLimitOptions() : 0;
    const CKDWORD frameRateMode = limitOptions & CK_FRAMERATE_MASK;
    const float frameRateLimit = timeManager ? timeManager->GetFrameRateLimit() : 0.0f;
    CK_LOG_FMT("PresentSync",
               "%s limitOptions=0x%X frameRateMode=0x%X frameRateLimit=%.3f inputFlags=0x%X resolvedFlags=0x%X waitVbl=%d",
               stage ? stage : "?", limitOptions, frameRateMode, frameRateLimit, inputFlags, resolvedFlags,
               (resolvedFlags & CK_RENDER_WAITVBL) ? 1 : 0);
    ++s_PresentFrameLogCount;
}

void RCKRenderContext::RestoreStereoRenderState(CK3dEntity *rootEntity, const VxMatrix &originalWorldMat) {
    if (rootEntity) {
        rootEntity->SetWorldMatrix(originalWorldMat, FALSE);
    }
    m_ProjectionMatrix[2][0] = 0.0f;
    m_FFPipeline.SetTransform(VXMATRIX_PROJECTION, m_ProjectionMatrix);
}

void RCKRenderContext::ExecutePreRenderCallbacks() {
    // IDA: CKRenderedScene::Draw at 0x100704ae line 146-158
    // Executes m_PreRenderCallBacks.m_PreCallBacks
    m_PreRenderCallBacks.ExecutePreCallbacks(this, FALSE);
}

void RCKRenderContext::ExecutePostRenderCallbacks(CKBOOL beforeTransparent) {
    // IDA: CKRenderedScene::Draw
    // - BeforeTransparent=TRUE: m_PostRenderCallBacks.m_PostCallBacks (line 180-192)
    // - BeforeTransparent=FALSE: m_PreRenderCallBacks.m_PostCallBacks (line 205-217)
    if (beforeTransparent) {
        m_PostRenderCallBacks.ExecutePostCallbacks(this, FALSE);
    } else {
        m_PreRenderCallBacks.ExecutePostCallbacks(this, FALSE);
    }
}

void RCKRenderContext::ExecutePostSpriteCallbacks() {
    // IDA: CKRenderedScene::Draw at 0x100704ae line 244-256
    // Executes m_PostSpriteRenderCallBacks.m_PostCallBacks
    m_PostSpriteRenderCallBacks.ExecutePostCallbacks(this, FALSE);
}

void RCKRenderContext::LoadPVInformationTexture() {
    // IDA: sub_1006A4D4
    m_PVInformation = m_Context->GetPVInformation();
}

void RCKRenderContext::DrawPVInformationWatermark() {}

void RCKRenderContext::AppendStateOnOffLine(CKBOOL on) {
    m_StateString += on ? "On\n" : "Off\n";
}

void RCKRenderContext::AppendStateUIntLine(CKDWORD value) {
    XString tmp;
    tmp.Format("%u\n", (unsigned) value);
    m_StateString += tmp;
}

void RCKRenderContext::AppendStateEnumLine(CKDWORD value, const char *const *table, int tableSize) {
    if ((int) value >= 0 && (int) value < tableSize && table[value]) {
        m_StateString += table[value];
    } else {
        AppendStateUIntLine(value);
    }
}

void RCKRenderContext::FillStateString() {
    // IDA: 0x1006e40c
    m_StateString = "Render States\n\n";
    if (!m_RasterizerContext)
        return;

    static const char *const kFillMode[] = {"\n", "Point\n", "Wireframe\n", "Solid\n"};
    static const char *const kShadeMode[] = {"\n", "Flat\n", "Gouraud\n", "Phong\n"};
    static const char *const kCullMode[] = {"\n", "None\n", "CW\n", "CCW\n"};

    static const char *const kCmpFunc[] = {
        "\n",
        "Never\n",
        "Less\n",
        "Equal\n",
        "LessEqual\n",
        "Greater\n",
        "NotEqual\n",
        "GreaterEqual\n",
        "Always\n",
    };

    static const char *const kBlendMode[] = {
        "\n",
        "Zero\n",
        "One\n",
        "SrcColor\n",
        "InvSrcColor\n",
        "SrcAlpha\n",
        "InvSrcAlpha\n",
        "DestAlpha\n",
        "InvDestAlpha\n",
        "DestColor\n",
        "InvDestColor\n",
        "SrcAlphaSat\n",
        "BothSrcAlpha\n",
        "BothInvSrcAlpha\n",
    };

    static const char *const kFogMode[] = {"None\n", "Exp\n", "Exp2\n", "Linear\n"};
    static const char *const kStencilOp[] = {
        "\n",
        "Keep\n",
        "Zero\n",
        "Replace\n",
        "IncrSat\n",
        "DecrSat\n",
        "Invert\n",
        "Incr\n",
        "Decr\n",
    };

    m_StateString += "Clipping : ";
    AppendStateOnOffLine(GetState(VXRENDERSTATE_CLIPPING) != 0);

    m_StateString += "Lighting : ";
    AppendStateOnOffLine(GetState(VXRENDERSTATE_LIGHTING) != 0);

    m_StateString += "Antialias : ";
    AppendStateOnOffLine(GetState(VXRENDERSTATE_ANTIALIAS) != 0);

    m_StateString += "Perspective Tex : ";
    AppendStateOnOffLine(GetState(VXRENDERSTATE_TEXTUREPERSPECTIVE) != 0);

    m_StateString += "Fill Mode : ";
    AppendStateEnumLine(GetState(VXRENDERSTATE_FILLMODE), kFillMode, (int) (sizeof(kFillMode) / sizeof(kFillMode[0])));

    m_StateString += "Shade Mode : ";
    AppendStateEnumLine(GetState(VXRENDERSTATE_SHADEMODE), kShadeMode, (int) (sizeof(kShadeMode) / sizeof(kShadeMode[0])));

    m_StateString += "Cull Mode : ";
    AppendStateEnumLine(GetState(VXRENDERSTATE_CULLMODE), kCullMode, (int) (sizeof(kCullMode) / sizeof(kCullMode[0])));

    if (GetState(VXRENDERSTATE_ZENABLE) != 0) {
        m_StateString += "ZWrite : ";
        AppendStateOnOffLine(GetState(VXRENDERSTATE_ZWRITEENABLE) != 0);

        m_StateString += "Z Cmp Function : ";
        AppendStateEnumLine(GetState(VXRENDERSTATE_ZFUNC), kCmpFunc, (int) (sizeof(kCmpFunc) / sizeof(kCmpFunc[0])));

        m_StateString += "Zbias : ";
        AppendStateUIntLine(GetState(VXRENDERSTATE_ZBIAS));
    }

    if (GetState(VXRENDERSTATE_ALPHATESTENABLE) != 0) {
        m_StateString += "Alpha Cmp Func : ";
        AppendStateEnumLine(GetState(VXRENDERSTATE_ALPHAFUNC), kCmpFunc, (int) (sizeof(kCmpFunc) / sizeof(kCmpFunc[0])));

        m_StateString += "Alpha Ref Value : ";
        AppendStateUIntLine(GetState(VXRENDERSTATE_ALPHAREF));
    }

    m_StateString += "Dithering : ";
    AppendStateOnOffLine(GetState(VXRENDERSTATE_DITHERENABLE) != 0);

    if (GetState(VXRENDERSTATE_ALPHABLENDENABLE) != 0) {
        m_StateString += "Alpha Blending : ";
        AppendStateEnumLine(GetState(VXRENDERSTATE_SRCBLEND), kBlendMode, (int) (sizeof(kBlendMode) / sizeof(kBlendMode[0])));
        m_StateString += "->";
        AppendStateEnumLine(GetState(VXRENDERSTATE_DESTBLEND), kBlendMode, (int) (sizeof(kBlendMode) / sizeof(kBlendMode[0])));
    }

    if (GetState(VXRENDERSTATE_FOGENABLE) != 0) {
        m_StateString += "Fog : ";
        AppendStateEnumLine(GetState(VXRENDERSTATE_FOGPIXELMODE), kFogMode, (int) (sizeof(kFogMode) / sizeof(kFogMode[0])));
    }

    if (GetState(VXRENDERSTATE_SPECULARENABLE) != 0) {
        m_StateString += "Specular : ";
        m_StateString += (GetState(VXRENDERSTATE_LOCALVIEWER) != 0) ? "Local Viewer\n" : "Infinite\n";
    }

    m_StateString += "Edge Antialias : ";
    AppendStateOnOffLine(GetState(VXRENDERSTATE_EDGEANTIALIAS) != 0);

    if (GetState(VXRENDERSTATE_STENCILENABLE) != 0) {
        m_StateString += "Stencil Fail : ";
        AppendStateEnumLine(GetState(VXRENDERSTATE_STENCILFAIL), kStencilOp, (int) (sizeof(kStencilOp) / sizeof(kStencilOp[0])));

        m_StateString += "Stencil Zfail : ";
        AppendStateEnumLine(GetState(VXRENDERSTATE_STENCILZFAIL), kStencilOp, (int) (sizeof(kStencilOp) / sizeof(kStencilOp[0])));

        m_StateString += "Stencil Pass : ";
        AppendStateEnumLine(GetState(VXRENDERSTATE_STENCILPASS), kStencilOp, (int) (sizeof(kStencilOp) / sizeof(kStencilOp[0])));

        m_StateString += "Stencil Cmp Func : ";
        AppendStateEnumLine(GetState(VXRENDERSTATE_STENCILFUNC), kCmpFunc, (int) (sizeof(kCmpFunc) / sizeof(kCmpFunc[0])));

        m_StateString += "Stencil Ref Value : ";
        AppendStateUIntLine(GetState(VXRENDERSTATE_STENCILREF));

        m_StateString += "Stencil Mask : ";
        AppendStateUIntLine(GetState(VXRENDERSTATE_STENCILMASK));

        m_StateString += "Stencil Write Mask : ";
        AppendStateUIntLine(GetState(VXRENDERSTATE_STENCILWRITEMASK));
    }

    m_StateString += "Normal Normalize : ";
    AppendStateOnOffLine(GetState(VXRENDERSTATE_NORMALIZENORMALS) != 0);

    m_StateString += "Clip Planes Enable : ";
    AppendStateUIntLine(GetState(VXRENDERSTATE_CLIPPLANEENABLE));

    m_StateString += "Inverse Winding : ";
    AppendStateOnOffLine(GetState(VXRENDERSTATE_INVERSEWINDING) != 0);

    m_StateString += "Texture Target : ";
    AppendStateOnOffLine(GetState(VXRENDERSTATE_TEXTURETARGET) != 0);
}

CKERROR RCKRenderContext::Clear(CK_RENDER_FLAGS Flags, CKDWORD Stencil) {
    const bool frameLog = RenderFrameLogEnabled();
    if (frameLog)
        CK_LOG("Clear", "enter");
    if (!m_RasterizerContext)
        return CKERR_INVALIDRENDERCONTEXT;

    CK_RENDER_FLAGS inputFlags = ResolveRenderFlags(Flags);
    CKTimeManager *timeManager = m_Context ? m_Context->GetTimeManager() : nullptr;
    CK_RENDER_FLAGS renderFlags = ApplyFrameRateLimitOptions(inputFlags, timeManager);

    if (!(renderFlags & (CK_RENDER_CLEARBACK | CK_RENDER_CLEARZ | CK_RENDER_CLEARSTENCIL)))
        return CK_OK;

    CKDWORD clearFlags = 0;
    if (renderFlags & CK_RENDER_CLEARBACK) clearFlags |= CKRST_CTXCLEAR_COLOR;
    if (renderFlags & CK_RENDER_CLEARZ) clearFlags |= CKRST_CTXCLEAR_DEPTH;
    if (renderFlags & CK_RENDER_CLEARSTENCIL) {
        clearFlags |= CKRST_CTXCLEAR_STENCIL;
        m_StencilFreeMask = Stencil;
    }

    CKDWORD clearColor = 0;
    if (frameLog)
        CK_LOG("Clear", "getting background material");
    if (!m_RenderedScene) {
        CK_LOG("Clear", "m_RenderedScene is NULL!");
        return CKERR_INVALIDRENDERCONTEXT;
    }
    CKMaterial *backgroundMaterial = m_RenderedScene->GetBackgroundMaterial();
    if (backgroundMaterial && (renderFlags & CK_RENDER_CLEARBACK)) {
        clearColor = RGBAFTOCOLOR(&backgroundMaterial->GetDiffuse());
    }

    // Configure the clear view in the render pipeline
    CKRECT viewRect;
    viewRect.left = m_ViewportData.ViewX;
    viewRect.top = m_ViewportData.ViewY;
    viewRect.right = m_ViewportData.ViewX + m_ViewportData.ViewWidth;
    viewRect.bottom = m_ViewportData.ViewY + m_ViewportData.ViewHeight;
    if (frameLog)
        CK_LOG("Clear", "SetViewRect/SetViewClear");
    m_RasterizerContext->SetViewRect(CKRP_VIEW_CLEAR, viewRect);
    m_RasterizerContext->SetViewClear(CKRP_VIEW_CLEAR, clearFlags, clearColor, 1.0f, Stencil);

    if (frameLog)
        CK_LOG("Clear", "done");
    return CK_OK;
}

CKERROR RCKRenderContext::DrawScene(CK_RENDER_FLAGS Flags) {
    const bool frameLog = RenderFrameLogEnabled();
    if (frameLog)
        CK_LOG("DrawScene", "enter");
    if (!m_RasterizerContext)
        return CKERR_INVALIDRENDERCONTEXT;

    CK_RENDER_FLAGS inputFlags = ResolveRenderFlags(Flags);
    CKTimeManager *timeManager = m_Context ? m_Context->GetTimeManager() : nullptr;
    CK_RENDER_FLAGS renderFlags = ApplyFrameRateLimitOptions(inputFlags, timeManager);
    if ((renderFlags & CK_RENDER_SKIPDRAWSCENE) != 0)
        return CK_OK;

    ++m_DrawSceneCalls;
    memset(&m_Stats, 0, sizeof(VxStats));
    m_Stats.SmoothedFps = m_SmoothedFps;

    if (!(renderFlags & CK_RENDER_DONOTUPDATEEXTENTS)) {
        m_ObjectExtents.Resize(0);
    }

    if (frameLog)
        CK_LOG("DrawScene", "calling m_RenderedScene->Draw");
    CKERROR err = m_RenderedScene->Draw(renderFlags);
    if (frameLog)
        CK_LOG("DrawScene", "done");

    --m_DrawSceneCalls;

    return err;
}

CKERROR RCKRenderContext::BackToFront(CK_RENDER_FLAGS Flags) {
    // IDA: 0x1006abdd
    if (m_DeviceDestroying)
        return CK_OK;
    if (!m_RasterizerContext)
        return CKERR_INVALIDRENDERCONTEXT;

    CK_RENDER_FLAGS inputFlags = ResolveRenderFlags(Flags);
    CKTimeManager *timeManager = m_Context ? m_Context->GetTimeManager() : nullptr;
    CK_RENDER_FLAGS renderFlags = ApplyFrameRateLimitOptions(inputFlags, timeManager);

    // Check if we need to do back-to-front or have render target
    if (!(renderFlags & CK_RENDER_DOBACKTOFRONT) && !m_TargetTexture)
        return CK_OK;

    // Screen dump functionality (Ctrl+Alt+F10)
    // VK_CONTROL=17, VK_MENU(Alt)=18, VK_F10=121
#if defined(_WIN32)
    if (m_RenderManager->m_EnableScreenDump.Value &&
        GetAsyncKeyState(VK_CONTROL) &&
        GetAsyncKeyState(VK_MENU) &&
        GetAsyncKeyState(VK_F10)) {
        DumpToFile("\\CKScreenShot_Color.bmp", nullptr, VXBUFFER_BACKBUFFER);
        DumpToFile("\\CKScreenShot_Depth.bmp", nullptr, VXBUFFER_ZBUFFER);
        DumpToFile("\\CKScreenShot_Stencil.bmp", nullptr, VXBUFFER_STENCILBUFFER);
        // Wait for keys to be released
        while (GetAsyncKeyState(VK_F10)) {}
        while (GetAsyncKeyState(VK_CONTROL)) {}
        while (GetAsyncKeyState(VK_MENU)) {}
    }
#endif

    if (m_TargetTexture) {
        // Direct render-target textures already own the drawn contents. Flush
        // the RTT views so later scene draws in this Virtools frame can sample
        // the updated texture, matching the old SetTargetTexture contract.
        m_FFPipeline.GetRenderPipeline().EndFrame(FALSE);
    } else {
        // Normal back-to-front path

        // PV Information watermark handling
        if (m_Context->IsPlaying()) {
            if (m_Context->GetPVInformation() != m_PVInformation) {
                LoadPVInformationTexture();
            }
            if (m_PVInformation) {
                DrawPVInformationWatermark();
            }
        }

        // End frame and present
        CKBOOL waitVbl = (renderFlags & CK_RENDER_WAITVBL) != 0;
        LogPresentFrameRateContract("BackToFront/EndFrame", inputFlags, renderFlags, timeManager);
        m_FFPipeline.GetRenderPipeline().EndFrame(waitVbl);
    }

    // Debug mode handling (Ctrl+Alt+F11 to enter, various keys while in debug mode)
    // VK_F11=122, VK_R=82, VK_INSERT=45, VK_HOME=36, VK_PRIOR(PageUp)=33
    if (m_RenderManager->m_EnableDebugMode.Value) {
#if defined(_WIN32)
        if ((m_Flags & 1) != 0) {
            // Debug mode is active
            if (m_CurrentObjectDesc.Length() > 0) {
                HDC hdc = GetDC((HWND) m_WinHandle);
                RECT rc;
                rc.left = 1;
                rc.top = 1;
                rc.right = 300;
                rc.bottom = 400;
                SetBkMode(hdc, TRANSPARENT);

                // Draw shadow (black text offset by 1,1)
                SetTextColor(hdc, 0x000000);
                DrawTextA(hdc, m_CurrentObjectDesc.CStr(), -1, &rc, DT_NOCLIP);
                rc.left++;
                rc.top++;
                SetTextColor(hdc, 0x000000);
                DrawTextA(hdc, m_CurrentObjectDesc.CStr(), -1, &rc, DT_NOCLIP);
                rc.left--;
                rc.top--;

                // Draw white text
                SetTextColor(hdc, 0xFFFFFF);
                DrawTextA(hdc, m_CurrentObjectDesc.CStr(), -1, &rc, DT_NOCLIP);

                // If showing cache state (m_Flags bit 2)
                if ((m_Flags & 2) != 0) {
                    m_CurrentObjectDesc = "Cache State:";
                    FillStateString();
                    rc.left = rc.right;
                    rc.right += 200;
                    rc.left++;
                    rc.top++;
                    SetTextColor(hdc, 0x000000);
                    DrawTextA(hdc, m_StateString.CStr(), -1, &rc, DT_NOCLIP);
                    rc.left--;
                    rc.top--;
                    SetTextColor(hdc, 0xFFFFFF);
                    DrawTextA(hdc, m_StateString.CStr(), -1, &rc, DT_NOCLIP);
                }

                ReleaseDC((HWND) m_WinHandle, hdc);
            }

            m_CurrentObjectDesc = "DEBUG RENDER MODE : Ins,Home,Page Up\n\n";

            // Wait for input
            CKBOOL done = FALSE;
            while (!done) {
                // R key - toggle cache state display (bit 2)
                if (GetAsyncKeyState('R')) {
                    if ((m_Flags & 2) != 0)
                        m_Flags &= ~2;
                    else
                        m_Flags |= 2;
                    done = TRUE;
                }
                // Insert key - set FPS interval to 1
                if (GetAsyncKeyState(VK_INSERT)) {
                    m_FpsInterval = 1;
                    done = TRUE;
                }
                // Home key - set FPS interval to 5
                if (GetAsyncKeyState(VK_HOME)) {
                    m_FpsInterval = 5;
                    done = TRUE;
                }
                // Page Up key - set FPS interval to 10
                if (GetAsyncKeyState(VK_PRIOR)) {
                    m_FpsInterval = 10;
                    done = TRUE;
                }
                // Ctrl+Alt+F11 - exit debug mode
                if (GetAsyncKeyState(VK_CONTROL) && GetAsyncKeyState(VK_MENU)) {
                    if (GetAsyncKeyState(VK_F11)) {
                        m_Flags &= ~1; // Clear debug mode flag
                        m_FpsInterval = 1;
                        done = TRUE;
                    }
                }
            }

            // Wait for keys to be released
            while (GetAsyncKeyState(VK_INSERT)) {}
            while (GetAsyncKeyState(VK_HOME)) {}
            while (GetAsyncKeyState(VK_PRIOR)) {}
            while (GetAsyncKeyState(VK_CONTROL)) {}
            while (GetAsyncKeyState(VK_MENU)) {}
            while (GetAsyncKeyState('R')) {}
        }

        // Check for Ctrl+Alt+F11 to enter debug mode
        if (GetAsyncKeyState(VK_CONTROL) && GetAsyncKeyState(VK_MENU) && GetAsyncKeyState(VK_F11)) {
            m_Flags |= 1; // Set debug mode flag
            m_FpsInterval = 1;
            m_CurrentObjectDesc = "DEBUG RENDER MODE : Ins,Home,Page Up\n\n";
            // Wait for keys to be released
            while (GetAsyncKeyState(VK_CONTROL)) {}
            while (GetAsyncKeyState(VK_MENU)) {}
        }
#endif
    }

    return CK_OK;
}

CKERROR RCKRenderContext::Render(CK_RENDER_FLAGS Flags) {
    // IDA: 0x1006948e
    VxTimeProfiler profiler;

    if (!m_Active)
        return CKERR_RENDERCONTEXTINACTIVE;
    if (!m_RasterizerContext)
        return CKERR_INVALIDRENDERCONTEXT;

    // Resolve flags and normalize present sync from CK_FRAMERATE_LIMITS.
    CK_RENDER_FLAGS renderFlags = ApplyFrameRateLimitOptions(
        ResolveRenderFlags(Flags), m_Context ? m_Context->GetTimeManager() : nullptr);

    // Prepare cameras for rendering
    PrepareCameras(renderFlags);
    m_Camera = nullptr;

    // IDA: Check for camera plane master attribute ("1CamPl8ne4SterCube2Rend")
    // This looks up an attribute on the attached camera to find a master camera.
    if (m_RenderedScene && m_RenderedScene->m_AttachedCamera) {
        CKAttributeManager *attrManager = m_Context->GetAttributeManager();
        if (attrManager) {
            CKAttributeType attrType = attrManager->GetAttributeTypeByName("1CamPl8ne4SterCube2Rend");
            CKParameterOut *attrParam = m_RenderedScene->m_AttachedCamera->GetAttributeParameter(attrType);
            if (attrParam) {
                CKDWORD *pCameraId = (CKDWORD *) attrParam->GetReadDataPtr(FALSE);
                if (pCameraId) {
                    CKObject *cameraObj = m_Context->GetObject(*pCameraId);
                    if (cameraObj && CKIsChildClassOf(cameraObj, CKCID_CAMERA)) {
                        m_Camera = (RCKCamera *) cameraObj;
                    }
                }
            }
        }
    }

    CKERROR err = CK_OK;

    // Check for stereo rendering
    if (FALSE) {
        // Stereo rendering path
        VxMatrix originalWorldMat;
        Vx3DMatrixIdentity(originalWorldMat);
        CK3dEntity *rootEntity = m_RenderedScene ? m_RenderedScene->m_RootEntity : nullptr;
        if (rootEntity) {
            const VxMatrix &wm = rootEntity->GetWorldMatrix();
            memcpy(&originalWorldMat, &wm, sizeof(VxMatrix));
        }

        // Get right vector from world matrix (first row)
        VxVector rightVec(originalWorldMat[0][0], originalWorldMat[0][1], originalWorldMat[0][2]);
        rightVec.Normalize();

        // Calculate eye offset
        float halfFocal = -0.5f * m_FocalLength;
        VxVector eyeOffset = rightVec * halfFocal;

        VxMatrix leftWorldMat = originalWorldMat;
        VxMatrix rightWorldMat = originalWorldMat;

        // Offset position for left and right eye
        leftWorldMat[3][0] -= eyeOffset.x;
        leftWorldMat[3][1] -= eyeOffset.y;
        leftWorldMat[3][2] -= eyeOffset.z;
        rightWorldMat[3][0] += eyeOffset.x;
        rightWorldMat[3][1] += eyeOffset.y;
        rightWorldMat[3][2] += eyeOffset.z;

        float projOffset = 2.0f * m_FocalLength * m_NearPlane / m_EyeSeparation;

        // Clear both buffers (stereo not supported in v2)
        err = Clear(renderFlags);
        if (err != CK_OK) {
            RestoreStereoRenderState(rootEntity, originalWorldMat);
            return err;
        }

        if (!rootEntity) {
            RestoreStereoRenderState(rootEntity, originalWorldMat);
            return CKERR_INVALIDRENDERCONTEXT;
        }

        // Render right eye
        rootEntity->SetWorldMatrix(rightWorldMat, FALSE);
        if (!m_Camera) {
            UpdateProjection(FALSE);
            m_ProjectionMatrix[2][0] = -0.5f * m_ProjectionMatrix[0][0] * projOffset;
            m_FFPipeline.SetTransform(VXMATRIX_PROJECTION, m_ProjectionMatrix);
        }
        err = DrawScene(renderFlags);
        if (err != CK_OK) {
            RestoreStereoRenderState(rootEntity, originalWorldMat);
            return err;
        }

        // Render left eye
        rootEntity->SetWorldMatrix(leftWorldMat, FALSE);
        if (!m_Camera) {
            m_ProjectionMatrix[2][0] = 0.5f * m_ProjectionMatrix[0][0] * projOffset;
            m_FFPipeline.SetTransform(VXMATRIX_PROJECTION, m_ProjectionMatrix);
        }
        err = DrawScene(renderFlags);
        if (err != CK_OK) {
            RestoreStereoRenderState(rootEntity, originalWorldMat);
            return err;
        }

        // Restore original state
        RestoreStereoRenderState(rootEntity, originalWorldMat);
    } else {
        // Normal rendering (non-stereo)
        const bool frameLog = RenderFrameLogEnabled();
        if (frameLog)
            CK_LOG("Render", "about to Clear");
        err = Clear(renderFlags);
        if (err != CK_OK)
            return err;

        if (frameLog)
            CK_LOG("Render", "about to DrawScene");
        err = DrawScene(renderFlags);
        if (err != CK_OK)
            return err;
        if (frameLog)
            CK_LOG("Render", "DrawScene done");
    }

    // FPS calculation
    ++m_TimeFpsCalc;
    float elapsed = m_RenderTimeProfiler.Current();
    if (elapsed >= 1000.0f) {
        float fps = (float) m_TimeFpsCalc * 1000.0f / elapsed;
        m_RenderTimeProfiler.Reset();
        m_TimeFpsCalc = 0;
        // Smooth FPS: 90% new value + 10% old value
        m_SmoothedFps = fps * 0.9f + m_SmoothedFps * 0.1f;
        m_Stats.SmoothedFps = m_SmoothedFps;
    }

    err = BackToFront(renderFlags);
    if (err != CK_OK)
        return err;

    // Handle extents tracking (when CK_RENDER_DONOTUPDATEEXTENTS flag is set)
    // Note: This stores render extent info, not object picking extents
    if ((renderFlags & CK_RENDER_DONOTUPDATEEXTENTS) != 0) {
        CKRenderExtents extents;
        extents.m_Rect = VxRect();
        GetViewRect(extents.m_Rect);
        extents.m_Flags = (CKDWORD) renderFlags; // Store flags
        extents.m_Camera = GetAttachedCamera() ? GetAttachedCamera()->GetID() : 0;
        m_Extents.PushBack(extents);
    }

    // Add profile time
    float profileTime = profiler.Current();
    m_Context->AddProfileTime(CK_PROFILE_RENDERTIME, profileTime);

    return CK_OK;
}

void RCKRenderContext::AddPreRenderCallBack(CK_RENDERCALLBACK Function, void *Argument, CKBOOL Temporary) {
    m_PreRenderCallBacks.AddPreCallback((void *) Function, Argument, Temporary, m_RenderManager);
}

void RCKRenderContext::RemovePreRenderCallBack(CK_RENDERCALLBACK Function, void *Argument) {
    m_PreRenderCallBacks.RemovePreCallback((void *) Function, Argument);
}

void RCKRenderContext::AddPostRenderCallBack(CK_RENDERCALLBACK Function, void *Argument, CKBOOL Temporary, CKBOOL BeforeTransparent) {
    // IDA: 0x1006ba31 - BeforeTransparent selects which container to use:
    // BeforeTransparent=TRUE -> m_PostRenderCallBacks (0x6C)
    // BeforeTransparent=FALSE -> m_PreRenderCallBacks (0x50)
    if (BeforeTransparent) {
        m_PostRenderCallBacks.AddPostCallback((void *) Function, Argument, Temporary, m_RenderManager);
    } else {
        m_PreRenderCallBacks.AddPostCallback((void *) Function, Argument, Temporary, m_RenderManager);
    }
}

void RCKRenderContext::RemovePostRenderCallBack(CK_RENDERCALLBACK Function, void *Argument) {
    // IDA: 0x1006ba88 - remove from both containers.
    m_PreRenderCallBacks.RemovePostCallback((void *) Function, Argument);
    m_PostRenderCallBacks.RemovePostCallback((void *) Function, Argument);
}

void RCKRenderContext::AddPostSpriteRenderCallBack(CK_RENDERCALLBACK Function, void *Argument, CKBOOL Temporary) {
    // IDA: 0x1006babb - uses m_PostSpriteRenderCallBacks at offset 0x88
    m_PostSpriteRenderCallBacks.AddPostCallback((void *) Function, Argument, Temporary, m_RenderManager);
}

void RCKRenderContext::RemovePostSpriteRenderCallBack(CK_RENDERCALLBACK Function, void *Argument) {
    // IDA: 0x1006baec - uses m_PostSpriteRenderCallBacks
    m_PostSpriteRenderCallBacks.RemovePostCallback((void *) Function, Argument);
}

VxDrawPrimitiveData *RCKRenderContext::GetDrawPrimitiveStructure(CKRST_DPFLAGS Flags, int VertexCount) {
    // IDA: 0x1006bc74
    if (Flags & CKRST_DP_VBUFFER) {
        m_DpFlags = Flags;
        m_VertexBufferIndex = 0;
        m_StartIndex = 0;
        m_VertexBufferCount = VertexCount;
    } else {
        m_DpFlags = 0;
        m_VertexBufferIndex = 0;
        m_StartIndex = -1;
        m_VertexBufferCount = 0;
    }

    // IDA: 0x1006bdb1 - call UserDrawPrimitiveDataClass::GetStructure
    return m_UserDrawPrimitiveData->GetStructure(Flags, VertexCount);
}

CKWORD *RCKRenderContext::GetDrawPrimitiveIndices(int IndicesCount) {
    // IDA: 0x1006bf52 - uses UserDrawPrimitiveDataClass::GetIndices
    return m_UserDrawPrimitiveData->GetIndices(IndicesCount);
}

void RCKRenderContext::Transform(VxVector *Dest, VxVector *Src, CK3dEntity *Ref) {
    if (!Src || !Dest) return;

    VxMatrix world = Ref ? Ref->GetWorldMatrix() : m_WorldMatrix;
    VxMatrix wvp;
    VxMatrix vp;
    Vx3DMultiplyMatrix4(vp, m_ViewMatrix, m_ProjectionMatrix);
    Vx3DMultiplyMatrix4(wvp, world, vp);

    VxVector4 clip;
    clip.x = Src->x * wvp[0][0] + Src->y * wvp[1][0] + Src->z * wvp[2][0] + wvp[3][0];
    clip.y = Src->x * wvp[0][1] + Src->y * wvp[1][1] + Src->z * wvp[2][1] + wvp[3][1];
    clip.z = Src->x * wvp[0][2] + Src->y * wvp[1][2] + Src->z * wvp[2][2] + wvp[3][2];
    clip.w = Src->x * wvp[0][3] + Src->y * wvp[1][3] + Src->z * wvp[2][3] + wvp[3][3];

    if (clip.w != 0.0f) {
        float invW = 1.0f / clip.w;
        float ndcX = clip.x * invW;
        float ndcY = clip.y * invW;
        Dest->x = (ndcX * 0.5f + 0.5f) * m_ViewportData.ViewWidth + m_ViewportData.ViewX;
        Dest->y = (0.5f - ndcY * 0.5f) * m_ViewportData.ViewHeight + m_ViewportData.ViewY;
        Dest->z = clip.z * invW;
    } else {
        Dest->x = Dest->y = Dest->z = 0.0f;
    }
}

CKERROR RCKRenderContext::GoFullScreen(int Width, int Height, int Bpp, int Driver, int RefreshRate) {
    // IDA: 0x1006c0cb
    CKERROR err = CK_OK;

    // Check if already fullscreen
    if (m_Fullscreen)
        return CKERR_ALREADYFULLSCREEN;

    // Check if another context is already fullscreen
    if (m_RenderManager->GetFullscreenContext())
        return CKERR_ALREADYFULLSCREEN;

    // Save current settings for restoration later
    CKRenderContextSettings savedSettings;
    savedSettings.m_Rect.left = m_RasterizerContext->m_PosX;
    savedSettings.m_Rect.top = m_RasterizerContext->m_PosY;
    savedSettings.m_Rect.right = m_RasterizerContext->m_Width;
    savedSettings.m_Rect.bottom = m_RasterizerContext->m_Height;
    savedSettings.m_Bpp = m_RasterizerContext->m_Bpp;
    savedSettings.m_Zbpp = m_RasterizerContext->m_ZBpp;
    savedSettings.m_StencilBpp = m_RasterizerContext->m_StencilBpp;
    m_FullscreenSettings = savedSettings;

    // Save window parent and position
    m_AppHandle = VxGetParent(m_WinHandle);
    VxGetWindowRect(m_WinHandle, &m_WinRect);
    if (m_AppHandle) {
        VxScreenToClient(m_AppHandle, (CKPOINT *) &m_WinRect.left);
        VxScreenToClient(m_AppHandle, (CKPOINT *) &m_WinRect.right);
    }

    // Destroy current device
    DestroyDevice();

    // Create fullscreen device
    CKRECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = Width;
    rect.bottom = Height;

    err = Create(m_WinHandle, Driver, &rect, TRUE, Bpp, 0, 0, RefreshRate);

    if (err != CK_OK) {
        // Failed - restore window
        VxSetParent(m_WinHandle, m_AppHandle);
        VxMoveWindow(m_WinHandle, m_WinRect.left, m_WinRect.top,
                     m_WinRect.right - m_WinRect.left, m_WinRect.bottom - m_WinRect.top, FALSE);

        // Try to recreate old context
        CKRECT oldRect;
        oldRect.left = m_FullscreenSettings.m_Rect.left;
        oldRect.top = m_FullscreenSettings.m_Rect.top;
        oldRect.right = m_FullscreenSettings.m_Rect.right + oldRect.left;
        oldRect.bottom = m_FullscreenSettings.m_Rect.bottom + oldRect.top;

        Create(m_WinHandle, m_DriverIndex, &oldRect, FALSE,
               m_FullscreenSettings.m_Bpp, m_FullscreenSettings.m_Zbpp,
               m_FullscreenSettings.m_StencilBpp, 0);
    } else {
        // Success
        m_RenderedScene->UpdateViewportSize(TRUE, CK_RENDER_USECURRENTSETTINGS);
        Clear((CK_RENDER_FLAGS) (CK_RENDER_CLEARBACK | CK_RENDER_CLEARZ | CK_RENDER_CLEARSTENCIL), 0);
        BackToFront(CK_RENDER_USECURRENTSETTINGS);
        Clear((CK_RENDER_FLAGS) (CK_RENDER_CLEARBACK | CK_RENDER_CLEARZ | CK_RENDER_CLEARSTENCIL), 0);
    }

    return err;
}

CKERROR RCKRenderContext::StopFullScreen() {
    // IDA: 0x1006c2ef
    // Check if we are the fullscreen context
    if (m_RenderManager->GetFullscreenContext() != m_RasterizerContext)
        return CKERR_INVALIDRENDERCONTEXT;

    if (!m_Fullscreen)
        return CK_OK;

    m_Fullscreen = FALSE;
    DestroyDevice();

    // Restore window parent and position
    VxSetParent(m_WinHandle, m_AppHandle);
    VxMoveWindow(m_WinHandle, m_WinRect.left, m_WinRect.top,
                 m_WinRect.right - m_WinRect.left, m_WinRect.bottom - m_WinRect.top, FALSE);

    // Recreate windowed context with saved settings
    CKRECT rect;
    rect.left = m_FullscreenSettings.m_Rect.left;
    rect.top = m_FullscreenSettings.m_Rect.top;
    rect.right = m_FullscreenSettings.m_Rect.right + rect.left;
    rect.bottom = m_FullscreenSettings.m_Rect.bottom + rect.top;

    CKERROR err = Create(m_WinHandle, m_DriverIndex, &rect, FALSE,
                         m_FullscreenSettings.m_Bpp, m_FullscreenSettings.m_Zbpp,
                         m_FullscreenSettings.m_StencilBpp, 0);

    m_RenderedScene->UpdateViewportSize(FALSE, CK_RENDER_USECURRENTSETTINGS);

    return err;
}

CKBOOL RCKRenderContext::IsFullScreen() {
    return m_Fullscreen;
}

int RCKRenderContext::GetDriverIndex() {
    // IDA: 0x10067647
    return m_DriverIndex;
}

CKBOOL RCKRenderContext::ChangeDriver(int NewDriver) {
    // IDA: 0x1006765b
    // Cannot change driver while fullscreen
    if (m_Fullscreen)
        return FALSE;

    // Already on this driver
    if (NewDriver == m_DriverIndex)
        return FALSE;

    // If -1, use current driver index
    if (NewDriver == -1) {
        NewDriver = m_DriverIndex;
    } else {
        // Check if forced to software
        if (m_RenderManager->m_ForceSoftware.Value != 0) {
            CKRasterizerDriver *drv = m_RenderManager->GetDriver(NewDriver);
            if (!drv || drv->m_Hardware) {
                NewDriver = m_RenderManager->GetPreferredSoftwareDriver();
            }
        }
    }

    // Get the new driver
    CKRasterizerDriver *newDriver = m_RenderManager->GetDriver(NewDriver);
    CKRasterizerDriver *oldDriver = m_RasterizerDriver;

    if (!newDriver)
        return FALSE;

    // Check 2D caps
    if (!(newDriver->m_2DCaps.Caps & CKRST_2DCAPS_WINDOWED))
        return FALSE;

    // Save current settings for fallback (IDA assumes m_RasterizerContext is valid)
    m_FullscreenSettings.m_Rect.left = m_RasterizerContext->m_PosX;
    m_FullscreenSettings.m_Rect.top = m_RasterizerContext->m_PosY;
    m_FullscreenSettings.m_Rect.right = m_RasterizerContext->m_Width;
    m_FullscreenSettings.m_Rect.bottom = m_RasterizerContext->m_Height;
    m_FullscreenSettings.m_Bpp = m_RasterizerContext->m_Bpp;
    m_FullscreenSettings.m_Zbpp = m_RasterizerContext->m_ZBpp;
    m_FullscreenSettings.m_StencilBpp = m_RasterizerContext->m_StencilBpp;

    m_DeviceDestroying = TRUE;

    // Notify render manager we're destroying device
    m_RenderManager->DestroyingDevice((CKRenderContext *) this);

    // Destroy old context
    if (m_RasterizerDriver && m_RasterizerContext) {
        m_RasterizerDriver->DestroyContext(m_RasterizerContext);
    }
    m_RasterizerContext = nullptr;
    m_RasterizerDriver = nullptr;
    m_ProjectionUpdated = FALSE;

    // Set new driver and create context
    m_RasterizerDriver = newDriver;
    m_RasterizerContext = m_RasterizerDriver->CreateContext();

    // Settings moved out of rasterizer context in v2
    // m_RasterizerContext->m_EnableScreenDump = m_RenderManager->m_EnableScreenDump.Value;
    // m_RasterizerContext->m_Antialias = m_RenderManager->m_Antialias.Value;

    // Try to create the context with current settings
    CKBOOL created = m_RasterizerContext->Create(
        m_WinHandle,
        m_Settings.m_Rect.left, m_Settings.m_Rect.top,
        m_Settings.m_Rect.right, m_Settings.m_Rect.bottom,
        m_Settings.m_Bpp, 0, 0,
        m_Settings.m_Zbpp, m_Settings.m_StencilBpp);

    if (created) {
        // Success - update driver index and settings
        m_DriverIndex = NewDriver;
        m_Settings.m_Rect.left = m_RasterizerContext->m_PosX;
        m_Settings.m_Rect.top = m_RasterizerContext->m_PosY;
        m_Settings.m_Rect.right = m_RasterizerContext->m_Width;
        m_Settings.m_Rect.bottom = m_RasterizerContext->m_Height;
        m_Settings.m_Bpp = m_RasterizerContext->m_Bpp;
        m_Settings.m_Zbpp = m_RasterizerContext->m_ZBpp;
        m_Settings.m_StencilBpp = m_RasterizerContext->m_StencilBpp;

        // SetTransparentMode removed in rasterizer v2
        m_DeviceDestroying = FALSE;
        return TRUE;
    } else {
        // Failed - restore old driver
        m_RasterizerDriver->DestroyContext(m_RasterizerContext);
        m_RasterizerDriver = oldDriver;
        m_RasterizerContext = m_RasterizerDriver->CreateContext();
        // m_Antialias moved out of rasterizer context in v2

        CKBOOL restored = m_RasterizerContext->Create(
            m_WinHandle,
            m_FullscreenSettings.m_Rect.left, m_FullscreenSettings.m_Rect.top,
            m_FullscreenSettings.m_Rect.right, m_FullscreenSettings.m_Rect.bottom,
            m_FullscreenSettings.m_Bpp, 0, 0,
            m_FullscreenSettings.m_Zbpp, m_FullscreenSettings.m_StencilBpp);

        m_DeviceDestroying = FALSE;

        if (!restored) {
            m_RasterizerDriver->DestroyContext(m_RasterizerContext);
            m_RasterizerContext = nullptr;
        }
        return FALSE;
    }
}

WIN_HANDLE RCKRenderContext::GetWindowHandle() {
    return m_WinHandle;
}

void RCKRenderContext::ScreenToClient(Vx2DVector *ioPoint) {
    // IDA: 0x1006c075
    VxScreenToClient(m_WinHandle, (CKPOINT *) ioPoint);
}

void RCKRenderContext::ClientToScreen(Vx2DVector *ioPoint) {
    // IDA: 0x1006c096
    VxClientToScreen(m_WinHandle, (CKPOINT *) ioPoint);
}

CKERROR RCKRenderContext::SetWindowRect(VxRect &rect, CKDWORD Flags) {
    return Resize((int) rect.left, (int) rect.top, (int) rect.GetWidth(), (int) rect.GetHeight(), Flags);
}

void RCKRenderContext::GetWindowRect(VxRect &rect, CKBOOL ScreenRelative) {
    // IDA: 0x1006cd2f
    if (ScreenRelative) {
        CKPOINT pt1, pt2;
        pt1.x = m_Settings.m_Rect.left;
        pt1.y = m_Settings.m_Rect.top;
        pt2.x = m_Settings.m_Rect.right + m_Settings.m_Rect.left;
        pt2.y = m_Settings.m_Rect.bottom + m_Settings.m_Rect.top;
        VxClientToScreen(m_WinHandle, &pt1);
        VxClientToScreen(m_WinHandle, &pt2);
        rect.left = (float) pt1.x;
        rect.top = (float) pt1.y;
        rect.right = (float) pt2.x;
        rect.bottom = (float) pt2.y;
    } else {
        rect.left = (float) m_Settings.m_Rect.left;
        rect.top = (float) m_Settings.m_Rect.top;
        rect.right = (float) (m_Settings.m_Rect.right + m_Settings.m_Rect.left);
        rect.bottom = (float) (m_Settings.m_Rect.bottom + m_Settings.m_Rect.top);
    }
}

int RCKRenderContext::GetHeight() {
    // IDA: 0x1006ce44
    return m_Settings.m_Rect.bottom;
}

int RCKRenderContext::GetWidth() {
    // IDA: 0x1006ce30
    return m_Settings.m_Rect.right;
}

CKERROR RCKRenderContext::Resize(int PosX, int PosY, int SizeX, int SizeY, CKDWORD Flags) {
    // IDA: 0x1006cb04
    if (m_DeviceDestroying)
        return CKERR_INVALIDRENDERCONTEXT;

    // If no rasterizer context, try to create one
    if (!m_RasterizerContext) {
        if (SizeX && SizeY) {
            CKRECT rect;
            rect.left = PosX;
            rect.top = PosY;
            rect.bottom = SizeY + PosY;
            rect.right = SizeX + PosX;
            Create(m_WinHandle, m_DriverIndex, &rect, FALSE, -1, -1, -1, 0);
        } else {
            Create(m_WinHandle, m_DriverIndex, nullptr, FALSE, -1, -1, -1, 0);
        }

        if (!m_RasterizerContext)
            return CKERR_INVALIDRENDERCONTEXT;
    }

    if (m_Fullscreen)
        return CKERR_ALREADYFULLSCREEN;

    // Update position if not VX_RESIZE_NOMOVE
    if ((Flags & VX_RESIZE_NOMOVE) == 0) {
        m_Settings.m_Rect.left = PosX;
        m_Settings.m_Rect.top = PosY;
    }

    // Update size if not VX_RESIZE_NOSIZE
    if ((Flags & VX_RESIZE_NOSIZE) == 0) {
        if (!SizeX || !SizeY) {
            CKRECT clientRect;
            VxGetClientRect(m_WinHandle, &clientRect);
            SizeX = clientRect.right;
            SizeY = clientRect.bottom;
        }
        m_Settings.m_Rect.right = SizeX;
        m_Settings.m_Rect.bottom = SizeY;
        m_ViewportData.ViewX = 0;
        m_ViewportData.ViewY = 0;
        m_ViewportData.ViewWidth = SizeX;
        m_ViewportData.ViewHeight = SizeY;
        m_ProjectionUpdated = FALSE;
    }

    if (m_RasterizerContext->Resize(PosX, PosY, SizeX, SizeY, Flags)) {
        m_RenderedScene->UpdateViewportSize(FALSE, CK_RENDER_USECURRENTSETTINGS);
        return CK_OK;
    } else {
        m_RenderedScene->UpdateViewportSize(FALSE, CK_RENDER_USECURRENTSETTINGS);
        return CKERR_OUTOFMEMORY;
    }
}

void RCKRenderContext::SetViewRect(VxRect &rect) {
    // IDA: 0x1006ce58
    m_ViewportData.ViewX = (int) rect.left;
    m_ViewportData.ViewY = (int) rect.top;
    m_ViewportData.ViewWidth = (int) rect.GetWidth();
    m_ViewportData.ViewHeight = (int) rect.GetHeight();
    m_FFPipeline.SetViewport(m_ViewportData);
    UpdateProjection(TRUE);
}

void RCKRenderContext::GetViewRect(VxRect &rect) {
    // IDA: 0x1006cec2
    rect.left = (float) m_ViewportData.ViewX;
    rect.top = (float) m_ViewportData.ViewY;
    rect.right = rect.left + (float) m_ViewportData.ViewWidth;
    rect.bottom = rect.top + (float) m_ViewportData.ViewHeight;
}

VX_PIXELFORMAT RCKRenderContext::GetPixelFormat(int *Bpp, int *Zbpp, int *StencilBpp) {
    // IDA: 0x100675f2
    if (Bpp) *Bpp = m_Settings.m_Bpp;
    if (Zbpp) *Zbpp = m_Settings.m_Zbpp;
    if (StencilBpp) *StencilBpp = m_Settings.m_StencilBpp;

    return _32_ARGB8888;
}

void RCKRenderContext::SetState(VXRENDERSTATETYPE State, CKDWORD Value) {
    m_FFPipeline.SetRenderState(State, Value);
}

CKDWORD RCKRenderContext::GetState(VXRENDERSTATETYPE State) {
    return m_FFPipeline.GetRenderState(State);
}

CKBOOL RCKRenderContext::SetTexture(CKTexture *tex, CKBOOL Clamped, int Stage) {
    if (tex) {
        return tex->SetAsCurrent(this, Clamped, Stage);
    } else {
        m_FFPipeline.ResetTextureStage(Stage);
        return TRUE;
    }
}

CKBOOL RCKRenderContext::SetTextureStageState(CKRST_TEXTURESTAGESTATETYPE State, CKDWORD Value, int Stage) {
    m_FFPipeline.SetTextureStageState(Stage, State, Value);
    return TRUE;
}

CKRasterizerContext *RCKRenderContext::GetRasterizerContext() {
    return m_RasterizerContext;
}

void RCKRenderContext::SetClearBackground(CKBOOL ClearBack) {
    if (ClearBack)
        m_RenderFlags = (CK_RENDER_FLAGS) (m_RenderFlags | CK_RENDER_CLEARBACK);
    else
        m_RenderFlags = (CK_RENDER_FLAGS) (m_RenderFlags & ~CK_RENDER_CLEARBACK);
}

CKBOOL RCKRenderContext::GetClearBackground() {
    return (m_RenderFlags & CK_RENDER_CLEARBACK) != 0;
}

void RCKRenderContext::SetClearZBuffer(CKBOOL ClearZ) {
    if (ClearZ)
        m_RenderFlags = (CK_RENDER_FLAGS) (m_RenderFlags | CK_RENDER_CLEARZ);
    else
        m_RenderFlags = (CK_RENDER_FLAGS) (m_RenderFlags & ~CK_RENDER_CLEARZ);
}

CKBOOL RCKRenderContext::GetClearZBuffer() {
    return (m_RenderFlags & CK_RENDER_CLEARZ) != 0;
}

void RCKRenderContext::GetGlobalRenderMode(VxShadeType *Shading, CKBOOL *Texture, CKBOOL *Wireframe) {
    // IDA: 0x10067b26
    *Shading = (VxShadeType) m_Shading;
    *Texture = m_TextureEnabled;
    *Wireframe = m_DisplayWireframe;
}

void RCKRenderContext::SetGlobalRenderMode(VxShadeType Shading, CKBOOL Texture, CKBOOL Wireframe) {
    // IDA: 0x10067af5
    m_Shading = Shading;
    m_TextureEnabled = Texture;
    m_DisplayWireframe = Wireframe;
}

void RCKRenderContext::SetCurrentRenderOptions(CKDWORD flags) {
    m_RenderFlags = (CK_RENDER_FLAGS) flags;
}

CKDWORD RCKRenderContext::GetCurrentRenderOptions() {
    return m_RenderFlags;
}

void RCKRenderContext::ChangeCurrentRenderOptions(CKDWORD Add, CKDWORD Remove) {
    m_RenderFlags = (CK_RENDER_FLAGS) (m_RenderFlags | Add);
    m_RenderFlags = (CK_RENDER_FLAGS) (m_RenderFlags & ~Remove);
}

void RCKRenderContext::SetCurrentExtents(VxRect &extents) {
    m_CurrentExtents = extents;
}

void RCKRenderContext::GetCurrentExtents(VxRect &extents) {
    extents = m_CurrentExtents;
}

void RCKRenderContext::SetAmbientLight(float R, float G, float B) {
    // IDA: 0x1006b7c7
    m_RenderedScene->m_AmbientLight = RGBAFTOCOLOR(R, G, B, 1.0f);
}

void RCKRenderContext::SetAmbientLight(CKDWORD Color) {
    // IDA: 0x1006b7f7
    m_RenderedScene->m_AmbientLight = Color;
}

CKDWORD RCKRenderContext::GetAmbientLight() {
    // IDA: 0x1006b810
    return m_RenderedScene->m_AmbientLight;
}

void RCKRenderContext::SetFogMode(VXFOG_MODE Mode) {
    // IDA: 0x1006b824
    m_RenderedScene->m_FogMode = Mode;
}

void RCKRenderContext::SetFogStart(float Start) {
    // IDA: 0x1006b851
    m_RenderedScene->m_FogStart = Start;
}

void RCKRenderContext::SetFogEnd(float End) {
    // IDA: 0x1006b87e
    m_RenderedScene->m_FogEnd = End;
}

void RCKRenderContext::SetFogDensity(float Density) {
    // IDA: 0x1006b8ab
    m_RenderedScene->m_FogDensity = Density;
}

void RCKRenderContext::SetFogColor(CKDWORD Color) {
    // IDA: 0x1006b8d8
    m_RenderedScene->m_FogColor = Color;
}

VXFOG_MODE RCKRenderContext::GetFogMode() {
    // IDA: 0x1006b83d
    return (VXFOG_MODE) m_RenderedScene->m_FogMode;
}

float RCKRenderContext::GetFogStart() {
    // IDA: 0x1006b86a
    return m_RenderedScene->m_FogStart;
}

float RCKRenderContext::GetFogEnd() {
    // IDA: 0x1006b897
    return m_RenderedScene->m_FogEnd;
}

float RCKRenderContext::GetFogDensity() {
    // IDA: 0x1006b8c4
    return m_RenderedScene->m_FogDensity;
}

CKDWORD RCKRenderContext::GetFogColor() {
    // IDA: 0x1006b8f1
    return m_RenderedScene->m_FogColor;
}

CKBOOL RCKRenderContext::DrawPrimitive(VXPRIMITIVETYPE pType, CKWORD *indices, int indexcount, VxDrawPrimitiveData *data) {
    if (!data)
        return FALSE;
    if (data->VertexCount <= 0)
        return FALSE;

    // Set lighting mode based on normals
    if ((data->Flags & CKRST_DP_LIGHT) != 0 && data->NormalPtr) {
        m_FFPipeline.SetRenderState(VXRENDERSTATE_LIGHTING, 1);
    } else {
        if (data->SpecularColorPtr)
            data->Flags |= CKRST_DP_SPECULAR;
        if (data->ColorPtr)
            data->Flags |= CKRST_DP_DIFFUSE;
        m_FFPipeline.SetRenderState(VXRENDERSTATE_LIGHTING, 0);
    }

    if (!indices)
        indexcount = data->VertexCount;

    // Update stats
    switch (pType) {
    case VX_POINTLIST:
        m_Stats.NbPointsDrawn += data->VertexCount;
        break;
    case VX_LINELIST:
        m_Stats.NbLinesDrawn += indexcount >> 1;
        break;
    case VX_LINESTRIP:
        m_Stats.NbLinesDrawn += indexcount - 1;
        break;
    case VX_TRIANGLELIST:
        m_Stats.NbTrianglesDrawn += indexcount / 3;
        break;
    case VX_TRIANGLESTRIP:
    case VX_TRIANGLEFAN:
        m_Stats.NbTrianglesDrawn += indexcount - 2;
        break;
    default:
        break;
    }
    m_Stats.NbVerticesProcessed += data->VertexCount;

    CKRasterizerEncoder *encoder = m_FFPipeline.GetRenderPipeline().GetEncoder();
    CKRenderView view;
    if (data->Flags & CKRST_DP_TRANSFORM) {
        view = m_Current3DView;
        if (m_FFPipeline.GetRenderState(VXRENDERSTATE_ALPHABLENDENABLE) ||
            !m_FFPipeline.GetRenderState(VXRENDERSTATE_ZWRITEENABLE)) {
            view = CKRP_VIEW_TRANSPARENT;
        }
    } else {
        view = m_Current2DView;
    }
    m_FFPipeline.SetViewport(m_ViewportData);
    VxDrawPrimitiveData drawData = *data;
    drawData.Flags &= ~CKRST_DP_VBUFFER;
    m_FFPipeline.DrawPrimitive(encoder, view, pType, indices, indexcount, &drawData);
    return TRUE;
}

void RCKRenderContext::TransformVertices(int VertexCount, VxTransformData *data, CK3dEntity *Ref) {
    if (VertexCount <= 0 || !data || !data->InVertices)
        return;

    VxMatrix world = Ref ? Ref->GetWorldMatrix() : m_WorldMatrix;
    VxMatrix vp;
    VxMatrix wvp;
    Vx3DMultiplyMatrix4(vp, m_ViewMatrix, m_ProjectionMatrix);
    Vx3DMultiplyMatrix4(wvp, world, vp);

    const unsigned int inStride = data->InStride ? data->InStride : sizeof(VxVector);
    const unsigned int outStride = data->OutStride ? data->OutStride : sizeof(VxVector4);
    const unsigned int screenStride = data->ScreenStride ? data->ScreenStride : sizeof(VxVector4);
    CKBYTE *in = static_cast<CKBYTE *>(data->InVertices);
    CKBYTE *out = static_cast<CKBYTE *>(data->OutVertices);
    CKBYTE *screen = static_cast<CKBYTE *>(data->ScreenVertices);

    CKDWORD allAnd = VXCLIP_ALL;
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    bool haveScreen = false;

    for (int i = 0; i < VertexCount; ++i) {
        const VxVector *src = reinterpret_cast<const VxVector *>(in + i * inStride);
        VxVector4 clip;
        clip.x = src->x * wvp[0][0] + src->y * wvp[1][0] + src->z * wvp[2][0] + wvp[3][0];
        clip.y = src->x * wvp[0][1] + src->y * wvp[1][1] + src->z * wvp[2][1] + wvp[3][1];
        clip.z = src->x * wvp[0][2] + src->y * wvp[1][2] + src->z * wvp[2][2] + wvp[3][2];
        clip.w = src->x * wvp[0][3] + src->y * wvp[1][3] + src->z * wvp[2][3] + wvp[3][3];

        if (out)
            *reinterpret_cast<VxVector4 *>(out + i * outStride) = clip;

        CKDWORD flags = 0;
        if (clip.x < -clip.w) flags |= VXCLIP_LEFT;
        if (clip.x >  clip.w) flags |= VXCLIP_RIGHT;
        if (clip.y < -clip.w) flags |= VXCLIP_BOTTOM;
        if (clip.y >  clip.w) flags |= VXCLIP_TOP;
        if (clip.z < 0.0f)    flags |= VXCLIP_FRONT;
        if (clip.z >  clip.w) flags |= VXCLIP_BACK;

        if (data->ClipFlags)
            data->ClipFlags[i] = flags;
        allAnd &= flags;

        if (screen) {
            VxVector4 dst;
            if (clip.w != 0.0f) {
                const float rhw = 1.0f / clip.w;
                const float ndcX = clip.x * rhw;
                const float ndcY = clip.y * rhw;
                dst.x = (ndcX * 0.5f + 0.5f) * m_ViewportData.ViewWidth + m_ViewportData.ViewX;
                dst.y = (0.5f - ndcY * 0.5f) * m_ViewportData.ViewHeight + m_ViewportData.ViewY;
                dst.z = clip.z * rhw;
                dst.w = rhw;
            } else {
                dst.x = dst.y = dst.z = dst.w = 0.0f;
            }
            *reinterpret_cast<VxVector4 *>(screen + i * screenStride) = dst;

            if (!haveScreen) {
                minX = maxX = dst.x;
                minY = maxY = dst.y;
                haveScreen = true;
            } else {
                if (dst.x < minX) minX = dst.x;
                if (dst.x > maxX) maxX = dst.x;
                if (dst.y < minY) minY = dst.y;
                if (dst.y > maxY) maxY = dst.y;
            }
        }
    }

    data->m_Offscreen = allAnd;
    if (haveScreen) {
        data->m_2dExtents.left = (int)minX;
        data->m_2dExtents.top = (int)minY;
        data->m_2dExtents.right = (int)maxX;
        data->m_2dExtents.bottom = (int)maxY;
    }
}

CKDWORD RCKRenderContext::ComputeBoxVisibility(const VxBbox &box, const VxMatrix &world, VxRect *extents) {
    if (!box.IsValid())
        return 1;

    const float frustumClass = m_Frustum.Classify(box, world);
    if (frustumClass > 0.0f)
        return 0;

    VxMatrix vp;
    VxMatrix wvp;
    Vx3DMultiplyMatrix4(vp, m_ViewMatrix, m_ProjectionMatrix);
    Vx3DMultiplyMatrix4(wvp, world, vp);

    const float right = (float)m_ViewportData.ViewX + (float)m_ViewportData.ViewWidth;
    const float bottom = (float)m_ViewportData.ViewY + (float)m_ViewportData.ViewHeight;
    VxRect screen((float)m_ViewportData.ViewX, (float)m_ViewportData.ViewY, right, bottom);

    VXCLIP_FLAGS allOr = (VXCLIP_FLAGS)0;
    VXCLIP_FLAGS allAnd = (VXCLIP_FLAGS)VXCLIP_ALL;
    VxRect projected(100000000.0f, 100000000.0f, -100000000.0f, -100000000.0f);
    const XBOOL visible = VxTransformBox2D(wvp, box, &screen, &projected, allOr, allAnd);
    if (!visible) {
        if (extents)
            *extents = screen;
        return 1;
    }

    if (extents)
        *extents = projected;

    return (allOr & VXCLIP_ALL || frustumClass == 0.0f) ? 1 : 2;
}

void RCKRenderContext::SetWorldTransformationMatrix(const VxMatrix &M) {
    m_WorldMatrix = M;
    m_FFPipeline.SetTransform(VXMATRIX_WORLD, M);
}

void RCKRenderContext::SetProjectionTransformationMatrix(const VxMatrix &M) {
    m_ProjectionMatrix = M;
    m_FFPipeline.SetTransform(VXMATRIX_PROJECTION, M);
}

void RCKRenderContext::SetViewTransformationMatrix(const VxMatrix &M) {
    m_ViewMatrix = M;
    m_FFPipeline.SetTransform(VXMATRIX_VIEW, M);
}

const VxMatrix &RCKRenderContext::GetWorldTransformationMatrix() {
    return m_WorldMatrix;
}

const VxMatrix &RCKRenderContext::GetProjectionTransformationMatrix() {
    return m_ProjectionMatrix;
}

const VxMatrix &RCKRenderContext::GetViewTransformationMatrix() {
    return m_ViewMatrix;
}

CKBOOL RCKRenderContext::SetUserClipPlane(CKDWORD ClipPlaneIndex, const VxPlane &PlaneEquation) {
    if (ClipPlaneIndex >= 6)
        return FALSE;

    m_UserClipPlanes[ClipPlaneIndex] = PlaneEquation;
    m_FFPipeline.SetUserClipPlane((int)ClipPlaneIndex, PlaneEquation);
    return TRUE;
}

CKBOOL RCKRenderContext::GetUserClipPlane(CKDWORD ClipPlaneIndex, VxPlane &PlaneEquation) {
    if (ClipPlaneIndex >= 6)
        return FALSE;

    PlaneEquation = m_UserClipPlanes[ClipPlaneIndex];
    return TRUE;
}

// IDA: 0x1006823b - Internal 2D picking method
CK2dEntity *RCKRenderContext::_Pick2D(const Vx2DVector &pt, CKBOOL ignoreUnpickable) {
    // IDA lines 9-13: Copy point and adjust to local coordinates
    float x = pt.x;
    float y = pt.y;
    x = x - (float) m_Settings.m_Rect.left;
    y = y - (float) m_Settings.m_Rect.top;

    Vx2DVector localPt;
    localPt.x = x;
    localPt.y = y;

    // IDA line 14-15: Try foreground 2D root first (Get2dRoot(0) = foreground)
    RCK2dEntity *root = (RCK2dEntity *) Get2dRoot(FALSE);
    CK2dEntity *result = root->Pick(localPt, ignoreUnpickable);
    if (result)
        return result;

    // IDA line 18-19: Then try background 2D root (Get2dRoot(1) = background)
    root = (RCK2dEntity *) Get2dRoot(TRUE);
    return root->Pick(localPt, ignoreUnpickable);
}

// IDA: 0x100682bc - Internal 3D picking method
CK3dEntity *RCKRenderContext::Pick3D(const Vx2DVector &pt, VxIntersectionDesc *idesc, CK3dEntity *filter, CKBOOL ignoreUnpickable) {
    // IDA line 83-85: Get object extent count, early exit if empty
    const int objCount = m_ObjectExtents.Size();
    if (objCount == 0)
        return nullptr;

    // IDA line 86-89: Copy point to local variable
    Vx2DVector localPt;
    localPt.x = pt.x;
    localPt.y = pt.y;

    // IDA line 89-90: Initialize rayStart to (0,0,0)
    VxVector rayStart(0.0f, 0.0f, 0.0f);
    VxVector rayEnd;

    // IDA line 91-98: Viewport bounds check
    const float viewX = (float) m_ViewportData.ViewX;
    const float viewY = (float) m_ViewportData.ViewY;

    if (localPt.x < viewX || localPt.y < viewY)
        return nullptr;

    const float viewWidth = (float) m_ViewportData.ViewWidth;
    const float viewHeight = (float) m_ViewportData.ViewHeight;

    if (viewX + viewWidth < localPt.x || viewY + viewHeight < localPt.y)
        return nullptr;

    // IDA line 99: Calculate inverse view width
    const float invViewWidth = 1.0f / viewWidth;

    // =========================================================================
    // Path 1: Camera-relative picking using m_Extents (IDA: 0x100683b3-0x100687c0)
    // =========================================================================
    const int extCount = m_Extents.Size();
    if (extCount > 0) {
        VxIntersectionDesc tempDesc;

        // IDA line 105: Iterate extents backward
        for (int i = extCount - 1; i >= 0; --i) {
            CKRenderExtents *ext = &m_Extents[i];

            // IDA line 108-118: Check if point is within extent rect
            if (localPt.x < ext->m_Rect.left || ext->m_Rect.right < localPt.x)
                continue;
            if (localPt.y < ext->m_Rect.top || ext->m_Rect.bottom < localPt.y)
                continue;

            // IDA line 120-121: Get camera from extent
            CKCamera *camera = (CKCamera *) m_Context->GetObject(ext->m_Camera);
            if (!camera)
                continue;

            // IDA line 123-124: Get camera back plane and FOV
            const float backPlane = camera->GetBackPlane();
            const float cameraFov = camera->GetFov();

            // IDA line 125-126: Initialize ray (VxRay constructor)
            VxRay ray;
            // IDA line 127-129: Get camera position and set ray origin
            camera->GetPosition(&ray.m_Origin);
            const VxMatrix &camMat = camera->GetWorldMatrix();
            ray.m_Direction.x = camMat[2][0];
            ray.m_Direction.y = camMat[2][1];
            ray.m_Direction.z = camMat[2][2];

            // IDA line 130-131: Calculate half FOV tangent
            const float halfFovTan = tanf(cameraFov * 0.5f);

            // IDA line 132: Calculate scale factor
            const float extentWidth = ext->m_Rect.GetWidth();
            const float scale = halfFovTan / (extentWidth * 0.5f);

            // IDA line 133-138: Adjust ray direction by camera right vector
            const VxVector camRight(camMat[0][0], camMat[0][1], camMat[0][2]);
            const float extCenterX = (ext->m_Rect.left + ext->m_Rect.right) * 0.5f;
            const float offsetX = (localPt.x - extCenterX) * scale;
            ray.m_Direction.x += camRight.x * offsetX;
            ray.m_Direction.y += camRight.y * offsetX;
            ray.m_Direction.z += camRight.z * offsetX;

            // IDA line 139-144: Adjust ray direction by camera up vector (subtracted)
            const VxVector camUp(camMat[1][0], camMat[1][1], camMat[1][2]);
            const float extCenterY = (ext->m_Rect.top + ext->m_Rect.bottom) * 0.5f;
            const float offsetY = (localPt.y - extCenterY) * scale;
            ray.m_Direction.x -= camUp.x * offsetY;
            ray.m_Direction.y -= camUp.y * offsetY;
            ray.m_Direction.z -= camUp.z * offsetY;

            // IDA line 145: Normalize ray direction
            ray.m_Direction.Normalize();

            // IDA line 146-147: Calculate ray end point
            VxVector rayEndPt;
            rayEndPt.x = ray.m_Origin.x + ray.m_Direction.x * backPlane;
            rayEndPt.y = ray.m_Origin.y + ray.m_Direction.y * backPlane;
            rayEndPt.z = ray.m_Origin.z + ray.m_Direction.z * backPlane;

            // IDA line 148-149: Initialize best distance and picked entity
            float bestDistance = backPlane;
            CK3dEntity *pickedEntity = nullptr;

            // IDA line 150-165: Iterate through object extents
            for (int j = 0; j < objCount; ++j) {
                CKObjectExtents *objExt = &m_ObjectExtents[j];
                RCK3dEntity *entity = (RCK3dEntity *) objExt->m_Entity;

                // IDA line 153: Skip null entities and apply filter
                if (!entity)
                    continue;
                if (filter && entity != (RCK3dEntity *) filter)
                    continue;
                // IDA: Check pickable flag unless ignoreUnpickable
                if (!ignoreUnpickable && (entity->GetMoveableFlags() & VX_MOVEABLE_PICKABLE) == 0)
                    continue;

                // IDA line 155: Get bounding box
                const VxBbox &bbox = entity->GetBoundingBox(FALSE);

                // IDA line 156-163: Test ray-box intersection, then ray intersection
                if (VxIntersect::RayBox(ray, bbox)) {
                    if (entity->RayIntersection(&ray.m_Origin, &rayEndPt, &tempDesc, nullptr, CKRAYINTERSECTION_SEGMENT)) {
                        if (tempDesc.Distance < bestDistance) {
                            memcpy(idesc, &tempDesc, sizeof(VxIntersectionDesc));
                            bestDistance = tempDesc.Distance;
                            pickedEntity = (CK3dEntity *) entity;
                        }
                    }
                }
            }

            // IDA line 166-167: If found, return picked entity
            if (pickedEntity)
                return pickedEntity;

            // IDA line 168-169: If extent has CLEARBACK flag, return null (stop searching)
            if ((ext->m_Flags & CK_RENDER_CLEARBACK) != 0)
                return nullptr;
        }
    }

    // =========================================================================
    // Path 2: Standard picking using m_ObjectExtents (IDA: 0x100687db-0x10068ab4)
    // =========================================================================

    // IDA line 175-176: Calculate normalized screen coordinates
    const float nx = (localPt.x - viewX + localPt.x - viewX) * invViewWidth - 1.0f;
    const float ny = (viewY - localPt.y + viewY - localPt.y + viewHeight) * invViewWidth;

    // IDA line 177-190: Calculate ray start/end based on perspective mode
    if (m_Perspective) {
        // IDA line 179-182: Perspective projection
        const float halfFovTan = tanf(m_Fov * 0.5f);
        rayEnd.x = nx * m_NearPlane * halfFovTan;
        rayEnd.y = ny * m_NearPlane * halfFovTan;
    } else {
        // IDA line 186-189: Orthographic projection
        rayStart.x = nx / m_Zoom;
        rayEnd.x = rayStart.x;
        rayStart.y = ny / m_Zoom;
        rayEnd.y = rayStart.y;
    }

    // IDA line 191: Set ray end Z to near plane
    rayEnd.z = m_NearPlane;

    // IDA line 192-195: Initialize best distance and result entity
    float bestDistance = 1.0e30f;
    CK3dEntity *bestEntity = nullptr;
    RCK3dEntity *currentEntity = nullptr;
    VxIntersectionDesc tempDesc;

    // IDA line 196-248: Iterate through object extents
    for (int k = 0; k < objCount; ++k) {
        CKObjectExtents *ext = &m_ObjectExtents[k];
        currentEntity = (RCK3dEntity *) ext->m_Entity;

        // IDA line 199: Skip null entities and apply filter
        if (!currentEntity)
            continue;
        if (filter && currentEntity != (RCK3dEntity *) filter)
            continue;
        // IDA: Check pickable flag unless ignoreUnpickable
        if (!ignoreUnpickable && (currentEntity->GetMoveableFlags() & VX_MOVEABLE_PICKABLE) == 0)
            continue;

        // IDA line 201-212: Check if point is within object extent rect
        if (localPt.x > ext->m_Rect.right)
            continue;
        if (localPt.x < ext->m_Rect.left)
            continue;
        if (localPt.y > ext->m_Rect.bottom)
            continue;
        if (localPt.y < ext->m_Rect.top)
            continue;

        // IDA line 214-215: Clear temp descriptor
        memset(&tempDesc, 0, sizeof(tempDesc));

        // IDA line 216: Get transformed intersection point
        VxVector transformedPt;

        // IDA line 216-228: Check mesh Pick2D
        if (currentEntity->GetCurrentMesh()) {
            RCKMesh *mesh = (RCKMesh *) currentEntity->GetCurrentMesh();
            if (mesh->Pick2D(localPt, &tempDesc, this, currentEntity)) {
                // IDA line 220: Transform intersection point to root entity space
                currentEntity->Transform(&transformedPt, &tempDesc.IntersectionPoint, m_RenderedScene->m_RootEntity);

                // IDA line 221-222: Calculate distance from rayStart
                VxVector diff;
                diff.x = transformedPt.x - rayStart.x;
                diff.y = transformedPt.y - rayStart.y;
                diff.z = transformedPt.z - rayStart.z;
                const float dist = diff.Magnitude();

                // IDA line 223-227: Update best if closer
                if (dist < bestDistance) {
                    memcpy(idesc, &tempDesc, sizeof(VxIntersectionDesc));
                    bestDistance = dist;
                    bestEntity = (CK3dEntity *) currentEntity;
                }
            }
        }

        // IDA line 231-242: Ray intersection test
        CK3dEntity *refEntity = m_RenderedScene ? m_RenderedScene->m_RootEntity : nullptr;
        if (currentEntity->RayIntersection(&rayStart, &rayEnd, &tempDesc, refEntity, CKRAYINTERSECTION_DEFAULT)) {
            if (tempDesc.Distance < bestDistance) {
                memcpy(idesc, &tempDesc, sizeof(VxIntersectionDesc));
                bestDistance = tempDesc.Distance;
                bestEntity = (CK3dEntity *) currentEntity;
            }
        }
    }

    // IDA line 249: Return best entity found
    return bestEntity;
}

// IDA: 0x1006810c
CKRenderObject *RCKRenderContext::Pick(int x, int y, CKPICKRESULT *oRes, CKBOOL iIgnoreUnpickable) {
    CKPOINT pt = {x, y};
    return Pick(pt, oRes, iIgnoreUnpickable);
}

// IDA: 0x1006810c
CKRenderObject *RCKRenderContext::Pick(CKPOINT pt, CKPICKRESULT *oRes, CKBOOL iIgnoreUnpickable) {
    // IDA line 14: Initialize VxIntersectionDesc
    VxIntersectionDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.FaceIndex = -1;

    // IDA line 15-17: Convert screen point to local coordinates (relative to render context window)
    Vx2DVector localPt;
    localPt.y = (float) (pt.y - m_Settings.m_Rect.top);
    localPt.x = (float) (pt.x - m_Settings.m_Rect.left);

    // IDA line 18: Pick 3D objects
    CK3dEntity *obj = Pick3D(localPt, &desc, nullptr, iIgnoreUnpickable);

    // IDA line 19-34: Fill result structure if provided
    if (oRes) {
        // IDA line 21-22: Convert local coords back to screen coords for 2D picking
        localPt.x = (float) m_Settings.m_Rect.left + localPt.x;
        localPt.y = (float) m_Settings.m_Rect.top + localPt.y;

        // IDA line 23: Also pick 2D entities
        CK2dEntity *sprite = _Pick2D(localPt, iIgnoreUnpickable);

        // IDA line 24-29: Copy intersection data from desc to oRes
        oRes->IntersectionPoint = desc.IntersectionPoint;
        oRes->IntersectionNormal = desc.IntersectionNormal;
        oRes->TexU = desc.TexU;
        oRes->TexV = desc.TexV;
        oRes->Distance = desc.Distance;
        oRes->FaceIndex = desc.FaceIndex;

        // IDA line 30-34: Set Sprite to 2D entity ID if found
        if (sprite) {
            oRes->Sprite = sprite->GetID();
        } else {
            oRes->Sprite = 0;
        }
    }

    // IDA line 36: Return 3D picked object (or null if none)
    return obj;
}

// IDA: 0x1006ED80 - Rectangle intersection test
// Returns: 0 = no intersection, 1 = fully contained, 2 = partial intersection
static int RectIntersectTest(const VxRect *a, const VxRect *b) {
    // IDA line 23-28: Check a.left >= b.right (no intersection)
    if (a->left >= b->right)
        return 0;
    // IDA line 29-30: Check a.right < b.left (no intersection)
    if (a->right < b->left)
        return 0;
    // IDA line 31-36: Check a.top >= b.bottom (no intersection)
    if (a->top >= b->bottom)
        return 0;
    // IDA line 37-38: Check a.bottom < b.top (no intersection)
    if (a->bottom < b->top)
        return 0;

    // IDA line 39-40: Check if a.left < b.left (partial)
    if (a->left < b->left)
        return 2;
    // IDA line 41-46: Check if a.right > b.right (partial)
    if (a->right > b->right)
        return 2;
    // IDA line 47-48: Check if a.top < b.top (partial)
    if (a->top < b->top)
        return 2;
    // IDA line 49-56: Check if a.bottom > b.bottom (partial), else fully inside
    if (a->bottom > b->bottom)
        return 2;

    // Fully contained
    return 1;
}

// IDA: 0x10068abc
CKERROR RCKRenderContext::RectPick(const VxRect &r, XObjectPointerArray &oObjects, CKBOOL Intersect) {
    // IDA line 29-30: Return error if oObjects is null (shouldn't happen with reference, but matches IDA check)
    // In C++ with reference this check is not needed, but we keep the logic flow

    // IDA line 31-34: Convert rect to local integer coordinates
    int localLeft = (int) r.left - m_Settings.m_Rect.left;
    int localTop = (int) r.top - m_Settings.m_Rect.top;
    int localRight = (int) r.right - m_Settings.m_Rect.left;
    int localBottom = (int) r.bottom - m_Settings.m_Rect.top;

    // IDA line 35-36: Initialize temp rects
    VxRect extRect;
    VxRect homogRect;

    // IDA line 37: Copy input rect to pickRect
    VxRect pickRect = r;

    // IDA line 38-41: Create offset and apply to pickRect (sub_10060010)
    Vx2DVector offset;
    offset.x = (float) m_Settings.m_Rect.left;
    offset.y = (float) m_Settings.m_Rect.top;
    pickRect.left -= offset.x;
    pickRect.right -= offset.x;
    pickRect.top -= offset.y;
    pickRect.bottom -= offset.y;

    // IDA line 42: Normalize pickRect (sub_1006ED20)
    pickRect.Normalize();

    // IDA line 43-66: Iterate through 3D entities in rendered scene
    if (m_RenderedScene) {
        for (CK3dEntity **it = (CK3dEntity **) m_RenderedScene->m_3DEntities.Begin();
             it != (CK3dEntity **) m_RenderedScene->m_3DEntities.End(); ++it) {
            CK3dEntity *ent = *it;
            if (!ent)
                continue;

            // IDA line 52: Check if interface object (ObjectFlags & 2) (sub_10060380)
            if ((ent->GetObjectFlags() & CK_OBJECT_INTERFACEOBJ) != 0)
                continue;

            // IDA line 54: Check if visible
            if (!ent->IsVisible())
                continue;

            // IDA line 56: Get render extents
            ent->GetRenderExtents(extRect);

            // IDA line 57-61: Test intersection
            int result = RectIntersectTest(&extRect, &pickRect);
            if (result != 0) {
                // IDA line 60: If not Intersect mode OR result is not partial (2)
                if (!Intersect || result != 2) {
                    // IDA line 61: InsertFront
                    oObjects.PushFront(ent);
                }
            }
        }
    }

    // IDA line 67-85: Iterate through background 2D entities (Get2dRoot(1))
    {
        CK2dEntity *current = nullptr;
        CK2dEntity *root2D = Get2dRoot(TRUE);
        while (true) {
            current = root2D->HierarchyParser(current);
            if (!current)
                break;

            // IDA line 75: Skip interface objects (ObjectFlags & 1)
            if ((current->GetObjectFlags() & CK_OBJECT_INTERFACEOBJ) != 0)
                continue;
            // IDA line 75: Skip non-pickable 2D entities (Flags & 0x80)
            if ((current->GetFlags() & CK_2DENTITY_NOTPICKABLE) != 0)
                continue;

            // IDA line 77: Get extents
            current->GetExtents(homogRect, extRect);

            // IDA line 78-82: Test intersection
            int result = RectIntersectTest(&extRect, &pickRect);
            if (result != 0) {
                if (!Intersect || result != 2) {
                    oObjects.PushFront(current);
                }
            }
        }
    }

    // IDA line 86-104: Iterate through foreground 2D entities (Get2dRoot(0))
    {
        CK2dEntity *current = nullptr;
        CK2dEntity *root2D = Get2dRoot(FALSE);
        while (true) {
            current = root2D->HierarchyParser(current);
            if (!current)
                break;

            // IDA line 94: Skip interface objects
            if ((current->GetObjectFlags() & CK_OBJECT_INTERFACEOBJ) != 0)
                continue;
            // IDA line 94: Skip non-pickable 2D entities
            if ((current->GetFlags() & CK_2DENTITY_NOTPICKABLE) != 0)
                continue;

            // IDA line 96: Get extents
            current->GetExtents(homogRect, extRect);

            // IDA line 97-101: Test intersection
            int result = RectIntersectTest(&extRect, &pickRect);
            if (result != 0) {
                if (!Intersect || result != 2) {
                    oObjects.PushFront(current);
                }
            }
        }
    }

    // IDA line 105: Return CK_OK
    return CK_OK;
}

void RCKRenderContext::AttachViewpointToCamera(CKCamera *cam) {
    // IDA: 0x1006b92d
    if (cam) {
        cam->ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
        m_RenderedScene->m_AttachedCamera = cam;

        // Copy camera's world matrix to root entity
        const VxMatrix &worldMat = cam->GetWorldMatrix();
        m_RenderedScene->m_RootEntity->SetWorldMatrix(worldMat);

        static int s_attachLogCount = 0;
        if (CameraAttachLogEnabled() && s_attachLogCount < 16) {
            CK3dEntity *target = cam->GetClassID() == CKCID_TARGETCAMERA ? cam->GetTarget() : nullptr;
            int aspectWidth = 0;
            int aspectHeight = 0;
            cam->GetAspectRatio(aspectWidth, aspectHeight);
            CK_LOG_FMT("RenderContext",
                       "AttachViewpointToCamera #%d: camera=%p name=%s class=%d target=%p targetName=%s fov=%.3f near=%.3f far=%.3f aspect=%d:%d pos=(%.3f %.3f %.3f) dir=(%.3f %.3f %.3f)",
                       s_attachLogCount,
                       (void *)cam,
                       cam->GetName() ? cam->GetName() : "",
                       cam->GetClassID(),
                       (void *)target,
                       target && target->GetName() ? target->GetName() : "",
                       cam->GetFov(), cam->GetFrontPlane(), cam->GetBackPlane(),
                       aspectWidth, aspectHeight,
                       worldMat[3][0], worldMat[3][1], worldMat[3][2],
                       worldMat[2][0], worldMat[2][1], worldMat[2][2]);
            ++s_attachLogCount;
        }

        if (cam->GetFlags() & CK_3DENTITY_CAMERAIGNOREASPECT) {
            SetFullViewport(&m_ViewportData, m_Settings.m_Rect.right, m_Settings.m_Rect.bottom);
        }
    }
}

void RCKRenderContext::DetachViewpointFromCamera() {
    m_RenderedScene->m_AttachedCamera = nullptr;
}

CKCamera *RCKRenderContext::GetAttachedCamera() {
    // IDA: 0x1006b919
    return (CKCamera *) m_RenderedScene->m_AttachedCamera;
}

CK3dEntity *RCKRenderContext::GetViewpoint() {
    return m_RenderedScene->GetRootEntity();
}

CKMaterial *RCKRenderContext::GetBackgroundMaterial() {
    // IDA: 0x1006b7b3
    return m_RenderedScene->m_BackgroundMaterial;
}

void RCKRenderContext::GetBoundingBox(VxBbox *BBox) {
    // IDA: 0x1006bbd9
    if (!BBox) return;

    BBox->Min = VxVector(1e30f, 1e30f, 1e30f);
    BBox->Max = VxVector(-1e30f, -1e30f, -1e30f);

    // Iterate scene graph root node children
    CKSceneGraphRootNode *rootNode = &m_RenderManager->m_SceneGraphRootNode;
    int count = rootNode->m_Children.Size();
    for (int i = 0; i < count; ++i) {
        CKSceneGraphNode *node = rootNode->m_Children[i];
        if (node && node->m_Entity) {
            RCK3dEntity *ent = node->m_Entity;
            if (ent->IsInRenderContext(this)) {
                BBox->Merge(node->m_Bbox);
            }
        }
    }
}

void RCKRenderContext::GetStats(VxStats *stats) {
    // IDA: 0x1006bbb5
    if (stats) memcpy(stats, &m_Stats, sizeof(VxStats));
}

void RCKRenderContext::SetCurrentMaterial(CKMaterial *mat, CKBOOL Lit) {
    if (mat) {
        mat->SetAsCurrent(this, Lit, 0);
    } else {
        m_FFPipeline.ResetMaterial();
        m_FFPipeline.DisableTextureStagesFrom(0);
    }
}

void RCKRenderContext::Activate(CKBOOL active) {
    m_Active = active;
}

int RCKRenderContext::DumpToMemory(const VxRect *iRect, VXBUFFER_TYPE buffer, VxImageDescEx &desc) {
    if (!m_RasterizerContext || buffer != VXBUFFER_BACKBUFFER)
        return FALSE;

    const int fbWidth = m_Settings.m_Rect.right > 0 ? m_Settings.m_Rect.right : m_RasterizerContext->m_Width;
    const int fbHeight = m_Settings.m_Rect.bottom > 0 ? m_Settings.m_Rect.bottom : m_RasterizerContext->m_Height;
    if (fbWidth <= 0 || fbHeight <= 0)
        return FALSE;

    VxRect rect = iRect ? *iRect : VxRect(0.0f, 0.0f, (float)fbWidth, (float)fbHeight);
    int left = (int)rect.left;
    int top = (int)rect.top;
    int right = (int)rect.right;
    int bottom = (int)rect.bottom;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > fbWidth) right = fbWidth;
    if (bottom > fbHeight) bottom = fbHeight;
    if (right <= left || bottom <= top)
        return FALSE;

    VxImageDescEx outDesc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, outDesc);
    outDesc.Width = right - left;
    outDesc.Height = bottom - top;
    outDesc.BytesPerLine = outDesc.Width * 4;
    outDesc.TotalImageSize = outDesc.BytesPerLine * outDesc.Height;
    outDesc.Image = desc.Image;
    desc.Set(outDesc);

    if (!desc.Image)
        return outDesc.TotalImageSize;

    VxImageDescEx fullDesc = outDesc;
    fullDesc.Width = fbWidth;
    fullDesc.Height = fbHeight;
    fullDesc.BytesPerLine = fbWidth * 4;
    fullDesc.TotalImageSize = fullDesc.BytesPerLine * fbHeight;

    XBYTE *fullImage = desc.Image;
    if (left != 0 || top != 0 || right != fbWidth || bottom != fbHeight) {
        fullImage = new XBYTE[fullDesc.TotalImageSize];
    }
    fullDesc.Image = fullImage;

    CKERROR err = m_RasterizerContext->ReadFrameBuffer(0, &fullDesc);
    if (err != CK_OK) {
        if (fullImage != desc.Image)
            delete[] fullImage;
        return FALSE;
    }

    if (fullImage != desc.Image) {
        for (int y = 0; y < outDesc.Height; ++y) {
            memcpy(desc.Image + y * outDesc.BytesPerLine,
                   fullImage + (top + y) * fullDesc.BytesPerLine + left * 4,
                   outDesc.BytesPerLine);
        }
        delete[] fullImage;
    }

    return outDesc.TotalImageSize;
}

int RCKRenderContext::CopyToVideo(const VxRect *iRect, VXBUFFER_TYPE buffer, VxImageDescEx &desc) {
    if (!m_RasterizerContext || buffer != VXBUFFER_BACKBUFFER)
        return FALSE;

    int fbWidth = m_Settings.m_Rect.right;
    int fbHeight = m_Settings.m_Rect.bottom;
    if (fbWidth <= 0 || fbHeight <= 0)
        return FALSE;

    VxRect rect;
    if (iRect) {
        rect = *iRect;
    } else {
        rect.left = 0.0f;
        rect.top = 0.0f;
        rect.right = (float)fbWidth;
        rect.bottom = (float)fbHeight;
    }

    int left = (int)rect.left;
    int top = (int)rect.top;
    int right = (int)rect.right;
    int bottom = (int)rect.bottom;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > fbWidth) right = fbWidth;
    if (bottom > fbHeight) bottom = fbHeight;
    if (right <= left || bottom <= top)
        return FALSE;

    const int width = right - left;
    const int height = bottom - top;
    if (desc.Width != width || desc.Height != height || desc.BitsPerPixel <= 0)
        return FALSE;

    VxImageDescEx videoFormat;
    VxPixelFormat2ImageDesc(_32_ARGB8888, videoFormat);
    videoFormat.Width = width;
    videoFormat.Height = height;
    videoFormat.BytesPerLine = width * videoFormat.BitsPerPixel / 8;
    videoFormat.TotalImageSize = videoFormat.BytesPerLine * height;

    if (!desc.Image)
        return videoFormat.TotalImageSize;

    VxImageDescEx uploadDesc = desc;
    CKBYTE *converted = nullptr;
    if (!SameCopyPixelFormat(desc, videoFormat)) {
        converted = ConvertCopyImage(desc, videoFormat, uploadDesc);
        if (!converted)
            return FALSE;
    } else {
        if (uploadDesc.BytesPerLine <= 0)
            uploadDesc.BytesPerLine = width * uploadDesc.BitsPerPixel / 8;
        if (uploadDesc.TotalImageSize <= 0)
            uploadDesc.TotalImageSize = uploadDesc.BytesPerLine * height;
    }

    if (m_CopyToVideoTexture == 0) {
        m_CopyToVideoTexture = m_RenderManager->CreateObjectIndex(CKRST_OBJ_TEXTURE);
    }

    CKERROR err = CK_OK;
    if (m_CopyToVideoWidth != width || m_CopyToVideoHeight != height) {
        if (m_CopyToVideoWidth > 0 && m_CopyToVideoHeight > 0)
            m_RasterizerContext->DeleteObject(m_CopyToVideoTexture, CKRST_OBJ_TEXTURE);

        CKTextureDesc texDesc;
        texDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
        texDesc.Format = videoFormat;
        texDesc.MipMapCount = 1;
        err = m_RasterizerContext->CreateTexture(m_CopyToVideoTexture, &texDesc, &uploadDesc);
        if (err == CK_OK) {
            m_CopyToVideoWidth = width;
            m_CopyToVideoHeight = height;
        }
    } else {
        err = m_RasterizerContext->UpdateTexture(m_CopyToVideoTexture, 0, 0, nullptr, &uploadDesc);
    }

    delete[] converted;
    if (err != CK_OK)
        return FALSE;

    CKFixedFunctionPipeline &ffp = m_FFPipeline;
    const CKDWORD oldCull = ffp.GetRenderState(VXRENDERSTATE_CULLMODE);
    const CKDWORD oldLighting = ffp.GetRenderState(VXRENDERSTATE_LIGHTING);
    const CKDWORD oldFog = ffp.GetRenderState(VXRENDERSTATE_FOGENABLE);
    const CKDWORD oldZEnable = ffp.GetRenderState(VXRENDERSTATE_ZENABLE);
    const CKDWORD oldZWrite = ffp.GetRenderState(VXRENDERSTATE_ZWRITEENABLE);
    const CKDWORD oldZFunc = ffp.GetRenderState(VXRENDERSTATE_ZFUNC);
    const CKDWORD oldAlphaBlend = ffp.GetRenderState(VXRENDERSTATE_ALPHABLENDENABLE);
    const CKDWORD oldAlphaTest = ffp.GetRenderState(VXRENDERSTATE_ALPHATESTENABLE);
    const CKDWORD oldClipPlanes = ffp.GetRenderState(VXRENDERSTATE_CLIPPLANEENABLE);
    const CKViewportData oldViewport = m_ViewportData;
    CKFFTextureStageSnapshot oldStages[CKFF_MAX_TEXTURE_STAGES];
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        ffp.SaveTextureStage(stage, oldStages[stage]);

    ffp.SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
    ffp.SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_FOGENABLE, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_ZENABLE, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_ZWRITEENABLE, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_ALWAYS);
    ffp.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 0);
    ffp.DisableTextureStagesFrom(0);
    ffp.SetTexture(0, m_CopyToVideoTexture);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    ffp.SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    ffp.SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSCLAMP);

    m_ViewportData.ViewX = 0;
    m_ViewportData.ViewY = 0;
    m_ViewportData.ViewWidth = fbWidth;
    m_ViewportData.ViewHeight = fbHeight;
    ffp.SetViewport(m_ViewportData);

    VxDrawPrimitiveData *data = GetDrawPrimitiveStructure(CKRST_DP_CL_VCT, 4);
    if (data) {
        float *pos = (float *)data->PositionPtr;
        float *uv = (float *)data->TexCoordPtr;
        CKDWORD white = 0xFFFFFFFF;
        VxFillStructure(4, data->ColorPtr, data->ColorStride, 4, &white);

        const float x0 = (float)left;
        const float y0 = (float)top;
        const float x1 = (float)right;
        const float y1 = (float)bottom;
        const float coords[8] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        memcpy(uv, coords, sizeof(coords));

        pos[0] = x0; pos[1] = y0; pos[2] = 0.0f; pos[3] = 1.0f;
        pos = (float *)((CKBYTE *)pos + data->PositionStride);
        pos[0] = x1; pos[1] = y0; pos[2] = 0.0f; pos[3] = 1.0f;
        pos = (float *)((CKBYTE *)pos + data->PositionStride);
        pos[0] = x1; pos[1] = y1; pos[2] = 0.0f; pos[3] = 1.0f;
        pos = (float *)((CKBYTE *)pos + data->PositionStride);
        pos[0] = x0; pos[1] = y1; pos[2] = 0.0f; pos[3] = 1.0f;

        DrawPrimitive(VX_TRIANGLEFAN, nullptr, 4, data);
    }

    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        ffp.RestoreTextureStage(stage, oldStages[stage]);
    m_ViewportData = oldViewport;
    ffp.SetViewport(m_ViewportData);
    ffp.SetRenderState(VXRENDERSTATE_CULLMODE, oldCull);
    ffp.SetRenderState(VXRENDERSTATE_LIGHTING, oldLighting);
    ffp.SetRenderState(VXRENDERSTATE_FOGENABLE, oldFog);
    ffp.SetRenderState(VXRENDERSTATE_ZENABLE, oldZEnable);
    ffp.SetRenderState(VXRENDERSTATE_ZWRITEENABLE, oldZWrite);
    ffp.SetRenderState(VXRENDERSTATE_ZFUNC, oldZFunc);
    ffp.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, oldAlphaBlend);
    ffp.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, oldAlphaTest);
    ffp.SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, oldClipPlanes);

    return data ? videoFormat.TotalImageSize : FALSE;
}

CKERROR RCKRenderContext::DumpToFile(CKSTRING filename, const VxRect *rect, VXBUFFER_TYPE buffer) {
    // IDA: 0x1006cf8f
    if (!filename)
        return -1;

    VxImageDescEx desc;
    const int requiredBytes = DumpToMemory(rect, buffer, desc);
    if (!requiredBytes)
        return -1;

    XBYTE *mem = new XBYTE[(size_t) requiredBytes];
    desc.Image = mem;

    if (DumpToMemory(rect, buffer, desc)) {
        if (CKSaveBitmap(filename, desc)) {
            delete[] mem;
            return 0;
        }

        delete[] mem;
        return -21;
    }

    delete[] mem;
    return -1;
}

VxDirectXData *RCKRenderContext::GetDirectXInfo() {
    // IDA: 0x1006bb75
    // Only return DirectX info if Family is 0 (DirectX family)
    if (m_RasterizerContext && m_RasterizerContext->m_Driver->m_2DCaps.Family == 0) {
        return (VxDirectXData *) nullptr;
    }
    return nullptr;
}

void RCKRenderContext::WarnEnterThread() {
    // WarnThread removed in rasterizer v2
}

void RCKRenderContext::WarnExitThread() {
    // WarnThread removed in rasterizer v2
}

// IDA: 0x10068220
CK2dEntity *RCKRenderContext::Pick2D(const Vx2DVector &v) {
    return _Pick2D(v, FALSE);
}

CKBOOL RCKRenderContext::SetRenderTarget(CKTexture *texture, int CubeMapFace) {
    // IDA: 0x1006c42d
    // Cannot set new texture target while one is already active
    if (m_TargetTexture && texture)
        return FALSE;
    if (!m_RasterizerContext)
        return FALSE;

    if (texture) {
        if (CubeMapFace < CKRST_CUBEFACE_XPOS || CubeMapFace > CKRST_CUBEFACE_ZNEG)
            return FALSE;
        if (!texture->IsCubeMap() && CubeMapFace != CKRST_CUBEFACE_XPOS)
            return FALSE;
        if (texture->IsCubeMap() &&
            (texture->GetSlotCount() != 6 || texture->GetWidth() != texture->GetHeight()))
            return FALSE;

        RCKTexture *target = static_cast<RCKTexture *>(texture);
        if (!target->EnsureRenderTarget(this, FALSE))
            return FALSE;

        m_CubeMapFace = static_cast<CKRST_CUBEFACE>(CubeMapFace);

        if (m_TargetFrameBuffer == 0) {
            RCKRenderManager *rm = static_cast<RCKRenderManager *>(m_Context->GetRenderManager());
            m_TargetFrameBuffer = rm->CreateObjectIndex(CKRST_OBJ_FRAMEBUFFER);
        }
        if (m_TargetDepthTexture == 0) {
            RCKRenderManager *rm = static_cast<RCKRenderManager *>(m_Context->GetRenderManager());
            m_TargetDepthTexture = rm->CreateObjectIndex(CKRST_OBJ_TEXTURE);
        }

        CKDepthTextureDesc depthDesc = {};
        depthDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_DEPTHSTENCIL;
        depthDesc.Width = (CKDWORD)texture->GetWidth();
        depthDesc.Height = (CKDWORD)texture->GetHeight();
        depthDesc.MipMapCount = 1;
        const CKBOOL needsStencil = m_Settings.m_StencilBpp > 0;
        depthDesc.DepthFormat = needsStencil ? CKRST_DEPTHFMT_D24S8 : CKRST_DEPTHFMT_D24;
        CKERROR depthErr = m_RasterizerContext->CreateDepthTexture(m_TargetDepthTexture, &depthDesc);
        if (depthErr != CK_OK && !needsStencil) {
            depthDesc.DepthFormat = CKRST_DEPTHFMT_D16;
            if (m_RasterizerContext->CreateDepthTexture(m_TargetDepthTexture, &depthDesc) != CK_OK)
                return FALSE;
        } else if (depthErr != CK_OK) {
            return FALSE;
        }

        CKFrameBufferAttachmentDesc color;
        color.Texture = target->GetRstTextureIndex();
        color.Mip = 0;
        color.Layer = (CKDWORD)CubeMapFace;

        CKFrameBufferDesc desc;
        desc.Color = &color;
        desc.ColorCount = 1;
        desc.DepthStencil.Texture = m_TargetDepthTexture;
        desc.DepthStencil.Mip = 0;
        desc.DepthStencil.Layer = 0;

        if (m_RasterizerContext->CreateFrameBuffer(m_TargetFrameBuffer, &desc) != CK_OK)
            return FALSE;

        m_RasterizerContext->SetViewFrameBuffer(CKRP_VIEW_CLEAR, m_TargetFrameBuffer);
        m_RasterizerContext->SetViewFrameBuffer(CKRP_VIEW_BACKGROUND2D, m_TargetFrameBuffer);
        m_RasterizerContext->SetViewFrameBuffer(CKRP_VIEW_RENDERFIRST3D, m_TargetFrameBuffer);
        m_RasterizerContext->SetViewFrameBuffer(CKRP_VIEW_OPAQUE3D, m_TargetFrameBuffer);
        m_RasterizerContext->SetViewFrameBuffer(CKRP_VIEW_TRANSPARENT, m_TargetFrameBuffer);
        m_RasterizerContext->SetViewFrameBuffer(CKRP_VIEW_FOREGROUND2D, m_TargetFrameBuffer);
    }

    if (texture) {
        m_TargetTexture = (RCKTexture *) texture;

        m_Settings.m_Rect.left = 0;
        m_Settings.m_Rect.top = 0;
        m_Settings.m_Rect.right = texture->GetWidth();
        m_Settings.m_Rect.bottom = texture->GetHeight();

        m_ViewportData.ViewX = 0;
        m_ViewportData.ViewY = 0;
        m_ViewportData.ViewWidth = texture->GetWidth();
        m_ViewportData.ViewHeight = texture->GetHeight();

        UpdateProjection(TRUE);
        return TRUE;
    }

    m_TargetTexture = nullptr;
    m_CubeMapFace = CKRST_CUBEFACE_XPOS;
    m_RasterizerContext->SetViewFrameBuffer(CKRP_VIEW_CLEAR, 0);
    m_RasterizerContext->SetViewFrameBuffer(CKRP_VIEW_BACKGROUND2D, 0);
    m_RasterizerContext->SetViewFrameBuffer(CKRP_VIEW_RENDERFIRST3D, 0);
    m_RasterizerContext->SetViewFrameBuffer(CKRP_VIEW_OPAQUE3D, 0);
    m_RasterizerContext->SetViewFrameBuffer(CKRP_VIEW_TRANSPARENT, 0);
    m_RasterizerContext->SetViewFrameBuffer(CKRP_VIEW_FOREGROUND2D, 0);

    CKRenderContextSettings savedSettings;
    savedSettings.m_Rect.left = m_RasterizerContext->m_PosX;
    savedSettings.m_Rect.top = m_RasterizerContext->m_PosY;
    savedSettings.m_Rect.right = m_RasterizerContext->m_Width;
    savedSettings.m_Rect.bottom = m_RasterizerContext->m_Height;
    savedSettings.m_Bpp = m_RasterizerContext->m_Bpp;
    savedSettings.m_Zbpp = m_RasterizerContext->m_ZBpp;
    savedSettings.m_StencilBpp = m_RasterizerContext->m_StencilBpp;
    m_Settings = savedSettings;

    SetFullViewport(&m_ViewportData, m_Settings.m_Rect.right, m_Settings.m_Rect.bottom);
    UpdateProjection(TRUE);
    return TRUE;
}

void RCKRenderContext::AddRemoveSequence(CKBOOL Start) {
    m_Start = Start;
    if (!Start) {
        m_RenderManager->m_SceneGraphRootNode.Rebuild();
    }
}

void RCKRenderContext::SetTransparentMode(CKBOOL Trans) {
    m_TransparentMode = Trans;
    // SetTransparentMode removed in rasterizer v2
}

void RCKRenderContext::AddDirtyRect(CKRECT *Rect) {
    // AddDirtyRect removed in rasterizer v2
}

void RCKRenderContext::RestoreScreenBackup() {
    // RestoreScreenBackup removed in rasterizer v2
}

CKDWORD RCKRenderContext::GetStencilFreeMask() {
    return m_StencilFreeMask;
}

void RCKRenderContext::UsedStencilBits(CKDWORD stencilBits) {
    // Original semantics: m_StencilFreeMask is a "used bits" mask (1 = used).
    m_StencilFreeMask |= stencilBits;
}

int RCKRenderContext::GetFirstFreeStencilBits() {
    // Original semantics: return the first bit index where the used mask is 0, else -1.
    for (int i = 0; i < 32; ++i) {
        if (((1u << i) & (CKDWORD) m_StencilFreeMask) == 0)
            return i;
    }
    return -1;
}

VxDrawPrimitiveData *RCKRenderContext::LockCurrentVB(CKDWORD VertexCount) {
    if (!m_DpFlags || !(m_DpFlags & CKRST_DP_VBUFFER))
        return nullptr;
    if (VertexCount > m_VertexBufferCount)
        m_VertexBufferCount = VertexCount;
    return m_UserDrawPrimitiveData->GetStructure((CKRST_DPFLAGS)m_DpFlags, VertexCount);
}

CKBOOL RCKRenderContext::ReleaseCurrentVB() {
    m_DpFlags = 0;
    m_VertexBufferIndex = 0;
    m_StartIndex = -1;
    m_VertexBufferCount = 0;
    return TRUE;
}

void RCKRenderContext::SetTextureMatrix(const VxMatrix &M, int Stage) {
    m_FFPipeline.SetTransform((VXMATRIX_TYPE)(VXMATRIX_TEXTURE0 + Stage), M);
}

void RCKRenderContext::SetStereoParameters(float EyeSeparation, float FocalLength) {
    // IDA: 0x10068093
    m_EyeSeparation = EyeSeparation;
    m_FocalLength = FocalLength;
}

void RCKRenderContext::GetStereoParameters(float &EyeSeparation, float &FocalLength) {
    // IDA: 0x100680b8
    EyeSeparation = m_EyeSeparation;
    FocalLength = m_FocalLength;
}

CKERROR RCKRenderContext::Create(void *Window, int Driver, CKRECT *rect, CKBOOL Fullscreen, int Bpp, int Zbpp, int StencilBpp, int RefreshRate) {
    // Initialize timing and stereo parameters
    m_SmoothedFps = 0.0f;
    m_TimeFpsCalc = 0;
    m_RenderTimeProfiler.Reset();
    m_FocalLength = 0.4f;
    m_EyeSeparation = 100.0f;

    // Check if another context is fullscreen
    if (m_RenderManager->GetFullscreenContext())
        return CKERR_ALREADYFULLSCREEN;

    // Must not have existing rasterizer context
    if (m_RasterizerContext)
        return CKERR_INVALIDRENDERCONTEXT;

    // Check if forcing software driver
    if (m_RenderManager->m_ForceSoftware.Value != 0) {
        CKRasterizerDriver *driverToCheck = m_RenderManager->GetDriver(Driver);
        if (!driverToCheck || driverToCheck->m_Hardware) {
            Driver = m_RenderManager->GetPreferredSoftwareDriver();
        }
    }

    // Get the rasterizer driver
    m_RasterizerDriver = m_RenderManager->GetDriver(Driver);
    if (!m_RasterizerDriver)
        return CKERR_INVALIDRENDERCONTEXT;

    m_WinHandle = Window;
    WIN_HANDLE oldParent = nullptr;
    CKRECT oldWindowRect = {0, 0, 0, 0};
    CKBOOL restoreWindowOnFailure = FALSE;

    // Get rect from parameter or window client rect
    CKRECT localRect;
    if (rect) {
        localRect = *rect;
    } else {
        VxGetClientRect(m_WinHandle, &localRect);
    }

    // Set driver index for non-fullscreen mode
    if (!Fullscreen) {
        m_DriverIndex = Driver;
    }

    m_DeviceDestroying = TRUE;

    // Fullscreen keeps child render windows attached to their owner.  The Player
    // owns the top-level fullscreen transition; detaching a child render HWND
    // leaves it outside that window and breaks focus, sizing, and restore.
    if (Fullscreen) {
        oldParent = VxGetParent(m_WinHandle);
        VxGetWindowRect(m_WinHandle, &oldWindowRect);
        if (oldParent) {
            VxScreenToClient(oldParent, (CKPOINT *) &oldWindowRect.left);
            VxScreenToClient(oldParent, (CKPOINT *) &oldWindowRect.right);
        }
        restoreWindowOnFailure = TRUE;
        m_AppHandle = oldParent;
        if (!VxMoveWindow(m_WinHandle, 0, 0, localRect.right, localRect.bottom, FALSE)) {
            if (restoreWindowOnFailure) {
                VxSetParent(m_WinHandle, oldParent);
                VxMoveWindow(m_WinHandle, oldWindowRect.left, oldWindowRect.top,
                             oldWindowRect.right - oldWindowRect.left, oldWindowRect.bottom - oldWindowRect.top, FALSE);
            }
            m_DeviceDestroying = FALSE;
            return CKERR_INVALIDOPERATION;
        }
    }

    // Set viewport
    int width = localRect.right - localRect.left;
    int height = localRect.bottom - localRect.top;
    SetFullViewport(&m_ViewportData, width, height);

    // Create rasterizer context
    m_RasterizerContext = m_RasterizerDriver->CreateContext();

    // Settings moved out of rasterizer context in v2
    // m_RasterizerContext->m_Antialias = (m_RenderManager->m_Antialias.Value != 0);
    // m_RasterizerContext->m_EnableScreenDump = (m_RenderManager->m_EnableScreenDump.Value != 0);
    // m_RasterizerContext->m_EnsureVertexShader = (m_RenderManager->m_EnsureVertexShader.Value != 0);

    // Create the actual rasterizer context
    if (!m_RasterizerContext->Create(m_WinHandle, localRect.left, localRect.top,
                                     width, height, Bpp, Fullscreen, RefreshRate, Zbpp, StencilBpp)) {
        m_RasterizerDriver->DestroyContext(m_RasterizerContext);
        m_RasterizerContext = nullptr;
        if (Fullscreen && restoreWindowOnFailure) {
            VxSetParent(m_WinHandle, oldParent);
            VxMoveWindow(m_WinHandle, oldWindowRect.left, oldWindowRect.top,
                         oldWindowRect.right - oldWindowRect.left, oldWindowRect.bottom - oldWindowRect.top, FALSE);
        }
        m_DeviceDestroying = FALSE;
        return CKERR_CANCREATERENDERCONTEXT;
    }

    // Initialize the fixed-function pipeline
    m_FFPipeline.Init(m_RasterizerContext);

    m_Fullscreen = Fullscreen;

    // Save settings from rasterizer context
    m_Settings.m_Rect.left = m_RasterizerContext->m_PosX;
    m_Settings.m_Rect.top = m_RasterizerContext->m_PosY;
    m_Settings.m_Rect.right = m_RasterizerContext->m_Width;
    m_Settings.m_Rect.bottom = m_RasterizerContext->m_Height;
    m_Settings.m_Bpp = m_RasterizerContext->m_Bpp;
    m_Settings.m_Zbpp = m_RasterizerContext->m_ZBpp;
    m_Settings.m_StencilBpp = m_RasterizerContext->m_StencilBpp;

    // Copy to fullscreen settings if not fullscreen
    if (!Fullscreen) {
        m_FullscreenSettings = m_Settings;
    }

    m_DeviceDestroying = FALSE;
    m_ProjectionUpdated = FALSE;
    m_Active = TRUE;
    m_VertexBufferCount = 0;
    m_DpFlags = 0;
    m_VertexBufferIndex = 0;
    m_StartIndex = -1;

    // If going fullscreen with uninitialized WinRect, save settings
    if (Fullscreen && m_WinRect.left == -1 && m_WinRect.right == -1) {
        m_FullscreenSettings.m_Rect.left = m_RasterizerContext->m_PosX;
        m_FullscreenSettings.m_Rect.top = m_RasterizerContext->m_PosY;
        m_FullscreenSettings.m_Rect.right = m_RasterizerContext->m_Width;
        m_FullscreenSettings.m_Rect.bottom = m_RasterizerContext->m_Height;
        m_WinRect.left = 0;
        m_WinRect.top = 0;
        m_WinRect.right = m_FullscreenSettings.m_Rect.right;
        m_WinRect.bottom = m_FullscreenSettings.m_Rect.bottom; // IDA bug: sets .right again, we fix it
    }

    return CK_OK;
}

RCKRenderContext::RCKRenderContext(CKContext *Context, CKSTRING name) : CKRenderContext(Context, name) {
    // IDA: 0x10066952

    m_Current3dEntity = nullptr;
    m_RenderManager = (RCKRenderManager *) Context->GetRenderManager();
    m_MaskFree = m_RenderManager->GetRenderContextMaskFree();
    if (!m_MaskFree) {
        m_Context->OutputToConsole("Error: no more render context mask available", TRUE);
    }
    m_WinHandle = nullptr;
    m_AppHandle = nullptr;
    m_RenderFlags = CK_RENDER_DEFAULTSETTINGS;

    // Create RenderedScene
    m_RenderedScene = new CKRenderedScene(this);

    // Create UserDrawPrimitiveData
    m_UserDrawPrimitiveData = new UserDrawPrimitiveDataClass();

    m_Fullscreen = FALSE;
    m_Active = FALSE;
    m_Perspective = TRUE;
    m_ProjectionUpdated = FALSE;
    m_DeviceDestroying = FALSE;
    m_Start = FALSE;
    m_TransparentMode = FALSE;
    m_RasterizerContext = nullptr;
    m_RasterizerDriver = nullptr;
    m_DriverIndex = 0;
    m_DisplayWireframe = FALSE;
    m_TextureEnabled = TRUE;
    m_Shading = GouraudShading;
    m_Zoom = 1.0f;
    m_NearPlane = 1.0f;
    m_FarPlane = 4000.0f;
    m_Fov = PI / 4.0f;

    // Initialize viewport
    m_ViewportData.ViewX = 0;
    m_ViewportData.ViewY = 0;
    m_ViewportData.ViewWidth = 0;
    m_ViewportData.ViewHeight = 0;
    m_ViewportData.ViewZMin = 0.0f;
    m_ViewportData.ViewZMax = 1.0f;

    m_ObjectExtents.Resize(500);

    // Clear stats
    memset(&m_Stats, 0, sizeof(m_Stats));

    // Initialize window rect
    m_WinRect.left = -1;
    m_WinRect.right = -1;
    m_WinRect.top = 0;
    m_WinRect.bottom = 0;

    // Initialize current extents
    m_CurrentExtents.left = 1000000.0f;
    m_CurrentExtents.top = 1000000.0f;
    m_CurrentExtents.bottom = -1000000.0f;
    m_CurrentExtents.right = -1000000.0f;

    m_FpsFrameCount = 0;
    m_TimeFpsCalc = 0;
    m_SmoothedFps = 0.0f;
    m_Flags = 0;
    m_SceneTraversalCalls = 0;
    m_TargetTexture = nullptr;
    m_TargetFrameBuffer = 0;
    m_TargetDepthTexture = 0;
    m_CopyToVideoTexture = 0;
    m_CopyToVideoWidth = 0;
    m_CopyToVideoHeight = 0;
    m_CubeMapFace = CKRST_CUBEFACE_XPOS;
    m_DrawSceneCalls = 0;
    m_SortTransparentObjects = 0;
    m_FocalLength = 0.4f;
    m_EyeSeparation = 100.0f;
    m_Camera = nullptr;
    m_PVInformation = (CKDWORD) -1;
    m_NCUTex = nullptr;
    m_DpFlags = 0;
    m_VertexBufferCount = 0;
    m_VertexBufferIndex = 0;
    m_StartIndex = (CKDWORD) -1;

    // Additional fields initialization
    m_StencilFreeMask = 0;
    m_FpsInterval = 0;
    memset(&m_Settings, 0, sizeof(m_Settings));
    memset(&m_FullscreenSettings, 0, sizeof(m_FullscreenSettings));
    m_ProjectionMatrix.Identity();
    Vx3DMatrixIdentity(m_WorldMatrix);
    Vx3DMatrixIdentity(m_ViewMatrix);
    m_Current2DView = CKRP_VIEW_FOREGROUND2D;
    m_Current3DView = CKRP_VIEW_OPAQUE3D;
}

RCKRenderContext::~RCKRenderContext() {
    // Based on IDA at 0x10066ebb
    DestroyDevice();
    DetachAll();
    ClearCallbacks();

    if (m_UserDrawPrimitiveData) {
        delete m_UserDrawPrimitiveData;
        m_UserDrawPrimitiveData = nullptr;
    }

    if (m_RenderedScene) {
        delete m_RenderedScene;
        m_RenderedScene = nullptr;
    }

    // Release the render context mask
    if (m_RenderManager)
        m_RenderManager->ReleaseRenderContextMaskFree(m_MaskFree);
}

CKERROR RCKRenderContext::RemapDependencies(CKDependenciesContext &context) {
    return CKObject::RemapDependencies(context);
}

CKERROR RCKRenderContext::Copy(CKObject &o, CKDependenciesContext &context) {
    CKRenderContext::Copy(o, context);

    if (CKIsChildClassOf(&o, CKCID_RENDERCONTEXT)) {
        RCKRenderContext *src = (RCKRenderContext *) &o;
        // Copy relevant settings
        m_Fullscreen = src->m_Fullscreen;
        m_DriverIndex = src->m_DriverIndex;
        m_RenderFlags = src->m_RenderFlags;
        m_Settings = src->m_Settings;
        m_WinRect = src->m_WinRect;
    }
    return CK_OK;
}

CKBOOL RCKRenderContext::DestroyDevice() {
    // Based on IDA at 0x10067558
    m_DeviceDestroying = TRUE;

    // Notify render manager that device is being destroyed
    if (m_RenderManager)
        m_RenderManager->DestroyingDevice(this);

    m_FFPipeline.Shutdown();

    if (m_RasterizerContext) {
        if (m_TargetFrameBuffer != 0)
            m_RasterizerContext->DeleteObject(m_TargetFrameBuffer, CKRST_OBJ_FRAMEBUFFER);
        if (m_TargetDepthTexture != 0)
            m_RasterizerContext->DeleteObject(m_TargetDepthTexture, CKRST_OBJ_TEXTURE);
        if (m_CopyToVideoTexture != 0)
            m_RasterizerContext->DeleteObject(m_CopyToVideoTexture, CKRST_OBJ_TEXTURE);
    }
    if (m_RenderManager) {
        if (m_TargetFrameBuffer != 0)
            m_RenderManager->ReleaseObjectIndex(m_TargetFrameBuffer, CKRST_OBJ_FRAMEBUFFER);
        if (m_TargetDepthTexture != 0)
            m_RenderManager->ReleaseObjectIndex(m_TargetDepthTexture, CKRST_OBJ_TEXTURE);
        if (m_CopyToVideoTexture != 0)
            m_RenderManager->ReleaseObjectIndex(m_CopyToVideoTexture, CKRST_OBJ_TEXTURE);
    }

    // Destroy the rasterizer context
    if (m_RasterizerDriver)
        m_RasterizerDriver->DestroyContext(m_RasterizerContext);

    m_RasterizerContext = nullptr;
    m_RasterizerDriver = nullptr;
    m_TargetTexture = nullptr;
    m_TargetFrameBuffer = 0;
    m_TargetDepthTexture = 0;
    m_CopyToVideoTexture = 0;
    m_CopyToVideoWidth = 0;
    m_CopyToVideoHeight = 0;
    m_DeviceDestroying = FALSE;
    m_Fullscreen = FALSE;

    return TRUE;
}

void RCKRenderContext::ClearCallbacks() {
    // Based on IDA at 0x1006d7a2
    RCKRenderManager *rm = (RCKRenderManager *) m_Context->GetRenderManager();
    if (rm) {
        rm->RemoveTemporaryCallback(&m_PreRenderCallBacks);
        rm->RemoveTemporaryCallback(&m_PostRenderCallBacks);
        rm->RemoveTemporaryCallback(&m_PostSpriteRenderCallBacks);
    }

    // Clear callback containers
    m_PreRenderCallBacks.Clear();
    m_PostRenderCallBacks.Clear();
    m_PostSpriteRenderCallBacks.Clear();
}

void RCKRenderContext::OnClearAll() {
    ClearCallbacks();
    DetachAll();
    m_NCUTex = nullptr;
    SetFullViewport(&m_ViewportData, m_Settings.m_Rect.right, m_Settings.m_Rect.bottom);
}

void RCKRenderContext::PreSave(CKFile *file, CKDWORD flags) {
    CKRenderContext::PreSave(file, flags);
}

CKStateChunk *RCKRenderContext::Save(CKFile *file, CKDWORD flags) {
    return CKRenderContext::Save(file, flags);
}

CKERROR RCKRenderContext::Load(CKStateChunk *chunk, CKFile *file) {
    return CKRenderContext::Load(chunk, file);
}

void RCKRenderContext::PostLoad() {
    CKRenderContext::PostLoad();
}

void RCKRenderContext::SetFullViewport(CKViewportData *vp, int width, int height) {
    if (!vp) return;
    vp->ViewX = 0;
    vp->ViewY = 0;
    vp->ViewWidth = width;
    vp->ViewHeight = height;
    if (vp == &m_ViewportData)
        m_FFPipeline.SetViewport(m_ViewportData);
}

void RCKRenderContext::SetClipRect(VxRect *rect) {
    // IDA: 0x1006c808
    if (!rect || !m_RasterizerContext)
        return;

    int left = (int) rect->left;
    int top = (int) rect->top;
    int right = (int) rect->right;
    int bottom = (int) rect->bottom;

    const int clipWidth = right - left;
    const int clipHeight = bottom - top;
    const int safeClipWidth = (clipWidth >= 1) ? clipWidth : 1;
    const int safeClipHeight = (clipHeight >= 1) ? clipHeight : 1;

    // Viewport clip rect applied per-view in v2
    (void)left; (void)top; (void)safeClipWidth; (void)safeClipHeight;

    const double invW = 1.0 / (double) safeClipWidth;
    const double invH = 1.0 / (double) safeClipHeight;
    const float dx = (float) (right + left - (m_ViewportData.ViewWidth + 2 * m_ViewportData.ViewX));
    const float dy = (float) (top + bottom - (m_ViewportData.ViewHeight + 2 * m_ViewportData.ViewY));

    VxMatrix clipProj;
    clipProj.Clear();

    clipProj[0][0] = (float) ((double) m_ViewportData.ViewWidth * (double) m_ProjectionMatrix[0][0] / (double) safeClipWidth);
    clipProj[1][1] = (float) ((double) m_ViewportData.ViewHeight * (double) m_ProjectionMatrix[1][1] / (double) safeClipHeight);
    clipProj[2][0] = (float) (-dx * invW);
    clipProj[2][1] = (float) (dy * invH);
    clipProj[2][2] = m_FarPlane / (m_FarPlane - m_NearPlane);
    clipProj[3][2] = -clipProj[2][2] * m_NearPlane;
    clipProj[2][3] = 1.0f;

    m_FFPipeline.SetTransform(VXMATRIX_PROJECTION, clipProj);
}

void RCKRenderContext::UpdateProjection(CKBOOL forceUpdate) {
    if (!forceUpdate && m_ProjectionUpdated)
        return;

    if (!m_RasterizerContext)
        return;

    const float aspect = (float) ((double) m_ViewportData.ViewWidth / (double) m_ViewportData.ViewHeight);

    if (m_Perspective)
        m_ProjectionMatrix.Perspective(m_Fov, aspect, m_NearPlane, m_FarPlane);
    else
        m_ProjectionMatrix.Orthographic(m_Zoom, aspect, m_NearPlane, m_FarPlane);

    m_FFPipeline.SetTransform(VXMATRIX_PROJECTION, m_ProjectionMatrix);
    m_ProjectionUpdated = TRUE;

    const float right = (float) m_Settings.m_Rect.right;
    const float bottom = (float) m_Settings.m_Rect.bottom;
    VxRect rect(0.0f, 0.0f, right, bottom);

    ((RCK2dEntity *) Get2dRoot(TRUE))->SetRect(rect);
    ((RCK2dEntity *) Get2dRoot(FALSE))->SetRect(rect);
}

void RCKRenderContext::FlushSprite3DBatchesIfNeeded() {
    // IDA: sub_1000D2F0 (0x1000D2F0)
    // Called before rendering entities to flush any pending sprite batches
    // if transparent sorting is enabled
    if (m_SortTransparentObjects) {
        if (m_Sprite3DBatches.Size() > 0) {
            CallSprite3DBatches();
        }
    }
}

void RCKRenderContext::AddSprite3DBatch(RCKSprite3D *sprite) {
    // IDA: 0x1006DBF9
    if (!sprite)
        return;

    RCKMaterial *material = static_cast<RCKMaterial *>(sprite->GetMaterial());
    if (!material)
        return;

    // Add sprite data to material's batch (returns TRUE if this is a new batch)
    if (material->AddSprite3DBatch(sprite)) {
        // If we already have pending batches and sorting is enabled, flush first
        if (m_Sprite3DBatches.Size() > 0 && m_SortTransparentObjects) {
            CallSprite3DBatches();
        }
        // Add material to the batch list
        m_Sprite3DBatches.PushBack(material);
    }
}

void RCKRenderContext::CallSprite3DBatches() {
    // IDA: 0x1006DC61
    if (m_Sprite3DBatches.Size() == 0) {
        return;
    }

    // Use original literals (do not define new constants)

    m_FFPipeline.SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    m_FFPipeline.SetRenderState(VXRENDERSTATE_WRAP0, FALSE);
    VxMatrix identity;
    Vx3DMatrixIdentity(identity);
    m_FFPipeline.SetTransform(VXMATRIX_WORLD, identity);

    VxDrawPrimitiveData dpData{};
    dpData.TexCoordStride = sizeof(CKVertex);
    dpData.SpecularColorStride = sizeof(CKVertex);
    dpData.ColorStride = sizeof(CKVertex);
    dpData.PositionStride = sizeof(CKVertex);

    const int batchCount = m_Sprite3DBatches.Size();
    for (int i = 0; i < batchCount; ++i) {
        RCKMaterial *material = m_Sprite3DBatches[i];
        if (!material) {
            continue;
        }

        CKSprite3DBatch *batch = material->m_Sprite3DBatch;
        if (batch) {
            CKDWORD colors[2];
            colors[0] = RGBAFTOCOLOR(&material->m_MaterialData.Diffuse);
            // IDA: sub_1001BA90 => RGBAFTOCOLOR(spec) | 0xFF000000
            colors[1] = RGBAFTOCOLOR(&material->m_SpecularColor) | 0xFF000000;

            const int spriteCount = batch->m_Vertices.Size() >> 2;
            if (spriteCount) {
                m_Stats.NbObjectDrawn += spriteCount;
                m_Stats.NbTrianglesDrawn += 2 * spriteCount;
                m_Stats.NbVerticesProcessed += 4 * spriteCount;

                material->SetAsCurrent(this, FALSE, 0);
                m_FFPipeline.SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
                m_FFPipeline.SetRenderState(VXRENDERSTATE_WRAP0, FALSE);

                CKVertex *vertices = batch->m_Vertices.Begin();
                VxFillStructure(4 * spriteCount, &vertices->Diffuse, sizeof(CKVertex), 8, colors);
                batch->m_VertexCount = 4 * spriteCount;

                const int indexCount = 6 * spriteCount;
                batch->m_Indices.Resize(indexCount);

                CKWORD *indices = batch->m_Indices.Begin();
                if (indexCount > (int) batch->m_IndexCount) {
                    int v = 0;
                    for (int j = 0; j < spriteCount; ++j) {
                        indices[0] = (CKWORD) v;
                        indices[1] = (CKWORD) (v + 1);
                        indices[2] = (CKWORD) (v + 2);
                        indices[3] = (CKWORD) v;
                        indices[4] = (CKWORD) (v + 2);
                        indices[5] = (CKWORD) (v + 3);
                        v += 4;
                        indices += 6;
                    }
                    batch->m_IndexCount = indexCount;
                    indices = batch->m_Indices.Begin();
                }

                dpData.Flags = CKRST_DP_TR_VCST;
                if (batch->m_Flags) {
                    dpData.Flags |= CKRST_DP_DOCLIP;
                }
                dpData.VertexCount = 4 * spriteCount;
                dpData.PositionPtr = vertices;
                dpData.ColorPtr = &vertices->Diffuse;
                dpData.SpecularColorPtr = &vertices->Specular;
                dpData.TexCoordPtr = &vertices->tu;

                const CKRenderView saved3DView = m_Current3DView;
                if (material->IsAlphaTransparent() || material->AlphaBlendEnabled() ||
                    m_FFPipeline.GetRenderState(VXRENDERSTATE_ALPHABLENDENABLE))
                    m_Current3DView = CKRP_VIEW_TRANSPARENT;
                DrawPrimitive(VX_TRIANGLELIST, indices, indexCount, &dpData);
                m_Current3DView = saved3DView;
            }
        }

        material->FlushSprite3DBatch();
    }

    m_Sprite3DBatches.Resize(0);
}

void RCKRenderContext::CheckObjectExtents() {
    // IDA: 0x1006d81f
    for (CKObjectExtents *it = m_ObjectExtents.Begin(); it != m_ObjectExtents.End(); ++it) {
        CK3dEntity *entity = it->m_Entity;
        if (entity) {
            if (entity->IsToBeDeleted())
                it->m_Entity = nullptr;
        }
    }
}

int RCKRenderContext::ClassifyTransparentOrder(const RCK3dEntity *a, const RCK3dEntity *b, const VxVector &cam) {
    // IDA: sub_10009BB9
    const VxBbox &localBox = a->m_LocalBoundingBox;

    const float dz = localBox.Max.z - localBox.Min.z;
    if (dz < EPSILON) {
        const VxPlane plane(a->m_WorldMatrix[2], a->m_WorldMatrix[3]);
        const float prod = DotProduct(plane.m_Normal, cam) * plane.Classify(b->m_WorldBoundingBox);
        if (prod != 0.0f)
            return (prod >= 0.0f) ? 1 : -1;
        return a->m_WorldBoundingBox.Classify(b->m_WorldBoundingBox, cam);
    }

    const float dy = localBox.Max.y - localBox.Min.y;
    if (dy >= EPSILON) {
        const float dx = localBox.Max.x - localBox.Min.x;
        if (dx >= EPSILON)
            return a->m_WorldBoundingBox.Classify(b->m_WorldBoundingBox, cam);

        const VxPlane plane(a->m_WorldMatrix[0], a->m_WorldMatrix[3]);
        const float prod = DotProduct(plane.m_Normal, cam) * plane.Classify(b->m_WorldBoundingBox);
        if (prod == 0.0f)
            return a->m_WorldBoundingBox.Classify(b->m_WorldBoundingBox, cam);
        return (prod >= 0.0f) ? 1 : -1;
    }

    const VxPlane plane(a->m_WorldMatrix[1], a->m_WorldMatrix[3]);
    const float prod = DotProduct(plane.m_Normal, cam) * plane.Classify(b->m_WorldBoundingBox);
    if (prod == 0.0f)
        return a->m_WorldBoundingBox.Classify(b->m_WorldBoundingBox, cam);
    return (prod >= 0.0f) ? 1 : -1;
}

void RCKRenderContext::RenderTransparents(CKDWORD flags) {
    // IDA: 0x1006d070
    const int count = m_TransparentObjects.Size();
    if (count <= 0)
        return;

    if (m_RenderManager && m_RenderManager->m_SortTransparentObjects.Value) {
        if (count == 1) {
            RCK3dEntity *entity = m_TransparentObjects[0];
            if (entity)
                entity->Render((CKRenderContext *) this, flags);
            return;
        }

        m_TransparentObjectsSortTimeProfiler.Reset();

        VxMatrix viewProj;
        Vx3DMultiplyMatrix(viewProj, m_ViewMatrix, m_ProjectionMatrix);

        struct TransparentItem {
            RCK3dEntity *entity;
            float zhMin;
            float zhMax;
        };

        XArray<TransparentItem> items;
        items.Resize(count);

        for (int i = 0; i < count; ++i) {
            RCK3dEntity *entity = m_TransparentObjects[i];
            items[i].entity = entity;
            items[i].zhMin = 0.0f;
            items[i].zhMax = 0.0f;

            if (!entity)
                continue;

            if (CKIsChildClassOf(entity, CKCID_SPRITE3D)) {
                entity->m_MoveableFlags &= ~VX_MOVEABLE_UPTODATE;
                entity->UpdateBox(TRUE);
            }

            VxMatrix mvp;
            Vx3DMultiplyMatrix4(mvp, viewProj, entity->m_WorldMatrix);
            const VxBbox &bbox = entity->GetBoundingBox(FALSE);
            VxProjectBoxZExtents(mvp, bbox, items[i].zhMin, items[i].zhMax);
        }

        // IDA: uses root entity world position for tie-breaking.
        VxVector cameraPos(0.0f);
        const CK3dEntity *rootEntity = m_RenderedScene ? m_RenderedScene->GetRootEntity() : nullptr;
        if (rootEntity) {
            const VxMatrix &rootWorld = rootEntity->GetWorldMatrix();
            cameraPos = static_cast<VxVector>(rootWorld[3]);
        }

        CKBOOL noSwaps = TRUE;
        for (int i = 1; i < count; ++i) {
            for (int k = count - 1; k >= i; --k) {
                TransparentItem &curr = items[k];
                TransparentItem &prev = items[k - 1];

                if (!curr.entity || !prev.entity)
                    continue;

                const CKSceneGraphNode *currNode = curr.entity ? curr.entity->m_SceneGraphNode : nullptr;
                const CKSceneGraphNode *prevNode = prev.entity ? prev.entity->m_SceneGraphNode : nullptr;

                const CKWORD currPri = currNode ? (CKWORD) currNode->m_MaxPriority : 0;
                const CKWORD prevPri = prevNode ? (CKWORD) prevNode->m_MaxPriority : 0;

                if (currPri > prevPri) {
                    TransparentItem tmp = curr;
                    curr = prev;
                    prev = tmp;
                    noSwaps = FALSE;
                    continue;
                }

                if (currPri == prevPri) {
                    // IDA-aligned behavior (same as CKSceneGraphRootNode::SortTransparentObjects):
                    // - If projected Z ranges do NOT overlap, swap directly.
                    // - If they overlap, use the expensive tie-breaker, then a final EPSILON compare.
                    if (prev.zhMin < curr.zhMax) {
                        if (!(curr.zhMin <= prev.zhMax)) {
                            TransparentItem tmp = curr;
                            curr = prev;
                            prev = tmp;
                            noSwaps = FALSE;
                            continue;
                        }

                        const int cmp1 = ClassifyTransparentOrder(prev.entity, curr.entity, cameraPos);
                        if (cmp1 < 0) {
                            TransparentItem tmp = curr;
                            curr = prev;
                            prev = tmp;
                            noSwaps = FALSE;
                            continue;
                        }
                        if (cmp1 > 0)
                            continue;

                        const int cmp2 = ClassifyTransparentOrder(curr.entity, prev.entity, cameraPos);
                        if (cmp2 < 0)
                            continue;
                        if (cmp2 > 0) {
                            TransparentItem tmp = curr;
                            curr = prev;
                            prev = tmp;
                            noSwaps = FALSE;
                            continue;
                        }

                        if (prev.zhMax + EPSILON < curr.zhMax) {
                            TransparentItem tmp = curr;
                            curr = prev;
                            prev = tmp;
                            noSwaps = FALSE;
                        }
                    }
                }
            }

            if (noSwaps)
                break;
            noSwaps = TRUE;
        }

        m_Stats.TransparentObjectsSortTime = m_TransparentObjectsSortTimeProfiler.Current();

        for (int i = 0; i < count; ++i) {
            if (items[i].entity)
                items[i].entity->Render((CKRenderContext *) this, flags);
        }
    } else {
        for (int i = 0; i < count; ++i) {
            RCK3dEntity *entity = m_TransparentObjects[i];
            if (entity)
                entity->Render((CKRenderContext *) this, flags);
        }
    }
}

void RCKRenderContext::AddExtents2D(const VxRect &rect, CKObject *obj) {
    if (obj) {
        // Add to object extents list
        CKObjectExtents extents;
        extents.m_Rect = rect;
        extents.m_Entity = (CK3dEntity *) obj;
        extents.m_Camera = 0;
        m_ObjectExtents.PushBack(extents);
    } else {
        // Merge with current extents (no associated object)
        if (rect.left < m_CurrentExtents.left)
            m_CurrentExtents.left = rect.left;
        if (rect.top < m_CurrentExtents.top)
            m_CurrentExtents.top = rect.top;
        if (rect.right > m_CurrentExtents.right)
            m_CurrentExtents.right = rect.right;
        if (rect.bottom > m_CurrentExtents.bottom)
            m_CurrentExtents.bottom = rect.bottom;
    }
}

//=============================================================================
// Static Class Registration Methods
//=============================================================================

CKObject *RCKRenderContext::CreateInstance(CKContext *Context) {
    return new RCKRenderContext(Context, nullptr);
}

CKSTRING RCKRenderContext::GetClassName() {
    return "Render Context";
}

int RCKRenderContext::GetDependenciesCount(int mode) {
    return 0;
}

CKSTRING RCKRenderContext::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKRenderContext::Register() {
    CKCLASSNOTIFYFROMCID(RCKRenderContext, CKCID_RENDEROBJECT);
}

void RCKRenderContext::PrepareCameras(CK_RENDER_FLAGS Flags) {
    if (!(Flags & CK_RENDER_OPTIONSMASK))
        Flags = static_cast<CK_RENDER_FLAGS>(m_RenderFlags);

    m_RenderedScene->PrepareCameras(Flags);
}

// ============================================================
// UserDrawPrimitiveDataClass implementation
// IDA: 0x1006e27e - GetStructure
// IDA: 0x1006e0f7 - ClearStructure
// IDA: 0x1006e18a - AllocateStructure
// ============================================================

UserDrawPrimitiveDataClass::UserDrawPrimitiveDataClass()
    : VxDrawPrimitiveData(),
      m_Indices(nullptr),
      m_MaxIndexCount(0),
      m_MaxVertexCount(0) {
    memset((VxDrawPrimitiveData *) this, 0, sizeof(VxDrawPrimitiveData));
    memset(m_CachedDP, 0, sizeof(m_CachedDP));
}

UserDrawPrimitiveDataClass::~UserDrawPrimitiveDataClass() {
    delete[] m_Indices;
    m_Indices = nullptr;
    m_MaxIndexCount = 0;
    m_MaxVertexCount = 0;
    ClearStructure();
}

VxDrawPrimitiveData *UserDrawPrimitiveDataClass::GetStructure(CKRST_DPFLAGS DpFlags, int VertexCount) {
    // IDA: 0x1006e27e
    // If requested vertex count is larger than current allocation, reallocate
    if (VertexCount > m_MaxVertexCount) {
        m_MaxVertexCount = VertexCount;
        ClearStructure();
        AllocateStructure();
    }

    memcpy(m_CachedDP, (VxDrawPrimitiveData *) this, sizeof(VxDrawPrimitiveData));
    VxDrawPrimitiveData *cached = (VxDrawPrimitiveData *) m_CachedDP;

    cached->VertexCount = VertexCount;
    cached->Flags = DpFlags;

    // Match original behavior: null out optional pointers based on flags.
    if (!(DpFlags & CKRST_DP_SPECULAR))
        cached->SpecularColorPtr = nullptr;
    if (!(DpFlags & CKRST_DP_DIFFUSE))
        cached->ColorPtr = nullptr;
    if (!(DpFlags & CKRST_DP_LIGHT))
        cached->NormalPtr = nullptr;

    if ((DpFlags & CKRST_DP_SPECULAR) && cached->SpecularColorPtr) {
        const bool positionT = (DpFlags & CKRST_DP_TRANSFORM) == 0;
        CKDWORD defaultSpecular = positionT ? 0xFF000000 : 0x00000000;
        VxFillStructure(VertexCount,
                        cached->SpecularColorPtr,
                        cached->SpecularColorStride,
                        sizeof(CKDWORD),
                        &defaultSpecular);
    }

    return cached;
}

void UserDrawPrimitiveDataClass::ClearStructure() {
    // IDA: 0x1006e0f7
    VxDeleteAligned(PositionPtr);
    VxDeleteAligned(NormalPtr);
    VxDeleteAligned(ColorPtr);
    VxDeleteAligned(SpecularColorPtr);
    VxDeleteAligned(TexCoordPtr);

    for (int i = 0; i < 7; ++i) {
        VxDeleteAligned(TexCoordPtrs[i]);
    }

    // Re-initialize the base structure
    memset((VxDrawPrimitiveData *) this, 0, sizeof(VxDrawPrimitiveData));
}

void UserDrawPrimitiveDataClass::AllocateStructure() {
    // IDA: 0x1006e18a
    const int maxVertices = m_MaxVertexCount;

    // Strides must be valid; many callers rely on them.
    PositionStride = sizeof(VxVector4);
    NormalStride = sizeof(VxVector);
    ColorStride = sizeof(CKDWORD);
    SpecularColorStride = sizeof(CKDWORD);
    TexCoordStride = sizeof(Vx2DVector);
    for (int i = 0; i < (CKRST_MAX_STAGES - 1); ++i)
        TexCoordStrides[i] = 8;

    ColorPtr = VxNewAligned(sizeof(CKDWORD) * maxVertices, 16);         // DWORD per vertex
    SpecularColorPtr = VxNewAligned(sizeof(CKDWORD) * maxVertices, 16); // DWORD per vertex
    NormalPtr = VxNewAligned(sizeof(VxVector) * maxVertices, 16);       // VxVector (12 bytes) per vertex
    PositionPtr = VxNewAligned(sizeof(VxVector4) * maxVertices, 16);     // VxVector4 (16 bytes) per vertex
    TexCoordPtr = VxNewAligned(sizeof(Vx2DVector) * maxVertices, 16);      // Vx2DVector (8 bytes) per vertex

    for (int i = 0; i < 7; ++i) {
        TexCoordPtrs[i] = VxNewAligned(8 * maxVertices, 16); // Vx2DVector per vertex
    }

    memcpy(m_CachedDP, (VxDrawPrimitiveData *) this, sizeof(VxDrawPrimitiveData));
}

CKWORD *UserDrawPrimitiveDataClass::GetIndices(int IndicesCount) {
    // IDA: 0x1006e332
    if (IndicesCount > m_MaxIndexCount) {
        delete[] m_Indices;
        m_Indices = new CKWORD[IndicesCount];
        m_MaxIndexCount = IndicesCount;
    }
    return m_Indices;
}

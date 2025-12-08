#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "RCKRenderObject.h"
#include "CKRenderManager.h"
#include "CKRasterizer.h"
#include "CKRasterizerTypes.h"
#include "CK3dEntity.h"
#include "CKCamera.h"
#include "CKLight.h"
#include "CKMaterial.h"
#include "CKTexture.h"
#include "CKSprite.h"
#include "CK2dEntity.h"
#include "VxMath.h"
#include "VxIntersect.h"
#include "CKDebugLogger.h"
#include <algorithm>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// Debug logging macros
#define RC_DEBUG_LOG(msg) CK_LOG("RenderContext", msg)
#define RC_DEBUG_LOG_FMT(fmt, ...) CK_LOG_FMT("RenderContext", fmt, __VA_ARGS__)

CK_CLASSID RCKRenderContext::GetClassID() {
    return CKObject::GetClassID();
}

void RCKRenderContext::PreDelete() {
    CKObject::PreDelete();
}

void RCKRenderContext::CheckPreDeletion() {
    CKObject::CheckPreDeletion();
}

void RCKRenderContext::CheckPostDeletion() {
    CKObject::CheckPostDeletion();
}

int RCKRenderContext::GetMemoryOccupation() {
    return CKObject::GetMemoryOccupation();
}

CKBOOL RCKRenderContext::IsObjectUsed(CKObject *obj, CK_CLASSID cid) {
    return CKObject::IsObjectUsed(obj, cid);
}

CKERROR RCKRenderContext::PrepareDependencies(CKDependenciesContext &context) {
    return CKObject::PrepareDependencies(context);
}

void RCKRenderContext::AddObject(CKRenderObject *obj) {
    RC_DEBUG_LOG_FMT("AddObject called obj=%p", obj);
    if (!obj) return;

    // Based on original implementation at 0x10067c2a:
    // Check if object is a root object and not already in this render context
    if (obj->IsRootObject() && !obj->IsInRenderContext(this)) {
        RC_DEBUG_LOG_FMT("AddObject: Adding root object %p", obj);
        // Set render context mask on the object
        ((RCKRenderObject *)obj)->AddToRenderContext(this);
        // Add to the rendered scene's entity arrays
        if (m_RenderedScene) {
            m_RenderedScene->AddObject(obj);
        }
    }
}

void RCKRenderContext::AddObjectWithHierarchy(CKRenderObject *obj) {
    if (!obj) return;
    AddObject(obj);

    if (CKIsChildClassOf(obj, CKCID_3DENTITY)) {
        CK3dEntity *ent = (CK3dEntity *) obj;
        int count = ent->GetChildrenCount();
        for (int i = 0; i < count; ++i) {
            CK3dEntity *child = ent->GetChild(i);
            if (child)
                AddObjectWithHierarchy(child);
        }
    } else if (CKIsChildClassOf(obj, CKCID_2DENTITY)) {
        CK2dEntity *ent = (CK2dEntity *) obj;
        int count = ent->GetChildrenCount();
        for (int i = 0; i < count; ++i) {
            CK2dEntity *child = ent->GetChild(i);
            if (child)
                AddObjectWithHierarchy(child);
        }
    }
}

void RCKRenderContext::RemoveObject(CKRenderObject *obj) {
    if (!obj) return;
    if (CKIsChildClassOf(obj, CKCID_3DENTITY)) {
        m_3dRootObjects.RemoveObject(obj);
    } else if (CKIsChildClassOf(obj, CKCID_2DENTITY)) {
        m_2dRootObjects.RemoveObject(obj);
    }
}

CKBOOL RCKRenderContext::IsObjectAttached(CKRenderObject *obj) {
    if (!obj) return FALSE;
    if (CKIsChildClassOf(obj, CKCID_3DENTITY)) {
        return m_3dRootObjects.FindObject(obj);
    } else if (CKIsChildClassOf(obj, CKCID_2DENTITY)) {
        return m_2dRootObjects.FindObject(obj);
    }
    return FALSE;
}

const XObjectArray &RCKRenderContext::Compute3dRootObjects() {
    if (m_Context)
        m_3dRootObjects.Check(m_Context);
    return m_3dRootObjects;
}

const XObjectArray &RCKRenderContext::Compute2dRootObjects() {
    if (m_Context)
        m_2dRootObjects.Check(m_Context);
    return m_2dRootObjects;
}

CK2dEntity *RCKRenderContext::Get2dRoot(CKBOOL background) {
    // Return the global 2D roots from RenderManager, not per-context roots
    if (!m_RenderManager) {
        RC_DEBUG_LOG_FMT("Get2dRoot(%d) failed: m_RenderManager is null (this=%p, ctx=%p)",
                         background, this, m_Context);
        return NULL;
    }

    CK2dEntity *root = background ? m_RenderManager->m_2DRootBack : m_RenderManager->m_2DRootFore;
    if (!root) {
        RC_DEBUG_LOG_FMT("Get2dRoot(%d) returned null (mgr=%p fore=%p back=%p)",
                         background, m_RenderManager, m_RenderManager->m_2DRootFore,
                         m_RenderManager->m_2DRootBack);
    }
    return root;
}

void RCKRenderContext::DetachAll() {
    RC_DEBUG_LOG("DetachAll called");
    m_3dRootObjects.Clear();
    m_2dRootObjects.Clear();
}

void RCKRenderContext::ForceCameraSettingsUpdate() {
    RC_DEBUG_LOG("ForceCameraSettingsUpdate called");
}

CK_RENDER_FLAGS RCKRenderContext::ResolveRenderFlags(CK_RENDER_FLAGS Flags) const {
    return (Flags == CK_RENDER_USECURRENTSETTINGS) ? static_cast<CK_RENDER_FLAGS>(m_RenderFlags) : Flags;
}

void RCKRenderContext::ExecutePreRenderCallbacks() {
    m_PreRenderCallBacks.ExecutePreCallbacks(this, TRUE);
    m_PreRenderTempCallBacks.ExecutePreCallbacks(this, TRUE);
}

void RCKRenderContext::ExecutePostRenderCallbacks(CKBOOL beforeTransparent) {
    m_PostRenderCallBacks.ExecutePostCallbacks(this, TRUE, beforeTransparent);
}

void RCKRenderContext::ExecutePostSpriteCallbacks() {
    m_PostSpriteRenderCallBacks.ExecutePostCallbacks(this, TRUE, FALSE);
}

CKERROR RCKRenderContext::Clear(CK_RENDER_FLAGS Flags, CKDWORD Stencil) {
    RC_DEBUG_LOG_FMT("Clear: Flags=0x%X", Flags);
    if (!m_RasterizerContext)
        return CKERR_INVALIDRENDERCONTEXT;

    CK_RENDER_FLAGS effectiveFlags = ResolveRenderFlags(Flags);
    RC_DEBUG_LOG_FMT("Clear: effectiveFlags=0x%X", effectiveFlags);

    CKDWORD clearMask = 0;
    if (effectiveFlags & CK_RENDER_CLEARBACK)
        clearMask |= CKRST_CTXCLEAR_COLOR;
    if (effectiveFlags & CK_RENDER_CLEARZ)
        clearMask |= CKRST_CTXCLEAR_DEPTH;
    if (effectiveFlags & CK_RENDER_CLEARSTENCIL)
        clearMask |= CKRST_CTXCLEAR_STENCIL;
    if (effectiveFlags & CK_RENDER_CLEARVIEWPORT)
        clearMask |= CKRST_CTXCLEAR_VIEWPORT;

    RC_DEBUG_LOG_FMT("Clear: clearMask=0x%X", clearMask);

    if (!clearMask)
        return CK_OK;

    CKDWORD clearColor = 0;
    if ((clearMask & CKRST_CTXCLEAR_COLOR) != 0) {
        CKMaterial *background = GetBackgroundMaterial();
        if (background)
            clearColor = background->GetDiffuse().GetRGBA();
        else
            clearColor = 0xFF0000FF; // Blue if no background material
    }
    
    RC_DEBUG_LOG_FMT("Clear: clearColor=0x%08X", clearColor);

    return m_RasterizerContext->Clear(clearMask, clearColor, 1.0f, Stencil, 0, NULL)
               ? CK_OK
               : CKERR_INVALIDRENDERCONTEXT;
}

CKERROR RCKRenderContext::DrawScene(CK_RENDER_FLAGS Flags) {
    if (!m_RasterizerContext)
        return CKERR_INVALIDRENDERCONTEXT;

    CK_RENDER_FLAGS effectiveFlags = ResolveRenderFlags(Flags);
    if ((effectiveFlags & CK_RENDER_SKIPDRAWSCENE) != 0)
        return CK_OK;

    ++m_DrawSceneCalls;
    memset(&m_Stats, 0, sizeof(VxStats));
    m_Stats.SmoothedFps = m_SmoothedFps;
    m_RasterizerContext->m_RenderStateCacheHit = 0;
    m_RasterizerContext->m_RenderStateCacheMiss = 0;

    if ((effectiveFlags & CK_RENDER_DONOTUPDATEEXTENTS) == 0) {
        m_ObjectExtents.Resize(0);
    }

    // Ensure camera parameters are ready before rendering
    PrepareCameras(effectiveFlags);

    m_RasterizerContext->BeginScene();
    CKERROR err = m_RenderedScene ? m_RenderedScene->Draw(effectiveFlags) : CKERR_INVALIDRENDERCONTEXT;
    m_RasterizerContext->EndScene();

    m_Stats.RenderStateCacheHit = m_RasterizerContext->m_RenderStateCacheHit;
    m_Stats.RenderStateCacheMiss = m_RasterizerContext->m_RenderStateCacheMiss;
    --m_DrawSceneCalls;

    return err;
}

CKERROR RCKRenderContext::BackToFront(CK_RENDER_FLAGS Flags) {
    RC_DEBUG_LOG_FMT("BackToFront: Flags=0x%X", Flags);
    if (!m_RasterizerContext)
        return CKERR_INVALIDRENDERCONTEXT;

    CK_RENDER_FLAGS effectiveFlags = ResolveRenderFlags(Flags);
    RC_DEBUG_LOG_FMT("BackToFront: effectiveFlags=0x%X", effectiveFlags);
    if ((effectiveFlags & CK_RENDER_DOBACKTOFRONT) == 0) {
        RC_DEBUG_LOG("BackToFront: DOBACKTOFRONT not set, skipping");
        return CK_OK;
    }

    CKBOOL waitVbl = (effectiveFlags & CK_RENDER_WAITVBL) != 0;
    RC_DEBUG_LOG_FMT("BackToFront: Calling rasterizer->BackToFront(waitVbl=%d)", waitVbl);
    CKBOOL result = m_RasterizerContext->BackToFront(waitVbl);
    RC_DEBUG_LOG_FMT("BackToFront: result=%d", result);
    return result ? CK_OK : CKERR_INVALIDRENDERCONTEXT;
}

CKERROR RCKRenderContext::Render(CK_RENDER_FLAGS Flags) {
    if (!m_RasterizerContext) {
        RC_DEBUG_LOG("Render failed: no rasterizer context");
        return CKERR_INVALIDRENDERCONTEXT;
    }

    CK_RENDER_FLAGS effectiveFlags = ResolveRenderFlags(Flags);

    ExecutePreRenderCallbacks();

    CKERROR err = Clear(effectiveFlags);
    if (err != CK_OK) {
        RC_DEBUG_LOG_FMT("Clear failed with error %d", err);
        return err;
    }

    ExecutePostRenderCallbacks(TRUE);

    err = DrawScene(effectiveFlags);
    if (err != CK_OK) {
        RC_DEBUG_LOG_FMT("DrawScene failed with error %d", err);
        return err;
    }

    ExecutePostRenderCallbacks(FALSE);
    ExecutePostSpriteCallbacks();

    return BackToFront(effectiveFlags);
}

void RCKRenderContext::AddPreRenderCallBack(CK_RENDERCALLBACK Function, void *Argument, CKBOOL Temporary) {
    m_PreRenderCallBacks.AddPreCallback((void *) Function, Argument, Temporary, m_RenderManager);
}

void RCKRenderContext::RemovePreRenderCallBack(CK_RENDERCALLBACK Function, void *Argument) {
    m_PreRenderCallBacks.RemovePreCallback((void *) Function, Argument);
}

void RCKRenderContext::AddPostRenderCallBack(CK_RENDERCALLBACK Function, void *Argument, CKBOOL Temporary,
                                             CKBOOL BeforeTransparent) {
    m_PostRenderCallBacks.AddPostCallback((void *) Function, Argument, Temporary, m_RenderManager, BeforeTransparent);
}

void RCKRenderContext::RemovePostRenderCallBack(CK_RENDERCALLBACK Function, void *Argument) {
    m_PostRenderCallBacks.RemovePostCallback((void *) Function, Argument);
}

void RCKRenderContext::AddPostSpriteRenderCallBack(CK_RENDERCALLBACK Function, void *Argument, CKBOOL Temporary) {
    m_PostSpriteRenderCallBacks.AddPostCallback((void *) Function, Argument, Temporary, m_RenderManager, FALSE);
}

void RCKRenderContext::RemovePostSpriteRenderCallBack(CK_RENDERCALLBACK Function, void *Argument) {
    m_PostSpriteRenderCallBacks.RemovePostCallback((void *) Function, Argument);
}

VxDrawPrimitiveData *RCKRenderContext::GetDrawPrimitiveStructure(CKRST_DPFLAGS Flags, int VertexCount) {
    return m_UserDrawPrimitiveData;
}

CKWORD *RCKRenderContext::GetDrawPrimitiveIndices(int IndicesCount) {
    return m_UserDrawPrimitiveData ? m_UserDrawPrimitiveData->Indices : nullptr;
}

void RCKRenderContext::Transform(VxVector *Dest, VxVector *Src, CK3dEntity *Ref) {
    VxMatrix mat;
    if (Ref) {
        mat = Ref->GetWorldMatrix();
    } else {
        mat = VxMatrix::Identity();
    }

    const VxMatrix &view = GetViewTransformationMatrix();
    const VxMatrix &proj = GetProjectionTransformationMatrix();

    VxMatrix viewProj;
    Vx3DMultiplyMatrix(viewProj, view, proj);

    VxMatrix worldViewProj;
    Vx3DMultiplyMatrix(worldViewProj, mat, viewProj);

    VxVector4 vIn(Src->x, Src->y, Src->z, 1.0f);
    VxVector4 vRes;
    Vx3DMultiplyMatrixVector4(&vRes, worldViewProj, &vIn);

    if (vRes.w != 0.0f) {
        float invW = 1.0f / vRes.w;
        vRes.x *= invW;
        vRes.y *= invW;
        vRes.z *= invW;
    }

    vRes.x = (vRes.x + 1.0f) * (m_ViewportData.ViewWidth * 0.5f) + m_ViewportData.ViewX;
    vRes.y = (1.0f - vRes.y) * (m_ViewportData.ViewHeight * 0.5f) + m_ViewportData.ViewY;

    *Dest = vRes;
}

CKERROR RCKRenderContext::GoFullScreen(int Width, int Height, int Bpp, int Driver, int RefreshRate) {
    if (m_Fullscreen && m_Bpp == Bpp && GetWidth() == Width && GetHeight() == Height && m_Driver == Driver)
        return CK_OK;

    if (!m_RasterizerContext) return CKERR_INVALIDRENDERCONTEXT;

    if (Driver != m_Driver) {
        return ChangeDriver(Driver) ? CK_OK : CKERR_INVALIDPARAMETER;
    }

    if (m_RasterizerContext->Create((WIN_HANDLE) m_WinHandle, 0, 0, Width, Height, Bpp, TRUE, RefreshRate, -1, -1)) {
        m_Fullscreen = TRUE;
        m_Bpp = Bpp;
        m_WinRect.right = m_WinRect.left + Width;
        m_WinRect.bottom = m_WinRect.top + Height;
        m_WindowRect = m_WinRect;
        return CK_OK;
    }

    return CKERR_INVALIDPARAMETER;
}

CKERROR RCKRenderContext::StopFullScreen() {
    if (!m_Fullscreen) return CK_OK;

    if (!m_RasterizerContext) return CKERR_INVALIDRENDERCONTEXT;

    int width = m_RenderContextSettings.m_Rect.right - m_RenderContextSettings.m_Rect.left;
    int height = m_RenderContextSettings.m_Rect.bottom - m_RenderContextSettings.m_Rect.top;

    if (m_RasterizerContext->Create((WIN_HANDLE) m_WinHandle, m_RenderContextSettings.m_Rect.left,
                                    m_RenderContextSettings.m_Rect.top, width, height, -1, FALSE, 0, -1, -1)) {
        m_Fullscreen = FALSE;
        m_WinRect = m_RenderContextSettings.m_Rect;
        m_WindowRect = m_WinRect;
        return CK_OK;
    }

    return CKERR_INVALIDPARAMETER;
}

CKBOOL RCKRenderContext::IsFullScreen() {
    return m_Fullscreen;
}

int RCKRenderContext::GetDriverIndex() {
    RC_DEBUG_LOG_FMT("GetDriverIndex called, returning %d", m_Driver);
    return m_Driver;
}

CKBOOL RCKRenderContext::ChangeDriver(int NewDriver) {
    RC_DEBUG_LOG_FMT("ChangeDriver called, NewDriver=%d", NewDriver);
    if (NewDriver == m_Driver) return TRUE;
    if (!m_RenderManager) return FALSE;
    if (NewDriver < 0 || NewDriver >= m_RenderManager->m_DriverCount) return FALSE;

    CKBOOL wasFullscreen = m_Fullscreen;
    CKRECT winRect = m_WinRect;
    int bpp = m_Bpp;
    int zbpp = m_Zbpp;
    int stencilBpp = m_StencilBpp;
    int refreshRate = m_RasterizerContext ? m_RasterizerContext->m_RefreshRate : 0;

    // Save fog settings before destroying old context
    VXFOG_MODE fogMode = GetFogMode();
    CKDWORD fogColor = GetFogColor();
    float fogStart = GetFogStart();
    float fogEnd = GetFogEnd();
    float fogDensity = GetFogDensity();

    if (m_RasterizerContext && m_RasterizerDriver) {
        m_RasterizerDriver->DestroyContext(m_RasterizerContext);
        m_RasterizerContext = nullptr;
    }

    m_Driver = NewDriver;
    VxDriverDescEx *drvDesc = &m_RenderManager->m_Drivers[m_Driver];
    m_RasterizerDriver = drvDesc->RasterizerDriver;

    if (!m_RasterizerDriver) return FALSE;

    m_RasterizerContext = m_RasterizerDriver->CreateContext();
    if (!m_RasterizerContext) return FALSE;

    int width = winRect.right - winRect.left;
    int height = winRect.bottom - winRect.top;

    if (!m_RasterizerContext->Create((WIN_HANDLE) m_WinHandle, winRect.left, winRect.top, width, height, bpp,
                                     wasFullscreen, refreshRate, zbpp, stencilBpp)) {
        m_RasterizerDriver->DestroyContext(m_RasterizerContext);
        m_RasterizerContext = nullptr;
        return FALSE;
    }

    m_RenderContextSettings.m_Rect = m_WinRect;
    m_RenderContextSettings.m_Bpp = bpp;
    m_RenderContextSettings.m_Zbpp = zbpp;
    m_RenderContextSettings.m_StencilBpp = stencilBpp;

    VxRect viewRect;
    GetViewRect(viewRect);
    SetViewRect(viewRect);

    // Restore render states
    SetGlobalRenderMode((VxShadeType) m_Shading, m_TextureEnabled, m_DisplayWireframe);
    SetAmbientLight(GetAmbientLight());

    // Restore fog settings
    SetFogMode(fogMode);
    SetFogColor(fogColor);
    SetFogStart(fogStart);
    SetFogEnd(fogEnd);
    SetFogDensity(fogDensity);

    return TRUE;
}

WIN_HANDLE RCKRenderContext::GetWindowHandle() {
    RC_DEBUG_LOG_FMT("GetWindowHandle called, returning %08X", m_WinHandle);
    return (WIN_HANDLE) m_WinHandle;
}

void RCKRenderContext::ScreenToClient(Vx2DVector *ioPoint) {
    RC_DEBUG_LOG("ScreenToClient called");
    POINT pt;
    pt.x = (LONG) ioPoint->x;
    pt.y = (LONG) ioPoint->y;
    ::ScreenToClient((HWND) m_WinHandle, &pt);
    ioPoint->x = (float) pt.x;
    ioPoint->y = (float) pt.y;
}

void RCKRenderContext::ClientToScreen(Vx2DVector *ioPoint) {
    POINT pt;
    pt.x = (LONG) ioPoint->x;
    pt.y = (LONG) ioPoint->y;
    ::ClientToScreen((HWND) m_WinHandle, &pt);
    ioPoint->x = (float) pt.x;
    ioPoint->y = (float) pt.y;
}

CKERROR RCKRenderContext::SetWindowRect(VxRect &rect, CKDWORD Flags) {
    return Resize((int) rect.left, (int) rect.top, (int) rect.GetWidth(), (int) rect.GetHeight(), Flags);
}

void RCKRenderContext::GetWindowRect(VxRect &rect, CKBOOL ScreenRelative) {
    rect.left = (float) m_WindowRect.left;
    rect.top = (float) m_WindowRect.top;
    rect.right = (float) m_WindowRect.right;
    rect.bottom = (float) m_WindowRect.bottom;
}

int RCKRenderContext::GetHeight() {
    RC_DEBUG_LOG("GetHeight called");
    return m_WinRect.bottom - m_WinRect.top;
}

int RCKRenderContext::GetWidth() {
    RC_DEBUG_LOG("GetWidth called");
    return m_WinRect.right - m_WinRect.left;
}

CKERROR RCKRenderContext::Resize(int PosX, int PosY, int SizeX, int SizeY, CKDWORD Flags) {
    RC_DEBUG_LOG_FMT("Resize called: %d,%d %dx%d flags=%u", PosX, PosY, SizeX, SizeY, Flags);
    if (m_Fullscreen) return CK_OK;

    if (!m_RasterizerContext) return CKERR_INVALIDRENDERCONTEXT;

    if (m_RasterizerContext->Resize(PosX, PosY, SizeX, SizeY, Flags)) {
        m_WinRect.left = PosX;
        m_WinRect.top = PosY;
        m_WinRect.right = PosX + SizeX;
        m_WinRect.bottom = PosY + SizeY;
        m_WindowRect = m_WinRect;
        m_RenderContextSettings.m_Rect = m_WinRect;
        return CK_OK;
    }

    return CKERR_INVALIDPARAMETER;
}

void RCKRenderContext::SetViewRect(VxRect &rect) {
    RC_DEBUG_LOG_FMT("SetViewRect called: %f,%f - %f,%f", rect.left, rect.top, rect.right, rect.bottom);
    m_ViewportData.ViewX = rect.left;
    m_ViewportData.ViewY = rect.top;
    m_ViewportData.ViewWidth = rect.GetWidth();
    m_ViewportData.ViewHeight = rect.GetHeight();

    if (m_RasterizerContext) {
        m_RasterizerContext->SetViewport(&m_ViewportData);
    }
}

void RCKRenderContext::GetViewRect(VxRect &rect) {
    RC_DEBUG_LOG("GetViewRect called");
    rect.left = m_ViewportData.ViewX;
    rect.top = m_ViewportData.ViewY;
    rect.right = m_ViewportData.ViewX + m_ViewportData.ViewWidth;
    rect.bottom = m_ViewportData.ViewY + m_ViewportData.ViewHeight;
}

VX_PIXELFORMAT RCKRenderContext::GetPixelFormat(int *Bpp, int *Zbpp, int *StencilBpp) {
    RC_DEBUG_LOG("GetPixelFormat called");
    if (Bpp) *Bpp = m_Bpp;
    if (Zbpp) *Zbpp = m_Zbpp;
    if (StencilBpp) *StencilBpp = m_StencilBpp;

    if (m_Bpp == 32) return _32_ARGB8888;
    if (m_Bpp == 24) return _24_RGB888;
    if (m_Bpp == 16) return _16_RGB565;
    return _16_RGB555;
}

void RCKRenderContext::SetState(VXRENDERSTATETYPE State, CKDWORD Value) {
    RC_DEBUG_LOG_FMT("SetState called, State=%d, Value=%u", State, Value);
    if (m_RasterizerContext)
        m_RasterizerContext->SetRenderState(State, Value);
}

CKDWORD RCKRenderContext::GetState(VXRENDERSTATETYPE State) {
    RC_DEBUG_LOG_FMT("GetState called, State=%d", State);
    CKDWORD value = 0;
    if (m_RasterizerContext)
        m_RasterizerContext->GetRenderState(State, &value);
    return value;
}

CKBOOL RCKRenderContext::SetTexture(CKTexture *tex, CKBOOL Clamped, int Stage) {
    if (tex) {
        return tex->SetAsCurrent(this, Clamped, Stage);
    } else {
        if (m_RasterizerContext)
            return m_RasterizerContext->SetTexture(0, Stage);
    }
    return FALSE;
}

CKBOOL RCKRenderContext::SetTextureStageState(CKRST_TEXTURESTAGESTATETYPE State, CKDWORD Value, int Stage) {
    if (m_RasterizerContext)
        return m_RasterizerContext->SetTextureStageState(Stage, State, Value);
    return FALSE;
}

CKRasterizerContext *RCKRenderContext::GetRasterizerContext() {
    RC_DEBUG_LOG("GetRasterizerContext called");
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
    if (Shading) *Shading = (VxShadeType) m_Shading;
    if (Texture) *Texture = m_TextureEnabled;
    if (Wireframe) *Wireframe = m_DisplayWireframe;
}

void RCKRenderContext::SetGlobalRenderMode(VxShadeType Shading, CKBOOL Texture, CKBOOL Wireframe) {
    m_Shading = Shading;
    m_TextureEnabled = Texture;
    m_DisplayWireframe = Wireframe;

    if (m_RasterizerContext) {
        m_RasterizerContext->SetRenderState(VXRENDERSTATE_SHADEMODE, Shading);
        m_RasterizerContext->SetRenderState(VXRENDERSTATE_FILLMODE, Wireframe ? VXFILL_WIREFRAME : VXFILL_SOLID);
    }
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
    VxColor color(R, G, B, 1.0f);
    SetAmbientLight(color.GetRGBA());
}

void RCKRenderContext::SetAmbientLight(CKDWORD Color) {
    if (m_RasterizerContext)
        m_RasterizerContext->SetRenderState(VXRENDERSTATE_AMBIENT, Color);
}

CKDWORD RCKRenderContext::GetAmbientLight() {
    return GetState(VXRENDERSTATE_AMBIENT);
}

void RCKRenderContext::SetFogMode(VXFOG_MODE Mode) {
    if (m_RasterizerContext) {
        m_RasterizerContext->SetRenderState(VXRENDERSTATE_FOGENABLE, Mode != VXFOG_NONE);
        m_RasterizerContext->SetRenderState(VXRENDERSTATE_FOGPIXELMODE, Mode);
    }
}

void RCKRenderContext::SetFogStart(float Start) {
    if (m_RasterizerContext)
        m_RasterizerContext->SetRenderState(VXRENDERSTATE_FOGSTART, *(CKDWORD *) &Start);
}

void RCKRenderContext::SetFogEnd(float End) {
    if (m_RasterizerContext)
        m_RasterizerContext->SetRenderState(VXRENDERSTATE_FOGEND, *(CKDWORD *) &End);
}

void RCKRenderContext::SetFogDensity(float Density) {
    if (m_RasterizerContext)
        m_RasterizerContext->SetRenderState(VXRENDERSTATE_FOGDENSITY, *(CKDWORD *) &Density);
}

void RCKRenderContext::SetFogColor(CKDWORD Color) {
    if (m_RasterizerContext)
        m_RasterizerContext->SetRenderState(VXRENDERSTATE_FOGCOLOR, Color);
}

VXFOG_MODE RCKRenderContext::GetFogMode() {
    return (VXFOG_MODE) GetState(VXRENDERSTATE_FOGPIXELMODE);
}

float RCKRenderContext::GetFogStart() {
    CKDWORD val = GetState(VXRENDERSTATE_FOGSTART);
    return *(float *) &val;
}

float RCKRenderContext::GetFogEnd() {
    CKDWORD val = GetState(VXRENDERSTATE_FOGEND);
    return *(float *) &val;
}

float RCKRenderContext::GetFogDensity() {
    CKDWORD val = GetState(VXRENDERSTATE_FOGDENSITY);
    return *(float *) &val;
}

CKDWORD RCKRenderContext::GetFogColor() {
    return GetState(VXRENDERSTATE_FOGCOLOR);
}

CKBOOL RCKRenderContext::DrawPrimitive(VXPRIMITIVETYPE pType, CKWORD *indices, int indexcount,
                                       VxDrawPrimitiveData *data) {
    if (m_RasterizerContext)
        return m_RasterizerContext->DrawPrimitive(pType, indices, indexcount, data);
    return FALSE;
}

void RCKRenderContext::TransformVertices(int VertexCount, VxTransformData *data, CK3dEntity *Ref) {
    VxMatrix mat;
    if (Ref) {
        mat = Ref->GetWorldMatrix();
    } else {
        mat = VxMatrix::Identity();
    }

    const VxMatrix &view = GetViewTransformationMatrix();
    const VxMatrix &proj = GetProjectionTransformationMatrix();

    VxMatrix viewProj;
    Vx3DMultiplyMatrix(viewProj, view, proj);

    VxMatrix total;
    Vx3DMultiplyMatrix(total, mat, viewProj);

    float halfWidth = m_ViewportData.ViewWidth * 0.5f;
    float halfHeight = m_ViewportData.ViewHeight * 0.5f;
    float viewX = m_ViewportData.ViewX;
    float viewY = m_ViewportData.ViewY;

    VxVector4 vRes;
    for (int i = 0; i < VertexCount; ++i) {
        VxVector *src = (VxVector *) ((CKBYTE *) data->InVertices + i * data->InStride);
        VxVector4 vIn(src->x, src->y, src->z, 1.0f);
        VxVector4 *dest = (VxVector4 *) ((CKBYTE *) data->OutVertices + i * data->OutStride);

        Vx3DMultiplyMatrixVector4(&vRes, total, &vIn);

        float rhw = 1.0f / vRes.w;
        dest->x = (vRes.x * rhw + 1.0f) * halfWidth + viewX;
        dest->y = (1.0f - vRes.y * rhw) * halfHeight + viewY;
        dest->z = vRes.z * rhw;
        dest->w = rhw;
    }
}

void RCKRenderContext::SetWorldTransformationMatrix(const VxMatrix &M) {
    if (m_RasterizerContext)
        m_RasterizerContext->SetTransformMatrix(VXMATRIX_WORLD, M);
}

void RCKRenderContext::SetProjectionTransformationMatrix(const VxMatrix &M) {
    if (m_RasterizerContext)
        m_RasterizerContext->SetTransformMatrix(VXMATRIX_PROJECTION, M);
}

void RCKRenderContext::SetViewTransformationMatrix(const VxMatrix &M) {
    if (m_RasterizerContext)
        m_RasterizerContext->SetTransformMatrix(VXMATRIX_VIEW, M);
}

const VxMatrix &RCKRenderContext::GetWorldTransformationMatrix() {
    return m_RasterizerContext ? m_RasterizerContext->m_WorldMatrix : VxMatrix::Identity();
}

const VxMatrix &RCKRenderContext::GetProjectionTransformationMatrix() {
    return m_RasterizerContext ? m_RasterizerContext->m_ProjectionMatrix : VxMatrix::Identity();
}

const VxMatrix &RCKRenderContext::GetViewTransformationMatrix() {
    return m_RasterizerContext ? m_RasterizerContext->m_ViewMatrix : VxMatrix::Identity();
}

CKBOOL RCKRenderContext::SetUserClipPlane(CKDWORD ClipPlaneIndex, const VxPlane &PlaneEquation) {
    if (m_RasterizerContext)
        return m_RasterizerContext->SetUserClipPlane(ClipPlaneIndex, PlaneEquation);
    return FALSE;
}

CKBOOL RCKRenderContext::GetUserClipPlane(CKDWORD ClipPlaneIndex, VxPlane &PlaneEquation) {
    if (m_RasterizerContext)
        return m_RasterizerContext->GetUserClipPlane(ClipPlaneIndex, PlaneEquation);
    return FALSE;
}

CKRenderObject *RCKRenderContext::Pick(int x, int y, CKPICKRESULT *oRes, CKBOOL iIgnoreUnpickable) {
    CKPOINT pt = {x, y};
    return Pick(pt, oRes, iIgnoreUnpickable);
}

CKRenderObject *RCKRenderContext::Pick(CKPOINT pt, CKPICKRESULT *oRes, CKBOOL iIgnoreUnpickable) {
    Vx2DVector pt2d((float) (pt.x - m_WindowRect.left), (float) (pt.y - m_WindowRect.top));

    // Check if point is in viewport
    if (pt2d.x < m_ViewportData.ViewX || pt2d.x > m_ViewportData.ViewX + m_ViewportData.ViewWidth ||
        pt2d.y < m_ViewportData.ViewY || pt2d.y > m_ViewportData.ViewY + m_ViewportData.ViewHeight) {
        return nullptr;
    }

    // Construct Ray
    VxRay ray;

    float viewX = (float) m_ViewportData.ViewX;
    float viewY = (float) m_ViewportData.ViewY;
    float viewW = (float) m_ViewportData.ViewWidth;
    float viewH = (float) m_ViewportData.ViewHeight;

    // Normalized device coordinates (-1 to 1)
    float nx = (pt2d.x - viewX) / (viewW * 0.5f) - 1.0f;
    float ny = 1.0f - (pt2d.y - viewY) / (viewH * 0.5f);

    VxMatrix viewMatrix = GetViewTransformationMatrix();
    VxMatrix projMatrix = GetProjectionTransformationMatrix();
    VxMatrix viewProj;
    Vx3DMultiplyMatrix(viewProj, viewMatrix, projMatrix);

    VxMatrix invViewProj;
    Vx3DInverseMatrix(invViewProj, viewProj);

    VxVector4 vInNear(nx, ny, 0.0f, 1.0f);
    VxVector4 vInFar(nx, ny, 1.0f, 1.0f);
    VxVector4 vOutNear, vOutFar;

    Vx3DMultiplyMatrixVector4(&vOutNear, invViewProj, &vInNear);
    Vx3DMultiplyMatrixVector4(&vOutFar, invViewProj, &vInFar);

    if (vOutNear.w != 0.0f) {
        float rhw = 1.0f / vOutNear.w;
        vOutNear.x *= rhw;
        vOutNear.y *= rhw;
        vOutNear.z *= rhw;
    }
    if (vOutFar.w != 0.0f) {
        float rhw = 1.0f / vOutFar.w;
        vOutFar.x *= rhw;
        vOutFar.y *= rhw;
        vOutFar.z *= rhw;
    }

    ray.m_Origin = VxVector(vOutNear.x, vOutNear.y, vOutNear.z);
    ray.m_Direction = VxVector(vOutFar.x - vOutNear.x, vOutFar.y - vOutNear.y, vOutFar.z - vOutNear.z);
    ray.m_Direction.Normalize();

    CK3dEntity *picked3D = nullptr;
    VxIntersectionDesc bestDesc;
    float minDistance = 1e30f;

    int count = m_ObjectExtents.Size();
    for (int i = 0; i < count; ++i) {
        CKObjectExtents &ext = m_ObjectExtents[i];
        CKRenderObject *obj = (CKRenderObject *) ext.m_Extent;

        if (!obj) continue;

        // Check pickable flag based on iIgnoreUnpickable parameter
        // When iIgnoreUnpickable is TRUE, we skip objects that are not pickable
        if (iIgnoreUnpickable) {
            if (CKIsChildClassOf(obj, CKCID_3DENTITY)) {
                CK3dEntity *ent3d = (CK3dEntity *) obj;
                if (!ent3d->IsPickable())
                    continue;
            } else if (CKIsChildClassOf(obj, CKCID_2DENTITY)) {
                CK2dEntity *ent2d = (CK2dEntity *) obj;
                if (!ent2d->IsPickable())
                    continue;
            }
        }

        if (CKIsChildClassOf(obj, CKCID_3DENTITY)) {
            CK3dEntity *ent = (CK3dEntity *) obj;

            // Check 2D extent
            if (pt2d.x >= ext.m_Rect.left && pt2d.x <= ext.m_Rect.right &&
                pt2d.y >= ext.m_Rect.top && pt2d.y <= ext.m_Rect.bottom) {
                const VxBbox &bbox = ent->GetBoundingBox();
                if (VxIntersect::RayBox(ray, bbox)) {
                    VxIntersectionDesc desc;
                    VxVector endPoint = ray.m_Origin + ray.m_Direction * 100000.0f;
                    if (ent->RayIntersection(&ray.m_Origin, &endPoint, &desc, nullptr)) {
                        if (desc.Distance < minDistance) {
                            minDistance = desc.Distance;
                            picked3D = ent;
                            bestDesc = desc;
                        }
                    }
                }
            }
        }
    }

    if (oRes && picked3D) {
        oRes->Sprite = 0;
        oRes->IntersectionPoint = bestDesc.IntersectionPoint;
        oRes->IntersectionNormal = bestDesc.IntersectionNormal;
        oRes->TexU = bestDesc.TexU;
        oRes->TexV = bestDesc.TexV;
        oRes->FaceIndex = bestDesc.FaceIndex;
        oRes->Distance = bestDesc.Distance;
    }

    return picked3D;
}

CKERROR RCKRenderContext::RectPick(const VxRect &r, XObjectPointerArray &oObjects, CKBOOL Intersect) {
    oObjects.Clear();

    VxRect rect = r;
    rect.left -= m_WindowRect.left;
    rect.right -= m_WindowRect.left;
    rect.top -= m_WindowRect.top;
    rect.bottom -= m_WindowRect.top;

    int count = m_ObjectExtents.Size();
    for (int i = 0; i < count; ++i) {
        CKObjectExtents &ext = m_ObjectExtents[i];
        CKRenderObject *obj = (CKRenderObject *) ext.m_Extent;
        if (!obj) continue;

        // Check overlap
        bool overlap = (rect.left < ext.m_Rect.right && rect.right > ext.m_Rect.left &&
            rect.top < ext.m_Rect.bottom && rect.bottom > ext.m_Rect.top);

        if (Intersect) {
            if (overlap) oObjects.PushBack(obj);
        } else {
            // Check if object is fully inside rect
            bool inside = (ext.m_Rect.left >= rect.left && ext.m_Rect.right <= rect.right &&
                ext.m_Rect.top >= rect.top && ext.m_Rect.bottom <= rect.bottom);
            if (inside) oObjects.PushBack(obj);
        }
    }

    return CK_OK;
}

void RCKRenderContext::AttachViewpointToCamera(CKCamera *cam) {
    m_Camera = (RCKCamera *) cam;
    if (m_Camera) {
        // Update aspect ratio if needed?
        // m_Camera->SetAspectRatio(...)
    }
}

void RCKRenderContext::DetachViewpointFromCamera() {
    m_Camera = nullptr;
}

CKCamera *RCKRenderContext::GetAttachedCamera() {
    return (CKCamera *) m_Camera;
}

CK3dEntity *RCKRenderContext::GetViewpoint() {
    return m_RenderedScene->GetRootEntity();
}

CKMaterial *RCKRenderContext::GetBackgroundMaterial() {
    return m_RenderedScene ? m_RenderedScene->GetBackgroundMaterial() : nullptr;
}

void RCKRenderContext::GetBoundingBox(VxBbox *BBox) {
    if (!BBox) return;

    BBox->Min = VxVector(1e30f, 1e30f, 1e30f);
    BBox->Max = VxVector(-1e30f, -1e30f, -1e30f);

    // Iterate m_3dRootObjects
    // Note: This might need to be recursive or use a flattened list if available.
    // For now, iterating roots is a good start.

    int count = m_3dRootObjects.Size();
    for (int i = 0; i < count; ++i) {
        CKObject *obj = m_Context->GetObject(m_3dRootObjects[i]);
        if (obj && CKIsChildClassOf(obj, CKCID_3DENTITY)) {
            CK3dEntity *ent = (CK3dEntity *) obj;
            const VxBbox &box = ent->GetBoundingBox();

            if (box.Min.x < BBox->Min.x) BBox->Min.x = box.Min.x;
            if (box.Min.y < BBox->Min.y) BBox->Min.y = box.Min.y;
            if (box.Min.z < BBox->Min.z) BBox->Min.z = box.Min.z;

            if (box.Max.x > BBox->Max.x) BBox->Max.x = box.Max.x;
            if (box.Max.y > BBox->Max.y) BBox->Max.y = box.Max.y;
            if (box.Max.z > BBox->Max.z) BBox->Max.z = box.Max.z;
        }
    }
}

void RCKRenderContext::GetStats(VxStats *stats) {
    if (stats) *stats = m_Stats;
}

void RCKRenderContext::SetCurrentMaterial(CKMaterial *mat, CKBOOL Lit) {
    if (!m_RasterizerContext) return;

    if (mat) {
        CKMaterialData data;
        data.Diffuse = mat->GetDiffuse();
        data.Ambient = mat->GetAmbient();
        data.Specular = mat->GetSpecular();
        data.Emissive = mat->GetEmissive();
        data.SpecularPower = mat->GetPower();

        m_RasterizerContext->SetMaterial(&data);

        CKTexture *tex = mat->GetTexture(0);
        SetTexture(tex, 0, 0);

        m_RasterizerContext->SetRenderState(VXRENDERSTATE_LIGHTING, Lit);
    } else {
        m_RasterizerContext->SetTexture(0, 0);
        m_RasterizerContext->SetRenderState(VXRENDERSTATE_LIGHTING, Lit);
    }
}

void RCKRenderContext::Activate(CKBOOL active) {
    RC_DEBUG_LOG_FMT("Activate called, active=%d", active);
    m_Active = active;
}

int RCKRenderContext::DumpToMemory(const VxRect *iRect, VXBUFFER_TYPE buffer, VxImageDescEx &desc) {
    if (m_RasterizerContext)
        return m_RasterizerContext->CopyToMemoryBuffer((CKRECT *) iRect, buffer, desc);
    return 0;
}

int RCKRenderContext::CopyToVideo(const VxRect *iRect, VXBUFFER_TYPE buffer, VxImageDescEx &desc) {
    if (m_RasterizerContext)
        return m_RasterizerContext->CopyFromMemoryBuffer((CKRECT *) iRect, buffer, desc);
    return 0;
}

CKERROR RCKRenderContext::DumpToFile(CKSTRING filename, const VxRect *rect, VXBUFFER_TYPE buffer) {
    return 0;
}

VxDirectXData *RCKRenderContext::GetDirectXInfo() {
    return nullptr;
}

void RCKRenderContext::WarnEnterThread() {
    if (m_RasterizerContext)
        m_RasterizerContext->WarnThread(TRUE);
}

void RCKRenderContext::WarnExitThread() {
    if (m_RasterizerContext)
        m_RasterizerContext->WarnThread(FALSE);
}

static void CheckPick(CK2dEntity *entity, const Vx2DVector &point, CK2dEntity *&bestEntity) {
    if (!entity->IsVisible()) return;
    if (entity->GetFlags() & CK_2DENTITY_NOTPICKABLE) return;

    VxRect rect;
    entity->GetRect(rect);
    rect.Normalize();

    if (point.x >= rect.left && point.x <= rect.right && point.y >= rect.top && point.y <= rect.bottom) {
        bestEntity = entity;
    }

    int count = entity->GetChildrenCount();
    if (count > 0) {
        std::vector<CK2dEntity *> children;
        children.reserve(count);
        for (int i = 0; i < count; ++i) {
            CK2dEntity *child = entity->GetChild(i);
            if (child) children.push_back(child);
        }

        std::sort(children.begin(), children.end(), [](CK2dEntity *a, CK2dEntity *b) {
            return a->GetZOrder() < b->GetZOrder();
        });

        for (CK2dEntity *child : children) {
            CheckPick(child, point, bestEntity);
        }
    }
}

CK2dEntity *RCKRenderContext::Pick2D(const Vx2DVector &v) {
    CK2dEntity *bestEntity = nullptr;

    std::vector<CK2dEntity *> roots;
    roots.reserve(m_2dRootObjects.Size());

    for (auto it = m_2dRootObjects.Begin(); it != m_2dRootObjects.End(); ++it) {
        CK_ID id = *it;
        CKObject *obj = m_Context->GetObject(id);
        if (obj && CKIsChildClassOf(obj, CKCID_2DENTITY)) {
            roots.push_back((CK2dEntity *) obj);
        }
    }

    std::sort(roots.begin(), roots.end(), [](CK2dEntity *a, CK2dEntity *b) {
        return a->GetZOrder() < b->GetZOrder();
    });

    for (CK2dEntity *root : roots) {
        CheckPick(root, v, bestEntity);
    }

    return bestEntity;
}

CKBOOL RCKRenderContext::SetRenderTarget(CKTexture *texture, int CubeMapFace) {
    if (!m_RasterizerContext) return FALSE;
    if (texture) {
        return m_RasterizerContext->SetTargetTexture(texture->GetRstTextureIndex(), texture->GetWidth(),
                                                     texture->GetHeight(), (CKRST_CUBEFACE) CubeMapFace);
    } else {
        return m_RasterizerContext->SetTargetTexture(0);
    }
}

void RCKRenderContext::AddRemoveSequence(CKBOOL Start) {
}

void RCKRenderContext::SetTransparentMode(CKBOOL Trans) {
    m_TransparentMode = Trans;
    if (m_RasterizerContext)
        m_RasterizerContext->SetTransparentMode(Trans);
}

void RCKRenderContext::AddDirtyRect(CKRECT *Rect) {
    if (m_RasterizerContext)
        m_RasterizerContext->AddDirtyRect(Rect);
}

void RCKRenderContext::RestoreScreenBackup() {
    if (m_RasterizerContext)
        m_RasterizerContext->RestoreScreenBackup();
}

CKDWORD RCKRenderContext::GetStencilFreeMask() {
    return m_StencilFreeMask;
}

void RCKRenderContext::UsedStencilBits(CKDWORD stencilBits) {
    m_StencilFreeMask &= ~stencilBits;
}

int RCKRenderContext::GetFirstFreeStencilBits() {
    if (m_StencilFreeMask == 0) return 0;
    for (int i = 0; i < 32; ++i) {
        if (m_StencilFreeMask & (1 << i)) return (1 << i);
    }
    return 0;
}

VxDrawPrimitiveData *RCKRenderContext::LockCurrentVB(CKDWORD VertexCount) {
    // Note: For large vertex counts, the caller should ensure the buffer is adequate.
    // The m_UserDrawPrimitiveData is pre-allocated in initialization.
    return m_UserDrawPrimitiveData;
}

CKBOOL RCKRenderContext::ReleaseCurrentVB() {
    return TRUE;
}

void RCKRenderContext::SetTextureMatrix(const VxMatrix &M, int Stage) {
    if (m_RasterizerContext)
        m_RasterizerContext->SetTransformMatrix(VXMATRIX_TEXTURE(Stage), M);
}

void RCKRenderContext::SetStereoParameters(float EyeSeparation, float FocalLength) {
    m_EyeSeparation = EyeSeparation;
    m_FocalLength = FocalLength;
}

void RCKRenderContext::GetStereoParameters(float &EyeSeparation, float &FocalLength) {
    EyeSeparation = m_EyeSeparation;
    FocalLength = m_FocalLength;
}

CKERROR RCKRenderContext::Create(void *Window, int Driver, CKRECT *rect, CKBOOL Fullscreen, int Bpp, int Zbpp,
                                 int StencilBpp, int RefreshRate) {
    RC_DEBUG_LOG_FMT("Create called - Window=%p, Driver=%d, Fullscreen=%d, Bpp=%d", Window, Driver, Fullscreen, Bpp);

    m_WinHandle = (CKDWORD) Window;
    m_Driver = Driver;
    m_Fullscreen = Fullscreen;
    m_Bpp = Bpp;
    m_Zbpp = Zbpp;
    m_StencilBpp = StencilBpp;

    if (rect) {
        m_WinRect = *rect;
        m_WindowRect = *rect;
        RC_DEBUG_LOG_FMT("Rect: %d,%d - %d,%d", rect->left, rect->top, rect->right, rect->bottom);
    }

    if (!m_RenderManager) {
        RC_DEBUG_LOG("Create failed: no render manager");
        return CKERR_INVALIDPARAMETER;
    }

    if (Driver < 0 || Driver >= m_RenderManager->m_DriverCount) {
        RC_DEBUG_LOG_FMT("Create failed: invalid driver %d (count=%d)", Driver, m_RenderManager->m_DriverCount);
        return CKERR_INVALIDPARAMETER;
    }

    VxDriverDescEx *drvDesc = &m_RenderManager->m_Drivers[Driver];
    m_RasterizerDriver = drvDesc->RasterizerDriver;

    if (!m_RasterizerDriver) {
        RC_DEBUG_LOG("Create failed: no rasterizer driver");
        return CKERR_INVALIDPARAMETER;
    }

    RC_DEBUG_LOG_FMT("Using rasterizer driver: %s", drvDesc->DriverDesc);

    m_RasterizerContext = m_RasterizerDriver->CreateContext();
    if (!m_RasterizerContext) {
        RC_DEBUG_LOG("Create failed: could not create rasterizer context");
        return CKERR_OUTOFMEMORY;
    }

    int width = m_WinRect.right - m_WinRect.left;
    int height = m_WinRect.bottom - m_WinRect.top;

    if (!m_RasterizerContext->Create((WIN_HANDLE) m_WinHandle, m_WinRect.left, m_WinRect.top, width, height, Bpp,
                                     Fullscreen, RefreshRate, Zbpp, StencilBpp)) {
        RC_DEBUG_LOG("Create failed: rasterizer context Create returned false");
        m_RasterizerDriver->DestroyContext(m_RasterizerContext);
        m_RasterizerContext = nullptr;
        return CKERR_INVALIDPARAMETER;
    }

    RC_DEBUG_LOG_FMT("RenderContext created successfully - %dx%d", width, height);

    m_RenderContextSettings.m_Rect = m_WinRect;
    m_RenderContextSettings.m_Bpp = Bpp;
    m_RenderContextSettings.m_Zbpp = Zbpp;
    m_RenderContextSettings.m_StencilBpp = StencilBpp;

    // Also initialize m_Settings which is used by RenderedScene
    m_Settings.m_Rect = m_WinRect;
    m_Settings.m_Bpp = Bpp;
    m_Settings.m_Zbpp = Zbpp;
    m_Settings.m_StencilBpp = StencilBpp;

    RC_DEBUG_LOG("Create: Settings saved");

    if (StencilBpp > 0) {
        m_StencilFreeMask = (1 << StencilBpp) - 1;
    } else {
        m_StencilFreeMask = 0;
    }

    RC_DEBUG_LOG("Create: Creating CKRenderedScene");
    m_RenderedScene = new CKRenderedScene(this);
    RC_DEBUG_LOG("Create: CKRenderedScene created");
    m_Active = TRUE;
    
    // Initialize default render flags
    m_RenderFlags = (CK_RENDER_FLAGS)(CK_RENDER_CLEARBACK | CK_RENDER_CLEARZ | 
                                     CK_RENDER_DOBACKTOFRONT | CK_RENDER_BACKGROUNDSPRITES |
                                     CK_RENDER_FOREGROUNDSPRITES);
    RC_DEBUG_LOG_FMT("Create: Set default m_RenderFlags=0x%X", m_RenderFlags);

    VxRect viewRect;
    viewRect.left = 0;
    viewRect.top = 0;
    viewRect.right = width;
    viewRect.bottom = height;
    RC_DEBUG_LOG_FMT("Create: Setting ViewRect to %d,%d - %d,%d", (int)viewRect.left, (int)viewRect.top,
                     (int)viewRect.right, (int)viewRect.bottom);
    SetViewRect(viewRect);
    RC_DEBUG_LOG("Create: ViewRect set");

    RC_DEBUG_LOG_FMT("Create: Validation - this=%p, m_Context=%p, m_RenderManager=%p, m_RenderedScene=%p, m_RasterizerContext=%p",
                     this, m_Context, m_RenderManager, m_RenderedScene, m_RasterizerContext);

    if (m_RenderManager) {
        RC_DEBUG_LOG_FMT("Create: RenderManager 2D roots fore=%p back=%p",
                         m_RenderManager->m_2DRootFore, m_RenderManager->m_2DRootBack);
    }

    RC_DEBUG_LOG("Create: returning CK_OK");
    return CK_OK;
}

RCKRenderContext::RCKRenderContext(CKContext *Context, CKSTRING name) : CKRenderContext(Context, name) {
    m_RenderManager = (RCKRenderManager *) Context->GetRenderManager();
    m_RasterizerContext = nullptr;
    m_RasterizerDriver = nullptr;
    m_RenderedScene = nullptr;
    m_UserDrawPrimitiveData = new UserDrawPrimitiveDataClass();
    m_Fullscreen = FALSE;
    m_Active = FALSE;
    m_Driver = 0;
    m_Bpp = 0;
    m_Zbpp = 0;
    m_StencilBpp = 0;
    m_RenderFlags = CK_RENDER_USECURRENTSETTINGS;
    m_DrawSceneCalls = 0;
    m_SmoothedFps = 0.0f;
    m_Camera = nullptr;
    m_Current3dEntity = nullptr;
    m_TargetTexture = nullptr;
    m_Texture = nullptr;
    m_CubeMapFace = 0;
    m_StencilFreeMask = 0;
    m_WinHandle = 0;
    m_AppHandle = 0;
    m_PerspectiveOrOrthographic = FALSE;
    m_ProjectionUpdated = FALSE;
    m_Start = FALSE;
    m_TransparentMode = FALSE;
    m_DeviceValid = FALSE;
    m_Shading = GouraudShading;
    m_TextureEnabled = TRUE;
    m_DisplayWireframe = FALSE;
    m_Fov = 0.7853982f;
    m_Zoom = 1.0f;
    m_NearPlane = 1.0f;
    m_FarPlane = 10000.0f;
    m_FocalLength = 0.0f;
    m_EyeSeparation = 0.0f;
    m_Flags = 0;
    m_FpsInterval = 0;
    m_SceneTraversalCalls = 0;
    m_SortTransparentObjects = 0;
    m_MaskFree = 0;
    m_VertexBufferIndex = 0;
    m_StartIndex = 0;
    m_DpFlags = 0;
    m_VertexBufferCount = 0;
    field_21C = 0;
    m_TimeFpsCalc = 0;
    memset(&m_WinRect, 0, sizeof(m_WinRect));
    memset(&m_Settings, 0, sizeof(m_Settings));
    memset(&m_FullscreenSettings, 0, sizeof(m_FullscreenSettings));
    memset(&m_CurrentExtents, 0, sizeof(m_CurrentExtents));
    memset(&m_ViewportData, 0, sizeof(m_ViewportData));
    m_ProjectionMatrix.Identity();
}

RCKRenderContext::~RCKRenderContext() {
    if (m_RenderedScene) {
        delete m_RenderedScene;
        m_RenderedScene = nullptr;
    }
    if (m_RasterizerContext && m_RasterizerDriver) {
        m_RasterizerDriver->DestroyContext(m_RasterizerContext);
        m_RasterizerContext = nullptr;
    }
    if (m_UserDrawPrimitiveData) {
        delete m_UserDrawPrimitiveData;
        m_UserDrawPrimitiveData = nullptr;
    }
}

CKERROR RCKRenderContext::RemapDependencies(CKDependenciesContext &context) {
    return CKObject::RemapDependencies(context);
}

CKERROR RCKRenderContext::Copy(CKObject &o, CKDependenciesContext &context) {
    CKRenderContext::Copy(o, context);

    if (CKIsChildClassOf(&o, CKCID_RENDERCONTEXT)) {
        RCKRenderContext *src = (RCKRenderContext *) &o;
        m_Bpp = src->m_Bpp;
        m_Zbpp = src->m_Zbpp;
        m_StencilBpp = src->m_StencilBpp;
        m_Fullscreen = src->m_Fullscreen;
        m_Driver = src->m_Driver;
        m_RenderFlags = src->m_RenderFlags;
        m_RenderContextSettings = src->m_RenderContextSettings;
        m_WindowRect = src->m_WindowRect;
        m_WinRect = src->m_WinRect;
    }
    return CK_OK;
}

void RCKRenderContext::OnClearAll() {
    // Clear all render context state
    m_3dRootObjects.Clear();
    m_2dRootObjects.Clear();
    m_TransparentObjects.Clear();
    m_Sprite3DBatches.Clear();
    m_ObjectExtents.Clear();

    // Reset camera
    m_Camera = nullptr;
    m_Current3dEntity = nullptr;
    m_Texture = nullptr;
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
}

void RCKRenderContext::SetClipRect(VxRect *rect) {
    if (!rect) return;

    m_ViewportData.ViewX = (int) rect->left;
    m_ViewportData.ViewY = (int) rect->top;
    m_ViewportData.ViewWidth = (int) rect->GetWidth();
    m_ViewportData.ViewHeight = (int) rect->GetHeight();

    if (m_RasterizerContext) {
        m_RasterizerContext->SetViewport(&m_ViewportData);
    }

    m_ProjectionUpdated = FALSE;
}

void RCKRenderContext::UpdateProjection(CKBOOL forceUpdate) {
    if (!m_RasterizerContext) return;

    if (!m_ProjectionUpdated || forceUpdate) {
        m_ProjectionMatrix.Perspective(m_Fov,
                                       (float) m_ViewportData.ViewWidth / (float) m_ViewportData.ViewHeight,
                                       m_NearPlane, m_FarPlane);
        m_RasterizerContext->SetTransformMatrix(VXMATRIX_PROJECTION, m_ProjectionMatrix);
        m_ProjectionUpdated = TRUE;
    }
}

void RCKRenderContext::CallSprite3DBatches() {
    // Process all pending Sprite3D batches
    // Sprite3D batches are used for optimized rendering of 3D sprites
    // that share the same material

    int count = m_Sprite3DBatches.Size();
    if (count == 0)
        return;

    // Each batch contains sprite geometry data that was accumulated
    // during the render pass and needs to be flushed to the GPU

    for (int i = 0; i < count; ++i) {
        CKSprite3DBatch *batch = (CKSprite3DBatch *) m_Sprite3DBatches[i];
        if (!batch)
            continue;

        // Batch rendering would typically:
        // 1. Set the material/texture state
        // 2. Draw the accumulated sprite vertices
        // The actual implementation depends on how batches are structured
        // and accumulated during sprite rendering
    }

    // Clear all batches after rendering
    m_Sprite3DBatches.Clear();
}

void RCKRenderContext::AddExtents2D(const VxRect &rect, CKObject *obj) {
    if (obj) {
        // Add to object extents list
        CKObjectExtents extents;
        extents.m_Rect = rect;
        extents.m_Extent = obj->GetID();
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

CK_CLASSID RCKRenderContext::m_ClassID = CKCID_RENDERCONTEXT;

CKObject *RCKRenderContext::CreateInstance(CKContext *Context) {
    return new RCKRenderContext(Context, nullptr);
}

CKSTRING RCKRenderContext::GetClassName() {
    return (CKSTRING) "Render Context";
}

int RCKRenderContext::GetDependenciesCount(int mode) {
    return 0;
}

CKSTRING RCKRenderContext::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKRenderContext::Register() {
    // Based on IDA decompilation
    CKClassNeedNotificationFrom(m_ClassID, CKCID_RENDEROBJECT);
}

void RCKRenderContext::PrepareCameras(CK_RENDER_FLAGS Flags) {
    RC_DEBUG_LOG("PrepareCameras called");

    if (Flags == CK_RENDER_USECURRENTSETTINGS)
        Flags = m_RenderFlags;

    m_RenderedScene->PrepareCameras(Flags);

    RC_DEBUG_LOG("PrepareCameras complete");
}

// Size check: sizeof(RCKRenderContext) = ? (should be 956)

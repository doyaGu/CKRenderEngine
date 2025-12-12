#include "RCKRenderContext.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "RCKRenderManager.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKMesh.h"
#include "RCKTexture.h"
#include "RCKMaterial.h"
#include "RCKSprite3D.h"
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
#include "CKTimeManager.h"
#include "CKAttributeManager.h"
#include "CKParameterOut.h"
#include "CKSceneGraph.h"
#include "VxMath.h"
#include "VxIntersect.h"
#include "CKDebugLogger.h"

// Debug logging macros
#define RC_DEBUG_LOG(msg) CK_LOG("RenderContext", msg)
#define RC_DEBUG_LOG_FMT(fmt, ...) CK_LOG_FMT("RenderContext", fmt, __VA_ARGS__)

CK_CLASSID RCKRenderContext::GetClassID() {
    return m_ClassID;
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
    // Based on IDA at 0x10067c2a
    RC_DEBUG_LOG_FMT("AddObject called: obj=%p IsRoot=%d InRC=%d", 
                     obj, obj ? obj->IsRootObject() : -1, obj ? obj->IsInRenderContext(this) : -1);
    if (obj && obj->IsRootObject() && !obj->IsInRenderContext(this)) {
        RC_DEBUG_LOG_FMT("AddObject: Adding obj=%p to render context", obj);
        ((RCKRenderObject *)obj)->AddToRenderContext(this);
        m_RenderedScene->AddObject(obj);
    }
}

void RCKRenderContext::AddObjectWithHierarchy(CKRenderObject *obj) {
    // Based on IDA at 0x10067cb5
    if (obj) {
        AddObject(obj);
        
        if (CKIsChildClassOf(obj, CKCID_3DENTITY)) {
            CK3dEntity *ent = (CK3dEntity *)obj;
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
        ((RCKRenderObject *)obj)->RemoveFromRenderContext(this);
        
        // If it's a 3D entity, also clear it from object extents
        if (CKIsChildClassOf(obj, CKCID_3DENTITY)) {
            int count = m_ObjectExtents.Size();
            for (int i = 0; i < count; ++i) {
                if (m_ObjectExtents[i].m_Entity == (CKDWORD)obj) {
                    m_ObjectExtents[i].m_Entity = 0;
                    break;
                }
            }
        }
        
        m_RenderedScene->RemoveObject(obj);
    }
}

CKBOOL RCKRenderContext::IsObjectAttached(CKRenderObject *obj) {
    // Based on IDA at 0x10067bf7
    if (obj)
        return obj->IsInRenderContext(this);
    return FALSE;
}

const XObjectArray &RCKRenderContext::Compute3dRootObjects() {
    // IDA: 0x10067e41
    // Clear and rebuild the root objects array from scene graph
    m_RootObjects.Clear();
    
    if (m_RenderManager) {
        // Iterate through scene graph root node children
        CKSceneGraphNode &rootNode = m_RenderManager->m_SceneGraphRootNode;
        for (int i = 0; i < rootNode.m_Children.Size(); ++i) {
            CKSceneGraphNode *child = rootNode.m_Children[i];
            if (child && child->m_Entity) {
                m_RootObjects.PushBack(child->m_Entity->GetID());
            }
        }
    }
    
    return m_RootObjects;
}

const XObjectArray &RCKRenderContext::Compute2dRootObjects() {
    // IDA: 0x10067ec2
    // Get 2D root entities from both background and foreground
    CK2dEntity *bgRoot = Get2dRoot(TRUE);
    CK2dEntity *fgRoot = Get2dRoot(FALSE);
    
    int bgCount = bgRoot ? bgRoot->GetChildrenCount() : 0;
    int fgCount = fgRoot ? fgRoot->GetChildrenCount() : 0;
    
    m_RootObjects.Resize(bgCount + fgCount);
    
    // Add background children
    for (int i = 0; i < bgCount; ++i) {
        CK2dEntity *child = (CK2dEntity*)bgRoot->GetChild(i);
        if (child) {
            m_RootObjects[i] = child->GetID();
        } else {
            m_RootObjects[i] = 0;
        }
    }
    
    // Add foreground children
    for (int j = 0; j < fgCount; ++j) {
        CK2dEntity *child = (CK2dEntity*)fgRoot->GetChild(j);
        if (child) {
            m_RootObjects[bgCount + j] = child->GetID();
        } else {
            m_RootObjects[bgCount + j] = 0;
        }
    }
    
    return m_RootObjects;
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
    // Based on IDA at 0x10067e01
    m_ObjectExtents.Resize(0);
    
    if (m_RasterizerContext)
        m_RasterizerContext->FlushRenderStateCache();
    
    if (m_RenderedScene)
        m_RenderedScene->DetachAll();
}

void RCKRenderContext::ForceCameraSettingsUpdate() {
    // IDA: 0x10069c2b
    RC_DEBUG_LOG("ForceCameraSettingsUpdate called");
    if (m_RenderedScene) {
        m_RenderedScene->ForceCameraSettingsUpdate();
    }
}

CK_RENDER_FLAGS RCKRenderContext::ResolveRenderFlags(CK_RENDER_FLAGS Flags) const {
    return (Flags == CK_RENDER_USECURRENTSETTINGS) ? static_cast<CK_RENDER_FLAGS>(m_RenderFlags) : Flags;
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
    // Loads and initializes the PV Information watermark texture
    m_PVInformation = m_Context->GetPVInformation();
    if (m_PVInformation) {
        m_PVTimeProfiler.Reset();
        
        // Load appropriate bitmap based on PV information state
        // PVInformation values: 4 = resource 0x67, 6 = resource 0x65, other = resource 0x66
        HMODULE hModule = GetModuleHandleA("CK2_3D.dll");
        HBITMAP hBitmap = nullptr;
        if (m_PVInformation == 4) {
            hBitmap = LoadBitmapA(hModule, MAKEINTRESOURCEA(0x67));
        } else if (m_PVInformation == 6) {
            hBitmap = LoadBitmapA(hModule, MAKEINTRESOURCEA(0x65));
        } else {
            hBitmap = LoadBitmapA(hModule, MAKEINTRESOURCEA(0x66));
        }
        
        // Create NCU texture if not already created
        if (!m_NCUTex) {
            m_NCUTex = (RCKTexture *)m_Context->CreateObject(
                CKCID_TEXTURE, 
                (CKSTRING)"NCUTex",
                CK_OBJECTCREATION_NONAMECHECK, 
                nullptr);
            if (m_NCUTex) {
                m_NCUTex->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
                m_NCUTex->Create(256, 32, 32, 0);
            }
        }
        
        if (hBitmap && m_NCUTex) {
            // Convert bitmap and blit to texture
            // Note: This simplified implementation - full implementation would use
            // VxConvertBitmapTo24, VxConvertBitmap, VxDoBlit
            // For now, we just delete the bitmap
            DeleteObject(hBitmap);
        }
    }
}

void RCKRenderContext::DrawPVInformationWatermark() {
    // IDA: sub_1006A690
    // Draws the PV Information watermark texture at the bottom center of viewport
    if (!m_NCUTex)
        return;
    
    // For PVInformation 4 or 6, only display for first 5 seconds
    if (m_PVInformation == 6 || m_PVInformation == 4) {
        float elapsed = m_PVTimeProfiler.Current();
        if (elapsed >= 5000.0f)
            return;
    }
    
    // Set up render states
    m_NCUTex->SetAsCurrent(this, FALSE, 0);
    m_RasterizerContext->SetRenderState(VXRENDERSTATE_LIGHTING, 0);
    m_RasterizerContext->SetRenderState(VXRENDERSTATE_ZENABLE, 0);
    m_RasterizerContext->SetRenderState(VXRENDERSTATE_CLIPPING, 0);
    m_RasterizerContext->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, 0);
    m_RasterizerContext->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);
    m_RasterizerContext->SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_GOURAUD);
    m_RasterizerContext->SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, 0);
    m_RasterizerContext->SetRenderState(VXRENDERSTATE_STENCILENABLE, 0);
    m_RasterizerContext->SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, 0);
    m_RasterizerContext->SetRenderState(VXRENDERSTATE_SPECULARENABLE, 0);
    m_RasterizerContext->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
    m_RasterizerContext->SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, 1);
    m_RasterizerContext->SetTextureStageState(0, CKRST_TSS_MAGFILTER, 2);
    m_RasterizerContext->SetTextureStageState(0, CKRST_TSS_MINFILTER, 2);
    
    // Calculate watermark position (centered at bottom of viewport)
    int texWidth = m_NCUTex->GetWidth();
    int texHeight = m_NCUTex->GetHeight();
    float x1 = (float)((m_ViewportData.ViewWidth - texWidth) / 2 + m_ViewportData.ViewX);
    float x2 = (float)((texWidth + m_ViewportData.ViewWidth) / 2 + m_ViewportData.ViewX);
    float y1 = (float)(m_ViewportData.ViewY + m_ViewportData.ViewHeight - texHeight);
    float y2 = (float)(m_ViewportData.ViewY + m_ViewportData.ViewHeight);
    
    // Set up draw primitive data for quad
    VxDrawPrimitiveData dp;
    memset(&dp, 0, sizeof(dp));
    
    VxUV uvs[4] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f}
    };
    
    CKDWORD colors[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    
    // Position data (x, y, z, rhw format for transformed vertices)
    float positions[16];
    positions[0] = x1; positions[1] = y1; positions[2] = 0.0f; positions[3] = 1.0f;
    positions[4] = x2; positions[5] = y1; positions[6] = 0.0f; positions[7] = 1.0f;
    positions[8] = x2; positions[9] = y2; positions[10] = 0.0f; positions[11] = 1.0f;
    positions[12] = x1; positions[13] = y2; positions[14] = 0.0f; positions[15] = 1.0f;
    
    dp.Flags = CKRST_DP_TRANSFORM;
    dp.VertexCount = 4;
    dp.TexCoordPtr = uvs;
    dp.TexCoordStride = sizeof(VxUV);
    dp.PositionPtr = positions;
    dp.PositionStride = 16;
    dp.ColorPtr = colors;
    dp.ColorStride = 4;
    
    m_RasterizerContext->DrawPrimitive(VX_TRIANGLEFAN, nullptr, 0, &dp);
    
    // Restore render states
    m_RasterizerContext->SetRenderState(VXRENDERSTATE_ZENABLE, 1);
    m_RasterizerContext->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, 1);
    m_RasterizerContext->SetRenderState(VXRENDERSTATE_CLIPPING, 1);
}

void RCKRenderContext::FillStateString() {
    // IDA: RCKRenderContext::FillStateString
    // Fills m_StateString with current render state information
    // This would query the rasterizer context for various state values
    // For now, provide a stub implementation
    m_StateString = "";
    if (m_RasterizerContext) {
        // Could add detailed state information here
        // For debugging cache state, texture state, etc.
    }
}

CKERROR RCKRenderContext::Clear(CK_RENDER_FLAGS Flags, CKDWORD Stencil) {
    // IDA: 0x10069c72
    if (!m_RasterizerContext)
        return CKERR_INVALIDRENDERCONTEXT;

    CK_RENDER_FLAGS effectiveFlags = ResolveRenderFlags(Flags);

    // Check if any clear flags are set (CK_RENDER_CLEARBACK | CK_RENDER_CLEARZ | CK_RENDER_CLEARSTENCIL = 0x70)
    if ((effectiveFlags & 0x70) == 0)
        return CK_OK;

    CKMaterial *backgroundMaterial = m_RenderedScene ? m_RenderedScene->GetBackgroundMaterial() : nullptr;

    // If not clearing viewport only, set full screen viewport temporarily
    if ((effectiveFlags & CK_RENDER_CLEARVIEWPORT) == 0) {
        CKViewportData fullVp;
        fullVp.ViewX = 0;
        fullVp.ViewY = 0;
        fullVp.ViewWidth = m_Settings.m_Rect.right;
        fullVp.ViewHeight = m_Settings.m_Rect.bottom;
        fullVp.ViewZMin = 0.0f;
        fullVp.ViewZMax = 1.0f;
        m_RasterizerContext->SetViewport(&fullVp);
    }

    // If background material has a texture, render it as a fullscreen quad
    if (backgroundMaterial && (effectiveFlags & CK_RENDER_CLEARBACK) != 0) {
        CKTexture *bgTexture = backgroundMaterial->GetTexture(0);
        if (bgTexture) {
            // Set up render states for fullscreen quad
            bgTexture->SetAsCurrent(this, FALSE, 0);
            m_RasterizerContext->SetTextureStageState(1, CKRST_TSS_STAGEBLEND, 0);
            m_RasterizerContext->SetVertexShader(0);
            m_RasterizerContext->SetRenderState(VXRENDERSTATE_LIGHTING, 0);
            m_RasterizerContext->SetRenderState(VXRENDERSTATE_SPECULARENABLE, 0);
            m_RasterizerContext->SetRenderState(VXRENDERSTATE_ZENABLE, 0);
            m_RasterizerContext->SetRenderState(VXRENDERSTATE_CLIPPING, 0);
            m_RasterizerContext->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, 0);
            m_RasterizerContext->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);
            m_RasterizerContext->SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_GOURAUD);
            m_RasterizerContext->SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, 0);
            m_RasterizerContext->SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, 0);
            m_RasterizerContext->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
            m_RasterizerContext->SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, 1);
            m_RasterizerContext->SetTextureStageState(0, CKRST_TSS_MAGFILTER, 2);
            m_RasterizerContext->SetTextureStageState(0, CKRST_TSS_MINFILTER, 2);
            m_RasterizerContext->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, 0);
            m_RasterizerContext->SetTextureStageState(0, CKRST_TSS_TEXCOORDINDEX, 0);

            // Set up fullscreen quad vertices
            VxDrawPrimitiveData dp;
            memset(&dp, 0, sizeof(dp));

            // UV coordinates
            float uvs[8] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
            CKDWORD colors[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
            VxVector4 positions[4];

            float w = (float)m_Settings.m_Rect.right;
            float h = (float)m_Settings.m_Rect.bottom;

            positions[0] = VxVector4(0.0f, 0.0f, 0.0f, 1.0f);
            positions[1] = VxVector4(w, 0.0f, 0.0f, 1.0f);
            positions[2] = VxVector4(w, h, 0.0f, 1.0f);
            positions[3] = VxVector4(0.0f, h, 0.0f, 1.0f);

            dp.Flags = CKRST_DP_TRANSFORM;
            dp.VertexCount = 4;
            dp.PositionPtr = positions;
            dp.PositionStride = sizeof(VxVector4);
            dp.ColorPtr = colors;
            dp.ColorStride = sizeof(CKDWORD);
            dp.TexCoordPtr = uvs;
            dp.TexCoordStride = sizeof(float) * 2;

            m_RasterizerContext->DrawPrimitive(VX_TRIANGLEFAN, nullptr, 0, &dp);

            // Clear the CK_RENDER_CLEARBACK flag since we rendered the background texture
            effectiveFlags = (CK_RENDER_FLAGS)(effectiveFlags & ~CK_RENDER_CLEARBACK);

            // Restore render states
            m_RasterizerContext->SetRenderState(VXRENDERSTATE_ZENABLE, 1);
            m_RasterizerContext->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, 1);
            m_RasterizerContext->SetRenderState(VXRENDERSTATE_CLIPPING, 1);
        }
    }

    // Perform actual clear if still needed
    if (effectiveFlags & 0x70) {
        if ((effectiveFlags & CK_RENDER_CLEARSTENCIL) != 0)
            m_StencilFreeMask = Stencil;

        CKDWORD clearColor = 0;
        if (backgroundMaterial)
            clearColor = RGBAFTOCOLOR(&backgroundMaterial->GetDiffuse());

        m_RasterizerContext->Clear(effectiveFlags, clearColor, 1.0f, Stencil, 0, nullptr);
    }

    // Restore viewport if we changed it
    if ((effectiveFlags & CK_RENDER_CLEARVIEWPORT) == 0)
        m_RasterizerContext->SetViewport(&m_ViewportData);

    return CK_OK;
}

CKERROR RCKRenderContext::DrawScene(CK_RENDER_FLAGS Flags) {
    // IDA: 0x1006a37d
    if (!m_RasterizerContext)
        return CKERR_INVALIDRENDERCONTEXT;

    CK_RENDER_FLAGS effectiveFlags = ResolveRenderFlags(Flags);
    if ((effectiveFlags & CK_RENDER_SKIPDRAWSCENE) != 0)
        return CK_OK;

    RC_DEBUG_LOG_FMT("DrawScene called: RenderedScene=%p, Flags=0x%x", m_RenderedScene, effectiveFlags);

    ++m_DrawSceneCalls;
    memset(&m_Stats, 0, sizeof(VxStats));
    m_Stats.SmoothedFps = m_SmoothedFps;
    m_RasterizerContext->m_RenderStateCacheHit = 0;
    m_RasterizerContext->m_RenderStateCacheMiss = 0;

    if ((effectiveFlags & CK_RENDER_DONOTUPDATEEXTENTS) == 0) {
        m_ObjectExtents.Resize(0);
    }

    m_RasterizerContext->BeginScene();
    CKERROR err = m_RenderedScene ? m_RenderedScene->Draw(effectiveFlags) : CKERR_INVALIDRENDERCONTEXT;
    m_RasterizerContext->EndScene();

    m_Stats.RenderStateCacheHit = m_RasterizerContext->m_RenderStateCacheHit;
    m_Stats.RenderStateCacheMiss = m_RasterizerContext->m_RenderStateCacheMiss;
    --m_DrawSceneCalls;

    return err;
}

CKERROR RCKRenderContext::BackToFront(CK_RENDER_FLAGS Flags) {
    // IDA: 0x1006abdd
    if (m_DeviceValid)
        return CK_OK;
    if (!m_RasterizerContext)
        return CKERR_INVALIDRENDERCONTEXT;

    CK_RENDER_FLAGS effectiveFlags = ResolveRenderFlags(Flags);
    
    // Check if we need to do back-to-front or have render target
    if ((effectiveFlags & CK_RENDER_DOBACKTOFRONT) == 0 && !m_TargetTexture)
        return CK_OK;

    // Screen dump functionality (Ctrl+Alt+F10)
    // VK_CONTROL=17, VK_MENU(Alt)=18, VK_F10=121
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

    if (m_TargetTexture) {
        // Render-to-texture path
        int height = m_TargetTexture->GetHeight();
        int width = m_TargetTexture->GetWidth();
        VxRect rect(0.0f, 0.0f, (float)width, (float)height);
        
        // Get current pixel format and target's video format
        VX_PIXELFORMAT srcFormat = GetPixelFormat(nullptr, nullptr, nullptr);
        VX_PIXELFORMAT dstFormat = m_TargetTexture->GetVideoPixelFormat();
        
        // Handle pixel format conversion
        // _32_RGB888 -> _32_ARGB8888
        if (srcFormat == _32_RGB888)
            srcFormat = _32_ARGB8888;
        if (dstFormat == _32_RGB888)
            dstFormat = _32_ARGB8888;
        // _16_RGB555 -> _16_ARGB1555
        if (srcFormat == _16_RGB555)
            srcFormat = _16_ARGB1555;
        if (dstFormat == _16_RGB555)
            dstFormat = _16_ARGB1555;
        
        // If formats don't match, re-create texture with correct format
        if (dstFormat != srcFormat) {
            m_TargetTexture->SetDesiredVideoFormat(srcFormat);
            m_TargetTexture->FreeVideoMemory();
            m_TargetTexture->SystemToVideoMemory(this, FALSE);
        }
        
        // Copy render context to texture
        if (!m_TargetTexture->CopyContext(this, &rect, &rect, m_CubeMapFace)) {
            m_TargetTexture = nullptr;
        }
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
        
        // Call rasterizer BackToFront
        CKBOOL waitVbl = (effectiveFlags & CK_RENDER_WAITVBL) != 0;
        m_RasterizerContext->BackToFront(waitVbl);
    }

    // Debug mode handling (Ctrl+Alt+F11 to enter, various keys while in debug mode)
    // VK_F11=122, VK_R=82, VK_INSERT=45, VK_HOME=36, VK_PRIOR(PageUp)=33
    if (m_RenderManager->m_EnableDebugMode.Value) {
        if ((m_Flags & 1) != 0) {
            // Debug mode is active
            if (m_CurrentObjectDesc.Length() > 0) {
                HDC hdc = GetDC((HWND)m_WinHandle);
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
                
                ReleaseDC((HWND)m_WinHandle, hdc);
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
                        m_Flags &= ~1;  // Clear debug mode flag
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
            m_Flags |= 1;  // Set debug mode flag
            m_FpsInterval = 1;
            m_CurrentObjectDesc = "DEBUG RENDER MODE : Ins,Home,Page Up\n\n";
            // Wait for keys to be released
            while (GetAsyncKeyState(VK_CONTROL)) {}
            while (GetAsyncKeyState(VK_MENU)) {}
        }
    }

    return CK_OK;
}

CKERROR RCKRenderContext::Render(CK_RENDER_FLAGS Flags) {
    // IDA: 0x1006948e
    VxTimeProfiler profiler;

    RC_DEBUG_LOG_FMT("Render called: Active=%d, Rasterizer=%p, Flags=0x%x", m_Active, m_RasterizerContext, Flags);

    if (!m_Active)
        return CKERR_RENDERCONTEXTINACTIVE;
    if (!m_RasterizerContext)
        return CKERR_INVALIDRENDERCONTEXT;

    // Resolve flags - if zero, use current settings
    CK_RENDER_FLAGS effectiveFlags = ResolveRenderFlags(Flags);

    // IDA: Check TimeManager for VBL sync settings
    CKTimeManager *timeManager = m_Context->GetTimeManager();
    if (timeManager) {
        CKDWORD limitOptions = timeManager->GetLimitOptions();
        if (limitOptions & CK_FRAMERATE_SYNC) {
            effectiveFlags = (CK_RENDER_FLAGS)(effectiveFlags | CK_RENDER_WAITVBL);
        }
    }

    // Prepare cameras for rendering
    PrepareCameras(effectiveFlags);
    m_Camera = nullptr;

    // IDA: Check for camera plane master attribute ("1campl8ne4ster")
    // This looks up an attribute on the attached camera to find a master camera.
    if (m_RenderedScene && m_RenderedScene->m_AttachedCamera) {
        CKAttributeManager *attrManager = m_Context->GetAttributeManager();
        if (attrManager) {
            CKAttributeType attrType = attrManager->GetAttributeTypeByName("1campl8ne4ster");
            CKParameterOut *attrParam = m_RenderedScene->m_AttachedCamera->GetAttributeParameter(attrType);
            if (attrParam) {
                CKDWORD *pCameraId = (CKDWORD *)attrParam->GetReadDataPtr(FALSE);
                if (pCameraId) {
                    m_Camera = (RCKCamera *)m_Context->GetObject(*pCameraId);
                }
            }
        }
    }

    CKERROR err = CK_OK;

    // Check for stereo rendering
    if (m_RasterizerContext->m_Driver && m_RasterizerContext->m_Driver->m_Stereo) {
        // Stereo rendering path
        VxMatrix originalWorldMat;
        if (m_RenderedScene && m_RenderedScene->m_RootEntity) {
            Vx3DMatrixIdentity(originalWorldMat);
            const VxMatrix &wm = m_RenderedScene->m_RootEntity->GetWorldMatrix();
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

        // Clear both buffers
        m_RasterizerContext->SetDrawBuffer(CKRST_DRAWBOTH);
        err = Clear(effectiveFlags);
        if (err != CK_OK)
            return err;

        // Render right eye
        m_RasterizerContext->SetDrawBuffer(CKRST_DRAWRIGHT);
        m_RenderedScene->m_RootEntity->SetWorldMatrix(rightWorldMat, FALSE);
        if (!m_Camera) {
            UpdateProjection(FALSE);
            m_ProjectionMatrix[2][0] = -0.5f * m_ProjectionMatrix[0][0] * projOffset;
            m_RasterizerContext->SetTransformMatrix(VXMATRIX_PROJECTION, m_ProjectionMatrix);
        }
        err = DrawScene(effectiveFlags);
        if (err != CK_OK)
            return err;

        // Render left eye
        m_RasterizerContext->SetDrawBuffer(CKRST_DRAWLEFT);
        m_RenderedScene->m_RootEntity->SetWorldMatrix(leftWorldMat, FALSE);
        if (!m_Camera) {
            m_ProjectionMatrix[2][0] = 0.5f * m_ProjectionMatrix[0][0] * projOffset;
            m_RasterizerContext->SetTransformMatrix(VXMATRIX_PROJECTION, m_ProjectionMatrix);
        }
        err = DrawScene(effectiveFlags);
        if (err != CK_OK)
            return err;

        // Restore original state
        m_RenderedScene->m_RootEntity->SetWorldMatrix(originalWorldMat, FALSE);
        m_ProjectionMatrix[2][0] = 0.0f;
        m_RasterizerContext->SetTransformMatrix(VXMATRIX_PROJECTION, m_ProjectionMatrix);
        m_RasterizerContext->SetDrawBuffer(CKRST_DRAWBOTH);
    } else {
        // Normal rendering (non-stereo)
        err = Clear(effectiveFlags);
        if (err != CK_OK)
            return err;

        err = DrawScene(effectiveFlags);
        if (err != CK_OK)
            return err;
    }

    // FPS calculation
    ++m_TimeFpsCalc;
    float elapsed = m_RenderTimeProfiler.Current();
    if (elapsed >= 1000.0f) {
        float fps = (float)m_TimeFpsCalc * 1000.0f / elapsed;
        m_RenderTimeProfiler.Reset();
        m_TimeFpsCalc = 0;
        // Smooth FPS: 90% new value + 10% old value
        m_SmoothedFps = fps * 0.9f + m_SmoothedFps * 0.1f;
        m_Stats.SmoothedFps = m_SmoothedFps;
    }

    err = BackToFront(effectiveFlags);
    if (err != CK_OK)
        return err;

    // Handle extents tracking (when CK_RENDER_DONOTUPDATEEXTENTS flag is set)
    // Note: This stores render extent info, not object picking extents
    if ((effectiveFlags & CK_RENDER_DONOTUPDATEEXTENTS) != 0) {
        CKObjectExtents extents;
        extents.m_Rect.left = 0.0f;
        extents.m_Rect.top = 0.0f;
        extents.m_Rect.right = 0.0f;
        extents.m_Rect.bottom = 0.0f;
        GetViewRect(extents.m_Rect);
        extents.m_Entity = (CKDWORD)effectiveFlags;  // Store flags
        extents.m_Camera = GetAttachedCamera() ? (CKDWORD)GetAttachedCamera() : 0;
        // Note: m_Extents is XVoidArray - would need different handling
        // For now, skip this as it requires additional infrastructure
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

void RCKRenderContext::AddPostRenderCallBack(CK_RENDERCALLBACK Function, void *Argument, CKBOOL Temporary,
                                             CKBOOL BeforeTransparent) {
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
    // Try both containers since we don't know which one the callback was added to
    m_PostRenderCallBacks.RemovePostCallback((void *) Function, Argument);
    m_PreRenderCallBacks.RemovePostCallback((void *) Function, Argument);
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
    if ((Flags & CKRST_DP_VBUFFER) != 0 &&
        m_RasterizerContext &&
        (m_RasterizerContext->m_Driver->m_3DCaps.CKRasterizerSpecificCaps & CKRST_SPECIFICCAPS_CANDOVERTEXBUFFER) != 0) {

        CKDWORD vertexSize = 0;
        CKDWORD vertexFormat = CKRSTGetVertexFormat(Flags, vertexSize);
        CKDWORD index = m_RasterizerContext->GetDynamicVertexBuffer(
            vertexFormat, VertexCount, vertexSize, (Flags & CKRST_DP_DOCLIP) != 0);

        if (m_RasterizerContext->GetVertexBufferData(index)) {
            m_VertexBufferIndex = index;
            m_StartIndex = (CKDWORD)-1;
            m_DpFlags = Flags;
            return LockCurrentVB(VertexCount);
        }
    }

    // Fall back to user draw primitive data
    m_DpFlags = 0;
    m_VertexBufferIndex = 0;
    m_StartIndex = (CKDWORD)-1;
    m_VertexBufferCount = 0;

    // IDA: 0x1006bdb1 - call UserDrawPrimitiveDataClass::GetStructure
    // Note: DpFlags mask 0xEFFFFFFF removes CKRST_DP_VBUFFER flag
    return m_UserDrawPrimitiveData->GetStructure((CKRST_DPFLAGS)(Flags & 0xEFFFFFFF), VertexCount);
}

CKWORD *RCKRenderContext::GetDrawPrimitiveIndices(int IndicesCount) {
    // IDA: 0x1006bf52 - uses UserDrawPrimitiveDataClass::GetIndices
    return m_UserDrawPrimitiveData ? m_UserDrawPrimitiveData->GetIndices(IndicesCount) : nullptr;
}

void RCKRenderContext::Transform(VxVector *Dest, VxVector *Src, CK3dEntity *Ref) {
    // IDA: 0x1006bf71
    if (!m_RasterizerContext)
        return;

    VxVector4 screenResult;
    VxTransformData transformData;
    memset(&transformData, 0, sizeof(transformData));
    transformData.ClipFlags = 0;
    transformData.InStride = 16;  // sizeof(VxVector4) - actually VxVector is 12 bytes, but IDA says 16
    transformData.InVertices = Src;
    transformData.OutStride = 0;
    transformData.OutVertices = NULL;
    transformData.ScreenStride = 16;  // sizeof(VxVector4)
    transformData.ScreenVertices = &screenResult;

    if (Ref) {
        m_RasterizerContext->SetTransformMatrix(VXMATRIX_WORLD, Ref->GetWorldMatrix());
    }

    m_RasterizerContext->TransformVertices(1, &transformData);
    *Dest = screenResult;
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
    if (m_RasterizerContext) {
        savedSettings.m_Rect.left = m_RasterizerContext->m_PosX;
        savedSettings.m_Rect.top = m_RasterizerContext->m_PosY;
        savedSettings.m_Rect.right = m_RasterizerContext->m_Width;
        savedSettings.m_Rect.bottom = m_RasterizerContext->m_Height;
        savedSettings.m_Bpp = m_RasterizerContext->m_Bpp;
        savedSettings.m_Zbpp = m_RasterizerContext->m_ZBpp;
        savedSettings.m_StencilBpp = m_RasterizerContext->m_StencilBpp;
    }
    m_FullscreenSettings = savedSettings;

    // Save window parent and position
    m_AppHandle = (CKDWORD)VxGetParent((void *)m_WinHandle);
    VxGetWindowRect((void *)m_WinHandle, &m_WinRect);
    VxScreenToClient((void *)m_AppHandle, (CKPOINT *)&m_WinRect.left);
    VxScreenToClient((void *)m_AppHandle, (CKPOINT *)&m_WinRect.right);

    // Destroy current device
    DestroyDevice();

    // Create fullscreen device
    CKRECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = Width;
    rect.bottom = Height;

    err = Create((void *)m_WinHandle, Driver, &rect, TRUE, Bpp, 0, 0, RefreshRate);

    if (err != CK_OK) {
        // Failed - restore window
        VxSetParent((void *)m_WinHandle, (void *)m_AppHandle);
        VxMoveWindow((void *)m_WinHandle, m_WinRect.left, m_WinRect.top,
                     m_WinRect.right - m_WinRect.left, m_WinRect.bottom - m_WinRect.top, FALSE);

        // Try to recreate old context
        CKRECT oldRect;
        oldRect.left = m_FullscreenSettings.m_Rect.left;
        oldRect.top = m_FullscreenSettings.m_Rect.top;
        oldRect.right = m_FullscreenSettings.m_Rect.right + oldRect.left;
        oldRect.bottom = m_FullscreenSettings.m_Rect.bottom + oldRect.top;

        Create((void *)m_WinHandle, m_DriverIndex, &oldRect, FALSE,
               m_FullscreenSettings.m_Bpp, m_FullscreenSettings.m_Zbpp,
               m_FullscreenSettings.m_StencilBpp, 0);
    } else {
        // Success
        m_RenderedScene->UpdateViewportSize(TRUE, CK_RENDER_USECURRENTSETTINGS);
        Clear((CK_RENDER_FLAGS)(CK_RENDER_CLEARBACK | CK_RENDER_CLEARZ | CK_RENDER_CLEARSTENCIL), 0);
        BackToFront(CK_RENDER_USECURRENTSETTINGS);
        Clear((CK_RENDER_FLAGS)(CK_RENDER_CLEARBACK | CK_RENDER_CLEARZ | CK_RENDER_CLEARSTENCIL), 0);
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
    VxSetParent((void *)m_WinHandle, (void *)m_AppHandle);
    VxMoveWindow((void *)m_WinHandle, m_WinRect.left, m_WinRect.top,
                 m_WinRect.right - m_WinRect.left, m_WinRect.bottom - m_WinRect.top, FALSE);

    // Recreate windowed context with saved settings
    CKRECT rect;
    rect.left = m_FullscreenSettings.m_Rect.left;
    rect.top = m_FullscreenSettings.m_Rect.top;
    rect.right = m_FullscreenSettings.m_Rect.right + rect.left;
    rect.bottom = m_FullscreenSettings.m_Rect.bottom + rect.top;

    CKERROR err = Create((void *)m_WinHandle, m_DriverIndex, &rect, FALSE,
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
    RC_DEBUG_LOG_FMT("GetDriverIndex called, returning %d", m_DriverIndex);
    return m_DriverIndex;
}

CKBOOL RCKRenderContext::ChangeDriver(int NewDriver) {
    // IDA: 0x1006765b
    RC_DEBUG_LOG_FMT("ChangeDriver called, NewDriver=%d", NewDriver);
    
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
    if ((newDriver->m_2DCaps.Caps & 1) == 0)
        return FALSE;
    
    // Save current settings for fallback
    if (m_RasterizerContext) {
        m_FullscreenSettings.m_Rect.left = m_RasterizerContext->m_PosX;
        m_FullscreenSettings.m_Rect.top = m_RasterizerContext->m_PosY;
        m_FullscreenSettings.m_Rect.right = m_RasterizerContext->m_Width;
        m_FullscreenSettings.m_Rect.bottom = m_RasterizerContext->m_Height;
        m_FullscreenSettings.m_Bpp = m_RasterizerContext->m_Bpp;
        m_FullscreenSettings.m_Zbpp = m_RasterizerContext->m_ZBpp;
        m_FullscreenSettings.m_StencilBpp = m_RasterizerContext->m_StencilBpp;
    }
    
    m_DeviceValid = TRUE;
    
    // Notify render manager we're destroying device
    m_RenderManager->DestroyingDevice((CKRenderContext *)this);
    
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
    
    // Copy settings
    m_RasterizerContext->m_EnableScreenDump = m_RenderManager->m_EnableScreenDump.Value;
    m_RasterizerContext->m_Antialias = m_RenderManager->m_Antialias.Value;
    
    // Try to create the context with current settings
    BOOL created = m_RasterizerContext->Create(
        (WIN_HANDLE)m_WinHandle,
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
        
        if (m_RasterizerContext)
            m_RasterizerContext->SetTransparentMode(m_TransparentMode);
        
        m_DeviceValid = FALSE;
        return TRUE;
    } else {
        // Failed - restore old driver
        m_RasterizerDriver->DestroyContext(m_RasterizerContext);
        m_RasterizerDriver = oldDriver;
        m_RasterizerContext = m_RasterizerDriver->CreateContext();
        m_RasterizerContext->m_Antialias = m_RenderManager->m_Antialias.Value;
        
        BOOL restored = m_RasterizerContext->Create(
            (WIN_HANDLE)m_WinHandle,
            m_FullscreenSettings.m_Rect.left, m_FullscreenSettings.m_Rect.top,
            m_FullscreenSettings.m_Rect.right, m_FullscreenSettings.m_Rect.bottom,
            m_FullscreenSettings.m_Bpp, 0, 0,
            m_FullscreenSettings.m_Zbpp, m_FullscreenSettings.m_StencilBpp);
        
        m_DeviceValid = FALSE;
        
        if (!restored) {
            m_RasterizerDriver->DestroyContext(m_RasterizerContext);
            m_RasterizerContext = nullptr;
        }
        return FALSE;
    }
}

WIN_HANDLE RCKRenderContext::GetWindowHandle() {
    RC_DEBUG_LOG_FMT("GetWindowHandle called, returning %08X", m_WinHandle);
    return (WIN_HANDLE) m_WinHandle;
}

void RCKRenderContext::ScreenToClient(Vx2DVector *ioPoint) {
    // IDA: 0x1006c075
    VxScreenToClient((void *)m_WinHandle, (CKPOINT *)ioPoint);
}

void RCKRenderContext::ClientToScreen(Vx2DVector *ioPoint) {
    // IDA: 0x1006c096
    VxClientToScreen((void *)m_WinHandle, (CKPOINT *)ioPoint);
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
        VxClientToScreen((void *)m_WinHandle, &pt1);
        VxClientToScreen((void *)m_WinHandle, &pt2);
        rect.left = (float)pt1.x;
        rect.top = (float)pt1.y;
        rect.right = (float)pt2.x;
        rect.bottom = (float)pt2.y;
    } else {
        rect.left = (float)m_Settings.m_Rect.left;
        rect.top = (float)m_Settings.m_Rect.top;
        rect.right = (float)(m_Settings.m_Rect.right + m_Settings.m_Rect.left);
        rect.bottom = (float)(m_Settings.m_Rect.bottom + m_Settings.m_Rect.top);
    }
}

int RCKRenderContext::GetHeight() {
    // IDA: 0x1006ce44
    RC_DEBUG_LOG("GetHeight called");
    return m_Settings.m_Rect.bottom;
}

int RCKRenderContext::GetWidth() {
    // IDA: 0x1006ce30
    RC_DEBUG_LOG("GetWidth called");
    return m_Settings.m_Rect.right;
}

CKERROR RCKRenderContext::Resize(int PosX, int PosY, int SizeX, int SizeY, CKDWORD Flags) {
    // IDA: 0x1006cb04
    RC_DEBUG_LOG_FMT("Resize called: %d,%d %dx%d flags=%u", PosX, PosY, SizeX, SizeY, Flags);

    if (m_DeviceValid)
        return CKERR_INVALIDRENDERCONTEXT;

    // If no rasterizer context, try to create one
    if (!m_RasterizerContext) {
        if (SizeX && SizeY) {
            CKRECT rect;
            rect.left = PosX;
            rect.top = PosY;
            rect.bottom = SizeY + PosY;
            rect.right = SizeX + PosX;
            Create((void *)m_WinHandle, m_DriverIndex, &rect, FALSE, -1, -1, -1, 0);
        } else {
            Create((void *)m_WinHandle, m_DriverIndex, nullptr, FALSE, -1, -1, -1, 0);
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
            VxGetClientRect((void *)m_WinHandle, &clientRect);
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
    RC_DEBUG_LOG_FMT("SetViewRect called: %f,%f - %f,%f", rect.left, rect.top, rect.right, rect.bottom);
    m_ViewportData.ViewX = (int)rect.left;
    m_ViewportData.ViewY = (int)rect.top;
    m_ViewportData.ViewWidth = (int)rect.GetWidth();
    m_ViewportData.ViewHeight = (int)rect.GetHeight();
    UpdateProjection(TRUE);
}

void RCKRenderContext::GetViewRect(VxRect &rect) {
    // IDA: 0x1006cec2
    RC_DEBUG_LOG("GetViewRect called");
    rect.left = (float)m_ViewportData.ViewX;
    rect.top = (float)m_ViewportData.ViewY;
    rect.right = (float)m_ViewportData.ViewWidth;
    rect.bottom = (float)m_ViewportData.ViewHeight;
}

VX_PIXELFORMAT RCKRenderContext::GetPixelFormat(int *Bpp, int *Zbpp, int *StencilBpp) {
    // IDA: 0x100675f2
    RC_DEBUG_LOG("GetPixelFormat called");
    if (Bpp) *Bpp = m_Settings.m_Bpp;
    if (Zbpp) *Zbpp = m_Settings.m_Zbpp;
    if (StencilBpp) *StencilBpp = m_Settings.m_StencilBpp;

    // Return pixel format from rasterizer context
    if (m_RasterizerContext)
        return m_RasterizerContext->m_PixelFormat;

    // Fallback based on bpp
    if (m_Settings.m_Bpp == 32) return _32_ARGB8888;
    if (m_Settings.m_Bpp == 24) return _24_RGB888;
    if (m_Settings.m_Bpp == 16) return _16_RGB565;
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
    return (VXFOG_MODE)m_RenderedScene->m_FogMode;
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

CKBOOL RCKRenderContext::DrawPrimitive(VXPRIMITIVETYPE pType, CKWORD *indices, int indexcount,
                                       VxDrawPrimitiveData *data) {
    // IDA: 0x1006b314
    if (!data)
        return FALSE;
    if (data->VertexCount <= 0)
        return FALSE;

    // Set lighting mode based on normals
    if ((data->Flags & CKRST_DP_LIGHT) != 0 && data->NormalPtr) {
        m_RasterizerContext->SetRenderState(VXRENDERSTATE_LIGHTING, 1);
    } else {
        // Enable diffuse color flag if color pointer exists
        if (data->SpecularColorPtr)
            data->Flags |= CKRST_DP_SPECULAR;
        if (data->ColorPtr)
            data->Flags |= CKRST_DP_DIFFUSE;
        m_RasterizerContext->SetRenderState(VXRENDERSTATE_LIGHTING, 0);
    }

    // If no indices, use vertex count
    if (!indices)
        indexcount = data->VertexCount;

    // Update stats based on primitive type
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

    // Check if using vertex buffer
    if ((data->Flags & CKRST_DP_VBUFFER) == 0 || !m_VertexBufferIndex) {
        return m_RasterizerContext->DrawPrimitive(pType, indices, indexcount, data);
    }

    // Unlock any locked vertex buffers
    while (m_VertexBufferCount) {
        --m_VertexBufferCount;
        m_RasterizerContext->UnlockVertexBuffer(m_VertexBufferIndex);
    }

    return m_RasterizerContext->DrawPrimitiveVB(pType, m_VertexBufferIndex, m_StartIndex,
                                                 data->VertexCount, indices, indexcount);
}

void RCKRenderContext::TransformVertices(int VertexCount, VxTransformData *data, CK3dEntity *Ref) {
    if (!data)
        return;
    if (Ref) {
        m_RasterizerContext->SetTransformMatrix(VXMATRIX_WORLD, Ref->GetWorldMatrix());
    }
    m_RasterizerContext->TransformVertices(VertexCount, data);
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

// IDA: 0x1006823c - Internal 2D picking method
CK2dEntity *RCKRenderContext::_Pick2D(const Vx2DVector &pt, CKBOOL ignoreUnpickable) {
    // Adjust point to local coordinates
    Vx2DVector localPt;
    localPt.x = pt.x - (float)m_Settings.m_Rect.left;
    localPt.y = pt.y - (float)m_Settings.m_Rect.top;
    
    // Try foreground 2D root first (FALSE = foreground)
    RCK2dEntity *root = (RCK2dEntity *)Get2dRoot(FALSE);
    CK2dEntity *result = root ? root->Pick(localPt, ignoreUnpickable) : nullptr;
    if (result)
        return result;
    
    // Then try background 2D root (TRUE = background)
    root = (RCK2dEntity *)Get2dRoot(TRUE);
    return root ? root->Pick(localPt, ignoreUnpickable) : nullptr;
}

// IDA: 0x100682be - Internal 3D picking method  
CK3dEntity *RCKRenderContext::Pick3D(const Vx2DVector &pt, VxIntersectionDesc *desc, CK3dEntity *filter, CKBOOL ignoreUnpickable) {
    // Get object extents count
    int objCount = m_ObjectExtents.Size();
    if (objCount == 0)
        return nullptr;
    
    // Copy point for local use
    Vx2DVector localPt = pt;
    VxVector rayStart(0.0f, 0.0f, 0.0f);
    VxVector rayEnd;
    
    // Get viewport bounds
    float viewX = (float)m_ViewportData.ViewX;
    float viewY = (float)m_ViewportData.ViewY;
    
    // Check if point is within viewport
    if (localPt.x < viewX || localPt.y < viewY)
        return nullptr;
    
    float viewWidth = (float)m_ViewportData.ViewWidth;
    float viewHeight = (float)m_ViewportData.ViewHeight;
    
    if (localPt.x > viewX + viewWidth || localPt.y > viewY + viewHeight)
        return nullptr;
    
    // Calculate inverse view width for normalization
    float invViewWidth = 1.0f / viewWidth;
    
    // =========================================================================
    // Path 1: Use m_Extents array for pre-calculated frustum-based picking
    // IDA: lines 98-171 - uses camera-relative picking with m_Extents
    // =========================================================================
    int extCount = m_Extents.Size();
    if (extCount > 0) {
        VxIntersectionDesc tempDesc;
        memset(&tempDesc, 0, sizeof(VxIntersectionDesc));
        
        // Iterate through extents in reverse order (last rendered = front)
        for (int i = extCount - 1; i >= 0; --i) {
            CKObjectExtents *ext = &m_Extents[i];
            
            // Check if point is within extent bounds
            if (localPt.x < ext->m_Rect.left || localPt.x > ext->m_Rect.right)
                continue;
            if (localPt.y < ext->m_Rect.top || localPt.y > ext->m_Rect.bottom)
                continue;
            
            // Get the entity associated with this extent (stored as CK_ID)
            CKObject *obj = m_Context->GetObject(ext->m_Entity);
            if (!obj)
                continue;
            
            CK3dEntity *extEntity = (CK3dEntity *)obj;
            
            // Calculate picking ray based on extent and camera geometry
            float focalLen = m_FocalLength;
            if (focalLen <= 0.0f)
                focalLen = m_FarPlane;
            
            float halfFovTan = tanf(m_Fov * 0.5f);
            float extWidth = ext->m_Rect.right - ext->m_Rect.left;
            float scale = halfFovTan / (extWidth * 0.5f);
            
            // Get camera matrix for ray construction
            if (m_RenderedScene && m_RenderedScene->m_AttachedCamera) {
                const VxMatrix &camMat = m_RenderedScene->m_AttachedCamera->GetWorldMatrix();
                VxVector camRight(camMat[0][0], camMat[0][1], camMat[0][2]);
                VxVector camUp(camMat[1][0], camMat[1][1], camMat[1][2]);
                VxVector camFwd(camMat[2][0], camMat[2][1], camMat[2][2]);
                
                float extCenterX = (ext->m_Rect.left + ext->m_Rect.right) * 0.5f;
                float extCenterY = (ext->m_Rect.top + ext->m_Rect.bottom) * 0.5f;
                float offsetX = (localPt.x - extCenterX) * scale;
                float offsetY = (localPt.y - extCenterY) * scale;
                
                VxVector rayDir;
                rayDir.x = camFwd.x + camRight.x * offsetX - camUp.x * offsetY;
                rayDir.y = camFwd.y + camRight.y * offsetX - camUp.y * offsetY;
                rayDir.z = camFwd.z + camRight.z * offsetX - camUp.z * offsetY;
                rayDir.Normalize();
                
                rayStart.x = camMat[3][0];
                rayStart.y = camMat[3][1];
                rayStart.z = camMat[3][2];
                
                VxVector rayEndPt;
                rayEndPt.x = rayStart.x + rayDir.x * focalLen;
                rayEndPt.y = rayStart.y + rayDir.y * focalLen;
                rayEndPt.z = rayStart.z + rayDir.z * focalLen;
                
                float minDist = focalLen;
                CK3dEntity *pickedEntity = nullptr;
                
                // Check all object extents for ray intersection
                for (int j = 0; j < objCount; ++j) {
                    CKObjectExtents *objExt = &m_ObjectExtents[j];
                    CK3dEntity *objEntity = (CK3dEntity *)(CKDWORD)objExt->m_Entity;
                    
                    if (!objEntity)
                        continue;
                    if (filter && objEntity != (CK3dEntity *)filter)
                        continue;
                    if (!ignoreUnpickable && !objEntity->IsPickable())
                        continue;
                    
                    // Check bounding box intersection first
                    const VxBbox &bbox = objEntity->GetBoundingBox(FALSE);
                    VxRay ray;
                    ray.m_Origin = rayStart;
                    ray.m_Direction = rayDir;
                    
                    if (VxIntersect::RayBox(ray, bbox)) {
                        if (objEntity->RayIntersection(&rayStart, &rayEndPt, &tempDesc, nullptr, CKRAYINTERSECTION_SEGMENT)) {
                            if (tempDesc.TexV < minDist) {
                                memcpy(desc, &tempDesc, sizeof(VxIntersectionDesc));
                                minDist = tempDesc.TexV;
                                pickedEntity = objEntity;
                            }
                        }
                    }
                }
                
                if (pickedEntity)
                    return pickedEntity;
                
                // If extent has blocking flag (0x20), stop search
                if ((ext->m_Camera & 0x20) != 0)
                    return nullptr;
            }
        }
    }
    
    // =========================================================================
    // Path 2: Standard ray intersection using m_ObjectExtents
    // IDA: lines 173-246
    // =========================================================================
    
    float nx = (localPt.x - viewX) * 2.0f * invViewWidth - 1.0f;
    float ny = (viewY - localPt.y) * 2.0f * invViewWidth + viewHeight * invViewWidth;
    
    if (m_Perspective) {
        float halfFovTan = tanf(m_Fov * 0.5f);
        rayEnd.x = nx * m_NearPlane * halfFovTan;
        rayEnd.y = ny * m_NearPlane * halfFovTan;
    } else {
        rayEnd.x = nx / m_Zoom;
        rayEnd.y = ny / m_Zoom;
    }
    rayEnd.z = m_NearPlane;
    
    float minDistance = 1.0e30f;
    CK3dEntity *bestEntity = nullptr;
    VxIntersectionDesc tempDesc;
    
    for (int i = 0; i < objCount; ++i) {
        CKObjectExtents *ext = &m_ObjectExtents[i];
        CK3dEntity *entity = (CK3dEntity *)(CKDWORD)ext->m_Entity;
        
        if (!entity)
            continue;
        if (filter && entity != (CK3dEntity *)filter)
            continue;
        if (!ignoreUnpickable && !entity->IsPickable())
            continue;
        
        // Check if point is within the entity's 2D extent
        if (localPt.x > ext->m_Rect.right || localPt.x < ext->m_Rect.left)
            continue;
        if (localPt.y > ext->m_Rect.bottom || localPt.y < ext->m_Rect.top)
            continue;
        
        memset(&tempDesc, 0, sizeof(VxIntersectionDesc));
        VxVector transformedPt;
        
        // Check if entity has a mesh and try Pick2D first
        RCKMesh *mesh = (RCKMesh *)entity->GetCurrentMesh();
        if (mesh) {
            if (mesh->Pick2D(localPt, &tempDesc, this, (RCK3dEntity *)entity)) {
                entity->Transform(&transformedPt, &tempDesc.IntersectionPoint, m_RenderedScene->m_RootEntity);
                
                VxVector diff;
                diff.x = transformedPt.x - rayStart.x;
                diff.y = transformedPt.y - rayStart.y;
                diff.z = transformedPt.z - rayStart.z;
                float distance = diff.Magnitude();
                
                if (distance < minDistance) {
                    memcpy(desc, &tempDesc, sizeof(VxIntersectionDesc));
                    minDistance = distance;
                    bestEntity = entity;
                }
            }
        }
        
        // Try ray intersection
        CK3dEntity *refEntity = m_RenderedScene ? m_RenderedScene->m_RootEntity : nullptr;
        if (entity->RayIntersection(&rayStart, &rayEnd, &tempDesc, refEntity, CKRAYINTERSECTION_DEFAULT)) {
            if (tempDesc.TexV < minDistance) {
                memcpy(desc, &tempDesc, sizeof(VxIntersectionDesc));
                minDistance = tempDesc.TexV;
                bestEntity = entity;
            }
        }
    }
    
    return bestEntity;
}

// IDA: 0x1006810c
CKRenderObject *RCKRenderContext::Pick(int x, int y, CKPICKRESULT *oRes, CKBOOL iIgnoreUnpickable) {
    CKPOINT pt = {x, y};
    return Pick(pt, oRes, iIgnoreUnpickable);
}

// IDA: 0x1006810c
CKRenderObject *RCKRenderContext::Pick(CKPOINT pt, CKPICKRESULT *oRes, CKBOOL iIgnoreUnpickable) {
    VxIntersectionDesc desc;
    memset(&desc, 0, sizeof(VxIntersectionDesc));
    
    // Convert screen point to local coordinates (relative to render context window)
    Vx2DVector localPt;
    localPt.x = (float)(pt.x - m_Settings.m_Rect.left);
    localPt.y = (float)(pt.y - m_Settings.m_Rect.top);
    
    // Pick 3D objects first
    CK3dEntity *picked3D = Pick3D(localPt, &desc, nullptr, iIgnoreUnpickable);
    
    // Fill result structure if provided
    if (oRes) {
        // Convert local coords back to screen coords for 2D picking
        Vx2DVector screenPt;
        screenPt.x = localPt.x + (float)m_Settings.m_Rect.left;
        screenPt.y = localPt.y + (float)m_Settings.m_Rect.top;
        
        // Also pick 2D entities
        CK2dEntity *picked2D = _Pick2D(screenPt, iIgnoreUnpickable);
        
        // Copy intersection data from desc to oRes
        oRes->IntersectionPoint = desc.IntersectionPoint;
        oRes->IntersectionNormal = desc.IntersectionNormal;
        oRes->TexU = desc.TexU;
        oRes->TexV = desc.TexV;
        oRes->Distance = desc.Distance;
        oRes->FaceIndex = desc.FaceIndex;
        
        // Set Sprite to 2D entity ID if found
        if (picked2D) {
            oRes->Sprite = picked2D->GetID();
        } else {
            oRes->Sprite = 0;
        }
    }
    
    // Return 3D picked object (or null if none)
    return picked3D;
}

// Helper function for rectangle intersection test
// Returns: 0 = no intersection, 1 = fully contained, 2 = partial intersection
static int RectIntersectTest(const VxRect *a, const VxRect *b) {
    // Check for no intersection
    if (a->left >= b->right) return 0;
    if (a->right <= b->left) return 0;
    if (a->top >= b->bottom) return 0;
    if (a->bottom <= b->top) return 0;
    
    // Check if a is fully inside b
    if (a->left >= b->left && a->right <= b->right &&
        a->top >= b->top && a->bottom <= b->bottom) {
        return 1;
    }
    
    // Partial intersection
    return 2;
}

// IDA: 0x10068abc
CKERROR RCKRenderContext::RectPick(const VxRect &r, XObjectPointerArray &oObjects, CKBOOL Intersect) {
    if (!&oObjects)
        return CKERR_INVALIDPARAMETER;
    
    // Copy rect and adjust to local coordinates
    VxRect pickRect = r;
    float offsetX = (float)m_Settings.m_Rect.left;
    float offsetY = (float)m_Settings.m_Rect.top;
    pickRect.left -= offsetX;
    pickRect.right -= offsetX;
    pickRect.top -= offsetY;
    pickRect.bottom -= offsetY;
    
    // Normalize rect
    if (pickRect.left > pickRect.right) {
        float t = pickRect.left;
        pickRect.left = pickRect.right;
        pickRect.right = t;
    }
    if (pickRect.top > pickRect.bottom) {
        float t = pickRect.top;
        pickRect.top = pickRect.bottom;
        pickRect.bottom = t;
    }
    
    // Iterate through 3D entities in rendered scene
    if (m_RenderedScene) {
        int count = m_RenderedScene->m_3DEntities.Size();
        for (int i = 0; i < count; ++i) {
            CK3dEntity *ent = m_RenderedScene->m_3DEntities[i];
            if (!ent)
                continue;
            
            // Check if not pickable
            if (!ent->IsPickable())
                continue;
            
            // Check if visible
            if (!ent->IsVisible())
                continue;
            
            // Get screen extents
            VxRect extRect;
            ent->GetRenderExtents(extRect);
            
            // Test intersection
            int result = RectIntersectTest(&extRect, &pickRect);
            if (result) {
                // result 1 = fully contained, result 2 = partial intersection
                if (!Intersect || result != 2) {
                    oObjects.Insert(0, ent);
                }
            }
        }
    }
    
    // Iterate through background 2D entities (Get2dRoot(TRUE))
    CK2dEntity *root2D = Get2dRoot(TRUE);
    CK2dEntity *ent2D = root2D ? root2D->HierarchyParser(root2D) : nullptr;
    while (ent2D) {
        // Check visible and pickable
        if (!(ent2D->GetObjectFlags() & CK_OBJECT_NOTTOBELISTEDANDSAVED) &&
            !(ent2D->GetFlags() & CK_2DENTITY_NOTPICKABLE)) {
            
            VxRect homogRect, extRect;
            ent2D->GetExtents(homogRect, extRect);
            
            int result = RectIntersectTest(&extRect, &pickRect);
            if (result) {
                if (!Intersect || result != 2) {
                    oObjects.Insert(0, ent2D);
                }
            }
        }
        ent2D = root2D->HierarchyParser(ent2D);
    }
    
    // Iterate through foreground 2D entities (Get2dRoot(FALSE))
    root2D = Get2dRoot(FALSE);
    ent2D = root2D ? root2D->HierarchyParser(root2D) : nullptr;
    while (ent2D) {
        if (!(ent2D->GetObjectFlags() & CK_OBJECT_NOTTOBELISTEDANDSAVED) &&
            !(ent2D->GetFlags() & CK_2DENTITY_NOTPICKABLE)) {
            
            VxRect homogRect, extRect;
            ent2D->GetExtents(homogRect, extRect);
            
            int result = RectIntersectTest(&extRect, &pickRect);
            if (result) {
                if (!Intersect || result != 2) {
                    oObjects.Insert(0, ent2D);
                }
            }
        }
        ent2D = root2D->HierarchyParser(ent2D);
    }
    
    return CK_OK;
}

void RCKRenderContext::AttachViewpointToCamera(CKCamera *cam) {
    // IDA: 0x1006b92d
    if (cam) {
        cam->ModifyObjectFlags(0, 0x400); // Clear some flag
        m_RenderedScene->m_AttachedCamera = cam;
        
        // Copy camera's world matrix to root entity
        const VxMatrix &worldMat = cam->GetWorldMatrix();
        m_RenderedScene->m_RootEntity->SetWorldMatrix(worldMat);
        
        // Check if camera has flag 0x2000 set
        if (cam->GetFlags() & 0x2000) {
            SetFullViewport(&m_ViewportData, m_Settings.m_Rect.right, m_Settings.m_Rect.bottom);
        }
    }
}

void RCKRenderContext::DetachViewpointFromCamera() {
    m_RenderedScene->m_AttachedCamera = nullptr;
}

CKCamera *RCKRenderContext::GetAttachedCamera() {
    // IDA: 0x1006b919
    return (CKCamera *)m_RenderedScene->m_AttachedCamera;
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
    // IDA: 0x1006b6f0
    if (mat) {
        mat->SetAsCurrent(this, Lit, 0);
    } else {
        if (m_RasterizerContext)
            m_RasterizerContext->SetTexture(0, 0);
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
    // IDA: 0x1006bb75
    // Only return DirectX info if Family is 0 (DirectX family)
    if (m_RasterizerContext && m_RasterizerContext->m_Driver->m_2DCaps.Family == 0) {
        return (VxDirectXData *)m_RasterizerContext->GetImplementationSpecificData();
    }
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

    m_CubeMapFace = static_cast<CKRST_CUBEFACE>(CubeMapFace);

    CKDWORD textureIndex = 0;
    int width = 0;
    int height = 0;

    if (texture) {
        textureIndex = texture->GetRstTextureIndex();
        width = texture->GetWidth();
        height = texture->GetHeight();
        // Check if texture is a movie with certain flags - height becomes -1
        // (This is simplified - original checks movie info flags)
    }

    CKBOOL result = TRUE;
    if (!m_TargetTexture || texture) {
        result = m_RasterizerContext->SetTargetTexture(textureIndex, width, height, (CKRST_CUBEFACE) CubeMapFace);
    } else {
        m_TargetTexture = NULL;
    }

    if (!result) {
        // If SetTargetTexture failed, check for COPYTEXTURE capability as fallback
        if (!(m_RasterizerDriver->m_3DCaps.CKRasterizerSpecificCaps & CKRST_SPECIFICCAPS_COPYTEXTURE))
            return FALSE;
        m_TargetTexture = (RCKTexture *)texture;
        result = TRUE;
    }

    if (texture) {
        // Update settings and viewport to texture dimensions
        m_Settings.m_Rect.left = 0;
        m_Settings.m_Rect.top = 0;
        m_Settings.m_Rect.right = texture->GetWidth();
        m_Settings.m_Rect.bottom = texture->GetHeight();

        m_ViewportData.ViewX = 0;
        m_ViewportData.ViewY = 0;
        m_ViewportData.ViewWidth = texture->GetWidth();
        m_ViewportData.ViewHeight = texture->GetHeight();

        m_RasterizerContext->SetRenderState(VXRENDERSTATE_TEXTURETARGET, 1);
        UpdateProjection(TRUE);
    } else {
        // Restore settings from rasterizer context
        // Note: m_Rect stores (left, top, width, height) not (left, top, right, bottom)
        m_Settings.m_Rect.left = m_RasterizerContext->m_PosX;
        m_Settings.m_Rect.top = m_RasterizerContext->m_PosY;
        m_Settings.m_Rect.right = m_RasterizerContext->m_Width;
        m_Settings.m_Rect.bottom = m_RasterizerContext->m_Height;

        SetFullViewport(&m_ViewportData, m_Settings.m_Rect.right, m_Settings.m_Rect.bottom);
        m_RasterizerContext->SetRenderState(VXRENDERSTATE_TEXTURETARGET, 0);
        UpdateProjection(TRUE);
    }

    return result;
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
    // IDA: 0x1006bdbc
    CKVertexBufferDesc *vbDesc = m_RasterizerContext->GetVertexBufferData(m_VertexBufferIndex);
    if (!vbDesc)
        return NULL;
    if (!m_DpFlags)
        return NULL;

    CKRST_LOCKFLAGS lockFlags;
    if (vbDesc->m_CurrentVCount + VertexCount <= vbDesc->m_MaxVertexCount) {
        lockFlags = CKRST_LOCK_NOOVERWRITE;
        m_StartIndex = vbDesc->m_CurrentVCount;
        vbDesc->m_CurrentVCount += VertexCount;
    } else {
        lockFlags = CKRST_LOCK_DISCARD;
        vbDesc->m_CurrentVCount = VertexCount;
        m_StartIndex = 0;
    }

    void *lockedPtr = m_RasterizerContext->LockVertexBuffer(m_VertexBufferIndex, m_StartIndex, VertexCount, lockFlags);

    m_UserDrawPrimitiveData->Flags = m_DpFlags;
    m_UserDrawPrimitiveData->VertexCount = VertexCount;
    CKRSTSetupDPFromVertexBuffer((CKBYTE *)lockedPtr, vbDesc, *m_UserDrawPrimitiveData);

    ++m_VertexBufferCount;
    return m_UserDrawPrimitiveData;
}

CKBOOL RCKRenderContext::ReleaseCurrentVB() {
    // IDA: 0x1006bf09
    if (!m_VertexBufferIndex)
        return FALSE;
    while (m_VertexBufferCount) {
        --m_VertexBufferCount;
        m_RasterizerContext->UnlockVertexBuffer(m_VertexBufferIndex);
    }
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
    // IDA: 0x1006711b
    RC_DEBUG_LOG_FMT("Create called - Window=%p, Driver=%d, Fullscreen=%d, Bpp=%d", Window, Driver, Fullscreen, Bpp);

    // Initialize timing and stereo parameters
    m_SmoothedFps = 0.0f;
    m_TimeFpsCalc = 0;
    m_RenderTimeProfiler.Reset();
    m_FocalLength = 0.40000001f;
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
        return CK_OK;  // IDA returns CK_OK here, not an error

    m_WinHandle = (CKDWORD)Window;

    // Get rect from parameter or window client rect
    CKRECT localRect;
    if (rect) {
        localRect = *rect;
    } else {
        VxGetClientRect((void *)m_WinHandle, &localRect);
    }

    // Set driver index for non-fullscreen mode
    if (!Fullscreen) {
        m_DriverIndex = Driver;
    }

    m_DeviceValid = TRUE;

    // For fullscreen, reparent window to desktop
    if (Fullscreen) {
        m_AppHandle = (CKDWORD)VxGetParent((void *)m_WinHandle);
        VxSetParent((void *)m_WinHandle, nullptr);
        if (!VxMoveWindow((void *)m_WinHandle, 0, 0, localRect.right, localRect.bottom, FALSE)) {
            m_DeviceValid = FALSE;
            return CKERR_INVALIDOPERATION;
        }
    }

    // Set viewport
    int width = localRect.right - localRect.left;
    int height = localRect.bottom - localRect.top;
    SetFullViewport(&m_ViewportData, width, height);

    // Create rasterizer context
    m_RasterizerContext = m_RasterizerDriver->CreateContext();

    // Apply render manager settings
    m_RasterizerContext->m_Antialias = (m_RenderManager->m_Antialias.Value != 0);
    m_RasterizerContext->m_EnableScreenDump = (m_RenderManager->m_EnableScreenDump.Value != 0);
    m_RasterizerContext->m_EnsureVertexShader = (m_RenderManager->m_EnsureVertexShader.Value != 0);

    // Create the actual rasterizer context
    if (!m_RasterizerContext->Create((WIN_HANDLE)m_WinHandle, localRect.left, localRect.top,
                                     width, height, Bpp, Fullscreen, RefreshRate, Zbpp, StencilBpp)) {
        m_RasterizerDriver->DestroyContext(m_RasterizerContext);
        m_RasterizerContext = nullptr;
        m_DeviceValid = FALSE;
        return CKERR_CANCREATERENDERCONTEXT;
    }

    // Set transparent mode
    if (m_RasterizerContext)
        m_RasterizerContext->SetTransparentMode(m_TransparentMode);

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

    m_DeviceValid = FALSE;
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
        m_WinRect.bottom = m_FullscreenSettings.m_Rect.bottom;  // IDA bug: sets .right again, we fix it
    }

    // Calculate stencil free mask
    if (StencilBpp > 0) {
        m_StencilFreeMask = (1 << StencilBpp) - 1;
    } else {
        m_StencilFreeMask = 0;
    }

    RC_DEBUG_LOG_FMT("Create: returning CK_OK (fullscreen=%d, %dx%d)", Fullscreen, width, height);
    return CK_OK;
}

RCKRenderContext::RCKRenderContext(CKContext *Context, CKSTRING name) : CKRenderContext(Context, name) {
    // IDA: 0x10066952
    // Note: CKCallbacksContainer constructors are called implicitly by member initialization
    // Note: VxFrustum, VxMatrix, VxRect constructors are called implicitly
    // Note: CKRenderContextSettings constructors called implicitly
    // Note: VxTimeProfiler constructors called implicitly
    // Note: XString constructors called implicitly
    // Note: XArray constructors called implicitly
    
    m_Current3dEntity = nullptr;
    m_RenderManager = (RCKRenderManager *) Context->GetRenderManager();
    m_MaskFree = m_RenderManager->GetRenderContextMaskFree();
    if (!m_MaskFree) {
        m_Context->OutputToConsole((CKSTRING)"Error: no more render context mask available", TRUE);
    }
    m_WinHandle = 0;
    m_AppHandle = 0;
    m_RenderFlags = 255;  // IDA: 0x10066ba3
    
    // Create RenderedScene
    m_RenderedScene = new CKRenderedScene(this);
    
    // Create UserDrawPrimitiveData
    m_UserDrawPrimitiveData = new UserDrawPrimitiveDataClass();
    
    m_Fullscreen = FALSE;
    m_Active = FALSE;
    m_Perspective = TRUE;  // IDA: m_Perspective = 1
    m_ProjectionUpdated = FALSE;
    m_DeviceValid = FALSE;
    m_Start = FALSE;
    m_TransparentMode = FALSE;
    m_RasterizerContext = nullptr;
    m_RasterizerDriver = nullptr;
    // NOTE: m_Driver removed - only m_DriverIndex exists in IDA structure
    m_DriverIndex = 0;
    m_DisplayWireframe = FALSE;
    m_TextureEnabled = TRUE;
    m_Shading = GouraudShading;  // IDA: 2
    m_Zoom = 1.0f;
    m_NearPlane = 1.0f;
    m_FarPlane = 4000.0f;  // IDA: 4000.0
    m_Fov = 0.78539819f;   // IDA: PI/4
    
    // Initialize viewport
    m_ViewportData.ViewX = 0;
    m_ViewportData.ViewY = 0;
    m_ViewportData.ViewWidth = 0;
    m_ViewportData.ViewHeight = 0;
    m_ViewportData.ViewZMin = 0.0f;
    m_ViewportData.ViewZMax = 1.0f;  // IDA: 1.0
    
    // Allocate object extents array - IDA uses XVoidArray
    // m_ObjectExtents is now XVoidArray, need to allocate differently
    // IDA: XArray<CKObjectExtents>::Allocate(&this->m_ObjectExtents, 500);
    // For XVoidArray, we need to resize it
    m_ObjectExtents.Resize(500);
    
    // Clear stats
    memset(&m_Stats, 0, sizeof(m_Stats));
    
    // Initialize window rect
    m_WinRect.left = -1;  // IDA
    m_WinRect.right = -1;  // IDA
    m_WinRect.top = 0;
    m_WinRect.bottom = 0;
    
    // Initialize current extents
    m_CurrentExtents.left = 1000000.0f;   // IDA
    m_CurrentExtents.top = 1000000.0f;    // IDA
    m_CurrentExtents.bottom = -1000000.0f;  // IDA
    m_CurrentExtents.right = -1000000.0f;   // IDA
    
    m_FpsFrameCount = 0;
    m_TimeFpsCalc = 0;
    m_SmoothedFps = 0.0f;
    m_Flags = 0;
    m_SceneTraversalCalls = 0;
    m_TargetTexture = nullptr;
    m_CubeMapFace = CKRST_CUBEFACE_XPOS;  // IDA: 0
    m_DrawSceneCalls = 0;
    m_SortTransparentObjects = 0;
    m_FocalLength = 0.40000001f;  // IDA: 0.40000001
    m_EyeSeparation = 100.0f;     // IDA: 100.0
    m_Camera = nullptr;
    m_PVInformation = (CKDWORD)-1;  // IDA: -1
    m_NCUTex = nullptr;
    m_DpFlags = 0;
    m_VertexBufferCount = 0;
    m_VertexBufferIndex = 0;
    m_StartIndex = (CKDWORD)-1;  // IDA: -1
    
    // Additional fields initialization
    m_StencilFreeMask = 0;
    m_FpsInterval = 0;
    memset(&m_Settings, 0, sizeof(m_Settings));
    memset(&m_FullscreenSettings, 0, sizeof(m_FullscreenSettings));
    m_ProjectionMatrix.Identity();
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
    
    // The remaining cleanup (XArray destructors, XString destructors, etc.)
    // will be handled automatically by their destructors
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
    m_DeviceValid = TRUE;
    
    // Notify render manager that device is being destroyed
    if (m_RenderManager)
        m_RenderManager->DestroyingDevice(this);
    
    // Destroy the rasterizer context
    if (m_RasterizerDriver)
        m_RasterizerDriver->DestroyContext(m_RasterizerContext);
    
    m_RasterizerContext = nullptr;
    m_RasterizerDriver = nullptr;
    m_DeviceValid = FALSE;
    m_Fullscreen = FALSE;
    
    return TRUE;
}

void RCKRenderContext::ClearCallbacks() {
    // Based on IDA at 0x1006d7a2
    RCKRenderManager *rm = (RCKRenderManager *)m_Context->GetRenderManager();
    if (rm) {
        rm->RemoveTemporaryCallback(&m_PreRenderCallBacks);
        rm->RemoveTemporaryCallback(&m_PostSpriteRenderCallBacks);
        rm->RemoveTemporaryCallback(&m_PostRenderCallBacks);
    }
    
    // Clear callback containers
    m_PostRenderCallBacks.Clear();
    m_PreRenderCallBacks.Clear();
    m_PostSpriteRenderCallBacks.Clear();
}

void RCKRenderContext::OnClearAll() {
    // Clear all render context state
    m_RootObjects.Clear();
    m_TransparentObjects.Clear();
    m_Sprite3DBatches.Clear();
    m_ObjectExtents.Clear();

    // Reset camera
    m_Camera = nullptr;
    m_Current3dEntity = nullptr;
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
    // IDA: 0x1006c68d
    if (!forceUpdate && m_ProjectionUpdated)
        return;
    
    if (!m_RasterizerContext)
        return;
    
    float aspect = (float)m_ViewportData.ViewWidth / (float)m_ViewportData.ViewHeight;
    
    if (m_Perspective) {
        m_ProjectionMatrix.Perspective(m_Fov, aspect, m_NearPlane, m_FarPlane);
    } else {
        m_ProjectionMatrix.Orthographic(m_Zoom, aspect, m_NearPlane, m_FarPlane);
    }
    
    m_RasterizerContext->SetTransformMatrix(VXMATRIX_PROJECTION, m_ProjectionMatrix);
    m_RasterizerContext->SetViewport(&m_ViewportData);
    m_ProjectionUpdated = TRUE;
    
    // Update 2D root extents
    float right = (float)m_Settings.m_Rect.right;
    float bottom = (float)m_Settings.m_Rect.bottom;
    VxRect rect(0.0f, 0.0f, right, bottom);
    
    CK2dEntity *root2d = Get2dRoot(TRUE);
    if (root2d) {
        root2d->SetRect(rect);
    }
    root2d = Get2dRoot(FALSE);
    if (root2d) {
        root2d->SetRect(rect);
    }
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
    // Process all pending Sprite3D batches from materials
    
    int batchCount = m_Sprite3DBatches.Size();
    if (batchCount == 0)
        return;
    
    // Disable lighting and wrap mode for sprite rendering
    m_RasterizerContext->SetRenderState(VXRENDERSTATE_LIGHTING, 0);
    m_RasterizerContext->SetRenderState(VXRENDERSTATE_WRAP0, 0);
    
    // Set identity world matrix
    m_RasterizerContext->SetTransformMatrix(VXMATRIX_WORLD, VxMatrix::Identity());
    
    // Setup draw primitive data structure for batch rendering
    VxDrawPrimitiveData dpData;
    memset(&dpData, 0, sizeof(dpData));
    dpData.PositionStride = 32;      // sizeof(CKVertex)
    dpData.ColorStride = 32;
    dpData.SpecularColorStride = 32;
    dpData.TexCoordStride = 32;
    
    // Iterate through all materials with pending batches
    for (int i = 0; i < batchCount; ++i) {
        RCKMaterial *material = m_Sprite3DBatches[i];
        if (!material || !material->m_Sprite3DBatch)
            continue;
        
        CKSprite3DBatch *batch = material->m_Sprite3DBatch;
        
        // Calculate colors from material
        CKDWORD diffuseColor = RGBAFTOCOLOR(&material->m_MaterialData.Diffuse);
        CKDWORD specularColor = RGBAFTOCOLOR(&material->m_SpecularColor) | 0xFF000000;
        CKDWORD colors[2] = { diffuseColor, specularColor };
        
        // Get sprite count (vertices / 4, since each sprite has 4 vertices)
        int spriteCount = batch->m_Vertices.Size() >> 2;  // Divide by 4
        if (spriteCount == 0)
            continue;
        
        // Update statistics
        m_Stats.NbObjectDrawn += spriteCount;
        m_Stats.NbTrianglesDrawn += 2 * spriteCount;      // 2 triangles per sprite
        m_Stats.NbVerticesProcessed += 4 * spriteCount;   // 4 vertices per sprite
        
        // Set material as current
        material->SetAsCurrent(this, FALSE, FALSE);
        
        // Get vertex data pointer
        CKVertex *vertices = batch->m_Vertices.Begin();
        
        // Fill vertex colors using VxFillStructure
        // This fills the Diffuse and Specular fields of each vertex
        VxFillStructure(4 * spriteCount, &vertices->Diffuse, 32, 8, colors);
        
        // Set vertex count
        batch->m_VertexCount = 4 * spriteCount;
        
        // Calculate index count (6 indices per sprite for 2 triangles)
        int indexCount = 6 * spriteCount;
        
        // Resize index buffer if needed
        batch->m_Indices.Resize(indexCount);
        
        CKWORD *indices = batch->m_Indices.Begin();
        
        // Generate indices if we need more than currently allocated
        if (indexCount > (int)batch->m_IndexCount) {
            int v = 0;
            for (int j = 0; j < spriteCount; ++j) {
                // First triangle: 0, 1, 2
                indices[0] = (CKWORD)v;
                indices[1] = (CKWORD)(v + 1);
                indices[2] = (CKWORD)(v + 2);
                // Second triangle: 0, 2, 3
                indices[3] = (CKWORD)v;
                indices[4] = (CKWORD)(v + 2);
                indices[5] = (CKWORD)(v + 3);
                v += 4;
                indices += 6;
            }
            batch->m_IndexCount = indexCount;
            indices = batch->m_Indices.Begin();
        }
        
        // Setup draw primitive flags
        dpData.Flags = CKRST_DP_TR_VCST;  // Transformed vertices with color, specular, texture
        if (batch->m_Flags)
            dpData.Flags |= CKRST_DP_DOCLIP;
        
        // Setup vertex pointers
        dpData.VertexCount = 4 * spriteCount;
        dpData.PositionPtr = vertices;
        dpData.ColorPtr = &vertices->Diffuse;
        dpData.SpecularColorPtr = &vertices->Specular;
        dpData.TexCoordPtr = &vertices->tu;
        
        // Draw the batch
        m_RasterizerContext->DrawPrimitive(VX_TRIANGLELIST, indices, indexCount, &dpData);
        
        // Clear material's batch flag and reset batch data
        // Clear flag bit 5 (0x20) which indicates batch is pending
        material->m_Flags &= ~0x20;
        
        // Reset the batch
        batch->m_Indices.Resize(0);
        batch->m_Vertices.Resize(0);
        batch->m_VertexCount = 0;
    }
    
    // Clear all batches
    m_Sprite3DBatches.Resize(0);
}

void RCKRenderContext::AddExtents2D(const VxRect &rect, CKObject *obj) {
    if (obj) {
        // Add to object extents list
        CKObjectExtents extents;
        extents.m_Rect = rect;
        extents.m_Entity = (CKDWORD)obj;
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
        Flags = static_cast<CK_RENDER_FLAGS>(m_RenderFlags);

    m_RenderedScene->PrepareCameras(Flags);

    RC_DEBUG_LOG("PrepareCameras complete");
}

// ============================================================
// UserDrawPrimitiveDataClass implementation
// IDA: 0x1006e27e - GetStructure
// IDA: 0x1006e0f7 - ClearStructure
// IDA: 0x1006e18a - AllocateStructure
// ============================================================

VxDrawPrimitiveData *UserDrawPrimitiveDataClass::GetStructure(CKRST_DPFLAGS DpFlags, int VertexCount) {
    // IDA: 0x1006e27e
    // If requested vertex count is larger than current allocation, reallocate
    if (VertexCount > (int)m_CachedData[28]) {
        m_CachedData[28] = VertexCount;
        ClearStructure();
        AllocateStructure();
    }

    // Copy the base VxDrawPrimitiveData to cached area (0x68 bytes = 104 bytes = sizeof VxDrawPrimitiveData)
    memcpy(m_CachedData, this, 0x68);

    // Set vertex count and flags in the cached copy
    m_CachedData[0] = VertexCount;
    m_CachedData[1] = DpFlags & 0xEFFFFFFF;  // Mask out CKRST_DP_VBUFFER flag

    // Set SpecularColorPtr (m_CachedData[8]) based on flags
    if ((DpFlags & 0x20) != 0)  // CKRST_DP_SPECULAR flag
        m_CachedData[8] = (DWORD)SpecularColorPtr;
    else
        m_CachedData[8] = 0;

    // Set ColorPtr (m_CachedData[6]) based on flags
    if ((DpFlags & 0x10) != 0)  // CKRST_DP_DIFFUSE flag
        m_CachedData[6] = (DWORD)ColorPtr;
    else
        m_CachedData[6] = 0;

    return (VxDrawPrimitiveData *)m_CachedData;
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
    memset(this, 0, sizeof(VxDrawPrimitiveData));
}

void UserDrawPrimitiveDataClass::AllocateStructure() {
    // IDA: 0x1006e18a
    int maxVertices = m_CachedData[28];

    ColorPtr = VxNewAligned(4 * maxVertices, 16);      // DWORD per vertex
    SpecularColorPtr = VxNewAligned(4 * maxVertices, 16);  // DWORD per vertex
    NormalPtr = VxNewAligned(12 * maxVertices, 16);    // VxVector (12 bytes) per vertex
    PositionPtr = VxNewAligned(16 * maxVertices, 16);  // VxVector4 (16 bytes) per vertex
    TexCoordPtr = VxNewAligned(8 * maxVertices, 16);   // Vx2DVector (8 bytes) per vertex

    for (int i = 0; i < 7; ++i) {
        TexCoordPtrs[i] = VxNewAligned(8 * maxVertices, 16);  // Vx2DVector per vertex
    }

    // Copy updated pointers to cached area
    memcpy(m_CachedData, this, 0x68);
}

CKWORD *UserDrawPrimitiveDataClass::GetIndices(int IndicesCount) {
    // IDA: 0x1006e332
    // m_CachedData[26]: Indices pointer
    // m_CachedData[27]: MaxIndexCount
    if (IndicesCount > (int)m_CachedData[27]) {
        delete[] (CKWORD *)m_CachedData[26];
        m_CachedData[26] = (CKDWORD)new CKWORD[IndicesCount];
        m_CachedData[27] = IndicesCount;
    }
    return (CKWORD *)m_CachedData[26];
}

// Size check: sizeof(RCKRenderContext) = ? (should be 956)

#include "RCKRenderManager.h"
#include "RCKRenderContext.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKMesh.h"
#include "CKLevel.h"
#include "RCKTexture.h"
#include "RCKSprite.h"
#include "RCKSpriteText.h"
#include "RCKVertexBuffer.h"
#include "CKRasterizer.h"
#include "VxImageDescEx.h"
#include "VxMath.h"
#include "CKMaterial.h"
#include "CKParameterManager.h"
#include "CKDebugLogger.h"

#define CK_DEBUG_LOG(msg) CK_LOG("RenderManager", msg)
#define CK_DEBUG_LOG_FMT(fmt, ...) CK_LOG_FMT("RenderManager", fmt, __VA_ARGS__)

// External reference to rasterizer info array from CK2_3D.cpp
extern XClassArray<CKRasterizerInfo> g_RasterizersInfo;

// Helper function to update driver description from rasterizer driver
static void UpdateDriverDescCaps(VxDriverDescEx *drvDesc) {
    CKRasterizerDriver *rstDriver = drvDesc->RasterizerDriver;

    if (!rstDriver) {
        // NULL rasterizer case
        CK_DEBUG_LOG("UpdateDriverDescCaps: NULL rasterizer driver, using defaults");
        memset(&drvDesc->DriverDesc, 0, sizeof(drvDesc->DriverDesc));
        strcpy(drvDesc->DriverDesc, "NULL Rasterizer");
        strcpy(drvDesc->DriverDesc2, "NULL Rasterizer");
        drvDesc->CapsUpToDate = TRUE;
        drvDesc->Hardware = FALSE;
        drvDesc->DisplayModeCount = 1;
        drvDesc->DisplayModes = new VxDisplayMode[1];
        drvDesc->DisplayModes[0].Width = 640;
        drvDesc->DisplayModes[0].Height = 480;
        drvDesc->DisplayModes[0].Bpp = 32;
        drvDesc->DisplayModes[0].RefreshRate = 60; // Changed from 0 to 60
        drvDesc->Caps2D.Caps = 7;
        return;
    }

    // Copy caps from rasterizer driver
    drvDesc->CapsUpToDate = rstDriver->m_CapsUpToDate;
    strncpy(drvDesc->DriverDesc, rstDriver->m_Desc.CStr(), sizeof(drvDesc->DriverDesc) - 1);
    strncpy(drvDesc->DriverDesc2, rstDriver->m_Desc.CStr(), sizeof(drvDesc->DriverDesc2) - 1);
    drvDesc->Hardware = rstDriver->m_Hardware;

    CK_DEBUG_LOG_FMT("UpdateDriverDescCaps: Driver=%s, Hardware=%d", drvDesc->DriverDesc, drvDesc->Hardware);

    // Copy 3D and 2D caps
    memcpy(&drvDesc->Caps3D, &rstDriver->m_3DCaps, sizeof(Vx3DCapsDesc));
    drvDesc->Caps2D = rstDriver->m_2DCaps;

    // Copy texture formats
    int texFormatCount = rstDriver->m_TextureFormats.Size();
    drvDesc->TextureFormats.Resize(texFormatCount);
    for (int i = 0; i < texFormatCount; ++i) {
        drvDesc->TextureFormats[i] = rstDriver->m_TextureFormats[i].Format;
    }
    CK_DEBUG_LOG_FMT("UpdateDriverDescCaps: Texture formats: %d", texFormatCount);

    // Copy display modes
    int displayModeCount = rstDriver->m_DisplayModes.Size();
    drvDesc->DisplayModeCount = displayModeCount;
    if (drvDesc->DisplayModes) {
        delete[] drvDesc->DisplayModes;
    }
    drvDesc->DisplayModes = new VxDisplayMode[displayModeCount];
    memcpy(drvDesc->DisplayModes, rstDriver->m_DisplayModes.Begin(), displayModeCount * sizeof(VxDisplayMode));

    CK_DEBUG_LOG_FMT("UpdateDriverDescCaps: Display modes: %d", displayModeCount);
    // Log first few display modes for debugging
    for (int i = 0; i < displayModeCount && i < 5; ++i) {
        CK_DEBUG_LOG_FMT("  Mode[%d]: %dx%d@%dbpp %dHz", i,
                         drvDesc->DisplayModes[i].Width,
                         drvDesc->DisplayModes[i].Height,
                         drvDesc->DisplayModes[i].Bpp,
                         drvDesc->DisplayModes[i].RefreshRate);
    }
}

RCKRenderManager::RCKRenderManager(CKContext *context) : CKRenderManager(context, (CKSTRING) "Render Manager") {
    CK_DEBUG_LOG("RCKRenderManager::RCKRenderManager - Starting initialization");

    // Initialize options
    m_TextureVideoFormat.Set("TextureVideoFormat", _16_ARGB1555);
    m_SpriteVideoFormat.Set("SpriteVideoFormat", _16_ARGB1555);

    m_EnableScreenDump.Set("EnableScreenDump", FALSE);
    m_Options.PushBack(&m_EnableScreenDump);

    m_EnableDebugMode.Set("EnableDebugMode", FALSE);
    m_Options.PushBack(&m_EnableDebugMode);

    m_VertexCache.Set("VertexCache", 16);
    m_Options.PushBack(&m_VertexCache);

    m_SortTransparentObjects.Set("SortTransparentObjects", TRUE);
    m_Options.PushBack(&m_SortTransparentObjects);

    m_TextureCacheManagement.Set("TextureCacheManagement", TRUE);
    m_Options.PushBack(&m_TextureCacheManagement);

    m_UseIndexBuffers.Set("UseIndexBuffers", FALSE);
    m_Options.PushBack(&m_UseIndexBuffers);

    m_ForceLinearFog.Set("ForceLinearFog", FALSE);
    m_Options.PushBack(&m_ForceLinearFog);

    m_EnsureVertexShader.Set("EnsureVertexShader", FALSE);
    m_Options.PushBack(&m_EnsureVertexShader);

    m_ForceSoftware.Set("ForceSoftware", FALSE);
    m_Options.PushBack(&m_ForceSoftware);

    m_DisableFilter.Set("DisableFilter", FALSE);
    m_Options.PushBack(&m_DisableFilter);

    m_DisableDithering.Set("DisableDithering", FALSE);
    m_Options.PushBack(&m_DisableDithering);

    m_Antialias.Set("Antialias", 0);
    m_Options.PushBack(&m_Antialias);

    m_DisableMipmap.Set("DisableMipmap", FALSE);
    m_Options.PushBack(&m_DisableMipmap);

    m_DisableSpecular.Set("DisableSpecular", FALSE);
    m_Options.PushBack(&m_DisableSpecular);

    m_DisablePerspectiveCorrection.Set("DisablePerspectiveCorrection", FALSE);
    m_Options.PushBack(&m_DisablePerspectiveCorrection);

    m_RenderContextMaskFree = -1;
    m_Context->RegisterNewManager(this);

    // Initialize driver-related fields
    m_DriverCount = 0;
    m_Drivers = nullptr;
    m_DefaultMat = nullptr;
    m_2DRootFore = nullptr;
    m_2DRootBack = nullptr;

    // Get main window for rasterizer initialization
    WIN_HANDLE mainWindow = m_Context->GetMainWindow();
    CK_DEBUG_LOG_FMT("MainWindow handle: %p", mainWindow);

    // Start rasterizers and count available drivers
    int rstInfoCount = g_RasterizersInfo.Size();
    CK_DEBUG_LOG_FMT("Rasterizer info count: %d", rstInfoCount);

    for (CKRasterizerInfo *rstInfo = g_RasterizersInfo.Begin(); rstInfo != g_RasterizersInfo.End();) {
        CKRasterizer *rasterizer = nullptr;

        if (rstInfo->StartFct) {
            CK_DEBUG_LOG_FMT("Starting rasterizer: %s", rstInfo->Desc.CStr());
            rasterizer = rstInfo->StartFct(mainWindow);
        }

        if (rasterizer) {
            int driverCount = rasterizer->GetDriverCount();
            CK_DEBUG_LOG_FMT("Rasterizer started successfully, driver count: %d", driverCount);
            m_DriverCount += driverCount;
            m_Rasterizers.PushBack(rasterizer);
            ++rstInfo;
        } else {
            CK_DEBUG_LOG("Rasterizer failed to start, removing from list");
            // Remove failed rasterizer info - move to next without incrementing
            rstInfo = g_RasterizersInfo.Remove(rstInfo);
        }
    }

    CK_DEBUG_LOG_FMT("Total driver count: %d", m_DriverCount);

    // Link rasterizers together
    int rasterizerCount = m_Rasterizers.Size();
    for (int i = 0; i < rasterizerCount; ++i) {
        for (int j = 0; j < rasterizerCount; ++j) {
            if (i != j) {
                m_Rasterizers[i]->LinkRasterizer(m_Rasterizers[j]);
            }
        }
    }

    // Allocate driver description array
    if (m_DriverCount > 0) {
        m_Drivers = new VxDriverDescEx[m_DriverCount];
        memset(m_Drivers, 0, sizeof(VxDriverDescEx) * m_DriverCount);
    }

    CKDWORD driverId = 0;

    // First pass: enumerate hardware drivers
    for (int i = 0; i < rasterizerCount; ++i) {
        CKRasterizer *rasterizer = m_Rasterizers[i];
        int drvCount = rasterizer->GetDriverCount();

        for (int k = 0; k < drvCount; ++k) {
            CKRasterizerDriver *rstDriver = rasterizer->GetDriver(k);
            if (rstDriver && rstDriver->m_Hardware) {
                VxDriverDescEx *drvDesc = &m_Drivers[driverId];
                drvDesc->Rasterizer = rasterizer;
                drvDesc->RasterizerDriver = rstDriver;
                drvDesc->DriverId = driverId;
                UpdateDriverDescCaps(drvDesc);
                CK_DEBUG_LOG_FMT("Hardware driver %d: %s", driverId, drvDesc->DriverDesc);
                ++driverId;
            }
        }
    }

    // Second pass: enumerate software drivers
    for (int i = 0; i < rasterizerCount; ++i) {
        CKRasterizer *rasterizer = m_Rasterizers[i];
        int drvCount = rasterizer->GetDriverCount();

        for (int m = 0; m < drvCount; ++m) {
            CKRasterizerDriver *rstDriver = rasterizer->GetDriver(m);
            if (!rstDriver || !rstDriver->m_Hardware) {
                VxDriverDescEx *drvDesc = &m_Drivers[driverId];
                drvDesc->Rasterizer = rasterizer;
                drvDesc->RasterizerDriver = rstDriver;
                drvDesc->DriverId = driverId;
                UpdateDriverDescCaps(drvDesc);
                CK_DEBUG_LOG_FMT("Software driver %d: %s", driverId, drvDesc->DriverDesc);
                ++driverId;
            }
        }
    }

    // Create default material
    CK_DEBUG_LOG("About to create default material");
    m_DefaultMat = (CKMaterial *) m_Context->CreateObject(CKCID_MATERIAL, (CKSTRING) "Default Mat",
                                                          CK_OBJECTCREATION_NONAMECHECK);
    if (m_DefaultMat) {
        CK_DEBUG_LOG_FMT("Default material created at %p", m_DefaultMat);
        m_DefaultMat->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
        CK_DEBUG_LOG("Default material flags modified");
    } else {
        CK_DEBUG_LOG("Failed to create default material");
    }

    // Create 2D root entities
    CK_DEBUG_LOG("About to create 2DRootFore");
    m_2DRootFore = (CK2dEntity *) m_Context->CreateObject(CKCID_2DENTITY, (CKSTRING) "2DRootFore",
                                                          CK_OBJECTCREATION_NONAMECHECK);
    if (m_2DRootFore) {
        CK_DEBUG_LOG_FMT("2DRootFore created at %p", m_2DRootFore);
        m_2DRootFore->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
        CK_DEBUG_LOG("2DRootFore flags modified");
        ((RCK2dEntity *) m_2DRootFore)->ModifyFlags(0, CK_2DENTITY_RATIOOFFSET | CK_2DENTITY_CLIPTOCAMERAVIEW);
        CK_DEBUG_LOG("2DRootFore entity flags modified");
        m_2DRootFore->Show(CKHIDE);
        CK_DEBUG_LOG("2DRootFore hidden");
    } else {
        CK_DEBUG_LOG("Failed to create 2DRootFore");
    }

    CK_DEBUG_LOG("About to create 2DRootBack");
    m_2DRootBack = (CK2dEntity *) m_Context->CreateObject(CKCID_2DENTITY, (CKSTRING) "2DRootBack",
                                                          CK_OBJECTCREATION_NONAMECHECK);
    if (m_2DRootBack) {
        CK_DEBUG_LOG_FMT("2DRootBack created at %p", m_2DRootBack);
        m_2DRootBack->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
        CK_DEBUG_LOG("2DRootBack flags modified");
        ((RCK2dEntity *) m_2DRootBack)->ModifyFlags(0, CK_2DENTITY_RATIOOFFSET | CK_2DENTITY_CLIPTOCAMERAVIEW);
        CK_DEBUG_LOG("2DRootBack entity flags modified");
        m_2DRootBack->Show(CKHIDE);
        CK_DEBUG_LOG("2DRootBack hidden");
    } else {
        CK_DEBUG_LOG("Failed to create 2DRootBack");
    }

    // Register default material effects
    RegisterDefaultEffects();

    // Initialize scene graph root node with full render context mask
    m_CKSceneGraphRootNode.m_RenderContextMask = 0xFFFFFFFF;
    m_CKSceneGraphRootNode.m_EntityMask = 0xFFFFFFFF;

    CK_DEBUG_LOG_FMT("RCKRenderManager initialization complete - %d drivers available", m_DriverCount);
}

CKERROR RCKRenderManager::PreClearAll() {
    m_CKSceneGraphRootNode.Clear();
    m_CKSceneGraphRootNode.m_TransparentObjects.Clear();
    m_CKSceneGraphRootNode.m_RenderContextMask = 0xFFFFFFFF;
    m_CKSceneGraphRootNode.m_EntityMask = 0xFFFFFFFF;
    m_CKSceneGraphRootNode.m_ChildToBeParsedCount = 0;

    // Rebuild scene graph nodes for all existing 3D entities so they keep valid links
    XArray<RCK3dEntity *> entities;
    for (int i = 0; i < CKGetClassCount(); ++i) {
        if (!CKIsChildClassOf(i, CKCID_3DENTITY))
            continue;

        int count = m_Context->GetObjectsCountByClassID(i);
        CK_ID *ids = m_Context->GetObjectsListByClassID(i);
        for (int j = 0; j < count; ++j) {
            RCK3dEntity *entity = (RCK3dEntity *) m_Context->GetObject(ids[j]);
            if (entity)
                entities.PushBack(entity);
        }
    }

    // Pass 1: recreate nodes (attached to root by CreateNode)
    for (int i = 0; i < entities.Size(); ++i) {
        RCK3dEntity *entity = entities[i];
        entity->m_SceneGraphNode = CreateNode(entity);
    }

    // Pass 2: reattach to real parents and restore render context masks
    for (int i = 0; i < entities.Size(); ++i) {
        RCK3dEntity *entity = entities[i];
        CKSceneGraphNode *node = entity->m_SceneGraphNode;
        if (!node)
            continue;

        // Move under its parent node if present
        if (entity->m_Parent && entity->m_Parent->m_SceneGraphNode) {
            m_CKSceneGraphRootNode.RemoveNode(node);
            entity->m_Parent->m_SceneGraphNode->AddNode(node);
        }

        node->SetRenderContextMask(entity->GetInRenderContextMask(), TRUE);
    }

    DetachAllObjects();
    ClearTemporaryCallbacks();
    DeleteAllVertexBuffers();

    for (int i = 0; i < CKGetClassCount(); ++i) {
        if (CKIsChildClassOf(i, CKCID_RENDERCONTEXT)) {
            int count = m_Context->GetObjectsCountByClassID(i);
            CK_ID *ids = m_Context->GetObjectsListByClassID(i);
            for (int j = 0; j < count; ++j) {
                RCKRenderContext *ctx = (RCKRenderContext *) m_Context->GetObject(ids[j]);
                if (ctx)
                    ctx->OnClearAll();
            }
        } else if (CKIsChildClassOf(i, CKCID_3DENTITY)) {
            int count = m_Context->GetObjectsCountByClassID(i);
            CK_ID *ids = m_Context->GetObjectsListByClassID(i);
            for (int j = 0; j < count; ++j) {
                RCK3dEntity *entity = (RCK3dEntity *) m_Context->GetObject(ids[j]);
                if (entity)
                    entity->RemoveAllCallbacks();
            }
        } else if (CKIsChildClassOf(i, CKCID_MESH)) {
            int count = m_Context->GetObjectsCountByClassID(i);
            CK_ID *ids = m_Context->GetObjectsListByClassID(i);
            for (int j = 0; j < count; ++j) {
                RCKMesh *mesh = (RCKMesh *) m_Context->GetObject(ids[j]);
                if (mesh)
                    mesh->RemoveAllCallbacks();
            }
        }
    }

    m_DefaultMat = nullptr;
    return CK_OK;
}

CKERROR RCKRenderManager::PreProcess() {
    return CKBaseManager::PreProcess();
}

CKERROR RCKRenderManager::PostProcess() {
    return CKBaseManager::PostProcess();
}

CKERROR RCKRenderManager::SequenceAddedToScene(CKScene *scn, CK_ID *objids, int count) {
    CK_DEBUG_LOG_FMT("SequenceAddedToScene: scn=%p, count=%d", scn, count);
    
    // Try to add to Level's render contexts first
    CKLevel *level = m_Context->GetCurrentLevel();
    int ctxCount = 0;
    
    if (level && level->GetCurrentScene() == scn) {
        ctxCount = level->GetRenderContextCount();
        CK_DEBUG_LOG_FMT("SequenceAddedToScene: Level has %d render contexts", ctxCount);
        for (int i = 0; i < ctxCount; ++i) {
            CKRenderContext *ctx = level->GetRenderContext(i);
            if (ctx) {
                for (int j = 0; j < count; ++j) {
                    CKObject *obj = m_Context->GetObject(objids[j]);
                    if (obj && CKIsChildClassOf(obj, CKCID_RENDEROBJECT)) {
                        CK_DEBUG_LOG_FMT("SequenceAddedToScene: Adding object %s (class=%X) to level context", 
                                         obj->GetName() ? obj->GetName() : "(null)", obj->GetClassID());
                        ctx->AddObjectWithHierarchy((CKRenderObject *) obj);
                    }
                }
            }
        }
    }
    
    // If no level contexts, add to all render contexts managed by RenderManager
    if (ctxCount == 0) {
        ctxCount = GetRenderContextCount();
        CK_DEBUG_LOG_FMT("SequenceAddedToScene: No level contexts, using %d manager contexts", ctxCount);
        for (int i = 0; i < ctxCount; ++i) {
            CKRenderContext *ctx = GetRenderContext(i);
            if (ctx) {
                for (int j = 0; j < count; ++j) {
                    CKObject *obj = m_Context->GetObject(objids[j]);
                    if (obj && CKIsChildClassOf(obj, CKCID_RENDEROBJECT)) {
                        CK_DEBUG_LOG_FMT("SequenceAddedToScene: Adding object %s (class=%X) to manager context", 
                                         obj->GetName() ? obj->GetName() : "(null)", obj->GetClassID());
                        ctx->AddObjectWithHierarchy((CKRenderObject *) obj);
                    }
                }
            }
        }
    }

    return CK_OK;
}

CKERROR RCKRenderManager::SequenceRemovedFromScene(CKScene *scn, CK_ID *objids, int count) {
    CKLevel *level = m_Context->GetCurrentLevel();
    if (!level)
        return CK_OK;

    if (level->GetCurrentScene() != scn)
        return CK_OK;

    int ctxCount = level->GetRenderContextCount();
    for (int i = 0; i < ctxCount; ++i) {
        CKRenderContext *ctx = level->GetRenderContext(i);
        if (ctx) {
            for (int j = 0; j < count; ++j) {
                CKObject *obj = m_Context->GetObject(objids[j]);
                if (obj && CKIsChildClassOf(obj, CKCID_RENDEROBJECT))
                    ctx->RemoveObject((CKRenderObject *) obj);
            }
        }
    }

    return CK_OK;
}

CKERROR RCKRenderManager::OnCKEnd() {
    return CKBaseManager::OnCKEnd();
}

CKERROR RCKRenderManager::OnCKPause() {
    RemoveAllTemporaryCallbacks();

    int ctxCount = GetRenderContextCount();
    for (int i = 0; i < ctxCount; ++i) {
        RCKRenderContext *ctx = (RCKRenderContext *) GetRenderContext(i);
        if (ctx)
            ctx->m_PVInformation = 0;
    }

    return CK_OK;
}

CKERROR RCKRenderManager::SequenceToBeDeleted(CK_ID *objids, int count) {
    m_Objects.Check();
    m_MovedEntities.Check();
    m_CKSceneGraphRootNode.Check();
    return CK_OK;
}

CKERROR RCKRenderManager::SequenceDeleted(CK_ID *objids, int count) {
    m_RenderContexts.Check(m_Context);
    return CK_OK;
}

CKDWORD RCKRenderManager::GetValidFunctionsMask() {
    return 0xC029F;
}

int RCKRenderManager::GetRenderDriverCount() {
    return m_DriverCount;
}

// Static array to hold VxDriverDesc structures - use fixed size to avoid dynamic allocation issues
static VxDriverDesc s_DriverDescCache[16];
static int s_DriverDescCacheCount = 0;

VxDriverDesc *RCKRenderManager::GetRenderDriverDescription(int Driver) {
    CK_DEBUG_LOG_FMT("GetRenderDriverDescription: Driver=%d, m_DriverCount=%d", Driver, m_DriverCount);

    if (Driver < 0 || Driver >= m_DriverCount || Driver >= 16) {
        CK_DEBUG_LOG("GetRenderDriverDescription: Driver index out of range!");
        return nullptr;
    }

    VxDriverDescEx *drv = &m_Drivers[Driver];
    CK_DEBUG_LOG_FMT("GetRenderDriverDescription: drv=%p, CapsUpToDate=%d", drv, drv->CapsUpToDate);

    if (!drv->CapsUpToDate && drv->RasterizerDriver) {
        CK_DEBUG_LOG("GetRenderDriverDescription: Updating caps");
        UpdateDriverDescCaps(drv);
    }

    CK_DEBUG_LOG("GetRenderDriverDescription: Getting desc from cache");

    // Copy data to VxDriverDesc - directly copy without XSArray issues
    VxDriverDesc *desc = &s_DriverDescCache[Driver];
    CK_DEBUG_LOG_FMT("GetRenderDriverDescription: desc=%p, copying data", desc);

    strncpy(desc->DriverDesc, drv->DriverDesc, sizeof(desc->DriverDesc) - 1);
    desc->DriverDesc[sizeof(desc->DriverDesc) - 1] = '\0';
    strncpy(desc->DriverName, drv->DriverDesc2, sizeof(desc->DriverName) - 1);
    desc->DriverName[sizeof(desc->DriverName) - 1] = '\0';
    desc->IsHardware = drv->Hardware;
    desc->DisplayModeCount = drv->DisplayModeCount;
    desc->DisplayModes = drv->DisplayModes;
    // Point to the same TextureFormats - no copy needed
    // Note: We can't easily copy XSArray, so we reference the original
    // desc->TextureFormats stays as-is (caller should use drv->TextureFormats)
    desc->Caps2D = drv->Caps2D;
    desc->Caps3D = drv->Caps3D;

    CK_DEBUG_LOG_FMT("GetRenderDriverDescription: Returning desc=%p, DriverDesc=%s, DriverName=%s",
                     desc, desc->DriverDesc, desc->DriverName);

    return desc;
}

void RCKRenderManager::GetDesiredTexturesVideoFormat(VxImageDescEx &VideoFormat) {
    VxPixelFormat2ImageDesc((VX_PIXELFORMAT) m_TextureVideoFormat.Value, VideoFormat);
}

void RCKRenderManager::SetDesiredTexturesVideoFormat(VxImageDescEx &VideoFormat) {
    m_TextureVideoFormat.Value = VxImageDesc2PixelFormat(VideoFormat);
}

CKRenderContext *RCKRenderManager::GetRenderContext(int pos) {
    return (CKRenderContext *) m_RenderContexts.GetObject(m_Context, pos);
}

CKRenderContext *RCKRenderManager::GetRenderContextFromPoint(CKPOINT &pt) {
    return nullptr;
}

int RCKRenderManager::GetRenderContextCount() {
    return m_RenderContexts.Size();
}

void RCKRenderManager::Process() {
    for (CK_ID *it = m_RenderContexts.Begin(); it != m_RenderContexts.End(); ++it) {
        CKRenderContext *dev = (CKRenderContext *) m_Context->GetObject(*it);
        if (dev)
            dev->Render();
    }
}

void RCKRenderManager::FlushTextures() {
    int textureCount = m_Context->GetObjectsCountByClassID(CKCID_TEXTURE);
    CK_ID *textureIds = m_Context->GetObjectsListByClassID(CKCID_TEXTURE);
    for (int i = 0; i < textureCount; ++i) {
        CKTexture *texture = (CKTexture *) m_Context->GetObject(textureIds[i]);
        if (texture)
            texture->FreeVideoMemory();
    }

    int spriteCount = m_Context->GetObjectsCountByClassID(CKCID_SPRITE);
    CK_ID *spriteIds = m_Context->GetObjectsListByClassID(CKCID_SPRITE);
    for (int i = 0; i < spriteCount; ++i) {
        CKSprite *sprite = (CKSprite *) m_Context->GetObject(spriteIds[i]);
        if (sprite)
            sprite->FreeVideoMemory();
    }

    int spriteTextCount = m_Context->GetObjectsCountByClassID(CKCID_SPRITETEXT);
    CK_ID *spriteTextIds = m_Context->GetObjectsListByClassID(CKCID_SPRITETEXT);
    for (int i = 0; i < spriteTextCount; ++i) {
        RCKSpriteText *spriteText = (RCKSpriteText *) m_Context->GetObject(spriteTextIds[i]);
        if (spriteText)
            spriteText->FreeVideoMemory();
    }
}

CKRenderContext *RCKRenderManager::CreateRenderContext(void *Window, int Driver, CKRECT *rect, CKBOOL Fullscreen,
                                                       int Bpp, int Zbpp, int StencilBpp, int RefreshRate) {
    CK_DEBUG_LOG_FMT("CreateRenderContext: Window=%p, Driver=%d, Fullscreen=%d, Bpp=%d", Window, Driver, Fullscreen,
                     Bpp);

    RCKRenderContext *dev = (RCKRenderContext *) m_Context->CreateObject(CKCID_RENDERCONTEXT);
    if (!dev) {
        CK_DEBUG_LOG("CreateRenderContext: Failed to create RenderContext object");
        return nullptr;
    }

    // Allocate a free mask bit for this context
    // Find first available bit in m_RenderContextMaskFree
    CKDWORD mask = 0;
    for (int i = 0; i < 32; i++) {
        CKDWORD bit = (1 << i);
        if (m_RenderContextMaskFree & bit) {
            mask = bit;
            m_RenderContextMaskFree &= ~bit;  // Mark as used
            break;
        }
    }
    
    if (mask == 0) {
        CK_DEBUG_LOG("CreateRenderContext: No free render context mask available");
        m_Context->DestroyObject(dev);
        return nullptr;
    }
    
    dev->m_MaskFree = mask;
    CK_DEBUG_LOG_FMT("CreateRenderContext: Allocated mask 0x%X", mask);

    CK_DEBUG_LOG("CreateRenderContext: RenderContext object created, calling Create...");

    if (dev->Create(Window, Driver, rect, Fullscreen, Bpp, Zbpp, StencilBpp, RefreshRate) != CK_OK) {
        CK_DEBUG_LOG("CreateRenderContext: Create failed, destroying object");
        m_RenderContextMaskFree |= mask;  // Return mask to free pool
        m_Context->DestroyObject(dev);
        return nullptr;
    }

    CK_DEBUG_LOG("CreateRenderContext: Success");
    CK_DEBUG_LOG_FMT("CreateRenderContext: dev=%p, dev->GetID()=%d", dev, dev->GetID());
    CK_DEBUG_LOG("CreateRenderContext: About to add to m_RenderContexts");
    m_RenderContexts.PushBack(dev->GetID());
    CK_DEBUG_LOG("CreateRenderContext: Added to m_RenderContexts");
    
    // Verify 2D roots are accessible from the newly created context
    CK2dEntity *fore = dev->Get2dRoot(FALSE);
    CK2dEntity *back = dev->Get2dRoot(TRUE);
    CK_DEBUG_LOG_FMT("CreateRenderContext: Verifying 2D roots - fore=%p back=%p", fore, back);
    
    CK_DEBUG_LOG_FMT("CreateRenderContext: Returning dev=%p", dev);
    return dev;
}

CKERROR RCKRenderManager::DestroyRenderContext(CKRenderContext *context) {
    if (!context)
        return CKERR_INVALIDPARAMETER;

    CKLevel *level = m_Context->GetCurrentLevel();
    if (level)
        level->RemoveRenderContext(context);

    if (!m_RenderContexts.RemoveObject(context))
        return CKERR_INVALIDPARAMETER;

    m_Context->DestroyObject(context);
    return CK_OK;
}

void RCKRenderManager::RemoveRenderContext(CKRenderContext *context) {
    if (context)
        m_RenderContexts.RemoveObject(context);
}

CKVertexBuffer *RCKRenderManager::CreateVertexBuffer() {
    RCKVertexBuffer *vertexBuffer = new RCKVertexBuffer(m_Context);
    m_VertexBuffers.PushBack(vertexBuffer);
    return vertexBuffer;
}

void RCKRenderManager::DestroyVertexBuffer(CKVertexBuffer *VB) {
    if (VB) {
        m_VertexBuffers.Remove(VB);
        delete VB;
    }
}

void RCKRenderManager::SetRenderOptions(CKSTRING RenderOptionString, CKDWORD Value) {
}

const VxEffectDescription &RCKRenderManager::GetEffectDescription(int EffectIndex) {
    return m_Effects[EffectIndex];
}

int RCKRenderManager::GetEffectCount() {
    return m_Effects.Size();
}

int RCKRenderManager::AddEffect(const VxEffectDescription &NewEffect) {
    int size = m_Effects.Size();
    m_Effects.PushBack(NewEffect);
    // *((_DWORD *)XArray::End(&this->m_Effects) - 23) = size;
    return size;
}

CKDWORD RCKRenderManager::CreateObjectIndex(CKRST_OBJECTTYPE type) {
    if (m_DriverCount == 0)
        return 0;
    return m_Drivers->Rasterizer->CreateObjectIndex(type, TRUE);
}

CKBOOL RCKRenderManager::ReleaseObjectIndex(CKDWORD index, CKRST_OBJECTTYPE type) {
    if (m_DriverCount == 0)
        return true;
    return m_Drivers->Rasterizer->ReleaseObjectIndex(index, type, TRUE);
}

void RCKRenderManager::DetachAllObjects() {
}

void RCKRenderManager::DeleteAllVertexBuffers() {
}

void RCKRenderManager::AddTemporaryCallback(CKCallbacksContainer *callbacks, void *Function, void *Argument,
                                            CKBOOL preOrPost) {
}

void RCKRenderManager::ClearTemporaryCallbacks() {
}

void RCKRenderManager::RemoveAllTemporaryCallbacks() {
}

void RCKRenderManager::RegisterDefaultEffects() {
    // Define GUIDs for parameter types
    const CKGUID GUID_MATERIAL_EFFECT(0x62C563E3, 0x7323470D);
    const CKGUID GUID_TEX_COORDS_GENERATOR(0x7DBC28F5, 0x3F161E80);
    const CKGUID GUID_TEXTURE_BLENDING(0x21E87A54, 0x65D37993);
    const CKGUID GUID_TEXGEN_REFERENTIAL(0x724B7421, 0x07213ADD);
    const CKGUID GUID_COMBINE2_TEXTURES(0x154264EA, 0x1EB15971);
    const CKGUID GUID_COMBINE3_TEXTURES(0x235E15CC, 0x65903824);
    const CKGUID GUID_BUMPMAP_PARAMS(0x36DA22D9, 0x4AC44B4C);
    const CKGUID GUID_3DENTITY(0x5B8A05D5, 0x31EA28D4);
    const CKGUID GUID_FLOAT(0x47884C3F, 0x432C2C20);

    // Effect 0: None
    VxEffectDescription effectNone;
    effectNone.Summary = "None";
    AddEffect(effectNone);

    // Effect 1: TexGen
    VxEffectDescription effectTexGen;
    effectTexGen.Summary = "TexGen";
    effectTexGen.Description = "Generate texture coordinates.\r\n"
        "The mapping parameter determines which type of coordinates to generate.\r\n"
        "The TexGen parameter controls the ways texture coordinates are generated or transformed:\r\n"
        "None : Mesh texture coordinates.\r\n"
        "Transform : Mesh texture coordinates multiplied by the referential matrix.\r\n"
        "Reflect : Generate texture coordinates that simulate a reflection .\r\n"
        "Planar: Generate texture coordinates projected as projeted from the referential plane.\r\n"
        "Cube Reflect: Generate texture coordinates using reflection vector for a cube map.\r\n"
        "It use the current viewpoint as referential";
    effectTexGen.DescImage = "Effect_CubeReflMap.jpg";
    effectTexGen.MaxTextureCount = 0;
    effectTexGen.NeededTextureCoordsCount = 0;
    effectTexGen.ParameterType = GUID_TEX_COORDS_GENERATOR;
    effectTexGen.ParameterDescription = "TexGen Type";
    effectTexGen.ParameterDefaultValue = "Reflect";
    AddEffect(effectTexGen);

    // Effect 2: TexGen with referential
    VxEffectDescription effectTexGenRef;
    effectTexGenRef.Summary = "TexGen with referential";
    effectTexGenRef.Description = "Generate texture coordinates.\r\n"
        "The TexGen parameter controls the ways texture coordinates are generated or transformed:\r\n"
        "None : Mesh texture coordinates.\r\n"
        "Transform : Mesh texture coordinates multiplied by the referential matrix.\r\n"
        "Reflect : Generate texture coordinates that simulate a reflection .\r\n"
        "Planar: Generate texture coordinates projected as projeted from the referential plane.\r\n"
        "Cube Reflect: Generate texture coordinates using reflection vector for a cube map.\r\n"
        "This works as Tex coordinates generation effect but an additionnal referential can be used instead of the viewpoint.\r\n";
    effectTexGenRef.DescImage = "Effect_CubeReflMap.jpg";
    effectTexGenRef.MaxTextureCount = 0;
    effectTexGenRef.NeededTextureCoordsCount = 0;
    effectTexGenRef.ParameterType = GUID_TEXGEN_REFERENTIAL;
    effectTexGenRef.ParameterDescription = "TexGen Params";
    effectTexGenRef.ParameterDefaultValue = "Reflect,NULL";
    AddEffect(effectTexGenRef);

    // Effect 3: Environment Bump Map
    VxEffectDescription effectBumpEnv;
    effectBumpEnv.Summary = "Environment Bump Map";
    effectBumpEnv.Description = "The default amplitude of the bump effect is 2.0f, this can be amplified or reduced"
        "by an offset given in the amplitude parameter.\r\n"
        "The Env. Texture can be either a cube map or a normal texture and the way it is combined with the base texture can be given in the parameter\r\n"
        "See the Tex coords generation effect for details on the TexGen param\r\n"
        "See the Combine 2 Textures effect for details on the Combine param";
    effectBumpEnv.DescImage = "Effect_BumpMapSmooth.jpg";
    effectBumpEnv.MaxTextureCount = 2;
    effectBumpEnv.NeededTextureCoordsCount = 2;
    effectBumpEnv.Tex1Description = "Bump Texture";
    effectBumpEnv.Tex2Description = "Env. Texture";
    effectBumpEnv.ParameterType = GUID_BUMPMAP_PARAMS;
    effectBumpEnv.ParameterDescription = "Params";
    effectBumpEnv.ParameterDefaultValue = "0,Add,Reflect,NULL";
    AddEffect(effectBumpEnv);

    // Effect 4: Floor DotProduct3 Lighting
    VxEffectDescription effectDP3;
    effectDP3.Summary = "Floor DotProduct3 Lighting";
    effectDP3.Description = "The Bump texture should be in normal format. "
        "When this effect is not valid because of video card limitation, the texture will simply be modulated with lighting.";
    effectDP3.DescImage = "Effect_DP3.jpg";
    effectDP3.MaxTextureCount = 1;
    effectDP3.NeededTextureCoordsCount = 1;
    effectDP3.Tex1Description = "Bump Texture (Normals)";
    effectDP3.ParameterType = GUID_3DENTITY;
    effectDP3.ParameterDescription = "Light";
    effectDP3.ParameterDefaultValue = "NULL";
    AddEffect(effectDP3);

    // Effect 5: Combine 2 Textures
    VxEffectDescription effectCombine2;
    effectCombine2.Summary = "Combine 2 Textures";
    effectCombine2.Description =
        "Blends two textures, the base material one and the one given in the effect, with a set of"
        "texture coordinates generated by a method similar to the one in the TexGen effect."
        "If you do not select a generation method here it will use the texture coordinates set in the channel (if available).\r\n";
    effectCombine2.DescImage = "Effect_Blend2Textures.jpg";
    effectCombine2.MaxTextureCount = 1;
    effectCombine2.NeededTextureCoordsCount = 0;
    effectCombine2.ParameterType = GUID_COMBINE2_TEXTURES;
    effectCombine2.ParameterDescription = "Params";
    effectCombine2.ParameterDefaultValue = "Modulate,None,NULL";
    AddEffect(effectCombine2);

    // Effect 6: Combine 3 Textures
    VxEffectDescription effectCombine3;
    effectCombine3.Summary = "Combine 3 Textures";
    effectCombine3.Description = "Effect similar than the Combine 2 Textures except than it will work on 3.\r\n";
    effectCombine3.DescImage = "Effect_Blend3Textures.jpg";
    effectCombine3.MaxTextureCount = 2;
    effectCombine3.NeededTextureCoordsCount = 0;
    effectCombine3.ParameterType = GUID_COMBINE3_TEXTURES;
    effectCombine3.ParameterDescription = "Params";
    effectCombine3.ParameterDefaultValue = "Modulate,None,NULL,Modulate,None,NULL";
    AddEffect(effectCombine3);

    // Build enumeration string for Material Effect
    XString effectEnum;
    for (int i = 0; i < m_Effects.Size(); ++i) {
        effectEnum << m_Effects[i].Summary;
        effectEnum << "=";
        effectEnum << m_Effects[i].EffectIndex;
        if (i < m_Effects.Size() - 1)
            effectEnum << ",";
    }

    // Register parameter types with ParameterManager
    CKParameterManager *pm = m_Context->GetParameterManager();
    if (!pm)
        return;

    // Register Material Effect enum
    pm->RegisterNewEnum(GUID_MATERIAL_EFFECT, (CKSTRING) "Material Effect", (CKSTRING) effectEnum.CStr());
    CKParameterTypeDesc *typeDesc = pm->GetParameterTypeDescription(GUID_MATERIAL_EFFECT);
    if (typeDesc)
        typeDesc->dwFlags |= CKPARAMETERTYPE_HIDDEN;

    // Register Tex Coords Generator enum
    pm->RegisterNewEnum(GUID_TEX_COORDS_GENERATOR, (CKSTRING) "Tex Coords Generator",
                        (CKSTRING)
                        "None=0,Transform=1,Reflect=2,Chrome=3,Planar=4,CubeMap Reflect=31,CubeMap SkyMap=32,CubeMap Normals=33,CubeMap Positions=34");
    typeDesc = pm->GetParameterTypeDescription(GUID_TEX_COORDS_GENERATOR);
    if (typeDesc)
        typeDesc->dwFlags |= CKPARAMETERTYPE_HIDDEN;

    // Register Texture Blending enum
    pm->RegisterNewEnum(GUID_TEXTURE_BLENDING, (CKSTRING) "Texture Blending",
                        (CKSTRING)
                        "None=0,Modulate=4,Modulate 2X=5,Modulate 4X=6,Add=7,Add Signed=8,Add Signed 2X=9,Subtract=10,Add Smooth=11,"
                        "Blend Using Diffuse Alpha=12,Blend Using Texture Alpha=13,Blend Using Current Alpha=16,"
                        "Modulate Alpha Add Color=18,Modulate Color Add Alpha=19,Modulate InvAlpha Add Color=20,Modulate InvColor Add Alpha=21");
    typeDesc = pm->GetParameterTypeDescription(GUID_TEXTURE_BLENDING);
    if (typeDesc)
        typeDesc->dwFlags |= CKPARAMETERTYPE_HIDDEN;

    // Register TexgenReferential structure: (TexGen, Referential)
    pm->RegisterNewStructure(GUID_TEXGEN_REFERENTIAL, (CKSTRING) "TexgenReferential",
                             (CKSTRING) "TexGen,Referential",
                             GUID_TEX_COORDS_GENERATOR, GUID_3DENTITY);

    // Register Combine2Textures structure: (Combine, TexGen, Referential)
    pm->RegisterNewStructure(GUID_COMBINE2_TEXTURES, (CKSTRING) "Combine 2 Textures",
                             (CKSTRING) "Combine,TexGen,Referential",
                             GUID_TEXTURE_BLENDING, GUID_TEX_COORDS_GENERATOR, GUID_3DENTITY);

    // Register Combine3Textures structure: (Combine1, TexGen1, Ref1, Combine2, TexGen2, Ref2)
    pm->RegisterNewStructure(GUID_COMBINE3_TEXTURES, (CKSTRING) "Combine 3 Textures",
                             (CKSTRING) "Combine1,TexGen1,Ref1,Combine2,TexGen2,Ref2",
                             GUID_TEXTURE_BLENDING, GUID_TEX_COORDS_GENERATOR, GUID_3DENTITY,
                             GUID_TEXTURE_BLENDING, GUID_TEX_COORDS_GENERATOR, GUID_3DENTITY);

    // Register BumpmapParameters structure: (Amplitude, EnvMap Combine, EnvMap TexGen, EnvMap Referential)
    pm->RegisterNewStructure(GUID_BUMPMAP_PARAMS, (CKSTRING) "Bumpmap Parameters",
                             (CKSTRING) "Amplitude,EnvMap Combine,EnvMap TexGen,EnvMap Referential",
                             GUID_FLOAT, GUID_TEXTURE_BLENDING, GUID_TEX_COORDS_GENERATOR, GUID_3DENTITY);
}

// =====================================================
// Scene Graph Node Management
// =====================================================

CKSceneGraphNode *RCKRenderManager::CreateNode(RCK3dEntity *entity) {
    CKSceneGraphNode *node = new CKSceneGraphNode(entity);
    if (node) {
        // Add to root node
        m_CKSceneGraphRootNode.AddNode(node);
        CK_DEBUG_LOG_FMT("CreateNode: Created node for entity=%p, total children=%d", 
                         entity, m_CKSceneGraphRootNode.m_Children.Size());
    }
    return node;
}

void RCKRenderManager::DeleteNode(CKSceneGraphNode *node) {
    if (!node)
        return;
    
    // Remove from parent
    if (node->m_Parent) {
        node->m_Parent->RemoveNode(node);
    }
    
    // Delete the node
    delete node;
}

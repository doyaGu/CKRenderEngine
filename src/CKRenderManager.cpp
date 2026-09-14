#include "RCKRenderManager.h"

#include "CKRasterizerRegistration.h"
#include "CKRasterizerCapsBaseline.h"

#include "CKLevel.h"
#include "CKMaterial.h"
#include "CKParameterManager.h"
#include "CKRenderSettings.h"
#include "RCKRenderContext.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKMesh.h"
#include "RCKTexture.h"
#include "RCKSprite.h"
#include "RCKSpriteText.h"
#include "RCKVertexBuffer.h"

// External reference to rasterizer info array from CK2_3D.cpp
extern XClassArray<CKRasterizerInfo> g_RasterizersInfo;

static CKBOOL IsHardwareDriver(CKRasterizerDriver *driver) {
    if (!driver)
        return FALSE;
    CKRasterizerDriverDesc desc = {};
    return driver->GetDesc(&desc) ? desc.Hardware : FALSE;
}

static void ClearMeshVertexBufferState(RCKMeshBufferState &state) {
    state.VertexBufferReady = 0;
    state.VertexBuffer = 0;
    state.VertexBufferDpFlags = 0;
    state.VertexBufferVertexFormat = 0;
    state.VertexBufferStride = 0;
    state.VertexBufferVertexCount = 0;
    state.VertexBufferWrapAware = FALSE;
}

static void ClearMeshIndexBufferState(RCKMeshBufferState &state) {
    state.IndexBufferReady = FALSE;
    state.IndexBuffer = 0;
    state.IndexBufferIndexCount = 0;
}

// Helper function to update driver description from rasterizer driver
static void UpdateDriverDescCaps(VxDriverDescEx *drvDesc) {
    CKRasterizerDriver *rstDriver = drvDesc->RasterizerDriver;

    if (!rstDriver) {
        // NULL rasterizer case
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

    CKRasterizerDriverDesc rasterizerDesc = {};
    if (!rstDriver->GetDesc(&rasterizerDesc))
        return;

    drvDesc->CapsUpToDate = rasterizerDesc.CapsFinal;
    strncpy(drvDesc->DriverDesc, rasterizerDesc.Description.CStr(), sizeof(drvDesc->DriverDesc) - 1);
    strncpy(drvDesc->DriverDesc2, rasterizerDesc.Description.CStr(), sizeof(drvDesc->DriverDesc2) - 1);
    drvDesc->DriverDesc[sizeof(drvDesc->DriverDesc) - 1] = '\0';
    drvDesc->DriverDesc2[sizeof(drvDesc->DriverDesc2) - 1] = '\0';
    drvDesc->Hardware = rasterizerDesc.Hardware;

    memset(&drvDesc->Caps3D, 0, sizeof(drvDesc->Caps3D));
    memset(&drvDesc->Caps2D, 0, sizeof(drvDesc->Caps2D));
    if (!CKRSTGetCapsBaseline(&drvDesc->Caps3D, &drvDesc->Caps2D)) {
        drvDesc->Caps3D.MinTextureWidth = 1;
        drvDesc->Caps3D.MinTextureHeight = 1;
        drvDesc->Caps3D.MaxTextureWidth = 4096;
        drvDesc->Caps3D.MaxTextureHeight = 4096;
        drvDesc->Caps3D.MaxTextureRatio = 4096;
        drvDesc->Caps3D.MaxClipPlanes = CKRST_MAX_USER_CLIP_PLANES;
        drvDesc->Caps3D.MaxActiveLights = CKRST_MAX_LIGHTS;
        drvDesc->Caps3D.MaxNumberBlendStage = CKRST_MAX_TEXTURE_STAGES;
        drvDesc->Caps3D.MaxNumberTextureStage = CKRST_MAX_TEXTURE_STAGES;
        drvDesc->Caps2D.Family = CKRST_DIRECTX;
        drvDesc->Caps2D.Caps = CKRST_2DCAPS_WINDOWED | CKRST_2DCAPS_3D;
    }

    CKRasterizerNativeCapsDesc nativeCaps;
    if (rstDriver->GetNativeCaps(&nativeCaps)) {
        Vx3DCapsDesc limits = {};
        limits.MaxTextureWidth = nativeCaps.MaxTextureSize;
        limits.MaxTextureHeight = nativeCaps.MaxTextureSize;
        limits.MaxTextureRatio = nativeCaps.MaxTextureSize;
        limits.MaxClipPlanes = nativeCaps.MaxUserClipPlanes;
        limits.MaxActiveLights = nativeCaps.MaxLights;
        limits.MaxNumberBlendStage = nativeCaps.MaxTextureStages;
        limits.MaxNumberTextureStage = nativeCaps.MaxTextureStages;
        CKRSTLowerCapsToLimits(&drvDesc->Caps3D, &limits);
    }

    const XDWORD hardwareCaps =
        CKRST_SPECIFICCAPS_HARDWARE |
        CKRST_SPECIFICCAPS_HARDWARETL |
        CKRST_SPECIFICCAPS_SOFTWARE;
    drvDesc->Caps3D.CKRasterizerSpecificCaps &= ~hardwareCaps;
    drvDesc->Caps3D.CKRasterizerSpecificCaps |= rasterizerDesc.Hardware
        ? CKRST_SPECIFICCAPS_HARDWARE | CKRST_SPECIFICCAPS_HARDWARETL
        : CKRST_SPECIFICCAPS_SOFTWARE;
    drvDesc->Caps2D.Caps |= CKRST_2DCAPS_WINDOWED | CKRST_2DCAPS_3D;
#if defined(_WIN32)
    drvDesc->Caps2D.Caps |= CKRST_2DCAPS_GDI;
#endif

    // Copy texture formats
    int texFormatCount = rstDriver->GetTextureFormatCount();
    drvDesc->TextureFormats.Resize(texFormatCount);
    for (int i = 0; i < texFormatCount; ++i) {
        CKTextureDesc textureDesc;
        if (rstDriver->GetTextureFormat(i, &textureDesc))
            drvDesc->TextureFormats[i] = textureDesc.Format;
    }

    // Copy display modes
    int displayModeCount = rstDriver->GetDisplayModeCount();
    drvDesc->DisplayModeCount = displayModeCount;
    if (drvDesc->DisplayModes) {
        delete[] drvDesc->DisplayModes;
    }
    drvDesc->DisplayModes = new VxDisplayMode[displayModeCount];
    for (int i = 0; i < displayModeCount; ++i)
        rstDriver->GetDisplayMode(i, &drvDesc->DisplayModes[i]);
}

static void ApplyIniRenderOptions(RCKRenderManager *manager) {
    if (!manager)
        return;

    const CKRenderSettingsView settings = CKRenderRootSettings();
    for (int i = 0; i < manager->m_Options.Size(); ++i) {
        VxOption *option = manager->m_Options[i];
        if (option)
            option->Value = settings.GetDword(option->Key.CStr(), option->Value);
    }

    manager->m_TextureVideoFormat.Value =
        settings.GetPixelFormat(manager->m_TextureVideoFormat.Key.CStr(), manager->m_TextureVideoFormat.Value);
    manager->m_SpriteVideoFormat.Value =
        settings.GetPixelFormat(manager->m_SpriteVideoFormat.Key.CStr(), manager->m_SpriteVideoFormat.Value);
}

static void ApplyRenderOptionChange(RCKRenderManager *manager, CKSTRING RenderOptionString, CKDWORD oldValue, CKDWORD newValue);
static void ApplyRenderOptionsToContexts(RCKRenderManager *manager);
static void InvalidateTextureVideoMemory(RCKRenderManager *manager);
static void ApplyTextureVideoFormat(RCKRenderManager *manager, VX_PIXELFORMAT format);
static void ApplySpriteVideoFormat(RCKRenderManager *manager, VX_PIXELFORMAT format);

RCKRenderManager::RCKRenderManager(CKContext *context) : CKRenderManager(context, "Render Manager") {
    // Initialize options
    m_TextureVideoFormat.Set("TextureVideoFormat", _32_ARGB8888);
    m_SpriteVideoFormat.Set("SpriteVideoFormat", _32_ARGB8888);
    m_Options.PushBack(&m_TextureVideoFormat);
    m_Options.PushBack(&m_SpriteVideoFormat);

    m_EnableScreenDump.Set("EnableScreenDump", FALSE);
    m_Options.PushBack(&m_EnableScreenDump);

    m_VertexCache.Set("VertexCache", 16);
    m_Options.PushBack(&m_VertexCache);

    m_SortTransparentObjects.Set("SortTransparentObjects", TRUE);
    m_Options.PushBack(&m_SortTransparentObjects);

    m_TextureCacheManagement.Set("TextureCacheManagement", TRUE);
    m_Options.PushBack(&m_TextureCacheManagement);

    m_UseIndexBuffers.Set("UseIndexBuffers", TRUE);
    m_Options.PushBack(&m_UseIndexBuffers);

    m_ForceLinearFog.Set("ForceLinearFog", FALSE);
    m_Options.PushBack(&m_ForceLinearFog);

    m_ForceSoftware.Set("ForceSoftware", FALSE);
    m_Options.PushBack(&m_ForceSoftware);

    m_DisableFilter.Set("DisableFilter", FALSE);
    m_Options.PushBack(&m_DisableFilter);

    m_Antialias.Set("Antialias", 0);
    m_Options.PushBack(&m_Antialias);

    m_DisableMipmap.Set("DisableMipmap", FALSE);
    m_Options.PushBack(&m_DisableMipmap);

    m_DisableSpecular.Set("DisableSpecular", FALSE);
    m_Options.PushBack(&m_DisableSpecular);

    ApplyIniRenderOptions(this);

    m_RenderContextMaskFree = -1;
    m_Context->RegisterNewManager(this);

    // Initialize driver-related fields
    m_DriverCount = 0;
    m_Drivers = nullptr;
    m_DefaultMat = nullptr;
    m_2DRootFore = nullptr;
    m_2DRootBack = nullptr;
    m_2DRootForeId = 0;
    m_2DRootBackId = 0;

    // Get main window for rasterizer initialization
    WIN_HANDLE mainWindow = m_Context->GetMainWindow();

    // Start rasterizers and count available drivers
    int rstInfoCount = g_RasterizersInfo.Size();

    for (CKRasterizerInfo *rstInfo = g_RasterizersInfo.Begin(); rstInfo != g_RasterizersInfo.End();) {
        CKRasterizer *rasterizer = nullptr;

        if (rstInfo->StartFct) {
            rasterizer = rstInfo->StartFct(mainWindow);
        }

        if (rasterizer) {
            int driverCount = rasterizer->GetDriverCount();
            m_DriverCount += driverCount;
            m_Rasterizers.PushBack(rasterizer);
            ++rstInfo;
        } else {
            if (rstInfo->DllInstance) {
                VxSharedLibrary sl;
                sl.Attach(rstInfo->DllInstance);
                sl.ReleaseLibrary();
                rstInfo->DllInstance = nullptr;
            }

            // Remove failed rasterizer info - move to next without incrementing
            rstInfo = g_RasterizersInfo.Remove(rstInfo);
        }
    }

    CKBOOL hasSoftwareDriver = FALSE;
    for (int i = 0; i < m_Rasterizers.Size() && !hasSoftwareDriver; ++i) {
        CKRasterizer *rasterizer = m_Rasterizers[i];
        for (int driverIndex = 0; rasterizer &&
             driverIndex < rasterizer->GetDriverCount(); ++driverIndex) {
            CKRasterizerDriver *driver = rasterizer->GetDriver(driverIndex);
            if (driver && !IsHardwareDriver(driver)) {
                hasSoftwareDriver = TRUE;
                break;
            }
        }
    }

    if (!hasSoftwareDriver) {
        CKRasterizerInfo info;
        CKNullRasterizerGetInfo(&info);
        CKRasterizer *fallback = info.StartFct(mainWindow);
        if (fallback && fallback->GetDriverCount() > 0) {
            info.DllInstance = nullptr;
            info.DllName = "";
            info.Desc = "NULL Rasterizer";
            info.InterfaceRevision = CKRST_INTERFACE_REVISION;
            g_RasterizersInfo.PushBack(info);
            m_Rasterizers.PushBack(fallback);
            m_DriverCount += fallback->GetDriverCount();
        } else if (fallback) {
            info.CloseFct(fallback);
        }
    }

    int rasterizerCount = m_Rasterizers.Size();

    // Allocate driver description array
    if (m_DriverCount > 0) {
        m_Drivers = new VxDriverDescEx[m_DriverCount]();
    }

    CKDWORD driverId = 0;

    // First pass: enumerate hardware drivers
    for (int i = 0; i < rasterizerCount; ++i) {
        CKRasterizer *rasterizer = m_Rasterizers[i];
        int drvCount = rasterizer->GetDriverCount();

        for (int k = 0; k < drvCount; ++k) {
            CKRasterizerDriver *rstDriver = rasterizer->GetDriver(k);
            if (IsHardwareDriver(rstDriver)) {
                VxDriverDescEx *drvDesc = &m_Drivers[driverId];
                drvDesc->Rasterizer = rasterizer;
                drvDesc->RasterizerDriver = rstDriver;
                drvDesc->DriverId = driverId;
                UpdateDriverDescCaps(drvDesc);
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
            if (!rstDriver || !IsHardwareDriver(rstDriver)) {
                VxDriverDescEx *drvDesc = &m_Drivers[driverId];
                drvDesc->Rasterizer = rasterizer;
                drvDesc->RasterizerDriver = rstDriver;
                drvDesc->DriverId = driverId;
                UpdateDriverDescCaps(drvDesc);
                ++driverId;
            }
        }
    }

    // Create default material
    m_DefaultMat = (CKMaterial *) m_Context->CreateObject(CKCID_MATERIAL, "Default Mat", CK_OBJECTCREATION_NONAMECHECK);
    if (m_DefaultMat) {
        m_DefaultMat->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
    }
    // Create 2D root entities
    m_2DRootFore = (CK2dEntity *) m_Context->CreateObject(CKCID_2DENTITY, "2DRootFore", CK_OBJECTCREATION_NONAMECHECK);
    if (m_2DRootFore) {
        m_2DRootFore->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
        ((RCK2dEntity *) m_2DRootFore)->ModifyFlags(0, CK_2DENTITY_RATIOOFFSET | CK_2DENTITY_CLIPTOCAMERAVIEW);
        m_2DRootFore->Show(CKHIDE);
        m_2DRootForeId = m_2DRootFore->GetID();
    }

    m_2DRootBack = (CK2dEntity *) m_Context->CreateObject(CKCID_2DENTITY, "2DRootBack", CK_OBJECTCREATION_NONAMECHECK);
    if (m_2DRootBack) {
        m_2DRootBack->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
        ((RCK2dEntity *) m_2DRootBack)->ModifyFlags(0, CK_2DENTITY_RATIOOFFSET | CK_2DENTITY_CLIPTOCAMERAVIEW);
        m_2DRootBack->Show(CKHIDE);
        m_2DRootBackId = m_2DRootBack->GetID();
    }

    // Register default material effects
    RegisterDefaultEffects();
}

RCKRenderManager::~RCKRenderManager() {
    DeleteAllVertexBuffers();

    while (m_MeshBufferEntries.Size() > 0) {
        MeshBufferEntryTable::Iterator entry = m_MeshBufferEntries.Begin();
        CKObject *object = m_Context->GetObject(entry.GetKey());
        if (object && CKIsChildClassOf(object, CKCID_MESH))
            DeleteMeshBuffers(static_cast<RCKMesh *>(object), FALSE);
        else
            m_MeshBufferEntries.Remove(entry);
    }

    while (m_TextureEntries.Size() > 0) {
        TextureEntryTable::Iterator entry = m_TextureEntries.Begin();
        CKObject *object = m_Context->GetObject(entry.GetKey());
        if (object && CKIsChildClassOf(object, CKCID_TEXTURE))
            DeleteTextureObjects(static_cast<RCKTexture *>(object), FALSE);
        else if (object && CKIsChildClassOf(object, CKCID_SPRITE))
            DeleteSpriteObjects(static_cast<RCKSprite *>(object), FALSE);
        else if (object)
            DeleteTextureEntries(object, FALSE);
        else
            m_TextureEntries.Remove(entry);
    }

    // Clean up drivers
    for (int i = 0; i < m_DriverCount; ++i) {
        delete[] m_Drivers[i].DisplayModes;
        m_Drivers[i].TextureFormats.Clear();
    }
    delete[] m_Drivers;
    m_Drivers = nullptr;

    // Close rasterizers
    int rstInfoCount = g_RasterizersInfo.Size();
    for (int i = 0; i < rstInfoCount; ++i) {
        CKRasterizerInfo &info = g_RasterizersInfo[i];
        if (info.CloseFct && m_Rasterizers[i]) {
            info.CloseFct(m_Rasterizers[i]);
        }
    }
}

CKERROR RCKRenderManager::PreClearAll() {
    m_SceneGraphRootNode.Clear();
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
    SaveLastFrameMatrix();
    CleanMovedEntities();
    RemoveAllTemporaryCallbacks();
    return CK_OK;
}

CKERROR RCKRenderManager::PostProcess() {
    // Set moved flag on all moved entities
    for (RCK3dEntity **it = (RCK3dEntity **) m_MovedEntities.Begin();
         it != (RCK3dEntity **) m_MovedEntities.End(); ++it) {
        (*it)->m_MoveableFlags |= VX_MOVEABLE_RESERVED2;
    }

    // Clear extents for all render contexts
    int ctxCount = GetRenderContextCount();
    for (int j = 0; j < ctxCount; ++j) {
        RCKRenderContext *ctx = (RCKRenderContext *) GetRenderContext(j);
        if (ctx) {
            ctx->m_Extents.Resize(0);
        }
    }

    return CK_OK;
}

CKERROR RCKRenderManager::SequenceAddedToScene(CKScene *scn, CK_ID *objids, int count) {
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
                if (obj && CKIsChildClassOf(obj, CKCID_RENDEROBJECT)) {
                    ctx->AddObject((CKRenderObject *) obj);
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
    CKObject *obj = m_Context->GetObject(m_2DRootForeId);
    if (obj && CKIsChildClassOf(obj, CKCID_2DENTITY)) {
        m_Context->DestroyObject(obj);
    }

    obj = m_Context->GetObject(m_2DRootBackId);
    if (obj && CKIsChildClassOf(obj, CKCID_2DENTITY)) {
        m_Context->DestroyObject(obj);
    }

    m_2DRootFore = nullptr;
    m_2DRootBack = nullptr;

    m_2DRootForeId = 0;
    m_2DRootBackId = 0;

    return CK_OK;
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
    m_Entities.Check();
    m_MovedEntities.Check();
    m_SceneGraphRootNode.Check();
    return CK_OK;
}

CKERROR RCKRenderManager::SequenceDeleted(CK_ID *objids, int count) {
    m_RenderContexts.Check(m_Context);
    return CK_OK;
}

CKDWORD RCKRenderManager::GetValidFunctionsMask() {
    return CKMANAGER_FUNC_OnSequenceToBeDeleted |
           CKMANAGER_FUNC_OnSequenceDeleted |
           CKMANAGER_FUNC_PreProcess |
           CKMANAGER_FUNC_PostProcess |
           CKMANAGER_FUNC_PreClearAll |
           CKMANAGER_FUNC_OnCKEnd |
           CKMANAGER_FUNC_OnCKPause |
           CKMANAGER_FUNC_OnSequenceAddedToScene |
           CKMANAGER_FUNC_OnSequenceRemovedFromScene;
}

int RCKRenderManager::GetRenderDriverCount() {
    return m_DriverCount;
}

// Cache public driver descriptors to keep returned pointers valid between calls.
static XClassArray<VxDriverDesc> s_DriverDescCache;

VxDriverDesc *RCKRenderManager::GetRenderDriverDescription(int Driver) {
    if (Driver < 0 || Driver >= m_DriverCount) {
        return nullptr;
    }

    VxDriverDescEx *drv = &m_Drivers[Driver];

    if (!drv->CapsUpToDate) {
        UpdateDriverDescCaps(drv);
    }

    if (s_DriverDescCache.Size() < m_DriverCount) {
        s_DriverDescCache.Resize(m_DriverCount);
    }

    VxDriverDesc *desc = &s_DriverDescCache[Driver];

    strncpy(desc->DriverDesc, drv->DriverDesc, sizeof(desc->DriverDesc) - 1);
    desc->DriverDesc[sizeof(desc->DriverDesc) - 1] = '\0';
    strncpy(desc->DriverName, drv->DriverDesc2, sizeof(desc->DriverName) - 1);
    desc->DriverName[sizeof(desc->DriverName) - 1] = '\0';
    desc->IsHardware = drv->Hardware;
    desc->DisplayModeCount = drv->DisplayModeCount;
    desc->DisplayModes = drv->DisplayModes;
    int textureFormatCount = drv->TextureFormats.Size();
    desc->TextureFormats.Resize(textureFormatCount);
    for (int i = 0; i < textureFormatCount; ++i) {
        desc->TextureFormats[i] = drv->TextureFormats[i];
    }

    desc->Caps2D = drv->Caps2D;
    desc->Caps3D = drv->Caps3D;

    return desc;
}

void RCKRenderManager::GetDesiredTexturesVideoFormat(VxImageDescEx &VideoFormat) {
    VxPixelFormat2ImageDesc((VX_PIXELFORMAT) m_TextureVideoFormat.Value, VideoFormat);
}

void RCKRenderManager::SetDesiredTexturesVideoFormat(VxImageDescEx &VideoFormat) {
    const CKDWORD oldValue = m_TextureVideoFormat.Value;
    const CKDWORD newValue = VxImageDesc2PixelFormat(VideoFormat);
    if (oldValue == newValue)
        return;
    m_TextureVideoFormat.Value = newValue;
    ApplyTextureVideoFormat(this, (VX_PIXELFORMAT)newValue);
}

CKRenderContext *RCKRenderManager::GetRenderContext(int pos) {
    return (CKRenderContext *) m_RenderContexts.GetObject(m_Context, pos);
}

CKRenderContext *RCKRenderManager::GetRenderContextFromPoint(CKPOINT &pt) {
    for (CK_ID *it = m_RenderContexts.Begin(); it != m_RenderContexts.End(); ++it) {
        RCKRenderContext *ctx = (RCKRenderContext *) m_Context->GetObject(*it);
        if (ctx) {
            WIN_HANDLE win = ctx->GetWindowHandle();
            if (win) {
                CKRECT rect;
                VxGetWindowRect(win, &rect);
                if (VxPtInRect(&rect, &pt))
                    return ctx;
            }
        }
    }
    return nullptr;
}

int RCKRenderManager::GetRenderContextCount() {
    return m_RenderContexts.Size();
}

void RCKRenderManager::Process() {
    for (CK_ID *it = m_RenderContexts.Begin(); it != m_RenderContexts.End(); ++it) {
        CKRenderContext *dev = (CKRenderContext *) m_Context->GetObject(*it);
        if (dev)
            dev->Render(CK_RENDER_USECURRENTSETTINGS);
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

CKRenderContext *RCKRenderManager::CreateRenderContext(void *Window, int Driver, CKRECT *rect, CKBOOL Fullscreen, int Bpp, int Zbpp, int StencilBpp, int RefreshRate) {
    RCKRenderContext *dev = (RCKRenderContext *) m_Context->CreateObject(CKCID_RENDERCONTEXT, nullptr, CK_OBJECTCREATION_NONAMECHECK);
    if (!dev)
        return nullptr;

    if (dev->Create(Window, Driver, rect, Fullscreen, Bpp, Zbpp, StencilBpp, RefreshRate) != CK_OK) {
        m_Context->DestroyObject(dev);
        return nullptr;
    }

    m_RenderContexts.PushBack(dev->GetID());
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
        m_RenderContexts.Remove(context->GetID());
}

CKVertexBuffer *RCKRenderManager::CreateVertexBuffer() {
    RCKVertexBuffer *vertexBuffer = new RCKVertexBuffer(m_Context);
    m_VertexBuffers.PushBack(vertexBuffer);
    return vertexBuffer;
}

void RCKRenderManager::DestroyVertexBuffer(CKVertexBuffer *VB) {
    if (VB) {
        if (m_VertexBuffers.Remove(VB))
            delete VB;
    }
}

void RCKRenderManager::SetRenderOptions(CKSTRING RenderOptionString, CKDWORD Value) {
    if (!RenderOptionString)
        return;

    for (int i = 0; i < m_Options.Size(); ++i) {
        VxOption *option = m_Options[i];
        if (stricmp(option->Key.CStr(), RenderOptionString) == 0) {
            const CKDWORD oldValue = option->Value;
            if (oldValue == Value)
                return;
            option->Value = Value;
            ApplyRenderOptionChange(this, RenderOptionString, oldValue, Value);
            return;
        }
    }
}

static void ApplyRenderOptionChange(RCKRenderManager *manager, CKSTRING RenderOptionString, CKDWORD oldValue, CKDWORD newValue) {
    (void)oldValue;

    if (stricmp(RenderOptionString, manager->m_TextureVideoFormat.Key.CStr()) == 0) {
        ApplyTextureVideoFormat(manager, (VX_PIXELFORMAT)newValue);
        return;
    }

    if (stricmp(RenderOptionString, manager->m_SpriteVideoFormat.Key.CStr()) == 0) {
        ApplySpriteVideoFormat(manager, (VX_PIXELFORMAT)newValue);
        return;
    }

    if (stricmp(RenderOptionString, manager->m_DisableMipmap.Key.CStr()) == 0) {
        InvalidateTextureVideoMemory(manager);
    }

    ApplyRenderOptionsToContexts(manager);
}

static void ApplyRenderOptionsToContexts(RCKRenderManager *manager) {
    for (CK_ID *it = manager->m_RenderContexts.Begin(); it != manager->m_RenderContexts.End(); ++it) {
        RCKRenderContext *context = (RCKRenderContext *) manager->m_Context->GetObject(*it);
        if (context)
            context->ApplyRenderOptions();
    }
}

static void InvalidateTextureVideoMemory(RCKRenderManager *manager) {
    int textureCount = manager->m_Context->GetObjectsCountByClassID(CKCID_TEXTURE);
    CK_ID *textureIds = manager->m_Context->GetObjectsListByClassID(CKCID_TEXTURE);
    for (int i = 0; i < textureCount; ++i) {
        CKTexture *texture = (CKTexture *) manager->m_Context->GetObject(textureIds[i]);
        if (texture)
            texture->FreeVideoMemory();
    }
}

static void ApplyTextureVideoFormat(RCKRenderManager *manager, VX_PIXELFORMAT format) {
    int textureCount = manager->m_Context->GetObjectsCountByClassID(CKCID_TEXTURE);
    CK_ID *textureIds = manager->m_Context->GetObjectsListByClassID(CKCID_TEXTURE);
    for (int i = 0; i < textureCount; ++i) {
        CKTexture *texture = (CKTexture *) manager->m_Context->GetObject(textureIds[i]);
        if (texture)
            texture->SetDesiredVideoFormat(format);
    }
}

static void ApplySpriteVideoFormat(RCKRenderManager *manager, VX_PIXELFORMAT format) {
    int spriteCount = manager->m_Context->GetObjectsCountByClassID(CKCID_SPRITE);
    CK_ID *spriteIds = manager->m_Context->GetObjectsListByClassID(CKCID_SPRITE);
    for (int i = 0; i < spriteCount; ++i) {
        CKSprite *sprite = (CKSprite *) manager->m_Context->GetObject(spriteIds[i]);
        if (sprite)
            sprite->SetDesiredVideoFormat(format);
    }

    int spriteTextCount = manager->m_Context->GetObjectsCountByClassID(CKCID_SPRITETEXT);
    CK_ID *spriteTextIds = manager->m_Context->GetObjectsListByClassID(CKCID_SPRITETEXT);
    for (int i = 0; i < spriteTextCount; ++i) {
        RCKSpriteText *spriteText = (RCKSpriteText *) manager->m_Context->GetObject(spriteTextIds[i]);
        if (spriteText)
            spriteText->SetDesiredVideoFormat(format);
    }
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
    m_Effects[size].EffectIndex = (VX_EFFECT) size;
    return size;
}

CKMaterial *RCKRenderManager::GetDefaultMaterial() {
    if (!m_DefaultMat) {
        m_DefaultMat = (CKMaterial *) m_Context->CreateObject(CKCID_MATERIAL, "Default Mat", CK_OBJECTCREATION_NONAMECHECK);
        if (m_DefaultMat) {
            m_DefaultMat->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
        }
    }
    return m_DefaultMat;
}

void RCKRenderManager::DetachAllObjects() {
    m_MovedEntities.Clear();
    m_Entities.Clear();

    for (CK_ID *it = m_RenderContexts.Begin(); it != m_RenderContexts.End(); ++it) {
        CKRenderContext *ctx = (CKRenderContext *) m_Context->GetObject(*it);
        if (ctx) {
            ctx->DetachAll();
            ctx->SetCurrentRenderOptions(255);
        }
    }
}

CKBOOL RCKRenderManager::RegisterRasterizerContext(
    CKRasterizerContext *Context, int DriverIndex) {
    if (!Context || DriverIndex < 0 || DriverIndex >= m_DriverCount)
        return FALSE;

    int *registeredDriver = m_ContextDrivers.FindPtr(Context);
    if (registeredDriver)
        return *registeredDriver == DriverIndex;

    return m_ContextDrivers.Insert(Context, DriverIndex, FALSE);
}

void RCKRenderManager::ForgetRasterizerContext(
    CKRasterizerContext *Context) {
    if (!Context)
        return;

    ForgetTextureObjects(Context);
    ForgetVertexBufferObjects(Context);
    ForgetMeshBuffers(Context);
    m_ContextDrivers.Remove(Context);
}

void RCKRenderManager::DeleteAllVertexBuffers() {
    for (CKVertexBuffer **it = (CKVertexBuffer **) m_VertexBuffers.Begin();
         it < (CKVertexBuffer **) m_VertexBuffers.End(); ++it) {
        if (*it) {
            delete *it;
        }
    }
    m_VertexBuffers.Resize(0);
}

void RCKRenderManager::SaveLastFrameMatrix() {
    for (RCK3dEntity **it = (RCK3dEntity **) m_Entities.Begin();
         it != (RCK3dEntity **) m_Entities.End(); ++it) {
        (*it)->SaveLastFrameMatrix();
    }
}

void RCKRenderManager::RegisterLastFrameEntity(RCK3dEntity *entity) {
    if (!entity) return;
    m_Entities.AddIfNotHere(entity);
}

void RCKRenderManager::UnregisterLastFrameEntity(RCK3dEntity *entity) {
    if (!entity) return;
    m_Entities.Remove(entity);
}

void RCKRenderManager::CleanMovedEntities() {
    int count = 0;
    for (RCK3dEntity **it = (RCK3dEntity **) m_MovedEntities.Begin();
         it != (RCK3dEntity **) m_MovedEntities.End(); ++it) {
        RCK3dEntity *entity = *it;
        if ((entity->GetMoveableFlags() & VX_MOVEABLE_RESERVED2) != 0) {
            // Entity was moved this frame, clear both flags
            entity->m_MoveableFlags &= ~(VX_MOVEABLE_HASMOVED | VX_MOVEABLE_RESERVED2);
        } else {
            // Entity was not moved, just clear the flag and keep in list
            entity->m_MoveableFlags &= ~VX_MOVEABLE_RESERVED2;
            m_MovedEntities[count++] = entity;
        }
    }
    m_MovedEntities.Resize(count);
}

void RCKRenderManager::AddTemporaryCallback(CKCallbacksContainer *callbacks, void *Function, void *Argument, CKBOOL preOrPost) {
    VxCallBack cb;
    cb.callback = callbacks;
    cb.argument = Function;
    cb.arg2 = Argument;

    if (preOrPost)
        m_TemporaryPreRenderCallbacks.PushBack(cb);
    else
        m_TemporaryPostRenderCallbacks.PushBack(cb);
}

void RCKRenderManager::RemoveTemporaryCallback(CKCallbacksContainer *callbacks) {
    // Remove from pre-render callbacks array
    for (int i = m_TemporaryPreRenderCallbacks.Size() - 1; i >= 0; --i) {
        if (m_TemporaryPreRenderCallbacks[i].callback == callbacks) {
            m_TemporaryPreRenderCallbacks.RemoveAt(i);
        }
    }

    // Remove from post-render callbacks array
    for (int i = m_TemporaryPostRenderCallbacks.Size() - 1; i >= 0; --i) {
        if (m_TemporaryPostRenderCallbacks[i].callback == callbacks) {
            m_TemporaryPostRenderCallbacks.RemoveAt(i);
        }
    }
}

void RCKRenderManager::ClearTemporaryCallbacks() {
    m_TemporaryPreRenderCallbacks.Resize(0);
    m_TemporaryPostRenderCallbacks.Resize(0);
}

void RCKRenderManager::RemoveAllTemporaryCallbacks() {
    // Remove all pre-render callbacks from their containers
    for (VxCallBack *it = m_TemporaryPreRenderCallbacks.Begin();
         it != m_TemporaryPreRenderCallbacks.End(); ++it) {
        CKCallbacksContainer *container = (CKCallbacksContainer *) it->callback;
        if (container) {
            container->RemovePreCallback(it->argument, it->arg2);
        }
    }

    // Remove all post-render callbacks from their containers
    for (VxCallBack *it = m_TemporaryPostRenderCallbacks.Begin();
         it != m_TemporaryPostRenderCallbacks.End(); ++it) {
        CKCallbacksContainer *container = (CKCallbacksContainer *) it->callback;
        if (container) {
            container->RemovePostCallback(it->argument, it->arg2);
        }
    }

    ClearTemporaryCallbacks();
}

void RCKRenderManager::RegisterDefaultEffects() {
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
    effectTexGen.ParameterType = CKPGUID_TEXGENEFFECT;
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
    effectTexGenRef.ParameterType = CKPGUID_TEXGENREFEFFECT;
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
    effectBumpEnv.ParameterType = CKPGUID_BUMPMAPPARAM;
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
    effectDP3.ParameterType = CKPGUID_3DENTITY;
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
    effectCombine2.ParameterType = CKPGUID_COMBINE2TEX;
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
    effectCombine3.ParameterType = CKPGUID_COMBINE3TEX;
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
    pm->RegisterNewEnum(CKPGUID_MATERIALEFFECT, "Material Effect", (CKSTRING) effectEnum.CStr());
    CKParameterTypeDesc *typeDesc = pm->GetParameterTypeDescription(CKPGUID_MATERIALEFFECT);
    if (typeDesc)
        typeDesc->dwFlags |= CKPARAMETERTYPE_HIDDEN;

    // Register Tex Coords Generator enum
    pm->RegisterNewEnum(CKPGUID_TEXGENEFFECT, "Tex Coords Generator",
                        "None=0,Transform=1,Reflect=2,Chrome=3,Planar=4,CubeMap Reflect=31,CubeMap SkyMap=32,CubeMap Normals=33,CubeMap Positions=34");
    typeDesc = pm->GetParameterTypeDescription(CKPGUID_TEXGENEFFECT);
    if (typeDesc)
        typeDesc->dwFlags |= CKPARAMETERTYPE_HIDDEN;

    // Register Texture Blending enum
    pm->RegisterNewEnum(CKPGUID_TEXCOMBINE, "Texture Blending",
                        "None=0,Modulate=4,Modulate 2X=5,Modulate 4X=6,Add=7,Add Signed=8,Add Signed 2X=9,Subtract=10,Add Smooth=11,"
                        "Blend Using Diffuse Alpha=12,Blend Using Texture Alpha=13,Blend Using Current Alpha=16,"
                        "Modulate Alpha Add Color=18,Modulate Color Add Alpha=19,Modulate InvAlpha Add Color=20,Modulate InvColor Add Alpha=21");
    typeDesc = pm->GetParameterTypeDescription(CKPGUID_TEXCOMBINE);
    if (typeDesc)
        typeDesc->dwFlags |= CKPARAMETERTYPE_HIDDEN;

    // Register TexgenReferential structure: (TexGen, Referential)
    XArray<CKGUID> texgenRefGuids;
    texgenRefGuids.PushBack(CKPGUID_TEXGENEFFECT);
    texgenRefGuids.PushBack(CKPGUID_3DENTITY);
    pm->RegisterNewStructure(CKPGUID_TEXGENREFEFFECT, "TexgenReferential",
                             "TexGen,Referential",
                             texgenRefGuids);

    // Register Combine2Textures structure: (Combine, TexGen, Referential)
    XArray<CKGUID> combine2TexGuids;
    combine2TexGuids.PushBack(CKPGUID_TEXCOMBINE);
    combine2TexGuids.PushBack(CKPGUID_TEXGENEFFECT);
    combine2TexGuids.PushBack(CKPGUID_3DENTITY);
    pm->RegisterNewStructure(CKPGUID_COMBINE2TEX, "Combine 2 Textures",
                             "Combine,TexGen,Referential",
                             combine2TexGuids);

    // Register Combine3Textures structure: (Combine1, TexGen1, Ref1, Combine2, TexGen2, Ref2)
    XArray<CKGUID> combine3TexGuids;
    combine3TexGuids.PushBack(CKPGUID_TEXCOMBINE);
    combine3TexGuids.PushBack(CKPGUID_TEXGENEFFECT);
    combine3TexGuids.PushBack(CKPGUID_3DENTITY);
    combine3TexGuids.PushBack(CKPGUID_TEXCOMBINE);
    combine3TexGuids.PushBack(CKPGUID_TEXGENEFFECT);
    combine3TexGuids.PushBack(CKPGUID_3DENTITY);
    pm->RegisterNewStructure(CKPGUID_COMBINE3TEX, "Combine 3 Textures",
                             "Combine1,TexGen1,Ref1,Combine2,TexGen2,Ref2",
                             combine3TexGuids);

    // Register BumpmapParameters structure: (Amplitude, EnvMap Combine, EnvMap TexGen, EnvMap Referential)
    XArray<CKGUID> bumpmapParamGuids;
    bumpmapParamGuids.PushBack(CKPGUID_FLOAT);
    bumpmapParamGuids.PushBack(CKPGUID_TEXCOMBINE);
    bumpmapParamGuids.PushBack(CKPGUID_TEXGENEFFECT);
    bumpmapParamGuids.PushBack(CKPGUID_3DENTITY);
    pm->RegisterNewStructure(CKPGUID_BUMPMAPPARAM, "Bumpmap Parameters",
                             "Amplitude,EnvMap Combine,EnvMap TexGen,EnvMap Referential",
                             bumpmapParamGuids);
}

// =====================================================
// Scene Graph Node Management
// =====================================================

CKSceneGraphNode *RCKRenderManager::CreateNode(RCK3dEntity *entity) {
    CKSceneGraphNode *node = new CKSceneGraphNode(entity);
    if (node) {
        m_SceneGraphRootNode.AddNode(node);
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

// =====================================================
// Driver Management
// =====================================================

CKRasterizerDriver *RCKRenderManager::GetDriver(int DriverIndex) {
    // IDA: 0x1006f7f0
    if (DriverIndex < 0 || DriverIndex >= m_DriverCount)
        return nullptr;
    return m_Drivers[DriverIndex].RasterizerDriver;
}

VxDriverDescEx *RCKRenderManager::GetDriverDescription(int DriverIndex) {
    if (DriverIndex < 0 || DriverIndex >= m_DriverCount)
        return nullptr;

    VxDriverDescEx *driver = &m_Drivers[DriverIndex];
    if (!driver->CapsUpToDate)
        UpdateDriverDescCaps(driver);
    return driver;
}

VxDriverDescEx *RCKRenderManager::GetDriverDescription(CKRasterizerContext *Context) {
    if (!Context)
        return nullptr;

    int *driverIndex = m_ContextDrivers.FindPtr(Context);
    return driverIndex ? GetDriverDescription(*driverIndex) : nullptr;
}

CKRasterizerContext *RCKRenderManager::GetFullscreenContext() {
    for (ContextDriverTable::Iterator it = m_ContextDrivers.Begin();
         it != m_ContextDrivers.End(); ++it) {
        CKRasterizerContext *context = it.GetKey();
        CKRasterizerContextDesc desc = {};
        if (context && context->GetDesc(&desc) && desc.Fullscreen)
            return context;
    }
    return nullptr;
}

void RCKRenderManager::RefreshDriverCaps(int DriverIndex) {
    if (DriverIndex < 0 || DriverIndex >= m_DriverCount)
        return;
    CKRasterizerDriver *driver = m_Drivers[DriverIndex].RasterizerDriver;
    if (!driver)
        return;
    // Context creation refreshes native limits before this method is called.
    // Keep the engine on the driver interface instead of reaching into a
    // concrete rasterizer implementation.
    UpdateDriverDescCaps(&m_Drivers[DriverIndex]);
}

int RCKRenderManager::GetPreferredSoftwareDriver() {
    // IDA: 0x100733f0
    // First pass: prefer OpenGL software driver
    for (int i = 0; i < m_DriverCount; ++i) {
        if (!m_Drivers[i].Hardware && m_Drivers[i].Caps2D.Family == CKRST_OPENGL) {
            return i;
        }
    }

    // Second pass: any software driver
    for (int i = 0; i < m_DriverCount; ++i) {
        if (!m_Drivers[i].Hardware) {
            return i;
        }
    }

    return 0;
}

void RCKRenderManager::FindNearestTextureFormatWithAlpha(
    int DriverIndex, VxImageDescEx &Format) const {
    if (DriverIndex < 0 || DriverIndex >= m_DriverCount)
        return;

    const XSArray<VxImageDescEx> &formats =
        m_Drivers[DriverIndex].TextureFormats;
    int best = -1;
    int bestDifference = 64;
    for (int i = 0; i < formats.Size(); ++i) {
        if (!formats[i].AlphaMask)
            continue;
        const int difference =
            XAbs((int)formats[i].BitsPerPixel - (int)Format.BitsPerPixel);
        if (difference < bestDifference) {
            best = i;
            bestDifference = difference;
        }
    }

    if (best >= 0) {
        Format.BitsPerPixel = formats[best].BitsPerPixel;
        Format.RedMask = formats[best].RedMask;
        Format.GreenMask = formats[best].GreenMask;
        Format.BlueMask = formats[best].BlueMask;
        Format.AlphaMask = formats[best].AlphaMask;
    }
}

RCKRenderManager::VertexBufferEntry *RCKRenderManager::FindVertexBufferEntry(
    RCKVertexBuffer *Buffer, CKRasterizerContext *Context) {
    VertexBufferEntryArray *entries = Buffer
        ? m_VertexBufferEntries.FindPtr(Buffer)
        : nullptr;
    if (!entries)
        return nullptr;

    for (VertexBufferEntry *entry = entries->Begin();
         entry != entries->End(); ++entry) {
        if (entry->Context == Context)
            return entry;
    }
    return nullptr;
}

RCKRenderManager::VertexBufferEntry *RCKRenderManager::AddVertexBufferEntry(
    RCKVertexBuffer *Buffer, CKRasterizerContext *Context) {
    if (!Buffer || !Context)
        return nullptr;

    VertexBufferEntry *entry = FindVertexBufferEntry(Buffer, Context);
    if (entry)
        return entry;

    VertexBufferEntryArray *entries = m_VertexBufferEntries.FindPtr(Buffer);
    if (!entries) {
        VertexBufferEntryArray empty;
        if (!m_VertexBufferEntries.Insert(Buffer, empty, FALSE))
            return nullptr;
        entries = m_VertexBufferEntries.FindPtr(Buffer);
    }
    if (!entries)
        return nullptr;

    VertexBufferEntry newEntry = {};
    newEntry.Context = Context;
    entries->PushBack(newEntry);
    return &entries->Back();
}

CKBOOL RCKRenderManager::SelectVertexBufferObject(
    RCKVertexBuffer *Buffer, CKRasterizerContext *Context) {
    if (!Buffer || !Context)
        return FALSE;

    VertexBufferEntry *entry = FindVertexBufferEntry(Buffer, Context);
    VertexBufferEntry state = {};
    state.Context = Context;
    if (entry)
        state = *entry;
    Buffer->SetRasterizerObjectState(state);
    return entry ? entry->Valid : FALSE;
}

CKBOOL RCKRenderManager::VertexBufferObjectUpdated(RCKVertexBuffer *Buffer) {
    if (!Buffer)
        return FALSE;

    VertexBufferEntry state = {};
    Buffer->GetRasterizerObjectState(state);
    if (!state.Context || !state.ObjectIndex)
        return FALSE;

    VertexBufferEntry *entry = AddVertexBufferEntry(Buffer, state.Context);
    if (!entry)
        return FALSE;

    *entry = state;
    return TRUE;
}

CKBOOL RCKRenderManager::DeleteVertexBufferObjects(RCKVertexBuffer *Buffer, CKBOOL PreserveOnFailure) {
    if (!Buffer)
        return FALSE;

    VertexBufferEntry selected = {};
    Buffer->GetRasterizerObjectState(selected);
    VertexBufferEntryArray *entries = m_VertexBufferEntries.FindPtr(Buffer);
    CKBOOL result = TRUE;
    if (entries) {
        for (int i = entries->Size() - 1; i >= 0; --i) {
            VertexBufferEntry &entry = (*entries)[i];
            if (!entry.Context || !entry.ObjectIndex) {
                entries->RemoveAt(i);
                continue;
            }
            if (!entry.Context->DeleteObject(entry.ObjectIndex, CKRST_OBJ_VERTEXBUFFER)) {
                result = FALSE;
                if (PreserveOnFailure)
                    continue;
            }
            entries->RemoveAt(i);
        }
        if (entries->Size() == 0) {
            m_VertexBufferEntries.Remove(Buffer);
            VertexBufferEntry empty = {};
            Buffer->SetRasterizerObjectState(empty);
        } else {
            VertexBufferEntry *retained = nullptr;
            for (VertexBufferEntry *entry = entries->Begin(); entry != entries->End(); ++entry) {
                if (entry->Context == selected.Context) {
                    retained = entry;
                    break;
                }
            }
            Buffer->SetRasterizerObjectState(retained ? *retained : (*entries)[0]);
        }
    } else {
        if (selected.Context && selected.ObjectIndex)
            result = selected.Context->DeleteObject(selected.ObjectIndex, CKRST_OBJ_VERTEXBUFFER);
        if (result || !PreserveOnFailure) {
            VertexBufferEntry empty = {};
            Buffer->SetRasterizerObjectState(empty);
        }
    }
    return result;
}

void RCKRenderManager::ForgetVertexBufferObjects(
    CKRasterizerContext *Context) {
    if (!Context)
        return;

    for (VertexBufferEntryTable::Iterator it = m_VertexBufferEntries.Begin();
         it != m_VertexBufferEntries.End();) {
        RCKVertexBuffer *buffer = it.GetKey();
        VertexBufferEntryArray &entries = *it;
        for (int i = entries.Size() - 1; i >= 0; --i) {
            if (entries[i].Context == Context)
                entries.RemoveAt(i);
        }

        VertexBufferEntry selected = {};
        if (buffer)
            buffer->GetRasterizerObjectState(selected);
        if (buffer && selected.Context == Context) {
            if (entries.Size() > 0)
                buffer->SetRasterizerObjectState(entries[0]);
            else {
                VertexBufferEntry empty = {};
                buffer->SetRasterizerObjectState(empty);
            }
        }

        if (entries.Size() == 0)
            it = m_VertexBufferEntries.Remove(it);
        else
            ++it;
    }
}

RCKRenderManager::MeshBufferEntry *RCKRenderManager::FindMeshBufferEntry(
    RCKMesh *Mesh, CKRasterizerContext *Context) {
    MeshBufferEntryArray *entries = Mesh
        ? m_MeshBufferEntries.FindPtr(Mesh->GetID())
        : nullptr;
    if (!entries)
        return nullptr;

    for (MeshBufferEntry *entry = entries->Begin();
         entry != entries->End(); ++entry) {
        if (entry->Context == Context)
            return entry;
    }
    return nullptr;
}

RCKRenderManager::MeshBufferEntry *RCKRenderManager::AddMeshBufferEntry(
    RCKMesh *Mesh, CKRasterizerContext *Context) {
    if (!Mesh || !Context)
        return nullptr;

    MeshBufferEntry *entry = FindMeshBufferEntry(Mesh, Context);
    if (entry)
        return entry;

    const CK_ID meshId = Mesh->GetID();
    MeshBufferEntryArray *entries = m_MeshBufferEntries.FindPtr(meshId);
    if (!entries) {
        MeshBufferEntryArray empty;
        if (!m_MeshBufferEntries.Insert(meshId, empty, FALSE))
            return nullptr;
        entries = m_MeshBufferEntries.FindPtr(meshId);
    }
    if (!entries)
        return nullptr;

    MeshBufferEntry newEntry = {};
    newEntry.Context = Context;
    entries->PushBack(newEntry);
    return &entries->Back();
}

void RCKRenderManager::ResetMeshBufferState(
    RCKMesh *Mesh, CKRasterizerContext *Context) {
    if (!Mesh)
        return;

    MeshBufferEntry state = {};
    state.Context = Context;
    Mesh->SetRasterizerBufferState(state);
}

void RCKRenderManager::RestoreMeshBufferState(
    RCKMesh *Mesh, const MeshBufferEntry &Entry) {
    if (Mesh)
        Mesh->SetRasterizerBufferState(Entry);
}

CKBOOL RCKRenderManager::SelectMeshBuffers(
    RCKMesh *Mesh, CKRasterizerContext *Context) {
    if (!Mesh || !Context)
        return FALSE;

    MeshBufferEntry *entry = FindMeshBufferEntry(Mesh, Context);
    if (!entry) {
        ResetMeshBufferState(Mesh, Context);
        return FALSE;
    }

    RestoreMeshBufferState(Mesh, *entry);
    return TRUE;
}

CKBOOL RCKRenderManager::StoreMeshBuffers(RCKMesh *Mesh) {
    if (!Mesh)
        return FALSE;

    MeshBufferEntry state = {};
    Mesh->GetRasterizerBufferState(state);
    if (!state.Context)
        return FALSE;

    MeshBufferEntry *entry = FindMeshBufferEntry(Mesh, state.Context);
    if (!entry && !state.VertexBuffer && !state.IndexBuffer)
        return FALSE;
    if (!entry)
        entry = AddMeshBufferEntry(Mesh, state.Context);
    if (!entry)
        return FALSE;

    *entry = state;
    return TRUE;
}

void RCKRenderManager::MeshVerticesChanged(RCKMesh *Mesh) {
    MeshBufferEntryArray *entries = Mesh
        ? m_MeshBufferEntries.FindPtr(Mesh->GetID())
        : nullptr;
    if (!entries)
        return;

    for (MeshBufferEntry *entry = entries->Begin();
         entry != entries->End(); ++entry) {
        entry->VertexBufferReady = 0;
    }
}

void RCKRenderManager::MeshIndicesChanged(RCKMesh *Mesh) {
    MeshBufferEntryArray *entries = Mesh
        ? m_MeshBufferEntries.FindPtr(Mesh->GetID())
        : nullptr;
    if (!entries)
        return;

    for (MeshBufferEntry *entry = entries->Begin();
         entry != entries->End(); ++entry) {
        entry->IndexBufferReady = FALSE;
    }
}

CKBOOL RCKRenderManager::DeleteMeshBuffers(RCKMesh *Mesh, CKBOOL PreserveOnFailure) {
    if (!Mesh)
        return FALSE;

    const CK_ID meshId = Mesh->GetID();
    MeshBufferEntry selected = {};
    Mesh->GetRasterizerBufferState(selected);
    MeshBufferEntryArray *entries = m_MeshBufferEntries.FindPtr(meshId);
    CKBOOL found = FALSE;
    CKBOOL result = TRUE;
    if (entries) {
        for (int i = entries->Size() - 1; i >= 0; --i) {
            MeshBufferEntry &entry = (*entries)[i];
            if (!entry.Context) {
                entries->RemoveAt(i);
                continue;
            }
            if (entry.VertexBuffer) {
                found = TRUE;
                entry.VertexBufferReady = 0;
                if (entry.Context->DeleteObject(entry.VertexBuffer, CKRST_OBJ_VERTEXBUFFER) || !PreserveOnFailure)
                    ClearMeshVertexBufferState(entry);
                else
                    result = FALSE;
            }
            if (entry.IndexBuffer) {
                found = TRUE;
                entry.IndexBufferReady = FALSE;
                if (entry.Context->DeleteObject(entry.IndexBuffer, CKRST_OBJ_INDEXBUFFER) || !PreserveOnFailure)
                    ClearMeshIndexBufferState(entry);
                else
                    result = FALSE;
            }
            if (!entry.VertexBuffer && !entry.IndexBuffer)
                entries->RemoveAt(i);
        }
        if (entries->Size() == 0) {
            m_MeshBufferEntries.Remove(meshId);
            ResetMeshBufferState(Mesh, nullptr);
        } else {
            MeshBufferEntry *retained = nullptr;
            for (MeshBufferEntry *entry = entries->Begin(); entry != entries->End(); ++entry) {
                if (entry->Context == selected.Context) {
                    retained = entry;
                    break;
                }
            }
            RestoreMeshBufferState(Mesh, retained ? *retained : (*entries)[0]);
        }
    } else {
        MeshBufferEntry state = selected;
        if (state.Context && state.VertexBuffer) {
            found = TRUE;
            state.VertexBufferReady = 0;
            if (state.Context->DeleteObject(state.VertexBuffer, CKRST_OBJ_VERTEXBUFFER) || !PreserveOnFailure)
                ClearMeshVertexBufferState(state);
            else
                result = FALSE;
        }
        if (state.Context && state.IndexBuffer) {
            found = TRUE;
            state.IndexBufferReady = FALSE;
            if (state.Context->DeleteObject(state.IndexBuffer, CKRST_OBJ_INDEXBUFFER) || !PreserveOnFailure)
                ClearMeshIndexBufferState(state);
            else
                result = FALSE;
        }
        if (state.VertexBuffer || state.IndexBuffer)
            RestoreMeshBufferState(Mesh, state);
        else
            ResetMeshBufferState(Mesh, nullptr);
    }
    return found && result;
}

void RCKRenderManager::ForgetMeshBuffers(CKRasterizerContext *Context) {
    if (!Context)
        return;

    for (MeshBufferEntryTable::Iterator it = m_MeshBufferEntries.Begin();
         it != m_MeshBufferEntries.End();) {
        MeshBufferEntryArray &entries = *it;
        for (int i = entries.Size() - 1; i >= 0; --i) {
            if (entries[i].Context == Context)
                entries.RemoveAt(i);
        }

        CKObject *object = m_Context->GetObject(it.GetKey());
        RCKMesh *mesh = object && CKIsChildClassOf(object, CKCID_MESH)
            ? static_cast<RCKMesh *>(object)
            : nullptr;
        MeshBufferEntry selected = {};
        if (mesh)
            mesh->GetRasterizerBufferState(selected);
        if (entries.Size() == 0) {
            if (mesh && selected.Context == Context)
                ResetMeshBufferState(mesh, nullptr);
            it = m_MeshBufferEntries.Remove(it);
            continue;
        }

        if (!mesh) {
            it = m_MeshBufferEntries.Remove(it);
            continue;
        }

        if (selected.Context == Context)
            RestoreMeshBufferState(mesh, entries[0]);

        ++it;
    }
}

RCKRenderManager::TextureEntry *RCKRenderManager::FindTextureEntry(
    CKObject *Object, CKRasterizerContext *Context) {
    TextureEntryArray *entries = Object
        ? m_TextureEntries.FindPtr(Object->GetID())
        : nullptr;
    if (!entries)
        return nullptr;

    for (TextureEntry *entry = entries->Begin(); entry != entries->End(); ++entry) {
        if (entry->Context == Context)
            return entry;
    }
    return nullptr;
}

RCKRenderManager::TextureEntry *RCKRenderManager::AddTextureEntry(
    CKObject *Object, CKRasterizerContext *Context) {
    if (!Object || !Context)
        return nullptr;

    TextureEntry *entry = FindTextureEntry(Object, Context);
    if (entry)
        return entry;

    const CK_ID objectId = Object->GetID();
    TextureEntryArray *entries = m_TextureEntries.FindPtr(objectId);
    if (!entries) {
        TextureEntryArray empty;
        if (!m_TextureEntries.Insert(objectId, empty, FALSE))
            return nullptr;
        entries = m_TextureEntries.FindPtr(objectId);
    }
    if (!entries)
        return nullptr;

    TextureEntry newEntry = {};
    newEntry.Context = Context;
    newEntry.Dirty = TRUE;
    entries->PushBack(newEntry);
    return &entries->Back();
}

CKBOOL RCKRenderManager::DeleteTextureEntry(
    CKObject *Object, CKRasterizerContext *Context) {
    if (!Object || !Context)
        return FALSE;

    const CK_ID objectId = Object->GetID();
    TextureEntryArray *entries = m_TextureEntries.FindPtr(objectId);
    if (!entries)
        return FALSE;

    for (int i = 0; i < entries->Size(); ++i) {
        TextureEntry &entry = (*entries)[i];
        if (entry.Context != Context)
            continue;

        const CKBOOL result = entry.ObjectIndex != 0
            ? Context->DeleteObject(entry.ObjectIndex, CKRST_OBJ_TEXTURE)
            : FALSE;
        if (!result && entry.ObjectIndex != 0)
            return FALSE;
        entries->RemoveAt(i);
        if (entries->Size() == 0)
            m_TextureEntries.Remove(objectId);
        return result;
    }
    return FALSE;
}

CKBOOL RCKRenderManager::DeleteTextureEntries(CKObject *Object, CKBOOL PreserveOnFailure) {
    if (!Object)
        return FALSE;

    const CK_ID objectId = Object->GetID();
    TextureEntryArray *entries = m_TextureEntries.FindPtr(objectId);
    if (!entries)
        return FALSE;

    CKBOOL found = FALSE;
    CKBOOL result = TRUE;
    for (int i = entries->Size() - 1; i >= 0; --i) {
        TextureEntry &entry = (*entries)[i];
        if (!entry.Context || entry.ObjectIndex == 0) {
            entries->RemoveAt(i);
            continue;
        }
        found = TRUE;
        if (!entry.Context->DeleteObject(entry.ObjectIndex, CKRST_OBJ_TEXTURE)) {
            result = FALSE;
            if (PreserveOnFailure)
                continue;
        }
        entries->RemoveAt(i);
    }
    if (entries->Size() == 0)
        m_TextureEntries.Remove(objectId);
    return found && result;
}

void RCKRenderManager::ResetTextureObjectState(
    RCKTexture *Texture, CKRasterizerContext *Context) {
    Texture->m_RasterizerContext = Context;
    Texture->m_ObjectIndex = 0;
    Texture->m_InVideoMemory = FALSE;
    Texture->m_TextureFlags = 0;
    Texture->m_CachedMipMapCount = 0;
    memset(&Texture->m_VideoFormat, 0, sizeof(Texture->m_VideoFormat));
}

void RCKRenderManager::RestoreTextureObjectState(
    RCKTexture *Texture, const TextureEntry &Entry) {
    Texture->m_RasterizerContext = Entry.Context;
    Texture->m_ObjectIndex = Entry.ObjectIndex;
    Texture->m_InVideoMemory = Entry.ObjectIndex != 0;
    Texture->m_TextureFlags = Entry.Flags;
    Texture->m_CachedMipMapCount = Entry.MipMapCount;
    Texture->m_VideoFormat = Entry.Format;
}

void RCKRenderManager::ResetTextureObjectState(
    RCKSprite *Sprite, CKRasterizerContext *Context) {
    Sprite->m_RasterizerContext = Context;
    Sprite->m_ObjectIndex = 0;
    Sprite->m_InVideoMemory = FALSE;
    memset(&Sprite->m_VideoFormatDesc, 0, sizeof(Sprite->m_VideoFormatDesc));
}

void RCKRenderManager::RestoreTextureObjectState(
    RCKSprite *Sprite, const TextureEntry &Entry) {
    Sprite->m_RasterizerContext = Entry.Context;
    Sprite->m_ObjectIndex = Entry.ObjectIndex;
    Sprite->m_InVideoMemory = Entry.ObjectIndex != 0;
    Sprite->m_VideoFormatDesc = Entry.Format;
}

CKBOOL RCKRenderManager::SelectTextureObject(
    RCKTexture *Texture, CKRasterizerContext *Context) {
    if (!Texture || !Context)
        return FALSE;

    if (Texture->ToRestore())
        TextureObjectChanged(Texture);

    TextureEntry *entry = FindTextureEntry(Texture, Context);
    if (!entry) {
        ResetTextureObjectState(Texture, Context);
        return FALSE;
    }

    RestoreTextureObjectState(Texture, *entry);
    return Texture->m_InVideoMemory;
}

CKBOOL RCKRenderManager::TextureObjectNeedsUpdate(RCKTexture *Texture) {
    if (!Texture || !Texture->m_RasterizerContext || !Texture->m_InVideoMemory)
        return FALSE;

    if (Texture->ToRestore())
        TextureObjectChanged(Texture);

    TextureEntry *entry = FindTextureEntry(Texture, Texture->m_RasterizerContext);
    return entry ? entry->Dirty : TRUE;
}

void RCKRenderManager::TextureObjectChanged(RCKTexture *Texture) {
    TextureEntryArray *entries = Texture
        ? m_TextureEntries.FindPtr(Texture->GetID())
        : nullptr;
    if (!entries)
        return;

    for (TextureEntry *entry = entries->Begin(); entry != entries->End(); ++entry)
        entry->Dirty = TRUE;
}

void RCKRenderManager::TextureObjectUpdated(RCKTexture *Texture, CKBOOL Dirty) {
    if (!Texture || !Texture->m_RasterizerContext || !Texture->m_InVideoMemory ||
        Texture->m_ObjectIndex == 0)
        return;

    TextureEntry *entry = AddTextureEntry(Texture, Texture->m_RasterizerContext);
    if (!entry)
        return;

    entry->ObjectIndex = Texture->m_ObjectIndex;
    entry->Flags = Texture->m_TextureFlags;
    entry->MipMapCount = Texture->m_CachedMipMapCount;
    entry->Format = Texture->m_VideoFormat;
    entry->Dirty = Dirty;
}

CKBOOL RCKRenderManager::DeleteTextureObject(
    RCKTexture *Texture, CKRasterizerContext *Context) {
    if (!Texture || !Context)
        return FALSE;

    TextureEntry *entry = FindTextureEntry(Texture, Context);
    const CKBOOL result = entry
        ? DeleteTextureEntry(Texture, Context)
        : (Texture->m_RasterizerContext == Context && Texture->m_ObjectIndex != 0
               ? Context->DeleteObject(Texture->m_ObjectIndex, CKRST_OBJ_TEXTURE)
               : FALSE);
    if (result && Texture->m_RasterizerContext == Context)
        ResetTextureObjectState(Texture, Context);
    return result;
}

CKBOOL RCKRenderManager::DeleteTextureObjects(RCKTexture *Texture, CKBOOL PreserveOnFailure) {
    if (!Texture)
        return FALSE;

    TextureEntryArray *entries = m_TextureEntries.FindPtr(Texture->GetID());
    const CKBOOL tracked = entries != nullptr;
    const CKBOOL result = entries
        ? DeleteTextureEntries(Texture, PreserveOnFailure)
        : (Texture->m_RasterizerContext && Texture->m_ObjectIndex != 0
               ? Texture->m_RasterizerContext->DeleteObject(Texture->m_ObjectIndex, CKRST_OBJ_TEXTURE)
               : FALSE);
    if (result || !PreserveOnFailure) {
        ResetTextureObjectState(Texture, Texture->m_RasterizerContext);
    } else {
        TextureEntry *retained = FindTextureEntry(Texture, Texture->m_RasterizerContext);
        entries = m_TextureEntries.FindPtr(Texture->GetID());
        if (!retained && entries && entries->Size() > 0)
            retained = &(*entries)[0];
        if (retained)
            RestoreTextureObjectState(Texture, *retained);
        else if (tracked)
            ResetTextureObjectState(Texture, Texture->m_RasterizerContext);
    }
    return result;
}

CKBOOL RCKRenderManager::SelectSpriteObject(
    RCKSprite *Sprite, CKRasterizerContext *Context) {
    if (!Sprite || !Context)
        return FALSE;

    if (Sprite->ToRestore())
        SpriteObjectChanged(Sprite);

    TextureEntry *entry = FindTextureEntry(Sprite, Context);
    if (!entry) {
        ResetTextureObjectState(Sprite, Context);
        return FALSE;
    }

    RestoreTextureObjectState(Sprite, *entry);
    return Sprite->m_InVideoMemory;
}

CKBOOL RCKRenderManager::SpriteObjectNeedsUpdate(RCKSprite *Sprite) {
    if (!Sprite || !Sprite->m_RasterizerContext || !Sprite->m_InVideoMemory)
        return FALSE;

    if (Sprite->ToRestore())
        SpriteObjectChanged(Sprite);

    TextureEntry *entry = FindTextureEntry(Sprite, Sprite->m_RasterizerContext);
    return entry ? entry->Dirty : TRUE;
}

void RCKRenderManager::SpriteObjectChanged(RCKSprite *Sprite) {
    TextureEntryArray *entries = Sprite
        ? m_TextureEntries.FindPtr(Sprite->GetID())
        : nullptr;
    if (!entries)
        return;

    for (TextureEntry *entry = entries->Begin(); entry != entries->End(); ++entry)
        entry->Dirty = TRUE;
}

void RCKRenderManager::SpriteObjectUpdated(RCKSprite *Sprite, CKBOOL Dirty) {
    if (!Sprite || !Sprite->m_RasterizerContext || !Sprite->m_InVideoMemory ||
        Sprite->m_ObjectIndex == 0)
        return;

    TextureEntry *entry = AddTextureEntry(Sprite, Sprite->m_RasterizerContext);
    if (!entry)
        return;

    entry->ObjectIndex = Sprite->m_ObjectIndex;
    entry->Flags = 0;
    entry->MipMapCount = 0;
    entry->Format = Sprite->m_VideoFormatDesc;
    entry->Dirty = Dirty;
}

CKBOOL RCKRenderManager::DeleteSpriteObject(
    RCKSprite *Sprite, CKRasterizerContext *Context) {
    if (!Sprite || !Context)
        return FALSE;

    TextureEntry *entry = FindTextureEntry(Sprite, Context);
    const CKBOOL result = entry
        ? DeleteTextureEntry(Sprite, Context)
        : (Sprite->m_RasterizerContext == Context && Sprite->m_ObjectIndex != 0
               ? Context->DeleteObject(Sprite->m_ObjectIndex, CKRST_OBJ_TEXTURE)
               : FALSE);
    if (result && Sprite->m_RasterizerContext == Context)
        ResetTextureObjectState(Sprite, Context);
    return result;
}

CKBOOL RCKRenderManager::DeleteSpriteObjects(RCKSprite *Sprite, CKBOOL PreserveOnFailure) {
    if (!Sprite)
        return FALSE;

    TextureEntryArray *entries = m_TextureEntries.FindPtr(Sprite->GetID());
    const CKBOOL tracked = entries != nullptr;
    const CKBOOL result = entries
        ? DeleteTextureEntries(Sprite, PreserveOnFailure)
        : (Sprite->m_ObjectIndex == 0
               ? TRUE
               : (Sprite->m_RasterizerContext
                      ? Sprite->m_RasterizerContext->DeleteObject(Sprite->m_ObjectIndex, CKRST_OBJ_TEXTURE)
                      : FALSE));
    if (result || !PreserveOnFailure) {
        ResetTextureObjectState(Sprite, Sprite->m_RasterizerContext);
    } else {
        TextureEntry *retained = FindTextureEntry(Sprite, Sprite->m_RasterizerContext);
        entries = m_TextureEntries.FindPtr(Sprite->GetID());
        if (!retained && entries && entries->Size() > 0)
            retained = &(*entries)[0];
        if (retained)
            RestoreTextureObjectState(Sprite, *retained);
        else if (tracked)
            ResetTextureObjectState(Sprite, Sprite->m_RasterizerContext);
    }
    return result;
}

void RCKRenderManager::ForgetTextureObjects(CKRasterizerContext *Context) {
    if (!Context)
        return;

    for (TextureEntryTable::Iterator it = m_TextureEntries.Begin();
         it != m_TextureEntries.End();) {
        TextureEntryArray &entries = *it;
        for (int i = entries.Size() - 1; i >= 0; --i) {
            if (entries[i].Context == Context)
                entries.RemoveAt(i);
        }

        CKObject *object = m_Context->GetObject(it.GetKey());
        if (!object) {
            it = m_TextureEntries.Remove(it);
            continue;
        }

        RCKTexture *texture = CKIsChildClassOf(object, CKCID_TEXTURE)
            ? static_cast<RCKTexture *>(object)
            : nullptr;
        RCKSprite *sprite = CKIsChildClassOf(object, CKCID_SPRITE)
            ? static_cast<RCKSprite *>(object)
            : nullptr;

        if (entries.Size() == 0) {
            if (texture && texture->m_RasterizerContext == Context)
                ResetTextureObjectState(texture, nullptr);
            else if (sprite && sprite->m_RasterizerContext == Context)
                ResetTextureObjectState(sprite, nullptr);
            it = m_TextureEntries.Remove(it);
            continue;
        }

        if (texture && texture->m_RasterizerContext == Context)
            RestoreTextureObjectState(texture, entries[0]);
        else if (sprite && sprite->m_RasterizerContext == Context)
            RestoreTextureObjectState(sprite, entries[0]);

        ++it;
    }
}

#include "RCKRenderManager.h"
#include "RCKRenderContext.h"

RCKRenderManager::RCKRenderManager(CKContext *context) : CKRenderManager(context, (CKSTRING)"Render Manager") {
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

    // sub_10072FC6(this);

    m_RenderContextMaskFree = -1;
    m_Context->RegisterNewManager(this);
}

CKERROR RCKRenderManager::PreClearAll() {
    m_CKSceneGraphRootNode.Clear();
    DetachAllObjects();
    ClearTemporaryCallbacks();
    DeleteAllVertexBuffers();

    for (int i = 0; i < CKGetClassCount(); ++i) {
        if (CKIsChildClassOf(i, CKCID_RENDERCONTEXT)) {
            int count = m_Context->GetObjectsCountByClassID(i);
            CK_ID *ids = m_Context->GetObjectsListByClassID(i);
            for (int j = 0; j < count; ++j) {
                RCKRenderContext *ctx = (RCKRenderContext *)m_Context->GetObject(ids[j]);
                if (ctx)
                    ctx->OnClearAll();
            }
        } else if (CKIsChildClassOf(i, CKCID_3DENTITY)) {
            int count = m_Context->GetObjectsCountByClassID(i);
            CK_ID *ids = m_Context->GetObjectsListByClassID(i);
            for (int j = 0; j < count; ++j) {
                RCK3dEntity *entity = (RCK3dEntity *)m_Context->GetObject(ids[j]);
                if (entity)
                    entity->RemoveAllCallbacks();
            }
        } else if (CKIsChildClassOf(i, CKCID_MESH)) {
            int count = m_Context->GetObjectsCountByClassID(i);
            CK_ID *ids = m_Context->GetObjectsListByClassID(i);
            for (int j = 0; j < count; ++j) {
                RCKMesh *mesh = (RCKMesh *)m_Context->GetObject(ids[j]);
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
                    ctx->AddObject((CKRenderObject *)obj);
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
                    ctx->RemoveObject((CKRenderObject *)obj);
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
        RCKRenderContext *ctx = (RCKRenderContext *)GetRenderContext(i);
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

VxDriverDesc *RCKRenderManager::GetRenderDriverDescription(int Driver) {
    if (Driver < 0 || Driver >= m_DriverCount)
        return nullptr;

//    VxDriverDescEx *drv = &m_Drivers[Driver];
//    if (!drv->CapsUpToDate)
//        UpdateDriverDescCaps(drv);
//    return drv->DriverDesc;
}

void RCKRenderManager::GetDesiredTexturesVideoFormat(VxImageDescEx &VideoFormat) {
    VxPixelFormat2ImageDesc((VX_PIXELFORMAT)m_TextureVideoFormat.Value, VideoFormat);
}

void RCKRenderManager::SetDesiredTexturesVideoFormat(VxImageDescEx &VideoFormat) {
    m_TextureVideoFormat.Value = VxImageDesc2PixelFormat(VideoFormat);
}

CKRenderContext *RCKRenderManager::GetRenderContext(int pos) {
    return (CKRenderContext *)m_RenderContexts.GetObject(m_Context, pos);
}

CKRenderContext *RCKRenderManager::GetRenderContextFromPoint(CKPOINT &pt) {
    return nullptr;
}

int RCKRenderManager::GetRenderContextCount() {
    return m_RenderContexts.Size();
}

void RCKRenderManager::Process() {
    for (CK_ID *it = m_RenderContexts.Begin(); it != m_RenderContexts.End(); ++it) {
        CKRenderContext *dev = (CKRenderContext *)m_Context->GetObject(*it);
        if (dev)
            dev->Render();
    }
}

void RCKRenderManager::FlushTextures() {
    int textureCount = m_Context->GetObjectsCountByClassID(CKCID_TEXTURE);
    CK_ID *textureIds = m_Context->GetObjectsListByClassID(CKCID_TEXTURE);
    for (int i = 0; i < textureCount; ++i) {
        CKTexture *texture = (CKTexture *)m_Context->GetObject(textureIds[i]);
        if (texture)
            texture->FreeVideoMemory();
    }

    int spriteCount = m_Context->GetObjectsCountByClassID(CKCID_SPRITE);
    CK_ID *spriteIds = m_Context->GetObjectsListByClassID(CKCID_SPRITE);
    for (int i = 0; i < spriteCount; ++i) {
        CKSprite *sprite = (CKSprite *)m_Context->GetObject(spriteIds[i]);
        if (sprite)
            sprite->FreeVideoMemory();
    }

    int spriteTextCount = m_Context->GetObjectsCountByClassID(CKCID_SPRITETEXT);
    CK_ID *spriteTextIds = m_Context->GetObjectsListByClassID(CKCID_SPRITETEXT);
    for (int i = 0; i < spriteTextCount; ++i) {
        CKSpriteText *spriteText = (CKSpriteText *)m_Context->GetObject(spriteTextIds[i]);
        if (spriteText)
            spriteText->FreeVideoMemory();
    }
}

CKRenderContext *RCKRenderManager::CreateRenderContext(void *Window, int Driver, CKRECT *rect, CKBOOL Fullscreen,
                                                       int Bpp, int Zbpp, int StencilBpp, int RefreshRate) {
    RCKRenderContext *dev = (RCKRenderContext *)m_Context->CreateObject(CKCID_RENDERCONTEXT);
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

void RCKRenderManager::ClearTemporaryCallbacks() {

}

void RCKRenderManager::RemoveAllTemporaryCallbacks() {

}
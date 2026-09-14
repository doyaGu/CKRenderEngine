#ifndef RCKRENDERMANAGER_H
#define RCKRENDERMANAGER_H

#include "XObjectArray.h"
#include "XSHashTable.h"
#include "CKRenderEngineTypes.h"
#include "CKRenderEngineEnums.h"
#include "CKRenderManager.h"
#include "CKSceneGraph.h"
#include "RCKRasterizerObjectStates.h"
#include "VertexCacheOptimizer.h"

class RCK3dEntity;
class RCKMesh;
class RCKSprite;
class RCKTexture;
struct RCKVertexBuffer;

class RCKRenderManager : public CKRenderManager {
public:
    explicit RCKRenderManager(CKContext *context);
    ~RCKRenderManager() override;

    CKERROR PreClearAll() override;
    CKERROR PreProcess() override;
    CKERROR PostProcess() override;
    CKERROR SequenceAddedToScene(CKScene *scn, CK_ID *objids, int count) override;
    CKERROR SequenceRemovedFromScene(CKScene *scn, CK_ID *objids, int count) override;
    CKERROR OnCKEnd() override;
    CKERROR OnCKPause() override;
    CKERROR SequenceToBeDeleted(CK_ID *objids, int count) override;
    CKERROR SequenceDeleted(CK_ID *objids, int count) override;
    CKDWORD GetValidFunctionsMask() override;

    int GetRenderDriverCount() override;
    VxDriverDesc *GetRenderDriverDescription(int Driver) override;
    void GetDesiredTexturesVideoFormat(VxImageDescEx &VideoFormat) override;
    void SetDesiredTexturesVideoFormat(VxImageDescEx &VideoFormat) override;
    CKRenderContext *GetRenderContext(int pos) override;
    CKRenderContext *GetRenderContextFromPoint(CKPOINT &pt) override;
    int GetRenderContextCount() override;
    void Process() override;
    void FlushTextures() override;
    CKRenderContext *CreateRenderContext(void *Window, int Driver = 0, CKRECT *rect = NULL, CKBOOL Fullscreen = FALSE, int Bpp = -1, int Zbpp = -1, int StencilBpp = -1, int RefreshRate = 0) override;
    CKERROR DestroyRenderContext(CKRenderContext *context) override;
    void RemoveRenderContext(CKRenderContext *context) override;
    CKVertexBuffer *CreateVertexBuffer() override;
    void DestroyVertexBuffer(CKVertexBuffer *VB) override;
    void SetRenderOptions(CKSTRING RenderOptionString, CKDWORD Value) override;
    const VxEffectDescription &GetEffectDescription(int EffectIndex) override;
    int GetEffectCount() override;
    int AddEffect(const VxEffectDescription &NewEffect) override;

    CKMaterial *GetDefaultMaterial();

    void DetachAllObjects();
    CKBOOL RegisterRasterizerContext(CKRasterizerContext *Context, int DriverIndex);
    void ForgetRasterizerContext(CKRasterizerContext *Context);

    void DeleteAllVertexBuffers();

    void AddTemporaryCallback(
        CKCallbacksContainer *callbacks,
        void *Function,
        void *Argument,
        CKBOOL preOrPost);
    void RemoveTemporaryCallback(CKCallbacksContainer *callbacks);
    void ClearTemporaryCallbacks();
    void RemoveAllTemporaryCallbacks();
    void RegisterDefaultEffects();

    void SaveLastFrameMatrix();
    void CleanMovedEntities();

    // Last-frame tracking list management (mirrors CK2_3D.dll sub_1000D770/D7A0)
    void RegisterLastFrameEntity(RCK3dEntity *entity);
    void UnregisterLastFrameEntity(RCK3dEntity *entity);

    // Scene graph node management
    CKSceneGraphNode *CreateNode(RCK3dEntity *entity);
    void DeleteNode(CKSceneGraphNode *node);
    CKSceneGraphRootNode *GetRootNode() { return &m_SceneGraphRootNode; }

    // Entity movement tracking (called when entities move)
    void AddMovedEntity(RCK3dEntity *entity) { m_MovedEntities.PushBack((CKObject*)entity); }

    // Render context mask management
    CKDWORD GetRenderContextMaskFree() { return m_RenderContextMaskFree; }
    void ReleaseRenderContextMaskFree(CKDWORD mask) { m_RenderContextMaskFree |= mask; }

    // Driver management
    CKRasterizerDriver *GetDriver(int DriverIndex);
    VxDriverDescEx *GetDriverDescription(int DriverIndex);
    VxDriverDescEx *GetDriverDescription(CKRasterizerContext *Context);
    // A driver reports the capability baseline until one of its contexts
    // exists; the concrete rasterizer then lowers numeric limits to what it
    // really supports, so caps are refreshed after a context is created.
    void RefreshDriverCaps(int DriverIndex);
    CKRasterizerContext *GetFullscreenContext();
    int GetPreferredSoftwareDriver();
    void FindNearestTextureFormatWithAlpha(int DriverIndex, VxImageDescEx &Format) const;

    // Resource-object coordination used by the concrete render objects.
    CKBOOL SelectTextureObject(RCKTexture *Texture, CKRasterizerContext *Context);
    CKBOOL TextureObjectNeedsUpdate(RCKTexture *Texture);
    void TextureObjectChanged(RCKTexture *Texture);
    void TextureObjectUpdated(RCKTexture *Texture, CKBOOL Dirty = FALSE);
    CKBOOL DeleteTextureObject(RCKTexture *Texture, CKRasterizerContext *Context);
    CKBOOL DeleteTextureObjects(RCKTexture *Texture, CKBOOL PreserveOnFailure = TRUE);
    CKBOOL SelectSpriteObject(RCKSprite *Sprite, CKRasterizerContext *Context);
    CKBOOL SpriteObjectNeedsUpdate(RCKSprite *Sprite);
    void SpriteObjectChanged(RCKSprite *Sprite);
    void SpriteObjectUpdated(RCKSprite *Sprite, CKBOOL Dirty = FALSE);
    CKBOOL DeleteSpriteObject(RCKSprite *Sprite, CKRasterizerContext *Context);
    CKBOOL DeleteSpriteObjects(RCKSprite *Sprite, CKBOOL PreserveOnFailure = TRUE);
    CKBOOL SelectVertexBufferObject(RCKVertexBuffer *Buffer, CKRasterizerContext *Context);
    CKBOOL VertexBufferObjectUpdated(RCKVertexBuffer *Buffer);
    CKBOOL DeleteVertexBufferObjects(RCKVertexBuffer *Buffer, CKBOOL PreserveOnFailure = TRUE);
    CKBOOL SelectMeshBuffers(RCKMesh *Mesh, CKRasterizerContext *Context);
    CKBOOL StoreMeshBuffers(RCKMesh *Mesh);
    void MeshVerticesChanged(RCKMesh *Mesh);
    void MeshIndicesChanged(RCKMesh *Mesh);
    CKBOOL DeleteMeshBuffers(RCKMesh *Mesh, CKBOOL PreserveOnFailure = TRUE);

    XClassArray<VxCallBack> m_TemporaryPreRenderCallbacks;  // 0x28
    XClassArray<VxCallBack> m_TemporaryPostRenderCallbacks; // 0x34
    XSObjectArray m_RenderContexts;                          // 0x40
    XArray<CKRasterizer *> m_Rasterizers;                    // 0x48
    VxDriverDescEx *m_Drivers;                               // 0x54
    int m_DriverCount;                                       // 0x58
    CKMaterial *m_DefaultMat;                                // 0x5C
    CKDWORD m_RenderContextMaskFree;                         // 0x60
    CKSceneGraphRootNode m_SceneGraphRootNode;               // 0x64 (84 bytes)
    XObjectPointerArray m_MovedEntities;                     // 0xB8
    XObjectPointerArray m_Entities;                          // 0xC4
    VertexCacheOptimizer m_VertexCacheOptimizer;             // 0xD0
    XArray<CKVertexBuffer *> m_VertexBuffers;
    VxOption m_ForceLinearFog;
    VxOption m_ForceSoftware;
    VxOption m_DisableFilter;
    VxOption m_Antialias;
    VxOption m_DisableMipmap;
    VxOption m_DisableSpecular;
    VxOption m_UseIndexBuffers;
    VxOption m_EnableScreenDump;
    VxOption m_VertexCache;
    VxOption m_SortTransparentObjects;
    VxOption m_TextureCacheManagement;
    VxOption m_TextureVideoFormat;
    VxOption m_SpriteVideoFormat;
    XArray<VxOption*> m_Options;
    CK2dEntity *m_2DRootFore;
    CK2dEntity *m_2DRootBack;
    CK_ID m_2DRootBackId;
    CK_ID m_2DRootForeId;
    XClassArray<VxEffectDescription> m_Effects;

private:
    typedef XSHashTable<int, CKRasterizerContext *> ContextDriverTable;

    ContextDriverTable m_ContextDrivers;

    struct TextureEntry {
        CKRasterizerContext *Context;
        CKDWORD ObjectIndex;
        CKDWORD Flags;
        CKDWORD MipMapCount;
        VxImageDescEx Format;
        CKBOOL Dirty;
    };

    typedef XClassArray<TextureEntry> TextureEntryArray;
    typedef XSHashTable<TextureEntryArray, CK_ID> TextureEntryTable;

    TextureEntry *FindTextureEntry(CKObject *Object,
                                   CKRasterizerContext *Context);
    TextureEntry *AddTextureEntry(CKObject *Object,
                                  CKRasterizerContext *Context);
    CKBOOL DeleteTextureEntry(CKObject *Object,
                              CKRasterizerContext *Context);
    CKBOOL DeleteTextureEntries(CKObject *Object, CKBOOL PreserveOnFailure);
    void ResetTextureObjectState(RCKTexture *Texture,
                                 CKRasterizerContext *Context);
    void RestoreTextureObjectState(RCKTexture *Texture,
                                   const TextureEntry &Entry);
    void ResetTextureObjectState(RCKSprite *Sprite,
                                 CKRasterizerContext *Context);
    void RestoreTextureObjectState(RCKSprite *Sprite,
                                   const TextureEntry &Entry);

    void ForgetTextureObjects(CKRasterizerContext *Context);

    TextureEntryTable m_TextureEntries;

    typedef RCKVertexBufferObjectState VertexBufferEntry;
    typedef XClassArray<VertexBufferEntry> VertexBufferEntryArray;
    typedef XSHashTable<VertexBufferEntryArray, RCKVertexBuffer *>
        VertexBufferEntryTable;

    VertexBufferEntry *FindVertexBufferEntry(
        RCKVertexBuffer *Buffer, CKRasterizerContext *Context);
    VertexBufferEntry *AddVertexBufferEntry(
        RCKVertexBuffer *Buffer, CKRasterizerContext *Context);
    void ForgetVertexBufferObjects(CKRasterizerContext *Context);

    VertexBufferEntryTable m_VertexBufferEntries;

    typedef RCKMeshBufferState MeshBufferEntry;
    typedef XClassArray<MeshBufferEntry> MeshBufferEntryArray;
    typedef XSHashTable<MeshBufferEntryArray, CK_ID> MeshBufferEntryTable;

    MeshBufferEntry *FindMeshBufferEntry(
        RCKMesh *Mesh, CKRasterizerContext *Context);
    MeshBufferEntry *AddMeshBufferEntry(
        RCKMesh *Mesh, CKRasterizerContext *Context);
    void ResetMeshBufferState(
        RCKMesh *Mesh, CKRasterizerContext *Context);
    void RestoreMeshBufferState(
        RCKMesh *Mesh, const MeshBufferEntry &Entry);
    void ForgetMeshBuffers(CKRasterizerContext *Context);

    MeshBufferEntryTable m_MeshBufferEntries;
};

#endif // RCKRENDERMANAGER_H

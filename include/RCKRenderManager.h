#ifndef RCKRENDERMANAGER_H
#define RCKRENDERMANAGER_H

#include "XObjectArray.h"
#include "CKRenderEngineTypes.h"
#include "CKRenderEngineEnums.h"
#include "CKRenderManager.h"
#include "CKSceneGraph.h"

class RCKRenderManager : public CKRenderManager {
public:
    explicit RCKRenderManager(CKContext *context);

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

    CKDWORD CreateObjectIndex(CKRST_OBJECTTYPE type);
    CKBOOL ReleaseObjectIndex(CKDWORD index, CKRST_OBJECTTYPE type);

    void DetachAllObjects();

    void DeleteAllVertexBuffers();

    void ClearTemporaryCallbacks();
    void RemoveAllTemporaryCallbacks();

public:
    XClassArray<VxCallBack> m_TemporaryPreRenderCallbacks;
    XClassArray<VxCallBack> m_TemporaryPostRenderCallbacks;
    XSObjectArray m_RenderContexts;
    XArray<CKRasterizer *> m_Rasterizers;
    VxDriverDescEx *m_Drivers;
    int m_DriverCount;
    CKMaterial *m_DefaultMat;
    CKDWORD m_RenderContextMaskFree;
    CKSceneGraphRootNode m_CKSceneGraphRootNode;
    CKDWORD field_B0;
    CKDWORD field_B4;
    XObjectPointerArray m_MovedEntities;
    XObjectPointerArray m_Objects;
    CKDWORD field_D0;
    CKDWORD field_D4;
    CKDWORD field_D8;
    CKDWORD field_DC;
    CKDWORD field_E0;
    CKDWORD field_E4;
    CKDWORD field_E8;
    CKDWORD field_EC;
    CKDWORD field_F0;
    CKDWORD field_F4;
    CKDWORD field_F8;
    CKDWORD field_FC;
    CKDWORD field_100;
    CKDWORD field_104;
    CKDWORD field_108;
    CKDWORD field_10C;
    CKDWORD field_110;
    CKDWORD field_114;
    CKDWORD field_118;
    CKDWORD field_11C;
    CKDWORD field_120;
    CKDWORD field_124;
    CKDWORD field_128;
    CKDWORD field_12C;
    CKDWORD field_130;
    CKDWORD field_134;
    CKDWORD field_138;
    XArray<CKVertexBuffer *> m_VertexBuffers;
    VxOption m_ForceLinearFog;
    VxOption m_ForceSoftware;
    VxOption m_EnsureVertexShader;
    VxOption m_DisableFilter;
    VxOption m_DisableDithering;
    VxOption m_Antialias;
    VxOption m_DisableMipmap;
    VxOption m_DisableSpecular;
    VxOption m_UseIndexBuffers;
    VxOption m_EnableScreenDump;
    VxOption m_EnableDebugMode;
    VxOption m_VertexCache;
    VxOption m_SortTransparentObjects;
    VxOption m_TextureCacheManagement;
    VxOption m_DisablePerspectiveCorrection;
    VxOption m_TextureVideoFormat;
    VxOption m_SpriteVideoFormat;
    XArray<VxOption*> m_Options;
    CK2dEntity *m_2DRootFore;
    CK2dEntity *m_2DRootBack;
    CKDWORD m_2DRootBackName;
    CKDWORD m_2DRootForeName;
    XClassArray<VxEffectDescription> m_Effects;
};

#endif // RCKRENDERMANAGER_H

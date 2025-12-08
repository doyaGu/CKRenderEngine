#ifndef RCK3DENTITY_H
#define RCK3DENTITY_H

#include "CKRenderEngineTypes.h"

#include "RCKRenderObject.h"
#include "CK3dEntity.h"
#include "CKSceneGraph.h"
#include "VxQuaternion.h"
#include "VxRect.h"
#include "VxMatrix.h"

class RCKSkin;

#define CKRENDER_UPDATEEXTENTS 0x00000FF
#define CKRENDER_DONTSETMATRIX 0x0000100

class RCK3dEntity : public RCKRenderObject {
public:

#undef CK_PURE
#define CK_3DIMPLEMENTATION
#include "CK3dEntity.h"
#undef CK_3DIMPLEMENTATION

    //--------------------------------------------------------
    ////               Private Part

    //----------------------------------------------------------
    // Internal functions
    virtual CKBOOL IsHiddenByParent();
    virtual CKBOOL IsVisible();

    explicit RCK3dEntity(CKContext *Context, CKSTRING name = nullptr);
    ~RCK3dEntity() override;
    CK_CLASSID GetClassID() override;

    void PreSave(CKFile *file, CKDWORD flags) override;
    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;
    void PostLoad() override;

    void PreDelete() override;
    void CheckPreDeletion() override;

    int GetMemoryOccupation() override;
    int IsObjectUsed(CKObject *o, CK_CLASSID cid) override;

    //--------------------------------------------
    // Dependencies Functions	{Secret}
    CKERROR PrepareDependencies(CKDependenciesContext &context) override;
    CKERROR RemapDependencies(CKDependenciesContext &context) override;
    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    void AddToScene(CKScene *scene, CKBOOL dependencies = TRUE) override;
    void RemoveFromScene(CKScene *scene, CKBOOL dependencies = TRUE) override;

    CKBOOL IsToBeRendered() override;
    void SetZOrder(int Z) override;
    int GetZOrder() override;
    CKBOOL IsToBeRenderedLast() override;
    
    void WorldMatrixChanged(int updateChildren, int keepScale);

    //--------------------------------------------
    // Class Registering	{Secret}
    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CK3dEntity *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

    RCK3dEntity *m_Parent;
    XObjectPointerArray m_Meshes;
    RCKMesh *m_CurrentMesh;
    CKDWORD field_14;
    XSObjectPointerArray m_ObjectAnimations;
    CKDWORD m_LastFrameMatrix;
    RCKSkin *m_Skin;
    VxMatrix m_LocalMatrix;
    VxMatrix m_WorldMatrix;
    CKDWORD m_MoveableFlags;
    VxMatrix m_InverseWorldMatrix;
    XSObjectPointerArray m_Children;
    VxBbox m_LocalBoundingBox;
    VxBbox m_WorldBoundingBox;
    CKDWORD field_124;
    CKDWORD field_128;
    CKDWORD field_12C;
    CKDWORD field_130;
    CKDWORD field_134;
    CKDWORD field_138;
    VxRect m_RenderExtents;
    CKSceneGraphNode *m_SceneGraphNode;
};

#endif // RCK3DENTITY_H

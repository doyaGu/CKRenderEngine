#ifndef RCKMESH_H
#define RCKMESH_H

#include "CKRenderEngineTypes.h"

#include "CKMesh.h"

class RCKMesh : public CKMesh {
public:

    //--------------------------------------------------------
    ////               Private Part

    //----------------------------------------------------------
    // Internal functions
    void Show(CK_OBJECT_SHOWOPTION show = CKSHOW) override;

    explicit RCKMesh(CKContext *Context, CKSTRING name = nullptr);
    ~RCKMesh() override;
    CK_CLASSID GetClassID() override;

    void PreSave(CKFile *file, CKDWORD flags) override;
    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    void CheckPreDeletion() override;

    int GetMemoryOccupation() override;
    int IsObjectUsed(CKObject *o, CK_CLASSID cid) override;

    //--------------------------------------------
    // Dependencies Functions	{Secret}
    CKERROR PrepareDependencies(CKDependenciesContext &context, CKBOOL iCaller = TRUE) override;
    CKERROR RemapDependencies(CKDependenciesContext &context) override;
    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    void AddToScene(CKScene *scene, CKBOOL dependencies = TRUE) override;
    void RemoveFromScene(CKScene *scene, CKBOOL dependencies = TRUE) override;

    //--------------------------------------------
    // Class Registering	{Secret}
    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKMesh *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    CKDWORD m_Flags;
    VxVector m_BaryCenter;
    CKDWORD m_Radius;
    VxBbox m_LocalBox;
    XArray<CKFace> m_Faces;
    XArray<CKWORD> m_FaceVertexIndices;
    XArray<CKWORD> m_LineIndices;
    XArray<VxVertex> m_Vertices;
    CKDWORD m_VertexWeights;
    XArray<VxColors> m_VertexColors;
    CKDWORD m_DrawFlags;
    CKDWORD m_FaceChannelMask;
    XClassArray<CKMaterialChannel> m_MaterialChannels;
    XVoidArray field_D0;
    XArray<CKMaterialGroup *> m_MaterialGroups;
    CKDWORD m_Valid;
    CKDWORD field_EC;
    CKDWORD m_VertexBuffer;
    CKDWORD m_IndexBuffer;
    void *m_ProgressiveMesh;
    CKCallbacksContainer *m_Callbacks;
    CKCallbacksContainer *m_SubMeshCallbacks;
};

#endif // RCKMESH_H

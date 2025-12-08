#ifndef RCKPATCHMESH_H
#define RCKPATCHMESH_H

#include "CKRenderEngineTypes.h"

#include "RCKMesh.h"
#include "CKPatchMesh.h"

struct RCKTVPatch {
    CK_ID Material;
    CKDWORD Flags;
    CKDWORD Type;
    CKDWORD SubType;
    XArray<short> Vertices;
    XArray<float> UVs;
};

class RCKPatchMesh : public RCKMesh {
public:

#undef CK_PURE
#define CK_3DIMPLEMENTATION
#include "CKPatchMesh.h"
#undef CK_3DIMPLEMENTATION

    explicit RCKPatchMesh(CKContext *Context, CKSTRING name = nullptr);
    ~RCKPatchMesh() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;
    void PreSave(CKFile *file, CKDWORD flags) override;
    void LoadVertices(CKStateChunk *chunk) override;

    int GetMemoryOccupation() override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    // Internal helpers
    void ClearPatches();
    void *GetPatchEdgesBegin();
    void ClearPatchEdges();
    void *GetTVPatchVerticesBegin(int index);
    void ClearTVPatchVertices(int index);
    void *GetTVPatchUVsBegin(int index);
    void ClearTVPatchUVs(int index);
    void Rebuild();

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKPatchMesh *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    int m_VecCount;
    int m_VertCount;
    VxVector *m_Verts;
    VxVector *m_Vecs;
    int m_IterationCount;
    
    XArray<CKPatch> m_Patches;
    XArray<CKPatchEdge> m_PatchEdges;
    XArray<RCKTVPatch> m_TVPatches;
    CKDWORD m_PatchFlags;
};

#endif // RCKPATCHMESH_H

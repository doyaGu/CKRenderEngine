#ifndef RCKPATCHMESH_H
#define RCKPATCHMESH_H

#include "CKRenderEngineTypes.h"

#include "RCKMesh.h"

class RCKPatchMesh : public RCKMesh {
public:
    /**************************************************************************
    Summary: Converts a mesh(CKMesh) to a bezier patch(CKPatchMesh)
    Remarks:
        + This method is not implemented
    {Secret}
    *****************************************************************************/
    virtual CKERROR FromMesh(RCKMesh *m);

    /**************************************************************************
    Summary: Converts a bezier patch(CKPatchMesh) to a  mesh(CKMesh).
    Remarks:
        + This method is not implemented
    {Secret}
    *****************************************************************************/
    virtual CKERROR ToMesh(RCKMesh *m, int stepcount);

    /**************************************************************************
    Summary: Sets the number of iteration steps to be used for tesselation.

    Arguments:
        count: Step count for tesselation
    See also: GetIterationCount
    *****************************************************************************/
    virtual void SetIterationCount(int count);

    /**************************************************************************
    Summary: Gets the number of iteration steps used for tesselation.

    Return Value:
        Step count for tesselation.
    See also: SetIterationCount
    *****************************************************************************/
    virtual int GetIterationCount();

    //-------------------------------------
    // Mesh building

    /************************************************
    Summary: Builds the base mesh for rendering.

    Remarks:
    + This method computes the base mesh vertices and face data to be rendered.
    + This method is automatically called when vertices have been moved (notified by CKMesh::ModifierVertexMove for example)
    or the tesselation level has changed before rendering.
    See Also: CleanRenderMesh
    ************************************************/
    virtual void BuildRenderMesh();

    virtual void CleanRenderMesh();

    virtual void Clear();

    virtual void ComputePatchAux(int index);

    virtual void ComputePatchInteriors(int index);

    virtual CKDWORD GetPatchFlags();

    virtual void SetPatchFlags(CKDWORD Flags);

    //-------------------------------------------
    // Control Points

    /************************************************
    Summary: Sets verts and vecs counts of the patch mesh.

    Arguments:
        VertCount: Number of verts (corner control points) to be set.
        VecCount: Number of vects (edge and interior control points) to be set.
    Remarks:
        + See CKPatch for more details on the difference between verts and vecs.
    See Also: CKPatch,GetVertCount,SetVert,GetVert,SetVec,GetVec
    ************************************************/
    virtual void SetVertVecCount(int VertCount, int VecCount);

    /************************************************
    Summary: Gets verts (corner control points) count of the mesh.

    Return Value:
        Number of verts in the patch mesh.
    See Also: CKPatch,GetVecCount,SetVert,GetVert,SetVec,GetVec
    ************************************************/
    virtual int GetVertCount();

    /************************************************
    Summary: Sets a corner control point position.

    Arguments:
        index: Index of the vert whose position is to be set.
        cp:    Position.
    See Also: GetVert, GetVerts,GetVertCount
    ************************************************/
    virtual void SetVert(int index, VxVector *cp);

    /************************************************
    Summary: Gets a corner control point position.

    Arguments:
        index: Index of the vert whose position is to be get.
        cp:    Position to be filled.
    See Also: SetVert, GetVerts,GetVertCount
    ************************************************/
    virtual void GetVert(int index, VxVector *cp);

    /************************************************
    Summary: Gets all corner control points

    Return Value:
        A pointer to the list of verts (corner control points).
    See Also: SetVert,GetVert,GetVertCount
    ************************************************/
    virtual VxVector *GetVerts();

    /************************************************
    Summary: Gets vecs (edge and interior control points) count of the mesh.

    Return Value:
        Number of verts in the patch mesh.
    See Also: CKPatch,GetVecCount,SetVert,GetVert,SetVec,GetVec
    ************************************************/
    virtual int GetVecCount();

    /************************************************
    Summary: Sets a edge control point position.

    Arguments:
        index: Index of the vec whose position is to be set.
        cp:    Position.
    See Also: SetVec, GetVecs,GetVecCount
    ************************************************/
    virtual void SetVec(int index, VxVector *cp);

    /************************************************
    Summary: Gets a edge control point position.

    Arguments:
        index: Index of the vec whose position is to be get.
        cp:    Position to be filled.
    See Also: SetVec, GetVecs,GetVecCount
    ************************************************/
    virtual void GetVec(int index, VxVector *cp);

    /************************************************
    Summary: Gets all edge and interiors  control points

    Return Value:
        A pointer to the list of vecs (edge and interior control points).
    See Also: SetVec,GetVec,GetVecCount
    ************************************************/
    virtual VxVector *GetVecs();

    //--------------------------------------------
    // Edges

    /************************************************
    Summary: Sets the number of edges.

    Arguments:
        count: Number of edges to be set.
    See Also: GetEdgeCount,SetEdge,GetEdge,GetEdges
    ************************************************/
    virtual void SetEdgeCount(int count);

    /************************************************
    Summary: Returns the number of edges.

    Return Value:
        Number of edges.
    See Also: SetEdgeCount,SetEdge,GetEdge,GetEdges
    ************************************************/
    virtual int GetEdgeCount();

    /************************************************
    Summary: Sets a given edge data.

    Arguments:
        index: Index of the edge to be set.
        edge:  A pointer to a CKPatchEdge structure containing the edge data.
    See Also: SetEdgeCount,GetEdgeCount,GetEdge,GetEdges,CKPatchEdge
    ************************************************/
    virtual void SetEdge(int index, CKPatchEdge *edge);

    /************************************************
    Summary: Gets a given edge data.

    Arguments:
        index: Index of the edge to be get.
        edge:  A pointer to a CKPatchEdge structure that will be filled with the edge data.
    See Also: SetEdgeCount,GetEdgeCount,SetEdge,GetEdges,CKPatchEdge
    ************************************************/
    virtual void GetEdge(int index, CKPatchEdge *edge);

    /************************************************
    Summary: Gets a pointer to the list of edges.

    See Also: SetEdgeCount,GetEdgeCount,SetEdge,GetEdge,CKPatchEdge
    ************************************************/
    virtual CKPatchEdge *GetEdges();

    //---------------------------------------------
    // Patches

    /************************************************
    Summary: Sets the number of patches.

    Arguments:
        count: Number of patches to be set for the patch mesh.
    See Also: GetPatchCount,SetPatch,GetPatch,GetPatches
    ************************************************/
    virtual void SetPatchCount(int count);

    /************************************************
    Summary: Gets the number of patches.

    Return Value:
        Number of patches.
    See Also: SetPatchCount,SetPatch,GetPatch,GetPatches
    ************************************************/
    virtual int GetPatchCount();

    /************************************************
    Summary: Sets the description of a given patch.

    Arguments:
        index: Index of the patch to be set.
        p: A pointer to a CKPatch structure containing the description of the patch.
    See Also: CKPatch,SetPatchCount,GetPatch,GetPatches,SetPatchSM,SetPatchMaterial
    ************************************************/
    virtual void SetPatch(int index, CKPatch *p);

    /************************************************
    Summary: Gets the description of a given patch.

    Arguments:
        index: Index of the patch to be set.
        p: A pointer to a CKPatch structure to be filled with the description of the patch.
    See Also: CKPatch,SetPatchCount,SetPatch,GetPatches,SetPatchSM,SetPatchMaterial
    ************************************************/
    virtual void GetPatch(int index, CKPatch *p);

    /************************************************
    Summary: Gets the smoothing group of a patch.

    Arguments:
        Index: Index of patch whose smoothing group is to be obtained.
    Return Value:
        Smoothing group of the patch.
    Remarks:
        + The smoothing group is a DWORD mask, if the binary AND of the smoothing group value of
        two adjacent patches is not 0 then they are smoothed together otherwise there is a sharp edge
        between the two patches. The default value of a patch smoothing group is 0xFFFFFFFF which means
        all patches are smoothed together.
    See Also: SetPatchSM,SetPatch,CKPatch
    ************************************************/
    virtual CKDWORD GetPatchSM(int index);

    /************************************************
    Summary: Sets the smoothing group for the patch.

    Arguments:
        index: Index of patch whose smoothing group is to be set.
        smoothing: Smoothing group of the patch to be set.
    Remarks:
        + The smoothing group is a DWORD mask, if the binary AND of the smoothing group value of
        two adjacent patches is not 0 then they are smoothed together otherwise there is a sharp edge
        between the two patches. The default value of a patch smoothing group is 0xFFFFFFFF which means
        all patches are smoothed together.
    See Also: GetPatchSM,SetPatch,CKPatch
    ************************************************/
    virtual void SetPatchSM(int index, CKDWORD smoothing);

    /************************************************
    Summary: Gets the material used on a given patch.

    Arguments:
        Index: Index of patch whose material is to be obtained.
    Return Value:
        Pointer to the material.
    See Also: SetPatchMaterial,CKPatch
    ************************************************/
    virtual CKMaterial *GetPatchMaterial(int index);

    /************************************************
    Summary: Sets the material used for a given patch.

    Arguments:
        index: Index of patch whose material is to be obtained.
        mat: A Pointer to the material to use for this patch.
    See Also: GetPatchMaterial,CKPatch
    ************************************************/
    virtual void SetPatchMaterial(int index, CKMaterial *mat);

    /************************************************
    Summary: Gets a pointer to the list of patches.

    See Also: CKPatch,SetPatchCount,SetPatch,GetPatch
    ************************************************/
    virtual CKPatch *GetPatches();

    //-------------------------------------------
    // Texture Patches

    /************************************************
    Summary: Sets the number of texture patches

    Arguments:
        count: Number of texture patches to be set.
        Channel: Index of the channel (-1 for default texture coordinates or >=0 for an additional material channel).
    Remarks:
        + The number of texture patches can be 0 (no texturing ) otherwise it must be equal
        to the number of patches.
    See Also: SetPatchCount,GetTVPatchCount
    ************************************************/
    virtual void SetTVPatchCount(int count, int Channel = -1);

    /************************************************
    Summary: Gets the number of texture patches

    Arguments:
        Channel: Index of the channel (-1 for default texture coordinates or >=0 for an additional material channel).
    Remarks:
        + The number of texture patches can be 0 (no texturing ) otherwise it must be equal
        to the number of patches.
    See Also: SetPatchCount,SetTVPatchCount
    ************************************************/
    virtual int GetTVPatchCount(int Channel = -1);

    /************************************************
    Summary: Sets the mapping of a given patch.

    Arguments:
        index: Index of the patch which mapping should be set.
        tvpatch: A pointer to a CKTVPatch structure that contains the indices of the texture coordinates of the corner points of the patch.
        Channel: Index of the channel (-1 for default texture coordinates or >=0 for an additional material channel).
    See Also: GetTVPatch, GetTVPatches,GetTVPatchCount,SetTVPatchCount,SetTVCount
    ************************************************/
    virtual void SetTVPatch(int index, CKTVPatch *tvpatch, int Channel = -1);

    /************************************************
    Summary: Gets the mapping of a given patch.

    Arguments:
        index: Index of the patch which mapping should be set.
        tvpatch: A pointer to a CKTVPatch structure that will be filled with the indices of the texture coordinates of the corner points of the patch.
        Channel: Index of the channel (-1 for default texture coordinates or >=0 for an additional material channel).
    See Also: SetTVPatch, GetTVPatches,GetTVPatchCount,SetTVPatchCount,SetTVCount
    ************************************************/
    virtual void GetTVPatch(int index, CKTVPatch *tvpatch, int Channel = -1);

    /************************************************
    Summary: Gets a pointer to the list of texture patches.

    Arguments:
        Channel: Index of the channel (-1 for default texture coordinates or >=0 for an additional material channel).
    See Also: SetTVPatch, GetTVPatch,GetTVPatchCount,SetTVPatchCount,SetTVCount
    ************************************************/
    virtual CKTVPatch *GetTVPatches(int Channel = -1);

    //-----------------------------------------
    // Texture Verts

    /************************************************
    Summary: Sets the number of the texture coordinates for a given channel.

    Arguments:
        count: Number of texture vertices to be set.
        Channel: Index of the channel (-1 for default texture coordinates or >=0 for an additional material channel).
    See Also: GetTVCount,GetTVPatchCount,SetTV,GetTV,GetTVs
    ************************************************/
    virtual void SetTVCount(int count, int Channel = -1);

    /************************************************
    Summary: Returns the number of the texture coordinates for a given channel.

    Arguments:
        Channel: Index of the channel (-1 for default texture coordinates or >=0 for an additional material channel).
    Return Value:
        Number of texture coordinates.
    See Also: SetTVCount,SetTV,GetTV,GetTVs,GetTVPatchCount
    ************************************************/
    virtual int GetTVCount(int Channel = -1);

    /************************************************
    Summary: Sets the texture coordinate values.

    Arguments:
        index: Index of the texture vertex whose value has to be set.
        u: U texture coordinate value.
        v: V texture coordinate value.
        Channel: Index of the channel (-1 for default texture coordinates or >=0 for an additional material channel).
    See Also: GetTV, GetTVs,SetTVCount,SetTVPatch
    ************************************************/
    virtual void SetTV(int index, float u, float v, int Channel = -1);

    /************************************************
    Summary: Gets the texture coordinate values.

    Arguments:
        index: Index of the texture vertex whose value has to be retrieve.
        u: U texture coordinate value.
        v: V texture coordinate value.
        Channel: Index of the channel (-1 for default texture coordinates or >=0 for an additional material channel).
    See Also: GetTV, GetTVs,SetTVCount,SetTVPatch
    ************************************************/
    virtual void GetTV(int index, float *u, float *v, int Channel = -1);

    /************************************************
    Summary: Gets a pointer to the list of texture coordinates.

    Arguments:
        Channel: Index of the channel (-1 for default texture coordinates or >=0 for an additional material channel).
    See Also: GetTV, SetTV,GetTVCount,GetTVPatchCount
    ************************************************/
    virtual VxUV *GetTVs(int Channel = -1);

    explicit RCKPatchMesh(CKContext *Context, CKSTRING name = nullptr);
    ~RCKPatchMesh() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKPatchMesh *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
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
    CKDWORD m_PatchFlags;
    CKDWORD field_13C;
    CKDWORD field_140;
    CKDWORD field_144;
    CKDWORD field_148;
    CKDWORD field_14C;
    CKDWORD field_150;
    CKDWORD field_154;
    CKDWORD field_158;
    CKDWORD field_15C;
    CKDWORD field_160;
    CKDWORD field_164;
    CKDWORD field_168;
    CKDWORD field_16C;
    CKDWORD field_170;
    CKDWORD field_174;
    CKDWORD field_178;
    CKDWORD field_17C;
    CKDWORD field_180;
    CKDWORD field_184;
    CKDWORD field_188;
    CKDWORD field_18C;
    CKDWORD field_190;
    CKDWORD field_194;
    CKDWORD field_198;
    CKDWORD field_19C;
    CKDWORD field_1A0;
    CKDWORD field_1A4;
    CKDWORD field_1A8;
};

#endif // RCKPATCHMESH_H

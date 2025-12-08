#ifndef RCKSKIN_H
#define RCKSKIN_H

#include "CKSkin.h"
#include "XClassArray.h"
#include "VxMatrix.h"
#include "VxVector.h"

class CK3dEntity;
class CKContext;

/**
 * @brief Internal structure for per-bone vertex data used in skinning calculations.
 * 
 * Each entry contains:
 * - A list of weighted vertices affected by a bone
 * - A list of weighted normals affected by a bone
 * - A list of vertex indices for quick lookup
 */
struct RCKSkinBonePoints {
    XClassArray<VxVector4> m_WeightedVertices;  // Vertices with weight in w component
    XClassArray<VxVector4> m_WeightedNormals;   // Normals with weight in w component
    XClassArray<CKWORD> m_VertexIndices;        // Indices of affected vertices
};

/**
 * @brief Implementation of CKSkinBoneData.
 * 
 * Contains information about a single bone in a skin:
 * - Reference to the 3D entity representing the bone
 * - The bone's initial inverse world matrix
 * - The computed transformation matrix for skinning
 */
class RCKSkinBoneData : public CKSkinBoneData {
public:
    RCKSkinBoneData();
    virtual ~RCKSkinBoneData() = default;

    // CKSkinBoneData interface
    void SetBone(CK3dEntity *ent) override;
    CK3dEntity *GetBone() override;
    void SetBoneInitialInverseMatrix(const VxMatrix &M) override;

    // Internal access for RCKSkin
    VxMatrix &GetInitialInverseMatrix() { return m_InitialInverseMatrix; }
    VxMatrix &GetTransformMatrix() { return m_TransformMatrix; }

protected:
    CK3dEntity *m_Bone;                 // The 3D entity representing this bone
    VxMatrix m_InitialInverseMatrix;    // Original inverse world matrix of the bone
    VxMatrix m_TransformMatrix;         // Computed transformation matrix for skinning
};

/**
 * @brief Implementation of CKSkinVertexData.
 * 
 * Contains information about how a single vertex is influenced by bones:
 * - Number of bones affecting this vertex
 * - Array of bone indices
 * - Array of weights for each bone
 * - The vertex's original position
 */
class RCKSkinVertexData : public CKSkinVertexData {
public:
    RCKSkinVertexData();
    ~RCKSkinVertexData();

    // CKSkinVertexData interface
    void SetBoneCount(int BoneCount) override;
    int GetBoneCount() override;
    int GetBone(int n) override;
    void SetBone(int n, int BoneIdx) override;
    float GetWeight(int n) override;
    void SetWeight(int n, float Weight) override;
    VxVector &GetInitialPos() override;
    void SetInitialPos(VxVector &pos) override;

protected:
    int m_BoneCount;        // Number of bones affecting this vertex
    int *m_Bones;           // Array of bone indices
    float *m_Weights;       // Array of weights for each bone
    VxVector m_InitialPos;  // Original position of the vertex
};

/**
 * @brief Implementation of CKSkin.
 * 
 * Manages skeletal skinning for meshes. Contains:
 * - Arrays of bone data and vertex data
 * - Per-bone point lists for efficient skinning calculations
 * - Optional normal skinning data
 * - Matrices for coordinate transformation
 */
class RCKSkin : public CKSkin {
public:
    RCKSkin();
    ~RCKSkin();

    // CKSkin interface
    void SetObjectInitMatrix(const VxMatrix &Mat) override;
    void SetBoneCount(int BoneCount) override;
    void SetVertexCount(int Count) override;
    int GetBoneCount() override;
    int GetVertexCount() override;
    void ConstructBoneTransfoMatrices(CKContext *context) override;
    CKBOOL CalcPoints(int VertexCount, CKBYTE *VertexPtr, CKDWORD VStride) override;
    CKSkinBoneData *GetBoneData(int BoneIdx) override;
    CKSkinVertexData *GetVertexData(int VertexIdx) override;
    void RemapVertices(const XArray<int> &permutation) override;
    void SetNormalCount(int Count) override;
    int GetNormalCount() override;
    void SetNormal(int Index, const VxVector &Norm) override;
    VxVector &GetNormal(int Index) override;
    CKBOOL CalcPoints(int VertexCount, CKBYTE *VertexPtr, CKDWORD VStride, 
                      CKBYTE *NormalPtr, CKDWORD NStride) override;

    // Internal methods
    void BuildBonePointLists();
    void ClearBonePointLists();

protected:
    VxMatrix m_ObjectInitMatrix;                       // Original world matrix of the owner entity
    VxMatrix m_InverseWorldMatrix;                     // Inverse of object init matrix
    XClassArray<RCKSkinBoneData> m_BoneData;          // Array of bone data
    XClassArray<RCKSkinBonePoints> m_Points;          // Per-bone vertex/normal lists
    XClassArray<RCKSkinVertexData> m_VertexData;      // Array of vertex data
    XClassArray<VxVector> m_Normals;                  // Optional normal array
    CKDWORD m_Flags;                                   // Skin flags (bit 0 = use weighted mode)
};

#endif // RCKSKIN_H

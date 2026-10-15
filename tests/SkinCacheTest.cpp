#include "TextureTestObserver.h"
#include "RCKSkin.h"

namespace {
class CacheSkin : public RCKSkin {
public:
    int CachedBoneCount() const { return m_Points.Size(); }
    int CachedCount(int bone) const { return m_Points[bone].m_WeightedVertices.Size(); }
    const VxVector4 *CachedPositions(int bone) const { return m_Points[bone].m_WeightedVertices.Begin(); }
    CKDWORD Flags() const { return m_Flags; }
};
class MeasuredSkinBone : public RCKSkinBoneData {
public:
    size_t BoneOffset() const { return reinterpret_cast<const char *>(&m_Bone) - reinterpret_cast<const char *>(this); }
    size_t FlagsOffset() const { return reinterpret_cast<const char *>(&m_BoneFlags) - reinterpret_cast<const char *>(this); }
    size_t InitialOffset() const { return reinterpret_cast<const char *>(&m_InitialInverseMatrix) - reinterpret_cast<const char *>(this); }
    size_t TransformOffset() const { return reinterpret_cast<const char *>(&m_TransformMatrix) - reinterpret_cast<const char *>(this); }
};
class SkinCacheTest : public TextureTestWorld {
protected:
    static void Vec(const VxVector &actual, float x, float y, float z) {
        EXPECT_NEAR(x, actual.x, .0001f); EXPECT_NEAR(y, actual.y, .0001f); EXPECT_NEAR(z, actual.z, .0001f);
    }
    static void Influence(CKSkin &skin, int vertex, int bone, float weight, const VxVector &point) {
        CKSkinVertexData *data = skin.GetVertexData(vertex);
        VxVector position = point; data->SetInitialPos(position);
        data->SetBoneCount(1); data->SetBone(0, bone); data->SetWeight(0, weight);
    }
    static void Translation(RCK3dEntity &bone, float x, float y, float z) {
        VxMatrix matrix; Vx3DMatrixIdentity(matrix);
        matrix[3][0] = x; matrix[3][1] = y; matrix[3][2] = z; bone.SetWorldMatrix(matrix);
    }
    static CKBOOL Points(CKSkin &skin, int count, VxVector *out, VxVector *normals = nullptr) {
        return skin.CalcPointsEx(count, reinterpret_cast<CKBYTE *>(out), sizeof(VxVector),
            reinterpret_cast<CKBYTE *>(normals), sizeof(VxVector));
    }
};

TEST_F(SkinCacheTest, PartialSingleBoneAndUnboundVertexRetainInitialPosition) {
    CacheSkin skin; RCK3dEntity bone(Context); Translation(bone, 8, 4, 0);
    skin.SetBoneCount(1); skin.GetBoneData(0)->SetBone(reinterpret_cast<CK3dEntity *>(&bone)); skin.SetVertexCount(2);
    Influence(skin, 0, 0, .25f, VxVector(2, 3, 4));
    VxVector initial(9, 8, 7); skin.GetVertexData(1)->SetInitialPos(initial);
    VxVector out[2]; ASSERT_TRUE(Points(skin, 2, out));
    Vec(out[0], 4, 4, 4); Vec(out[1], 9, 8, 7); EXPECT_NE(0u, skin.Flags() & 1);
}

TEST_F(SkinCacheTest, PublicInfluenceAndPositionEditsAreVisibleAfterFirstEvaluation) {
    CacheSkin skin; RCK3dEntity first(Context), second(Context);
    Translation(first, 8, 0, 0); Translation(second, 0, 8, 0);
    skin.SetBoneCount(2); skin.GetBoneData(0)->SetBone(reinterpret_cast<CK3dEntity *>(&first)); skin.GetBoneData(1)->SetBone(reinterpret_cast<CK3dEntity *>(&second));
    skin.SetVertexCount(1); Influence(skin, 0, 0, 1, VxVector(2, 3, 4));
    VxVector out; ASSERT_TRUE(Points(skin, 1, &out)); Vec(out, 10, 3, 4);
    CKSkinVertexData *vertex = skin.GetVertexData(0); vertex->SetWeight(0, .25f);
    ASSERT_TRUE(Points(skin, 1, &out)); Vec(out, 4, 3, 4);
    vertex->SetBone(0, 1); ASSERT_TRUE(Points(skin, 1, &out)); Vec(out, 2, 5, 4);
    VxVector position(6, 7, 8); vertex->SetInitialPos(position);
    ASSERT_TRUE(Points(skin, 1, &out)); Vec(out, 6, 9, 8);
    vertex->GetInitialPos().z = 11; ASSERT_TRUE(Points(skin, 1, &out)); Vec(out, 6, 9, 11);
    VxBbox box; ASSERT_TRUE(skin.CalcLocalBBox(Context, &box));
    Vec(box.Min, 6, 9, 11); Vec(box.Max, 6, 9, 11);
}

TEST_F(SkinCacheTest, MutableNormalReferenceRefreshesRotatedNormalAndIgnoresTranslation) {
    CacheSkin skin; RCK3dEntity bone(Context);
    VxMatrix matrix; Vx3DMatrixIdentity(matrix);
    matrix[0][0] = 0; matrix[0][1] = 1; matrix[1][0] = -1; matrix[1][1] = 0;
    matrix[3][0] = 20; bone.SetWorldMatrix(matrix);
    skin.SetBoneCount(1); skin.GetBoneData(0)->SetBone(reinterpret_cast<CK3dEntity *>(&bone)); skin.SetVertexCount(1);
    Influence(skin, 0, 0, 1, VxVector(2, 0, 0)); skin.SetNormalCount(1);
    skin.SetNormal(0, VxVector(1, 0, 0)); VxVector out, normal;
    ASSERT_TRUE(Points(skin, 1, &out, &normal)); Vec(out, 20, 2, 0); Vec(normal, 0, 1, 0);
    skin.GetNormal(0) = VxVector(0, 1, 0);
    ASSERT_TRUE(Points(skin, 1, &out, &normal)); Vec(normal, -1, 0, 0);
    skin.SetNormal(0, VxVector(0, 0, 1));
    ASSERT_TRUE(Points(skin, 1, &out, &normal)); Vec(normal, 0, 0, 1);
}

TEST_F(SkinCacheTest, NormalDisableAndReenableDoNotRetainOldNormalLists) {
    CacheSkin skin; RCK3dEntity bone(Context); skin.SetBoneCount(1); skin.GetBoneData(0)->SetBone(reinterpret_cast<CK3dEntity *>(&bone));
    skin.SetVertexCount(1); Influence(skin, 0, 0, 1, VxVector(1, 2, 3));
    skin.SetNormalCount(1); EXPECT_EQ(1, skin.GetNormalCount()); skin.SetNormal(0, VxVector(0, 1, 0));
    VxVector out, normal; ASSERT_TRUE(Points(skin, 1, &out, &normal)); Vec(normal, 0, 1, 0);
    skin.SetNormalCount(0); EXPECT_EQ(0, skin.GetNormalCount()); normal = VxVector(9, 8, 7);
    ASSERT_TRUE(Points(skin, 1, &out, &normal)); Vec(normal, 9, 8, 7);
    skin.SetNormalCount(1); skin.SetNormal(0, VxVector(1, 0, 0));
    ASSERT_TRUE(Points(skin, 1, &out, &normal)); Vec(normal, 1, 0, 0);
}

TEST_F(SkinCacheTest, RepeatedBuildReplacesListsWithoutDuplicatingInfluencesOrAllocatingAgain) {
    CacheSkin skin; RCK3dEntity bone(Context); Translation(bone, 4, 0, 0);
    skin.SetBoneCount(1); skin.GetBoneData(0)->SetBone(reinterpret_cast<CK3dEntity *>(&bone)); skin.SetVertexCount(1);
    Influence(skin, 0, 0, 1, VxVector(1, 2, 3)); skin.BuildBonePointLists();
    const VxVector4 *storage = skin.CachedPositions(0); EXPECT_EQ(1, skin.CachedCount(0));
    skin.GetVertexData(0)->GetInitialPos().x = 5; skin.BuildBonePointLists();
    EXPECT_EQ(storage, skin.CachedPositions(0)); EXPECT_EQ(1, skin.CachedCount(0));
    VxVector out; ASSERT_TRUE(Points(skin, 1, &out)); Vec(out, 9, 2, 3);
    skin.ClearBonePointLists(); EXPECT_EQ(0, skin.CachedBoneCount());
    ASSERT_TRUE(Points(skin, 1, &out)); Vec(out, 9, 2, 3);
}

TEST_F(SkinCacheTest, ZeroAndNegativeWeightsAreNotEmittedAsBoneContributions) {
    CacheSkin skin; RCK3dEntity bone(Context); Translation(bone, 8, 0, 0);
    skin.SetBoneCount(1); skin.GetBoneData(0)->SetBone(reinterpret_cast<CK3dEntity *>(&bone)); skin.SetVertexCount(1);
    Influence(skin, 0, 0, .75f, VxVector(2, 3, 4));
    CKSkinVertexData *vertex = skin.GetVertexData(0); vertex->SetBoneCount(3);
    vertex->SetBone(1, 0); vertex->SetWeight(1, 0); vertex->SetBone(2, 0); vertex->SetWeight(2, -.25f);
    VxVector out; ASSERT_TRUE(Points(skin, 1, &out)); Vec(out, 7.5f, 2.25f, 3);
    EXPECT_EQ(1, skin.CachedCount(0));
}

TEST_F(SkinCacheTest, VertexIndexBeyondWordRangeReachesItsOwnOutput) {
    CacheSkin skin; RCK3dEntity bone(Context); Translation(bone, 10, 0, 0);
    skin.SetBoneCount(1); skin.GetBoneData(0)->SetBone(reinterpret_cast<CK3dEntity *>(&bone)); skin.SetVertexCount(65537);
    Influence(skin, 0, 0, 1, VxVector(1, 2, 3));
    Influence(skin, 65536, 0, 1, VxVector(7, 8, 9));
    VxVector *out = new VxVector[65537]; ASSERT_TRUE(Points(skin, 65537, out));
    Vec(out[0], 11, 2, 3); Vec(out[65536], 17, 8, 9); delete[] out;
}

TEST_F(SkinCacheTest, BoneDataBindingsMatricesFlagsAndBorrowedLifetimeReachSkinConsumer) {
    RCK3dEntity first(Context), second(Context); Translation(first, 10, 0, 0); Translation(second, 20, 0, 0);
    {
        CacheSkin skin; skin.SetBoneCount(1); skin.SetVertexCount(1);
        RCKSkinBoneData *bone = static_cast<RCKSkinBoneData *>(skin.GetBoneData(0));
        EXPECT_EQ(nullptr, bone->GetBone()); EXPECT_EQ(0u, bone->GetFlags());
        VxVector point(1, 2, 3), transformed;
        Vx3DMultiplyMatrixVector(&transformed, bone->GetInitialInverseMatrix(), &point); Vec(transformed, 1, 2, 3);
        Vx3DMultiplyMatrixVector(&transformed, bone->GetTransformMatrix(), &point); Vec(transformed, 1, 2, 3);
        bone->SetBone(reinterpret_cast<CK3dEntity *>(&first)); EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&first), bone->GetBone()); bone->SetFlags(0xa5020104u);
        VxMatrix inverse; Vx3DMatrixIdentity(inverse); inverse[3][0] = -4; bone->SetBoneInitialInverseMatrix(inverse);
        Influence(skin, 0, 0, 1, point); VxVector out;
        ASSERT_TRUE(Points(skin, 1, &out)); Vec(out, 7, 2, 3); EXPECT_EQ(0xa5020104u, bone->GetFlags());
        Vx3DMultiplyMatrixVector(&transformed, bone->GetTransformMatrix(), &point); Vec(transformed, 7, 2, 3);
        bone->GetInitialInverseMatrix()[3][0] = -2; bone->SetBone(reinterpret_cast<CK3dEntity *>(&second));
        ASSERT_TRUE(Points(skin, 1, &out)); Vec(out, 19, 2, 3);
        bone->SetBone(nullptr); ASSERT_TRUE(Points(skin, 1, &out)); Vec(out, 1, 2, 3);
    }
    EXPECT_FLOAT_EQ(10, first.GetWorldMatrix()[3][0]); EXPECT_FLOAT_EQ(20, second.GetWorldMatrix()[3][0]);
}

TEST_F(SkinCacheTest, BoneAndPointStorageLayoutMatchesPortableOwnershipContract) {
    MeasuredSkinBone bone;
    const size_t pointer = sizeof(void *);
    EXPECT_EQ(pointer, bone.BoneOffset()); EXPECT_EQ(pointer * 2, bone.FlagsOffset());
    EXPECT_EQ(pointer * 2 + 4, bone.InitialOffset()); EXPECT_EQ(pointer * 2 + 68, bone.TransformOffset());
    EXPECT_EQ(pointer == 4 ? 140u : 152u, sizeof(RCKSkinBoneData));
    EXPECT_EQ(pointer * 9, sizeof(RCKSkinBonePoints)); EXPECT_EQ(alignof(void *), alignof(RCKSkinBonePoints));
    RCKSkinBonePoints points;
    EXPECT_EQ(0u, reinterpret_cast<const char *>(&points.m_WeightedVertices) - reinterpret_cast<const char *>(&points));
    EXPECT_EQ(pointer * 3, reinterpret_cast<const char *>(&points.m_WeightedNormals) - reinterpret_cast<const char *>(&points));
    EXPECT_EQ(pointer * 6, reinterpret_cast<const char *>(&points.m_VertexIndices) - reinterpret_cast<const char *>(&points));
    points.m_WeightedVertices.Resize(1); points.m_WeightedNormals.Resize(1); points.m_VertexIndices.Resize(1);
    points.m_VertexIndices[0] = 65536; EXPECT_EQ(65536, points.m_VertexIndices[0]);
}
}

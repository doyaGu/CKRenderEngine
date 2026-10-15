#include "TextureTestObserver.h"
#include "RCKSkin.h"

namespace {
class StorageSkin : public RCKSkin {
public:
    int CachedBones() const { return m_Points.Size(); }
    CKDWORD Flags() const { return m_Flags; }
    const VxMatrix &Inverse() const { return m_InverseWorldMatrix; }
    size_t Offset(int field) const {
        const void *fields[] = {&m_ObjectInitMatrix, &m_InverseWorldMatrix, &m_BoneData,
                               &m_Points, &m_VertexData, &m_Normals, &m_Flags};
        return static_cast<const char *>(fields[field]) - reinterpret_cast<const char *>(this);
    }
};
class StorageVertex : public RCKSkinVertexData {
public:
    size_t Offset(int field) const {
        const void *fields[] = {&m_BoneCount, &m_Bones, &m_Weights, &m_InitialPos};
        return static_cast<const char *>(fields[field]) - reinterpret_cast<const char *>(this);
    }
};
void VectorIs(const VxVector &actual, const VxVector &expected) {
    EXPECT_NEAR(expected.x, actual.x, .0001f);
    EXPECT_NEAR(expected.y, actual.y, .0001f);
    EXPECT_NEAR(expected.z, actual.z, .0001f);
}
void Populate(CKSkinVertexData &vertex, int tag) {
    VxVector position(static_cast<float>(tag), static_cast<float>(tag * 2), 3.0f);
    vertex.SetInitialPos(position); vertex.SetBoneCount(2);
    vertex.SetBone(0, 0); vertex.SetBone(1, 1);
    vertex.SetWeight(0, .25f); vertex.SetWeight(1, .75f);
}
class SkinStorageTest : public TextureTestWorld {
protected:
    void Bones(StorageSkin &skin, RCK3dEntity &first, RCK3dEntity &second) {
        skin.SetBoneCount(2);
        VxMatrix matrix = VxMatrix::Identity(); matrix[3].x = 4.0f; first.SetWorldMatrix(matrix);
        matrix = VxMatrix::Identity(); matrix[3].y = 8.0f; second.SetWorldMatrix(matrix);
        skin.GetBoneData(0)->SetBone(reinterpret_cast<CK3dEntity *>(&first));
        skin.GetBoneData(1)->SetBone(reinterpret_cast<CK3dEntity *>(&second));
    }
    void Consume(StorageSkin &skin, int count, const int *tags) {
        VxVector positions[12];
        ASSERT_LE(count, 12);
        skin.ConstructBoneTransfoMatrices(Context);
        ASSERT_TRUE(skin.CalcPoints(count, reinterpret_cast<CKBYTE *>(positions), sizeof(VxVector)));
        for (int i = 0; i < count; ++i)
            VectorIs(positions[i], VxVector(static_cast<float>(tags[i]) + 1.0f,
                                           static_cast<float>(tags[i] * 2) + 6.0f, 3.0f));
    }
};

TEST_F(SkinStorageTest, EmptyStorageAndPortableLayoutHaveDefinedDefaults) {
    StorageSkin skin; StorageVertex vertex;
    EXPECT_EQ(0, skin.GetVertexCount()); EXPECT_EQ(0, skin.GetBoneCount());
    EXPECT_EQ(0, skin.GetNormalCount()); EXPECT_EQ(0, skin.CachedBones()); EXPECT_EQ(0u, skin.Flags());
    EXPECT_EQ(0, vertex.GetBoneCount()); EXPECT_EQ(nullptr, vertex.GetBonesArray()); EXPECT_EQ(nullptr, vertex.GetWeightsArray());
    VectorIs(vertex.GetInitialPos(), VxVector(0.0f, 0.0f, 0.0f));
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) {
            EXPECT_FLOAT_EQ(row == column ? 1.0f : 0.0f, skin.GetObjectInitMatrix()[row][column]);
            EXPECT_FLOAT_EQ(row == column ? 1.0f : 0.0f, skin.Inverse()[row][column]);
        }
    const size_t skinOffsets32[] = {4, 68, 132, 144, 156, 168, 180};
    const size_t skinOffsets64[] = {8, 72, 136, 160, 184, 208, 232};
    const size_t vertexOffsets32[] = {4, 8, 12, 16};
    const size_t vertexOffsets64[] = {8, 16, 24, 32};
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(sizeof(void *) == 4 ? skinOffsets32[i] : skinOffsets64[i], skin.Offset(i));
        RecordProperty("skin_offset_" + std::to_string(i), static_cast<int>(skin.Offset(i)));
    }
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(sizeof(void *) == 4 ? vertexOffsets32[i] : vertexOffsets64[i], vertex.Offset(i));
        RecordProperty("vertex_offset_" + std::to_string(i), static_cast<int>(vertex.Offset(i)));
    }
    EXPECT_EQ(sizeof(void *) == 4 ? 184u : 240u, sizeof(RCKSkin));
    EXPECT_EQ(sizeof(void *) == 4 ? 28u : 48u, sizeof(RCKSkinVertexData));
    EXPECT_EQ(sizeof(void *), alignof(RCKSkin)); EXPECT_EQ(sizeof(void *), alignof(RCKSkinVertexData));
    RecordProperty("skin_size", static_cast<int>(sizeof(RCKSkin)));
    RecordProperty("skin_alignment", static_cast<int>(alignof(RCKSkin)));
    RecordProperty("vertex_size", static_cast<int>(sizeof(RCKSkinVertexData)));
    RecordProperty("vertex_alignment", static_cast<int>(alignof(RCKSkinVertexData)));
}

TEST_F(SkinStorageTest, VertexInfluenceResizePreservesPrefixAndRawArraysAliasPublicAccessors) {
    RCKSkinVertexData vertex; Populate(vertex, 5);
    EXPECT_EQ(0, vertex.GetBonesArray()[0]); EXPECT_FLOAT_EQ(.75f, vertex.GetWeightsArray()[1]);
    vertex.GetBonesArray()[1] = 7; vertex.GetWeightsArray()[0] = .625f;
    EXPECT_EQ(7, vertex.GetBone(1)); EXPECT_FLOAT_EQ(.625f, vertex.GetWeight(0));
    vertex.SetBoneCount(4);
    EXPECT_EQ(4, vertex.GetBoneCount()); EXPECT_EQ(7, vertex.GetBone(1)); EXPECT_FLOAT_EQ(.625f, vertex.GetWeight(0));
    vertex.SetBone(2, 8); vertex.SetBone(3, 9); vertex.SetWeight(2, .125f); vertex.SetWeight(3, .25f);
    vertex.SetBoneCount(1);
    EXPECT_EQ(0, vertex.GetBone(0)); EXPECT_FLOAT_EQ(.625f, vertex.GetWeight(0));
    VectorIs(vertex.GetInitialPos(), VxVector(5.0f, 10.0f, 3.0f));
    vertex.GetInitialPos().y = -4.0f; EXPECT_FLOAT_EQ(-4.0f, vertex.GetInitialPos().y);
    vertex.SetBoneCount(0);
    EXPECT_EQ(0, vertex.GetBoneCount()); EXPECT_EQ(nullptr, vertex.GetBonesArray()); EXPECT_EQ(nullptr, vertex.GetWeightsArray());
    vertex.SetBoneCount(1); vertex.SetBone(0, 2); vertex.SetWeight(0, 1.0f);
    EXPECT_EQ(2, vertex.GetBone(0)); EXPECT_FLOAT_EQ(1.0f, vertex.GetWeight(0));
}

// These owned-copy cases are current-only: the old implicit memberwise copy
// leaves freed influence arrays and double deletion at normal scope teardown.
TEST_F(SkinStorageTest, VertexCopiesOwnInfluencesAndSelfAssignmentPreservesValues) {
    RCKSkinVertexData source; Populate(source, 4);
    RCKSkinVertexData copy(source);
    EXPECT_NE(source.GetBonesArray(), copy.GetBonesArray()); EXPECT_NE(source.GetWeightsArray(), copy.GetWeightsArray());
    source.SetBone(0, 9); source.SetWeight(1, .125f); source.GetInitialPos().x = 100.0f;
    EXPECT_EQ(0, copy.GetBone(0)); EXPECT_FLOAT_EQ(.75f, copy.GetWeight(1)); EXPECT_FLOAT_EQ(4.0f, copy.GetInitialPos().x);
    RCKSkinVertexData assigned; Populate(assigned, 10); assigned = copy;
    EXPECT_NE(copy.GetBonesArray(), assigned.GetBonesArray()); EXPECT_NE(copy.GetWeightsArray(), assigned.GetWeightsArray());
    copy.SetBoneCount(0); EXPECT_EQ(2, assigned.GetBoneCount()); EXPECT_FLOAT_EQ(.25f, assigned.GetWeight(0));
    assigned = assigned; EXPECT_EQ(2, assigned.GetBoneCount()); EXPECT_EQ(1, assigned.GetBone(1));
    VectorIs(assigned.GetInitialPos(), VxVector(4.0f, 8.0f, 3.0f));
    RCKSkinVertexData empty; assigned = empty;
    EXPECT_EQ(0, assigned.GetBoneCount()); EXPECT_EQ(nullptr, assigned.GetBonesArray()); EXPECT_EQ(nullptr, assigned.GetWeightsArray());
}

TEST_F(SkinStorageTest, VertexGrowthKeepsOwnedInfluencesAliveForSkinningConsumer) {
    StorageSkin skin; RCK3dEntity first(Context), second(Context); Bones(skin, first, second);
    skin.SetVertexCount(2); Populate(*skin.GetVertexData(0), 2); Populate(*skin.GetVertexData(1), 3);
    const int initial[] = {2, 3}; Consume(skin, 2, initial); EXPECT_EQ(2, skin.CachedBones());
    skin.SetVertexCount(5); EXPECT_EQ(0, skin.CachedBones());
    for (int i = 2; i < 5; ++i) {
        EXPECT_EQ(0, skin.GetVertexData(i)->GetBoneCount()); Populate(*skin.GetVertexData(i), i + 2);
    }
    const int grown[] = {2, 3, 4, 5, 6}; Consume(skin, 5, grown);
    skin.SetVertexCount(8); EXPECT_EQ(0, skin.CachedBones());
    for (int i = 5; i < 8; ++i) Populate(*skin.GetVertexData(i), i + 2);
    const int regrown[] = {2, 3, 4, 5, 6, 7, 8, 9}; Consume(skin, 8, regrown);
    skin.SetVertexCount(0); EXPECT_EQ(0, skin.GetVertexCount()); EXPECT_EQ(0, skin.CachedBones());
    skin.SetVertexCount(1); EXPECT_EQ(0, skin.GetVertexData(0)->GetBoneCount());
}

TEST_F(SkinStorageTest, ShrinkRegrowRetainsReservedVertexValuesAndSameCountInvalidatesLists) {
    StorageSkin skin; RCK3dEntity first(Context), second(Context); Bones(skin, first, second);
    skin.SetVertexCount(3); for (int i = 0; i < 3; ++i) Populate(*skin.GetVertexData(i), i + 2);
    const int all[] = {2, 3, 4}; Consume(skin, 3, all);
    skin.SetVertexCount(1); EXPECT_EQ(1, skin.GetVertexCount()); EXPECT_EQ(0, skin.CachedBones());
    Consume(skin, 1, all); skin.SetVertexCount(3); Consume(skin, 3, all);
    VxVector newPosition(10.0f, 20.0f, 3.0f); skin.GetVertexData(0)->SetInitialPos(newPosition);
    skin.SetVertexCount(3); EXPECT_EQ(0, skin.CachedBones());
    const int changed[] = {10, 3, 4}; Consume(skin, 3, changed);
}

TEST_F(SkinStorageTest, BoneResizePreservesReferencesAndMatricesAndSameCountClearsLists) {
    StorageSkin skin; RCK3dEntity first(Context), second(Context); Bones(skin, first, second);
    skin.SetVertexCount(1); Populate(*skin.GetVertexData(0), 5);
    VxMatrix initial = VxMatrix::Identity(); initial[3].z = 2.0f;
    skin.GetBoneData(0)->SetBoneInitialInverseMatrix(initial);
    skin.BuildBonePointLists(); ASSERT_EQ(2, skin.CachedBones());
    skin.SetBoneCount(2); EXPECT_EQ(0, skin.CachedBones());
    skin.SetBoneCount(4); EXPECT_EQ(4, skin.GetBoneCount());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&first), skin.GetBoneData(0)->GetBone());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&second), skin.GetBoneData(1)->GetBone());
    EXPECT_EQ(nullptr, skin.GetBoneData(2)->GetBone()); EXPECT_EQ(nullptr, skin.GetBoneData(3)->GetBone());
    EXPECT_FLOAT_EQ(2.0f, static_cast<RCKSkinBoneData *>(skin.GetBoneData(0))->GetInitialInverseMatrix()[3].z);
    skin.SetBoneCount(2); VxVector result;
    skin.ConstructBoneTransfoMatrices(Context);
    ASSERT_TRUE(skin.CalcPoints(1, reinterpret_cast<CKBYTE *>(&result), sizeof(VxVector)));
    VectorIs(result, VxVector(6.0f, 16.0f, 3.5f));
    skin.SetBoneCount(0); EXPECT_EQ(0, skin.GetBoneCount()); EXPECT_EQ(0, skin.CachedBones());
    skin.SetBoneCount(2); EXPECT_EQ(nullptr, skin.GetBoneData(0)->GetBone());
    EXPECT_FLOAT_EQ(0.0f, static_cast<RCKSkinBoneData *>(skin.GetBoneData(0))->GetInitialInverseMatrix()[3].z);
}

TEST_F(SkinStorageTest, RemapCompactsInfluencesAndNormalsAndRefreshesSkinningConsumer) {
    StorageSkin skin; RCK3dEntity first(Context), second(Context); Bones(skin, first, second);
    skin.SetVertexCount(4); skin.SetNormalCount(4);
    for (int i = 0; i < 4; ++i) {
        Populate(*skin.GetVertexData(i), i + 2); skin.SetNormal(i, VxVector(static_cast<float>(i + 1), 0.0f, 0.0f));
    }
    const int before[] = {2, 3, 4, 5}; Consume(skin, 4, before);
    XArray<int> mapping; mapping.Resize(4); mapping[0] = 1; mapping[1] = -1; mapping[2] = 0; mapping[3] = -1;
    skin.RemapVertices(mapping); EXPECT_EQ(2, skin.GetVertexCount()); EXPECT_EQ(2, skin.GetNormalCount());
    const int after[] = {4, 2}; Consume(skin, 2, after);
    VectorIs(skin.GetNormal(0), VxVector(3.0f, 0.0f, 0.0f)); VectorIs(skin.GetNormal(1), VxVector(1.0f, 0.0f, 0.0f));
    VxVector positions[2], normals[2];
    ASSERT_TRUE(skin.CalcPointsEx(2, reinterpret_cast<CKBYTE *>(positions), sizeof(VxVector),
                                 reinterpret_cast<CKBYTE *>(normals), sizeof(VxVector)));
    VectorIs(normals[0], VxVector(3.0f, 0.0f, 0.0f)); VectorIs(normals[1], VxVector(1.0f, 0.0f, 0.0f));
    mapping.Resize(2); mapping[0] = -1; mapping[1] = -1; skin.RemapVertices(mapping);
    EXPECT_EQ(0, skin.GetVertexCount()); EXPECT_EQ(0, skin.GetNormalCount());
}

TEST_F(SkinStorageTest, RemapMismatchedSizeLeavesAuthoredDataAndPointListsUntouched) {
    StorageSkin skin; RCK3dEntity first(Context), second(Context); Bones(skin, first, second);
    skin.SetVertexCount(2); Populate(*skin.GetVertexData(0), 3); Populate(*skin.GetVertexData(1), 4);
    const int tags[] = {3, 4}; Consume(skin, 2, tags);
    XArray<int> mapping; mapping.Resize(1); mapping[0] = 0; skin.RemapVertices(mapping);
    EXPECT_EQ(2, skin.GetVertexCount()); EXPECT_EQ(2, skin.CachedBones()); Consume(skin, 2, tags);
}
} // namespace

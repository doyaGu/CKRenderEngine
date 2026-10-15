#include "TextureTestObserver.h"
#include "RCKSkin.h"

namespace {
class CalculationSkin : public RCKSkin {
public:
    const VxMatrix &InverseObject() const { return m_InverseWorldMatrix; }
    const VxMatrix &Transform(int bone) { return m_BoneData[bone].GetTransformMatrix(); }
};

class SkinCalculationTest : public TextureTestWorld {
protected:
    RCK3dEntity *Bones[3] = {};
    void SetUp() override {
        TextureTestWorld::SetUp();
        for (int i = 0; i < 3; ++i) {
            Bones[i] = reinterpret_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
            ASSERT_NE(nullptr, Bones[i]);
        }
    }
    static void Vec(const VxVector &actual, const VxVector &expected) {
        EXPECT_NEAR(expected.x, actual.x, .0001f);
        EXPECT_NEAR(expected.y, actual.y, .0001f);
        EXPECT_NEAR(expected.z, actual.z, .0001f);
    }
    static VxMatrix Translation(float x, float y, float z) {
        VxMatrix result = VxMatrix::Identity(); result[3].Set(x, y, z, 1.0f); return result;
    }
    static VxMatrix RotationZ(float x, float y, float z) {
        VxMatrix result = Translation(x, y, z);
        result[0].Set(0, 1, 0, 0); result[1].Set(-1, 0, 0, 0); return result;
    }
    static VxMatrix RotationX(float x, float y, float z) {
        VxMatrix result = Translation(x, y, z);
        result[1].Set(0, 0, 1, 0); result[2].Set(0, -1, 0, 0); return result;
    }
    static VxMatrix RotationY(float x, float y, float z) {
        VxMatrix result = Translation(x, y, z);
        result[0].Set(0, 0, -1, 0); result[2].Set(1, 0, 0, 0); return result;
    }
    static void Vertex(CKSkin &skin, int index, VxVector point, int count, int first = 0, float weight = 1.0f) {
        CKSkinVertexData *vertex = skin.GetVertexData(index);
        vertex->SetInitialPos(point); vertex->SetBoneCount(count);
        if (count) { vertex->SetBone(0, first); vertex->SetWeight(0, weight); }
        if (count > 1) { vertex->SetBone(1, 1); vertex->SetWeight(1, 1.0f - weight); }
    }
    void Attach(CKSkin &skin, int count = 1) {
        skin.SetBoneCount(count);
        for (int i = 0; i < count; ++i)
            skin.GetBoneData(i)->SetBone(reinterpret_cast<CK3dEntity *>(Bones[i]));
    }
};

TEST_F(SkinCalculationTest, ObjectInitialMatrixStoresValueAndInverse) {
    CalculationSkin skin;
    const VxMatrix original = RotationZ(10, 20, 30);
    VxMatrix supplied = original; skin.SetObjectInitMatrix(supplied); supplied[3].x = 99;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            EXPECT_FLOAT_EQ(original[row][col], skin.GetObjectInitMatrix()[row][col]);
    VxVector world(8, 21, 33), local;
    Vx3DMultiplyMatrixVector(&local, skin.InverseObject(), &world);
    Vec(local, VxVector(1, 2, 3));
}

TEST_F(SkinCalculationTest, NoncommutingBindAndBoneMatricesHaveCorrectOrder) {
    CalculationSkin skin; Attach(skin); skin.SetVertexCount(1); skin.SetNormalCount(1);
    skin.SetObjectInitMatrix(RotationZ(10, 20, 30));
    skin.GetBoneData(0)->SetBoneInitialInverseMatrix(RotationX(-2, 3, -4));
    Bones[0]->SetWorldMatrix(RotationY(4, -5, 6));
    Vertex(skin, 0, VxVector(1, 2, 3), 1); skin.SetNormal(0, VxVector(1, 2, 3));
    skin.ConstructBoneTransfoMatrices(Context);
    VxVector fromConstruct, point, normal, source(1, 2, 3);
    Vx3DMultiplyMatrixVector(&fromConstruct, skin.Transform(0), &source);
    Vec(fromConstruct, VxVector(-55, -11, -30));
    ASSERT_TRUE(skin.CalcPointsEx(1, reinterpret_cast<CKBYTE *>(&point), sizeof(point), reinterpret_cast<CKBYTE *>(&normal), sizeof(normal)));
    Vec(point, VxVector(-55, -11, -30)); Vec(normal, VxVector(-3, -1, 2));
}

TEST_F(SkinCalculationTest, TwoBonesAccumulatePositionsAndUnnormalizedNormalsAtIndependentStrides) {
    CalculationSkin skin; Attach(skin, 2); skin.SetVertexCount(2); skin.SetNormalCount(2);
    Bones[0]->SetWorldMatrix(RotationZ(10, 0, 0)); Bones[1]->SetWorldMatrix(Translation(0, 20, 0));
    Vertex(skin, 0, VxVector(2, 4, 6), 2, 0, .25f);
    Vertex(skin, 1, VxVector(1, 0, 2), 2, 0, .75f);
    skin.SetNormal(0, VxVector(1, 0, 0)); skin.SetNormal(1, VxVector(0, 2, 0));
    struct Position { VxVector Value; CKDWORD Padding[2]; } positions[2];
    struct Normal { VxVector Value; CKDWORD Padding[4]; } normals[2];
    memset(positions, 0x5a, sizeof(positions)); memset(normals, 0xa5, sizeof(normals));
    ASSERT_TRUE(skin.CalcPointsEx(2, reinterpret_cast<CKBYTE *>(positions), sizeof(Position), reinterpret_cast<CKBYTE *>(normals), sizeof(Normal)));
    Vec(positions[0].Value, VxVector(3, 18.5f, 6)); Vec(normals[0].Value, VxVector(.75f, .25f, 0));
    Vec(positions[1].Value, VxVector(7.75f, 5.75f, 2)); Vec(normals[1].Value, VxVector(-1.5f, .5f, 0));
    for (int i = 0; i < 2; ++i) {
        for (int p = 0; p < 2; ++p) EXPECT_EQ(0x5a5a5a5au, positions[i].Padding[p]);
        for (int p = 0; p < 4; ++p) EXPECT_EQ(0xa5a5a5a5u, normals[i].Padding[p]);
    }
}

TEST_F(SkinCalculationTest, EvaluationRefreshesMovedBoneWithoutSeparateConstructCall) {
    CalculationSkin skin; Attach(skin); skin.SetVertexCount(1); Vertex(skin, 0, VxVector(1, 2, 3), 1);
    VxVector point;
    Bones[0]->SetWorldMatrix(Translation(4, 0, 0)); skin.ConstructBoneTransfoMatrices(Context);
    ASSERT_TRUE(skin.CalcPoints(1, reinterpret_cast<CKBYTE *>(&point), sizeof(point))); Vec(point, VxVector(5, 2, 3));
    Bones[0]->SetWorldMatrix(Translation(0, 7, 0));
    ASSERT_TRUE(skin.CalcPoints(1, reinterpret_cast<CKBYTE *>(&point), sizeof(point))); Vec(point, VxVector(1, 9, 3));
}

TEST_F(SkinCalculationTest, UninfluencedVertexSurvivesBesideAnAnimatedVertex) {
    CalculationSkin skin; Attach(skin); skin.SetVertexCount(2);
    Bones[0]->SetWorldMatrix(Translation(10, 0, 0));
    Vertex(skin, 0, VxVector(2, 3, 4), 0); Vertex(skin, 1, VxVector(5, 6, 7), 1);
    VxVector points[2]; ASSERT_TRUE(skin.CalcPoints(2, reinterpret_cast<CKBYTE *>(points), sizeof(VxVector)));
    Vec(points[0], VxVector(2, 3, 4)); Vec(points[1], VxVector(15, 6, 7));
}

TEST_F(SkinCalculationTest, SinglePartialWeightRetainsRestPoseButMultipleWeightsDoNotInventRemainder) {
    CalculationSkin skin; Attach(skin, 2); skin.SetVertexCount(3);
    Bones[0]->SetWorldMatrix(Translation(10, 0, 0)); Bones[1]->SetWorldMatrix(Translation(0, 20, 0));
    Vertex(skin, 0, VxVector(4, 8, 12), 1, 0, .25f);
    Vertex(skin, 1, VxVector(4, 8, 12), 2, 0, .25f); skin.GetVertexData(1)->SetWeight(1, .25f);
    Vertex(skin, 2, VxVector(4, 8, 12), 1, 0, 1.5f);
    VxVector points[3]; ASSERT_TRUE(skin.CalcPoints(3, reinterpret_cast<CKBYTE *>(points), sizeof(VxVector)));
    Vec(points[0], VxVector(6.5f, 8, 12)); Vec(points[1], VxVector(4.5f, 9, 6));
    Vec(points[2], VxVector(19, 8, 12));
}

TEST_F(SkinCalculationTest, MissingBonesDoNotReuseStaleTransformAndAllMissingRestoresInitialPoints) {
    CalculationSkin skin; Attach(skin, 2); skin.SetVertexCount(1);
    Bones[0]->SetWorldMatrix(Translation(10, 0, 0)); Bones[1]->SetWorldMatrix(Translation(0, 20, 0));
    Vertex(skin, 0, VxVector(4, 8, 12), 2, 0, .25f);
    VxVector point; ASSERT_TRUE(skin.CalcPoints(1, reinterpret_cast<CKBYTE *>(&point), sizeof(point)));
    Vec(point, VxVector(6.5f, 23, 12));
    skin.GetBoneData(0)->SetBone(nullptr); skin.ConstructBoneTransfoMatrices(Context);
    ASSERT_TRUE(skin.CalcPoints(1, reinterpret_cast<CKBYTE *>(&point), sizeof(point))); Vec(point, VxVector(3, 21, 9));
    skin.GetBoneData(1)->SetBone(nullptr);
    ASSERT_TRUE(skin.CalcPoints(1, reinterpret_cast<CKBYTE *>(&point), sizeof(point))); Vec(point, VxVector(4, 8, 12));
}

TEST_F(SkinCalculationTest, PrefixEvaluationLeavesFollowingPositionAndNormalRecordsUntouched) {
    CalculationSkin skin; Attach(skin); skin.SetVertexCount(3); skin.SetNormalCount(3);
    Bones[0]->SetWorldMatrix(Translation(10, 0, 0));
    for (int i = 0; i < 3; ++i) { Vertex(skin, i, VxVector(static_cast<float>(i), 2, 3), 1); skin.SetNormal(i, VxVector(0, 1, 0)); }
    VxVector points[3] = {VxVector(99, 98, 97), VxVector(99, 98, 97), VxVector(99, 98, 97)};
    VxVector normals[3] = {VxVector(9, 8, 7), VxVector(9, 8, 7), VxVector(9, 8, 7)};
    ASSERT_TRUE(skin.CalcPointsEx(2, reinterpret_cast<CKBYTE *>(points), sizeof(VxVector), reinterpret_cast<CKBYTE *>(normals), sizeof(VxVector)));
    Vec(points[1], VxVector(11, 2, 3)); Vec(normals[1], VxVector(0, 1, 0));
    Vec(points[2], VxVector(99, 98, 97)); Vec(normals[2], VxVector(9, 8, 7));
}

TEST_F(SkinCalculationTest, LargerOutputCapacityPreservesRecordsWithoutSkinVertices) {
    CalculationSkin skin; Attach(skin); skin.SetVertexCount(1); skin.SetNormalCount(1);
    Vertex(skin, 0, VxVector(1, 2, 3), 1); skin.SetNormal(0, VxVector(0, 1, 0));
    VxVector points[2] = {VxVector(99, 98, 97), VxVector(99, 98, 97)};
    VxVector normals[2] = {VxVector(9, 8, 7), VxVector(9, 8, 7)};
    ASSERT_TRUE(skin.CalcPointsEx(2, reinterpret_cast<CKBYTE *>(points), sizeof(VxVector), reinterpret_cast<CKBYTE *>(normals), sizeof(VxVector)));
    Vec(points[0], VxVector(1, 2, 3)); Vec(normals[0], VxVector(0, 1, 0));
    Vec(points[1], VxVector(99, 98, 97)); Vec(normals[1], VxVector(9, 8, 7));
}

TEST_F(SkinCalculationTest, AbsentNormalDataLeavesCallerNormalsAlone) {
    CalculationSkin skin; Attach(skin); skin.SetVertexCount(1); Vertex(skin, 0, VxVector(1, 2, 3), 1);
    VxVector point, normal(9, 8, 7);
    ASSERT_TRUE(skin.CalcPointsEx(1, reinterpret_cast<CKBYTE *>(&point), sizeof(point), reinterpret_cast<CKBYTE *>(&normal), sizeof(normal)));
    Vec(normal, VxVector(9, 8, 7));
}

TEST_F(SkinCalculationTest, LocalBoxContainsActualWeightedPositionsAndRestVertices) {
    CalculationSkin skin; Attach(skin, 2); skin.SetVertexCount(3);
    Bones[0]->SetWorldMatrix(Translation(10, 0, 0)); Bones[1]->SetWorldMatrix(Translation(0, 20, 0));
    Vertex(skin, 0, VxVector(4, 8, 12), 1, 0, .25f);
    Vertex(skin, 1, VxVector(4, 8, 12), 2, 0, .25f); skin.GetVertexData(1)->SetWeight(1, .25f);
    Vertex(skin, 2, VxVector(-4, -8, -12), 0);
    VxBbox box; ASSERT_TRUE(skin.CalcLocalBBox(Context, &box));
    Vec(box.Min, VxVector(-4, -8, -12)); Vec(box.Max, VxVector(6.5f, 9, 12));
    VxVector points[3]; ASSERT_TRUE(skin.CalcPoints(3, reinterpret_cast<CKBYTE *>(points), sizeof(VxVector)));
    for (int i = 0; i < 3; ++i) {
        EXPECT_LE(box.Min.x, points[i].x); EXPECT_LE(box.Min.y, points[i].y); EXPECT_LE(box.Min.z, points[i].z);
        EXPECT_GE(box.Max.x, points[i].x); EXPECT_GE(box.Max.y, points[i].y); EXPECT_GE(box.Max.z, points[i].z);
    }
}

TEST_F(SkinCalculationTest, BoneBoxUsesOwnerInverseAndSkipsNullBoneReferences) {
    CalculationSkin skin; Attach(skin, 2); skin.SetBoneCount(3);
    Bones[0]->SetWorldMatrix(Translation(8, 21, 33)); Bones[1]->SetWorldMatrix(Translation(15, 16, 28));
    Bones[2]->SetWorldMatrix(RotationZ(10, 20, 30));
    VxBbox box; ASSERT_TRUE(skin.CalcBonesBBox(Context, nullptr, &box));
    Vec(box.Min, VxVector(8, 16, 28)); Vec(box.Max, VxVector(15, 21, 33));
    ASSERT_TRUE(skin.CalcBonesBBox(Context, reinterpret_cast<CK3dEntity *>(Bones[2]), &box));
    Vec(box.Min, VxVector(-4, -5, -2)); Vec(box.Max, VxVector(1, 2, 3));
    skin.SetBoneCount(0); ASSERT_TRUE(skin.CalcBonesBBox(Context, nullptr, &box));
    EXPECT_GT(box.Min.x, box.Max.x); EXPECT_GT(box.Min.y, box.Max.y); EXPECT_GT(box.Min.z, box.Max.z);
    EXPECT_FALSE(skin.CalcBonesBBox(Context, nullptr, nullptr));
}

TEST_F(SkinCalculationTest, EmptyAndZeroCountEvaluationsLeaveOutputUntouched) {
    CalculationSkin skin; VxVector point(9, 8, 7); VxBbox box;
    EXPECT_FALSE(skin.CalcPoints(1, reinterpret_cast<CKBYTE *>(&point), sizeof(point)));
    EXPECT_FALSE(skin.CalcLocalBBox(Context, &box));
    skin.SetVertexCount(1); Vertex(skin, 0, VxVector(1, 2, 3), 0);
    EXPECT_FALSE(skin.CalcPoints(0, reinterpret_cast<CKBYTE *>(&point), sizeof(point)));
    EXPECT_FALSE(skin.CalcPoints(1, nullptr, sizeof(point))); EXPECT_FALSE(skin.CalcLocalBBox(Context, nullptr));
    Vec(point, VxVector(9, 8, 7));
}

TEST_F(SkinCalculationTest, RejectsNegativeVertexCount) {
    CalculationSkin skin; skin.SetVertexCount(1); Vertex(skin, 0, VxVector(1, 2, 3), 0);
    VxVector point(9, 8, 7);
    EXPECT_FALSE(skin.CalcPoints(-1, reinterpret_cast<CKBYTE *>(&point), sizeof(point)));
    Vec(point, VxVector(9, 8, 7));
}

class DispatchCalculationSkin : public RCKSkin {
public:
    int Calls = 0, Count = 0; CKBYTE *Position = nullptr, *Normal = nullptr; CKDWORD PositionStride = 0, NormalStride = 0;
    CKBOOL CalcPointsEx(int count, CKBYTE *position, CKDWORD stride, CKBYTE *normal, CKDWORD nstride) override {
        ++Calls; Count = count; Position = position; PositionStride = stride; Normal = normal; NormalStride = nstride; return FALSE;
    }
};
TEST_F(SkinCalculationTest, PositionOnlyWrapperDispatchesToVirtualExtendedEntry) {
    DispatchCalculationSkin skin; VxVector position;
    EXPECT_FALSE(skin.CalcPoints(7, reinterpret_cast<CKBYTE *>(&position), 20));
    EXPECT_EQ(1, skin.Calls); EXPECT_EQ(7, skin.Count); EXPECT_EQ(reinterpret_cast<CKBYTE *>(&position), skin.Position);
    EXPECT_EQ(20u, skin.PositionStride); EXPECT_EQ(nullptr, skin.Normal); EXPECT_EQ(0u, skin.NormalStride);
}
}

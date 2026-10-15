#include <gtest/gtest.h>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>
#include "CKAll.h"
#include "RCKKeyframeData.h"
#include "RCKObjectAnimation.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKMesh.h"
#include "MorphReferenceSamples.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

struct MorphOutput {
    CKDWORD Before;
    VxVector Position;
    CKDWORD After;
};

class MorphEvaluationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_ANIMATION);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override {
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }
    static void Vector(const VxVector &value, float x, float y, float z) {
        EXPECT_FLOAT_EQ(x, value.x);
        EXPECT_FLOAT_EQ(y, value.y);
        EXPECT_FLOAT_EQ(z, value.z);
    }
    static void Fill(RCKMorphController &controller, CKBOOL normals = TRUE, int vertices = 3) {
        controller.SetMorphVertexCount(vertices);
        for (int keyIndex = 0; keyIndex < 2; ++keyIndex) {
            ASSERT_EQ(keyIndex, controller.AddKey(2.0f + 8.0f * keyIndex, normals));
            CKMorphKey *key = static_cast<CKMorphKey *>(controller.GetKey(keyIndex));
            ASSERT_NE(nullptr, key);
            for (int vertex = 0; vertex < vertices; ++vertex) {
                key->PosArray[vertex].Set(2.0f + vertex + keyIndex * 8.0f,
                                         -4.0f + keyIndex * 16.0f,
                                         6.0f - keyIndex * 8.0f);
                if (normals) {
                    key->NormArray[vertex].xa = static_cast<short>(200 + vertex + keyIndex * 800);
                    key->NormArray[vertex].ya = static_cast<short>(300 + vertex + keyIndex * 400);
                }
            }
        }
    }
    static void Sentinels(MorphOutput *output, VxCompressedVector *normals, int count = 3) {
        for (int i = 0; i < count; ++i) {
            output[i].Before = 0x13572468u;
            output[i].Position.Set(123.0f, 234.0f, 345.0f);
            output[i].After = 0xfedcba98u;
            normals[i].xa = 111;
            normals[i].ya = 222;
        }
    }
    static void Guards(const MorphOutput *output, int count = 3) {
        for (int i = 0; i < count; ++i) {
            EXPECT_EQ(0x13572468u, output[i].Before);
            EXPECT_EQ(0xfedcba98u, output[i].After);
        }
    }
};

TEST_F(MorphEvaluationTest, EqualControllersIgnoreLengthButCheckTypeKeyAndVertexCounts) {
    RCKMorphController first, second, empty;
    Fill(first);
    Fill(second);
    first.SetLength(18.0f);
    second.SetLength(99.0f);
    EXPECT_TRUE(first.Compare(&second));
    EXPECT_TRUE(second.Compare(&first));
    EXPECT_FALSE(first.Compare(nullptr));
    RCKLinearPositionController wrong;
    EXPECT_FALSE(first.Compare(&wrong));
    EXPECT_FALSE(first.Compare(&empty));
    second.SetMorphVertexCount(2);
    EXPECT_FALSE(first.Compare(&second));
    EXPECT_FALSE(second.Compare(&first));
    RCKMorphController otherEmpty;
    EXPECT_TRUE(empty.Compare(&otherEmpty));
}

TEST_F(MorphEvaluationTest, ComparisonThresholdAppliesInclusivelyToTimeAndNeverToPositionValues) {
    RCKMorphController first, second;
    Fill(first, FALSE, 1);
    Fill(second, FALSE, 1);
    static_cast<CKMorphKey *>(second.GetKey(1))->TimeStep += .25f;
    EXPECT_FALSE(first.Compare(&second, .125f));
    EXPECT_TRUE(first.Compare(&second, .25f));
    EXPECT_TRUE(second.Compare(&first, .25f));
    CKMorphKey *secondKey = static_cast<CKMorphKey *>(second.GetKey(0));
    secondKey->PosArray[0].x = std::nextafter(secondKey->PosArray[0].x, FLT_MAX);
    EXPECT_FALSE(first.Compare(&second, 100.0f));
    EXPECT_FALSE(second.Compare(&first, 100.0f));
}

TEST_F(MorphEvaluationTest, PackedNormalComparisonChecksEachComponentExactly) {
    RCKMorphController first, second;
    Fill(first);
    Fill(second);
    CKMorphKey *key = static_cast<CKMorphKey *>(second.GetKey(1));
    ++key->NormArray[2].xa;
    EXPECT_FALSE(first.Compare(&second, 100.0f));
    --key->NormArray[2].xa;
    ++key->NormArray[2].ya;
    EXPECT_FALSE(first.Compare(&second, 100.0f));
    --key->NormArray[2].ya;
    EXPECT_TRUE(first.Compare(&second));
}

TEST_F(MorphEvaluationTest, OptionalNormalPresenceMustNotMakeEqualityDependOnArgumentOrder) {
    RCKMorphController positionsOnly, withNormals;
    Fill(positionsOnly, FALSE);
    Fill(withNormals, TRUE);
    EXPECT_FALSE(positionsOnly.Compare(&withNormals));
    EXPECT_FALSE(withNormals.Compare(&positionsOnly));
}

TEST_F(MorphEvaluationTest, ZeroVertexKeysStillRetainObservableNormalPresenceDuringComparison) {
    RCKMorphController positionsOnly, withNormals;
    Fill(positionsOnly, FALSE, 0);
    Fill(withNormals, TRUE, 0);
    EXPECT_FALSE(positionsOnly.Compare(&withNormals));
    EXPECT_FALSE(withNormals.Compare(&positionsOnly));
    RCKMorphController otherPositionsOnly;
    Fill(otherPositionsOnly, FALSE, 0);
    EXPECT_TRUE(positionsOnly.Compare(&otherPositionsOnly));
}

TEST_F(MorphEvaluationTest, MissingPositionPayloadDiffersFromStoredPositionsInBothDirections) {
    RCKMorphController omitted, stored;
    Fill(omitted, FALSE);
    Fill(stored, FALSE);
    CKMorphKey *key = static_cast<CKMorphKey *>(omitted.GetKey(0));
    delete[] key->PosArray;
    key->PosArray = nullptr;
    VxVector output(123.0f, 234.0f, 345.0f);
    ASSERT_TRUE(omitted.Evaluate(2.0f, 1, &output, sizeof(output), nullptr));
    Vector(output, 123.0f, 234.0f, 345.0f);
    ASSERT_TRUE(stored.Evaluate(2.0f, 1, &output, sizeof(output), nullptr));
    Vector(output, 2.0f, -4.0f, 6.0f);
    EXPECT_FALSE(omitted.Compare(&stored));
    EXPECT_FALSE(stored.Compare(&omitted));
}

TEST_F(MorphEvaluationTest, ObjectAnimationComparisonObservesNormalAvailabilityInBothDirections) {
    RCKObjectAnimation positionsOnly(Context), withNormals(Context);
    RCKMorphController *first = static_cast<RCKMorphController *>(positionsOnly.CreateController(CKANIMATION_MORPH_CONTROL));
    RCKMorphController *second = static_cast<RCKMorphController *>(withNormals.CreateController(CKANIMATION_MORPH_CONTROL));
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    Fill(*first, FALSE);
    Fill(*second, TRUE);
    positionsOnly.SetLength(10.0f);
    withNormals.SetLength(10.0f);
    EXPECT_FALSE(positionsOnly.Compare(reinterpret_cast<CKObjectAnimation *>(&withNormals)));
    EXPECT_FALSE(withNormals.Compare(reinterpret_cast<CKObjectAnimation *>(&positionsOnly)));
}

} // namespace

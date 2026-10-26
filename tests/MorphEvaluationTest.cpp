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

TEST_F(MorphEvaluationTest, GenericVirtualEntryReturnsFalseWithoutInterpretingTheResultBuffer) {
    RCKMorphController controller;
    Fill(controller);
    CKAnimController *base = &controller;
    CKDWORD output[4] = {0x13572468u, 0x24681357u, 0xabcdef01u, 0xfedcba98u};
    const CKDWORD expected[4] = {0x13572468u, 0x24681357u, 0xabcdef01u, 0xfedcba98u};
    EXPECT_FALSE(base->Evaluate(6.0f, output));
    EXPECT_EQ(0, std::memcmp(expected, output, sizeof(output)));
    EXPECT_FALSE(base->Evaluate(-20.0f, nullptr));
    EXPECT_FALSE(base->Evaluate(100.0f, output));
    EXPECT_EQ(0, std::memcmp(expected, output, sizeof(output)));
}

TEST_F(MorphEvaluationTest, EndpointsClampIntoStridedSubsetAndKeepEveryUnrequestedByte) {
    RCKMorphController controller;
    Fill(controller);
    CKMorphController *base = &controller;
    const float times[] = {-20.0f, 2.0f, 10.0f, 20.0f};
    for (int sample = 0; sample < 4; ++sample) {
        SCOPED_TRACE(times[sample]);
        MorphOutput output[3];
        VxCompressedVector normals[3];
        Sentinels(output, normals);
        ASSERT_TRUE(base->Evaluate(times[sample], 2, &output[0].Position, sizeof(MorphOutput), normals));
        const int endpoint = sample < 2 ? 0 : 1;
        for (int i = 0; i < 2; ++i) {
            Vector(output[i].Position, 2.0f + i + endpoint * 8.0f,
                   -4.0f + endpoint * 16.0f, 6.0f - endpoint * 8.0f);
            EXPECT_EQ(200 + i + endpoint * 800, normals[i].xa);
            EXPECT_EQ(300 + i + endpoint * 400, normals[i].ya);
        }
        Guards(output);
        Vector(output[2].Position, 123.0f, 234.0f, 345.0f);
        EXPECT_EQ(111, normals[2].xa);
        EXPECT_EQ(222, normals[2].ya);
    }
}

TEST_F(MorphEvaluationTest, InteriorSamplesHaveIndependentLinearPositionsAndPackedNormalValues) {
    RCKMorphController controller;
    Fill(controller);
    const float times[] = {3.0f, 4.0f, 6.0f, 8.0f, 9.0f};
    const float phase[] = {.125f, .25f, .5f, .75f, .875f};
    for (int sample = 0; sample < 5; ++sample) {
        MorphOutput output[3];
        VxCompressedVector normals[3];
        Sentinels(output, normals);
        ASSERT_TRUE(controller.Evaluate(times[sample], 3, &output[0].Position, sizeof(MorphOutput), normals));
        for (int i = 0; i < 3; ++i) {
            Vector(output[i].Position, 2.0f + i + phase[sample] * 8.0f,
                   -4.0f + phase[sample] * 16.0f, 6.0f - phase[sample] * 8.0f);
            EXPECT_EQ(200 + i + static_cast<int>(phase[sample] * 800), normals[i].xa);
            EXPECT_EQ(300 + i + static_cast<int>(phase[sample] * 400), normals[i].ya);
        }
        Guards(output);
    }
}

TEST_F(MorphEvaluationTest, PackedNormalSamplingRetainsFixedPointRoundingAndAngularWrapping) {
    RCKMorphController controller;
    Fill(controller, TRUE, 1);
    CKMorphKey *first = static_cast<CKMorphKey *>(controller.GetKey(0));
    CKMorphKey *second = static_cast<CKMorphKey *>(controller.GetKey(1));
    first->NormArray[0].xa = 100;
    first->NormArray[0].ya = 16000;
    second->NormArray[0].xa = -101;
    second->NormArray[0].ya = 400;
    VxCompressedVector normal;
    ASSERT_TRUE(controller.Evaluate(4.0f, 1, nullptr, 0, &normal));
    EXPECT_EQ(49, normal.xa); // floor(-201 / 4) + 100.
    EXPECT_EQ(16196, normal.ya); // Wrapped difference +784, quarter phase.
    ASSERT_TRUE(controller.Evaluate(6.0f, 1, nullptr, 0, &normal));
    EXPECT_EQ(-1, normal.xa);
    EXPECT_EQ(16392, normal.ya);
    ASSERT_TRUE(controller.Evaluate(20.0f, 1, nullptr, 0, &normal));
    EXPECT_EQ(-101, normal.xa);
    EXPECT_EQ(400, normal.ya); // Endpoint uses the original packed representation.
}

TEST_F(MorphEvaluationTest, OriginalRuntimeFixturesKeepValuesExceptTheKnownPositiveYawWrapDefect) {
    // Vertex 1 crosses from yaw 400 to 16000 between times 2 and 10.
    // Keep the captured DLL data intact; the corrected arc goes backwards by 784.
    const int originalYaw[] = {449,498,530,608,792,1159,1183,1184};
    const int correctedYaw[] = {351,302,269,191,8,-360,-384,-384};
    for (int fixtureIndex = 0; fixtureIndex < 10; ++fixtureIndex) {
        const MorphReference::Fixture &fixture = MorphReference::Fixtures[fixtureIndex];
        SCOPED_TRACE(fixture.name);
        RCKMorphController controller;
        ASSERT_EQ(fixture.wordCount * 4, controller.ReadKeysFrom(const_cast<unsigned int *>(fixture.words)));
        for (int sampleIndex = 0; sampleIndex < fixture.sampleCount; ++sampleIndex) {
            const MorphReference::Sample &sample = fixture.samples[sampleIndex];
            SCOPED_TRACE(sample.time);
            float positions[64];
            for (int i = 0; i < 64; ++i)
                positions[i] = -12345.0f;
            VxCompressedVector normals[8];
            const CKDWORD sentinel = 0x13572468u;
            for (int i = 0; i < 8; ++i)
                memcpy(normals + i, &sentinel, sizeof(sentinel));
            EXPECT_EQ(sample.success, controller.Evaluate(sample.time, fixture.vertexCount,
                fixture.outputMask & 1 ? positions + 4 : nullptr, fixture.stride,
                fixture.outputMask & 2 ? normals + 1 : nullptr));
            const int positionFloats = fixture.vertexCount * fixture.stride / 4 + 8;
            for (int i = 0; i < positionFloats; ++i)
                EXPECT_NEAR(sample.vertices[i], positions[i], 5.0e-5f);
            VxCompressedVector expected[8];
            memcpy(expected, sample.normals, (fixture.vertexCount + 2) * sizeof(VxCompressedVector));
            if (fixtureIndex < 4 && sampleIndex >= 2 && sampleIndex <= 9) {
                EXPECT_EQ(originalYaw[sampleIndex - 2], expected[2].ya);
                expected[2].ya = static_cast<short>(correctedYaw[sampleIndex - 2]);
            }
            EXPECT_EQ(0, memcmp(expected, normals,
                                    (fixture.vertexCount + 2) * sizeof(VxCompressedVector)));
        }
    }
}

TEST_F(MorphEvaluationTest, NormalSeamSamplingReachesTheSameDirectionAsItsEndpointKey) {
    RCKMorphController controller;
    Fill(controller, TRUE, 1);
    CKMorphKey *first = static_cast<CKMorphKey *>(controller.GetKey(0));
    CKMorphKey *last = static_cast<CKMorphKey *>(controller.GetKey(1));
    first->NormArray[0].xa = last->NormArray[0].xa = 0;
    first->NormArray[0].ya = 100;
    last->NormArray[0].ya = -100;
    VxCompressedVector normal;
    ASSERT_TRUE(controller.Evaluate(6, 1, nullptr, 0, &normal));
    EXPECT_EQ(0, normal.ya);
    VxVector decoded;
    decoded = normal;
    EXPECT_FLOAT_EQ(0, decoded.x);
    EXPECT_FLOAT_EQ(0, decoded.y);
    EXPECT_FLOAT_EQ(1, decoded.z);
    ASSERT_TRUE(controller.Evaluate(10, 1, nullptr, 0, &normal));
    EXPECT_EQ(last->NormArray[0].ya & 16383, normal.ya & 16383);
}

TEST_F(MorphEvaluationTest, MergedAnimationNormalsUseTheShortYawArc) {
    RCKObjectAnimation first(Context), last(Context);
    RCKMorphController *a = static_cast<RCKMorphController *>(first.CreateController(CKANIMATION_MORPH_CONTROL));
    RCKMorphController *b = static_cast<RCKMorphController *>(last.CreateController(CKANIMATION_MORPH_CONTROL));
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, b);
    Fill(*a, TRUE, 1);
    Fill(*b, TRUE, 1);
    first.SetLength(10);
    last.SetLength(10);
    for (int key = 0; key < 2; ++key) {
        CKMorphKey *left = static_cast<CKMorphKey *>(a->GetKey(key));
        CKMorphKey *right = static_cast<CKMorphKey *>(b->GetKey(key));
        left->NormArray[0].xa = right->NormArray[0].xa = 0;
        left->NormArray[0].ya = 100;
        right->NormArray[0].ya = -100;
    }
    CKObjectAnimation *merged = first.CreateMergedAnimation(reinterpret_cast<CKObjectAnimation *>(&last));
    ASSERT_NE(nullptr, merged);
    VxCompressedVector normal;
    merged->SetMergeFactor(.5f);
    ASSERT_TRUE(merged->EvaluateMorphTarget(6, 1, nullptr, 0, &normal));
    EXPECT_EQ(0, normal.xa);
    EXPECT_EQ(0, normal.ya);
    merged->SetMergeFactor(1);
    ASSERT_TRUE(merged->EvaluateMorphTarget(6, 1, nullptr, 0, &normal));
    EXPECT_EQ((-100 & 16383), normal.ya & 16383);
}

TEST_F(MorphEvaluationTest, PositionOnlyNormalOnlyAndOmittedOutputsKeepTheSameSample) {
    RCKMorphController controller;
    Fill(controller);
    VxVector positions[3];
    VxCompressedVector normals[3];
    ASSERT_TRUE(controller.Evaluate(4.0f, 3, positions, sizeof(VxVector), nullptr));
    ASSERT_TRUE(controller.Evaluate(4.0f, 3, nullptr, 0, normals));
    ASSERT_TRUE(controller.Evaluate(4.0f, 3, nullptr, 0, nullptr));
    for (int i = 0; i < 3; ++i) {
        Vector(positions[i], 4.0f + i, 0.0f, 4.0f);
        EXPECT_EQ(400 + i, normals[i].xa);
        EXPECT_EQ(400 + i, normals[i].ya);
    }
}

TEST_F(MorphEvaluationTest, MissingNormalsLeaveTheOptionalNormalOutputUntouched) {
    RCKMorphController controller;
    Fill(controller, FALSE);
    const float times[] = {-1.0f, 6.0f, 20.0f};
    for (int sample = 0; sample < 3; ++sample) {
        MorphOutput output[3];
        VxCompressedVector normals[3];
        Sentinels(output, normals);
        ASSERT_TRUE(controller.Evaluate(times[sample], 3, &output[0].Position, sizeof(MorphOutput), normals));
        for (int i = 0; i < 3; ++i) {
            EXPECT_EQ(111, normals[i].xa);
            EXPECT_EQ(222, normals[i].ya);
        }
        Guards(output);
    }
}

TEST_F(MorphEvaluationTest, MixedNormalPresenceKeepsPositionsAndDoesNotReadAnAbsentEndpoint) {
    for (int missing = 0; missing < 2; ++missing) {
        RCKMorphController controller;
        controller.SetMorphVertexCount(3);
        for (int keyIndex = 0; keyIndex < 2; ++keyIndex) {
            ASSERT_EQ(keyIndex, controller.AddKey(2.0f + 8.0f * keyIndex, keyIndex != missing));
            CKMorphKey *key = static_cast<CKMorphKey *>(controller.GetKey(keyIndex));
            for (int vertex = 0; vertex < 3; ++vertex)
                key->PosArray[vertex].Set(2.0f + vertex + keyIndex * 8.0f,
                                         -4.0f + keyIndex * 16.0f, 6.0f - keyIndex * 8.0f);
        }
        VxVector output[3];
        VxCompressedVector normals[3];
        for (int i = 0; i < 3; ++i) {
            normals[i].xa = 111;
            normals[i].ya = 222;
        }
        ASSERT_TRUE(controller.Evaluate(6.0f, 3, output, sizeof(VxVector), normals));
        for (int i = 0; i < 3; ++i) {
            Vector(output[i], 6.0f + i, 4.0f, 2.0f);
            EXPECT_EQ(111, normals[i].xa);
            EXPECT_EQ(222, normals[i].ya);
        }
    }
}

TEST_F(MorphEvaluationTest, RepeatedKeyTimesChooseFirstEqualAndLastForTheFollowingInterval) {
    RCKMorphController controller;
    Fill(controller, FALSE, 1);
    static_cast<CKMorphKey *>(controller.GetKey(1))->TimeStep = 2.0f;
    ASSERT_EQ(2, controller.AddKey(10.0f, FALSE));
    static_cast<CKMorphKey *>(controller.GetKey(2))->PosArray[0].Set(18.0f, 28.0f, -10.0f);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(2.0f, 1, &output, sizeof(output), nullptr));
    Vector(output, 2.0f, -4.0f, 6.0f);
    ASSERT_TRUE(controller.Evaluate(6.0f, 1, &output, sizeof(output), nullptr));
    Vector(output, 14.0f, 20.0f, -6.0f);
}

TEST_F(MorphEvaluationTest, ZeroVertexRequestsNeedAKeyAndDoNotTouchEitherOutput) {
    RCKMorphController controller;
    MorphOutput output[3];
    VxCompressedVector normals[3];
    Sentinels(output, normals);
    EXPECT_FALSE(controller.Evaluate(6.0f, 0, &output[0].Position, sizeof(MorphOutput), normals));
    ASSERT_EQ(0, controller.AddKey(2.0f, TRUE));
    EXPECT_TRUE(controller.Evaluate(6.0f, 0, &output[0].Position, sizeof(MorphOutput), normals));
    Guards(output);
    Vector(output[0].Position, 123.0f, 234.0f, 345.0f);
    EXPECT_EQ(111, normals[0].xa);
    EXPECT_EQ(222, normals[0].ya);
}

TEST_F(MorphEvaluationTest, RejectedCountOrTimeNeverPartiallyUpdatesCallerBuffers) {
    RCKMorphController controller;
    Fill(controller);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float times[] = {6.0f, 6.0f, nan, inf, -inf};
    const int counts[] = {-1, 4, 3, 3, 3};
    for (int sample = 0; sample < 5; ++sample) {
        MorphOutput output[3];
        VxCompressedVector normals[3];
        Sentinels(output, normals);
        EXPECT_FALSE(controller.Evaluate(times[sample], counts[sample], &output[0].Position, sizeof(MorphOutput), normals));
        Guards(output);
        for (int i = 0; i < 3; ++i) {
            Vector(output[i].Position, 123.0f, 234.0f, 345.0f);
            EXPECT_EQ(111, normals[i].xa);
            EXPECT_EQ(222, normals[i].ya);
        }
    }
}

TEST_F(MorphEvaluationTest, UnselectedInvalidKeyTimeDoesNotHideBehindClampedSampling) {
    RCKMorphController controller;
    Fill(controller, TRUE, 1);
    CKMorphKey *second = static_cast<CKMorphKey *>(controller.GetKey(1));
    const float bad[] = {1.0f, std::numeric_limits<float>::infinity(),
                         std::numeric_limits<float>::quiet_NaN()};
    for (int i = 0; i < 3; ++i) {
        second->TimeStep = bad[i];
        VxVector value(123.0f, 234.0f, 345.0f);
        VxCompressedVector normal;
        normal.xa = 111;
        normal.ya = 222;
        EXPECT_FALSE(controller.Evaluate(-1.0f, 1, &value, sizeof(value), &normal));
        Vector(value, 123.0f, 234.0f, 345.0f);
        EXPECT_EQ(111, normal.xa);
        EXPECT_EQ(222, normal.ya);
    }
}

TEST_F(MorphEvaluationTest, WideFiniteInterpolationKeepsEndpointsAndTinyRepresentableContributions) {
    for (int example = 0; example < 5; ++example) {
        SCOPED_TRACE(example);
        RCKMorphController controller;
        Fill(controller, FALSE, 1);
        CKMorphKey *first = static_cast<CKMorphKey *>(controller.GetKey(0));
        CKMorphKey *last = static_cast<CKMorphKey *>(controller.GetKey(1));
        first->TimeStep = example == 0 || example == 4 ? -FLT_MAX : 0.0f;
        last->TimeStep = example == 1 || example == 3 ? 10.0f : (example == 4 ? FLT_MIN : FLT_MAX);
        float sample = example == 0 || example == 4 ? 0.0f : (example == 1 ? 5.0f : (example == 3 ? 10.0f : FLT_MIN));
        if (example == 1) {
            first->PosArray[0].Set(-FLT_MAX, FLT_MAX, 3.0f);
            last->PosArray[0].Set(FLT_MAX, -FLT_MAX, 5.0f);
        } else if (example == 2) {
            first->PosArray[0].Set(0.0f, 3.0f, 4.0f);
            last->PosArray[0].Set(FLT_MAX, 3.0f, 4.0f);
        } else if (example == 3) {
            first->PosArray[0].Set(FLT_MAX, -FLT_MAX, FLT_MAX);
            last->PosArray[0].Set(1.0f, 2.0f, 3.0f);
        } else if (example == 4) {
            first->PosArray[0].Set(FLT_MAX, 3.0f, 4.0f);
            last->PosArray[0].Set(0.0f, 3.0f, 4.0f);
        }
        VxVector value;
        ASSERT_TRUE(controller.Evaluate(sample, 1, &value, sizeof(value), nullptr));
        if (example == 0)
            Vector(value, 6.0f, 4.0f, 2.0f);
        else if (example == 1)
            Vector(value, 0.0f, 0.0f, 4.0f);
        else if (example == 3)
            Vector(value, 1.0f, 2.0f, 3.0f);
        else {
            EXPECT_NEAR(FLT_MIN, value.x, FLT_MIN * 1.0e-5f);
            EXPECT_FLOAT_EQ(3.0f, value.y);
            EXPECT_FLOAT_EQ(4.0f, value.z);
        }
    }
}

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

TEST_F(MorphEvaluationTest, ObjectAnimationForwardsTheTypedStridedSampleAndHonorsIgnoreMorph) {
    RCKObjectAnimation animation(Context);
    RCKMorphController *controller = static_cast<RCKMorphController *>(animation.CreateController(CKANIMATION_MORPH_CONTROL));
    ASSERT_NE(nullptr, controller);
    Fill(*controller);
    MorphOutput output[3];
    VxCompressedVector normals[3];
    Sentinels(output, normals);
    ASSERT_TRUE(animation.EvaluateMorphTarget(4.0f, 2, &output[0].Position, sizeof(MorphOutput), normals));
    Vector(output[0].Position, 4.0f, 0.0f, 4.0f);
    Vector(output[1].Position, 5.0f, 0.0f, 4.0f);
    Vector(output[2].Position, 123.0f, 234.0f, 345.0f);
    EXPECT_EQ(400, normals[0].xa);
    EXPECT_EQ(400, normals[0].ya);
    Guards(output);
    animation.SetFlags(animation.GetFlags() | CK_OBJECTANIMATION_IGNOREMORPH);
    EXPECT_FALSE(animation.EvaluateMorphTarget(8.0f, 2, &output[0].Position, sizeof(MorphOutput), normals));
    Vector(output[0].Position, 4.0f, 0.0f, 4.0f);
    EXPECT_EQ(400, normals[0].xa);
}

TEST_F(MorphEvaluationTest, ObjectAnimationPlaybackPublishesInterpolatedPositionsAndNormalsToItsMesh) {
    RCKMesh mesh(Context);
    ASSERT_TRUE(mesh.SetVertexCount(3));
    RCK3dEntity entity(Context);
    entity.SetCurrentMesh(reinterpret_cast<CKMesh *>(&mesh));
    RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKMorphController *controller = static_cast<RCKMorphController *>(animation.CreateController(CKANIMATION_MORPH_CONTROL));
    ASSERT_NE(nullptr, controller);
    Fill(*controller, TRUE);
    for (int keyIndex = 0; keyIndex < 2; ++keyIndex) {
        CKMorphKey *key = static_cast<CKMorphKey *>(controller->GetKey(keyIndex));
        for (int vertex = 0; vertex < 3; ++vertex) {
            key->NormArray[vertex].xa = 0;
            key->NormArray[vertex].ya = static_cast<short>(keyIndex * 4096);
        }
    }
    animation.SetLength(10.0f);
    ASSERT_EQ(CK_OK, animation.SetStep(.6f));
    const float diagonal = std::sqrt(.5f);
    for (int vertex = 0; vertex < 3; ++vertex) {
        VxVector position, normal;
        mesh.GetVertexPosition(vertex, &position);
        mesh.GetVertexNormal(vertex, &normal);
        Vector(position, 6.0f + vertex, 4.0f, 2.0f);
        EXPECT_NEAR(diagonal, normal.x, .0001f);
        EXPECT_NEAR(0.0f, normal.y, .0001f);
        EXPECT_NEAR(diagonal, normal.z, .0001f);
    }
    animation.Set3dEntity(nullptr);
    entity.SetCurrentMesh(nullptr);
    while (entity.GetMeshCount())
        entity.RemoveMesh(entity.GetMesh(0));
}

} // namespace

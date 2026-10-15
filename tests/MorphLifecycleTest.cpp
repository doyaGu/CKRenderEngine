#include <gtest/gtest.h>
#include <climits>
#include <cstring>
#include "RCKKeyframeData.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class LayoutMorph : public RCKMorphController {
public:
    void RecordLayout() {
#define MORPH_FIELD(field) \
        ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(reinterpret_cast<const char *>(&field) - reinterpret_cast<const char *>(this))); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(field)))
        MORPH_FIELD(m_Type); MORPH_FIELD(m_NbKeys); MORPH_FIELD(m_Length);
        MORPH_FIELD(m_Keys); MORPH_FIELD(m_VertexCount);
#undef MORPH_FIELD
        ::testing::Test::RecordProperty("controller_size", static_cast<int>(sizeof(RCKMorphController)));
        ::testing::Test::RecordProperty("controller_alignment", static_cast<int>(alignof(RCKMorphController)));
        ::testing::Test::RecordProperty("anim_base_size", static_cast<int>(sizeof(CKAnimController)));
        ::testing::Test::RecordProperty("morph_base_size", static_cast<int>(sizeof(CKMorphController)));
        ::testing::Test::RecordProperty("morph_key_size", static_cast<int>(sizeof(CKMorphKey)));
        ::testing::Test::RecordProperty("morph_key_alignment", static_cast<int>(alignof(CKMorphKey)));
        CKMorphKey key;
        ::testing::Test::RecordProperty("key_TimeStep_offset", static_cast<int>(reinterpret_cast<const char *>(&key.TimeStep) - reinterpret_cast<const char *>(&key)));
        ::testing::Test::RecordProperty("key_PosArray_offset", static_cast<int>(reinterpret_cast<const char *>(&key.PosArray) - reinterpret_cast<const char *>(&key)));
        ::testing::Test::RecordProperty("key_NormArray_offset", static_cast<int>(reinterpret_cast<const char *>(&key.NormArray) - reinterpret_cast<const char *>(&key)));
    }
};

class DeletedMorph : public RCKMorphController {
public:
    explicit DeletedMorph(int *destroyed) : Destroyed(destroyed) {}
    ~DeletedMorph() override { ++*Destroyed; }
private:
    int *Destroyed;
};

class MorphLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override { SetProcessorSpecific_FunctionsPtr(); }
    static VxVector Point(int key, int vertex) {
        return VxVector(2.0f + key * 8.0f + vertex, 3.0f + key * 4.0f, 4.0f + key * 4.0f);
    }
    static void Vec(const VxVector &value, const VxVector &expected) {
        EXPECT_FLOAT_EQ(expected.x, value.x); EXPECT_FLOAT_EQ(expected.y, value.y); EXPECT_FLOAT_EQ(expected.z, value.z);
    }
    static void Author(RCKMorphController &controller, int count = 3, CKBOOL normals = TRUE) {
        controller.SetMorphVertexCount(count); controller.SetLength(10.0f);
        ASSERT_EQ(count, controller.GetMorphVertexCount());
        for (int i = 0; i < 2; ++i) {
            ASSERT_EQ(i, controller.AddKey(i * 10.0f, normals));
            CKMorphKey *key = static_cast<CKMorphKey *>(controller.GetKey(i)); ASSERT_NE(nullptr, key);
            for (int vertex = 0; vertex < count; ++vertex) {
                key->PosArray[vertex] = Point(i, vertex);
                if (normals) { key->NormArray[vertex].xa = 200 + i * 800; key->NormArray[vertex].ya = 300; }
            }
        }
    }
    static void Consume(RCKMorphController &controller, int count, int retained, CKBOOL normals = TRUE) {
        ASSERT_LE(count, 5); ASSERT_EQ(count, controller.GetMorphVertexCount());
        ASSERT_EQ(2, controller.GetKeyCount()); EXPECT_FLOAT_EQ(10.0f, controller.GetLength());
        VxVector positions[5]; VxCompressedVector compressed[5];
        ASSERT_TRUE(controller.Evaluate(5.0f, count, positions, sizeof(VxVector), normals ? compressed : nullptr));
        for (int i = 0; i < count; ++i) {
            Vec(positions[i], i < retained ? VxVector(6.0f + i, 5.0f, 6.0f) : VxVector(0.0f));
            if (normals) { EXPECT_EQ(i < retained ? 600 : 0, compressed[i].xa); EXPECT_EQ(i < retained ? 300 : 0, compressed[i].ya); }
        }
    }
    static void Wire(RCKMorphController &controller, int count, int retained, CKBOOL normals = TRUE) {
        // Construct bytes from independently authored coordinates, never from
        // the controller's key array or from a second invocation of Dump.
        CKDWORD expected[45] = {}; expected[0] = 2; expected[1] = count; expected[2] = normals;
        int word = 3;
        for (int key = 0; key < 2; ++key) {
            const float time = key * 10.0f; std::memcpy(expected + word++, &time, sizeof(time));
            for (int vertex = 0; vertex < count; ++vertex) {
                const VxVector point = vertex < retained ? Point(key, vertex) : VxVector(0.0f);
                std::memcpy(expected + word, &point, sizeof(point)); word += 3;
            }
            if (normals) for (int vertex = 0; vertex < count; ++vertex)
                expected[word++] = vertex < retained ? (300u << 16) | (200u + key * 800u) : 0u;
        }
        CKDWORD output[47]; for (int i = 0; i < 47; ++i) output[i] = 0x92837465u;
        ASSERT_EQ(word * 4, controller.DumpKeysTo(nullptr));
        ASSERT_EQ(word * 4, controller.DumpKeysTo(output + 1));
        EXPECT_EQ(0, std::memcmp(expected, output + 1, word * 4));
        EXPECT_EQ(0x92837465u, output[0]);
        for (int i = word + 1; i < 47; ++i) EXPECT_EQ(0x92837465u, output[i]);
    }
};
}

TEST_F(MorphLifecycleTest, ConstructorCreatesEmptySerializableStateAndCanAcceptOwnedKeys) {
    RCKMorphController controller;
    EXPECT_EQ(CKANIMATION_MORPH_CONTROL, controller.GetType()); EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_EQ(0, controller.GetMorphVertexCount()); EXPECT_FLOAT_EQ(0.0f, controller.GetLength());
    EXPECT_EQ(nullptr, controller.GetKey(0)); EXPECT_FALSE(controller.Evaluate(0.0f, 0, nullptr, 0, nullptr));
    CKDWORD empty[3] = {11, 22, 33}; ASSERT_EQ(12, controller.DumpKeysTo(empty));
    EXPECT_EQ(0u, empty[0]); EXPECT_EQ(0u, empty[1]); EXPECT_EQ(0u, empty[2]);
    Author(controller); Consume(controller, 3, 3); Wire(controller, 3, 3);
}

TEST_F(MorphLifecycleTest, PortableLayoutRecordsOwnedFieldsAndInheritedControllerState) {
    LayoutMorph controller; controller.RecordLayout();
    EXPECT_EQ(sizeof(void *) == 8 ? 40u : 24u, sizeof(RCKMorphController));
    EXPECT_EQ(sizeof(void *), alignof(RCKMorphController));
    EXPECT_EQ(sizeof(void *) == 8 ? 24u : 16u, sizeof(CKMorphController));
    EXPECT_EQ(sizeof(CKMorphController), sizeof(CKAnimController));
    EXPECT_EQ(sizeof(void *) == 8 ? 24u : 12u, sizeof(CKMorphKey));
    Author(controller, 1); Consume(controller, 1, 1); Wire(controller, 1, 1);
}

TEST_F(MorphLifecycleTest, BorrowedKeyPayloadEditsReachEvaluationAndIndependentWire) {
    RCKMorphController controller; Author(controller, 1);
    EXPECT_EQ(nullptr, controller.GetKey(-1)); EXPECT_EQ(nullptr, controller.GetKey(2)); EXPECT_EQ(nullptr, controller.GetKey(INT_MAX));
    CKAnimController *base = &controller;
    CKMorphKey *first = static_cast<CKMorphKey *>(base->GetKey(0)); ASSERT_NE(nullptr, first);
    first->PosArray[0].Set(30.0f, 9.0f, 12.0f); first->NormArray[0].xa = 400;
    VxVector point; VxCompressedVector normal;
    ASSERT_TRUE(controller.Evaluate(0.0f, 1, &point, sizeof(point), &normal)); Vec(point, VxVector(30.0f, 9.0f, 12.0f));
    EXPECT_EQ(400, normal.xa); EXPECT_EQ(300, normal.ya);
    CKDWORD actual[13] = {}; ASSERT_EQ(52, base->DumpKeysTo(actual));
    float value; std::memcpy(&value, actual + 4, sizeof(value)); EXPECT_FLOAT_EQ(30.0f, value);
    EXPECT_EQ((300u << 16) | 400u, actual[7]);
}

TEST_F(MorphLifecycleTest, CloneOwnsBothPayloadsAfterSourceMutationAndDestruction) {
    RCKMorphController copy;
    {
        RCKMorphController source; Author(source); Author(copy, 1, FALSE);
        CKAnimController *destination = &copy; ASSERT_TRUE(destination->Clone(&source));
        EXPECT_EQ(CKANIMATION_MORPH_CONTROL, copy.GetType());
        for (int i = 0; i < 2; ++i) {
            CKMorphKey *a = static_cast<CKMorphKey *>(source.GetKey(i)), *b = static_cast<CKMorphKey *>(copy.GetKey(i));
            ASSERT_NE(a, b); ASSERT_NE(a->PosArray, b->PosArray); ASSERT_NE(a->NormArray, b->NormArray);
            a->PosArray[0].Set(-30.0f, -30.0f, -30.0f); a->NormArray[0].xa = 8000;
        }
    }
    Consume(copy, 3, 3); Wire(copy, 3, 3);
    copy.SetMorphVertexCount(5); Consume(copy, 5, 3); Wire(copy, 5, 3);
}

TEST_F(MorphLifecycleTest, CloneEmptySourceReleasesDestinationAndPreservesEmptyShapeAndLength) {
    RCKMorphController destination; Author(destination);
    RCKMorphController source; source.SetMorphVertexCount(5); source.SetLength(17.0f);
    ASSERT_TRUE(destination.Clone(&source));
    EXPECT_EQ(0, destination.GetKeyCount()); EXPECT_EQ(5, destination.GetMorphVertexCount());
    EXPECT_FLOAT_EQ(17.0f, destination.GetLength()); EXPECT_EQ(nullptr, destination.GetKey(0));
    CKDWORD output[3] = {}; ASSERT_EQ(12, destination.DumpKeysTo(output));
    EXPECT_EQ(0u, output[0]); EXPECT_EQ(5u, output[1]); EXPECT_EQ(0u, output[2]);
    destination.SetMorphVertexCount(3); Author(destination); Consume(destination, 3, 3);
}

TEST_F(MorphLifecycleTest, SelfCloneRetainsWritableBorrowedKeyAndPayloadAddresses) {
    RCKMorphController controller; Author(controller);
    CKMorphKey *key = static_cast<CKMorphKey *>(controller.GetKey(0));
    VxVector *positions = key->PosArray; VxCompressedVector *normals = key->NormArray;
    ASSERT_TRUE(controller.Clone(&controller));
    EXPECT_EQ(key, controller.GetKey(0)); EXPECT_EQ(positions, key->PosArray); EXPECT_EQ(normals, key->NormArray);
    Consume(controller, 3, 3); Wire(controller, 3, 3);
    positions[0].x = 22.0f; VxVector output;
    ASSERT_TRUE(controller.Evaluate(0.0f, 1, &output, sizeof(output), nullptr)); EXPECT_FLOAT_EQ(22.0f, output.x);
}

TEST_F(MorphLifecycleTest, CloneRejectsDifferentControllerWithoutChangingTheDestinationConsumer) {
    RCKMorphController controller; Author(controller);
    CKMorphKey *key = static_cast<CKMorphKey *>(controller.GetKey(0));
    RCKLinearPositionController position; position.SetLength(21.0f);
    EXPECT_FALSE(controller.Clone(&position)); EXPECT_FALSE(controller.Clone(nullptr));
    EXPECT_EQ(key, controller.GetKey(0)); Consume(controller, 3, 3); Wire(controller, 3, 3);
}

TEST_F(MorphLifecycleTest, ResizeGrowthShrinkZeroAndRegrowthComposeWithCloneDumpAndEvaluation) {
    RCKMorphController controller; Author(controller);
    const int counts[] = {5, 2, 4, 0, 1}; const int retained[] = {3, 2, 2, 0, 0};
    CKMorphKey *key = static_cast<CKMorphKey *>(controller.GetKey(0));
    for (int step = 0; step < 5; ++step) {
        controller.SetMorphVertexCount(counts[step]); ASSERT_EQ(key, controller.GetKey(0));
        Consume(controller, counts[step], retained[step]); Wire(controller, counts[step], retained[step]);
        RCKMorphController copy; ASSERT_TRUE(copy.Clone(&controller));
        Consume(copy, counts[step], retained[step]); Wire(copy, counts[step], retained[step]);
        ASSERT_NE(nullptr, static_cast<CKMorphKey *>(copy.GetKey(0))->PosArray);
        ASSERT_NE(nullptr, static_cast<CKMorphKey *>(copy.GetKey(0))->NormArray);
    }
}

TEST_F(MorphLifecycleTest, ResizePreservesOmittedNormalsAndPositionOnlyWireAcrossOwnedClone) {
    RCKMorphController controller; Author(controller, 3, FALSE);
    controller.SetMorphVertexCount(5); Consume(controller, 5, 3, FALSE); Wire(controller, 5, 3, FALSE);
    RCKMorphController copy; ASSERT_TRUE(copy.Clone(&controller));
    for (int i = 0; i < 2; ++i) EXPECT_EQ(nullptr, static_cast<CKMorphKey *>(copy.GetKey(i))->NormArray);
    Consume(copy, 5, 3, FALSE); Wire(copy, 5, 3, FALSE);
}

TEST_F(MorphLifecycleTest, ResizeAndClonePreserveOmittedPayloadsAndEndpointOutputOwnership) {
    RCKMorphController copy;
    {
        RCKMorphController source; Author(source);
        CKMorphKey *first = static_cast<CKMorphKey *>(source.GetKey(0));
        delete[] first->PosArray; first->PosArray = nullptr; delete[] first->NormArray; first->NormArray = nullptr;
        source.SetMorphVertexCount(5); ASSERT_TRUE(copy.Clone(&source));
    }
    CKMorphKey *first = static_cast<CKMorphKey *>(copy.GetKey(0)); ASSERT_NE(nullptr, first);
    EXPECT_EQ(nullptr, first->PosArray); EXPECT_EQ(nullptr, first->NormArray);
    VxVector points[5]; VxCompressedVector normals[5];
    for (int i = 0; i < 5; ++i) { points[i].Set(90.0f, 90.0f, 90.0f); normals[i].xa = 111; normals[i].ya = 222; }
    ASSERT_TRUE(copy.Evaluate(0.0f, 5, points, sizeof(VxVector), normals));
    for (int i = 0; i < 5; ++i) { Vec(points[i], VxVector(90.0f)); EXPECT_EQ(111, normals[i].xa); EXPECT_EQ(222, normals[i].ya); }
    ASSERT_TRUE(copy.Evaluate(11.0f, 5, points, sizeof(VxVector), normals));
    for (int i = 0; i < 5; ++i) {
        Vec(points[i], i < 3 ? Point(1, i) : VxVector(0.0f));
        EXPECT_EQ(i < 3 ? 1000 : 0, normals[i].xa); EXPECT_EQ(i < 3 ? 300 : 0, normals[i].ya);
    }
}

TEST_F(MorphLifecycleTest, SameCountResizeKeepsBorrowedPayloadUsableAndRejectedSizesRetainWire) {
    RCKMorphController controller; Author(controller);
    CKMorphKey *key = static_cast<CKMorphKey *>(controller.GetKey(0));
    VxVector *positions = key->PosArray; VxCompressedVector *normals = key->NormArray;
    controller.SetMorphVertexCount(3); EXPECT_EQ(positions, key->PosArray); EXPECT_EQ(normals, key->NormArray);
    controller.SetMorphVertexCount(-1); controller.SetMorphVertexCount(INT_MAX);
    EXPECT_EQ(key, controller.GetKey(0)); EXPECT_EQ(positions, key->PosArray); EXPECT_EQ(normals, key->NormArray);
    Consume(controller, 3, 3); Wire(controller, 3, 3);
    positions[0].y = 31.0f; VxVector output;
    ASSERT_TRUE(controller.Evaluate(0.0f, 1, &output, sizeof(output), nullptr)); EXPECT_FLOAT_EQ(31.0f, output.y);
}

TEST_F(MorphLifecycleTest, VirtualDeletionRunsConcreteLifetimeWhileCloneRetainsOwnedPayloads) {
    int destroyed = 0; RCKMorphController copy;
    DeletedMorph *owned = new DeletedMorph(&destroyed); Author(*owned);
    ASSERT_TRUE(copy.Clone(owned)); CKAnimController *base = owned; delete base;
    EXPECT_EQ(1, destroyed); Consume(copy, 3, 3); Wire(copy, 3, 3);
}

TEST_F(MorphLifecycleTest, BaseInterfacesDispatchAllMorphOverloadsAndRetainShapeAcrossRawLoad) {
    RCKMorphController concrete; CKMorphController *api = &concrete; CKAnimController *base = api;
    api->SetMorphVertexCount(1); api->SetLength(12.0f);
    VxVector source(2.0f, 3.0f, 4.0f); CKMorphKey key; key.TimeStep = 0.0f; key.PosArray = &source; key.NormArray = nullptr;
    ASSERT_EQ(0, base->AddKey(&key));
    CKMorphKey *first = static_cast<CKMorphKey *>(base->GetKey(0)); ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, first->NormArray);
    first->NormArray[0].xa = 200; first->NormArray[0].ya = 300;
    key.TimeStep = 10.0f; source.Set(10.0f, 7.0f, 8.0f); ASSERT_EQ(1, api->AddKey(&key, FALSE));
    ASSERT_EQ(2, api->AddKey(20.0f, FALSE));
    CKMorphKey *last = static_cast<CKMorphKey *>(api->GetKey(2)); ASSERT_NE(nullptr, last); last->PosArray[0].Set(14.0f, 9.0f, 10.0f);
    VxVector output(123.0f); EXPECT_FALSE(base->Evaluate(5.0f, &output)); Vec(output, VxVector(123.0f));
    ASSERT_TRUE(api->Evaluate(5.0f, 1, &output, sizeof(output), nullptr)); Vec(output, VxVector(6.0f, 5.0f, 6.0f));
    api->RemoveKey(2); ASSERT_EQ(2, base->GetKeyCount());
    RCKMorphController clone; CKAnimController *cloneBase = &clone;
    ASSERT_TRUE(cloneBase->Clone(base)); ASSERT_TRUE(base->Compare(cloneBase));
    CKDWORD words[11]; ASSERT_EQ(44, base->DumpKeysTo(words));
    RCKMorphController loaded; CKAnimController *loadedBase = &loaded; ASSERT_EQ(44, loadedBase->ReadKeysFrom(words));
    // The single wire flag omits mixed normals. Compare must observe that the
    // source's first normal payload is absent from this loaded position record.
    EXPECT_FALSE(base->Compare(loadedBase));
    EXPECT_EQ(1, loaded.GetMorphVertexCount());
    ASSERT_TRUE(loaded.Evaluate(5.0f, 1, &output, sizeof(output), nullptr)); Vec(output, VxVector(6.0f, 5.0f, 6.0f));
}

#include <gtest/gtest.h>
#include <climits>
#include <cstring>
#include <new>
#include "CKAll.h"
#include "RCKKeyframeData.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
thread_local bool FailNextKeyArray = false;

class LayoutLinearScale : public RCKLinearScaleController {
public:
    void RecordLayout() {
#define LINEAR_FIELD(field) \
        ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(reinterpret_cast<const char *>(&field) - reinterpret_cast<const char *>(this))); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(field)))
        LINEAR_FIELD(m_Type); LINEAR_FIELD(m_NbKeys); LINEAR_FIELD(m_Length); LINEAR_FIELD(m_Keys);
#undef LINEAR_FIELD
        ::testing::Test::RecordProperty("controller_size", static_cast<int>(sizeof(RCKLinearScaleController)));
        ::testing::Test::RecordProperty("controller_alignment", static_cast<int>(alignof(RCKLinearScaleController)));
        ::testing::Test::RecordProperty("anim_base_size", static_cast<int>(sizeof(CKAnimController)));
        ::testing::Test::RecordProperty("scale_key_size", static_cast<int>(sizeof(CKScaleKey)));
        ::testing::Test::RecordProperty("scale_key_alignment", static_cast<int>(alignof(CKScaleKey)));
        CKScaleKey key;
        ::testing::Test::RecordProperty("key_TimeStep_offset", static_cast<int>(reinterpret_cast<const char *>(&key.TimeStep) - reinterpret_cast<const char *>(&key)));
        ::testing::Test::RecordProperty("key_TimeStep_size", static_cast<int>(sizeof(key.TimeStep)));
        ::testing::Test::RecordProperty("key_Pos_offset", static_cast<int>(reinterpret_cast<const char *>(&key.Pos) - reinterpret_cast<const char *>(&key)));
        ::testing::Test::RecordProperty("key_Pos_size", static_cast<int>(sizeof(key.Pos)));
    }
};

class DeletedLinearScale : public RCKLinearScaleController {
public:
    explicit DeletedLinearScale(int *destroyed) : Destroyed(destroyed) {}
    ~DeletedLinearScale() override { ++*Destroyed; }
private:
    int *Destroyed;
};

class LinearScaleLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override { SetProcessorSpecific_FunctionsPtr(); }
    void TearDown() override { FailNextKeyArray = false; }
    static void Vec(const VxVector &value, const VxVector &expected) {
        EXPECT_FLOAT_EQ(expected.x, value.x);
        EXPECT_FLOAT_EQ(expected.y, value.y);
        EXPECT_FLOAT_EQ(expected.z, value.z);
    }
    static void Author(CKAnimController &controller) {
        VxVector first(2.0f, 4.0f, 6.0f), last(10.0f, 16.0f, 22.0f);
        CKScaleKey a(2.0f, first), b(6.0f, last);
        ASSERT_EQ(0, controller.AddKey(&a)); ASSERT_EQ(1, controller.AddKey(&b));
        controller.SetLength(12.0f);
    }
    static void Consume(CKAnimController &controller) {
        EXPECT_EQ(CKANIMATION_LINSCL_CONTROL, controller.GetType());
        ASSERT_EQ(2, controller.GetKeyCount()); EXPECT_FLOAT_EQ(12.0f, controller.GetLength());
        VxVector result;
        ASSERT_TRUE(controller.Evaluate(4.0f, &result)); Vec(result, VxVector(6.0f, 10.0f, 14.0f));
        const float expected[] = {2.0f, 2.0f, 4.0f, 6.0f, 6.0f, 10.0f, 16.0f, 22.0f};
        unsigned char wire[40]; std::memset(wire, 0xa5, sizeof(wire));
        ASSERT_EQ(36, controller.DumpKeysTo(nullptr)); ASSERT_EQ(36, controller.DumpKeysTo(wire));
        int count = -1; std::memcpy(&count, wire, sizeof(count)); EXPECT_EQ(2, count);
        EXPECT_EQ(0, std::memcmp(expected, wire + 4, sizeof(expected)));
        for (int i = 36; i < 40; ++i) EXPECT_EQ(0xa5, wire[i]);
    }
};
}

// Exercise the real Clone allocation before its commit, rather than inferring
// failure behavior from successful copies or a source-code matching check.
void *operator new[](size_t bytes, const std::nothrow_t &) noexcept {
    if (FailNextKeyArray) { FailNextKeyArray = false; return nullptr; }
    try { return ::operator new[](bytes); }
    catch (const std::bad_alloc &) { return nullptr; }
}

TEST_F(LinearScaleLifecycleTest, ConstructorCreatesEmptyUsableStandaloneController) {
    RCKLinearScaleController controller;
    EXPECT_EQ(CKANIMATION_LINSCL_CONTROL, controller.GetType()); EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_FLOAT_EQ(0.0f, controller.GetLength()); EXPECT_EQ(nullptr, controller.GetKey(0));
    VxVector untouched(70.0f, 80.0f, 90.0f); EXPECT_FALSE(controller.Evaluate(0.0f, &untouched));
    Vec(untouched, VxVector(70.0f, 80.0f, 90.0f));
    int empty = 123; ASSERT_EQ(4, controller.DumpKeysTo(&empty)); EXPECT_EQ(0, empty);
    Author(controller); Consume(controller);
}

TEST_F(LinearScaleLifecycleTest, PortableLayoutReportsInheritedStateOwnedKeysAndWireKeyLayout) {
    LayoutLinearScale controller; controller.RecordLayout();
    EXPECT_EQ(sizeof(void *) == 8 ? 32u : 20u, sizeof(RCKLinearScaleController));
    EXPECT_EQ(sizeof(void *), alignof(RCKLinearScaleController));
    EXPECT_EQ(sizeof(void *) == 8 ? 24u : 16u, sizeof(CKAnimController));
    EXPECT_EQ(16u, sizeof(CKScaleKey)); EXPECT_EQ(4u, alignof(CKScaleKey));
    Author(controller); Consume(controller);
}

TEST_F(LinearScaleLifecycleTest, FactoryAppliesOwnerLengthAndSuppliesTheLinearScaleCategory) {
    CKKeyframeData owner; owner.m_Length = 48.0f;
    CKAnimController *controller = owner.CreateController(CKANIMATION_CONTROLLER_SCL);
    ASSERT_NE(nullptr, controller); owner.m_ScaleController = controller;
    EXPECT_EQ(CKANIMATION_LINSCL_CONTROL, controller->GetType()); EXPECT_FLOAT_EQ(48.0f, controller->GetLength());
    EXPECT_EQ(0, controller->GetKeyCount()); Author(*controller); Consume(*controller);
}

TEST_F(LinearScaleLifecycleTest, GetKeyBorrowsWritableScaleUsedByEvaluationAndWire) {
    RCKLinearScaleController controller; Author(controller); CKAnimController *api = &controller;
    CKScaleKey *first = static_cast<CKScaleKey *>(api->GetKey(0)); ASSERT_NE(nullptr, first);
    CKScaleKey *second = static_cast<CKScaleKey *>(api->GetKey(1)); ASSERT_NE(nullptr, second);
    EXPECT_EQ(sizeof(CKScaleKey), reinterpret_cast<const char *>(second) - reinterpret_cast<const char *>(first));
    first->Pos.Set(30.0f, 40.0f, 50.0f);
    VxVector sample; ASSERT_TRUE(api->Evaluate(2.0f, &sample)); Vec(sample, VxVector(30.0f, 40.0f, 50.0f));
    ASSERT_TRUE(api->Evaluate(4.0f, &sample)); Vec(sample, VxVector(20.0f, 28.0f, 36.0f));
    float wire[9] = {}; ASSERT_EQ(36, api->DumpKeysTo(wire));
    EXPECT_FLOAT_EQ(30.0f, wire[2]); EXPECT_FLOAT_EQ(40.0f, wire[3]); EXPECT_FLOAT_EQ(50.0f, wire[4]);
}

TEST_F(LinearScaleLifecycleTest, GetKeyOutsideBorrowedRangeLeavesValidKeysConsumable) {
    RCKLinearScaleController controller; Author(controller);
    EXPECT_EQ(nullptr, controller.GetKey(-1)); EXPECT_EQ(nullptr, controller.GetKey(2));
    EXPECT_EQ(nullptr, controller.GetKey(INT_MAX)); Consume(controller);
}

TEST_F(LinearScaleLifecycleTest, CloneOwnsKeysAfterSourceMutationAndDestruction) {
    RCKLinearScaleController destination; Author(destination);
    {
        RCKLinearScaleController source; Author(source);
        CKAnimController *api = &destination; ASSERT_TRUE(api->Clone(&source));
        ASSERT_NE(source.GetKey(0), destination.GetKey(0)); ASSERT_NE(source.GetKey(1), destination.GetKey(1));
        static_cast<CKScaleKey *>(source.GetKey(0))->Pos.Set(-9.0f, -9.0f, -9.0f);
        source.RemoveKey(1); source.SetLength(99.0f);
    }
    Consume(destination);
}

TEST_F(LinearScaleLifecycleTest, CloneEmptySourceReleasesDestinationAndCopiesLength) {
    RCKLinearScaleController destination; Author(destination);
    RCKLinearScaleController source; source.SetLength(37.0f);
    ASSERT_TRUE(destination.Clone(&source)); EXPECT_EQ(0, destination.GetKeyCount());
    EXPECT_EQ(nullptr, destination.GetKey(0)); EXPECT_FLOAT_EQ(37.0f, destination.GetLength());
    int empty = 123; ASSERT_EQ(4, destination.DumpKeysTo(&empty)); EXPECT_EQ(0, empty);
    Author(destination); Consume(destination);
}

TEST_F(LinearScaleLifecycleTest, SelfClonePreservesBorrowedKeyAddressesAndRemainsWritable) {
    RCKLinearScaleController controller; Author(controller);
    CKScaleKey *first = static_cast<CKScaleKey *>(controller.GetKey(0)); CKKey *last = controller.GetKey(1);
    ASSERT_TRUE(controller.Clone(&controller)); EXPECT_EQ(first, controller.GetKey(0)); EXPECT_EQ(last, controller.GetKey(1));
    Consume(controller); first->Pos.x = 18.0f;
    VxVector output; ASSERT_TRUE(controller.Evaluate(4.0f, &output)); EXPECT_FLOAT_EQ(14.0f, output.x);
}

TEST_F(LinearScaleLifecycleTest, CloneRejectsNullAndOtherTypeWithoutChangingDestination) {
    RCKLinearScaleController controller; Author(controller); CKKey *borrowed = controller.GetKey(0);
    RCKLinearPositionController position; position.SetLength(91.0f);
    EXPECT_FALSE(controller.Clone(nullptr)); EXPECT_FALSE(controller.Clone(&position));
    EXPECT_EQ(borrowed, controller.GetKey(0)); Consume(controller);
}

TEST_F(LinearScaleLifecycleTest, CloneAllocationFailureRetainsDestinationAndDoesNotConsumeSource) {
    RCKLinearScaleController controller; Author(controller); CKKey *borrowed = controller.GetKey(0);
    RCKLinearScaleController source; VxVector position(99.0f, 88.0f, 77.0f); CKScaleKey key(7.0f, position);
    ASSERT_EQ(0, source.AddKey(&key)); source.SetLength(123.0f);
    FailNextKeyArray = true; const CKBOOL cloned = controller.Clone(&source);
    const bool failureReached = !FailNextKeyArray; FailNextKeyArray = false;
    ASSERT_TRUE(failureReached); EXPECT_FALSE(cloned); EXPECT_EQ(borrowed, controller.GetKey(0)); Consume(controller);
    EXPECT_EQ(1, source.GetKeyCount()); EXPECT_FLOAT_EQ(123.0f, source.GetLength());
    VxVector output; ASSERT_TRUE(source.Evaluate(7.0f, &output)); Vec(output, position);
    ASSERT_TRUE(controller.Clone(&source)); ASSERT_TRUE(controller.Evaluate(0.0f, &output)); Vec(output, position);
}

TEST_F(LinearScaleLifecycleTest, VirtualDeletionRunsConcreteLifetimeAndLeavesCloneIndependent) {
    int destroyed = 0; CKAnimController *source = new DeletedLinearScale(&destroyed); Author(*source);
    RCKLinearScaleController destination; ASSERT_TRUE(destination.Clone(source));
    delete source; EXPECT_EQ(1, destroyed); Consume(destination);
}

TEST_F(LinearScaleLifecycleTest, AllNineVirtualEntriesComposeThroughTheBaseInterface) {
    CKAnimController *api = new RCKLinearScaleController; Author(*api);
    RCKLinearScaleController copy; EXPECT_TRUE(copy.Clone(api)); EXPECT_TRUE(api->Compare(&copy, 0.0f));
    unsigned char wire[36]; ASSERT_EQ(36, api->DumpKeysTo(wire)); api->RemoveKey(0);
    EXPECT_EQ(1, api->GetKeyCount()); ASSERT_EQ(36, api->ReadKeysFrom(wire));
    EXPECT_FLOAT_EQ(2.0f, api->GetKey(0)->GetTime()); Consume(*api); delete api;
}

TEST_F(LinearScaleLifecycleTest, ClonedScaleControllerDrivesEntityAfterItsOriginalOwnerDies) {
    ASSERT_EQ(CK_OK, CKStartUp());
    CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
    CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
    CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_ANIMATION);
    CKBuildClassHierarchyTable();
    CKContext *context = nullptr; ASSERT_EQ(CK_OK, CKCreateContext(&context, nullptr, 0, 0));
    {
        RCK3dEntity entity(context); RCKObjectAnimation animation(context);
        animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); animation.SetLength(12.0f);
        CKAnimController *destination = animation.CreateController(CKANIMATION_LINSCL_CONTROL); ASSERT_NE(nullptr, destination);
        {
            CKKeyframeData owner; owner.m_ScaleController = owner.CreateController(CKANIMATION_LINSCL_CONTROL);
            ASSERT_NE(nullptr, owner.m_ScaleController); Author(*owner.m_ScaleController);
            ASSERT_TRUE(destination->Clone(owner.m_ScaleController));
        }
        ASSERT_EQ(CK_OK, animation.SetFrame(4.0f));
        const VxMatrix &middle = entity.GetLocalMatrix();
        EXPECT_FLOAT_EQ(6.0f, middle[0][0]); EXPECT_FLOAT_EQ(10.0f, middle[1][1]); EXPECT_FLOAT_EQ(14.0f, middle[2][2]);
        EXPECT_FLOAT_EQ(0.0f, middle[0][1]); EXPECT_FLOAT_EQ(0.0f, middle[1][2]); EXPECT_FLOAT_EQ(0.0f, middle[2][0]);
        static_cast<CKScaleKey *>(destination->GetKey(0))->Pos.Set(30.0f, 40.0f, 50.0f);
        ASSERT_EQ(CK_OK, animation.SetFrame(2.0f));
        const VxMatrix &first = entity.GetLocalMatrix();
        EXPECT_FLOAT_EQ(30.0f, first[0][0]); EXPECT_FLOAT_EQ(40.0f, first[1][1]); EXPECT_FLOAT_EQ(50.0f, first[2][2]);
        animation.Set3dEntity(nullptr);
    }
    CKCloseContext(context); CKShutdown();
}

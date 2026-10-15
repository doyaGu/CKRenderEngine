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

class LayoutLinearRotation : public RCKLinearRotationController {
public:
    void RecordLayout() {
#define LINEAR_FIELD(field) \
        ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(reinterpret_cast<const char *>(&field) - reinterpret_cast<const char *>(this))); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(field)))
        LINEAR_FIELD(m_Type); LINEAR_FIELD(m_NbKeys); LINEAR_FIELD(m_Length); LINEAR_FIELD(m_Keys);
#undef LINEAR_FIELD
        ::testing::Test::RecordProperty("controller_size", static_cast<int>(sizeof(RCKLinearRotationController)));
        ::testing::Test::RecordProperty("controller_alignment", static_cast<int>(alignof(RCKLinearRotationController)));
        ::testing::Test::RecordProperty("anim_base_size", static_cast<int>(sizeof(CKAnimController)));
        ::testing::Test::RecordProperty("rotation_key_size", static_cast<int>(sizeof(CKRotationKey)));
        ::testing::Test::RecordProperty("rotation_key_alignment", static_cast<int>(alignof(CKRotationKey)));
        CKRotationKey key;
        ::testing::Test::RecordProperty("key_TimeStep_offset", static_cast<int>(reinterpret_cast<const char *>(&key.TimeStep) - reinterpret_cast<const char *>(&key)));
        ::testing::Test::RecordProperty("key_TimeStep_size", static_cast<int>(sizeof(key.TimeStep)));
        ::testing::Test::RecordProperty("key_Rot_offset", static_cast<int>(reinterpret_cast<const char *>(&key.Rot) - reinterpret_cast<const char *>(&key)));
        ::testing::Test::RecordProperty("key_Rot_size", static_cast<int>(sizeof(key.Rot)));
    }
};

class DeletedLinearRotation : public RCKLinearRotationController {
public:
    explicit DeletedLinearRotation(int *destroyed) : Destroyed(destroyed) {}
    ~DeletedLinearRotation() override { ++*Destroyed; }
private:
    int *Destroyed;
};

class LinearRotationLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override { SetProcessorSpecific_FunctionsPtr(); }
    void TearDown() override { FailNextKeyArray = false; }
    static void Quat(const VxQuaternion &value, const VxQuaternion &expected) {
        EXPECT_NEAR(expected.x, value.x, 2.0e-6f);
        EXPECT_NEAR(expected.y, value.y, 2.0e-6f);
        EXPECT_NEAR(expected.z, value.z, 2.0e-6f);
        EXPECT_NEAR(expected.w, value.w, 2.0e-6f);
    }
    static void Author(CKAnimController &controller) {
        VxQuaternion first(0.0f, 0.0f, 0.0f, 1.0f), last(0.0f, 0.0f, 1.0f, 0.0f);
        CKRotationKey a(2.0f, first), b(6.0f, last);
        ASSERT_EQ(0, controller.AddKey(&a)); ASSERT_EQ(1, controller.AddKey(&b));
        controller.SetLength(12.0f);
    }
    static void Consume(CKAnimController &controller) {
        EXPECT_EQ(CKANIMATION_LINROT_CONTROL, controller.GetType());
        ASSERT_EQ(2, controller.GetKeyCount()); EXPECT_FLOAT_EQ(12.0f, controller.GetLength());
        VxQuaternion result;
        ASSERT_TRUE(controller.Evaluate(4.0f, &result)); Quat(result, VxQuaternion(0.0f, 0.0f, 0.7071067812f, 0.7071067812f));
        const float expected[] = {2.0f, 0.0f, 0.0f, 0.0f, 1.0f, 6.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        unsigned char wire[48]; std::memset(wire, 0xa5, sizeof(wire));
        ASSERT_EQ(44, controller.DumpKeysTo(nullptr)); ASSERT_EQ(44, controller.DumpKeysTo(wire));
        int count = -1; std::memcpy(&count, wire, sizeof(count)); EXPECT_EQ(2, count);
        EXPECT_EQ(0, std::memcmp(expected, wire + 4, sizeof(expected)));
        for (int i = 44; i < 48; ++i) EXPECT_EQ(0xa5, wire[i]);
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

TEST_F(LinearRotationLifecycleTest, ConstructorCreatesEmptyUsableStandaloneController) {
    RCKLinearRotationController controller;
    EXPECT_EQ(CKANIMATION_LINROT_CONTROL, controller.GetType()); EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_FLOAT_EQ(0.0f, controller.GetLength()); EXPECT_EQ(nullptr, controller.GetKey(0));
    VxQuaternion untouched(70.0f, 80.0f, 90.0f, 100.0f); EXPECT_FALSE(controller.Evaluate(0.0f, &untouched));
    Quat(untouched, VxQuaternion(70.0f, 80.0f, 90.0f, 100.0f));
    int empty = 123; ASSERT_EQ(4, controller.DumpKeysTo(&empty)); EXPECT_EQ(0, empty);
    Author(controller); Consume(controller);
}

TEST_F(LinearRotationLifecycleTest, PortableLayoutReportsInheritedStateOwnedKeysAndWireKeyLayout) {
    LayoutLinearRotation controller; controller.RecordLayout();
    EXPECT_EQ(sizeof(void *) == 8 ? 32u : 20u, sizeof(RCKLinearRotationController));
    EXPECT_EQ(sizeof(void *), alignof(RCKLinearRotationController));
    EXPECT_EQ(sizeof(void *) == 8 ? 24u : 16u, sizeof(CKAnimController));
    EXPECT_EQ(20u, sizeof(CKRotationKey)); EXPECT_EQ(4u, alignof(CKRotationKey));
    Author(controller); Consume(controller);
}

TEST_F(LinearRotationLifecycleTest, FactoryAppliesOwnerLengthAndSuppliesTheLinearRotationCategory) {
    CKKeyframeData owner; owner.m_Length = 48.0f;
    CKAnimController *controller = owner.CreateController(CKANIMATION_CONTROLLER_ROT);
    ASSERT_NE(nullptr, controller); owner.m_RotationController = controller;
    EXPECT_EQ(CKANIMATION_LINROT_CONTROL, controller->GetType()); EXPECT_FLOAT_EQ(48.0f, controller->GetLength());
    EXPECT_EQ(0, controller->GetKeyCount()); Author(*controller); Consume(*controller);
}

TEST_F(LinearRotationLifecycleTest, GetKeyBorrowsWritableRotationUsedByEvaluationAndWire) {
    RCKLinearRotationController controller; Author(controller); CKAnimController *api = &controller;
    CKRotationKey *first = static_cast<CKRotationKey *>(api->GetKey(0)); ASSERT_NE(nullptr, first);
    CKRotationKey *second = static_cast<CKRotationKey *>(api->GetKey(1)); ASSERT_NE(nullptr, second);
    EXPECT_EQ(sizeof(CKRotationKey), reinterpret_cast<const char *>(second) - reinterpret_cast<const char *>(first));
    first->Rot = VxQuaternion(0.0f, 0.0f, 1.0f, 0.0f);
    VxQuaternion sample; ASSERT_TRUE(api->Evaluate(2.0f, &sample)); Quat(sample, first->Rot);
    ASSERT_TRUE(api->Evaluate(4.0f, &sample)); Quat(sample, VxQuaternion(0.0f, 0.0f, 1.0f, 0.0f));
    float wire[11] = {}; ASSERT_EQ(44, api->DumpKeysTo(wire));
    EXPECT_FLOAT_EQ(0.0f, wire[2]); EXPECT_FLOAT_EQ(0.0f, wire[3]);
    EXPECT_FLOAT_EQ(1.0f, wire[4]); EXPECT_FLOAT_EQ(0.0f, wire[5]);
}

TEST_F(LinearRotationLifecycleTest, GetKeyOutsideBorrowedRangeLeavesValidKeysConsumable) {
    RCKLinearRotationController controller; Author(controller);
    EXPECT_EQ(nullptr, controller.GetKey(-1)); EXPECT_EQ(nullptr, controller.GetKey(2));
    EXPECT_EQ(nullptr, controller.GetKey(INT_MAX)); Consume(controller);
}

TEST_F(LinearRotationLifecycleTest, CloneOwnsKeysAfterSourceMutationAndDestruction) {
    RCKLinearRotationController destination; Author(destination);
    {
        RCKLinearRotationController source; Author(source);
        CKAnimController *api = &destination; ASSERT_TRUE(api->Clone(&source));
        ASSERT_NE(source.GetKey(0), destination.GetKey(0)); ASSERT_NE(source.GetKey(1), destination.GetKey(1));
        static_cast<CKRotationKey *>(source.GetKey(0))->Rot = VxQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
        source.RemoveKey(1); source.SetLength(99.0f);
    }
    Consume(destination);
}

TEST_F(LinearRotationLifecycleTest, CloneEmptySourceReleasesDestinationAndCopiesLength) {
    RCKLinearRotationController destination; Author(destination);
    RCKLinearRotationController source; source.SetLength(37.0f);
    ASSERT_TRUE(destination.Clone(&source)); EXPECT_EQ(0, destination.GetKeyCount());
    EXPECT_EQ(nullptr, destination.GetKey(0)); EXPECT_FLOAT_EQ(37.0f, destination.GetLength());
    int empty = 123; ASSERT_EQ(4, destination.DumpKeysTo(&empty)); EXPECT_EQ(0, empty);
    Author(destination); Consume(destination);
}

TEST_F(LinearRotationLifecycleTest, SelfClonePreservesBorrowedKeyAddressesAndRemainsWritable) {
    RCKLinearRotationController controller; Author(controller);
    CKRotationKey *first = static_cast<CKRotationKey *>(controller.GetKey(0)); CKKey *last = controller.GetKey(1);
    ASSERT_TRUE(controller.Clone(&controller)); EXPECT_EQ(first, controller.GetKey(0)); EXPECT_EQ(last, controller.GetKey(1));
    Consume(controller); first->Rot = VxQuaternion(0.0f, 0.0f, 1.0f, 0.0f);
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(4.0f, &output)); Quat(output, first->Rot);
}

TEST_F(LinearRotationLifecycleTest, CloneRejectsNullAndOtherTypeWithoutChangingDestination) {
    RCKLinearRotationController controller; Author(controller); CKKey *borrowed = controller.GetKey(0);
    RCKLinearScaleController scale; scale.SetLength(91.0f);
    EXPECT_FALSE(controller.Clone(nullptr)); EXPECT_FALSE(controller.Clone(&scale));
    EXPECT_EQ(borrowed, controller.GetKey(0)); Consume(controller);
}

TEST_F(LinearRotationLifecycleTest, CloneAllocationFailureRetainsDestinationAndDoesNotConsumeSource) {
    RCKLinearRotationController controller; Author(controller); CKKey *borrowed = controller.GetKey(0);
    RCKLinearRotationController source; VxQuaternion rotation(1.0f, 0.0f, 0.0f, 0.0f); CKRotationKey key(7.0f, rotation);
    ASSERT_EQ(0, source.AddKey(&key)); source.SetLength(123.0f);
    FailNextKeyArray = true; const CKBOOL cloned = controller.Clone(&source);
    const bool failureReached = !FailNextKeyArray; FailNextKeyArray = false;
    ASSERT_TRUE(failureReached); EXPECT_FALSE(cloned); EXPECT_EQ(borrowed, controller.GetKey(0)); Consume(controller);
    EXPECT_EQ(1, source.GetKeyCount()); EXPECT_FLOAT_EQ(123.0f, source.GetLength());
    VxQuaternion output; ASSERT_TRUE(source.Evaluate(7.0f, &output)); Quat(output, rotation);
    ASSERT_TRUE(controller.Clone(&source)); ASSERT_TRUE(controller.Evaluate(0.0f, &output)); Quat(output, rotation);
}

TEST_F(LinearRotationLifecycleTest, VirtualDeletionRunsConcreteLifetimeAndLeavesCloneIndependent) {
    int destroyed = 0; CKAnimController *source = new DeletedLinearRotation(&destroyed); Author(*source);
    RCKLinearRotationController destination; ASSERT_TRUE(destination.Clone(source));
    delete source; EXPECT_EQ(1, destroyed); Consume(destination);
}

TEST_F(LinearRotationLifecycleTest, AllNineVirtualEntriesComposeThroughTheBaseInterface) {
    CKAnimController *api = new RCKLinearRotationController; Author(*api);
    RCKLinearRotationController copy; EXPECT_TRUE(copy.Clone(api)); EXPECT_TRUE(api->Compare(&copy, 0.0f));
    unsigned char wire[44]; ASSERT_EQ(44, api->DumpKeysTo(wire)); api->RemoveKey(0);
    EXPECT_EQ(1, api->GetKeyCount()); ASSERT_EQ(44, api->ReadKeysFrom(wire));
    EXPECT_FLOAT_EQ(2.0f, api->GetKey(0)->GetTime()); Consume(*api); delete api;
}

TEST_F(LinearRotationLifecycleTest, ClonedRotationControllerDrivesEntityAfterItsOriginalOwnerDies) {
    ASSERT_EQ(CK_OK, CKStartUp());
    CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
    CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
    CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_ANIMATION);
    CKBuildClassHierarchyTable();
    CKContext *context = nullptr; ASSERT_EQ(CK_OK, CKCreateContext(&context, nullptr, 0, 0));
    {
        RCK3dEntity entity(context); RCKObjectAnimation animation(context);
        animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); animation.SetLength(12.0f);
        CKAnimController *destination = animation.CreateController(CKANIMATION_LINROT_CONTROL); ASSERT_NE(nullptr, destination);
        {
            CKKeyframeData owner; owner.m_RotationController = owner.CreateController(CKANIMATION_LINROT_CONTROL);
            ASSERT_NE(nullptr, owner.m_RotationController); Author(*owner.m_RotationController);
            ASSERT_TRUE(destination->Clone(owner.m_RotationController));
        }
        ASSERT_EQ(CK_OK, animation.SetFrame(4.0f));
        // q=(0,0,sqrt(1/2),sqrt(1/2)): Vx matrix convention uses
        // M01=2*(x*y-z*w)=-1 and M10=2*(x*y+z*w)=+1.
        const VxMatrix &middle = entity.GetLocalMatrix();
        EXPECT_NEAR(0.0f, middle[0][0], 2.0e-6f); EXPECT_NEAR(-1.0f, middle[0][1], 2.0e-6f);
        EXPECT_NEAR(1.0f, middle[1][0], 2.0e-6f); EXPECT_NEAR(0.0f, middle[1][1], 2.0e-6f);
        EXPECT_NEAR(1.0f, middle[2][2], 2.0e-6f);
        static_cast<CKRotationKey *>(destination->GetKey(0))->Rot = VxQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
        ASSERT_EQ(CK_OK, animation.SetFrame(2.0f));
        const VxMatrix &first = entity.GetLocalMatrix();
        EXPECT_FLOAT_EQ(1.0f, first[0][0]); EXPECT_FLOAT_EQ(-1.0f, first[1][1]); EXPECT_FLOAT_EQ(-1.0f, first[2][2]);
        animation.Set3dEntity(nullptr);
    }
    CKCloseContext(context); CKShutdown();
}

#include <gtest/gtest.h>
#include <climits>
#include <cmath>
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
thread_local void *ObservedKeys = nullptr;
thread_local void *ObservedTangents = nullptr;
thread_local int KeyReleases = 0;
thread_local int TangentReleases = 0;

VxQuaternion ZRotation(double degrees) {
    const double angle = degrees * 3.14159265358979323846 / 360.0;
    return VxQuaternion(0.0f, 0.0f, static_cast<float>(std::sin(angle)), static_cast<float>(std::cos(angle)));
}

class InspectTCBScaleAxis : public RCKTCBScaleAxisController {
public:
    VxQuaternion *Tangents() const { return m_Tangents; }
    void RecordLayout() {
#define TCB_FIELD(field) \
        ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(reinterpret_cast<const char *>(&field) - reinterpret_cast<const char *>(this))); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(field)))
        TCB_FIELD(m_Type); TCB_FIELD(m_NbKeys); TCB_FIELD(m_Length); TCB_FIELD(m_Keys); TCB_FIELD(m_Tangents);
#undef TCB_FIELD
        ::testing::Test::RecordProperty("controller_size", static_cast<int>(sizeof(RCKTCBScaleAxisController)));
        ::testing::Test::RecordProperty("controller_alignment", static_cast<int>(alignof(RCKTCBScaleAxisController)));
        ::testing::Test::RecordProperty("anim_base_size", static_cast<int>(sizeof(CKAnimController)));
        ::testing::Test::RecordProperty("tcb_scale_axis_key_size", static_cast<int>(sizeof(CKTCBScaleAxisKey)));
        ::testing::Test::RecordProperty("tcb_scale_axis_key_alignment", static_cast<int>(alignof(CKTCBScaleAxisKey)));
        CKTCBScaleAxisKey key;
#define KEY_FIELD(field) \
        ::testing::Test::RecordProperty("key_" #field "_offset", static_cast<int>(reinterpret_cast<const char *>(&key.field) - reinterpret_cast<const char *>(&key))); \
        ::testing::Test::RecordProperty("key_" #field "_size", static_cast<int>(sizeof(key.field)))
        KEY_FIELD(TimeStep); KEY_FIELD(Rot); KEY_FIELD(tension); KEY_FIELD(continuity);
        KEY_FIELD(bias); KEY_FIELD(easeto); KEY_FIELD(easefrom);
#undef KEY_FIELD
    }
};

class DeletedTCBScaleAxis : public InspectTCBScaleAxis {
public:
    explicit DeletedTCBScaleAxis(int *destroyed) : Destroyed(destroyed) {}
    ~DeletedTCBScaleAxis() override { ++*Destroyed; }
private:
    int *Destroyed;
};

class CustomTCBScaleAxis : public InspectTCBScaleAxis {
public:
    int Calls = 0;
    // This also compiles with the frozen pre-fix nonvirtual declaration.
    void ComputeTangents() {
        ++Calls;
        RCKTCBScaleAxisController::ComputeTangents();
        if (m_Tangents && m_NbKeys == 2) {
            m_Tangents[1] = VxQuaternion(0.0f, 0.0f, 0.0f, 1.0f);
            m_Tangents[2] = VxQuaternion(0.0f, 0.0f, 0.0f, 1.0f);
        }
    }
};

class TCBScaleAxisLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override { SetProcessorSpecific_FunctionsPtr(); }
    void TearDown() override {
        FailNextKeyArray = false; ObservedKeys = nullptr; ObservedTangents = nullptr;
        KeyReleases = 0; TangentReleases = 0;
    }
    static void Quat(const VxQuaternion &value, const VxQuaternion &expected) {
        EXPECT_NEAR(expected.x, value.x, 3.0e-6f); EXPECT_NEAR(expected.y, value.y, 3.0e-6f);
        EXPECT_NEAR(expected.z, value.z, 3.0e-6f); EXPECT_NEAR(expected.w, value.w, 3.0e-6f);
    }
    static void Author(CKAnimController &controller) {
        VxQuaternion first(0.0f, 0.0f, 0.0f, 1.0f), last = ZRotation(90.0);
        CKTCBScaleAxisKey a(2.0f, first), b(6.0f, last);
        ASSERT_EQ(0, controller.AddKey(&a)); ASSERT_EQ(1, controller.AddKey(&b));
        controller.SetLength(12.0f);
    }
    static void Consume(CKAnimController &controller) {
        EXPECT_EQ(CKANIMATION_TCBSCLAXIS_CONTROL, controller.GetType());
        ASSERT_EQ(2, controller.GetKeyCount()); EXPECT_FLOAT_EQ(12.0f, controller.GetLength());
        VxQuaternion result; ASSERT_TRUE(controller.Evaluate(3.0f, &result)); Quat(result, ZRotation(22.5));
        const float half = 0.7071067811865475f;
        const float expected[] = {2.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  6.0f, 0.0f, 0.0f, half, half, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        unsigned char wire[88]; std::memset(wire, 0xa5, sizeof(wire));
        ASSERT_EQ(84, controller.DumpKeysTo(nullptr)); ASSERT_EQ(84, controller.DumpKeysTo(wire));
        int count = -1; std::memcpy(&count, wire, sizeof(count)); EXPECT_EQ(2, count);
        EXPECT_EQ(0, std::memcmp(expected, wire + 4, sizeof(expected)));
        for (int i = 84; i < 88; ++i) EXPECT_EQ(0xa5, wire[i]);
    }
    static void Observe(InspectTCBScaleAxis &controller) {
        ObservedKeys = controller.GetKey(0); ObservedTangents = controller.Tangents();
        KeyReleases = 0; TangentReleases = 0;
    }
};
}

// Count only the two exact owned arrays; nothrow injection reaches Clone's
// actual allocation failure without changing the runtime implementation.
void *operator new[](size_t bytes, const std::nothrow_t &) noexcept {
    if (FailNextKeyArray) { FailNextKeyArray = false; return nullptr; }
    try { return ::operator new[](bytes); }
    catch (const std::bad_alloc &) { return nullptr; }
}

void operator delete[](void *pointer) noexcept {
    if (pointer && pointer == ObservedKeys) ++KeyReleases;
    if (pointer && pointer == ObservedTangents) ++TangentReleases;
    ::operator delete(pointer);
}

void operator delete[](void *pointer, size_t) noexcept { ::operator delete[](pointer); }

TEST_F(TCBScaleAxisLifecycleTest, ConstructorCreatesEmptyUsableControllerWithoutKeysOrCache) {
    InspectTCBScaleAxis controller;
    EXPECT_EQ(CKANIMATION_TCBSCLAXIS_CONTROL, controller.GetType()); EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_FLOAT_EQ(0.0f, controller.GetLength()); EXPECT_EQ(nullptr, controller.GetKey(0)); EXPECT_EQ(nullptr, controller.Tangents());
    VxQuaternion untouched(70.0f, 80.0f, 90.0f, 100.0f); EXPECT_FALSE(controller.Evaluate(0.0f, &untouched));
    Quat(untouched, VxQuaternion(70.0f, 80.0f, 90.0f, 100.0f));
    int empty = 123; ASSERT_EQ(4, controller.DumpKeysTo(&empty)); EXPECT_EQ(0, empty);
    controller.ComputeTangents(); EXPECT_EQ(nullptr, controller.Tangents()); Author(controller); Consume(controller);
}

TEST_F(TCBScaleAxisLifecycleTest, PortableLayoutReportsOwnedKeyAndTangentPointersAndAllWireFields) {
    InspectTCBScaleAxis controller; controller.RecordLayout();
    EXPECT_EQ(sizeof(void *) == 8 ? 40u : 24u, sizeof(RCKTCBScaleAxisController));
    EXPECT_EQ(sizeof(void *), alignof(RCKTCBScaleAxisController));
    EXPECT_EQ(sizeof(void *) == 8 ? 24u : 16u, sizeof(CKAnimController));
    EXPECT_EQ(40u, sizeof(CKTCBScaleAxisKey)); EXPECT_EQ(4u, alignof(CKTCBScaleAxisKey)); Author(controller); Consume(controller);
}

TEST_F(TCBScaleAxisLifecycleTest, FactorySuppliesTCBTypeAndOwnerLength) {
    CKKeyframeData owner; owner.m_Length = 48.0f;
    owner.m_ScaleAxisController = owner.CreateController(CKANIMATION_TCBSCLAXIS_CONTROL); ASSERT_NE(nullptr, owner.m_ScaleAxisController);
    EXPECT_EQ(CKANIMATION_TCBSCLAXIS_CONTROL, owner.m_ScaleAxisController->GetType());
    EXPECT_FLOAT_EQ(48.0f, owner.m_ScaleAxisController->GetLength());
    EXPECT_EQ(0, owner.m_ScaleAxisController->GetKeyCount()); Author(*owner.m_ScaleAxisController); Consume(*owner.m_ScaleAxisController);
}

TEST_F(TCBScaleAxisLifecycleTest, GetKeyBorrowsWritablePayloadAndExplicitRecomputeRefreshesInterior) {
    InspectTCBScaleAxis controller; Author(controller); Consume(controller);
    CKTCBScaleAxisKey *first = static_cast<CKTCBScaleAxisKey *>(controller.GetKey(0));
    CKTCBScaleAxisKey *last = static_cast<CKTCBScaleAxisKey *>(controller.GetKey(1)); ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, last);
    EXPECT_EQ(sizeof(CKTCBScaleAxisKey), reinterpret_cast<const char *>(last) - reinterpret_cast<const char *>(first));
    VxQuaternion *cache = controller.Tangents(); ASSERT_NE(nullptr, cache);
    last->Rot = VxQuaternion(0.0f, 0.0f, 1.0f, 0.0f); EXPECT_EQ(cache, controller.Tangents());
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(6.0f, &output)); Quat(output, last->Rot);
    float wire[21] = {}; ASSERT_EQ(84, controller.DumpKeysTo(wire));
    EXPECT_FLOAT_EQ(0.0f, wire[12]); EXPECT_FLOAT_EQ(0.0f, wire[13]); EXPECT_FLOAT_EQ(1.0f, wire[14]); EXPECT_FLOAT_EQ(0.0f, wire[15]);
    // A mutable GetKey borrow does not invalidate native tangents. Authoring
    // clients explicitly refresh the cache after direct key writes.
    controller.ComputeTangents(); ASSERT_TRUE(controller.Evaluate(3.0f, &output)); Quat(output, ZRotation(45.0));
}

TEST_F(TCBScaleAxisLifecycleTest, GetKeyOutsideBorrowedRangeRetainsCacheAndUsableKeys) {
    InspectTCBScaleAxis controller; Author(controller); Consume(controller);
    CKKey *key = controller.GetKey(0); VxQuaternion *cache = controller.Tangents();
    EXPECT_EQ(nullptr, controller.GetKey(-1)); EXPECT_EQ(nullptr, controller.GetKey(2)); EXPECT_EQ(nullptr, controller.GetKey(INT_MAX));
    EXPECT_EQ(key, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents()); Consume(controller);
}

TEST_F(TCBScaleAxisLifecycleTest, CloneCopiesAllTCBFieldsAndRebuildsIndependentCache) {
    InspectTCBScaleAxis source, destination; Author(source); Author(destination); Consume(source); Consume(destination);
    CKTCBScaleAxisKey *first = static_cast<CKTCBScaleAxisKey *>(source.GetKey(0));
    first->tension = 1.0f; first->continuity = -0.25f; first->bias = 0.75f; first->easeto = 0.125f; first->easefrom = 0.0f;
    source.ComputeTangents(); CKKey *sourceKey = source.GetKey(0); VxQuaternion *sourceCache = source.Tangents();
    Observe(destination); ASSERT_TRUE(destination.Clone(&source));
    EXPECT_EQ(1, KeyReleases); EXPECT_EQ(1, TangentReleases); ObservedKeys = nullptr; ObservedTangents = nullptr;
    EXPECT_EQ(nullptr, destination.Tangents()); EXPECT_NE(sourceKey, destination.GetKey(0));
    CKTCBScaleAxisKey *copy = static_cast<CKTCBScaleAxisKey *>(destination.GetKey(0));
    EXPECT_FLOAT_EQ(1.0f, copy->tension); EXPECT_FLOAT_EQ(-0.25f, copy->continuity); EXPECT_FLOAT_EQ(0.75f, copy->bias);
    EXPECT_FLOAT_EQ(0.125f, copy->easeto); EXPECT_FLOAT_EQ(0.0f, copy->easefrom);
    VxQuaternion sample; ASSERT_TRUE(destination.Evaluate(3.0f, &sample));
    // In the common Z axis, q0=0 and q1=90 degrees. T0=1 gives an
    // outgoing control rotation -45. The inner Slerp at 1/4 is -11.25,
    // and outer weight 3/8 gives 22.5 + 3/8*(-11.25-22.5) = 9.84375.
    Quat(sample, ZRotation(9.84375)); ASSERT_NE(nullptr, destination.Tangents()); EXPECT_NE(sourceCache, destination.Tangents());
    first->Rot = VxQuaternion(1.0f, 0.0f, 0.0f, 0.0f); source.RemoveKey(1); source.SetLength(99.0f);
    ASSERT_TRUE(destination.Evaluate(3.0f, &sample)); Quat(sample, ZRotation(9.84375));
    EXPECT_FLOAT_EQ(12.0f, destination.GetLength()); EXPECT_EQ(2, destination.GetKeyCount());
}

TEST_F(TCBScaleAxisLifecycleTest, CloneOwnsKeysAfterPopulatedSourceCacheAndSourceDestruction) {
    InspectTCBScaleAxis destination; Author(destination); Consume(destination);
    { InspectTCBScaleAxis source; Author(source); Consume(source); ASSERT_TRUE(destination.Clone(&source));
      ASSERT_NE(source.GetKey(0), destination.GetKey(0)); EXPECT_EQ(nullptr, destination.Tangents()); }
    Consume(destination);
}

TEST_F(TCBScaleAxisLifecycleTest, CloneEmptySourceReleasesBothArraysAndCopiesLength) {
    InspectTCBScaleAxis destination; Author(destination); Consume(destination);
    InspectTCBScaleAxis source; source.SetLength(37.0f); Observe(destination);
    ASSERT_TRUE(destination.Clone(&source)); EXPECT_EQ(1, KeyReleases); EXPECT_EQ(1, TangentReleases); ObservedKeys = nullptr; ObservedTangents = nullptr;
    EXPECT_EQ(0, destination.GetKeyCount()); EXPECT_EQ(nullptr, destination.GetKey(0)); EXPECT_EQ(nullptr, destination.Tangents());
    EXPECT_FLOAT_EQ(37.0f, destination.GetLength()); Author(destination); Consume(destination);
}

TEST_F(TCBScaleAxisLifecycleTest, SelfClonePreservesKeysAndExistingCacheWithoutAllocating) {
    InspectTCBScaleAxis controller; Author(controller); Consume(controller);
    CKKey *key = controller.GetKey(0); VxQuaternion *cache = controller.Tangents(); Observe(controller);
    FailNextKeyArray = true; const CKBOOL cloned = controller.Clone(&controller);
    const bool allocated = !FailNextKeyArray; FailNextKeyArray = false;
    EXPECT_TRUE(cloned); EXPECT_FALSE(allocated); EXPECT_EQ(key, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents());
    EXPECT_EQ(0, KeyReleases); EXPECT_EQ(0, TangentReleases); Consume(controller);
}

TEST_F(TCBScaleAxisLifecycleTest, CloneRejectsNullAndSameSizeDifferentCategoryWithoutChangingCache) {
    InspectTCBScaleAxis controller; Author(controller); Consume(controller); RCKTCBRotationController rotation; Author(rotation);
    CKKey *key = controller.GetKey(0); VxQuaternion *cache = controller.Tangents(); Observe(controller);
    EXPECT_FALSE(controller.Clone(nullptr)); EXPECT_FALSE(controller.Clone(&rotation));
    EXPECT_EQ(key, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents()); EXPECT_EQ(0, KeyReleases); EXPECT_EQ(0, TangentReleases); Consume(controller);
}

TEST_F(TCBScaleAxisLifecycleTest, CloneAllocationFailureRetainsDestinationKeysCacheAndLength) {
    InspectTCBScaleAxis destination, source; Author(destination); Consume(destination);
    VxQuaternion rotation(1.0f, 0.0f, 0.0f, 0.0f); CKTCBScaleAxisKey key(7.0f, rotation, 0.5f, -0.5f, 0.25f, 0.125f, 0.75f);
    ASSERT_EQ(0, source.AddKey(&key)); source.SetLength(123.0f);
    CKKey *borrowed = destination.GetKey(0); VxQuaternion *cache = destination.Tangents(); Observe(destination);
    FailNextKeyArray = true; const CKBOOL cloned = destination.Clone(&source); const bool failureReached = !FailNextKeyArray; FailNextKeyArray = false;
    ASSERT_TRUE(failureReached); EXPECT_FALSE(cloned); EXPECT_EQ(borrowed, destination.GetKey(0)); EXPECT_EQ(cache, destination.Tangents());
    EXPECT_EQ(0, KeyReleases); EXPECT_EQ(0, TangentReleases); Consume(destination);
    EXPECT_EQ(1, source.GetKeyCount()); EXPECT_FLOAT_EQ(123.0f, source.GetLength());
    VxQuaternion output; ASSERT_TRUE(source.Evaluate(7.0f, &output)); Quat(output, rotation);
    ASSERT_TRUE(destination.Clone(&source)); EXPECT_FLOAT_EQ(123.0f, destination.GetLength());
    EXPECT_EQ(nullptr, destination.Tangents()); ASSERT_TRUE(destination.Evaluate(0.0f, &output)); Quat(output, rotation);
}

TEST_F(TCBScaleAxisLifecycleTest, VirtualDeletionReleasesOwnedKeysAndTangentsExactlyOnce) {
    int destroyed = 0; DeletedTCBScaleAxis *source = new DeletedTCBScaleAxis(&destroyed); Author(*source); Consume(*source);
    InspectTCBScaleAxis destination; ASSERT_TRUE(destination.Clone(source)); Observe(*source); CKAnimController *api = source; delete api;
    EXPECT_EQ(1, destroyed); EXPECT_EQ(1, KeyReleases); EXPECT_EQ(1, TangentReleases); ObservedKeys = nullptr; ObservedTangents = nullptr; Consume(destination);
}

TEST_F(TCBScaleAxisLifecycleTest, BaseNineVirtualEntriesComposeThroughTheBaseInterface) {
    CKAnimController *api = new RCKTCBScaleAxisController; Author(*api);
    RCKTCBScaleAxisController copy; EXPECT_TRUE(copy.Clone(api)); EXPECT_TRUE(api->Compare(&copy, 0.0f));
    unsigned char wire[84]; ASSERT_EQ(84, api->DumpKeysTo(wire)); api->RemoveKey(0);
    EXPECT_EQ(1, api->GetKeyCount()); ASSERT_EQ(84, api->ReadKeysFrom(wire));
    EXPECT_FLOAT_EQ(2.0f, api->GetKey(0)->GetTime()); Consume(*api); delete api;
}

TEST_F(TCBScaleAxisLifecycleTest, TenthVirtualSlotCustomizesTangentsThroughEvaluateAndConcreteBase) {
    CustomTCBScaleAxis controller; CKAnimController *api = &controller; Author(*api);
    VxQuaternion output; ASSERT_TRUE(api->Evaluate(3.0f, &output));
    // Tangent controls both identity: inner rotation=0, endpoint rotation=22.5,
    // outer Squad weight=3/8. The independent expected rotation is 14.0625.
    EXPECT_EQ(1, controller.Calls); Quat(output, ZRotation(14.0625));
    ASSERT_TRUE(api->Evaluate(3.0f, &output)); EXPECT_EQ(1, controller.Calls);
    RCKTCBScaleAxisController *concreteBase = &controller; concreteBase->ComputeTangents(); EXPECT_EQ(2, controller.Calls);
    ASSERT_TRUE(api->Evaluate(3.0f, &output)); Quat(output, ZRotation(14.0625));
}

TEST_F(TCBScaleAxisLifecycleTest, ClonedTCBControllerDrivesEntityAfterOriginalOwnerAndCacheDie) {
    ASSERT_EQ(CK_OK, CKStartUp()); CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
    CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT); CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_ANIMATION); CKBuildClassHierarchyTable();
    CKContext *context = nullptr; ASSERT_EQ(CK_OK, CKCreateContext(&context, nullptr, 0, 0));
    {
        RCK3dEntity entity(context); RCKObjectAnimation animation(context);
        animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); animation.SetLength(12.0f);
        CKAnimController *destination = animation.CreateController(CKANIMATION_TCBSCLAXIS_CONTROL); ASSERT_NE(nullptr, destination);
        { CKKeyframeData owner; owner.m_ScaleAxisController = owner.CreateController(CKANIMATION_TCBSCLAXIS_CONTROL);
          ASSERT_NE(nullptr, owner.m_ScaleAxisController); Author(*owner.m_ScaleAxisController); Consume(*owner.m_ScaleAxisController);
          ASSERT_TRUE(destination->Clone(owner.m_ScaleAxisController)); }
        VxVector scale(2.0f, 5.0f, 11.0f), position(7.0f, 8.0f, 9.0f);
        animation.AddScaleKey(0.0f, &scale); animation.AddPositionKey(0.0f, &position);
        ASSERT_EQ(CK_OK, animation.SetFrame(3.0f)); const VxMatrix &first = entity.GetLocalMatrix();
        // The 22.5-degree principal-axis orientation composes a symmetric
        // stretch: R(axis)^-1 * diag(2,5,11) * R(axis), not a rigid pose.
        const float halfRoot = 0.7071067811865475f;
        EXPECT_NEAR(3.5f - 1.5f * halfRoot, first[0][0], 3.0e-6f);
        EXPECT_NEAR(-1.5f * halfRoot, first[0][1], 3.0e-6f);
        EXPECT_NEAR(-1.5f * halfRoot, first[1][0], 3.0e-6f);
        EXPECT_NEAR(3.5f + 1.5f * halfRoot, first[1][1], 3.0e-6f);
        EXPECT_NEAR(11.0f, first[2][2], 3.0e-6f);
        EXPECT_FLOAT_EQ(0.0f, first[0][2]); EXPECT_FLOAT_EQ(0.0f, first[2][0]);
        EXPECT_FLOAT_EQ(7.0f, first[3][0]); EXPECT_FLOAT_EQ(8.0f, first[3][1]); EXPECT_FLOAT_EQ(9.0f, first[3][2]);
        static_cast<CKTCBScaleAxisKey *>(destination->GetKey(1))->Rot = VxQuaternion(0.0f, 0.0f, 1.0f, 0.0f);
        static_cast<RCKTCBScaleAxisController *>(destination)->ComputeTangents();
        ASSERT_EQ(CK_OK, animation.SetFrame(3.0f)); const VxMatrix &updated = entity.GetLocalMatrix();
        // Recomputing the changed endpoint moves the principal axis to 45
        // degrees; unequal scale creates equal diagonals and symmetric shear.
        EXPECT_NEAR(3.5f, updated[0][0], 3.0e-6f); EXPECT_NEAR(-1.5f, updated[0][1], 3.0e-6f);
        EXPECT_NEAR(-1.5f, updated[1][0], 3.0e-6f); EXPECT_NEAR(3.5f, updated[1][1], 3.0e-6f);
        EXPECT_NEAR(11.0f, updated[2][2], 3.0e-6f);
        EXPECT_FLOAT_EQ(7.0f, updated[3][0]); EXPECT_FLOAT_EQ(8.0f, updated[3][1]); EXPECT_FLOAT_EQ(9.0f, updated[3][2]);
        animation.Set3dEntity(nullptr);
    }
    CKCloseContext(context); CKShutdown();
}

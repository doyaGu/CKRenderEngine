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

class InspectTCBRotation : public RCKTCBRotationController {
public:
    VxQuaternion *Tangents() const { return m_Tangents; }
    void RecordLayout() {
#define TCB_FIELD(field) \
        ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(reinterpret_cast<const char *>(&field) - reinterpret_cast<const char *>(this))); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(field)))
        TCB_FIELD(m_Type); TCB_FIELD(m_NbKeys); TCB_FIELD(m_Length); TCB_FIELD(m_Keys); TCB_FIELD(m_Tangents);
#undef TCB_FIELD
        ::testing::Test::RecordProperty("controller_size", static_cast<int>(sizeof(RCKTCBRotationController)));
        ::testing::Test::RecordProperty("controller_alignment", static_cast<int>(alignof(RCKTCBRotationController)));
        ::testing::Test::RecordProperty("anim_base_size", static_cast<int>(sizeof(CKAnimController)));
        ::testing::Test::RecordProperty("tcb_rotation_key_size", static_cast<int>(sizeof(CKTCBRotationKey)));
        ::testing::Test::RecordProperty("tcb_rotation_key_alignment", static_cast<int>(alignof(CKTCBRotationKey)));
        CKTCBRotationKey key;
#define KEY_FIELD(field) \
        ::testing::Test::RecordProperty("key_" #field "_offset", static_cast<int>(reinterpret_cast<const char *>(&key.field) - reinterpret_cast<const char *>(&key))); \
        ::testing::Test::RecordProperty("key_" #field "_size", static_cast<int>(sizeof(key.field)))
        KEY_FIELD(TimeStep); KEY_FIELD(Rot); KEY_FIELD(tension); KEY_FIELD(continuity);
        KEY_FIELD(bias); KEY_FIELD(easeto); KEY_FIELD(easefrom);
#undef KEY_FIELD
    }
};

class DeletedTCBRotation : public InspectTCBRotation {
public:
    explicit DeletedTCBRotation(int *destroyed) : Destroyed(destroyed) {}
    ~DeletedTCBRotation() override { ++*Destroyed; }
private:
    int *Destroyed;
};

class CustomTCBRotation : public InspectTCBRotation {
public:
    int Calls = 0;
    // This also compiles with the frozen pre-fix nonvirtual declaration.
    void ComputeTangents() {
        ++Calls;
        RCKTCBRotationController::ComputeTangents();
        if (m_Tangents && m_NbKeys == 2) {
            m_Tangents[1] = VxQuaternion(0.0f, 0.0f, 0.0f, 1.0f);
            m_Tangents[2] = VxQuaternion(0.0f, 0.0f, 0.0f, 1.0f);
        }
    }
};

class TCBRotationLifecycleTest : public ::testing::Test {
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
        CKTCBRotationKey a(2.0f, first), b(6.0f, last);
        ASSERT_EQ(0, controller.AddKey(&a)); ASSERT_EQ(1, controller.AddKey(&b));
        controller.SetLength(12.0f);
    }
    static void Consume(CKAnimController &controller) {
        EXPECT_EQ(CKANIMATION_TCBROT_CONTROL, controller.GetType());
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
    static void Observe(InspectTCBRotation &controller) {
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

TEST_F(TCBRotationLifecycleTest, ConstructorCreatesEmptyUsableControllerWithoutKeysOrCache) {
    InspectTCBRotation controller;
    EXPECT_EQ(CKANIMATION_TCBROT_CONTROL, controller.GetType()); EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_FLOAT_EQ(0.0f, controller.GetLength()); EXPECT_EQ(nullptr, controller.GetKey(0)); EXPECT_EQ(nullptr, controller.Tangents());
    VxQuaternion untouched(70.0f, 80.0f, 90.0f, 100.0f); EXPECT_FALSE(controller.Evaluate(0.0f, &untouched));
    Quat(untouched, VxQuaternion(70.0f, 80.0f, 90.0f, 100.0f));
    int empty = 123; ASSERT_EQ(4, controller.DumpKeysTo(&empty)); EXPECT_EQ(0, empty);
    controller.ComputeTangents(); EXPECT_EQ(nullptr, controller.Tangents()); Author(controller); Consume(controller);
}

TEST_F(TCBRotationLifecycleTest, PortableLayoutReportsOwnedKeyAndTangentPointersAndAllWireFields) {
    InspectTCBRotation controller; controller.RecordLayout();
    EXPECT_EQ(sizeof(void *) == 8 ? 40u : 24u, sizeof(RCKTCBRotationController));
    EXPECT_EQ(sizeof(void *), alignof(RCKTCBRotationController));
    EXPECT_EQ(sizeof(void *) == 8 ? 24u : 16u, sizeof(CKAnimController));
    EXPECT_EQ(40u, sizeof(CKTCBRotationKey)); EXPECT_EQ(4u, alignof(CKTCBRotationKey)); Author(controller); Consume(controller);
}

TEST_F(TCBRotationLifecycleTest, FactorySuppliesTCBTypeAndOwnerLength) {
    CKKeyframeData owner; owner.m_Length = 48.0f;
    owner.m_RotationController = owner.CreateController(CKANIMATION_TCBROT_CONTROL); ASSERT_NE(nullptr, owner.m_RotationController);
    EXPECT_EQ(CKANIMATION_TCBROT_CONTROL, owner.m_RotationController->GetType());
    EXPECT_FLOAT_EQ(48.0f, owner.m_RotationController->GetLength());
    EXPECT_EQ(0, owner.m_RotationController->GetKeyCount()); Author(*owner.m_RotationController); Consume(*owner.m_RotationController);
}

TEST_F(TCBRotationLifecycleTest, GetKeyBorrowsWritablePayloadAndExplicitRecomputeRefreshesInterior) {
    InspectTCBRotation controller; Author(controller); Consume(controller);
    CKTCBRotationKey *first = static_cast<CKTCBRotationKey *>(controller.GetKey(0));
    CKTCBRotationKey *last = static_cast<CKTCBRotationKey *>(controller.GetKey(1)); ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, last);
    EXPECT_EQ(sizeof(CKTCBRotationKey), reinterpret_cast<const char *>(last) - reinterpret_cast<const char *>(first));
    VxQuaternion *cache = controller.Tangents(); ASSERT_NE(nullptr, cache);
    last->Rot = VxQuaternion(0.0f, 0.0f, 1.0f, 0.0f); EXPECT_EQ(cache, controller.Tangents());
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(6.0f, &output)); Quat(output, last->Rot);
    float wire[21] = {}; ASSERT_EQ(84, controller.DumpKeysTo(wire));
    EXPECT_FLOAT_EQ(0.0f, wire[12]); EXPECT_FLOAT_EQ(0.0f, wire[13]); EXPECT_FLOAT_EQ(1.0f, wire[14]); EXPECT_FLOAT_EQ(0.0f, wire[15]);
    // A mutable GetKey borrow does not invalidate native tangents. Authoring
    // clients explicitly refresh the cache after direct key writes.
    controller.ComputeTangents(); ASSERT_TRUE(controller.Evaluate(3.0f, &output)); Quat(output, ZRotation(45.0));
}

TEST_F(TCBRotationLifecycleTest, GetKeyOutsideBorrowedRangeRetainsCacheAndUsableKeys) {
    InspectTCBRotation controller; Author(controller); Consume(controller);
    CKKey *key = controller.GetKey(0); VxQuaternion *cache = controller.Tangents();
    EXPECT_EQ(nullptr, controller.GetKey(-1)); EXPECT_EQ(nullptr, controller.GetKey(2)); EXPECT_EQ(nullptr, controller.GetKey(INT_MAX));
    EXPECT_EQ(key, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents()); Consume(controller);
}

TEST_F(TCBRotationLifecycleTest, CloneCopiesAllTCBFieldsAndRebuildsIndependentCache) {
    InspectTCBRotation source, destination; Author(source); Author(destination); Consume(source); Consume(destination);
    CKTCBRotationKey *first = static_cast<CKTCBRotationKey *>(source.GetKey(0));
    first->tension = 1.0f; first->continuity = -0.25f; first->bias = 0.75f; first->easeto = 0.125f; first->easefrom = 0.0f;
    source.ComputeTangents(); CKKey *sourceKey = source.GetKey(0); VxQuaternion *sourceCache = source.Tangents();
    Observe(destination); ASSERT_TRUE(destination.Clone(&source));
    EXPECT_EQ(1, KeyReleases); EXPECT_EQ(1, TangentReleases); ObservedKeys = nullptr; ObservedTangents = nullptr;
    EXPECT_EQ(nullptr, destination.Tangents()); EXPECT_NE(sourceKey, destination.GetKey(0));
    CKTCBRotationKey *copy = static_cast<CKTCBRotationKey *>(destination.GetKey(0));
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

TEST_F(TCBRotationLifecycleTest, CloneOwnsKeysAfterPopulatedSourceCacheAndSourceDestruction) {
    InspectTCBRotation destination; Author(destination); Consume(destination);
    { InspectTCBRotation source; Author(source); Consume(source); ASSERT_TRUE(destination.Clone(&source));
      ASSERT_NE(source.GetKey(0), destination.GetKey(0)); EXPECT_EQ(nullptr, destination.Tangents()); }
    Consume(destination);
}

TEST_F(TCBRotationLifecycleTest, CloneEmptySourceReleasesBothArraysAndCopiesLength) {
    InspectTCBRotation destination; Author(destination); Consume(destination);
    InspectTCBRotation source; source.SetLength(37.0f); Observe(destination);
    ASSERT_TRUE(destination.Clone(&source)); EXPECT_EQ(1, KeyReleases); EXPECT_EQ(1, TangentReleases); ObservedKeys = nullptr; ObservedTangents = nullptr;
    EXPECT_EQ(0, destination.GetKeyCount()); EXPECT_EQ(nullptr, destination.GetKey(0)); EXPECT_EQ(nullptr, destination.Tangents());
    EXPECT_FLOAT_EQ(37.0f, destination.GetLength()); Author(destination); Consume(destination);
}

TEST_F(TCBRotationLifecycleTest, SelfClonePreservesKeysAndExistingCacheWithoutAllocating) {
    InspectTCBRotation controller; Author(controller); Consume(controller);
    CKKey *key = controller.GetKey(0); VxQuaternion *cache = controller.Tangents(); Observe(controller);
    FailNextKeyArray = true; const CKBOOL cloned = controller.Clone(&controller);
    const bool allocated = !FailNextKeyArray; FailNextKeyArray = false;
    EXPECT_TRUE(cloned); EXPECT_FALSE(allocated); EXPECT_EQ(key, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents());
    EXPECT_EQ(0, KeyReleases); EXPECT_EQ(0, TangentReleases); Consume(controller);
}

TEST_F(TCBRotationLifecycleTest, CloneRejectsNullAndSameSizeDifferentCategoryWithoutChangingCache) {
    InspectTCBRotation controller; Author(controller); Consume(controller); RCKTCBScaleAxisController axis; Author(axis);
    CKKey *key = controller.GetKey(0); VxQuaternion *cache = controller.Tangents(); Observe(controller);
    EXPECT_FALSE(controller.Clone(nullptr)); EXPECT_FALSE(controller.Clone(&axis));
    EXPECT_EQ(key, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents()); EXPECT_EQ(0, KeyReleases); EXPECT_EQ(0, TangentReleases); Consume(controller);
}

TEST_F(TCBRotationLifecycleTest, CloneAllocationFailureRetainsDestinationKeysCacheAndLength) {
    InspectTCBRotation destination, source; Author(destination); Consume(destination);
    VxQuaternion rotation(1.0f, 0.0f, 0.0f, 0.0f); CKTCBRotationKey key(7.0f, rotation, 0.5f, -0.5f, 0.25f, 0.125f, 0.75f);
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

TEST_F(TCBRotationLifecycleTest, VirtualDeletionReleasesOwnedKeysAndTangentsExactlyOnce) {
    int destroyed = 0; DeletedTCBRotation *source = new DeletedTCBRotation(&destroyed); Author(*source); Consume(*source);
    InspectTCBRotation destination; ASSERT_TRUE(destination.Clone(source)); Observe(*source); CKAnimController *api = source; delete api;
    EXPECT_EQ(1, destroyed); EXPECT_EQ(1, KeyReleases); EXPECT_EQ(1, TangentReleases); ObservedKeys = nullptr; ObservedTangents = nullptr; Consume(destination);
}

TEST_F(TCBRotationLifecycleTest, BaseNineVirtualEntriesComposeThroughTheBaseInterface) {
    CKAnimController *api = new RCKTCBRotationController; Author(*api);
    RCKTCBRotationController copy; EXPECT_TRUE(copy.Clone(api)); EXPECT_TRUE(api->Compare(&copy, 0.0f));
    unsigned char wire[84]; ASSERT_EQ(84, api->DumpKeysTo(wire)); api->RemoveKey(0);
    EXPECT_EQ(1, api->GetKeyCount()); ASSERT_EQ(84, api->ReadKeysFrom(wire));
    EXPECT_FLOAT_EQ(2.0f, api->GetKey(0)->GetTime()); Consume(*api); delete api;
}

TEST_F(TCBRotationLifecycleTest, TenthVirtualSlotCustomizesTangentsThroughEvaluateAndConcreteBase) {
    CustomTCBRotation controller; CKAnimController *api = &controller; Author(*api);
    VxQuaternion output; ASSERT_TRUE(api->Evaluate(3.0f, &output));
    // Tangent controls both identity: inner rotation=0, endpoint rotation=22.5,
    // outer Squad weight=3/8. The independent expected rotation is 14.0625.
    EXPECT_EQ(1, controller.Calls); Quat(output, ZRotation(14.0625));
    ASSERT_TRUE(api->Evaluate(3.0f, &output)); EXPECT_EQ(1, controller.Calls);
    RCKTCBRotationController *concreteBase = &controller; concreteBase->ComputeTangents(); EXPECT_EQ(2, controller.Calls);
    ASSERT_TRUE(api->Evaluate(3.0f, &output)); Quat(output, ZRotation(14.0625));
}

TEST_F(TCBRotationLifecycleTest, ClonedTCBControllerDrivesEntityAfterOriginalOwnerAndCacheDie) {
    ASSERT_EQ(CK_OK, CKStartUp()); CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
    CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT); CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_ANIMATION); CKBuildClassHierarchyTable();
    CKContext *context = nullptr; ASSERT_EQ(CK_OK, CKCreateContext(&context, nullptr, 0, 0));
    {
        RCK3dEntity entity(context); RCKObjectAnimation animation(context);
        animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); animation.SetLength(12.0f);
        CKAnimController *destination = animation.CreateController(CKANIMATION_TCBROT_CONTROL); ASSERT_NE(nullptr, destination);
        { CKKeyframeData owner; owner.m_RotationController = owner.CreateController(CKANIMATION_TCBROT_CONTROL);
          ASSERT_NE(nullptr, owner.m_RotationController); Author(*owner.m_RotationController); Consume(*owner.m_RotationController);
          ASSERT_TRUE(destination->Clone(owner.m_RotationController)); }
        ASSERT_EQ(CK_OK, animation.SetFrame(3.0f)); const VxMatrix &first = entity.GetLocalMatrix();
        const float c = 0.9238795325f, s = 0.3826834324f;
        EXPECT_NEAR(c, first[0][0], 3.0e-6f); EXPECT_NEAR(-s, first[0][1], 3.0e-6f);
        EXPECT_NEAR(s, first[1][0], 3.0e-6f); EXPECT_NEAR(c, first[1][1], 3.0e-6f); EXPECT_NEAR(1.0f, first[2][2], 3.0e-6f);
        static_cast<CKTCBRotationKey *>(destination->GetKey(1))->Rot = VxQuaternion(0.0f, 0.0f, 1.0f, 0.0f);
        static_cast<RCKTCBRotationController *>(destination)->ComputeTangents();
        ASSERT_EQ(CK_OK, animation.SetFrame(3.0f)); const VxMatrix &updated = entity.GetLocalMatrix();
        const float half = 0.7071067812f;
        EXPECT_NEAR(half, updated[0][0], 3.0e-6f); EXPECT_NEAR(-half, updated[0][1], 3.0e-6f);
        EXPECT_NEAR(half, updated[1][0], 3.0e-6f); EXPECT_NEAR(half, updated[1][1], 3.0e-6f); EXPECT_NEAR(1.0f, updated[2][2], 3.0e-6f);
        animation.Set3dEntity(nullptr);
    }
    CKCloseContext(context); CKShutdown();
}

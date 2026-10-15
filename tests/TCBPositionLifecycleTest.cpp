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
thread_local void *ObservedKeys = nullptr;
thread_local void *ObservedTangents = nullptr;
thread_local int KeyReleases = 0;
thread_local int TangentReleases = 0;

class InspectTCBPosition : public RCKTCBPositionController {
public:
    VxVector *Tangents() const { return m_Tangents; }
    void RecordLayout() {
#define TCB_FIELD(field) \
        ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(reinterpret_cast<const char *>(&field) - reinterpret_cast<const char *>(this))); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(field)))
        TCB_FIELD(m_Type); TCB_FIELD(m_NbKeys); TCB_FIELD(m_Length); TCB_FIELD(m_Keys); TCB_FIELD(m_Tangents);
#undef TCB_FIELD
        ::testing::Test::RecordProperty("controller_size", static_cast<int>(sizeof(RCKTCBPositionController)));
        ::testing::Test::RecordProperty("controller_alignment", static_cast<int>(alignof(RCKTCBPositionController)));
        ::testing::Test::RecordProperty("anim_base_size", static_cast<int>(sizeof(CKAnimController)));
        ::testing::Test::RecordProperty("tcb_position_key_size", static_cast<int>(sizeof(CKTCBPositionKey)));
        ::testing::Test::RecordProperty("tcb_position_key_alignment", static_cast<int>(alignof(CKTCBPositionKey)));
        CKTCBPositionKey key;
#define KEY_FIELD(field) \
        ::testing::Test::RecordProperty("key_" #field "_offset", static_cast<int>(reinterpret_cast<const char *>(&key.field) - reinterpret_cast<const char *>(&key))); \
        ::testing::Test::RecordProperty("key_" #field "_size", static_cast<int>(sizeof(key.field)))
        KEY_FIELD(TimeStep); KEY_FIELD(Pos); KEY_FIELD(tension); KEY_FIELD(continuity);
        KEY_FIELD(bias); KEY_FIELD(easeto); KEY_FIELD(easefrom);
#undef KEY_FIELD
    }
};

class DeletedTCBPosition : public InspectTCBPosition {
public:
    explicit DeletedTCBPosition(int *destroyed) : Destroyed(destroyed) {}
    ~DeletedTCBPosition() override { ++*Destroyed; }
private:
    int *Destroyed;
};

class CustomTCBPosition : public InspectTCBPosition {
public:
    int Calls = 0;
    // No override specifier, so the same behavioral regression can run on
    // the frozen pre-fix class where this slot was mistakenly nonvirtual.
    void ComputeTangents() {
        ++Calls;
        RCKTCBPositionController::ComputeTangents();
        if (m_Tangents && m_NbKeys == 2)
            m_Tangents[1].Set(0.0f, 0.0f, 0.0f);
    }
};

class TCBPositionLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override { SetProcessorSpecific_FunctionsPtr(); }
    void TearDown() override {
        FailNextKeyArray = false; ObservedKeys = nullptr; ObservedTangents = nullptr;
        KeyReleases = 0; TangentReleases = 0;
    }
    static void Vec(const VxVector &value, const VxVector &expected) {
        EXPECT_NEAR(expected.x, value.x, 2.0e-6f);
        EXPECT_NEAR(expected.y, value.y, 2.0e-6f);
        EXPECT_NEAR(expected.z, value.z, 2.0e-6f);
    }
    static void Author(CKAnimController &controller) {
        VxVector first(2.0f, 4.0f, 6.0f), last(10.0f, 8.0f, 2.0f);
        CKTCBPositionKey a(2.0f, first), b(6.0f, last);
        ASSERT_EQ(0, controller.AddKey(&a)); ASSERT_EQ(1, controller.AddKey(&b));
        controller.SetLength(12.0f);
    }
    static void Consume(CKAnimController &controller) {
        EXPECT_EQ(CKANIMATION_TCBPOS_CONTROL, controller.GetType());
        ASSERT_EQ(2, controller.GetKeyCount()); EXPECT_FLOAT_EQ(12.0f, controller.GetLength());
        VxVector result; ASSERT_TRUE(controller.Evaluate(3.0f, &result)); Vec(result, VxVector(4.0f, 5.0f, 5.0f));
        const float expected[] = {2.0f, 2.0f, 4.0f, 6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  6.0f, 10.0f, 8.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        unsigned char wire[80]; std::memset(wire, 0xa5, sizeof(wire));
        ASSERT_EQ(76, controller.DumpKeysTo(nullptr)); ASSERT_EQ(76, controller.DumpKeysTo(wire));
        int count = -1; std::memcpy(&count, wire, sizeof(count)); EXPECT_EQ(2, count);
        EXPECT_EQ(0, std::memcmp(expected, wire + 4, sizeof(expected)));
        for (int i = 76; i < 80; ++i) EXPECT_EQ(0xa5, wire[i]);
    }
    static void Observe(InspectTCBPosition &controller) {
        ObservedKeys = controller.GetKey(0); ObservedTangents = controller.Tangents();
        KeyReleases = 0; TangentReleases = 0;
    }
};
}

// The hooks count releases of two exact owned arrays, not unrelated test
// allocations. The nothrow hook also reaches the real Clone failure branch.
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

TEST_F(TCBPositionLifecycleTest, ConstructorCreatesEmptyUsableControllerWithoutKeysOrCache) {
    InspectTCBPosition controller;
    EXPECT_EQ(CKANIMATION_TCBPOS_CONTROL, controller.GetType()); EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_FLOAT_EQ(0.0f, controller.GetLength()); EXPECT_EQ(nullptr, controller.GetKey(0)); EXPECT_EQ(nullptr, controller.Tangents());
    VxVector untouched(70.0f, 80.0f, 90.0f); EXPECT_FALSE(controller.Evaluate(0.0f, &untouched)); Vec(untouched, VxVector(70.0f, 80.0f, 90.0f));
    int empty = 123; ASSERT_EQ(4, controller.DumpKeysTo(&empty)); EXPECT_EQ(0, empty);
    controller.ComputeTangents(); EXPECT_EQ(nullptr, controller.Tangents()); Author(controller); Consume(controller);
}

TEST_F(TCBPositionLifecycleTest, PortableLayoutReportsOwnedKeyAndTangentPointersAndAllWireFields) {
    InspectTCBPosition controller; controller.RecordLayout();
    EXPECT_EQ(sizeof(void *) == 8 ? 40u : 24u, sizeof(RCKTCBPositionController));
    EXPECT_EQ(sizeof(void *), alignof(RCKTCBPositionController));
    EXPECT_EQ(sizeof(void *) == 8 ? 24u : 16u, sizeof(CKAnimController));
    EXPECT_EQ(36u, sizeof(CKTCBPositionKey)); EXPECT_EQ(4u, alignof(CKTCBPositionKey));
    Author(controller); Consume(controller);
}

TEST_F(TCBPositionLifecycleTest, FactorySuppliesTCBTypeAndOwnerLength) {
    CKKeyframeData owner; owner.m_Length = 48.0f;
    owner.m_PositionController = owner.CreateController(CKANIMATION_TCBPOS_CONTROL);
    ASSERT_NE(nullptr, owner.m_PositionController);
    EXPECT_EQ(CKANIMATION_TCBPOS_CONTROL, owner.m_PositionController->GetType());
    EXPECT_FLOAT_EQ(48.0f, owner.m_PositionController->GetLength());
    EXPECT_EQ(0, owner.m_PositionController->GetKeyCount()); Author(*owner.m_PositionController); Consume(*owner.m_PositionController);
}

TEST_F(TCBPositionLifecycleTest, GetKeyBorrowsWritablePayloadAndExplicitRecomputeRefreshesInterior) {
    InspectTCBPosition controller; Author(controller); Consume(controller);
    CKTCBPositionKey *first = static_cast<CKTCBPositionKey *>(controller.GetKey(0));
    CKTCBPositionKey *last = static_cast<CKTCBPositionKey *>(controller.GetKey(1));
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, last);
    EXPECT_EQ(sizeof(CKTCBPositionKey), reinterpret_cast<const char *>(last) - reinterpret_cast<const char *>(first));
    VxVector *cache = controller.Tangents(); ASSERT_NE(nullptr, cache);
    last->Pos.Set(18.0f, 12.0f, -2.0f);
    EXPECT_EQ(cache, controller.Tangents());
    VxVector output; ASSERT_TRUE(controller.Evaluate(6.0f, &output)); Vec(output, last->Pos);
    float wire[19] = {}; ASSERT_EQ(76, controller.DumpKeysTo(wire));
    EXPECT_FLOAT_EQ(18.0f, wire[11]); EXPECT_FLOAT_EQ(12.0f, wire[12]); EXPECT_FLOAT_EQ(-2.0f, wire[13]);
    // Native GetKey is a borrowed pointer: authoring clients explicitly refresh
    // tangents after direct writes. Reading a key is not an invalidation event.
    controller.ComputeTangents(); ASSERT_TRUE(controller.Evaluate(3.0f, &output)); Vec(output, VxVector(6.0f, 6.0f, 4.0f));
}

TEST_F(TCBPositionLifecycleTest, GetKeyOutsideBorrowedRangeRetainsCacheAndUsableKeys) {
    InspectTCBPosition controller; Author(controller); Consume(controller);
    CKKey *key = controller.GetKey(0); VxVector *cache = controller.Tangents();
    EXPECT_EQ(nullptr, controller.GetKey(-1)); EXPECT_EQ(nullptr, controller.GetKey(2)); EXPECT_EQ(nullptr, controller.GetKey(INT_MAX));
    EXPECT_EQ(key, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents()); Consume(controller);
}

TEST_F(TCBPositionLifecycleTest, CloneCopiesAllTCBFieldsAndRebuildsIndependentCache) {
    InspectTCBPosition source, destination; Author(source); Author(destination); Consume(source); Consume(destination);
    CKTCBPositionKey *first = static_cast<CKTCBPositionKey *>(source.GetKey(0));
    first->tension = 1.0f; first->continuity = -0.25f; first->bias = 0.75f; first->easeto = 0.125f; first->easefrom = 0.0f;
    source.ComputeTangents(); CKKey *sourceKey = source.GetKey(0); VxVector *sourceCache = source.Tangents();
    Observe(destination); ASSERT_TRUE(destination.Clone(&source));
    EXPECT_EQ(1, KeyReleases); EXPECT_EQ(1, TangentReleases); ObservedKeys = nullptr; ObservedTangents = nullptr;
    EXPECT_EQ(nullptr, destination.Tangents()); EXPECT_NE(sourceKey, destination.GetKey(0));
    CKTCBPositionKey *copy = static_cast<CKTCBPositionKey *>(destination.GetKey(0));
    EXPECT_FLOAT_EQ(1.0f, copy->tension); EXPECT_FLOAT_EQ(-0.25f, copy->continuity); EXPECT_FLOAT_EQ(0.75f, copy->bias);
    EXPECT_FLOAT_EQ(0.125f, copy->easeto); EXPECT_FLOAT_EQ(0.0f, copy->easefrom);
    VxVector sample; ASSERT_TRUE(destination.Evaluate(3.0f, &sample));
    // Two-key incoming tangent is delta, outgoing is zero when tension=1.
    Vec(sample, VxVector(2.875f, 4.4375f, 5.5625f));
    ASSERT_NE(nullptr, destination.Tangents()); EXPECT_NE(sourceCache, destination.Tangents());
    first->Pos.Set(50.0f, 60.0f, 70.0f); source.RemoveKey(1); source.SetLength(99.0f);
    ASSERT_TRUE(destination.Evaluate(3.0f, &sample)); Vec(sample, VxVector(2.875f, 4.4375f, 5.5625f));
    EXPECT_FLOAT_EQ(12.0f, destination.GetLength()); EXPECT_EQ(2, destination.GetKeyCount());
}

TEST_F(TCBPositionLifecycleTest, CloneOwnsKeysAfterPopulatedSourceCacheAndSourceDestruction) {
    InspectTCBPosition destination; Author(destination); Consume(destination);
    { InspectTCBPosition source; Author(source); Consume(source); ASSERT_TRUE(destination.Clone(&source));
      ASSERT_NE(source.GetKey(0), destination.GetKey(0)); EXPECT_EQ(nullptr, destination.Tangents()); }
    Consume(destination);
}

TEST_F(TCBPositionLifecycleTest, CloneEmptySourceReleasesBothArraysAndCopiesLength) {
    InspectTCBPosition destination; Author(destination); Consume(destination);
    InspectTCBPosition source; source.SetLength(37.0f); Observe(destination);
    ASSERT_TRUE(destination.Clone(&source)); EXPECT_EQ(1, KeyReleases); EXPECT_EQ(1, TangentReleases);
    ObservedKeys = nullptr; ObservedTangents = nullptr;
    EXPECT_EQ(0, destination.GetKeyCount()); EXPECT_EQ(nullptr, destination.GetKey(0)); EXPECT_EQ(nullptr, destination.Tangents());
    EXPECT_FLOAT_EQ(37.0f, destination.GetLength()); Author(destination); Consume(destination);
}

TEST_F(TCBPositionLifecycleTest, SelfClonePreservesKeysAndExistingCacheWithoutAllocating) {
    InspectTCBPosition controller; Author(controller); Consume(controller);
    CKKey *key = controller.GetKey(0); VxVector *cache = controller.Tangents(); Observe(controller);
    FailNextKeyArray = true; const CKBOOL cloned = controller.Clone(&controller);
    const bool allocated = !FailNextKeyArray; FailNextKeyArray = false;
    EXPECT_TRUE(cloned); EXPECT_FALSE(allocated); EXPECT_EQ(key, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents());
    EXPECT_EQ(0, KeyReleases); EXPECT_EQ(0, TangentReleases); Consume(controller);
}

TEST_F(TCBPositionLifecycleTest, CloneRejectsNullAndSameSizeDifferentCategoryWithoutChangingCache) {
    InspectTCBPosition controller; Author(controller); Consume(controller);
    RCKTCBScaleController scale; Author(scale);
    CKKey *key = controller.GetKey(0); VxVector *cache = controller.Tangents(); Observe(controller);
    EXPECT_FALSE(controller.Clone(nullptr)); EXPECT_FALSE(controller.Clone(&scale));
    EXPECT_EQ(key, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents());
    EXPECT_EQ(0, KeyReleases); EXPECT_EQ(0, TangentReleases); Consume(controller);
}

TEST_F(TCBPositionLifecycleTest, CloneAllocationFailureRetainsDestinationKeysCacheAndLength) {
    InspectTCBPosition destination, source; Author(destination); Consume(destination);
    VxVector position(99.0f, 88.0f, 77.0f); CKTCBPositionKey key(7.0f, position, 0.5f, -0.5f, 0.25f, 0.125f, 0.75f);
    ASSERT_EQ(0, source.AddKey(&key)); source.SetLength(123.0f);
    CKKey *borrowed = destination.GetKey(0); VxVector *cache = destination.Tangents(); Observe(destination);
    FailNextKeyArray = true; const CKBOOL cloned = destination.Clone(&source);
    const bool failureReached = !FailNextKeyArray; FailNextKeyArray = false;
    ASSERT_TRUE(failureReached); EXPECT_FALSE(cloned); EXPECT_EQ(borrowed, destination.GetKey(0)); EXPECT_EQ(cache, destination.Tangents());
    EXPECT_EQ(0, KeyReleases); EXPECT_EQ(0, TangentReleases); Consume(destination);
    EXPECT_EQ(1, source.GetKeyCount()); EXPECT_FLOAT_EQ(123.0f, source.GetLength());
    VxVector output; ASSERT_TRUE(source.Evaluate(7.0f, &output)); Vec(output, position);
    ASSERT_TRUE(destination.Clone(&source)); EXPECT_FLOAT_EQ(123.0f, destination.GetLength());
    EXPECT_EQ(nullptr, destination.Tangents()); ASSERT_TRUE(destination.Evaluate(0.0f, &output)); Vec(output, position);
}

TEST_F(TCBPositionLifecycleTest, VirtualDeletionReleasesOwnedKeysAndTangentsExactlyOnce) {
    int destroyed = 0; DeletedTCBPosition *source = new DeletedTCBPosition(&destroyed); Author(*source); Consume(*source);
    InspectTCBPosition destination; ASSERT_TRUE(destination.Clone(source)); Observe(*source);
    CKAnimController *api = source; delete api;
    EXPECT_EQ(1, destroyed); EXPECT_EQ(1, KeyReleases); EXPECT_EQ(1, TangentReleases);
    ObservedKeys = nullptr; ObservedTangents = nullptr; Consume(destination);
}

TEST_F(TCBPositionLifecycleTest, BaseNineVirtualEntriesComposeThroughTheBaseInterface) {
    CKAnimController *api = new RCKTCBPositionController; Author(*api);
    RCKTCBPositionController copy; EXPECT_TRUE(copy.Clone(api)); EXPECT_TRUE(api->Compare(&copy, 0.0f));
    unsigned char wire[76]; ASSERT_EQ(76, api->DumpKeysTo(wire)); api->RemoveKey(0);
    EXPECT_EQ(1, api->GetKeyCount()); ASSERT_EQ(76, api->ReadKeysFrom(wire));
    EXPECT_FLOAT_EQ(2.0f, api->GetKey(0)->GetTime()); Consume(*api); delete api;
}

TEST_F(TCBPositionLifecycleTest, TenthVirtualSlotCustomizesTangentsThroughEvaluateAndConcreteBase) {
    CustomTCBPosition controller; CKAnimController *api = &controller; Author(*api);
    VxVector output; ASSERT_TRUE(api->Evaluate(3.0f, &output));
    EXPECT_EQ(1, controller.Calls); Vec(output, VxVector(2.875f, 4.4375f, 5.5625f));
    ASSERT_TRUE(api->Evaluate(3.0f, &output)); EXPECT_EQ(1, controller.Calls);
    RCKTCBPositionController *concreteBase = &controller;
    concreteBase->ComputeTangents(); EXPECT_EQ(2, controller.Calls);
    ASSERT_TRUE(api->Evaluate(3.0f, &output)); Vec(output, VxVector(2.875f, 4.4375f, 5.5625f));
}

TEST_F(TCBPositionLifecycleTest, ClonedTCBControllerDrivesEntityAfterOriginalOwnerAndCacheDie) {
    ASSERT_EQ(CK_OK, CKStartUp());
    CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
    CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
    CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_ANIMATION);
    CKBuildClassHierarchyTable();
    CKContext *context = nullptr; ASSERT_EQ(CK_OK, CKCreateContext(&context, nullptr, 0, 0));
    {
        RCK3dEntity entity(context); RCKObjectAnimation animation(context);
        animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); animation.SetLength(12.0f);
        CKAnimController *destination = animation.CreateController(CKANIMATION_TCBPOS_CONTROL); ASSERT_NE(nullptr, destination);
        { CKKeyframeData owner; owner.m_PositionController = owner.CreateController(CKANIMATION_TCBPOS_CONTROL);
          ASSERT_NE(nullptr, owner.m_PositionController); Author(*owner.m_PositionController); Consume(*owner.m_PositionController);
          ASSERT_TRUE(destination->Clone(owner.m_PositionController)); }
        ASSERT_EQ(CK_OK, animation.SetFrame(3.0f));
        const VxMatrix &first = entity.GetLocalMatrix(); Vec(VxVector(first[3][0], first[3][1], first[3][2]), VxVector(4.0f, 5.0f, 5.0f));
        static_cast<CKTCBPositionKey *>(destination->GetKey(1))->Pos.Set(18.0f, 12.0f, -2.0f);
        static_cast<RCKTCBPositionController *>(destination)->ComputeTangents();
        ASSERT_EQ(CK_OK, animation.SetFrame(3.0f));
        const VxMatrix &updated = entity.GetLocalMatrix(); Vec(VxVector(updated[3][0], updated[3][1], updated[3][2]), VxVector(6.0f, 6.0f, 4.0f));
        animation.Set3dEntity(nullptr);
    }
    CKCloseContext(context); CKShutdown();
}

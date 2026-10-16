#include <gtest/gtest.h>
#include <climits>
#include <cstring>
#include <new>
#include "CKAll.h"
#include "RCKKeyframeData.h"
#include "RCKObjectAnimation.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"

extern void SetProcessorSpecific_FunctionsPtr();
namespace {
thread_local bool FailBezierKeys = false;
thread_local void *ObservedBezierKeys = nullptr;
thread_local int BezierKeyReleases = 0;

class InspectBezierScale : public RCKBezierScaleController {
public:
    CKBOOL Computed() const { return m_TangentsComputed; }
    void Layout() {
#define FIELD(field) \
        ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(reinterpret_cast<const char *>(&field) - reinterpret_cast<const char *>(this))); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(field)))
        FIELD(m_Type); FIELD(m_NbKeys); FIELD(m_Length); FIELD(m_Keys); FIELD(m_TangentsComputed);
#undef FIELD
        ::testing::Test::RecordProperty("controller_size", static_cast<int>(sizeof(RCKBezierScaleController)));
        ::testing::Test::RecordProperty("controller_alignment", static_cast<int>(alignof(RCKBezierScaleController)));
        ::testing::Test::RecordProperty("anim_base_size", static_cast<int>(sizeof(CKAnimController)));
        ::testing::Test::RecordProperty("bezier_scale_key_size", static_cast<int>(sizeof(CKBezierScaleKey)));
        ::testing::Test::RecordProperty("bezier_scale_key_alignment", static_cast<int>(alignof(CKBezierScaleKey)));
        CKBezierScaleKey key;
#define FIELD(field) \
        ::testing::Test::RecordProperty("key_" #field "_offset", static_cast<int>(reinterpret_cast<const char *>(&key.field) - reinterpret_cast<const char *>(&key))); \
        ::testing::Test::RecordProperty("key_" #field "_size", static_cast<int>(sizeof(key.field)))
        FIELD(TimeStep); FIELD(Pos); FIELD(Flags); FIELD(In); FIELD(Out);
#undef FIELD
    }
};

class DeletedBezierScale : public InspectBezierScale {
public:
    explicit DeletedBezierScale(int *destroyed) : Destroyed(destroyed) {}
    ~DeletedBezierScale() override { ++*Destroyed; }
private:
    int *Destroyed;
};

class CustomFullBezierScale : public InspectBezierScale {
public:
    int Calls = 0;
    // Same behavioral fixture compiles before the missing virtual is restored.
    void ComputeBezierPts() {
        ++Calls; RCKBezierScaleController::ComputeBezierPts();
        if (m_NbKeys > 0) m_Keys[0].Out.Set(0, 0, 0);
    }
};

class CustomIndexedBezierScale : public InspectBezierScale {
public:
    int Calls = 0;
    void ComputeBezierPts(int index) {
        ++Calls; RCKBezierScaleController::ComputeBezierPts(index);
        if (index == 0) m_Keys[0].Out.Set(4, 0, 0);
    }
};

class BezierScaleLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override { SetProcessorSpecific_FunctionsPtr(); }
    void TearDown() override { FailBezierKeys = false; ObservedBezierKeys = nullptr; BezierKeyReleases = 0; }
    static void Vec(const VxVector &actual, float x, float y, float z) {
        EXPECT_NEAR(x, actual.x, .00001f); EXPECT_NEAR(y, actual.y, .00001f); EXPECT_NEAR(z, actual.z, .00001f);
    }
    static void Author(CKAnimController &controller, CKBEZIERKEY_FLAGS mode = BEZIER_KEY_LINEAR) {
        CKBezierScaleKey a, b;
        a.TimeStep = 2; a.Pos.Set(2, 4, 6); a.In = a.Out = VxVector(0, 0, 0);
        b.TimeStep = 6; b.Pos.Set(10, 8, 2); b.In = b.Out = VxVector(0, 0, 0);
        a.Flags.SetInTangentMode(mode); a.Flags.SetOutTangentMode(mode);
        b.Flags.SetInTangentMode(mode); b.Flags.SetOutTangentMode(mode);
        ASSERT_EQ(0, controller.AddKey(&a)); ASSERT_EQ(1, controller.AddKey(&b)); controller.SetLength(12);
    }
    static void Consume(CKAnimController &controller) {
        EXPECT_EQ(CKANIMATION_BEZIERSCL_CONTROL, controller.GetType()); EXPECT_EQ(2, controller.GetKeyCount());
        VxVector output; ASSERT_TRUE(controller.Evaluate(3, &output)); Vec(output, 4, 5, 5);
        EXPECT_FLOAT_EQ(12, controller.GetLength());
    }
};
}

void *operator new[](size_t size, const std::nothrow_t &) noexcept {
    if (FailBezierKeys) { FailBezierKeys = false; return nullptr; }
    try { return ::operator new[](size); } catch (const std::bad_alloc &) { return nullptr; }
}
void operator delete[](void *pointer) noexcept {
    if (pointer && pointer == ObservedBezierKeys) ++BezierKeyReleases;
    ::operator delete(pointer);
}
void operator delete[](void *pointer, size_t) noexcept { ::operator delete[](pointer); }

TEST_F(BezierScaleLifecycleTest, ConstructorAndMeasuredLayoutDefineThePortableOwnershipContract) {
    InspectBezierScale controller; controller.Layout();
    EXPECT_EQ(sizeof(void *) == 8 ? 40u : 24u, sizeof(RCKBezierScaleController));
    EXPECT_EQ(sizeof(void *), alignof(RCKBezierScaleController)); EXPECT_EQ(44u, sizeof(CKBezierScaleKey));
    EXPECT_EQ(4u, alignof(CKBezierScaleKey)); EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_FLOAT_EQ(0, controller.GetLength()); EXPECT_EQ(nullptr, controller.GetKey(0)); EXPECT_FALSE(controller.Computed());
    VxVector output(70, 80, 90); EXPECT_FALSE(controller.Evaluate(0, &output)); Vec(output, 70, 80, 90);
    controller.ComputeBezierPts(); EXPECT_TRUE(controller.Computed());
    Author(controller); EXPECT_FALSE(controller.Computed()); Consume(controller);
}

TEST_F(BezierScaleLifecycleTest, FactoryCreatesTheScaleCategoryAndSuppliesTheOwnerLength) {
    CKKeyframeData owner; owner.m_Length = 48;
    owner.m_ScaleController = owner.CreateController(CKANIMATION_BEZIERSCL_CONTROL);
    ASSERT_NE(nullptr, owner.m_ScaleController); EXPECT_FLOAT_EQ(48, owner.m_ScaleController->GetLength());
    EXPECT_EQ(CKANIMATION_BEZIERSCL_CONTROL, owner.m_ScaleController->GetType());
    Author(*owner.m_ScaleController); Consume(*owner.m_ScaleController);
}

TEST_F(BezierScaleLifecycleTest, BorrowedKeyEditsUseTheExistingCacheUntilExplicitRecomputation) {
    InspectBezierScale controller; Author(controller); Consume(controller); EXPECT_TRUE(controller.Computed());
    CKBezierScaleKey *a = static_cast<CKBezierScaleKey *>(controller.GetKey(0));
    CKBezierScaleKey *b = static_cast<CKBezierScaleKey *>(controller.GetKey(1));
    EXPECT_EQ(44, reinterpret_cast<const char *>(b) - reinterpret_cast<const char *>(a));
    b->Pos.Set(18, 12, -2); EXPECT_TRUE(controller.Computed());
    EXPECT_EQ(nullptr, controller.GetKey(-1)); EXPECT_EQ(nullptr, controller.GetKey(2)); EXPECT_EQ(nullptr, controller.GetKey(INT_MAX));
    controller.ComputeBezierPts(); VxVector output; ASSERT_TRUE(controller.Evaluate(3, &output)); Vec(output, 6, 6, 4);
}

TEST_F(BezierScaleLifecycleTest, CloneOwnsAllFieldsAndInvalidatesItsComputedDerivatives) {
    InspectBezierScale source, destination; Author(source); Author(destination); Consume(source); Consume(destination);
    CKBezierScaleKey *key = static_cast<CKBezierScaleKey *>(source.GetKey(0));
    key->Flags.SetInTangentMode(BEZIER_KEY_TANGENTS); key->Flags.SetOutTangentMode(BEZIER_KEY_TANGENTS);
    key->In.Set(-1, 2, 3); key->Out.Set(4, 0, 0);
    source.ComputeBezierPts(); ObservedBezierKeys = destination.GetKey(0); BezierKeyReleases = 0;
    ASSERT_TRUE(destination.Clone(&source)); EXPECT_EQ(1, BezierKeyReleases); ObservedBezierKeys = nullptr;
    EXPECT_FALSE(destination.Computed()); ASSERT_NE(source.GetKey(0), destination.GetKey(0));
    EXPECT_EQ(0, std::memcmp(source.GetKey(0), destination.GetKey(0), sizeof(CKBezierScaleKey)));
    VxVector output; ASSERT_TRUE(destination.Evaluate(3, &output)); Vec(output, 5.125f, 4.4375f, 5.5625f);
    key->Pos.Set(90, 80, 70); source.RemoveKey(1); source.SetLength(99);
    ASSERT_TRUE(destination.Evaluate(3, &output)); Vec(output, 5.125f, 4.4375f, 5.5625f);
    EXPECT_FLOAT_EQ(12, destination.GetLength());
}

TEST_F(BezierScaleLifecycleTest, CloneSurvivesSourceDestructionAndEmptyCloneReleasesItsOwnedArray) {
    InspectBezierScale destination;
    { InspectBezierScale source; Author(source); Consume(source); ASSERT_TRUE(destination.Clone(&source)); }
    Consume(destination); InspectBezierScale empty; empty.SetLength(37);
    ObservedBezierKeys = destination.GetKey(0); BezierKeyReleases = 0;
    ASSERT_TRUE(destination.Clone(&empty)); EXPECT_EQ(1, BezierKeyReleases); ObservedBezierKeys = nullptr;
    EXPECT_EQ(0, destination.GetKeyCount()); EXPECT_EQ(nullptr, destination.GetKey(0));
    EXPECT_FALSE(destination.Computed()); EXPECT_FLOAT_EQ(37, destination.GetLength()); Author(destination); Consume(destination);
}

TEST_F(BezierScaleLifecycleTest, SelfClonePreservesKeysLengthAndComputedStateWithoutAllocation) {
    InspectBezierScale controller; Author(controller); Consume(controller); CKKey *key = controller.GetKey(0);
    FailBezierKeys = true; const CKBOOL copied = controller.Clone(&controller); const bool untouched = FailBezierKeys; FailBezierKeys = false;
    EXPECT_TRUE(copied); EXPECT_TRUE(untouched); EXPECT_EQ(key, controller.GetKey(0)); EXPECT_TRUE(controller.Computed()); Consume(controller);
}

TEST_F(BezierScaleLifecycleTest, CloneRejectsNullAndTheSameSizedPositionCategory) {
    InspectBezierScale controller; Author(controller); Consume(controller); RCKBezierPositionController position; Author(position);
    CKKey *key = controller.GetKey(0); EXPECT_FALSE(controller.Clone(nullptr)); EXPECT_FALSE(controller.Clone(&position));
    EXPECT_EQ(key, controller.GetKey(0)); EXPECT_TRUE(controller.Computed()); Consume(controller);
}

TEST_F(BezierScaleLifecycleTest, CloneAllocationFailureRetainsEveryObservableDestinationField) {
    InspectBezierScale source, destination; Author(source); Author(destination); Consume(destination);
    static_cast<CKBezierScaleKey *>(source.GetKey(0))->Pos.Set(90, 80, 70); source.SetLength(99);
    CKKey *key = destination.GetKey(0); ObservedBezierKeys = key; BezierKeyReleases = 0;
    FailBezierKeys = true; const CKBOOL copied = destination.Clone(&source); const bool failed = !FailBezierKeys; FailBezierKeys = false;
    EXPECT_FALSE(copied); EXPECT_TRUE(failed); EXPECT_EQ(0, BezierKeyReleases); ObservedBezierKeys = nullptr;
    EXPECT_EQ(key, destination.GetKey(0)); EXPECT_TRUE(destination.Computed()); Consume(destination);
}

TEST_F(BezierScaleLifecycleTest, VirtualDeletionReleasesTheExactOwnedArrayOnce) {
    int destroyed = 0; DeletedBezierScale *controller = new DeletedBezierScale(&destroyed); Author(*controller); Consume(*controller);
    ObservedBezierKeys = controller->GetKey(0); BezierKeyReleases = 0; CKAnimController *api = controller; delete api;
    EXPECT_EQ(1, destroyed); EXPECT_EQ(1, BezierKeyReleases); ObservedBezierKeys = nullptr;
}

TEST_F(BezierScaleLifecycleTest, BaseNineVirtualEntriesComposeTheSameUsableController) {
    CKAnimController *api = new RCKBezierScaleController; Author(*api); RCKBezierScaleController copy;
    ASSERT_TRUE(copy.Clone(api)); EXPECT_TRUE(api->Compare(&copy)); unsigned char wire[44]; ASSERT_EQ(44, api->DumpKeysTo(wire));
    api->RemoveKey(0); ASSERT_EQ(1, api->GetKeyCount()); ASSERT_EQ(44, api->ReadKeysFrom(wire)); Consume(*api); delete api;
}

TEST_F(BezierScaleLifecycleTest, TenthVirtualSlotCustomizesFullComputationThroughEvaluateAndConcreteBase) {
    CustomFullBezierScale controller; Author(controller); CKAnimController *api = &controller;
    VxVector output; ASSERT_TRUE(api->Evaluate(3, &output)); EXPECT_EQ(1, controller.Calls); Vec(output, 2.875f, 4.4375f, 5.5625f);
    ASSERT_TRUE(api->Evaluate(3, &output)); EXPECT_EQ(1, controller.Calls);
    RCKBezierScaleController *base = &controller; base->ComputeBezierPts(); EXPECT_EQ(2, controller.Calls);
}

TEST_F(BezierScaleLifecycleTest, EleventhVirtualSlotCustomizesTheFullSmoothPassAndDirectIndexedCall) {
    CustomIndexedBezierScale controller; Author(controller, BEZIER_KEY_AUTOSMOOTH); CKAnimController *api = &controller;
    VxVector output; ASSERT_TRUE(api->Evaluate(3, &output)); EXPECT_EQ(2, controller.Calls); Vec(output, 5.125f, 4.4375f, 5.5625f);
    RCKBezierScaleController *base = &controller; base->ComputeBezierPts(0); EXPECT_EQ(3, controller.Calls);
    ASSERT_TRUE(api->Evaluate(3, &output)); Vec(output, 5.125f, 4.4375f, 5.5625f);
}

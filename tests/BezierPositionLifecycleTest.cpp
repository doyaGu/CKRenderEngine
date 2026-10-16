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
thread_local bool FailNextArray = false;
thread_local void *ObservedKeys = nullptr;
thread_local int KeyReleases = 0;
class InspectBezierPosition : public RCKBezierPositionController {
public:
    bool Ready() const { return m_TangentsComputed != FALSE; }
    void RecordLayout() {
#define FIELD(f) ::testing::Test::RecordProperty(#f "_offset", static_cast<int>(reinterpret_cast<const char *>(&f)-reinterpret_cast<const char *>(this))); ::testing::Test::RecordProperty(#f "_size", static_cast<int>(sizeof(f)))
        FIELD(m_Type); FIELD(m_NbKeys); FIELD(m_Length); FIELD(m_Keys); FIELD(m_TangentsComputed);
#undef FIELD
        ::testing::Test::RecordProperty("controller_size", static_cast<int>(sizeof(RCKBezierPositionController)));
        ::testing::Test::RecordProperty("controller_alignment", static_cast<int>(alignof(RCKBezierPositionController)));
        ::testing::Test::RecordProperty("base_size", static_cast<int>(sizeof(CKAnimController)));
        CKBezierPositionKey key;
#define KEY_FIELD(f) ::testing::Test::RecordProperty("key_" #f "_offset", static_cast<int>(reinterpret_cast<const char *>(&key.f)-reinterpret_cast<const char *>(&key))); ::testing::Test::RecordProperty("key_" #f "_size", static_cast<int>(sizeof(key.f)))
        KEY_FIELD(TimeStep); KEY_FIELD(Pos); KEY_FIELD(Flags); KEY_FIELD(In); KEY_FIELD(Out);
#undef KEY_FIELD
    }
};
class DeletedBezierPosition : public InspectBezierPosition {
public:
    explicit DeletedBezierPosition(int *destroyed) : Destroyed(destroyed) {}
    ~DeletedBezierPosition() override { ++*Destroyed; }
private: int *Destroyed;
};
class CustomAllPosition : public InspectBezierPosition {
public:
    int Calls = 0;
    // Same behavioral regression compiles before the missing virtual is fixed.
    void ComputeBezierPts() {
        ++Calls; RCKBezierPositionController::ComputeBezierPts();
        if (m_NbKeys == 2) m_Keys[0].Out.Set(0, 0, 0);
    }
};
class CustomIndexedPosition : public InspectBezierPosition {
public:
    int Calls = 0;
    void ComputeBezierPts(int index) {
        ++Calls; RCKBezierPositionController::ComputeBezierPts(index);
        if (index == 0 && m_NbKeys == 2) m_Keys[0].Out.Set(0, 0, 0);
    }
};
void Add(CKAnimController &controller, float time, VxVector position, bool explicitTangents = true) {
    CKBezierKeyFlags flags;
    flags.SetInTangentMode(explicitTangents ? BEZIER_KEY_TANGENTS : BEZIER_KEY_AUTOSMOOTH);
    flags.SetOutTangentMode(explicitTangents ? BEZIER_KEY_TANGENTS : BEZIER_KEY_AUTOSMOOTH);
    VxVector in(time == 0 ? VxVector(4, 5, 6) : VxVector(0, 0, 0));
    VxVector out(time == 0 ? VxVector(2, 2, 2) : VxVector(7, 8, 9));
    CKBezierPositionKey key(time, position, flags, in, out); ASSERT_GE(controller.AddKey(&key), 0);
}
void Author(CKAnimController &controller) {
    Add(controller, 0, VxVector(2, 4, 6)); Add(controller, 8, VxVector(10, 12, 14)); controller.SetLength(12);
}
void Vec(const VxVector &value, float x, float y = 0, float z = 0) {
    EXPECT_NEAR(x, value.x, 2.0e-6f); EXPECT_NEAR(y, value.y, 2.0e-6f); EXPECT_NEAR(z, value.z, 2.0e-6f);
}
void Consume(CKAnimController &controller) {
    EXPECT_EQ(CKANIMATION_BEZIERPOS_CONTROL, controller.GetType()); ASSERT_EQ(2, controller.GetKeyCount()); EXPECT_FLOAT_EQ(12, controller.GetLength());
    VxVector output; ASSERT_TRUE(controller.Evaluate(2, &output)); Vec(output, 5.5f, 7.5f, 9.5f);
    unsigned char wire[96]; std::memset(wire, 0xa5, sizeof(wire)); ASSERT_EQ(92, controller.DumpKeysTo(nullptr)); ASSERT_EQ(92, controller.DumpKeysTo(wire));
    EXPECT_EQ(0xa5, wire[92]); EXPECT_EQ(0xa5, wire[95]);
}
void SmoothPair(CKAnimController &controller) { Add(controller, 0, VxVector(0, 0, 0), false); Add(controller, 8, VxVector(8, 0, 0), false); }
class BezierPositionLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override { SetProcessorSpecific_FunctionsPtr(); }
    void TearDown() override { FailNextArray = false; ObservedKeys = nullptr; KeyReleases = 0; }
};
}
void *operator new[](size_t bytes, const std::nothrow_t &) noexcept {
    if (FailNextArray) { FailNextArray = false; return nullptr; }
    try { return ::operator new[](bytes); } catch (const std::bad_alloc &) { return nullptr; }
}
void operator delete[](void *pointer) noexcept { if (pointer && pointer == ObservedKeys) ++KeyReleases; ::operator delete(pointer); }
void operator delete[](void *pointer, size_t) noexcept { if (pointer && pointer == ObservedKeys) ++KeyReleases; ::operator delete(pointer); }
namespace {
TEST_F(BezierPositionLifecycleTest, ConstructorCreatesEmptyUsableControllerAndColdDerivativeCache) {
    InspectBezierPosition controller; EXPECT_EQ(CKANIMATION_BEZIERPOS_CONTROL, controller.GetType()); EXPECT_EQ(0, controller.GetKeyCount()); EXPECT_FLOAT_EQ(0, controller.GetLength()); EXPECT_FALSE(controller.Ready());
    EXPECT_EQ(nullptr, controller.GetKey(0)); int count = 7; ASSERT_EQ(4, controller.DumpKeysTo(&count)); EXPECT_EQ(0, count);
    controller.ComputeBezierPts(); EXPECT_TRUE(controller.Ready()); Author(controller); EXPECT_FALSE(controller.Ready()); Consume(controller);
}
TEST_F(BezierPositionLifecycleTest, PortableLayoutRecordsPointerBooleanAndAllFortyFourByteKeyFields) {
    InspectBezierPosition controller; controller.RecordLayout(); EXPECT_EQ(sizeof(void *) == 8 ? 40u : 24u, sizeof(RCKBezierPositionController));
    EXPECT_EQ(sizeof(void *), alignof(RCKBezierPositionController)); EXPECT_EQ(sizeof(void *) == 8 ? 24u : 16u, sizeof(CKAnimController));
    EXPECT_EQ(44u, sizeof(CKBezierPositionKey)); EXPECT_EQ(4u, alignof(CKBezierPositionKey)); Author(controller); Consume(controller);
}
TEST_F(BezierPositionLifecycleTest, FactorySuppliesPositionCategoryAndOwnerLength) {
    CKKeyframeData owner; owner.m_Length = 48; owner.m_PositionController = owner.CreateController(CKANIMATION_BEZIERPOS_CONTROL); ASSERT_NE(nullptr, owner.m_PositionController);
    EXPECT_EQ(CKANIMATION_BEZIERPOS_CONTROL, owner.m_PositionController->GetType()); EXPECT_FLOAT_EQ(48, owner.m_PositionController->GetLength()); Author(*owner.m_PositionController); Consume(*owner.m_PositionController);
}
TEST_F(BezierPositionLifecycleTest, GetKeyBorrowsWritableFullKeysWithoutChangingTheComputedFlag) {
    InspectBezierPosition controller; Author(controller); Consume(controller); CKKey *first = controller.GetKey(0), *last = controller.GetKey(1);
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, last); EXPECT_EQ(44, reinterpret_cast<const char *>(last)-reinterpret_cast<const char *>(first)); EXPECT_TRUE(controller.Ready());
    static_cast<CKBezierPositionKey *>(first)->Out.Set(0, 0, 0); VxVector output; ASSERT_TRUE(controller.Evaluate(2, &output)); Vec(output, 3.25f, 5.25f, 7.25f); EXPECT_TRUE(controller.Ready());
    unsigned char wire[92]; ASSERT_EQ(92, controller.DumpKeysTo(wire)); EXPECT_EQ(first, controller.GetKey(0));
}
TEST_F(BezierPositionLifecycleTest, GetKeyOutsideTheBorrowedRangeRetainsKeysAndCachedBehavior) {
    InspectBezierPosition controller; Author(controller); Consume(controller); CKKey *key = controller.GetKey(0);
    EXPECT_EQ(nullptr, controller.GetKey(-1)); EXPECT_EQ(nullptr, controller.GetKey(2)); EXPECT_EQ(nullptr, controller.GetKey(INT_MAX)); EXPECT_EQ(key, controller.GetKey(0)); EXPECT_TRUE(controller.Ready()); Consume(controller);
}
TEST_F(BezierPositionLifecycleTest, CloneCopiesIndependentFullKeysLengthAndInvalidatesDestinationComputedFlag) {
    InspectBezierPosition source, destination; Author(source); Author(destination); Consume(source); Consume(destination);
    CKKey *sourceKey = source.GetKey(0); ObservedKeys = destination.GetKey(0); KeyReleases = 0; ASSERT_TRUE(destination.Clone(&source)); EXPECT_EQ(1, KeyReleases); ObservedKeys = nullptr;
    EXPECT_NE(sourceKey, destination.GetKey(0)); EXPECT_EQ(0, std::memcmp(source.GetKey(0), destination.GetKey(0), 88)); EXPECT_FALSE(destination.Ready()); EXPECT_TRUE(source.Ready());
    Consume(destination); static_cast<CKBezierPositionKey *>(source.GetKey(0))->Out.Set(100, 200, 300); source.RemoveKey(1); source.SetLength(99); Consume(destination);
}
TEST_F(BezierPositionLifecycleTest, CloneOwnsItsKeysAfterSourceDestruction) {
    InspectBezierPosition destination; { InspectBezierPosition source; Author(source); Consume(source); ASSERT_TRUE(destination.Clone(&source)); }
    Consume(destination);
}
TEST_F(BezierPositionLifecycleTest, CloneEmptySourceReleasesOnlyOwnedKeyArrayAndCopiesLength) {
    InspectBezierPosition source, destination; Author(destination); Consume(destination); source.SetLength(37); ObservedKeys = destination.GetKey(0);
    ASSERT_TRUE(destination.Clone(&source)); EXPECT_EQ(1, KeyReleases); ObservedKeys = nullptr; EXPECT_EQ(0, destination.GetKeyCount()); EXPECT_FALSE(destination.Ready()); EXPECT_FLOAT_EQ(37, destination.GetLength());
    Author(destination); Consume(destination);
}
TEST_F(BezierPositionLifecycleTest, SelfCloneDoesNotAllocateReleaseOrInvalidateComputedState) {
    InspectBezierPosition controller; Author(controller); Consume(controller); CKKey *borrowed = controller.GetKey(0); ObservedKeys = borrowed; FailNextArray = true;
    EXPECT_TRUE(controller.Clone(&controller)); EXPECT_TRUE(FailNextArray); FailNextArray = false; EXPECT_EQ(0, KeyReleases); EXPECT_EQ(borrowed, controller.GetKey(0)); EXPECT_TRUE(controller.Ready()); Consume(controller);
}
TEST_F(BezierPositionLifecycleTest, CloneRejectsNullAndAliasedKeyTypeWithDifferentControllerCategory) {
    InspectBezierPosition destination; Author(destination); Consume(destination); RCKBezierScaleController wrong; Author(wrong); CKKey *borrowed = destination.GetKey(0);
    EXPECT_FALSE(destination.Clone(nullptr)); EXPECT_FALSE(destination.Clone(&wrong)); EXPECT_EQ(borrowed, destination.GetKey(0)); EXPECT_TRUE(destination.Ready()); Consume(destination);
}
TEST_F(BezierPositionLifecycleTest, CloneAllocationFailurePreservesKeysLengthAndUsableComputedDerivatives) {
    InspectBezierPosition source, destination; Author(destination); Consume(destination); Add(source, 7, VxVector(99, 88, 77)); source.SetLength(123);
    CKKey *borrowed = destination.GetKey(0); ObservedKeys = borrowed; FailNextArray = true; EXPECT_FALSE(destination.Clone(&source)); EXPECT_FALSE(FailNextArray); EXPECT_EQ(0, KeyReleases);
    EXPECT_EQ(borrowed, destination.GetKey(0)); EXPECT_TRUE(destination.Ready()); Consume(destination); ASSERT_TRUE(destination.Clone(&source)); EXPECT_FALSE(destination.Ready()); EXPECT_FLOAT_EQ(123, destination.GetLength());
}
TEST_F(BezierPositionLifecycleTest, VirtualDeletionReleasesExactlyOneOwnedKeyArrayAndLeavesTheCloneUsable) {
    int destroyed = 0; DeletedBezierPosition *source = new DeletedBezierPosition(&destroyed); Author(*source); Consume(*source); InspectBezierPosition copy; ASSERT_TRUE(copy.Clone(source));
    ObservedKeys = source->GetKey(0); CKAnimController *api = source; delete api; EXPECT_EQ(1, destroyed); EXPECT_EQ(1, KeyReleases); ObservedKeys = nullptr; Consume(copy);
}
TEST_F(BezierPositionLifecycleTest, BaseNineVirtualEntriesComposeThroughTheBaseInterface) {
    CKAnimController *api = new RCKBezierPositionController; Author(*api); RCKBezierPositionController copy; ASSERT_TRUE(copy.Clone(api)); EXPECT_TRUE(api->Compare(&copy));
    unsigned char wire[92]; ASSERT_EQ(92, api->DumpKeysTo(wire)); api->RemoveKey(0); EXPECT_EQ(1, api->GetKeyCount()); ASSERT_EQ(92, api->ReadKeysFrom(wire)); EXPECT_FLOAT_EQ(0, api->GetKey(0)->GetTime()); Consume(*api); delete api;
}
TEST_F(BezierPositionLifecycleTest, TenthVirtualSlotCustomizesTheFullBuildThroughEvaluateAndConcreteBase) {
    CustomAllPosition controller; SmoothPair(controller); CKAnimController *api = &controller; VxVector output;
    ASSERT_TRUE(api->Evaluate(2, &output)); EXPECT_EQ(1, controller.Calls); Vec(output, .875f); ASSERT_TRUE(api->Evaluate(2, &output)); EXPECT_EQ(1, controller.Calls);
    RCKBezierPositionController *base = &controller; base->ComputeBezierPts(); EXPECT_EQ(2, controller.Calls); ASSERT_TRUE(api->Evaluate(2, &output)); Vec(output, .875f);
}
TEST_F(BezierPositionLifecycleTest, EleventhVirtualSlotReceivesEachSmoothKeyDuringFullBuildAndDirectIndexCall) {
    CustomIndexedPosition controller; SmoothPair(controller); RCKBezierPositionController *base = &controller; VxVector output;
    ASSERT_TRUE(base->Evaluate(2, &output)); EXPECT_EQ(2, controller.Calls); Vec(output, .875f); EXPECT_TRUE(controller.Ready());
    base->ComputeBezierPts(0); EXPECT_EQ(3, controller.Calls); ASSERT_TRUE(base->Evaluate(2, &output)); Vec(output, .875f);
}
} // namespace

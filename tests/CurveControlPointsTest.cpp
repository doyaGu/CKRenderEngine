#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKCurve.h"
#include "RCKCurvePoint.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class CurveControlPointsTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCurvePoint, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKCurve, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKCurve *API(RCKCurve &curve) { return reinterpret_cast<CKCurve *>(&curve); }
    static CKCurvePoint *API(RCKCurvePoint &point) { return reinterpret_cast<CKCurvePoint *>(&point); }
    static void ExpectPosition(RCK3dEntity &entity, float x, float y, float z) {
        VxVector position; entity.GetPosition(&position);
        EXPECT_FLOAT_EQ(x, position.x); EXPECT_FLOAT_EQ(y, position.y); EXPECT_FLOAT_EQ(z, position.z);
    }
};
}

TEST_F(CurveControlPointsTest, OpenAndCloseInvalidateOnlyOnStateTransition) {
    RCKCurve curve(Context); curve.ModifyObjectFlags(CK_OBJECT_UPTODATE | CK_OBJECT_PRIVATE, 0);
    const CKDWORD flags = curve.GetObjectFlags(); curve.Open(); EXPECT_EQ(flags, curve.GetObjectFlags());
    curve.Close(); EXPECT_FALSE(curve.IsOpen()); EXPECT_EQ(flags & ~CK_OBJECT_UPTODATE, curve.GetObjectFlags());
    curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); const CKDWORD closed = curve.GetObjectFlags();
    curve.Close(); EXPECT_EQ(closed, curve.GetObjectFlags()); curve.Open(); EXPECT_TRUE(curve.IsOpen());
    EXPECT_EQ(closed & ~CK_OBJECT_UPTODATE, curve.GetObjectFlags());
}
TEST_F(CurveControlPointsTest, FittingCoefficientStoresUnrestrictedValuesAndAlwaysInvalidates) {
    RCKCurve curve(Context); const float values[] = {0.0f, -2.0f, 1.5f, 1.5f};
    for (int i = 0; i < 4; ++i) {
        curve.ModifyObjectFlags(CK_OBJECT_UPTODATE | CK_OBJECT_PRIVATE, 0); const CKDWORD flags = curve.GetObjectFlags();
        curve.SetFittingCoeff(values[i]); EXPECT_EQ(values[i], curve.GetFittingCoeff());
        EXPECT_EQ(flags & ~CK_OBJECT_UPTODATE, curve.GetObjectFlags());
    }
}
TEST_F(CurveControlPointsTest, AddStoresOrderSetsParentAndPreservesWorldPosition) {
    RCKCurve curve(Context); RCKCurvePoint first(Context), second(Context);
    curve.SetPosition3f(10, 20, 30); first.SetPosition3f(2, 3, 4); second.SetPosition3f(-7, 8, 9);
    curve.ModifyObjectFlags(CK_OBJECT_UPTODATE | CK_OBJECT_PRIVATE, 0); const CKDWORD flags = curve.GetObjectFlags();
    ASSERT_EQ(CK_OK, curve.AddControlPoint(API(first))); ASSERT_EQ(CK_OK, curve.AddControlPoint(API(second)));
    EXPECT_EQ(2, curve.GetControlPointCount()); EXPECT_EQ(API(first), curve.GetControlPoint(0));
    EXPECT_EQ(API(second), curve.GetControlPoint(1)); EXPECT_EQ(API(curve), first.GetCurve());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&curve), first.GetParent()); EXPECT_EQ(2, curve.GetChildrenCount());
    ExpectPosition(first, 2, 3, 4); ExpectPosition(second, -7, 8, 9);
    EXPECT_EQ(flags & ~CK_OBJECT_UPTODATE, curve.GetObjectFlags()); first.SetCurve(nullptr); second.SetCurve(nullptr);
}
TEST_F(CurveControlPointsTest, AddNullAndDuplicateDoNotMutateListOwnerOrCache) {
    RCKCurve curve(Context); RCKCurvePoint point(Context); ASSERT_EQ(CK_OK, curve.AddControlPoint(API(point)));
    curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); const CKDWORD flags = curve.GetObjectFlags();
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.AddControlPoint(nullptr));
    EXPECT_EQ(CKERR_ALREADYPRESENT, curve.AddControlPoint(API(point))); EXPECT_EQ(flags, curve.GetObjectFlags());
    EXPECT_EQ(1, curve.GetControlPointCount()); EXPECT_EQ(API(curve), point.GetCurve()); point.SetCurve(nullptr);
}
TEST_F(CurveControlPointsTest, GetterReturnsNullForNegativeAndPastEndIndicesWithoutMutation) {
    RCKCurve curve(Context); RCKCurvePoint point(Context); ASSERT_EQ(CK_OK, curve.AddControlPoint(API(point)));
    curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); const CKDWORD flags = curve.GetObjectFlags();
    EXPECT_EQ(nullptr, curve.GetControlPoint(-1)); EXPECT_EQ(nullptr, curve.GetControlPoint(1));
    EXPECT_EQ(nullptr, curve.GetControlPoint(0x7fffffff)); EXPECT_EQ(API(point), curve.GetControlPoint(0));
    EXPECT_EQ(flags, curve.GetObjectFlags()); point.SetCurve(nullptr);
}
TEST_F(CurveControlPointsTest, InsertPlacesNewPointBeforeFoundPredecessorIncludingFirstAndLast) {
    RCKCurve curve(Context); RCKCurvePoint a(Context), b(Context), c(Context), d(Context);
    ASSERT_EQ(CK_OK, curve.AddControlPoint(API(a))); ASSERT_EQ(CK_OK, curve.AddControlPoint(API(b)));
    ASSERT_EQ(CK_OK, curve.InsertControlPoint(API(a), API(c)));
    ASSERT_EQ(CK_OK, curve.InsertControlPoint(API(b), API(d)));
    EXPECT_EQ(4, curve.GetControlPointCount()); EXPECT_EQ(API(c), curve.GetControlPoint(0));
    EXPECT_EQ(API(a), curve.GetControlPoint(1)); EXPECT_EQ(API(d), curve.GetControlPoint(2)); EXPECT_EQ(API(b), curve.GetControlPoint(3));
    a.SetCurve(nullptr); b.SetCurve(nullptr); c.SetCurve(nullptr); d.SetCurve(nullptr);
}
TEST_F(CurveControlPointsTest, InsertMissingOrNullPredecessorAppendsAndPreservesWorldPosition) {
    RCKCurve curve(Context); RCKCurvePoint a(Context), b(Context), missing(Context);
    curve.SetPosition3f(5, 7, 11); b.SetPosition3f(13, 17, 19);
    ASSERT_EQ(CK_OK, curve.InsertControlPoint(nullptr, API(a)));
    ASSERT_EQ(CK_OK, curve.InsertControlPoint(API(missing), API(b)));
    EXPECT_EQ(API(a), curve.GetControlPoint(0)); EXPECT_EQ(API(b), curve.GetControlPoint(1));
    EXPECT_EQ(API(curve), b.GetCurve()); ExpectPosition(b, 13, 17, 19); EXPECT_EQ(nullptr, missing.GetCurve());
    a.SetCurve(nullptr); b.SetCurve(nullptr);
}
TEST_F(CurveControlPointsTest, InsertRejectsNullAndDuplicateWithoutReparenting) {
    RCKCurve curve(Context), other(Context); RCKCurvePoint a(Context);
    ASSERT_EQ(CK_OK, curve.AddControlPoint(API(a))); ASSERT_EQ(CK_OK, other.AddControlPoint(API(a)));
    curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); const CKDWORD flags = curve.GetObjectFlags();
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.InsertControlPoint(API(a), nullptr));
    EXPECT_EQ(CKERR_ALREADYPRESENT, curve.InsertControlPoint(nullptr, API(a)));
    EXPECT_EQ(flags, curve.GetObjectFlags()); EXPECT_EQ(API(other), a.GetCurve()); EXPECT_EQ(1, curve.GetControlPointCount());
    a.SetCurve(nullptr);
}
TEST_F(CurveControlPointsTest, RemoveDetachesCurveReferenceButKeepsHierarchyAndWorldPosition) {
    RCKCurve curve(Context); RCKCurvePoint a(Context), b(Context); a.SetPosition3f(2, 3, 5);
    ASSERT_EQ(CK_OK, curve.AddControlPoint(API(a))); ASSERT_EQ(CK_OK, curve.AddControlPoint(API(b)));
    curve.ModifyObjectFlags(CK_OBJECT_UPTODATE | CK_OBJECT_PRIVATE, 0); const CKDWORD flags = curve.GetObjectFlags();
    ASSERT_EQ(CK_OK, curve.RemoveControlPoint(API(a), FALSE)); EXPECT_EQ(1, curve.GetControlPointCount());
    EXPECT_EQ(API(b), curve.GetControlPoint(0)); EXPECT_EQ(nullptr, a.GetCurve());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&curve), a.GetParent()); EXPECT_EQ(2, curve.GetChildrenCount());
    ExpectPosition(a, 2, 3, 5); EXPECT_EQ(&a, Context->GetObject(a.GetID()));
    EXPECT_EQ(flags & ~CK_OBJECT_UPTODATE, curve.GetObjectFlags()); b.SetCurve(nullptr);
}
TEST_F(CurveControlPointsTest, RemoveDeletePointFlagIsIgnoredAndDoesNotDestroyTheObject) {
    RCKCurve curve(Context); RCKCurvePoint point(Context); const CK_ID id = point.GetID();
    ASSERT_EQ(CK_OK, curve.AddControlPoint(API(point))); ASSERT_EQ(CK_OK, curve.RemoveControlPoint(API(point), TRUE));
    EXPECT_EQ(0, curve.GetControlPointCount()); EXPECT_EQ(&point, Context->GetObject(id)); EXPECT_EQ(nullptr, point.GetCurve());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&curve), point.GetParent());
}
TEST_F(CurveControlPointsTest, RemoveNullDoesNotInvalidateOrModifyList) {
    RCKCurve curve(Context); curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); const CKDWORD flags = curve.GetObjectFlags();
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.RemoveControlPoint(nullptr, FALSE));
    EXPECT_EQ(0, curve.GetControlPointCount()); EXPECT_EQ(flags, curve.GetObjectFlags());
}
TEST_F(CurveControlPointsTest, RemovingNonmemberClearsItsCurveReferenceButPreservesOwningListAndParent) {
    RCKCurve curve(Context), owner(Context); RCKCurvePoint point(Context);
    ASSERT_EQ(CK_OK, owner.AddControlPoint(API(point))); curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    ASSERT_EQ(CK_OK, curve.RemoveControlPoint(API(point), FALSE)); EXPECT_EQ(nullptr, point.GetCurve());
    EXPECT_EQ(0, curve.GetControlPointCount()); EXPECT_EQ(1, owner.GetControlPointCount());
    EXPECT_EQ(API(point), owner.GetControlPoint(0)); EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&owner), point.GetParent());
    EXPECT_FALSE(curve.IsUpToDate());
}
TEST_F(CurveControlPointsTest, RemoveAllClearsOnlyListAndPreservesPointOwnersHierarchyAndLifetime) {
    RCKCurve curve(Context); RCKCurvePoint a(Context), b(Context);
    ASSERT_EQ(CK_OK, curve.AddControlPoint(API(a))); ASSERT_EQ(CK_OK, curve.AddControlPoint(API(b)));
    curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); ASSERT_EQ(CK_OK, curve.RemoveAllControlPoints());
    EXPECT_EQ(0, curve.GetControlPointCount()); EXPECT_EQ(nullptr, curve.GetControlPoint(0)); EXPECT_FALSE(curve.IsUpToDate());
    EXPECT_EQ(API(curve), a.GetCurve()); EXPECT_EQ(API(curve), b.GetCurve()); EXPECT_EQ(2, curve.GetChildrenCount());
    EXPECT_EQ(&a, Context->GetObject(a.GetID())); EXPECT_EQ(&b, Context->GetObject(b.GetID()));
    curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); a.SetBias(0.125f); EXPECT_FALSE(curve.IsUpToDate());
    a.SetCurve(nullptr); b.SetCurve(nullptr);
}
TEST_F(CurveControlPointsTest, RemoveAllEmptyStillInvalidatesAndAllowsReaddingTheSamePoint) {
    RCKCurve curve(Context); RCKCurvePoint point(Context);
    curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); ASSERT_EQ(CK_OK, curve.RemoveAllControlPoints()); EXPECT_FALSE(curve.IsUpToDate());
    ASSERT_EQ(CK_OK, curve.AddControlPoint(API(point))); ASSERT_EQ(CK_OK, curve.RemoveAllControlPoints());
    ASSERT_EQ(CK_OK, curve.AddControlPoint(API(point))); EXPECT_EQ(1, curve.GetControlPointCount());
    EXPECT_EQ(API(point), curve.GetControlPoint(0)); EXPECT_EQ(API(curve), point.GetCurve()); point.SetCurve(nullptr);
}
TEST_F(CurveControlPointsTest, AddingPointToAnotherCurveReparentsItWithoutRemovingOldListEntry) {
    RCKCurve first(Context), second(Context); RCKCurvePoint point(Context); point.SetPosition3f(3, 5, 7);
    first.SetPosition3f(10, 20, 30); second.SetPosition3f(-2, -3, -5);
    ASSERT_EQ(CK_OK, first.AddControlPoint(API(point))); ASSERT_EQ(CK_OK, second.AddControlPoint(API(point)));
    EXPECT_EQ(1, first.GetControlPointCount()); EXPECT_EQ(API(point), first.GetControlPoint(0));
    EXPECT_EQ(1, second.GetControlPointCount()); EXPECT_EQ(API(second), point.GetCurve());
    EXPECT_EQ(0, first.GetChildrenCount()); EXPECT_EQ(1, second.GetChildrenCount()); ExpectPosition(point, 3, 5, 7);
    point.SetCurve(nullptr);
}

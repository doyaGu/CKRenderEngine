#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKCurve.h"
#include "RCKCurvePoint.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class CurvePointTangentsTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCurvePoint, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKCurve, CKCID_3DENTITY);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKCurvePoint *Point(RCKCurvePoint &point) { return reinterpret_cast<CKCurvePoint *>(&point); }
    static void Vec(const VxVector &v, float x, float y, float z) {
        EXPECT_NEAR(x, v.x, .0001f); EXPECT_NEAR(y, v.y, .0001f); EXPECT_NEAR(z, v.z, .0001f);
    }
    static void Attach(RCKCurve &curve, RCKCurvePoint &point, float x) {
        curve.Show(CKHIDE); point.SetPosition3f(x, 0, 0); Point(point)->UseTCB(FALSE); Point(point)->SetLinear(FALSE);
        VxVector zero(0, 0, 0); Point(point)->SetTangents(&zero, &zero); ASSERT_EQ(CK_OK, curve.AddControlPoint(Point(point)));
    }
};
}

TEST_F(CurvePointTangentsTest, NewPointProvidesZeroTangentsAndZeroCumulativeLength) {
    RCKCurvePoint point(Context); VxVector in(9, 8, 7), out(6, 5, 4); Point(point)->GetTangents(&in, &out);
    Vec(in, 0, 0, 0); Vec(out, 0, 0, 0); EXPECT_EQ(0, Point(point)->GetLength());
}
TEST_F(CurvePointTangentsTest, SDKStoresUnnormalizedTangentsWithoutClampingOrChangingTCBMode) {
    RCKCurvePoint point(Context); Point(point)->UseTCB(TRUE); VxVector in(-2, 3, -4), out(5, -6, 7), readIn, readOut;
    Point(point)->SetTangents(&in, &out); Point(point)->GetTangents(&readIn, &readOut);
    Vec(readIn, -2, 3, -4); Vec(readOut, 5, -6, 7); EXPECT_TRUE(Point(point)->IsTCB());
}
TEST_F(CurvePointTangentsTest, IncomingAndOutgoingInputsAreIndependentlyOptional) {
    RCKCurvePoint point(Context); VxVector in(1, 2, 3), out(4, 5, 6), replacement(-7, 8, -9), readIn, readOut;
    Point(point)->SetTangents(&in, &out); Point(point)->SetTangents(&replacement, nullptr);
    Point(point)->GetTangents(&readIn, &readOut); Vec(readIn, -7, 8, -9); Vec(readOut, 4, 5, 6);
    Point(point)->SetTangents(nullptr, &replacement); Point(point)->GetTangents(&readIn, &readOut); Vec(readOut, -7, 8, -9);
}
TEST_F(CurvePointTangentsTest, OptionalOutputsAndAliasedOutputObeyIncomingThenOutgoingCopyOrder) {
    RCKCurvePoint point(Context); VxVector in(1, 2, 3), out(4, 5, 6), result(9, 9, 9);
    Point(point)->SetTangents(&in, &out); Point(point)->GetTangents(&result, nullptr); Vec(result, 1, 2, 3);
    Point(point)->GetTangents(nullptr, &result); Vec(result, 4, 5, 6);
    Point(point)->GetTangents(&result, &result); Vec(result, 4, 5, 6); Point(point)->GetTangents(nullptr, nullptr);
}
TEST_F(CurvePointTangentsTest, SetterCopiesValuesRatherThanRetainingCallerVectors) {
    RCKCurvePoint point(Context); VxVector value(2, 3, 4), in, out; Point(point)->SetTangents(&value, &value);
    value.Set(9, 8, 7); Point(point)->GetTangents(&in, &out); Vec(in, 2, 3, 4); Vec(out, 2, 3, 4);
}
TEST_F(CurvePointTangentsTest, ExplicitTangentEditInvalidatesOwnerAndChangesIndependentHermiteSample) {
    RCKCurve curve(Context); RCKCurvePoint a(Context), b(Context); Attach(curve, a, 0); Attach(curve, b, 4);
    VxVector position; ASSERT_EQ(CK_OK, curve.GetLocalPos(.5f, &position)); Vec(position, 2, 0, 0); ASSERT_TRUE(curve.IsUpToDate());
    VxVector outgoing(0, 8, 0), incoming(0, -8, 0); Point(a)->SetTangents(nullptr, &outgoing); Point(b)->SetTangents(&incoming, nullptr);
    EXPECT_FALSE(curve.IsUpToDate()); ASSERT_EQ(CK_OK, curve.GetLocalPos(.5f, &position)); Vec(position, 2, 2, 0);
    EXPECT_TRUE(curve.IsUpToDate()); EXPECT_GT(curve.GetLength(), 4);
}
TEST_F(CurvePointTangentsTest, NullTangentInputsPreserveDataWhileNotifyingOnlyAssignedOwner) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); owner.Show(CKHIDE); other.Show(CKHIDE);
    point.SetCurve(reinterpret_cast<CKCurve *>(&owner)); VxVector in(1, 2, 3), out(4, 5, 6), readIn, readOut;
    Point(point)->SetTangents(&in, &out); owner.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); other.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    Point(point)->SetTangents(nullptr, nullptr); EXPECT_FALSE(owner.IsUpToDate()); EXPECT_TRUE(other.IsUpToDate());
    Point(point)->GetTangents(&readIn, &readOut); Vec(readIn, 1, 2, 3); Vec(readOut, 4, 5, 6);
}
TEST_F(CurvePointTangentsTest, GetLengthReadsCachedCumulativeValueWithoutUpdatingOwner) {
    RCKCurve curve(Context); RCKCurvePoint point(Context); Attach(curve, point, 0); point.SetCurveLength(13.25f);
    ASSERT_FALSE(curve.IsUpToDate()); EXPECT_EQ(13.25f, Point(point)->GetLength()); EXPECT_FALSE(curve.IsUpToDate());
}
TEST_F(CurvePointTangentsTest, CacheWriterStoresSignedValueAndDoesNotNotifyOwner) {
    RCKCurve curve(Context); RCKCurvePoint point(Context); Attach(curve, point, 0);
    curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); point.SetCurveLength(-3.5f);
    EXPECT_EQ(-3.5f, Point(point)->GetLength()); EXPECT_TRUE(curve.IsUpToDate());
}
TEST_F(CurvePointTangentsTest, RealCurveUpdateWritesDistanceFromFirstControlPoint) {
    RCKCurve curve(Context); RCKCurvePoint a(Context), b(Context), c(Context); Attach(curve, a, 0); Attach(curve, b, 3); Attach(curve, c, 3);
    Point(a)->SetLinear(TRUE); Point(b)->SetLinear(TRUE); Point(c)->SetLinear(TRUE); c.SetPosition3f(3, 4, 0);
    EXPECT_EQ(7, curve.GetLength()); EXPECT_EQ(0, Point(a)->GetLength()); EXPECT_EQ(3, Point(b)->GetLength()); EXPECT_EQ(7, Point(c)->GetLength());
}

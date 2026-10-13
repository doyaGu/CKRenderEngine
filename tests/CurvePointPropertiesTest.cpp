#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKCurve.h"
#include "RCKCurvePoint.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class CurvePointPropertiesTest : public ::testing::Test {
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
    static void Vec(const VxVector &value, float x, float y, float z) {
        EXPECT_FLOAT_EQ(x, value.x); EXPECT_FLOAT_EQ(y, value.y); EXPECT_FLOAT_EQ(z, value.z);
    }
    static CKDWORD Ready(RCKCurve &curve) {
        curve.ModifyObjectFlags(CK_OBJECT_UPTODATE | CK_OBJECT_PRIVATE, 0); return curve.GetObjectFlags();
    }
    static void Attach(RCKCurve &curve, RCKCurvePoint &point, float x, float y) {
        point.SetPosition3f(x, y, 0); point.UseTCB(FALSE); VxVector zero(0, 0, 0);
        point.SetTangents(&zero, &zero); ASSERT_EQ(CK_OK, curve.AddControlPoint(API(point)));
    }
};
}

TEST_F(CurvePointPropertiesTest, BiasStoresUnrestrictedValuesAndAlwaysInvalidatesOwnerOnly) {
    RCKCurve curve(Context); RCKCurvePoint point(Context); point.SetCurve(API(curve));
    const float values[] = {0, -2, 1.5f, 1.5f};
    for (int i = 0; i < 4; ++i) {
        const CKDWORD flags = Ready(curve); point.SetBias(values[i]); EXPECT_EQ(values[i], point.GetBias());
        EXPECT_EQ(flags & ~CK_OBJECT_UPTODATE, curve.GetObjectFlags());
    }
}
TEST_F(CurvePointPropertiesTest, TensionStoresUnrestrictedValuesAndAlwaysInvalidatesOwnerOnly) {
    RCKCurve curve(Context); RCKCurvePoint point(Context); point.SetCurve(API(curve));
    const float values[] = {0, -0.5f, 2, 2};
    for (int i = 0; i < 4; ++i) {
        const CKDWORD flags = Ready(curve); point.SetTension(values[i]); EXPECT_EQ(values[i], point.GetTension());
        EXPECT_EQ(flags & ~CK_OBJECT_UPTODATE, curve.GetObjectFlags());
    }
}
TEST_F(CurvePointPropertiesTest, ContinuityStoresUnrestrictedValuesAndAlwaysInvalidatesOwnerOnly) {
    RCKCurve curve(Context); RCKCurvePoint point(Context); point.SetCurve(API(curve));
    const float values[] = {0, -3, 2.5f, 2.5f};
    for (int i = 0; i < 4; ++i) {
        const CKDWORD flags = Ready(curve); point.SetContinuity(values[i]); EXPECT_EQ(values[i], point.GetContinuity());
        EXPECT_EQ(flags & ~CK_OBJECT_UPTODATE, curve.GetObjectFlags());
    }
}
TEST_F(CurvePointPropertiesTest, LinearRetainsRawBooleanAndAlwaysInvalidates) {
    RCKCurve curve(Context); RCKCurvePoint point(Context); point.SetCurve(API(curve));
    const CKBOOL values[] = {FALSE, TRUE, 7, -2, -2};
    for (int i = 0; i < 5; ++i) {
        const CKDWORD flags = Ready(curve); point.SetLinear(values[i]); EXPECT_EQ(values[i], point.IsLinear());
        EXPECT_EQ(flags & ~CK_OBJECT_UPTODATE, curve.GetObjectFlags());
    }
}
TEST_F(CurvePointPropertiesTest, TCBPublicModeNormalizesTruthyInputsAndDefaultArgument) {
    RCKCurvePoint point(Context); CKCurvePoint *api = API(point); EXPECT_TRUE(api->IsTCB());
    api->UseTCB(FALSE); EXPECT_EQ(FALSE, api->IsTCB()); api->UseTCB(7); EXPECT_EQ(TRUE, api->IsTCB());
    api->UseTCB(-2); EXPECT_EQ(TRUE, api->IsTCB()); api->UseTCB(FALSE); api->UseTCB(); EXPECT_EQ(TRUE, api->IsTCB());
}
TEST_F(CurvePointPropertiesTest, SwitchingTCBModeInvalidatesCachedOwnerAndKeepsOtherFlags) {
    RCKCurve curve(Context); RCKCurvePoint point(Context); point.SetCurve(API(curve));
    CKDWORD flags = Ready(curve); point.UseTCB(FALSE); EXPECT_EQ(flags & ~CK_OBJECT_UPTODATE, curve.GetObjectFlags());
    flags = Ready(curve); point.UseTCB(TRUE); EXPECT_EQ(flags & ~CK_OBJECT_UPTODATE, curve.GetObjectFlags());
}
TEST_F(CurvePointPropertiesTest, ModeSwitchRecomputesTangentsLengthAndSampleToFreshState) {
    RCKCurve curve(Context), fresh(Context); RCKCurvePoint a(Context), b(Context), c(Context), fa(Context), fb(Context), fc(Context);
    Attach(curve, a, 0, 0); Attach(curve, b, 2, 4); Attach(curve, c, 8, 0);
    Attach(fresh, fa, 0, 0); Attach(fresh, fb, 2, 4); Attach(fresh, fc, 8, 0);
    const float oldLength = curve.GetLength(); VxVector oldPosition; ASSERT_EQ(CK_OK, curve.GetLocalPos(0.25f, &oldPosition));
    fa.UseTCB(TRUE); fb.UseTCB(TRUE); fc.UseTCB(TRUE); const float expectedLength = fresh.GetLength();
    VxVector expected; ASSERT_EQ(CK_OK, fresh.GetLocalPos(0.25f, &expected));
    a.UseTCB(TRUE); b.UseTCB(TRUE); c.UseTCB(TRUE); EXPECT_FALSE(curve.IsUpToDate());
    EXPECT_FLOAT_EQ(expectedLength, curve.GetLength()); EXPECT_NE(oldLength, expectedLength);
    VxVector actual; ASSERT_EQ(CK_OK, curve.GetLocalPos(0.25f, &actual)); Vec(actual, expected.x, expected.y, expected.z);
    EXPECT_NE(oldPosition.y, actual.y); VxVector in, out; ASSERT_EQ(CK_OK, curve.GetTangentsByIndex(1, &in, &out));
    Vec(in, 4, 0, 0); Vec(out, 4, 0, 0);
}
TEST_F(CurvePointPropertiesTest, NotifyUpdateClearsOnlyCurrentCurveUpToDateBit) {
    RCKCurve curve(Context); RCKCurvePoint point(Context); point.SetCurve(API(curve));
    const CKDWORD flags = Ready(curve); const CKDWORD pointFlags = point.GetObjectFlags(); point.NotifyUpdate();
    EXPECT_EQ(flags & ~CK_OBJECT_UPTODATE, curve.GetObjectFlags()); EXPECT_EQ(pointFlags, point.GetObjectFlags());
}
TEST_F(CurvePointPropertiesTest, DetachedPropertyMutationAndNotificationNeedNoCurve) {
    RCKCurvePoint point(Context); point.SetBias(3); point.SetTension(-2); point.SetContinuity(4); point.SetLinear(7);
    point.UseTCB(FALSE); point.NotifyUpdate(); EXPECT_EQ(nullptr, point.GetCurve()); EXPECT_EQ(3, point.GetBias());
    EXPECT_EQ(-2, point.GetTension()); EXPECT_EQ(4, point.GetContinuity()); EXPECT_EQ(7, point.IsLinear()); EXPECT_FALSE(point.IsTCB());
}
TEST_F(CurvePointPropertiesTest, ReservedAndFittedCacheVectorsRemainIndependentAndDoNotInvalidate) {
    RCKCurve curve(Context); RCKCurvePoint point(Context); point.SetCurve(API(curve));
    const CKDWORD flags = Ready(curve); const VxVector reserved(1, -2, 3), fitted(8, 7, -6);
    point.SetReservedVector(&reserved); point.SetFittedVector(&fitted); VxVector actual;
    point.GetReservedVector(&actual); Vec(actual, 1, -2, 3); point.GetFittedVector(&actual); Vec(actual, 8, 7, -6);
    EXPECT_EQ(flags, curve.GetObjectFlags());
}
TEST_F(CurvePointPropertiesTest, OptionalNullCacheHelpersPreserveStoredValues) {
    RCKCurvePoint point(Context); const VxVector reserved(1, 2, 3), fitted(4, 5, 6);
    point.SetReservedVector(&reserved); point.SetFittedVector(&fitted); point.SetReservedVector(nullptr); point.SetFittedVector(nullptr);
    point.GetReservedVector(nullptr); point.GetFittedVector(nullptr); VxVector actual;
    point.GetReservedVector(&actual); Vec(actual, 1, 2, 3); point.GetFittedVector(&actual); Vec(actual, 4, 5, 6);
}

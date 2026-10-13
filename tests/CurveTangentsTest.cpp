#include <gtest/gtest.h>
#include <cmath>
#include "CKAll.h"
#include "RCKCurve.h"
#include "RCKCurvePoint.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class GeometryCurveProbe : public RCKCurve {
public:
    explicit GeometryCurveProbe(CKContext *context) : RCKCurve(context), Updates(0) { Show(CKHIDE); }
    void Update() override { ++Updates; RCKCurve::Update(); }
    void SetLoading(CKBOOL loading) { m_Loading = loading; }
    float CachedLength() const { return m_Length; }
    int Updates;
};

class CurveTangentsTest : public ::testing::Test {
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
    static CKCurvePoint *API(RCKCurvePoint &point) { return reinterpret_cast<CKCurvePoint *>(&point); }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static void Vec(const VxVector &value, float x, float y, float z, float tolerance = 0.0001f) {
        EXPECT_NEAR(x, value.x, tolerance); EXPECT_NEAR(y, value.y, tolerance); EXPECT_NEAR(z, value.z, tolerance);
    }
    static void Attach(GeometryCurveProbe &curve, RCKCurvePoint &point, float x, float y, float z, CKBOOL linear = TRUE) {
        const VxVector position(x, y, z);
        point.SetPosition(&position);
        point.SetLinear(linear);
        point.UseTCB(FALSE);
        VxVector zero(0.0f, 0.0f, 0.0f);
        point.SetTangents(&zero, &zero);
        ASSERT_EQ(CK_OK, curve.AddControlPoint(API(point)));
    }
    static void Reserved(RCKCurvePoint &point, float x, float y, float z) {
        const VxVector position(x, y, z); point.SetReservedVector(&position);
    }
};
}

TEST_F(CurveTangentsTest, ExplicitTangentsAllowIndependentNullableInputsAndOutputs) {
    GeometryCurveProbe curve(Context); RCKCurvePoint point(Context); Attach(curve, point, 0, 0, 0);
    VxVector incoming(1, 2, 3), outgoing(4, 5, 6), readIn, readOut;
    ASSERT_EQ(CK_OK, curve.SetTangentsByIndex(0, &incoming, &outgoing));
    ASSERT_EQ(CK_OK, curve.GetTangentsByIndex(0, &readIn, &readOut)); Vec(readIn, 1, 2, 3); Vec(readOut, 4, 5, 6);
    incoming.Set(7, 8, 9); ASSERT_EQ(CK_OK, curve.SetTangents(API(point), &incoming, nullptr));
    ASSERT_EQ(CK_OK, curve.GetTangents(API(point), nullptr, &readOut)); Vec(readOut, 4, 5, 6);
    ASSERT_EQ(CK_OK, curve.GetTangentsByIndex(0, &readIn, nullptr)); Vec(readIn, 7, 8, 9);
    EXPECT_EQ(CK_OK, curve.SetTangentsByIndex(0, nullptr, nullptr)); EXPECT_EQ(CK_OK, curve.GetTangentsByIndex(0, nullptr, nullptr));
}
TEST_F(CurveTangentsTest, TangentWrappersRejectNullForeignAndInvalidIndicesWithoutMutatingOutputs) {
    GeometryCurveProbe curve(Context); RCKCurvePoint point(Context), outsider(Context); Attach(curve, point, 0, 0, 0);
    VxVector incoming(1, 2, 3), outgoing(4, 5, 6);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.GetTangents(nullptr, &incoming, &outgoing));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.SetTangents(nullptr, &incoming, &outgoing));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.GetTangents(API(outsider), &incoming, &outgoing));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.SetTangents(API(outsider), &incoming, &outgoing));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.GetTangentsByIndex(-1, &incoming, &outgoing));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.GetTangentsByIndex(1, &incoming, &outgoing));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.SetTangentsByIndex(-1, &incoming, &outgoing));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.SetTangentsByIndex(1, &incoming, &outgoing)); Vec(incoming, 1, 2, 3); Vec(outgoing, 4, 5, 6);
}
TEST_F(CurveTangentsTest, TCBInteriorTangentsUseIndependentTensionContinuityAndBiasWeights) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 2, 4, 6); Attach(curve, c, 10, 6, 4);
    Reserved(a, 0, 0, 0); Reserved(b, 2, 4, 6); Reserved(c, 10, 6, 4);
    b.UseTCB(TRUE); b.SetTension(0.5f); b.SetContinuity(0.5f); b.SetBias(-0.5f);
    VxVector in, out; ASSERT_EQ(CK_OK, curve.GetTangentsByIndex(1, &in, &out));
    Vec(in, 4.625f, 1.375f, -0.75f); Vec(out, 1.875f, 1.125f, 0.75f);
    VxVector only; ASSERT_EQ(CK_OK, curve.GetTangents(API(b), &only, nullptr)); Vec(only, 4.625f, 1.375f, -0.75f);
    ASSERT_EQ(CK_OK, curve.GetTangents(API(b), nullptr, &only)); Vec(only, 1.875f, 1.125f, 0.75f);
}
TEST_F(CurveTangentsTest, TCBOpenEndpointsClampAndClosedEndpointsWrap) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 4, 0, 0); Attach(curve, c, 4, 6, 0);
    Reserved(a, 0, 0, 0); Reserved(b, 4, 0, 0); Reserved(c, 4, 6, 0); a.UseTCB(TRUE); c.UseTCB(TRUE);
    VxVector in, out; ASSERT_EQ(CK_OK, curve.GetTangentsByIndex(0, &in, &out)); Vec(in, 2, 0, 0); Vec(out, 2, 0, 0);
    ASSERT_EQ(CK_OK, curve.GetTangentsByIndex(2, &in, &out)); Vec(in, 0, 3, 0); Vec(out, 0, 3, 0);
    curve.Close(); ASSERT_EQ(CK_OK, curve.GetTangentsByIndex(0, &in, &out)); Vec(in, 0, -3, 0); Vec(out, 0, -3, 0);
    ASSERT_EQ(CK_OK, curve.GetTangentsByIndex(2, &in, &out)); Vec(in, -2, 0, 0); Vec(out, -2, 0, 0);
}
TEST_F(CurveTangentsTest, TCBSinglePointAndUnitTensionProduceZeroTangents) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context); Attach(curve, a, 1, 2, 3); Reserved(a, 1, 2, 3); a.UseTCB(TRUE);
    VxVector in, out; ASSERT_EQ(CK_OK, curve.GetTangentsByIndex(0, &in, &out)); Vec(in, 0, 0, 0); Vec(out, 0, 0, 0);
    curve.Close(); ASSERT_EQ(CK_OK, curve.GetTangentsByIndex(0, &in, &out)); Vec(in, 0, 0, 0); Vec(out, 0, 0, 0);
}
TEST_F(CurveTangentsTest, TCBTensionOneNullifiesBothTangentsAcrossDistinctNeighbors) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 2, 4, 6); Attach(curve, c, 10, 6, 4);
    Reserved(a, 0, 0, 0); Reserved(b, 2, 4, 6); Reserved(c, 10, 6, 4); b.UseTCB(TRUE); b.SetTension(1);
    b.SetContinuity(2); b.SetBias(-3); VxVector in, out; ASSERT_EQ(CK_OK, curve.GetTangentsByIndex(1, &in, &out));
    Vec(in, 0, 0, 0); Vec(out, 0, 0, 0);
}
TEST_F(CurveTangentsTest, UpdateComputesTCBCacheBeforeFittingAndPreservesTCBMode) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 2, 4, 6); Attach(curve, c, 10, 6, 4); b.UseTCB(TRUE);
    b.SetTension(0.5f); b.SetContinuity(0.5f); b.SetBias(-0.5f); curve.SetFittingCoeff(1.0f); curve.Update();
    VxVector in, out, fitted; b.GetTangents(&in, &out); Vec(in, 4.625f, 1.375f, -0.75f); Vec(out, 1.875f, 1.125f, 0.75f);
    b.GetFittedVector(&fitted); Vec(fitted, 5, 3, 2); EXPECT_TRUE(b.IsTCB()); EXPECT_TRUE(curve.IsUpToDate());
}
TEST_F(CurveTangentsTest, TCBSetTangentsInvertsDominantXSumWithoutChangingContinuityOrTension) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 5, 1, 1); Attach(curve, c, 6, 2, 2);
    Reserved(a, 0, 0, 0); Reserved(b, 5, 1, 1); Reserved(c, 6, 2, 2); b.UseTCB(TRUE); b.SetTension(0.5f); b.SetContinuity(0.25f);
    VxVector in(1, 100, 200), out(1, 300, 400); curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    ASSERT_EQ(CK_OK, curve.SetTangentsByIndex(1, &in, &out)); EXPECT_EQ(-0.5f, b.GetBias());
    VxVector readIn, readOut; ASSERT_EQ(CK_OK, curve.GetTangentsByIndex(1, &readIn, &readOut)); EXPECT_NEAR(2.0f, readIn.x + readOut.x, 0.0001f);
    EXPECT_EQ(0.5f, b.GetTension()); EXPECT_EQ(0.25f, b.GetContinuity()); EXPECT_FALSE(curve.IsUpToDate()); EXPECT_TRUE(b.IsTCB());
}
TEST_F(CurveTangentsTest, TCBSetTangentsUsesDominantYAndZComponents) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 1, 5, 1); Attach(curve, c, 2, 6, 2);
    Reserved(a, 0, 0, 0); Reserved(b, 1, 5, 1); Reserved(c, 2, 6, 2); b.UseTCB(TRUE);
    VxVector in(100, 1, 200), out(300, 1, 400); ASSERT_EQ(CK_OK, curve.SetTangents(API(b), &in, &out)); EXPECT_EQ(-1.0f, b.GetBias());
    Reserved(b, 1, 1, 5); Reserved(c, 2, 2, 6); in.Set(100, 200, 1); out.Set(300, 400, 1);
    ASSERT_EQ(CK_OK, curve.SetTangentsByIndex(1, &in, &out)); EXPECT_EQ(-1.0f, b.GetBias());
}
TEST_F(CurveTangentsTest, TCBSetTangentsDominantAxisTiePrefersXAndCoincidentChordDefaultsBiasZero) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 5, 5, 1); Attach(curve, c, 6, 6, 2);
    Reserved(a, 0, 0, 0); Reserved(b, 5, 5, 1); Reserved(c, 6, 6, 2); b.UseTCB(TRUE);
    VxVector in(1, 100, 200), out(1, 300, 400); ASSERT_EQ(CK_OK, curve.SetTangentsByIndex(1, &in, &out)); EXPECT_EQ(-1.0f, b.GetBias());
    Reserved(b, 1, 1, 1); Reserved(c, 2, 2, 2); ASSERT_EQ(CK_OK, curve.SetTangentsByIndex(1, &in, &out)); EXPECT_EQ(0.0f, b.GetBias());
}
TEST_F(CurveTangentsTest, TCBSetTangentsRejectsMissingInputWithoutChangingBias) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 5, 1, 1); Attach(curve, c, 6, 2, 2);
    Reserved(a, 0, 0, 0); Reserved(b, 5, 1, 1); Reserved(c, 6, 2, 2); b.UseTCB(TRUE); b.SetBias(0.75f);
    VxVector tangent(1, 2, 3); EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.SetTangentsByIndex(1, nullptr, &tangent));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.SetTangents(API(b), &tangent, nullptr)); EXPECT_EQ(0.75f, b.GetBias());
}
TEST_F(CurveTangentsTest, TCBUnitTensionKeepsFiniteBiasForZeroTangentsAndRejectsUnsatisfiableNonzeroRequest) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 5, 1, 1); Attach(curve, c, 6, 2, 2);
    Reserved(a, 0, 0, 0); Reserved(b, 5, 1, 1); Reserved(c, 6, 2, 2);
    b.UseTCB(TRUE); b.SetTension(1.0f); b.SetBias(0.375f);
    VxVector zero(0, 0, 0), in, out;
    EXPECT_EQ(CK_OK, curve.SetTangentsByIndex(1, &zero, &zero));
    EXPECT_TRUE(std::isfinite(b.GetBias())); EXPECT_EQ(0.375f, b.GetBias());
    ASSERT_EQ(CK_OK, curve.GetTangentsByIndex(1, &in, &out)); Vec(in, 0, 0, 0); Vec(out, 0, 0, 0);
    const VxVector nonzeroValue(1, 0, 0); VxVector nonzero = nonzeroValue;
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.SetTangents(API(b), &nonzero, &zero));
    EXPECT_TRUE(std::isfinite(b.GetBias())); EXPECT_EQ(0.375f, b.GetBias()); EXPECT_EQ(1.0f, b.GetTension());
    ASSERT_EQ(CK_OK, curve.GetTangents(API(b), &in, &out)); Vec(in, 0, 0, 0); Vec(out, 0, 0, 0);
}

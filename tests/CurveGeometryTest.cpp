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

class CurveGeometryTest : public ::testing::Test {
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

TEST_F(CurveGeometryTest, EmptyAndSinglePointLengthAreZeroAndUpdateTerminates) {
    GeometryCurveProbe curve(Context);
    EXPECT_EQ(0.0f, curve.GetLength()); EXPECT_EQ(1, curve.Updates);
    RCKCurvePoint point(Context); Attach(curve, point, 2.0f, 3.0f, 4.0f);
    EXPECT_EQ(0.0f, curve.GetLength()); EXPECT_EQ(2, curve.Updates);
    VxVector fitted; point.GetFittedVector(&fitted); Vec(fitted, 2.0f, 3.0f, 4.0f);
    EXPECT_TRUE(curve.IsUpToDate());
}
TEST_F(CurveGeometryTest, LengthUsesSegmentDistancesAndCachesCumulativePointLengths) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 3, 0, 0); Attach(curve, c, 3, 4, 0);
    EXPECT_EQ(7.0f, curve.GetLength()); EXPECT_EQ(0.0f, a.GetLength());
    EXPECT_EQ(3.0f, b.GetLength()); EXPECT_EQ(7.0f, c.GetLength());
    EXPECT_EQ(7.0f, curve.GetLength()); EXPECT_EQ(1, curve.Updates);
}
TEST_F(CurveGeometryTest, ClosedLengthIncludesClosingSegmentWithoutOverwritingFirstCumulativeLength) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 3, 0, 0); Attach(curve, c, 3, 4, 0); curve.Close();
    EXPECT_EQ(12.0f, curve.GetLength()); EXPECT_EQ(0.0f, a.GetLength());
    EXPECT_EQ(3.0f, b.GetLength()); EXPECT_EQ(7.0f, c.GetLength());
}
TEST_F(CurveGeometryTest, PointMovementInvalidatesAndRecomputesActualGeometry) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 3, 4, 0); EXPECT_EQ(5.0f, curve.GetLength());
    const VxVector moved(0, 0, 12); b.SetPosition(&moved); EXPECT_FALSE(curve.IsUpToDate());
    EXPECT_EQ(12.0f, curve.GetLength()); EXPECT_EQ(2, curve.Updates);
    VxVector position; ASSERT_EQ(CK_OK, curve.GetLocalPos(0.5f, &position)); Vec(position, 0, 0, 6);
}
TEST_F(CurveGeometryTest, UpdateWhileLoadingPreservesCacheAndDefersRecomputation) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 5, 0, 0); ASSERT_EQ(5.0f, curve.GetLength());
    const VxVector moved(9, 0, 0); b.SetPosition(&moved); curve.SetLoading(TRUE); curve.Update();
    EXPECT_EQ(5.0f, curve.CachedLength()); EXPECT_FALSE(curve.IsUpToDate());
    VxVector fitted; b.GetFittedVector(&fitted); Vec(fitted, 5, 0, 0);
    curve.SetLoading(FALSE); EXPECT_EQ(9.0f, curve.GetLength()); EXPECT_TRUE(curve.IsUpToDate());
}
TEST_F(CurveGeometryTest, FittingUsesOriginalNeighborsSimultaneouslyAndKeepsReservedPositions) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 4, 8, 0); Attach(curve, c, 8, 0, 0);
    curve.SetFittingCoeff(0.5f); curve.Update(); VxVector v;
    a.GetFittedVector(&v); Vec(v, 1, 2, 0); b.GetFittedVector(&v); Vec(v, 4, 4, 0);
    c.GetFittedVector(&v); Vec(v, 7, 2, 0); b.GetReservedVector(&v); Vec(v, 4, 8, 0);
    EXPECT_NEAR(2.0f * std::sqrt(13.0f), curve.GetLength(), 0.0001f);
}
TEST_F(CurveGeometryTest, ClosedFittingWrapsFirstAndLastNeighbors) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 6, 0, 0); Attach(curve, c, 0, 6, 0);
    curve.Close(); curve.SetFittingCoeff(1.0f); curve.Update(); VxVector v;
    a.GetFittedVector(&v); Vec(v, 3, 3, 0); b.GetFittedVector(&v); Vec(v, 0, 3, 0);
    c.GetFittedVector(&v); Vec(v, 3, 0, 0);
}
TEST_F(CurveGeometryTest, NegativeFittingCoefficientLeavesOriginalGeometry) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 4, 8, 0); Attach(curve, c, 8, 0, 0);
    curve.SetFittingCoeff(-2.0f); curve.Update(); VxVector v; b.GetFittedVector(&v); Vec(v, 4, 8, 0);
}
TEST_F(CurveGeometryTest, FittingAboveOneExtrapolatesWithoutClamping) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 4, 8, 0); Attach(curve, c, 8, 0, 0);
    curve.SetFittingCoeff(2.0f); curve.Update(); VxVector v; b.GetFittedVector(&v); Vec(v, 4, -8, 0);
}
TEST_F(CurveGeometryTest, EmptyAndSinglePointSamplingRejectsWithoutWritingOutputs) {
    GeometryCurveProbe curve(Context); VxVector position(9, 8, 7), direction(6, 5, 4);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.GetLocalPos(0.5f, &position, &direction));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.GetPos(0.5f, &position, &direction));
    RCKCurvePoint a(Context); Attach(curve, a, 1, 2, 3);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.GetLocalPos(0.5f, &position, &direction));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.GetPos(0.5f, &position, &direction));
    Vec(position, 9, 8, 7); Vec(direction, 6, 5, 4);
}
TEST_F(CurveGeometryTest, NullPositionRejectsAndPreservesOptionalDirection) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 4, 0, 0); VxVector direction(6, 5, 4);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.GetLocalPos(0.5f, nullptr, &direction));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.GetPos(0.5f, nullptr, &direction)); Vec(direction, 6, 5, 4);
}
TEST_F(CurveGeometryTest, OpenSamplingUsesDistanceWeightedSegmentRatherThanUniformPointIndex) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 3, 0, 0); Attach(curve, c, 3, 4, 0);
    VxVector position, direction; ASSERT_EQ(CK_OK, curve.GetLocalPos(0.5f, &position, &direction));
    Vec(position, 3, 0.5f, 0); Vec(direction, 0, 1, 0);
}
TEST_F(CurveGeometryTest, OpenStepsClampToEndpointPositions) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context);
    Attach(curve, a, 1, 2, 3); Attach(curve, b, 5, 6, 7); VxVector position;
    ASSERT_EQ(CK_OK, curve.GetLocalPos(-2.25f, &position)); Vec(position, 1, 2, 3);
    ASSERT_EQ(CK_OK, curve.GetLocalPos(3.25f, &position)); Vec(position, 5, 6, 7);
}
TEST_F(CurveGeometryTest, OriginAndExactControlPointDistancesSelectOutgoingSegment) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 3, 0, 0); Attach(curve, c, 3, 4, 0);
    VxVector position, direction;
    ASSERT_EQ(CK_OK, curve.GetLocalPos(0.0f, &position, &direction)); Vec(position, 0, 0, 0); Vec(direction, 1, 0, 0);
    ASSERT_EQ(CK_OK, curve.GetLocalPos(3.0f / 7.0f, &position, &direction)); Vec(position, 3, 0, 0); Vec(direction, 0, 1, 0);
    ASSERT_EQ(CK_OK, curve.GetPos(3.0f / 7.0f, &position, &direction)); Vec(position, 3, 0, 0); Vec(direction, 0, 1, 0);
}
TEST_F(CurveGeometryTest, ClosedLinearOriginIgnoresExplicitTangentsAndUsesActualChord) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 3, 0, 0); Attach(curve, c, 3, 4, 0); curve.Close();
    VxVector outgoing(0, 0, 9); a.SetTangents(nullptr, &outgoing); VxVector position, direction;
    ASSERT_EQ(CK_OK, curve.GetLocalPos(0.0f, &position, &direction)); Vec(position, 0, 0, 0); Vec(direction, 1, 0, 0);
    ASSERT_EQ(CK_OK, curve.GetPos(0.0f, &position, &direction)); Vec(position, 0, 0, 0); Vec(direction, 1, 0, 0);
}
TEST_F(CurveGeometryTest, ClosedStepsWrapPositiveAndNegativeCycles) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 3, 0, 0); Attach(curve, c, 3, 4, 0); curve.Close();
    VxVector position; ASSERT_EQ(CK_OK, curve.GetLocalPos(3.25f, &position)); Vec(position, 3, 0, 0);
    ASSERT_EQ(CK_OK, curve.GetLocalPos(-2.75f, &position)); Vec(position, 3, 0, 0);
    ASSERT_EQ(CK_OK, curve.GetLocalPos(-2.25f, &position)); Vec(position, 1.8f, 2.4f, 0);
}
TEST_F(CurveGeometryTest, ClosedOriginDirectionUsesFirstSegmentAndWrapsWholeCycle) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 3, 0, 0); Attach(curve, c, 3, 4, 0); curve.Close();
    VxVector position, direction; ASSERT_EQ(CK_OK, curve.GetLocalPos(0.0f, &position, &direction));
    Vec(position, 0, 0, 0); Vec(direction, 1, 0, 0);
    ASSERT_EQ(CK_OK, curve.GetLocalPos(1.0f, &position, &direction)); Vec(direction, 1, 0, 0);
}
TEST_F(CurveGeometryTest, CoincidentLinearPointsProduceFiniteStationarySample) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context);
    Attach(curve, a, 2, 3, 4); Attach(curve, b, 2, 3, 4); EXPECT_EQ(0.0f, curve.GetLength());
    VxVector position, direction; ASSERT_EQ(CK_OK, curve.GetLocalPos(0.75f, &position, &direction));
    Vec(position, 2, 3, 4); Vec(direction, 0, 0, 0);
}
TEST_F(CurveGeometryTest, HermiteArchMatchesIndependentBezierQuarterAndMidpointCoordinates) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context);
    Attach(curve, a, 0, 0, 0, FALSE); Attach(curve, b, 4, 0, 0, FALSE);
    VxVector outgoing(0, 8, 0), incoming(0, -8, 0); a.SetTangents(nullptr, &outgoing); b.SetTangents(&incoming, nullptr);
    // Bezier control polygon is (0,0), (0,8/3), (4,8/3), (4,0).
    VxVector position; ASSERT_EQ(CK_OK, curve.GetLocalPos(0.25f, &position)); Vec(position, 0.625f, 1.5f, 0);
    ASSERT_EQ(CK_OK, curve.GetLocalPos(0.5f, &position)); Vec(position, 2, 2, 0);
    ASSERT_EQ(CK_OK, curve.GetLocalPos(0.75f, &position)); Vec(position, 3.375f, 1.5f, 0);
    EXPECT_NEAR(6.3432973186f, curve.GetLength(), 0.001f);
}
TEST_F(CurveGeometryTest, HermiteDirectionUsesNativeForwardDifferenceIncludingEndpointExtrapolation) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context);
    Attach(curve, a, 0, 0, 0, FALSE); Attach(curve, b, 4, 0, 0, FALSE);
    VxVector outgoing(0, 8, 0), incoming(0, -8, 0); a.SetTangents(nullptr, &outgoing); b.SetTangents(&incoming, nullptr);
    VxVector position, direction; ASSERT_EQ(CK_OK, curve.GetLocalPos(0.5f, &position, &direction));
    Vec(position, 2, 2, 0); EXPECT_GT(direction.x, 0.999f); EXPECT_NEAR(-0.0133339f, direction.y, 0.0001f);
    ASSERT_EQ(CK_OK, curve.GetLocalPos(1.0f, &position, &direction)); Vec(position, 4, 0, 0);
    EXPECT_LT(direction.x, -0.01f); EXPECT_LT(direction.y, -0.999f); EXPECT_NEAR(1.0f, direction.Magnitude(), 0.0001f);
}
TEST_F(CurveGeometryTest, WorldSamplingTransformsPositionsAndNormalizesScaledDirections) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 4, 4, 0);
    const VxVector scale(2, 3, 4), translation(10, 20, 30); curve.SetScale(&scale); curve.SetPosition(&translation);
    curve.ModifyObjectFlags(0, CK_OBJECT_UPTODATE); VxVector local, world, direction;
    ASSERT_EQ(CK_OK, curve.GetLocalPos(0.5f, &local)); Vec(local, 2, 2, 0);
    ASSERT_EQ(CK_OK, curve.GetPos(0.5f, &world, &direction)); Vec(world, 14, 26, 30);
    Vec(direction, 2.0f / std::sqrt(13.0f), 3.0f / std::sqrt(13.0f), 0);
    EXPECT_NEAR(std::sqrt(32.0f), curve.GetLength(), 0.0001f);
}
TEST_F(CurveGeometryTest, ParentTransformContributesToWorldSamplingAndNotLocalCurveLength) {
    RCK3dEntity parent(Context); GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 4, 0, 0); curve.SetParent(Entity(parent), FALSE);
    const VxVector translation(10, 20, 30), scale(2, 3, 4); parent.SetScale(&scale); parent.SetPosition(&translation);
    curve.ModifyObjectFlags(0, CK_OBJECT_UPTODATE); VxVector position, direction;
    ASSERT_EQ(CK_OK, curve.GetPos(0.5f, &position, &direction)); Vec(position, 14, 20, 30); Vec(direction, 1, 0, 0);
    EXPECT_EQ(4.0f, curve.GetLength());
}
TEST_F(CurveGeometryTest, LocalPointCacheSurvivesLargeWorldTranslationWithoutRoundTripLoss) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 4, 0, 0);
    VxMatrix moved = curve.GetLocalMatrix(); moved[3].Set(100000000.0f, 0, 0); curve.SetLocalMatrix(moved);
    const VxVector local(4, 0, 0); b.SetPosition(&local, Entity(curve));
    EXPECT_EQ(4.0f, curve.GetLength()); VxVector position;
    ASSERT_EQ(CK_OK, curve.GetLocalPos(0.5f, &position)); Vec(position, 2, 0, 0);
    b.GetReservedVector(&position); Vec(position, 4, 0, 0);
}
TEST_F(CurveGeometryTest, LocalPointCacheSurvivesCollapsedWorldScaleThroughParentReferential) {
    GeometryCurveProbe curve(Context); RCKCurvePoint a(Context), b(Context);
    Attach(curve, a, 0, 0, 0); Attach(curve, b, 4, 0, 0);
    const VxVector scale(0, 2, 3); curve.SetScale(&scale);
    const VxVector local(4, 0, 0); b.SetPosition(&local, Entity(curve));
    EXPECT_EQ(4.0f, curve.GetLength()); VxVector position;
    ASSERT_EQ(CK_OK, curve.GetLocalPos(0.5f, &position)); Vec(position, 2, 0, 0);
    ASSERT_EQ(CK_OK, curve.GetPos(0.5f, &position)); Vec(position, 0, 0, 0);
}

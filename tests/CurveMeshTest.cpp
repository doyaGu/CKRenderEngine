#include <gtest/gtest.h>
#include <string>
#include "CKAll.h"
#include "RCKCurve.h"
#include "RCKCurvePoint.h"
#include "RCKMesh.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class MeshCurve : public RCKCurve {
public:
    explicit MeshCurve(CKContext *context, CKSTRING name = nullptr) : RCKCurve(context, name) {}
    void CachedLength(float length) { m_Length = length; ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); }
    CKDWORD PackedColor() const { return m_Color; }
};
class RenderCurve : public MeshCurve {
public:
    explicit RenderCurve(CKContext *context) : MeshCurve(context), Updates(0) {}
    void Update() override { ++Updates; RCKCurve::Update(); }
    int Updates;
};
static CKObject *FailMeshFactory(CKContext *) { return nullptr; }
class MeshFactoryFailure {
public:
    MeshFactoryFailure() : Descriptor(CKGetClassDesc(CKCID_MESH)), Previous(Descriptor->CreationFct) {
        Descriptor->CreationFct = FailMeshFactory;
    }
    ~MeshFactoryFailure() { Descriptor->CreationFct = Previous; }
private:
    CKClassDesc *Descriptor;
    CKCLASSCREATIONFCT Previous;
};
class CurveMeshTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKCurvePoint, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKCurve, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKCurvePoint *Point(RCKCurvePoint &point) { return reinterpret_cast<CKCurvePoint *>(&point); }
    static void Vec(const VxVector &v, float x, float y, float z) {
        EXPECT_NEAR(x, v.x, 0.0001f); EXPECT_NEAR(y, v.y, 0.0001f); EXPECT_NEAR(z, v.z, 0.0001f);
    }
    static void Position(CKMesh &mesh, int index, float x, float y, float z) {
        VxVector v; mesh.GetVertexPosition(index, &v); Vec(v, x, y, z);
    }
    static void CachePoint(RCKCurvePoint &point, float x, float length, CKBOOL linear) {
        VxVector position(x, 0.0f, 0.0f), zero(0.0f, 0.0f, 0.0f);
        point.SetFittedVector(&position); point.SetReservedVector(&position);
        point.SetCurveLength(length); point.UseTCB(FALSE); point.SetLinear(linear); point.SetTangents(&zero, &zero);
    }
    static void Lines(CKMesh &mesh, int count) {
        EXPECT_EQ(count, mesh.GetLineCount()); EXPECT_EQ(count + 1, mesh.GetVertexCount());
        for (int i = 0; i < count; ++i) {
            int a = -1, b = -1; mesh.GetLine(i, &a, &b); EXPECT_EQ(i, a); EXPECT_EQ(i + 1, b);
        }
    }
};
}

TEST_F(CurveMeshTest, StepCountStoresRequestedValueAndInvalidatesWithoutBuildingMesh) {
    MeshCurve curve(Context); EXPECT_EQ(100, curve.GetStepCount());
    const int values[] = {0, 1, 17, -2};
    for (int value : values) {
        curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
        EXPECT_EQ(CK_OK, curve.SetStepCount(value)); EXPECT_EQ(value, curve.GetStepCount());
        EXPECT_FALSE(curve.IsUpToDate()); EXPECT_EQ(nullptr, curve.GetCurrentMesh());
    }
}
TEST_F(CurveMeshTest, CreateLineMeshUsesCurveNameAndRegistersGeneratedResource) {
    MeshCurve curve(Context, const_cast<CKSTRING>("Path")); ASSERT_EQ(CK_OK, curve.CreateLineMesh());
    CKMesh *mesh = curve.GetCurrentMesh(); ASSERT_NE(nullptr, mesh);
    EXPECT_STREQ("PathLineMesh", mesh->GetName()); EXPECT_EQ(CKCID_MESH, mesh->GetClassID());
    EXPECT_EQ(mesh, Context->GetObject(mesh->GetID())); EXPECT_EQ(1, curve.GetMeshCount());
    EXPECT_EQ(CK_OBJECT_NOTTOBELISTEDANDSAVED, mesh->GetObjectFlags() & CK_OBJECT_NOTTOBELISTEDANDSAVED);
}
TEST_F(CurveMeshTest, UnnamedAndLongNamedMeshesUseSafeCompleteNames) {
    MeshCurve unnamed(Context); ASSERT_EQ(CK_OK, unnamed.CreateLineMesh()); EXPECT_STREQ("LineMesh", unnamed.GetCurrentMesh()->GetName());
    std::string name(400, 'c'); MeshCurve named(Context, const_cast<CKSTRING>(name.c_str()));
    ASSERT_EQ(CK_OK, named.CreateLineMesh()); EXPECT_STREQ((name + "LineMesh").c_str(), named.GetCurrentMesh()->GetName());
}
TEST_F(CurveMeshTest, GeneratedMeshMatchesDynamicStatusOfItsCurve) {
    MeshCurve ordinary(Context), dynamic(Context); dynamic.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0);
    ASSERT_EQ(CK_OK, ordinary.CreateLineMesh()); ASSERT_EQ(CK_OK, dynamic.CreateLineMesh());
    EXPECT_FALSE(ordinary.GetCurrentMesh()->IsDynamic()); EXPECT_TRUE(dynamic.GetCurrentMesh()->IsDynamic());
}
TEST_F(CurveMeshTest, RecreatingLineMeshAddsADistinctCurrentResource) {
    MeshCurve curve(Context); ASSERT_EQ(CK_OK, curve.CreateLineMesh()); CKMesh *first = curve.GetCurrentMesh();
    ASSERT_EQ(CK_OK, curve.CreateLineMesh()); EXPECT_NE(first, curve.GetCurrentMesh()); EXPECT_EQ(2, curve.GetMeshCount());
    EXPECT_EQ(first, Context->GetObject(first->GetID()));
}
TEST_F(CurveMeshTest, ActualFactoryFailureLeavesCurrentMeshAndReportsFailure) {
    MeshCurve curve(Context); MeshFactoryFailure failure;
    EXPECT_EQ(CKERR_OUTOFMEMORY, curve.CreateLineMesh()); EXPECT_EQ(nullptr, curve.GetCurrentMesh()); EXPECT_EQ(0, curve.GetMeshCount());
    EXPECT_EQ(CKERR_NOTFOUND, curve.UpdateMesh()); EXPECT_EQ(nullptr, curve.GetCurrentMesh());
}
TEST_F(CurveMeshTest, HiddenUpdateMeshDoesNotCreateOrModifyResource) {
    MeshCurve curve(Context); curve.Show(CKHIDE); EXPECT_EQ(CK_OK, curve.UpdateMesh()); EXPECT_EQ(nullptr, curve.GetCurrentMesh());
    ASSERT_EQ(CK_OK, curve.CreateLineMesh()); CKMesh *mesh = curve.GetCurrentMesh(); mesh->SetVertexCount(1);
    VxVector retained(3, 5, 7); mesh->SetVertexPosition(0, &retained);
    EXPECT_EQ(CK_OK, curve.UpdateMesh()); EXPECT_EQ(1, mesh->GetVertexCount()); Position(*mesh, 0, 3, 5, 7);
}
TEST_F(CurveMeshTest, EmptyCurveGeneratesZeroPositionsAndCompleteLineStrip) {
    MeshCurve curve(Context); curve.SetStepCount(4); ASSERT_EQ(CK_OK, curve.UpdateMesh());
    CKMesh *mesh = curve.GetCurrentMesh(); ASSERT_NE(nullptr, mesh); Lines(*mesh, 4);
    for (int i = 0; i < 5; ++i) {
        Position(*mesh, i, 0, 0, 0); EXPECT_EQ(0xffffffffu, mesh->GetVertexColor(i));
        EXPECT_EQ(0u, mesh->GetVertexSpecularColor(i));
    }
    EXPECT_FALSE(mesh->IsVisible());
}
TEST_F(CurveMeshTest, SingleControlPointUsesPlaceholderRatherThanItsWorldPosition) {
    MeshCurve curve(Context); RCKCurvePoint point(Context); VxVector p(13, 17, 19); point.SetPosition(&p);
    ASSERT_EQ(CK_OK, curve.AddControlPoint(Point(point))); curve.SetStepCount(2); curve.Update();
    CKMesh *mesh = curve.GetCurrentMesh(); ASSERT_NE(nullptr, mesh); Lines(*mesh, 2);
    for (int i = 0; i < 3; ++i) Position(*mesh, i, 0, 0, 0);
}
TEST_F(CurveMeshTest, ZeroStepCountProducesOnePlaceholderVertexWithoutLines) {
    MeshCurve curve(Context); curve.SetStepCount(0); ASSERT_EQ(CK_OK, curve.UpdateMesh());
    CKMesh *mesh = curve.GetCurrentMesh(); ASSERT_NE(nullptr, mesh); Lines(*mesh, 0); Position(*mesh, 0, 0, 0, 0);
}
TEST_F(CurveMeshTest, OpenLinearCurveUsesOneLinePerSegmentIndependentOfDisplaySteps) {
    MeshCurve curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    curve.AddControlPoint(Point(a)); curve.AddControlPoint(Point(b)); curve.AddControlPoint(Point(c));
    CachePoint(a, 0, 0, TRUE); CachePoint(b, 3, 3, TRUE); CachePoint(c, 7, 7, TRUE); curve.CachedLength(7);
    ASSERT_EQ(CK_OK, curve.UpdateMesh()); CKMesh *mesh = curve.GetCurrentMesh(); ASSERT_NE(nullptr, mesh);
    Lines(*mesh, 2); Position(*mesh, 0, 0, 0, 0); Position(*mesh, 1, 3, 0, 0); Position(*mesh, 2, 7, 0, 0);
    for (int i = 0; i < 3; ++i) EXPECT_EQ(0xff000000u, mesh->GetVertexSpecularColor(i));
}
TEST_F(CurveMeshTest, ClosedLinearCurveEndsAtFirstPointAndIncludesClosingLine) {
    MeshCurve curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    curve.AddControlPoint(Point(a)); curve.AddControlPoint(Point(b)); curve.AddControlPoint(Point(c)); curve.Close();
    CachePoint(a, 0, 0, TRUE); CachePoint(b, 3, 3, TRUE); CachePoint(c, 7, 7, TRUE); curve.CachedLength(14);
    ASSERT_EQ(CK_OK, curve.UpdateMesh()); CKMesh *mesh = curve.GetCurrentMesh(); ASSERT_NE(nullptr, mesh);
    Lines(*mesh, 3); Position(*mesh, 3, 0, 0, 0);
}
TEST_F(CurveMeshTest, CurvedMeshSamplesIndependentHermiteValuesAndEndpoint) {
    MeshCurve curve(Context); RCKCurvePoint a(Context), b(Context);
    curve.AddControlPoint(Point(a)); curve.AddControlPoint(Point(b));
    CachePoint(a, 0, 0, FALSE); CachePoint(b, 4, 4, FALSE); curve.SetStepCount(4); curve.CachedLength(4);
    ASSERT_EQ(CK_OK, curve.UpdateMesh()); CKMesh *mesh = curve.GetCurrentMesh(); ASSERT_NE(nullptr, mesh); Lines(*mesh, 4);
    Position(*mesh, 0, 0, 0, 0); Position(*mesh, 1, .625f, 0, 0); Position(*mesh, 2, 2, 0, 0);
    Position(*mesh, 3, 3.375f, 0, 0); Position(*mesh, 4, 4, 0, 0);
}
TEST_F(CurveMeshTest, CoincidentCurvedPointsProduceFiniteMinimumSegmentMesh) {
    MeshCurve curve(Context); RCKCurvePoint a(Context), b(Context);
    curve.AddControlPoint(Point(a)); curve.AddControlPoint(Point(b)); CachePoint(a, 2, 0, FALSE); CachePoint(b, 2, 0, FALSE); curve.CachedLength(0);
    ASSERT_EQ(CK_OK, curve.UpdateMesh()); CKMesh *mesh = curve.GetCurrentMesh(); ASSERT_NE(nullptr, mesh); Lines(*mesh, 1);
    Position(*mesh, 0, 2, 0, 0); Position(*mesh, 1, 2, 0, 0);
}
TEST_F(CurveMeshTest, SegmentAllocationAndEmissionAgreeAtFloatRoundingBoundary) {
    MeshCurve curve(Context); RCKCurvePoint a(Context), b(Context), c(Context);
    curve.AddControlPoint(Point(a)); curve.AddControlPoint(Point(b)); curve.AddControlPoint(Point(c));
    const float middle = 3.956521511077881f;
    CachePoint(a, 0, 0, FALSE); CachePoint(b, middle, middle, FALSE); CachePoint(c, 7, 7, FALSE);
    curve.SetStepCount(23); curve.CachedLength(7); ASSERT_EQ(CK_OK, curve.UpdateMesh());
    CKMesh *mesh = curve.GetCurrentMesh(); ASSERT_NE(nullptr, mesh);
    // Both segment budgets must use the same arithmetic so every allocated vertex is written.
    const int last = mesh->GetVertexCount() - 1; ASSERT_GT(last, 0); Position(*mesh, last, 7, 0, 0);
    Lines(*mesh, last);
}
TEST_F(CurveMeshTest, RuntimeHidesGeneratedMeshWhileInterfaceModeRetainsVisibility) {
    MeshCurve runtime(Context); ASSERT_EQ(CK_OK, runtime.UpdateMesh()); EXPECT_FALSE(runtime.GetCurrentMesh()->IsVisible());
    Context->SetInterfaceMode(TRUE); MeshCurve editor(Context); ASSERT_EQ(CK_OK, editor.UpdateMesh()); EXPECT_TRUE(editor.GetCurrentMesh()->IsVisible());
}
TEST_F(CurveMeshTest, ColorPackingAndMeshColorsIncludeAlpha) {
    MeshCurve curve(Context); curve.SetStepCount(2); curve.SetColor(VxColor(1.0f, .5f, .25f, .125f));
    EXPECT_EQ(0x1fff7f3fu, curve.PackedColor()); const VxColor color = curve.GetColor();
    EXPECT_FLOAT_EQ(1, color.r); EXPECT_NEAR(127.0f / 255, color.g, .000001f);
    EXPECT_NEAR(63.0f / 255, color.b, .000001f); EXPECT_NEAR(31.0f / 255, color.a, .000001f);
    CKMesh *mesh = curve.GetCurrentMesh(); ASSERT_NE(nullptr, mesh);
    for (int i = 0; i < mesh->GetVertexCount(); ++i) EXPECT_EQ(0x1fff7f3fu, mesh->GetVertexColor(i));
}
TEST_F(CurveMeshTest, ColorChangeRefreshesExistingMeshWithoutReplacingIt) {
    MeshCurve curve(Context); curve.SetStepCount(2); ASSERT_EQ(CK_OK, curve.UpdateMesh()); CKMesh *mesh = curve.GetCurrentMesh();
    curve.SetColor(VxColor(0.0f, 1.0f, 0.0f, 1.0f)); EXPECT_EQ(mesh, curve.GetCurrentMesh());
    for (int i = 0; i < mesh->GetVertexCount(); ++i) EXPECT_EQ(0xff00ff00u, mesh->GetVertexColor(i));
}
TEST_F(CurveMeshTest, HiddenColorChangeRefreshesMeshWhenShownThroughSdk) {
    MeshCurve curve(Context); curve.SetStepCount(2); ASSERT_EQ(CK_OK, curve.UpdateMesh()); CKMesh *mesh = curve.GetCurrentMesh();
    curve.Show(CKHIDE); curve.SetColor(VxColor(0.0f, 1.0f, 0.0f, 1.0f)); EXPECT_EQ(0xffffffffu, mesh->GetVertexColor(0));
    reinterpret_cast<CKCurve *>(&curve)->Show(CKSHOW); EXPECT_TRUE(curve.IsVisible());
    EXPECT_EQ(mesh, curve.GetCurrentMesh()); for (int i = 0; i < mesh->GetVertexCount(); ++i) EXPECT_EQ(0xff00ff00u, mesh->GetVertexColor(i));
}
TEST_F(CurveMeshTest, ShowBuildsVisibleMeshAndHideDoesNotReplaceItsContents) {
    MeshCurve curve(Context); curve.SetStepCount(2); curve.Show(CKHIDE); EXPECT_EQ(nullptr, curve.GetCurrentMesh());
    curve.Show(CKSHOW); CKMesh *mesh = curve.GetCurrentMesh(); ASSERT_NE(nullptr, mesh); Lines(*mesh, 2);
    curve.Show(CKHIDE); EXPECT_FALSE(curve.IsVisible()); EXPECT_EQ(mesh, curve.GetCurrentMesh()); Lines(*mesh, 2);
}
TEST_F(CurveMeshTest, RenderUpdatesDirtyGeometryBeforeInheritedNoMeshReturn) {
    RenderCurve curve(Context); RCKCurvePoint a(Context), b(Context); curve.Show(CKHIDE);
    VxVector first(0, 0, 0), second(3, 4, 0); a.SetPosition(&first); b.SetPosition(&second); a.SetLinear(TRUE);
    curve.AddControlPoint(Point(a)); curve.AddControlPoint(Point(b)); curve.ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    EXPECT_FALSE(curve.Render(nullptr, CKRENDER_UPDATEEXTENTS)); EXPECT_EQ(1, curve.Updates);
    EXPECT_EQ(5, curve.GetLength()); EXPECT_TRUE(curve.IsUpToDate()); EXPECT_EQ(nullptr, curve.GetCurrentMesh());
    EXPECT_FALSE(curve.Render(nullptr, CKRENDER_UPDATEEXTENTS)); EXPECT_EQ(1, curve.Updates);
}

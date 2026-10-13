#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKCurve.h"
#include "RCKCurvePoint.h"
#include "RCKMesh.h"
#include "RCKSkin.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class DependencyCurve : public RCKCurve {
public:
    explicit DependencyCurve(CKContext *context) : RCKCurve(context) {}
    void Append(CKObject *point) { m_ControlPoints.PushBack(point); }
    CKObject *RawPoint(int i) const { return m_ControlPoints[i]; }
    void Tail(CKBOOL open, float length, int steps, float fitting, CKDWORD color) {
        m_Opened = open; m_Length = length; m_StepCount = steps; m_FittingCoeff = fitting; m_Color = color;
    }
    CKBOOL RawOpen() const { return m_Opened; }
    float CachedLength() const { return m_Length; }
    CKDWORD PackedColor() const { return m_Color; }
};
class CurveDependencyMap : public CKDependenciesContext {
public:
    explicit CurveDependencyMap(CKContext *context) : CKDependenciesContext(context) {}
    void Map(CK_ID from, CK_ID to) { m_MapID.Insert(from, to); }
};
class CurvePreparedPoint : public RCKCurvePoint {
public:
    explicit CurvePreparedPoint(CKContext *context) : RCKCurvePoint(context), Calls(0) {}
    int Calls;
    CKERROR PrepareDependencies(CKDependenciesContext &context) override {
        ++Calls; RCKCurvePoint::PrepareDependencies(context); return CKERR_INVALIDPARAMETER;
    }
};
class CurvePreparedMesh : public RCKMesh {
public:
    explicit CurvePreparedMesh(CKContext *context) : RCKMesh(context), Calls(0) {}
    int Calls;
    CKERROR PrepareDependencies(CKDependenciesContext &context) override {
        ++Calls; RCKMesh::PrepareDependencies(context); return CKERR_INVALIDPARAMETER;
    }
};
class CopyOrderCurve : public DependencyCurve {
public:
    explicit CopyOrderCurve(CKContext *context) : DependencyCurve(context), Calls(0), SeenPoint(nullptr), SeenMesh(nullptr), SeenLength(0) {}
    int Calls;
    CKObject *SeenPoint;
    CKMesh *SeenMesh;
    float SeenLength;
    CKERROR CreateLineMesh() override {
        ++Calls; SeenPoint = GetControlPointCount() ? RawPoint(0) : nullptr;
        SeenMesh = GetCurrentMesh(); SeenLength = CachedLength(); return CKERR_INVALIDPARAMETER;
    }
};
class CurveDependenciesTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKCurvePoint, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKCurve, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKCurve *Curve(RCKCurve &curve) { return reinterpret_cast<CKCurve *>(&curve); }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static CKMesh *Mesh(RCKMesh &mesh) { return reinterpret_cast<CKMesh *>(&mesh); }
    static void Options(CKDependencies &options, CKDWORD own = 0, CKDWORD entity = 0) {
        options.Resize(80); for (int i = 0; i < 80; ++i) options[i] = 0;
        options.m_Flags = CK_DEPENDENCIES_CUSTOM; options[CKCID_CURVE] = own; options[CKCID_3DENTITY] = entity;
    }
};
}

TEST_F(CurveDependenciesTest, NoDependencyLabelsOrBitsAreExposedForAnyMode) {
    for (int mode = -2; mode <= 8; ++mode) {
        EXPECT_EQ(0, RCKCurve::GetDependenciesCount(mode));
        EXPECT_EQ(nullptr, RCKCurve::GetDependencies(-1, mode)); EXPECT_EQ(nullptr, RCKCurve::GetDependencies(0, mode));
        EXPECT_EQ(nullptr, RCKCurve::GetDependencies(1, mode));
    }
}
TEST_F(CurveDependenciesTest, PrepareAlwaysIncludesPointsAndCurrentMeshDespiteDisabledClassBits) {
    DependencyCurve curve(Context); RCKCurvePoint a(Context), b(Context); RCKMesh mesh(Context); curve.Append(&a); curve.Append(&b);
    curve.SetCurrentMesh(Mesh(mesh), TRUE);
    const CKDWORD ownBits[] = {0, 1, 0xffff};
    for (CKDWORD bits : ownBits) {
        CKDependencies options; Options(options, bits); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
        ASSERT_EQ(CK_OK, curve.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(curve.GetID()));
        EXPECT_TRUE(dependencies.IsDependenciesHere(a.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(b.GetID()));
        EXPECT_TRUE(dependencies.IsDependenciesHere(mesh.GetID())); dependencies.StopDependencies();
    }
}
TEST_F(CurveDependenciesTest, PrepareIgnoresPointAndMeshErrorsAndFinishesRealGraph) {
    DependencyCurve curve(Context); CurvePreparedPoint point(Context); CurvePreparedMesh mesh(Context);
    curve.Append(nullptr); curve.Append(&point); curve.SetCurrentMesh(Mesh(mesh), FALSE);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, curve.PrepareDependencies(dependencies)); EXPECT_EQ(1, point.Calls); EXPECT_EQ(1, mesh.Calls);
    EXPECT_TRUE(dependencies.IsDependenciesHere(curve.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(point.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(mesh.GetID())); dependencies.StopDependencies();
}
TEST_F(CurveDependenciesTest, PrepareCombinesInheritedHierarchyAndOwnPoints) {
    DependencyCurve curve(Context); RCKCurvePoint point(Context); RCK3dEntity child(Context);
    curve.Append(&point); child.SetParent(Entity(curve), TRUE);
    CKDependencies options; Options(options, 0, 2); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, curve.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(point.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(child.GetID())); dependencies.StopDependencies(); child.SetParent(nullptr, TRUE);
}
TEST_F(CurveDependenciesTest, IsObjectUsedRecognizesOwnPointsOnlyForCurvePointClass) {
    DependencyCurve curve(Context); RCKCurvePoint point(Context), unused(Context); curve.Append(&point);
    EXPECT_TRUE(curve.IsObjectUsed(&point, CKCID_CURVEPOINT)); EXPECT_FALSE(curve.IsObjectUsed(&unused, CKCID_CURVEPOINT));
    const CK_CLASSID classes[] = {CKCID_OBJECT, CKCID_3DENTITY, CKCID_MESH, CKCID_CURVE};
    for (CK_CLASSID cid : classes) EXPECT_FALSE(curve.IsObjectUsed(&point, cid));
}
TEST_F(CurveDependenciesTest, IsObjectUsedDelegatesInheritedMeshReferences) {
    DependencyCurve curve(Context); RCKMesh mesh(Context), unused(Context); curve.AddMesh(Mesh(mesh));
    EXPECT_TRUE(curve.IsObjectUsed(&mesh, CKCID_MESH)); EXPECT_TRUE(curve.IsObjectUsed(&mesh, CKCID_PATCHMESH));
    EXPECT_FALSE(curve.IsObjectUsed(&mesh, CKCID_CURVEPOINT)); EXPECT_FALSE(curve.IsObjectUsed(&unused, CKCID_MESH));
}
TEST_F(CurveDependenciesTest, CheckPreDeletionCompactsFlaggedPointsAndInvalidatesOnlyOnChange) {
    DependencyCurve curve(Context); RCKCurvePoint a(Context), b(Context), c(Context), d(Context);
    curve.Append(&a); curve.Append(&b); curve.Append(&c); curve.Append(&d); curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    curve.CheckPreDeletion(); EXPECT_EQ(4, curve.GetControlPointCount()); EXPECT_TRUE(curve.GetObjectFlags() & CK_OBJECT_UPTODATE);
    a.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); c.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    curve.CheckPreDeletion(); ASSERT_EQ(2, curve.GetControlPointCount()); EXPECT_EQ(&b, curve.RawPoint(0)); EXPECT_EQ(&d, curve.RawPoint(1));
    EXPECT_FALSE(curve.GetObjectFlags() & CK_OBJECT_UPTODATE);
    b.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); d.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    curve.CheckPreDeletion(); EXPECT_EQ(0, curve.GetControlPointCount());
    a.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED); b.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
    c.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED); d.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
}
TEST_F(CurveDependenciesTest, RemapAlwaysRemapsOwnArrayPreservingOrderAndInvalidatesCache) {
    DependencyCurve curve(Context); RCKCurvePoint a(Context), b(Context), mapped(Context); curve.Append(&a); curve.Append(&b);
    curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); CKDependencies options; Options(options);
    CurveDependencyMap dependencies(Context); dependencies.StartDependencies(&options); dependencies.Map(a.GetID(), mapped.GetID());
    ASSERT_EQ(CK_OK, curve.RemapDependencies(dependencies)); ASSERT_EQ(2, curve.GetControlPointCount());
    EXPECT_EQ(&mapped, curve.RawPoint(0)); EXPECT_EQ(&b, curve.RawPoint(1)); EXPECT_FALSE(curve.GetObjectFlags() & CK_OBJECT_UPTODATE);
    EXPECT_EQ(nullptr, mapped.GetCurve()); EXPECT_EQ(nullptr, mapped.GetParent()); dependencies.StopDependencies();
}
TEST_F(CurveDependenciesTest, RemapExplicitZeroUsesPointerArrayNullSlotContract) {
    DependencyCurve curve(Context); RCKCurvePoint point(Context); curve.Append(&point); curve.Append(nullptr);
    CKDependencies options; Options(options); CurveDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(point.GetID(), 0); ASSERT_EQ(CK_OK, curve.RemapDependencies(dependencies)); EXPECT_EQ(2, curve.GetControlPointCount());
    EXPECT_EQ(nullptr, curve.RawPoint(0)); EXPECT_EQ(nullptr, curve.RawPoint(1)); dependencies.StopDependencies();
}
TEST_F(CurveDependenciesTest, CopyTransfersEveryCurveTailFieldAndMakesAnIndependentPointArray) {
    DependencyCurve source(Context), destination(Context); RCKCurvePoint a(Context), b(Context), old(Context);
    source.Append(&a); source.Append(&b); destination.Append(&old); source.Tail(-3, 37.25f, -17, -.625f, 0x23456789);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); ASSERT_EQ(2, destination.GetControlPointCount());
    EXPECT_EQ(&a, destination.RawPoint(0)); EXPECT_EQ(&b, destination.RawPoint(1)); EXPECT_EQ(-3, destination.RawOpen());
    EXPECT_FLOAT_EQ(37.25f, destination.CachedLength()); EXPECT_EQ(-17, destination.GetStepCount());
    EXPECT_FLOAT_EQ(-.625f, destination.GetFittingCoeff()); EXPECT_EQ(0x23456789u, destination.PackedColor());
    destination.RemoveAllControlPoints(); ASSERT_EQ(2, source.GetControlPointCount()); EXPECT_EQ(&a, source.RawPoint(0));
    EXPECT_EQ(nullptr, a.GetCurve()); dependencies.StopDependencies();
}
TEST_F(CurveDependenciesTest, CopyCreatesOwnHiddenUnsavedLineMeshAfterInheritedMeshCopy) {
    DependencyCurve source(Context), destination(Context); RCKMesh sourceMesh(Context); source.SetCurrentMesh(Mesh(sourceMesh), TRUE);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); CKMesh *generated = destination.GetCurrentMesh(); ASSERT_NE(nullptr, generated);
    EXPECT_NE(Mesh(sourceMesh), generated); EXPECT_EQ(Mesh(sourceMesh), source.GetCurrentMesh());
    EXPECT_TRUE(generated->GetObjectFlags() & CK_OBJECT_NOTTOBELISTEDANDSAVED);
    EXPECT_EQ(2, destination.GetMeshCount()); EXPECT_EQ(Mesh(sourceMesh), destination.GetMesh(0)); EXPECT_EQ(generated, destination.GetMesh(1));
    dependencies.StopDependencies();
}
TEST_F(CurveDependenciesTest, CopyCallsVirtualMeshCreationBeforeTailTransferAndIgnoresItsResult) {
    DependencyCurve source(Context); CopyOrderCurve destination(Context); RCKCurvePoint a(Context), old(Context); RCKMesh sourceMesh(Context);
    source.Append(&a); source.Tail(FALSE, 113, 127, .25f, 0x11223344); source.SetCurrentMesh(Mesh(sourceMesh), TRUE);
    destination.Append(&old); destination.Tail(TRUE, 131, 137, .75f, 0x55667788);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_EQ(1, destination.Calls); EXPECT_EQ(&old, destination.SeenPoint);
    EXPECT_EQ(Mesh(sourceMesh), destination.SeenMesh); EXPECT_FLOAT_EQ(131, destination.SeenLength);
    EXPECT_EQ(&a, destination.RawPoint(0)); EXPECT_FLOAT_EQ(113, destination.CachedLength()); EXPECT_EQ(127, destination.GetStepCount());
    dependencies.StopDependencies();
}
TEST_F(CurveDependenciesTest, CopyPreservesInheritedParentWorldTransformAndZOrder) {
    DependencyCurve source(Context), destination(Context); RCK3dEntity parent(Context); source.SetParent(Entity(parent), TRUE);
    VxVector position(7, 11, 13); source.SetPosition(&position, nullptr, FALSE); source.SetZOrder(17);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_EQ(Entity(parent), destination.GetParent()); EXPECT_EQ(17, destination.GetZOrder());
    EXPECT_FLOAT_EQ(7, destination.GetWorldMatrix()[3][0]); EXPECT_FLOAT_EQ(11, destination.GetWorldMatrix()[3][1]); EXPECT_FLOAT_EQ(13, destination.GetWorldMatrix()[3][2]);
    dependencies.StopDependencies(); destination.SetParent(nullptr, TRUE); source.SetParent(nullptr, TRUE);
}
TEST_F(CurveDependenciesTest, SelfCopyPreservesMeshPointOwnershipAndSkinStorage) {
    DependencyCurve curve(Context); RCKCurvePoint point(Context); RCK3dEntity bone(Context);
    ASSERT_EQ(CK_OK, curve.AddControlPoint(reinterpret_cast<CKCurvePoint *>(&point))); ASSERT_EQ(CK_OK, curve.CreateLineMesh());
    CKMesh *mesh = curve.GetCurrentMesh(); CKSkin *skin = curve.CreateSkin(); ASSERT_NE(nullptr, skin);
    skin->SetBoneCount(1); skin->GetBoneData(0)->SetBone(Entity(bone)); curve.Tail(FALSE, 139, 149, .5f, 0x12345678);
    const int meshes = curve.GetMeshCount(); CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, curve.Copy(curve, dependencies)); EXPECT_EQ(mesh, curve.GetCurrentMesh()); EXPECT_EQ(meshes, curve.GetMeshCount());
    EXPECT_EQ(skin, curve.GetSkin()); ASSERT_EQ(1, curve.GetSkin()->GetBoneCount()); EXPECT_EQ(Entity(bone), curve.GetSkin()->GetBoneData(0)->GetBone());
    EXPECT_EQ(&point, curve.RawPoint(0)); EXPECT_EQ(Curve(curve), point.GetCurve()); EXPECT_EQ(Entity(curve), point.GetParent());
    EXPECT_FLOAT_EQ(139, curve.CachedLength()); dependencies.StopDependencies(); point.SetParent(nullptr, TRUE); point.SetCurve(nullptr);
}
TEST_F(CurveDependenciesTest, ActualContextCopyClonesPointsAndRemapsTheirCurveOwnershipWithZeroOwnBits) {
    RCKCurve *source = reinterpret_cast<RCKCurve *>(Context->CreateObject(CKCID_CURVE));
    RCKCurvePoint *point = reinterpret_cast<RCKCurvePoint *>(Context->CreateObject(CKCID_CURVEPOINT)); ASSERT_NE(nullptr, source); ASSERT_NE(nullptr, point);
    VxVector position(7, 11, 13); point->SetPosition(&position, nullptr, FALSE); ASSERT_EQ(CK_OK, source->AddControlPoint(reinterpret_cast<CKCurvePoint *>(point)));
    CKDependencies options; Options(options); RCKCurve *copy = reinterpret_cast<RCKCurve *>(Context->CopyObject(source, &options)); ASSERT_NE(nullptr, copy);
    ASSERT_NE(source, copy); ASSERT_EQ(1, copy->GetControlPointCount()); RCKCurvePoint *copiedPoint = reinterpret_cast<RCKCurvePoint *>(copy->GetControlPoint(0));
    ASSERT_NE(nullptr, copiedPoint); EXPECT_NE(point, copiedPoint); EXPECT_EQ(Curve(*copy), copiedPoint->GetCurve()); EXPECT_EQ(Entity(*copy), copiedPoint->GetParent());
    EXPECT_EQ(Curve(*source), point->GetCurve()); EXPECT_EQ(Entity(*source), point->GetParent());
    EXPECT_FLOAT_EQ(7, copiedPoint->GetWorldMatrix()[3][0]); EXPECT_FLOAT_EQ(11, copiedPoint->GetWorldMatrix()[3][1]); EXPECT_FLOAT_EQ(13, copiedPoint->GetWorldMatrix()[3][2]);
    ASSERT_NE(nullptr, copy->GetCurrentMesh()); EXPECT_TRUE(copy->GetCurrentMesh()->GetObjectFlags() & CK_OBJECT_NOTTOBELISTEDANDSAVED);
}

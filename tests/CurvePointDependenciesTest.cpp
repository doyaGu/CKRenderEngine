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
class DependencyCurvePoint : public RCKCurvePoint {
public:
    explicit DependencyCurvePoint(CKContext *context) : RCKCurvePoint(context) {}
    void Seed(CKCurve *owner) {
        m_Curve = owner; m_Tension = .125f; m_Continuity = -.25f; m_Bias = .375f; m_Length = 97;
        m_ReservedVector = VxVector(41, 43, 47); m_TangentIn = VxVector(2, 3, 5);
        m_m_TangentOut = VxVector(7, 11, 13); m_NotUsedVector = VxVector(53, 59, 61); m_UseTCB = 7; m_Linear = -3;
    }
    CKBOOL RawTCB() const { return m_UseTCB; }
};
class CurvePointDependencyMap : public CKDependenciesContext {
public:
    explicit CurvePointDependencyMap(CKContext *context) : CKDependenciesContext(context) {}
    void Map(CK_ID from, CK_ID to) { m_MapID.Insert(from, to); }
};
class CurvePointDependenciesTest : public ::testing::Test {
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
    static void Options(CKDependencies &options, CKDWORD entity = 0) {
        options.Resize(80); for (int i = 0; i < 80; ++i) options[i] = 0;
        options.m_Flags = CK_DEPENDENCIES_CUSTOM; options[CKCID_3DENTITY] = entity;
    }
    static void Vector(const VxVector &value, float x, float y, float z) {
        EXPECT_FLOAT_EQ(x, value.x); EXPECT_FLOAT_EQ(y, value.y); EXPECT_FLOAT_EQ(z, value.z);
    }
    static void Tail(DependencyCurvePoint &point, CKCurve *owner) {
        EXPECT_EQ(owner, point.GetCurve()); EXPECT_FLOAT_EQ(.125f, point.GetTension()); EXPECT_FLOAT_EQ(-.25f, point.GetContinuity());
        EXPECT_FLOAT_EQ(.375f, point.GetBias()); EXPECT_FLOAT_EQ(97, point.GetLength()); EXPECT_EQ(7, point.RawTCB());
        EXPECT_EQ(-3, point.IsLinear()); VxVector in, out, value; point.GetTangents(&in, &out);
        Vector(in, 2, 3, 5); Vector(out, 7, 11, 13); point.GetReservedVector(&value); Vector(value, 41, 43, 47);
        point.GetFittedVector(&value); Vector(value, 53, 59, 61);
    }
};
}

TEST_F(CurvePointDependenciesTest, RemapAlwaysReconnectsMappedCurveAndKeepsWorldPositionWithZeroClassBits) {
    RCKCurve oldOwner(Context), newOwner(Context); DependencyCurvePoint point(Context); point.SetCurve(Curve(oldOwner));
    VxVector ownerPos(100, 200, 300), pos(7, 11, 13); newOwner.SetPosition(&ownerPos); point.SetPosition(&pos);
    CKDependencies options; Options(options); CurvePointDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(oldOwner.GetID(), newOwner.GetID()); ASSERT_EQ(CK_OK, point.RemapDependencies(dependencies));
    EXPECT_EQ(Curve(newOwner), point.GetCurve()); EXPECT_EQ(Entity(newOwner), point.GetParent());
    EXPECT_EQ(0, oldOwner.GetChildrenCount()); EXPECT_EQ(1, newOwner.GetChildrenCount()); point.GetPosition(&pos); Vector(pos, 7, 11, 13);
    point.GetPosition(&pos, Entity(newOwner)); Vector(pos, -93, -189, -287); dependencies.StopDependencies();
    point.SetCurve(nullptr); point.SetParent(nullptr, TRUE);
}
TEST_F(CurvePointDependenciesTest, RemapUnmappedOwnerRetainsSameOwnerAndCorrectsDifferentParent) {
    RCKCurve owner(Context); RCK3dEntity parent(Context); DependencyCurvePoint point(Context); point.Seed(Curve(owner));
    point.SetParent(Entity(parent), TRUE); VxVector pos(7, 11, 13); point.SetPosition(&pos);
    CKDependencies options; Options(options); CurvePointDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, point.RemapDependencies(dependencies)); EXPECT_EQ(Curve(owner), point.GetCurve()); EXPECT_EQ(Entity(owner), point.GetParent());
    EXPECT_EQ(0, parent.GetChildrenCount()); point.GetPosition(&pos); Vector(pos, 7, 11, 13); Tail(point, Curve(owner));
    dependencies.StopDependencies(); point.SetCurve(nullptr); point.SetParent(nullptr, TRUE);
}
TEST_F(CurvePointDependenciesTest, RemapExplicitZeroOwnerDetachesParentAndKeepsWorldTransform) {
    RCKCurve owner(Context); DependencyCurvePoint point(Context); point.SetCurve(Curve(owner)); VxVector pos(7, 11, 13); point.SetPosition(&pos);
    CKDependencies options; Options(options); CurvePointDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(owner.GetID(), 0); ASSERT_EQ(CK_OK, point.RemapDependencies(dependencies));
    EXPECT_EQ(nullptr, point.GetCurve()); EXPECT_EQ(nullptr, point.GetParent()); EXPECT_EQ(0, owner.GetChildrenCount());
    point.GetPosition(&pos); Vector(pos, 7, 11, 13); dependencies.StopDependencies();
}
TEST_F(CurvePointDependenciesTest, RemapNullOwnerDetachesUnrelatedInheritedParent) {
    RCK3dEntity parent(Context); DependencyCurvePoint point(Context); point.SetParent(Entity(parent), TRUE);
    CKDependencies options; Options(options); CurvePointDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, point.RemapDependencies(dependencies)); EXPECT_EQ(nullptr, point.GetCurve()); EXPECT_EQ(nullptr, point.GetParent());
    EXPECT_EQ(0, parent.GetChildrenCount()); dependencies.StopDependencies();
}
TEST_F(CurvePointDependenciesTest, CopyTransfersEveryTailFieldWithoutRemappingOwnerOrMutatingSource) {
    RCKCurve owner(Context), otherOwner(Context); DependencyCurvePoint source(Context), destination(Context); source.Seed(Curve(owner));
    destination.Seed(Curve(otherOwner)); CKDependencies options; Options(options); CurvePointDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(owner.GetID(), otherOwner.GetID()); ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
    Tail(destination, Curve(owner)); Tail(source, Curve(owner)); EXPECT_EQ(nullptr, destination.GetParent());
    VxVector changed(101, 103, 107); destination.SetTangents(&changed, nullptr); VxVector original;
    source.GetTangents(&original, nullptr); Vector(original, 2, 3, 5); dependencies.StopDependencies();
    source.SetCurve(nullptr); destination.SetCurve(nullptr);
}
TEST_F(CurvePointDependenciesTest, CopyPreservesInheritedParentWorldZOrderAndMeshThenRemapReconnectsOwner) {
    RCKCurve owner(Context), mapped(Context); RCK3dEntity parent(Context); RCKMesh mesh(Context);
    DependencyCurvePoint source(Context), destination(Context); source.Seed(Curve(owner)); source.SetParent(Entity(parent), TRUE);
    VxVector pos(7, 11, 13); source.SetPosition(&pos); source.SetZOrder(29); source.SetCurrentMesh(Mesh(mesh), TRUE);
    CKDependencies options; Options(options); CurvePointDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(owner.GetID(), mapped.GetID()); ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
    EXPECT_EQ(Entity(parent), destination.GetParent()); EXPECT_EQ(Curve(owner), destination.GetCurve()); EXPECT_EQ(29, destination.GetZOrder());
    EXPECT_EQ(Mesh(mesh), destination.GetCurrentMesh()); EXPECT_EQ(1, destination.GetMeshCount()); destination.GetPosition(&pos); Vector(pos, 7, 11, 13);
    ASSERT_EQ(CK_OK, destination.RemapDependencies(dependencies)); EXPECT_EQ(Curve(mapped), destination.GetCurve()); EXPECT_EQ(Entity(mapped), destination.GetParent());
    EXPECT_EQ(Entity(parent), source.GetParent()); destination.GetPosition(&pos); Vector(pos, 7, 11, 13); dependencies.StopDependencies();
    destination.SetCurve(nullptr); destination.SetParent(nullptr, TRUE); source.SetCurve(nullptr); source.SetParent(nullptr, TRUE);
}
TEST_F(CurvePointDependenciesTest, DistinctCopyCreatesIndependentSkinWithBoneVertexAndNormalData) {
    RCK3dEntity bone(Context); DependencyCurvePoint source(Context), destination(Context); source.Seed(nullptr);
    CKSkin *skin = source.CreateSkin(); ASSERT_NE(nullptr, skin); skin->SetBoneCount(1); skin->GetBoneData(0)->SetBone(Entity(bone));
    skin->SetVertexCount(1); CKSkinVertexData *vertex = skin->GetVertexData(0); vertex->SetBoneCount(1); vertex->SetBone(0, 0); vertex->SetWeight(0, .75f);
    VxVector pos(7, 11, 13); vertex->SetInitialPos(pos); skin->SetNormalCount(1); skin->SetNormal(0, VxVector(0, 1, 0));
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); ASSERT_NE(nullptr, destination.GetSkin()); EXPECT_NE(skin, destination.GetSkin());
    CKSkin *copy = destination.GetSkin(); ASSERT_EQ(1, copy->GetBoneCount()); EXPECT_EQ(Entity(bone), copy->GetBoneData(0)->GetBone());
    ASSERT_EQ(1, copy->GetVertexCount()); ASSERT_EQ(1, copy->GetVertexData(0)->GetBoneCount()); EXPECT_EQ(0, copy->GetVertexData(0)->GetBone(0));
    EXPECT_FLOAT_EQ(.75f, copy->GetVertexData(0)->GetWeight(0)); Vector(copy->GetVertexData(0)->GetInitialPos(), 7, 11, 13);
    ASSERT_EQ(1, copy->GetNormalCount()); Vector(copy->GetNormal(0), 0, 1, 0); copy->GetVertexData(0)->SetWeight(0, .25f);
    EXPECT_FLOAT_EQ(.75f, vertex->GetWeight(0)); Tail(destination, nullptr); dependencies.StopDependencies();
}
TEST_F(CurvePointDependenciesTest, SelfCopyPreservesMeshSkinStorageBoneVertexAndPointOwnership) {
    RCKCurve owner(Context); RCK3dEntity bone(Context); RCKMesh mesh(Context); DependencyCurvePoint point(Context);
    point.SetCurve(Curve(owner)); point.Seed(Curve(owner)); point.SetCurrentMesh(Mesh(mesh), TRUE); CKSkin *skin = point.CreateSkin();
    ASSERT_NE(nullptr, skin); skin->SetBoneCount(1); skin->GetBoneData(0)->SetBone(Entity(bone)); skin->SetVertexCount(1);
    CKSkinVertexData *vertex = skin->GetVertexData(0); vertex->SetBoneCount(1); vertex->SetBone(0, 0); vertex->SetWeight(0, .75f);
    VxVector pos(7, 11, 13); vertex->SetInitialPos(pos); CKDependencies options; Options(options);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, point.Copy(point, dependencies));
    EXPECT_EQ(skin, point.GetSkin()); EXPECT_EQ(Mesh(mesh), point.GetCurrentMesh()); EXPECT_EQ(1, point.GetMeshCount());
    ASSERT_EQ(1, point.GetSkin()->GetBoneCount()); EXPECT_EQ(Entity(bone), point.GetSkin()->GetBoneData(0)->GetBone());
    ASSERT_EQ(1, point.GetSkin()->GetVertexCount()); EXPECT_EQ(vertex, point.GetSkin()->GetVertexData(0));
    ASSERT_EQ(1, point.GetSkin()->GetVertexData(0)->GetBoneCount()); EXPECT_FLOAT_EQ(.75f, point.GetSkin()->GetVertexData(0)->GetWeight(0));
    Tail(point, Curve(owner)); EXPECT_EQ(Entity(owner), point.GetParent()); dependencies.StopDependencies(); point.SetCurve(nullptr); point.SetParent(nullptr, TRUE);
}

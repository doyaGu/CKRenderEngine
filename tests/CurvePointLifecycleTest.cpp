#include <gtest/gtest.h>
#include <cstdint>
#include "CKAll.h"
#include "RCKCurve.h"
#include "RCKCurvePoint.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class CurvePointLayoutProbe : public RCKCurvePoint {
public:
    explicit CurvePointLayoutProbe(CKContext *context) : RCKCurvePoint(context) {}
    static void RecordLayout(RCKCurvePoint &point) {
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(&point);
        std::size_t previous = sizeof(RCK3dEntity);
#define FIELD(field) { auto member = &CurvePointLayoutProbe::field; \
        const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(point.*member)); \
        const std::size_t offset = address - begin; EXPECT_GE(offset, previous); \
        EXPECT_LE(offset + sizeof(point.*member), sizeof(RCKCurvePoint)); \
        EXPECT_EQ(0u, address % alignof(decltype(point.*member))); \
        ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(offset)); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(point.*member))); \
        previous = offset + sizeof(point.*member); }
        FIELD(m_Curve); FIELD(m_Tension); FIELD(m_Continuity); FIELD(m_Bias); FIELD(m_Length);
        FIELD(m_ReservedVector); FIELD(m_TangentIn); FIELD(m_m_TangentOut); FIELD(m_NotUsedVector);
        FIELD(m_UseTCB); FIELD(m_Linear);
#undef FIELD
        ::testing::Test::RecordProperty("SDLSize", static_cast<int>(sizeof(RCKCurvePoint)));
        ::testing::Test::RecordProperty("SDLAlignment", static_cast<int>(alignof(RCKCurvePoint)));
        ::testing::Test::RecordProperty("SDLBaseSize", static_cast<int>(sizeof(RCK3dEntity)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
        ::testing::Test::RecordProperty("m_ClassID_value", static_cast<int>(m_ClassID));
        ::testing::Test::RecordProperty("m_ClassID_size", static_cast<int>(sizeof(m_ClassID)));
    }
};
class CurvePointLifecycleTest : public ::testing::Test {
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
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static void Vec(const VxVector &value, float x, float y, float z) {
        EXPECT_FLOAT_EQ(x, value.x); EXPECT_FLOAT_EQ(y, value.y); EXPECT_FLOAT_EQ(z, value.z);
    }
};
}

TEST_F(CurvePointLifecycleTest, RegisteredDefaultsAndAllFieldsActualPlatformLayout) {
    RCKCurvePoint *point = static_cast<RCKCurvePoint *>(Context->CreateObject(CKCID_CURVEPOINT)); ASSERT_NE(nullptr, point);
    EXPECT_EQ(36, point->GetClassID()); EXPECT_TRUE(CKIsChildClassOf(point, CKCID_3DENTITY));
    EXPECT_EQ(point, Context->GetObject(point->GetID())); EXPECT_EQ(nullptr, point->GetCurve());
    EXPECT_EQ(nullptr, point->GetParent()); EXPECT_TRUE(API(*point)->IsTCB()); EXPECT_FALSE(API(*point)->IsLinear());
    EXPECT_EQ(0.0f, point->GetBias()); EXPECT_EQ(0.0f, point->GetTension()); EXPECT_EQ(0.0f, point->GetContinuity());
    EXPECT_EQ(0.0f, point->GetLength()); EXPECT_EQ(nullptr, point->GetCurrentMesh());
    CurvePointLayoutProbe::RecordLayout(*point);
}
TEST_F(CurvePointLifecycleTest, NamedConstructionHasDeterministicZeroCacheAndTangents) {
    char name[] = "Named control point"; RCKCurvePoint point(Context, name); EXPECT_STREQ(name, point.GetName());
    VxVector value(99, 98, 97); point.GetReservedVector(&value); Vec(value, 0, 0, 0);
    point.GetFittedVector(&value); Vec(value, 0, 0, 0); point.GetTangents(&value, nullptr); Vec(value, 0, 0, 0);
    point.GetTangents(nullptr, &value); Vec(value, 0, 0, 0); EXPECT_TRUE(point.IsTCB());
}
TEST_F(CurvePointLifecycleTest, ClassNameAndNativeParameterRegistration) {
    EXPECT_STREQ("Curve Point", RCKCurvePoint::GetClassName());
    CKClassDesc *description = CKGetClassDesc(CKCID_CURVEPOINT); ASSERT_NE(nullptr, description);
    description->Parameter = CKGUID(); RCKCurvePoint::Register(); EXPECT_EQ(CKPGUID_CURVEPOINT, description->Parameter);
    EXPECT_EQ(1556420943u, description->Parameter.d1); EXPECT_EQ(1916020857u, description->Parameter.d2);
}
TEST_F(CurvePointLifecycleTest, ClassHasNoOwnDependencyLabelsForAnyMode) {
    const int modes[] = {-1, 0, 1, 2, 3, 100};
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(0, RCKCurvePoint::GetDependenciesCount(modes[i]));
        EXPECT_EQ(nullptr, RCKCurvePoint::GetDependencies(0, modes[i]));
        EXPECT_EQ(nullptr, RCKCurvePoint::GetDependencies(-1, modes[i]));
    }
}
TEST_F(CurvePointLifecycleTest, DirectFactorySDKDispatchAndVirtualDestruction) {
    RCK3dEntity unrelated(Context); CKCurvePoint *api = RCKCurvePoint::CreateInstance(Context); ASSERT_NE(nullptr, api);
    RCKCurvePoint *point = reinterpret_cast<RCKCurvePoint *>(api); const CK_ID id = point->GetID();
    EXPECT_EQ(CKCID_CURVEPOINT, api->GetClassID()); EXPECT_EQ(nullptr, api->GetName());
    EXPECT_TRUE(api->IsTCB()); api->UseTCB(FALSE); EXPECT_FALSE(api->IsTCB()); api->SetBias(-0.375f);
    EXPECT_EQ(-0.375f, api->GetBias()); EXPECT_EQ(point, Context->GetObject(id));
    delete static_cast<CKObject *>(point); EXPECT_EQ(nullptr, Context->GetObject(id));
    EXPECT_EQ(&unrelated, Context->GetObject(unrelated.GetID()));
}
TEST_F(CurvePointLifecycleTest, MemoryOccupationUsesActualArchitectureTailAndBaseAllocation) {
    RCKCurvePoint point(Context);
    const int tail = static_cast<int>(sizeof(RCKCurvePoint) - sizeof(RCK3dEntity));
    EXPECT_EQ(point.RCK3dEntity::GetMemoryOccupation() + tail, point.GetMemoryOccupation());
    ASSERT_NE(nullptr, point.CreateSkin()); point.GetSkin()->SetBoneCount(2); point.GetSkin()->SetVertexCount(4);
    EXPECT_EQ(point.RCK3dEntity::GetMemoryOccupation() + tail, point.GetMemoryOccupation());
}
TEST_F(CurvePointLifecycleTest, SetCurvePreservesWorldPositionSetsParentWithoutAddingToList) {
    RCKCurve curve(Context); curve.SetPosition3f(10, 20, 30); RCKCurvePoint point(Context); point.SetPosition3f(2, 3, 5);
    point.SetCurve(API(curve)); EXPECT_EQ(API(curve), point.GetCurve()); EXPECT_EQ(Entity(curve), point.GetParent());
    EXPECT_EQ(0, curve.GetControlPointCount()); VxVector value; point.GetPosition(&value); Vec(value, 2, 3, 5);
    point.GetPosition(&value, Entity(curve)); Vec(value, -8, -17, -25);
}
TEST_F(CurvePointLifecycleTest, ClearingCurveRetainsParentAndExistingListMembership) {
    RCKCurve curve(Context); RCKCurvePoint point(Context); ASSERT_EQ(CK_OK, curve.AddControlPoint(API(point)));
    point.SetCurve(nullptr); EXPECT_EQ(nullptr, point.GetCurve()); EXPECT_EQ(Entity(curve), point.GetParent());
    EXPECT_EQ(1, curve.GetControlPointCount()); EXPECT_EQ(API(point), curve.GetControlPoint(0));
}
TEST_F(CurvePointLifecycleTest, ReassigningCurvePreservesSharedListAndNotifiesCurrentOwner) {
    RCKCurve first(Context), second(Context); RCKCurvePoint point(Context);
    ASSERT_EQ(CK_OK, first.AddControlPoint(API(point))); ASSERT_EQ(CK_OK, second.AddControlPoint(API(point)));
    EXPECT_EQ(1, first.GetControlPointCount()); EXPECT_EQ(1, second.GetControlPointCount());
    first.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); second.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    point.NotifyUpdate(); EXPECT_TRUE(first.IsUpToDate()); EXPECT_FALSE(second.IsUpToDate());
    EXPECT_EQ(API(second), point.GetCurve()); EXPECT_EQ(Entity(second), point.GetParent());
}
TEST_F(CurvePointLifecycleTest, DirectDeletionRemovesListedPointAndKeepsCurveSamplingUsable) {
    RCKCurve curve(Context); RCKCurvePoint survivor(Context), endpoint(Context); survivor.SetPosition3f(2, 3, 5);
    endpoint.SetPosition3f(6, 3, 5); survivor.SetLinear(TRUE);
    RCKCurvePoint *point = new RCKCurvePoint(Context); const CK_ID id = point->GetID();
    ASSERT_EQ(CK_OK, curve.AddControlPoint(API(*point))); ASSERT_EQ(CK_OK, curve.AddControlPoint(API(survivor)));
    ASSERT_EQ(CK_OK, curve.AddControlPoint(API(endpoint)));
    curve.GetLength(); ASSERT_TRUE(curve.IsUpToDate()); delete point;
    EXPECT_EQ(nullptr, Context->GetObject(id)); ASSERT_EQ(2, curve.GetControlPointCount());
    EXPECT_EQ(API(survivor), curve.GetControlPoint(0)); EXPECT_FALSE(curve.IsUpToDate()); EXPECT_EQ(4.0f, curve.GetLength());
    VxVector position; EXPECT_EQ(CK_OK, curve.GetLocalPos(0.5f, &position)); Vec(position, 4, 3, 5);
}
TEST_F(CurvePointLifecycleTest, DirectDeletionRemovesPointFromEverySharedCurveList) {
    RCKCurve first(Context), second(Context); RCKCurvePoint *point = new RCKCurvePoint(Context);
    ASSERT_EQ(CK_OK, first.AddControlPoint(API(*point))); ASSERT_EQ(CK_OK, second.AddControlPoint(API(*point)));
    first.GetLength(); second.GetLength(); delete point;
    ASSERT_EQ(0, first.GetControlPointCount()); ASSERT_EQ(0, second.GetControlPointCount());
    EXPECT_FALSE(first.IsUpToDate()); EXPECT_FALSE(second.IsUpToDate());
    EXPECT_EQ(0.0f, first.GetLength()); EXPECT_EQ(0.0f, second.GetLength());
}
TEST_F(CurvePointLifecycleTest, DirectDeletionClearsListWhenCurveOwnerIsAlreadyNull) {
    RCKCurve curve(Context); RCKCurvePoint *point = new RCKCurvePoint(Context);
    ASSERT_EQ(CK_OK, curve.AddControlPoint(API(*point))); point->SetCurve(nullptr); curve.GetLength(); delete point;
    ASSERT_EQ(0, curve.GetControlPointCount()); EXPECT_FALSE(curve.IsUpToDate()); EXPECT_EQ(0.0f, curve.GetLength());
}
TEST_F(CurvePointLifecycleTest, ContextDeletionRemovesPointWithoutDeletingItsCurve) {
    new RCKRenderManager(Context);
    RCKCurve *curve = static_cast<RCKCurve *>(Context->CreateObject(CKCID_CURVE)); ASSERT_NE(nullptr, curve);
    RCKCurvePoint *point = static_cast<RCKCurvePoint *>(Context->CreateObject(CKCID_CURVEPOINT)); ASSERT_NE(nullptr, point);
    ASSERT_EQ(CK_OK, curve->AddControlPoint(API(*point))); const CK_ID curveID = curve->GetID(); const CK_ID pointID = point->GetID();
    CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK, Context->DestroyObject(point, 0, &dependencies)); EXPECT_EQ(nullptr, Context->GetObject(pointID));
    ASSERT_EQ(curve, Context->GetObject(curveID)); EXPECT_EQ(0, curve->GetControlPointCount()); EXPECT_EQ(0.0f, curve->GetLength());
}

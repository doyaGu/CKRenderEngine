#include <gtest/gtest.h>
#include <cstdint>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKCurve.h"
#include "RCKCurvePoint.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class CurveLifecycleProbe : public RCKCurve {
public:
    explicit CurveLifecycleProbe(CKContext *context) : RCKCurve(context) {}
    float StoredLength() const { return m_Length; }
    CKBOOL Loading() const { return m_Loading; }
    int Capacity() const { return m_ControlPoints.Allocated(); }
    void ReservePoints(int count) { m_ControlPoints.Reserve(count); }
    static void RecordLayout(RCKCurve &curve) {
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(&curve);
        std::size_t previous = sizeof(RCK3dEntity);
#define FIELD(field) { auto member = &CurveLifecycleProbe::field; \
        const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(curve.*member)); \
        const std::size_t offset = address - begin; EXPECT_GE(offset, previous); \
        EXPECT_LE(offset + sizeof(curve.*member), sizeof(RCKCurve)); \
        EXPECT_EQ(0u, address % alignof(decltype(curve.*member))); \
        ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(offset)); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(curve.*member))); \
        previous = offset + sizeof(curve.*member); }
        FIELD(m_ControlPoints); FIELD(m_Opened); FIELD(m_Length); FIELD(m_StepCount);
        FIELD(m_FittingCoeff); FIELD(m_Color); FIELD(m_Loading);
#undef FIELD
        ::testing::Test::RecordProperty("SDLSize", static_cast<int>(sizeof(RCKCurve)));
        ::testing::Test::RecordProperty("SDLAlignment", static_cast<int>(alignof(RCKCurve)));
        ::testing::Test::RecordProperty("SDLBaseSize", static_cast<int>(sizeof(RCK3dEntity)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
        ::testing::Test::RecordProperty("m_ClassID_value", static_cast<int>(m_ClassID));
        ::testing::Test::RecordProperty("m_ClassID_size", static_cast<int>(sizeof(m_ClassID)));
    }
};
class CurveLifecycleTest : public ::testing::Test {
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
};
}

TEST_F(CurveLifecycleTest, RegisteredIdentityDefaultsAndActualPlatformLayout) {
    RCKCurve *curve = static_cast<RCKCurve *>(Context->CreateObject(CKCID_CURVE)); ASSERT_NE(nullptr, curve);
    EXPECT_EQ(43, curve->GetClassID()); EXPECT_EQ(curve, Context->GetObject(curve->GetID()));
    EXPECT_TRUE(CKIsChildClassOf(curve, CKCID_3DENTITY)); EXPECT_TRUE(curve->IsOpen());
    EXPECT_EQ(0, curve->GetControlPointCount()); EXPECT_EQ(100, curve->GetStepCount());
    EXPECT_EQ(0.0f, curve->GetFittingCoeff()); EXPECT_EQ(0xffffffffu, curve->GetColor().GetRGBA());
    EXPECT_EQ(nullptr, curve->GetCurrentMesh()); EXPECT_EQ(nullptr, curve->GetParent());
    CurveLifecycleProbe::RecordLayout(*curve);
}
TEST_F(CurveLifecycleTest, NamedConstructorInitializesStoredLengthAndLoadingWithoutCreatingMesh) {
    char name[] = "Named curve"; RCKCurve curve(Context, name); CurveLifecycleProbe probe(Context);
    EXPECT_STREQ(name, curve.GetName()); EXPECT_EQ(&curve, Context->GetObject(curve.GetID()));
    EXPECT_EQ(0.0f, probe.StoredLength()); EXPECT_FALSE(probe.Loading()); EXPECT_EQ(0, probe.Capacity());
    EXPECT_EQ(nullptr, probe.GetCurrentMesh()); EXPECT_TRUE(curve.IsVisible());
}
TEST_F(CurveLifecycleTest, ClassNameMatchesNativeDisplayName) { EXPECT_STREQ("Curve", RCKCurve::GetClassName()); }
TEST_F(CurveLifecycleTest, RegistrationPublishesNativeParameterAndPointNotification) {
    CKClassDesc *description = CKGetClassDesc(CKCID_CURVE); ASSERT_NE(nullptr, description);
    description->Parameter = CKGUID(); RCKCurve::Register();
    EXPECT_EQ(CKPGUID_CURVE, description->Parameter);
    EXPECT_EQ(275911435u, description->Parameter.d1); EXPECT_EQ(529887930u, description->Parameter.d2);
    EXPECT_TRUE(description->ToBeNotify.IsSet(CKCID_CURVEPOINT));
}
TEST_F(CurveLifecycleTest, DirectFactorySDKDispatchAndVirtualDeletionPreserveUnrelatedObjects) {
    RCK3dEntity unrelated(Context); CKCurve *api = RCKCurve::CreateInstance(Context); ASSERT_NE(nullptr, api);
    RCKCurve *curve = reinterpret_cast<RCKCurve *>(api); const CK_ID id = curve->GetID();
    EXPECT_EQ(CKCID_CURVE, api->GetClassID()); EXPECT_EQ(nullptr, api->GetName());
    EXPECT_TRUE(api->IsOpen()); api->Close(); EXPECT_FALSE(api->IsOpen());
    ASSERT_NE(nullptr, curve->CreateSkin()); curve->GetSkin()->SetBoneCount(2); curve->GetSkin()->SetVertexCount(3);
    delete static_cast<CKObject *>(curve);
    EXPECT_EQ(nullptr, Context->GetObject(id)); EXPECT_EQ(&unrelated, Context->GetObject(unrelated.GetID()));
}
TEST_F(CurveLifecycleTest, MemoryOccupationCountsActualPlatformTailAndAllocatedPointStorage) {
    CurveLifecycleProbe curve(Context); RCKCurvePoint first(Context), second(Context);
    const int tail = static_cast<int>(sizeof(RCKCurve) - sizeof(RCK3dEntity));
    EXPECT_EQ(curve.RCK3dEntity::GetMemoryOccupation() + tail, curve.GetMemoryOccupation());
    curve.ReservePoints(16); EXPECT_GE(curve.Capacity(), 16);
    ASSERT_EQ(CK_OK, curve.AddControlPoint(API(first))); ASSERT_EQ(CK_OK, curve.AddControlPoint(API(second)));
    EXPECT_EQ(curve.RCK3dEntity::GetMemoryOccupation() + tail + curve.Capacity() * static_cast<int>(sizeof(CKObject *)), curve.GetMemoryOccupation());
    ASSERT_EQ(CK_OK, curve.RemoveControlPoint(API(first), FALSE));
    EXPECT_EQ(curve.RCK3dEntity::GetMemoryOccupation() + tail + curve.Capacity() * static_cast<int>(sizeof(CKObject *)), curve.GetMemoryOccupation());
    ASSERT_EQ(CK_OK, curve.RemoveAllControlPoints());
    EXPECT_EQ(curve.RCK3dEntity::GetMemoryOccupation() + tail, curve.GetMemoryOccupation());
    first.SetCurve(nullptr); second.SetCurve(nullptr);
}
TEST_F(CurveLifecycleTest, ContextDeletionDeletesListedControlPointsThroughNativeDependencyGraph) {
    new RCKRenderManager(Context);
    RCKCurve *curve = static_cast<RCKCurve *>(Context->CreateObject(CKCID_CURVE)); ASSERT_NE(nullptr, curve);
    RCKCurvePoint *point = static_cast<RCKCurvePoint *>(Context->CreateObject(CKCID_CURVEPOINT)); ASSERT_NE(nullptr, point);
    ASSERT_EQ(CK_OK, curve->AddControlPoint(API(*point))); const CK_ID id = curve->GetID(), pointID = point->GetID();
    CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK, Context->DestroyObject(curve, 0, &dependencies));
    EXPECT_EQ(nullptr, Context->GetObject(id)); EXPECT_EQ(nullptr, Context->GetObject(pointID));
}
TEST_F(CurveLifecycleTest, DeletionAfterRemoveAllReleasesUnlistedSurvivingPointOwner) {
    new RCKRenderManager(Context);
    RCKCurve *curve = static_cast<RCKCurve *>(Context->CreateObject(CKCID_CURVE)); ASSERT_NE(nullptr, curve);
    RCKCurvePoint *point = static_cast<RCKCurvePoint *>(Context->CreateObject(CKCID_CURVEPOINT)); ASSERT_NE(nullptr, point);
    ASSERT_EQ(CK_OK, curve->AddControlPoint(API(*point))); ASSERT_EQ(CK_OK, curve->RemoveAllControlPoints());
    ASSERT_EQ(API(*curve), point->GetCurve()); CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK, Context->DestroyObject(curve, 0, &dependencies));
    ASSERT_EQ(nullptr, point->GetCurve()); point->SetBias(0.375f); EXPECT_EQ(0.375f, point->GetBias());
}
TEST_F(CurveLifecycleTest, DeletionFindsOwnerEvenAfterPointWasReparentedAway) {
    new RCKRenderManager(Context); RCK3dEntity parent(Context);
    RCKCurve *curve = static_cast<RCKCurve *>(Context->CreateObject(CKCID_CURVE)); ASSERT_NE(nullptr, curve);
    RCKCurvePoint point(Context); ASSERT_EQ(CK_OK, curve->AddControlPoint(API(point)));
    point.SetParent(reinterpret_cast<CK3dEntity *>(&parent), TRUE); ASSERT_EQ(CK_OK, curve->RemoveAllControlPoints());
    CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK, Context->DestroyObject(curve, 0, &dependencies));
    ASSERT_EQ(nullptr, point.GetCurve()); EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&parent), point.GetParent());
    point.SetTension(0.25f); EXPECT_EQ(0.25f, point.GetTension());
}
TEST_F(CurveLifecycleTest, DestroyingOldCurvePreservesPointReassignedToAnotherCurve) {
    new RCKRenderManager(Context);
    RCKCurve *old = static_cast<RCKCurve *>(Context->CreateObject(CKCID_CURVE)); ASSERT_NE(nullptr, old);
    RCKCurve other(Context); RCKCurvePoint point(Context);
    ASSERT_EQ(CK_OK, old->AddControlPoint(API(point))); ASSERT_EQ(CK_OK, other.AddControlPoint(API(point)));
    ASSERT_EQ(CK_OK, old->RemoveAllControlPoints()); ASSERT_EQ(API(other), point.GetCurve());
    CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK, Context->DestroyObject(old, 0, &dependencies));
    EXPECT_EQ(API(other), point.GetCurve()); EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&other), point.GetParent());
    other.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); point.SetBias(0.5f); EXPECT_FALSE(other.IsUpToDate());
    point.SetCurve(nullptr);
}
TEST_F(CurveLifecycleTest, DirectDeleteDetachesSurvivingPointOwnerAndParentWhilePreservingWorldPosition) {
    RCKCurvePoint point(Context); point.SetPosition3f(2, 3, 5);
    CKCurve *api = RCKCurve::CreateInstance(Context); ASSERT_NE(nullptr, api);
    RCKCurve *curve = reinterpret_cast<RCKCurve *>(api); curve->SetPosition3f(10, 20, 30);
    ASSERT_EQ(CK_OK, api->AddControlPoint(API(point))); const CK_ID id = curve->GetID();
    delete static_cast<CKObject *>(curve); EXPECT_EQ(nullptr, Context->GetObject(id));
    ASSERT_EQ(nullptr, point.GetCurve()); ASSERT_EQ(nullptr, point.GetParent());
    VxVector position; point.GetPosition(&position);
    EXPECT_FLOAT_EQ(2.0f, position.x); EXPECT_FLOAT_EQ(3.0f, position.y); EXPECT_FLOAT_EQ(5.0f, position.z);
    point.SetPosition3f(7, 11, 13); point.GetPosition(&position);
    EXPECT_FLOAT_EQ(7.0f, position.x); EXPECT_FLOAT_EQ(11.0f, position.y); EXPECT_FLOAT_EQ(13.0f, position.z);
}
TEST_F(CurveLifecycleTest, DirectDeleteDetachesRemovedPointParentEvenWhenItsOwnerIsAlreadyNull) {
    RCKCurvePoint point(Context); point.SetPosition3f(3, 5, 7);
    RCKCurve *curve = new RCKCurve(Context); curve->SetPosition3f(10, 20, 30);
    ASSERT_EQ(CK_OK, curve->AddControlPoint(API(point)));
    ASSERT_EQ(CK_OK, curve->RemoveControlPoint(API(point), FALSE)); ASSERT_EQ(nullptr, point.GetCurve());
    ASSERT_EQ(reinterpret_cast<CK3dEntity *>(curve), point.GetParent());
    delete static_cast<CKObject *>(curve);
    ASSERT_EQ(nullptr, point.GetCurve()); ASSERT_EQ(nullptr, point.GetParent());
    VxVector position; point.GetPosition(&position);
    EXPECT_FLOAT_EQ(3.0f, position.x); EXPECT_FLOAT_EQ(5.0f, position.y); EXPECT_FLOAT_EQ(7.0f, position.z);
    point.SetPosition3f(11, 13, 17); point.GetPosition(&position);
    EXPECT_FLOAT_EQ(11.0f, position.x); EXPECT_FLOAT_EQ(13.0f, position.y); EXPECT_FLOAT_EQ(17.0f, position.z);
}

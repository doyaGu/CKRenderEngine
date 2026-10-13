#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKCurve.h"
#include "RCKCurvePoint.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class BatchLifetimePoint : public RCKCurvePoint {
public:
    BatchLifetimePoint(CKContext *context, CK_ID parentID, bool *parentWasDeleted)
        : RCKCurvePoint(context), ParentID(parentID), ParentWasDeleted(parentWasDeleted) {}
    ~BatchLifetimePoint() override {
        // Observe the real Context deletion order without dereferencing the parent.
        *ParentWasDeleted = m_Context->GetObject(ParentID) == nullptr;
    }
private:
    CK_ID ParentID;
    bool *ParentWasDeleted;
};
class CurvePointParentLifetimeTest : public ::testing::Test {
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
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static CKCurvePoint *Point(RCKCurvePoint &point) { return reinterpret_cast<CKCurvePoint *>(&point); }
    static void Vec(const VxVector &v, float x, float y, float z) {
        EXPECT_NEAR(x, v.x, .0001f); EXPECT_NEAR(y, v.y, .0001f); EXPECT_NEAR(z, v.z, .0001f);
    }
};
}

TEST_F(CurvePointParentLifetimeTest, DirectListedPointDeletionDetachesCurveChildBeforeParentTranslation) {
    RCKCurve curve(Context); RCK3dEntity survivor(Context); curve.SetPosition3f(10, 0, 0); survivor.SetPosition3f(14, 2, 3);
    ASSERT_TRUE(survivor.SetParent(Entity(curve), TRUE)); RCKCurvePoint *point = new RCKCurvePoint(Context);
    ASSERT_EQ(CK_OK, curve.AddControlPoint(Point(*point))); ASSERT_EQ(2, curve.GetChildrenCount());
    const CK_ID pointID = point->GetID(); delete point; EXPECT_EQ(nullptr, Context->GetObject(pointID));
    EXPECT_EQ(0, curve.GetControlPointCount());
    ASSERT_EQ(1, curve.GetChildrenCount()); ASSERT_EQ(Entity(survivor), curve.GetChild(0));
    // ASSERTs prevent executing any propagation through a freed child on the old library.
    curve.SetPosition3f(20, 0, 0, nullptr, FALSE); Vec(survivor.GetWorldMatrix()[3], 24, 2, 3);
}
TEST_F(CurvePointParentLifetimeTest, DirectDeletionDetachesActualEntityParentAndClearsDifferentCurveOwnerList) {
    RCKCurve owner(Context); RCK3dEntity parent(Context), survivor(Context); parent.SetPosition3f(10, 0, 0);
    survivor.SetPosition3f(14, 2, 3); ASSERT_TRUE(survivor.SetParent(Entity(parent), TRUE));
    RCKCurvePoint *point = new RCKCurvePoint(Context); ASSERT_EQ(CK_OK, owner.AddControlPoint(Point(*point)));
    ASSERT_TRUE(point->SetParent(Entity(parent), TRUE)); ASSERT_EQ(reinterpret_cast<CKCurve *>(&owner), point->GetCurve());
    ASSERT_EQ(2, parent.GetChildrenCount()); delete point; EXPECT_EQ(0, owner.GetControlPointCount());
    ASSERT_EQ(1, parent.GetChildrenCount()); ASSERT_EQ(Entity(survivor), parent.GetChild(0));
    VxMatrix matrix = VxMatrix::Identity(); matrix[3].Set(20, 0, 0); parent.SetWorldMatrix(matrix, FALSE);
    Vec(survivor.GetWorldMatrix()[3], 24, 2, 3);
}
TEST_F(CurvePointParentLifetimeTest, NullCurveOwnerStillDetachesRetainedCurveParentBeforeMatrixPropagation) {
    RCKCurve curve(Context); RCK3dEntity survivor(Context); survivor.SetPosition3f(2, 3, 4);
    ASSERT_TRUE(survivor.SetParent(Entity(curve), TRUE)); RCKCurvePoint *point = new RCKCurvePoint(Context);
    ASSERT_EQ(CK_OK, curve.AddControlPoint(Point(*point))); point->SetCurve(nullptr);
    ASSERT_EQ(nullptr, point->GetCurve()); ASSERT_EQ(Entity(curve), point->GetParent()); delete point;
    EXPECT_EQ(0, curve.GetControlPointCount()); ASSERT_EQ(1, curve.GetChildrenCount()); ASSERT_EQ(Entity(survivor), curve.GetChild(0));
    VxMatrix matrix = VxMatrix::Identity(); matrix[0].Set(2, 0, 0); matrix[3].Set(10, 0, 0); curve.SetWorldMatrix(matrix, FALSE);
    Vec(survivor.GetWorldMatrix()[3], 14, 3, 4);
}
TEST_F(CurvePointParentLifetimeTest, LiteralDuplicatePointReferencesAreAllRemovedBeforeFreedPointCanBeSampled) {
    RCKCurve curve(Context); curve.Show(CKHIDE); RCKCurvePoint *point = new RCKCurvePoint(Context);
    CKStateChunk chunk(CKCID_CURVE, nullptr); chunk.StartWrite(); chunk.SetDataVersion(6);
    chunk.WriteIdentifier(0xffc00000u); chunk.WriteInt(3);
    chunk.WriteInt(point->GetID()); chunk.WriteInt(point->GetID()); chunk.WriteInt(point->GetID());
    chunk.WriteFloat(0); chunk.WriteInt(100); chunk.WriteInt(1); chunk.CloseChunk();
    ASSERT_EQ(CK_OK, curve.Load(&chunk, nullptr)); ASSERT_EQ(3, curve.GetControlPointCount());
    ASSERT_EQ(Point(*point), curve.GetControlPoint(0)); ASSERT_EQ(Point(*point), curve.GetControlPoint(1));
    ASSERT_EQ(Point(*point), curve.GetControlPoint(2)); curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    delete point; ASSERT_EQ(0, curve.GetControlPointCount()); EXPECT_FALSE(curve.IsUpToDate());
    EXPECT_EQ(0, curve.GetLength()); VxVector untouched(7, 11, 13);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.GetLocalPos(.5f, &untouched)); Vec(untouched, 7, 11, 13);
}
TEST_F(CurvePointParentLifetimeTest, ContextBatchDeletesOrdinaryEntityParentBeforePointWithoutDereferencingDeadParent) {
    new RCKRenderManager(Context);
    RCK3dEntity *parent = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY)); ASSERT_NE(nullptr, parent);
    bool parentWasDeleted = false; BatchLifetimePoint *point = new BatchLifetimePoint(Context, parent->GetID(), &parentWasDeleted);
    ASSERT_TRUE(point->SetParent(Entity(*parent), TRUE)); ASSERT_EQ(1, parent->GetChildrenCount());
    const CK_ID parentID = parent->GetID(), pointID = point->GetID(); CK_ID ids[2] = {parentID, pointID};
    CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK, Context->DestroyObjects(ids, 2, 0, &dependencies));
    EXPECT_TRUE(parentWasDeleted); EXPECT_EQ(nullptr, Context->GetObject(parentID)); EXPECT_EQ(nullptr, Context->GetObject(pointID));
}
TEST_F(CurvePointParentLifetimeTest, DirectPointDeletionDetachesSurvivingChildWithWorldPosePreserved) {
    RCKCurve owner(Context); RCK3dEntity child(Context); RCKCurvePoint *point = new RCKCurvePoint(Context);
    ASSERT_EQ(CK_OK, owner.AddControlPoint(Point(*point))); point->SetPosition3f(1, 2, 3); point->SetScale3f(2, 3, 4);
    VxMatrix world = VxMatrix::Identity(); world[0].Set(0, 2, 0); world[1].Set(-3, 0, 0);
    world[2].Set(0, 0, 4); world[3].Set(7, 11, 13); child.SetWorldMatrix(world);
    ASSERT_TRUE(child.SetParent(Entity(*point), TRUE)); ASSERT_EQ(1, point->GetChildrenCount());
    ASSERT_EQ(Entity(child), point->GetChild(0)); const CK_ID pointID = point->GetID(); delete point;
    EXPECT_EQ(nullptr, Context->GetObject(pointID)); EXPECT_EQ(0, owner.GetControlPointCount());
    ASSERT_EQ(nullptr, child.GetParent());
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            EXPECT_FLOAT_EQ(world[row][column], child.GetWorldMatrix()[row][column]);
            EXPECT_FLOAT_EQ(world[row][column], child.GetLocalMatrix()[row][column]);
        }
    }
    child.SetPosition3f(-3, 4, 5); Vec(child.GetWorldMatrix()[3], -3, 4, 5); Vec(child.GetLocalMatrix()[3], -3, 4, 5);
    VxMatrix replacement = VxMatrix::Identity(); replacement[0].Set(2, 0, 0); replacement[1].Set(0, 3, 0);
    replacement[2].Set(0, 0, 4); replacement[3].Set(10, 20, 30); child.SetWorldMatrix(replacement);
    VxVector input(1, 2, 3), result; child.Transform(&result, &input); Vec(result, 12, 26, 42);
    EXPECT_EQ(nullptr, child.GetParent());
}

#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKRenderObject.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class Dependency2dEntity : public RCK2dEntity {
public:
    explicit Dependency2dEntity(CKContext *context) : RCK2dEntity(context) {}
    const VxRect *OwnedRect() const { return m_HomogeneousRect; }
};
class Entity2dDependencyMap : public CKDependenciesContext {
public:
    explicit Entity2dDependencyMap(CKContext *context) : CKDependenciesContext(context) {}
    void Map(CKObject &from, CKObject *to) { m_MapID.Insert(from.GetID(), to ? to->GetID() : 0); }
};
class Entity2dDependenciesTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CK2dEntity *API(RCK2dEntity &entity) { return reinterpret_cast<CK2dEntity *>(&entity); }
    static void Options(CKDependencies &options, CKDWORD entity) {
        options.Resize(80); for (int i = 0; i < 80; ++i) options[i] = 0;
        options.m_Flags = CK_DEPENDENCIES_CUSTOM; options[CKCID_2DENTITY] = entity;
    }
    static void ExpectRect(const VxRect &rect, float left, float top, float right, float bottom) {
        EXPECT_FLOAT_EQ(left, rect.left); EXPECT_FLOAT_EQ(top, rect.top);
        EXPECT_FLOAT_EQ(right, rect.right); EXPECT_FLOAT_EQ(bottom, rect.bottom);
    }
};
}

TEST_F(Entity2dDependenciesTest, MaterialReferenceIsBorrowedAndCanBeReplacedOrCleared) {
    RCK2dEntity entity(Context); RCKMaterial first(Context), second(Context);
    EXPECT_EQ(nullptr, entity.GetMaterial()); entity.SetMaterial(&first); EXPECT_EQ(&first, entity.GetMaterial());
    entity.SetMaterial(&first); EXPECT_EQ(&first, entity.GetMaterial()); entity.SetMaterial(&second); EXPECT_EQ(&second, entity.GetMaterial());
    entity.SetMaterial(nullptr); EXPECT_EQ(nullptr, entity.GetMaterial()); EXPECT_EQ(CKCID_MATERIAL, first.GetClassID());
}

TEST_F(Entity2dDependenciesTest, PrepareCustomBitsSelectMaterialAndRecursiveChildrenWithoutParents) {
    RCK2dEntity parent(Context), owner(Context), child(Context), grandchild(Context); RCKMaterial material(Context);
    owner.SetParent(API(parent)); child.SetParent(API(owner)); grandchild.SetParent(API(child)); owner.SetMaterial(&material);
    for (CKDWORD bits = 0; bits < 4; ++bits) {
        CKDependencies options; Options(options, bits); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
        ASSERT_EQ(CK_OK, owner.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(owner.GetID()));
        EXPECT_EQ((bits & 1) != 0, dependencies.IsDependenciesHere(material.GetID()) != FALSE);
        EXPECT_EQ((bits & 2) != 0, dependencies.IsDependenciesHere(child.GetID()) != FALSE);
        EXPECT_EQ((bits & 2) != 0, dependencies.IsDependenciesHere(grandchild.GetID()) != FALSE);
        EXPECT_FALSE(dependencies.IsDependenciesHere(parent.GetID())); dependencies.StopDependencies();
    }
    grandchild.SetParent(nullptr); child.SetParent(nullptr); owner.SetParent(nullptr);
}

TEST_F(Entity2dDependenciesTest, PrepareEmptyReferencesAndRepeatedCallsFinishWithoutDuplicateObjects) {
    RCK2dEntity entity(Context); CKDependencies options; Options(options, 3); CKDependenciesContext dependencies(Context);
    dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, entity.PrepareDependencies(dependencies));
    EXPECT_NE(CK_OK, entity.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(entity.GetID()));
    dependencies.StopDependencies();
}

TEST_F(Entity2dDependenciesTest, PrepareSaveExclusionPropagatesBaseFailureWithoutFollowingReferences) {
    RCK2dEntity entity(Context), child(Context); RCKMaterial material(Context); child.SetParent(API(entity)); entity.SetMaterial(&material);
    entity.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0); CKDependencies options; Options(options, 3);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options); dependencies.SetOperationMode(CK_DEPENDENCIES_SAVE);
    EXPECT_EQ(CKERR_INCOMPATIBLEPARAMETERS, entity.PrepareDependencies(dependencies));
    EXPECT_FALSE(dependencies.IsDependenciesHere(entity.GetID())); EXPECT_FALSE(dependencies.IsDependenciesHere(child.GetID()));
    EXPECT_FALSE(dependencies.IsDependenciesHere(material.GetID())); child.SetParent(nullptr); dependencies.StopDependencies();
}

TEST_F(Entity2dDependenciesTest, RemapBitsIndependentlyReplaceMaterialAndParentAndKeepChildren) {
    RCK2dEntity parent(Context), newParent(Context); RCKMaterial material(Context), newMaterial(Context);
    for (CKDWORD bits = 0; bits < 4; ++bits) {
        RCK2dEntity entity(Context), child(Context); entity.SetParent(API(parent)); child.SetParent(API(entity)); entity.SetMaterial(&material);
        CKDependencies options; Options(options, bits); Entity2dDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
        dependencies.Map(parent, &newParent); dependencies.Map(material, &newMaterial); ASSERT_EQ(CK_OK, entity.RemapDependencies(dependencies));
        EXPECT_EQ((bits & 1) ? &newMaterial : &material, entity.GetMaterial()); EXPECT_EQ(API((bits & 2) ? newParent : parent), entity.GetParent());
        EXPECT_EQ((bits & 2) ? 0 : 1, parent.GetChildrenCount()); EXPECT_EQ((bits & 2) ? 1 : 0, newParent.GetChildrenCount());
        EXPECT_EQ(API(entity), child.GetParent()); EXPECT_EQ(API(child), entity.GetChild(0));
        child.SetParent(nullptr); entity.SetParent(nullptr); dependencies.StopDependencies();
    }
}

TEST_F(Entity2dDependenciesTest, RemapNullMaterialClearsButNullParentMappingRetainsNativeHierarchy) {
    RCK2dEntity parent(Context), entity(Context); RCKMaterial material(Context); entity.SetParent(API(parent)); entity.SetMaterial(&material);
    CKDependencies options; Options(options, 3); Entity2dDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(parent, nullptr); dependencies.Map(material, nullptr); ASSERT_EQ(CK_OK, entity.RemapDependencies(dependencies));
    EXPECT_EQ(nullptr, entity.GetMaterial()); EXPECT_EQ(API(parent), entity.GetParent()); EXPECT_EQ(1, parent.GetChildrenCount());
    entity.SetParent(nullptr); dependencies.StopDependencies();
}

TEST_F(Entity2dDependenciesTest, CopyPreservesNativeFieldsAndParentButDoesNotCopyChildList) {
    RCK2dEntity parent(Context), source(Context), child(Context), destination(Context); RCKMaterial material(Context);
    source.SetParent(API(parent)); child.SetParent(API(source)); source.SetRect(VxRect(3, 5, 23, 45));
    VxRect sourceRect(.2f, .3f, .7f, .9f); source.SetSourceRect(sourceRect); source.SetZOrder(-31); source.SetMaterial(&material);
    source.ModifyFlags(CK_2DENTITY_NOTPICKABLE, 0); CKDependencies options; Options(options, 0);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
    VxRect rect; destination.GetRect(rect); ExpectRect(rect, 3, 5, 23, 45); destination.GetSourceRect(rect); ExpectRect(rect, .2f, .3f, .7f, .9f);
    EXPECT_EQ(source.GetFlags(), destination.GetFlags()); EXPECT_EQ(-31, destination.GetZOrder()); EXPECT_EQ(&material, destination.GetMaterial());
    EXPECT_EQ(API(parent), destination.GetParent()); EXPECT_EQ(2, parent.GetChildrenCount()); EXPECT_EQ(0, destination.GetChildrenCount());
    EXPECT_EQ(API(source), child.GetParent()); destination.SetParent(nullptr); child.SetParent(nullptr); source.SetParent(nullptr); dependencies.StopDependencies();
}

TEST_F(Entity2dDependenciesTest, CopyOwnsIndependentHomogeneousRectThroughActualSourceDestructor) {
    RCK2dEntity parent(Context); parent.SetRect(VxRect(0, 0, 100, 200)); Dependency2dEntity destination(Context); destination.SetParent(API(parent));
    CKDependencies options; Options(options, 0); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    {
        Dependency2dEntity source(Context); source.SetParent(API(parent)); source.SetHomogeneousCoordinates(TRUE);
        ASSERT_EQ(CK_OK, source.SetHomogeneousRect(VxRect(.1f, .2f, .5f, .6f))); ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
        ASSERT_NE(nullptr, destination.OwnedRect()); EXPECT_NE(source.OwnedRect(), destination.OwnedRect());
        source.SetHomogeneousRect(VxRect(.2f, .3f, .7f, .8f)); ExpectRect(*destination.OwnedRect(), .1f, .2f, .5f, .6f);
        source.SetParent(nullptr);
    }
    ExpectRect(*destination.OwnedRect(), .1f, .2f, .5f, .6f); destination.SetParent(nullptr); dependencies.StopDependencies();
}

TEST_F(Entity2dDependenciesTest, CopyPlainSourceReleasesPreviousOwnedHomogeneousRect) {
    RCK2dEntity parent(Context), source(Context); parent.SetRect(VxRect(0, 0, 100, 200)); Dependency2dEntity destination(Context);
    destination.SetParent(API(parent)); destination.SetHomogeneousCoordinates(TRUE);
    ASSERT_EQ(CK_OK, destination.SetHomogeneousRect(VxRect(.1f, .2f, .5f, .6f))); ASSERT_NE(nullptr, destination.OwnedRect());
    CKDependencies options; Options(options, 0); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_FALSE(destination.IsHomogeneousCoordinates()); EXPECT_EQ(nullptr, destination.OwnedRect());
    dependencies.StopDependencies();
}

TEST_F(Entity2dDependenciesTest, SelfCopyPreservesOwnedCoordinatesIdentityAndSingleHierarchyEntry) {
    RCK2dEntity parent(Context); Dependency2dEntity entity(Context); parent.SetRect(VxRect(0, 0, 100, 200)); entity.SetParent(API(parent));
    entity.SetHomogeneousCoordinates(TRUE); ASSERT_EQ(CK_OK, entity.SetHomogeneousRect(VxRect(.125f, .25f, .75f, .875f)));
    const VxRect *owned = entity.OwnedRect(); CKDependencies options; Options(options, 0); CKDependenciesContext dependencies(Context);
    dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, entity.Copy(entity, dependencies)); ASSERT_NE(nullptr, entity.OwnedRect());
    EXPECT_EQ(owned, entity.OwnedRect()); ExpectRect(*entity.OwnedRect(), .125f, .25f, .75f, .875f);
    EXPECT_EQ(1, parent.GetChildrenCount()); EXPECT_EQ(API(entity), parent.GetChild(0)); entity.SetParent(nullptr); dependencies.StopDependencies();
}

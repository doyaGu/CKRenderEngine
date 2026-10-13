#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK3dObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class Object3dRegistrationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK3dObject, CKCID_3DENTITY);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }

    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    static RCK3dObject *Implementation(CK3dObject *object) {
        return reinterpret_cast<RCK3dObject *>(object);
    }
};

TEST_F(Object3dRegistrationTest, RegisteredFactoryDispatchesNativeClassIdentityAndName) {
    const int before = Context->GetObjectsCountByClassID(CKCID_3DOBJECT);
    CKObject *object = Context->CreateObject(CKCID_3DOBJECT, "Imported geometry");
    ASSERT_NE(nullptr, object);
    CK3dObject *geometry = CK3dObject::Cast(object);
    ASSERT_NE(nullptr, geometry);
    EXPECT_EQ(CKCID_3DOBJECT, object->GetClassID());
    EXPECT_EQ(41, object->GetClassID());
    EXPECT_EQ(object, Context->GetObject(object->GetID()));
    EXPECT_EQ(before + 1, Context->GetObjectsCountByClassID(CKCID_3DOBJECT));
    EXPECT_STREQ("Imported geometry", geometry->GetName());
    EXPECT_STREQ("3D Object", RCK3dObject::GetClassName());
    CKClassDesc *description = CKGetClassDesc(CKCID_3DOBJECT);
    ASSERT_NE(nullptr, description);
    EXPECT_STREQ("3D Object", description->NameFct());
    EXPECT_EQ(CKCID_3DENTITY, description->Parent);
}

TEST_F(Object3dRegistrationTest, DirectFactoryCreatesDistinctContextIdentitiesAndUnregistersOnDelete) {
    CK3dObject *first = RCK3dObject::CreateInstance(Context);
    CK3dObject *second = RCK3dObject::CreateInstance(Context);
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    EXPECT_NE(first->GetID(), second->GetID());
    EXPECT_EQ(first, reinterpret_cast<CK3dObject *>(Context->GetObject(first->GetID())));
    EXPECT_EQ(second, reinterpret_cast<CK3dObject *>(Context->GetObject(second->GetID())));
    EXPECT_EQ(CKCID_3DOBJECT, first->GetClassID());
    EXPECT_EQ(nullptr, first->GetName());
    const CK_ID firstID = first->GetID(), secondID = second->GetID();
    // Direct construction registers the identity; CreateObject separately
    // finalizes the class list. Delete direct factory instances directly.
    delete Implementation(first);
    EXPECT_EQ(nullptr, Context->GetObject(firstID));
    EXPECT_EQ(second, reinterpret_cast<CK3dObject *>(Context->GetObject(secondID)));
    delete Implementation(second);
    EXPECT_EQ(nullptr, Context->GetObject(secondID));
}

TEST_F(Object3dRegistrationTest, RegisterPublishesNativeParameterAndPreservesDependencyAndNotificationOptions) {
    CKClassDesc *description = CKGetClassDesc(CKCID_3DOBJECT);
    ASSERT_NE(nullptr, description);
    description->Parameter = CKGUID();
    description->ToBeNotify.Clear();
    description->ToBeNotify.Set(CKCID_3DENTITY);
    description->DefaultCopyDependencies = 0x55;
    RCK3dObject::Register();
    EXPECT_EQ(CKPGUID_OBJECT3D, description->Parameter);
    EXPECT_EQ(0x362E4DF8u, description->Parameter.d1);
    EXPECT_EQ(0x17443539u, description->Parameter.d2);
    EXPECT_EQ(0x55u, description->DefaultCopyDependencies);
    for (int classID = 0; classID <= CKCID_3DOBJECT; ++classID) {
        EXPECT_EQ(classID == CKCID_3DENTITY, description->ToBeNotify.IsSet(classID) != 0);
    }
}

TEST_F(Object3dRegistrationTest, DependencyCallbacksAdvertiseNoOwnOptionsForEveryOperationMode) {
    CKClassDesc *description = CKGetClassDesc(CKCID_3DOBJECT);
    ASSERT_NE(nullptr, description);
    const int modes[] = {CK_DEPENDENCIES_COPY, CK_DEPENDENCIES_DELETE, CK_DEPENDENCIES_REPLACE,
                         CK_DEPENDENCIES_SAVE, CK_DEPENDENCIES_BUILD, 0, -1, 99};
    for (int mode : modes) {
        EXPECT_EQ(0, RCK3dObject::GetDependenciesCount(mode));
        EXPECT_EQ(0, description->DependsCountFct(mode));
        for (int index = -1; index < 3; ++index) {
            EXPECT_EQ(nullptr, RCK3dObject::GetDependencies(index, mode));
            EXPECT_EQ(nullptr, description->DependsFct(index, mode));
        }
    }
}

TEST_F(Object3dRegistrationTest, SdkCastAcceptsGeometryAndRejectsBaseEntityAndRenderObject) {
    CKObject *geometry = Context->CreateObject(CKCID_3DOBJECT);
    CKObject *entity = Context->CreateObject(CKCID_3DENTITY);
    CKObject *render = Context->CreateObject(CKCID_RENDEROBJECT);
    ASSERT_NE(nullptr, geometry);
    ASSERT_NE(nullptr, entity);
    ASSERT_NE(nullptr, render);
    EXPECT_EQ(geometry, static_cast<CKObject *>(CK3dObject::Cast(geometry)));
    EXPECT_NE(nullptr, CK3dEntity::Cast(geometry));
    EXPECT_TRUE(CKIsChildClassOf(geometry, CKCID_RENDEROBJECT));
    EXPECT_EQ(nullptr, CK3dObject::Cast(entity));
    EXPECT_EQ(nullptr, CK3dObject::Cast(render));
    EXPECT_EQ(nullptr, CK3dObject::Cast(nullptr));
}

TEST_F(Object3dRegistrationTest, RegisteredFactoryDeletionRestoresClassListAndPreservesIndependentObject) {
    const int before = Context->GetObjectsCountByClassID(CKCID_3DOBJECT);
    CK3dObject *first = CK3dObject::Cast(Context->CreateObject(CKCID_3DOBJECT, "First geometry"));
    CK3dObject *second = CK3dObject::Cast(Context->CreateObject(CKCID_3DOBJECT, "Independent geometry"));
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    EXPECT_EQ(before + 2, Context->GetObjectsCountByClassID(CKCID_3DOBJECT));
    const CK_ID firstID = first->GetID(), secondID = second->GetID();
    CKDependencies dependencies;
    dependencies.m_Flags = CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK, Context->DestroyObject(first, 0, &dependencies));
    EXPECT_EQ(nullptr, Context->GetObject(firstID));
    EXPECT_EQ(second, reinterpret_cast<CK3dObject *>(Context->GetObject(secondID)));
    EXPECT_STREQ("Independent geometry", second->GetName());
    EXPECT_EQ(before + 1, Context->GetObjectsCountByClassID(CKCID_3DOBJECT));
    ASSERT_EQ(CK_OK, Context->DestroyObject(second, 0, &dependencies));
    EXPECT_EQ(nullptr, Context->GetObject(secondID));
    EXPECT_EQ(before, Context->GetObjectsCountByClassID(CKCID_3DOBJECT));
}

} // namespace

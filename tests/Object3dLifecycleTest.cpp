#include <gtest/gtest.h>
#include <cstddef>
#include <type_traits>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK3dObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class Object3dLifecycleTest : public ::testing::Test {
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

    RCK3dObject *Object() {
        return static_cast<RCK3dObject *>(Context->CreateObject(CKCID_3DOBJECT));
    }

    static CK3dEntity *Entity(RCK3dObject *object) {
        return reinterpret_cast<CK3dEntity *>(object);
    }

    static void DestroyOnly(CKContext &context, CKObject *object) {
        CKDependencies dependencies;
        dependencies.m_Flags = CK_DEPENDENCIES_NONE;
        ASSERT_EQ(CK_OK, context.DestroyObject(object, 0, &dependencies));
    }

    static void ExpectTranslation(RCK3dObject &object, float x, float y, float z) {
        VxVector actual;
        object.GetPosition(&actual);
        EXPECT_FLOAT_EQ(x, actual.x);
        EXPECT_FLOAT_EQ(y, actual.y);
        EXPECT_FLOAT_EQ(z, actual.z);
    }
};

TEST_F(Object3dLifecycleTest, NamedConstructorRegistersIdentityAndInheritsInitializedEntityState) {
    CK_ID id = 0;
    {
        char name[] = "Direct named 3D object";
        RCK3dObject object(Context, name);
        id = object.GetID();
        EXPECT_NE(0u, id);
        EXPECT_EQ(&object, Context->GetObject(id));
        EXPECT_STREQ(name, object.GetName());
        name[0] = 'X';
        EXPECT_STREQ("Direct named 3D object", object.GetName());
        EXPECT_EQ(CKCID_3DOBJECT, object.GetClassID());
        EXPECT_EQ(nullptr, object.GetParent());
        EXPECT_EQ(nullptr, object.GetCurrentMesh());
        EXPECT_EQ(nullptr, object.GetSkin());
        EXPECT_EQ(0, object.GetChildrenCount());
        EXPECT_EQ(0, object.GetMeshCount());
        EXPECT_EQ(0, object.GetObjectAnimationCount());
        const VxMatrix &local = object.GetLocalMatrix();
        const VxMatrix &world = object.GetWorldMatrix();
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                const float expected = row == column ? 1.0f : 0.0f;
                EXPECT_FLOAT_EQ(expected, local[row][column]);
                EXPECT_FLOAT_EQ(expected, world[row][column]);
            }
        }
        const VxVector input(2, -3, 4);
        VxVector output;
        object.Transform(&output, &input);
        EXPECT_FLOAT_EQ(input.x, output.x);
        EXPECT_FLOAT_EQ(input.y, output.y);
        EXPECT_FLOAT_EQ(input.z, output.z);
    }
    EXPECT_EQ(nullptr, Context->GetObject(id));
}

TEST_F(Object3dLifecycleTest, DirectFactoriesDeleteThroughVirtualBaseWithoutRemovingAnotherIdentity) {
    CK3dObject *first = RCK3dObject::CreateInstance(Context);
    CK3dObject *second = RCK3dObject::CreateInstance(Context);
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    const CK_ID firstID = first->GetID(), secondID = second->GetID();
    ASSERT_NE(firstID, secondID);
    EXPECT_EQ(first, Context->GetObject(firstID));
    EXPECT_EQ(second, Context->GetObject(secondID));
    CKObject *base = first;
    // Direct construction registers identity; Context::CreateObject owns class
    // list finalization. Direct deletion uses the ordinary virtual destructor.
    delete base;
    EXPECT_EQ(nullptr, Context->GetObject(firstID));
    EXPECT_EQ(second, Context->GetObject(secondID));
    VxVector position(3, 5, 7), actual;
    second->SetPosition(&position);
    second->GetPosition(&actual);
    EXPECT_FLOAT_EQ(3, actual.x);
    EXPECT_FLOAT_EQ(5, actual.y);
    EXPECT_FLOAT_EQ(7, actual.z);
    delete static_cast<CKObject *>(second);
    EXPECT_EQ(nullptr, Context->GetObject(secondID));
}

TEST_F(Object3dLifecycleTest, RegisteredParentDeletionDetachesBorrowedChildAndKeepsItsWorldPose) {
    RCK3dObject *parent = Object(), *child = Object(), *replacement = Object();
    ASSERT_NE(nullptr, parent);
    ASSERT_NE(nullptr, child);
    ASSERT_NE(nullptr, replacement);
    VxVector position(10, 2, -1);
    parent->SetPosition(&position);
    position.Set(15, 5, 7);
    child->SetPosition(&position);
    ASSERT_TRUE(child->SetParent(Entity(parent), TRUE));
    ASSERT_EQ(1, parent->GetChildrenCount());
    const CK_ID parentID = parent->GetID(), childID = child->GetID();
    // NONE disables optional inherited child dependencies. PreDelete detaches
    // this surviving borrowed child before the parent's destructor runs.
    DestroyOnly(*Context, parent);
    EXPECT_EQ(nullptr, Context->GetObject(parentID));
    ASSERT_EQ(child, Context->GetObject(childID));
    EXPECT_EQ(nullptr, child->GetParent());
    ExpectTranslation(*child, 15, 5, 7);
    position.Set(-8, 0, 0);
    replacement->SetPosition(&position);
    ASSERT_TRUE(child->SetParent(Entity(replacement), TRUE));
    ExpectTranslation(*child, 15, 5, 7);
    position.Set(-4, 0, 0);
    replacement->SetPosition(&position);
    ExpectTranslation(*child, 19, 5, 7);
}

TEST_F(Object3dLifecycleTest, RegisteredChildDeletionRemovesParentListEntryWithoutDeletingParent) {
    RCK3dObject *parent = Object(), *child = Object();
    ASSERT_NE(nullptr, parent);
    ASSERT_NE(nullptr, child);
    VxVector position(6, 4, 2);
    parent->SetPosition(&position);
    ASSERT_TRUE(child->SetParent(Entity(parent), TRUE));
    const CK_ID parentID = parent->GetID(), childID = child->GetID();
    DestroyOnly(*Context, child);
    EXPECT_EQ(nullptr, Context->GetObject(childID));
    ASSERT_EQ(parent, Context->GetObject(parentID));
    EXPECT_EQ(0, parent->GetChildrenCount());
    ExpectTranslation(*parent, 6, 4, 2);
    RCK3dObject *later = Object();
    ASSERT_NE(nullptr, later);
    ASSERT_TRUE(later->SetParent(Entity(parent), FALSE));
    ASSERT_EQ(1, parent->GetChildrenCount());
    EXPECT_EQ(Entity(later), parent->GetChild(0));
    ExpectTranslation(*later, 6, 4, 2);
}

TEST_F(Object3dLifecycleTest, ContextCopyConstructsIndependentIdentityAndRetainsRealPoseConsumers) {
    RCK3dObject *source = Object();
    ASSERT_NE(nullptr, source);
    VxMatrix pose = VxMatrix::Identity();
    pose[0][0] = 0; pose[0][1] = 1;
    pose[1][0] = -1; pose[1][1] = 0;
    pose[3][0] = 3; pose[3][1] = 5; pose[3][2] = 7;
    source->SetWorldMatrix(pose);
    RCK3dObject *copy = static_cast<RCK3dObject *>(Context->CopyObject(source));
    ASSERT_NE(nullptr, copy);
    EXPECT_NE(source, copy);
    EXPECT_NE(source->GetID(), copy->GetID());
    EXPECT_EQ(copy, Context->GetObject(copy->GetID()));
    EXPECT_EQ(CKCID_3DOBJECT, copy->GetClassID());
    const VxVector input(2, 0, 1);
    VxVector output;
    copy->Transform(&output, &input);
    EXPECT_FLOAT_EQ(3, output.x);
    EXPECT_FLOAT_EQ(7, output.y);
    EXPECT_FLOAT_EQ(8, output.z);
    VxVector inverse;
    copy->InverseTransform(&inverse, &output);
    EXPECT_FLOAT_EQ(input.x, inverse.x);
    EXPECT_FLOAT_EQ(input.y, inverse.y);
    EXPECT_FLOAT_EQ(input.z, inverse.z);
    VxVector changed(20, 30, 40);
    source->SetPosition(&changed);
    copy->Transform(&output, &input);
    EXPECT_FLOAT_EQ(3, output.x);
    EXPECT_FLOAT_EQ(7, output.y);
    EXPECT_FLOAT_EQ(8, output.z);
    const CK_ID sourceID = source->GetID(), copyID = copy->GetID();
    DestroyOnly(*Context, copy);
    EXPECT_EQ(nullptr, Context->GetObject(copyID));
    ASSERT_EQ(source, Context->GetObject(sourceID));
    ExpectTranslation(*source, 20, 30, 40);
}

TEST_F(Object3dLifecycleTest, CurrentArchitectureAddsNoInstanceStorageAndPreservesVirtualSdkIdentity) {
    RCK3dObject *object = Object();
    ASSERT_NE(nullptr, object);
    EXPECT_EQ(sizeof(RCK3dEntity), sizeof(RCK3dObject));
    EXPECT_EQ(alignof(RCK3dEntity), alignof(RCK3dObject));
    EXPECT_TRUE(std::has_virtual_destructor<RCK3dObject>::value);
    CKObject *base = object;
    RCK3dEntity *entity = object;
    EXPECT_EQ(static_cast<void *>(object), static_cast<void *>(entity));
    EXPECT_EQ(static_cast<void *>(object), static_cast<void *>(base));
    EXPECT_EQ(CKCID_3DOBJECT, base->GetClassID());
    EXPECT_EQ(CKCID_3DOBJECT, RCK3dObject::m_ClassID);
    EXPECT_EQ(CKCID_3DENTITY, object->RCK3dEntity::GetClassID());
    RecordProperty("SDL3dObjectSize", static_cast<int>(sizeof(RCK3dObject)));
    RecordProperty("SDL3dObjectAlignment", static_cast<int>(alignof(RCK3dObject)));
    RecordProperty("SDL3dEntitySize", static_cast<int>(sizeof(RCK3dEntity)));
    RecordProperty("SDL3dEntityAlignment", static_cast<int>(alignof(RCK3dEntity)));
    RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
    RecordProperty("ClassIDStaticValue", static_cast<int>(RCK3dObject::m_ClassID));
}

} // namespace

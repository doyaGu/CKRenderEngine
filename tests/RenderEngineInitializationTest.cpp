#include <gtest/gtest.h>
#include "CKContext.h"
// Exercise the static module variant used by the fixed runtime archive.
#define CKRE_STATIC_RENDER_ENGINE 1
#include "../src/CK2_3D.cpp"

namespace {
struct NativeRegistration {
    int Id, Parent;
    CKCLASSCREATIONFCT Factory;
    CKCLASSREGISTERFCT Register;
    CK_CLASSID *RuntimeId;
};
// IDs and parents come from the original 2.1 module's registration table.
#define NATIVE_REGISTRATION(Type, Id, Parent) \
    {Id, Parent, reinterpret_cast<CKCLASSCREATIONFCT>(&Type::CreateInstance), &Type::Register, &Type::m_ClassID}
const NativeRegistration Registrations[] = {
    NATIVE_REGISTRATION(RCKRenderContext, 12, 1),
    NATIVE_REGISTRATION(RCKKinematicChain, 13, 1),
    NATIVE_REGISTRATION(RCKMaterial, 30, 19),
    NATIVE_REGISTRATION(RCKTexture, 31, 19),
    NATIVE_REGISTRATION(RCKMesh, 32, 19),
    NATIVE_REGISTRATION(RCKPatchMesh, 53, 32),
    NATIVE_REGISTRATION(RCKAnimation, 16, 11),
    NATIVE_REGISTRATION(RCKKeyedAnimation, 18, 16),
    NATIVE_REGISTRATION(RCKObjectAnimation, 15, 11),
    NATIVE_REGISTRATION(RCKLayer, 51, 1),
    NATIVE_REGISTRATION(RCKRenderObject, 47, 19),
    NATIVE_REGISTRATION(RCK2dEntity, 27, 47),
    NATIVE_REGISTRATION(RCK3dEntity, 33, 47),
    NATIVE_REGISTRATION(RCKCamera, 34, 33),
    NATIVE_REGISTRATION(RCKLight, 38, 33),
    NATIVE_REGISTRATION(RCKCurvePoint, 36, 33),
    NATIVE_REGISTRATION(RCKCurve, 43, 33),
    NATIVE_REGISTRATION(RCK3dObject, 41, 33),
    NATIVE_REGISTRATION(RCKSprite3D, 37, 33),
    NATIVE_REGISTRATION(RCKCharacter, 40, 33),
    NATIVE_REGISTRATION(RCKPlace, 22, 33),
    NATIVE_REGISTRATION(RCKGrid, 50, 33),
    NATIVE_REGISTRATION(RCKBodyPart, 42, 41),
    NATIVE_REGISTRATION(RCKTargetCamera, 35, 34),
    NATIVE_REGISTRATION(RCKTargetLight, 39, 38),
    NATIVE_REGISTRATION(RCKSprite, 28, 27),
    NATIVE_REGISTRATION(RCKSpriteText, 29, 28)
};
#undef NATIVE_REGISTRATION
const int RegistrationCount = sizeof(Registrations) / sizeof(Registrations[0]);

int NativeParent(int id) {
    for (int i = 0; i < RegistrationCount; ++i)
        if (Registrations[i].Id == id) return Registrations[i].Parent;
    if (id == 11) return 19; // CKSceneObject -> CKBeObject -> CKObject.
    if (id == 19) return 1;
    return 0;
}

class RenderEngineInitializationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr, *Other = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        InitializeCK2_3D();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override {
        if (Other) CKCloseContext(Other);
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
};

TEST_F(RenderEngineInitializationTest, AllNativeRegistrationsHaveMatchingIdsParentsAndFactoryHooks) {
    ASSERT_EQ(27, RegistrationCount);
    for (int i = 0; i < RegistrationCount; ++i) {
        SCOPED_TRACE(Registrations[i].Id);
        const NativeRegistration &expected = Registrations[i];
        CKClassDesc *desc = CKGetClassDesc(expected.Id); ASSERT_NE(nullptr, desc);
        EXPECT_EQ(expected.Id, *expected.RuntimeId); EXPECT_EQ(expected.Parent, desc->Parent);
        EXPECT_EQ(expected.Factory, desc->CreationFct); EXPECT_EQ(expected.Register, desc->RegisterFct);
        EXPECT_NE(nullptr, desc->NameFct); EXPECT_NE(nullptr, desc->DependsFct); EXPECT_NE(nullptr, desc->DependsCountFct);
        EXPECT_EQ(expected.Parent, CKGetParentClassID(expected.Id));
    }
}

TEST_F(RenderEngineInitializationTest, HierarchyRecognizesAllTransitiveParentsAndRejectsOtherBranches) {
    for (int i = 0; i < RegistrationCount; ++i) {
        const int child = Registrations[i].Id;
        for (int j = 0; j < RegistrationCount; ++j) {
            const int candidate = Registrations[j].Id;
            if (child == candidate) continue;
            bool expected = false;
            for (int parent = NativeParent(child); parent != 0; parent = NativeParent(parent))
                if (parent == candidate) expected = true;
            SCOPED_TRACE(child);
            SCOPED_TRACE(candidate);
            EXPECT_EQ(expected, CKIsChildClassOf(child, candidate) != FALSE);
        }
    }
    EXPECT_TRUE(CKIsChildClassOf(29, 19)); EXPECT_TRUE(CKIsChildClassOf(18, 1));
}

TEST_F(RenderEngineInitializationTest, EveryRegisteredFactoryCreatesTheRequestedClassAndCanBeDestroyed) {
    ASSERT_EQ(CK_OK, InitInstanceFct(Context));
    CKObject *objects[27] = {}; CK_ID ids[27] = {};
    for (int i = 0; i < RegistrationCount; ++i) {
        SCOPED_TRACE(Registrations[i].Id);
        char name[64]; snprintf(name, sizeof(name), "registered_factory_%d", Registrations[i].Id);
        objects[i] = Context->CreateObject(Registrations[i].Id, name, CK_OBJECTCREATION_NONAMECHECK);
        ASSERT_NE(nullptr, objects[i]); ids[i] = objects[i]->GetID();
        EXPECT_EQ(Registrations[i].Id, objects[i]->GetClassID());
        EXPECT_EQ(objects[i], Context->GetObject(ids[i]));
    }
    for (int i = RegistrationCount - 1; i >= 0; --i) {
        SCOPED_TRACE(Registrations[i].Id);
        ASSERT_EQ(CK_OK, Context->DestroyObject(objects[i]));
        EXPECT_EQ(nullptr, Context->GetObject(ids[i]));
    }
}

TEST_F(RenderEngineInitializationTest, RepeatedRegistrationPreservesFactoriesAndLiveObjectState) {
    ASSERT_EQ(CK_OK, InitInstanceFct(Context));
    RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(32, "kept_mesh"));
    RCKCamera *camera = static_cast<RCKCamera *>(Context->CreateObject(34, "kept_camera"));
    ASSERT_NE(nullptr, mesh); ASSERT_NE(nullptr, camera);
    ASSERT_TRUE(mesh->SetVertexCount(4)); camera->SetFrontPlane(3.5f);
    const CK_ID meshId = mesh->GetID(), cameraId = camera->GetID();
    const int managers = Context->GetManagerCount(), objects = Context->GetObjectCount();
    for (int cycle = 0; cycle < 3; ++cycle) InitializeCK2_3D();
    EXPECT_EQ(managers, Context->GetManagerCount()); EXPECT_EQ(objects, Context->GetObjectCount());
    EXPECT_EQ(mesh, Context->GetObject(meshId)); EXPECT_EQ(camera, Context->GetObject(cameraId));
    EXPECT_EQ(4, mesh->GetVertexCount()); EXPECT_FLOAT_EQ(3.5f, camera->GetFrontPlane());
    for (int i = 0; i < RegistrationCount; ++i) {
        CKClassDesc *desc = CKGetClassDesc(Registrations[i].Id); ASSERT_NE(nullptr, desc);
        EXPECT_EQ(Registrations[i].Factory, desc->CreationFct);
        EXPECT_EQ(Registrations[i].Parent, desc->Parent);
    }
}

TEST_F(RenderEngineInitializationTest, InstanceEntryAddsOneDiscoverableManagerAndUsableRenderContext) {
    ASSERT_EQ(nullptr, Context->GetRenderManager());
    const int count = Context->GetManagerCount();
    ASSERT_EQ(CK_OK, InitInstanceFct(Context));
    RCKRenderManager *manager = static_cast<RCKRenderManager *>(Context->GetRenderManager()); ASSERT_NE(nullptr, manager);
    EXPECT_EQ(count + 1, Context->GetManagerCount());
    int found = 0;
    for (int i = 0; i < Context->GetManagerCount(); ++i) found += Context->GetManager(i) == manager;
    EXPECT_EQ(1, found); ASSERT_NE(nullptr, manager->m_2DRootFore); ASSERT_NE(nullptr, manager->m_2DRootBack);
    EXPECT_EQ(27, manager->m_2DRootFore->GetClassID()); EXPECT_EQ(27, manager->m_2DRootBack->GetClassID());
    CKObject *renderContext = Context->CreateObject(12, "initialized_context");
    ASSERT_NE(nullptr, renderContext); EXPECT_EQ(12, renderContext->GetClassID());
}

TEST_F(RenderEngineInitializationTest, ManagersAndResourcesRemainIndependentAcrossTwoContexts) {
    ASSERT_EQ(CK_OK, CKCreateContext(&Other, nullptr, 0, 0));
    ASSERT_EQ(CK_OK, InitInstanceFct(Context)); ASSERT_EQ(CK_OK, InitInstanceFct(Other));
    RCKRenderManager *first = static_cast<RCKRenderManager *>(Context->GetRenderManager());
    RCKRenderManager *second = static_cast<RCKRenderManager *>(Other->GetRenderManager());
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); EXPECT_NE(first, second);
    EXPECT_NE(first->m_2DRootFore, second->m_2DRootFore);
    first->m_DisableSpecular.Value = 7; second->m_DisableSpecular.Value = 0;
    RCKMaterial *firstMaterial = static_cast<RCKMaterial *>(Context->CreateObject(30, "own_material"));
    RCKMaterial *secondMaterial = static_cast<RCKMaterial *>(Other->CreateObject(30, "own_material"));
    ASSERT_NE(nullptr, firstMaterial); ASSERT_NE(nullptr, secondMaterial); EXPECT_NE(firstMaterial, secondMaterial);
    firstMaterial->SetPower(3); secondMaterial->SetPower(9);
    EXPECT_FLOAT_EQ(3, firstMaterial->GetPower()); EXPECT_FLOAT_EQ(9, secondMaterial->GetPower());
    CKCloseContext(Context); Context = nullptr;
    EXPECT_EQ(second, Other->GetRenderManager()); EXPECT_EQ(0, second->m_DisableSpecular.Value);
    EXPECT_EQ(secondMaterial, Other->GetObject(secondMaterial->GetID())); EXPECT_FLOAT_EQ(9, secondMaterial->GetPower());
    CKObject *mesh = Other->CreateObject(32, "after_first_close"); ASSERT_NE(nullptr, mesh); EXPECT_EQ(32, mesh->GetClassID());
}

TEST_F(RenderEngineInitializationTest, ClosingAndReinitializingContextsKeepsTheRegistrationUsable) {
    for (int cycle = 0; cycle < 3; ++cycle) {
        SCOPED_TRACE(cycle);
        ASSERT_EQ(CK_OK, InitInstanceFct(Context));
        ASSERT_NE(nullptr, Context->GetRenderManager());
        CKObject *sprite = Context->CreateObject(28, "cycle_sprite"); ASSERT_NE(nullptr, sprite); EXPECT_EQ(28, sprite->GetClassID());
        CKCloseContext(Context); Context = nullptr;
        InitializeCK2_3D(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        EXPECT_EQ(nullptr, Context->GetRenderManager());
    }
}

TEST_F(RenderEngineInitializationTest, RegistrationAloneDoesNotCreateManagersOrSceneInstances) {
    ASSERT_EQ(nullptr, Context->GetRenderManager());
    const int managers = Context->GetManagerCount(), objects = Context->GetObjectCount();
    InitializeCK2_3D();
    EXPECT_EQ(nullptr, Context->GetRenderManager());
    EXPECT_EQ(managers, Context->GetManagerCount()); EXPECT_EQ(objects, Context->GetObjectCount());
}
} // namespace

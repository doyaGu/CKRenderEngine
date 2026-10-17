#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKRenderedScene.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKCamera.h"
#include "RCKLight.h"
#include "RCKTargetCamera.h"
#include "RCKTargetLight.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();
extern XClassArray<CKRasterizerInfo> g_RasterizersInfo;

namespace {
CK2dEntity *SceneAPI(RCK2dEntity *entity) { return reinterpret_cast<CK2dEntity *>(entity); }
CKCamera *SceneAPI(RCKCamera *camera) { return reinterpret_cast<CKCamera *>(camera); }
CK_ID WatchedRoot = 0, WatchedMaterial = 0;
int WatchedRootDestructions = 0, WatchedMaterialDestructions = 0;
void (*WatchedRootDeletionObserver)(CKContext *) = nullptr;

// Real production object implementations; only destruction observation differs.
class LifecycleEntity : public RCK3dEntity {
public:
    explicit LifecycleEntity(CKContext *context) : RCK3dEntity(context) {}
    ~LifecycleEntity() override {
        if (GetID() == WatchedRoot) {
            ++WatchedRootDestructions;
            if (WatchedRootDeletionObserver)
                WatchedRootDeletionObserver(m_Context);
        }
    }
    static CK3dEntity *CreateInstance(CKContext *context) { return reinterpret_cast<CK3dEntity *>(new LifecycleEntity(context)); }
};

class LifecycleMaterial : public RCKMaterial {
public:
    explicit LifecycleMaterial(CKContext *context) : RCKMaterial(context) {}
    ~LifecycleMaterial() override {
        if (GetID() == WatchedMaterial)
            ++WatchedMaterialDestructions;
    }
    static RCKMaterial *CreateInstance(CKContext *context) { return new LifecycleMaterial(context); }
};

// This is a genuine separately owned Scene instance, not a cast of a base
// object to a test layout. No methods are overridden. It is deleted through
// this exact type because CKRenderedScene's destructor is not virtual.
class InspectableScene : public CKRenderedScene {
public:
    explicit InspectableScene(CKRenderContext *view) : CKRenderedScene(view) {}
    using CKRenderedScene::m_RenderContext;
    using CKRenderedScene::m_Context;
    using CKRenderedScene::m_BackgroundMaterial;
    using CKRenderedScene::m_RootEntity;
    using CKRenderedScene::m_AttachedCamera;
    using CKRenderedScene::m_3DEntities;
    using CKRenderedScene::m_Cameras;
    using CKRenderedScene::m_Lights;
    using CKRenderedScene::m_2DEntities;
    using CKRenderedScene::m_FogMode;
    using CKRenderedScene::m_FogStart;
    using CKRenderedScene::m_FogEnd;
    using CKRenderedScene::m_FogDensity;
    using CKRenderedScene::m_FogColor;
    using CKRenderedScene::m_AmbientLight;
    using CKRenderedScene::m_LightCount;
};

InspectableScene *SceneAwaitingClearAll = nullptr;
bool ObservedClearAllDuringRootDeletion = false;
bool SceneDeletedDuringClearAll = false;

void DeleteStandaloneSceneDuringClearAll(CKContext *context) {
    ObservedClearAllDuringRootDeletion = context->IsInClearAll() != FALSE;
    if (ObservedClearAllDuringRootDeletion) {
        delete SceneAwaitingClearAll;
        SceneAwaitingClearAll = nullptr;
        SceneDeletedDuringClearAll = true;
    }
}

class RenderedSceneLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *View = nullptr;
    InspectableScene *Scene = nullptr;
    XClassArray<CKRasterizerInfo> Saved;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(LifecycleEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKTargetCamera, CKCID_CAMERA);
        CKCLASSREGISTERCID(RCKTargetLight, CKCID_LIGHT);
        CKCLASSREGISTERCID(LifecycleMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        Saved = g_RasterizersInfo;
        g_RasterizersInfo.Clear();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        View = static_cast<RCKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT));
        ASSERT_NE(nullptr, View);
        Context->m_BehaviorContext.CurrentRenderContext = View;
        ASSERT_NE(nullptr, View->Get2dRoot(FALSE));
        ASSERT_NE(nullptr, View->Get2dRoot(TRUE));
        Scene = new InspectableScene(View);
        ASSERT_NE(nullptr, Scene->GetRootEntity());
        ASSERT_NE(nullptr, Scene->GetBackgroundMaterial());
        WatchedRoot = Scene->GetRootEntity()->GetID();
        WatchedMaterial = Scene->GetBackgroundMaterial()->GetID();
        WatchedRootDestructions = WatchedMaterialDestructions = 0;
    }

    void TearDown() override {
        WatchedRootDeletionObserver = nullptr;
        if (Scene) {
            if (View)
                Scene->DetachAll();
            delete Scene;
        }
        if (Context) {
            Context->m_BehaviorContext.CurrentRenderContext = nullptr;
            CKCloseContext(Context);
        }
        WatchedRoot = WatchedMaterial = 0;
        g_RasterizersInfo = Saved;
        CKShutdown();
    }

    template<class T> T *Create(CK_CLASSID type) {
        return static_cast<T *>(Context->CreateObject(type));
    }

    void Attach(CKRenderObject *object) {
        // Exactly the caller's two operations in RCKRenderContext::AddObject.
        // The internal Scene helper alone does not set membership bits.
        static_cast<RCKRenderObject *>(object)->AddToRenderContext(View);
        Scene->AddObject(object);
    }

    void Remove(CKRenderObject *object) {
        static_cast<RCKRenderObject *>(object)->RemoveFromRenderContext(View);
        Scene->RemoveObject(object);
    }

    void ExpectEmptyCollections() {
        EXPECT_EQ(0, Scene->m_3DEntities.Size());
        EXPECT_EQ(0, Scene->m_2DEntities.Size());
        EXPECT_EQ(0, Scene->m_Cameras.Size());
        EXPECT_EQ(0, Scene->m_Lights.Size());
        EXPECT_EQ(nullptr, Scene->m_AttachedCamera);
    }

    static void ExpectBlack(const VxColor &color) {
        EXPECT_FLOAT_EQ(0, color.r);
        EXPECT_FLOAT_EQ(0, color.g);
        EXPECT_FLOAT_EQ(0, color.b);
        EXPECT_FLOAT_EQ(1, color.a);
    }
};
}

TEST_F(RenderedSceneLifecycleTest, ConstructorCreatesDistinctPrivateObjectsAndInitializesSceneState) {
    EXPECT_EQ(View, Scene->m_RenderContext);
    EXPECT_EQ(Context, Scene->m_Context);
    EXPECT_EQ(Scene->m_RootEntity, Scene->GetRootEntity());
    EXPECT_EQ(Scene->m_BackgroundMaterial, Scene->GetBackgroundMaterial());
    EXPECT_NE(View->GetViewpoint(), Scene->GetRootEntity());
    EXPECT_NE(View->GetBackgroundMaterial(), Scene->GetBackgroundMaterial());
    EXPECT_EQ(Context->GetObject(WatchedRoot), Scene->GetRootEntity());
    EXPECT_EQ(Context->GetObject(WatchedMaterial), Scene->GetBackgroundMaterial());
    EXPECT_EQ(Context, Scene->GetRootEntity()->m_Context);
    EXPECT_EQ(Context, Scene->GetBackgroundMaterial()->m_Context);
    EXPECT_EQ(CK_OBJECT_NOTTOBELISTEDANDSAVED,
              Scene->GetRootEntity()->GetObjectFlags() & CK_OBJECT_NOTTOBELISTEDANDSAVED);
    EXPECT_EQ(CK_OBJECT_NOTTOBELISTEDANDSAVED,
              Scene->GetBackgroundMaterial()->GetObjectFlags() & CK_OBJECT_NOTTOBELISTEDANDSAVED);
    EXPECT_STREQ("Background Material", Scene->GetBackgroundMaterial()->GetName());
    ExpectBlack(Scene->GetBackgroundMaterial()->GetDiffuse());
    ExpectBlack(Scene->GetBackgroundMaterial()->GetAmbient());
    ExpectBlack(Scene->GetBackgroundMaterial()->GetSpecular());
    ExpectBlack(Scene->GetBackgroundMaterial()->GetEmissive());
    ASSERT_EQ(1, Scene->m_3DEntities.Size());
    EXPECT_EQ(Scene->GetRootEntity(), Scene->m_3DEntities[0]);
    EXPECT_EQ(0, Scene->m_2DEntities.Size());
    EXPECT_EQ(0, Scene->m_Cameras.Size());
    EXPECT_EQ(0, Scene->m_Lights.Size());
    EXPECT_EQ(nullptr, Scene->m_AttachedCamera);
    EXPECT_EQ(0u, Scene->m_FogMode);
    EXPECT_FLOAT_EQ(1, Scene->m_FogStart);
    EXPECT_FLOAT_EQ(100, Scene->m_FogEnd);
    EXPECT_FLOAT_EQ(1, Scene->m_FogDensity);
    EXPECT_EQ(0u, Scene->m_FogColor);
    EXPECT_EQ(0xff0f0f0fu, Scene->m_AmbientLight);
    EXPECT_EQ(0u, Scene->m_LightCount);
}

TEST_F(RenderedSceneLifecycleTest, RealDerivedCamerasAndLightsPopulateTheirIndependentBorrowedCollections) {
    RCK3dEntity *entity = Create<RCK3dEntity>(CKCID_3DENTITY);
    RCKTargetCamera *camera = Create<RCKTargetCamera>(CKCID_TARGETCAMERA);
    RCKTargetLight *light = Create<RCKTargetLight>(CKCID_TARGETLIGHT);
    ASSERT_NE(nullptr, entity);
    ASSERT_NE(nullptr, camera);
    ASSERT_NE(nullptr, light);
    Attach(entity);
    Attach(camera);
    Attach(light);
    ASSERT_EQ(4, Scene->m_3DEntities.Size());
    EXPECT_EQ(entity, Scene->m_3DEntities[1]);
    EXPECT_EQ(camera, Scene->m_3DEntities[2]);
    EXPECT_EQ(light, Scene->m_3DEntities[3]);
    ASSERT_EQ(1, Scene->m_Cameras.Size());
    ASSERT_EQ(1, Scene->m_Lights.Size());
    EXPECT_EQ(camera, Scene->m_Cameras[0]);
    EXPECT_EQ(light, Scene->m_Lights[0]);
    Scene->m_AttachedCamera = SceneAPI(camera);
    Remove(entity);
    EXPECT_EQ(3, Scene->m_3DEntities.Size());
    EXPECT_EQ(SceneAPI(camera), Scene->m_AttachedCamera);
    Remove(camera);
    EXPECT_EQ(nullptr, Scene->m_AttachedCamera);
    EXPECT_EQ(0, Scene->m_Cameras.Size());
    EXPECT_EQ(1, Scene->m_Lights.Size());
    EXPECT_FALSE(camera->IsInRenderContext(View));
    Remove(light);
    EXPECT_EQ(0, Scene->m_Lights.Size());
    EXPECT_EQ(1, Scene->m_3DEntities.Size());
    EXPECT_EQ(entity, Context->GetObject(entity->GetID()));
    EXPECT_EQ(camera, Context->GetObject(camera->GetID()));
    EXPECT_EQ(light, Context->GetObject(light->GetID()));
}

TEST_F(RenderedSceneLifecycleTest, RemovingVirtualRootObjectsDoesNotDetachAnExplicitTwoDimensionalParent) {
    RCK2dEntity *foreground = Create<RCK2dEntity>(CKCID_2DENTITY);
    RCK2dEntity *background = Create<RCK2dEntity>(CKCID_2DENTITY);
    RCK2dEntity *parent = Create<RCK2dEntity>(CKCID_2DENTITY);
    RCK2dEntity *child = Create<RCK2dEntity>(CKCID_2DENTITY);
    ASSERT_NE(nullptr, foreground);
    ASSERT_NE(nullptr, background);
    ASSERT_NE(nullptr, parent);
    ASSERT_NE(nullptr, child);
    background->SetBackground(TRUE);
    ASSERT_TRUE(child->SetParent(SceneAPI(parent)));
    Attach(foreground);
    Attach(background);
    Attach(child);
    ASSERT_EQ(3, Scene->m_2DEntities.Size());
    ASSERT_EQ(nullptr, foreground->GetParent());
    ASSERT_EQ(nullptr, background->GetParent());
    ASSERT_EQ(SceneAPI(parent), child->GetParent());
    EXPECT_EQ(SceneAPI(foreground), View->Get2dRoot(FALSE)->GetChild(0));
    EXPECT_EQ(SceneAPI(background), View->Get2dRoot(TRUE)->GetChild(0));
    Remove(foreground);
    EXPECT_EQ(0, View->Get2dRoot(FALSE)->GetChildrenCount());
    EXPECT_EQ(1, View->Get2dRoot(TRUE)->GetChildrenCount());
    Remove(background);
    EXPECT_EQ(0, View->Get2dRoot(TRUE)->GetChildrenCount());
    Remove(child);
    EXPECT_EQ(0, Scene->m_2DEntities.Size());
    EXPECT_EQ(SceneAPI(parent), child->GetParent());
    ASSERT_EQ(1, parent->GetChildrenCount());
    EXPECT_EQ(SceneAPI(child), parent->GetChild(0));
}

TEST_F(RenderedSceneLifecycleTest, RegisteredDetachClearsEveryMembershipButPreservesSharedRootsAndOtherContexts) {
    Manager->m_RenderContexts.PushBack(View->GetID());
    RCKRenderContext *other = Create<RCKRenderContext>(CKCID_RENDERCONTEXT);
    RCK3dEntity *entity = Create<RCK3dEntity>(CKCID_3DENTITY);
    RCKCamera *camera = Create<RCKCamera>(CKCID_CAMERA);
    RCKLight *light = Create<RCKLight>(CKCID_LIGHT);
    RCK2dEntity *overlay = Create<RCK2dEntity>(CKCID_2DENTITY);
    ASSERT_NE(nullptr, other);
    ASSERT_NE(nullptr, entity);
    ASSERT_NE(nullptr, camera);
    ASSERT_NE(nullptr, light);
    ASSERT_NE(nullptr, overlay);
    Attach(entity);
    Attach(camera);
    Attach(light);
    Attach(overlay);
    other->AddObject(entity);
    other->AddObject(overlay);
    Scene->m_AttachedCamera = SceneAPI(camera);
    ASSERT_EQ(1, View->Get2dRoot(FALSE)->GetChildrenCount());
    Scene->DetachAll();
    ExpectEmptyCollections();
    EXPECT_FALSE(entity->IsInRenderContext(View));
    EXPECT_FALSE(camera->IsInRenderContext(View));
    EXPECT_FALSE(light->IsInRenderContext(View));
    EXPECT_FALSE(overlay->IsInRenderContext(View));
    EXPECT_TRUE(entity->IsInRenderContext(other));
    EXPECT_TRUE(overlay->IsInRenderContext(other));
    EXPECT_TRUE(other->IsObjectAttached(entity));
    ASSERT_EQ(1, View->Get2dRoot(FALSE)->GetChildrenCount());
    EXPECT_EQ(SceneAPI(overlay), View->Get2dRoot(FALSE)->GetChild(0));
    EXPECT_EQ(Scene->GetRootEntity(), Context->GetObject(WatchedRoot));
    EXPECT_EQ(Scene->GetBackgroundMaterial(), Context->GetObject(WatchedMaterial));
    Scene->DetachAll();
    ExpectEmptyCollections();
    EXPECT_TRUE(entity->IsInRenderContext(other));
}

TEST_F(RenderedSceneLifecycleTest, UnregisteredDetachClearsBothSyntheticRootsWithoutDeletingBorrowedObjects) {
    RCK2dEntity *front = Create<RCK2dEntity>(CKCID_2DENTITY);
    RCK2dEntity *back = Create<RCK2dEntity>(CKCID_2DENTITY);
    ASSERT_NE(nullptr, front);
    ASSERT_NE(nullptr, back);
    back->SetBackground(TRUE);
    Attach(front);
    Attach(back);
    ASSERT_EQ(1, View->Get2dRoot(FALSE)->GetChildrenCount());
    ASSERT_EQ(1, View->Get2dRoot(TRUE)->GetChildrenCount());
    Scene->DetachAll();
    ExpectEmptyCollections();
    EXPECT_EQ(0, View->Get2dRoot(FALSE)->GetChildrenCount());
    EXPECT_EQ(0, View->Get2dRoot(TRUE)->GetChildrenCount());
    EXPECT_FALSE(front->IsInRenderContext(View));
    EXPECT_FALSE(back->IsInRenderContext(View));
    EXPECT_EQ(front, Context->GetObject(front->GetID()));
    EXPECT_EQ(back, Context->GetObject(back->GetID()));
}

TEST_F(RenderedSceneLifecycleTest, DestructionDeletesOnlyItsTwoPrivateObjectsExactlyOnce) {
    RCK3dEntity *entity = Create<RCK3dEntity>(CKCID_3DENTITY);
    RCKMaterial *material = Create<RCKMaterial>(CKCID_MATERIAL);
    ASSERT_NE(nullptr, entity);
    ASSERT_NE(nullptr, material);
    Attach(entity);
    const CK_ID originalRoot = View->GetViewpoint()->GetID();
    const CK_ID originalMaterial = View->GetBackgroundMaterial()->GetID();
    Scene->DetachAll();
    delete Scene;
    Scene = nullptr;
    EXPECT_EQ(1, WatchedRootDestructions);
    EXPECT_EQ(1, WatchedMaterialDestructions);
    EXPECT_EQ(nullptr, Context->GetObject(WatchedRoot));
    EXPECT_EQ(nullptr, Context->GetObject(WatchedMaterial));
    EXPECT_EQ(entity, Context->GetObject(entity->GetID()));
    EXPECT_EQ(material, Context->GetObject(material->GetID()));
    EXPECT_EQ(View->GetViewpoint(), Context->GetObject(originalRoot));
    EXPECT_EQ(View->GetBackgroundMaterial(), Context->GetObject(originalMaterial));
}

TEST_F(RenderedSceneLifecycleTest, ClearAllOwnsPrivateObjectDeletionBeforeTheSceneDestructorSkipsTheirDanglingHandles) {
    SceneAwaitingClearAll = Scene;
    Scene = nullptr;
    ObservedClearAllDuringRootDeletion = false;
    SceneDeletedDuringClearAll = false;
    WatchedRootDeletionObserver = DeleteStandaloneSceneDuringClearAll;
    const CKERROR result = Context->ClearAll();
    WatchedRootDeletionObserver = nullptr;
    View = nullptr;
    EXPECT_EQ(CK_OK, result);
    EXPECT_TRUE(ObservedClearAllDuringRootDeletion);
    EXPECT_TRUE(SceneDeletedDuringClearAll);
    EXPECT_EQ(nullptr, SceneAwaitingClearAll);
    EXPECT_EQ(1, WatchedRootDestructions);
    EXPECT_EQ(1, WatchedMaterialDestructions);
    // The observer runs inside the real object manager's deletion phase. The
    // material may already be gone and the root is currently being destroyed;
    // the Scene must leave both handles to that phase instead of deleting them.
    if (SceneAwaitingClearAll) {
        // Failure cleanup: ClearAll has finished and neither stale handle may
        // be used. This path is not the phase observation asserted above.
        SceneAwaitingClearAll->m_RootEntity = nullptr;
        SceneAwaitingClearAll->m_BackgroundMaterial = nullptr;
        delete SceneAwaitingClearAll;
        SceneAwaitingClearAll = nullptr;
    }
}

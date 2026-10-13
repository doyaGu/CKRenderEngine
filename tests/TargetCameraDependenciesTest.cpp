#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKRenderObject.h"
#include "RCKTargetCamera.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class DependencyTargetCamera : public RCKTargetCamera {
public:
    explicit DependencyTargetCamera(CKContext *context) : RCKTargetCamera(context) {}
    void Deferred(CK_ID id) { m_Target = id; }
    CK_ID TargetID() const { return m_Target; }
};
class TargetCameraDependencyMap : public CKDependenciesContext {
public:
    explicit TargetCameraDependencyMap(CKContext *context) : CKDependenciesContext(context) {}
    void Map(CK_ID from, CK_ID to) { m_MapID.Insert(from, to); }
};
class TargetCameraDependenciesTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKTargetCamera, CKCID_CAMERA);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static void Options(CKDependencies &options, CKDWORD camera) {
        options.Resize(80); for (int i = 0; i < 80; ++i) options[i] = 0;
        options.m_Flags = CK_DEPENDENCIES_CUSTOM; options[CKCID_TARGETCAMERA] = camera;
    }
    static void Membership(CKScene &scene, CKSceneObject &object, bool present) {
        EXPECT_EQ(present, object.IsInScene(&scene) != FALSE);
        EXPECT_EQ(present, scene.GetSceneObjectDesc(&object) != nullptr);
    }
    CKScene *Scene() { return static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE)); }
};
}

TEST_F(TargetCameraDependenciesTest, DependencyCountsExposeTargetInCopySaveDeleteOnly) {
    for (int mode = -2; mode <= 8; ++mode)
        EXPECT_EQ(mode == 1 || mode == 2 || mode == 4 ? 1 : 0, RCKTargetCamera::GetDependenciesCount(mode));
}
TEST_F(TargetCameraDependenciesTest, DependencyLabelUsesIndexIndependentOfMode) {
    for (int mode = -2; mode <= 8; ++mode) {
        ASSERT_NE(nullptr, RCKTargetCamera::GetDependencies(0, mode));
        EXPECT_STREQ("Target", RCKTargetCamera::GetDependencies(0, mode));
        EXPECT_EQ(nullptr, RCKTargetCamera::GetDependencies(-1, mode));
        EXPECT_EQ(nullptr, RCKTargetCamera::GetDependencies(1, mode));
    }
}
TEST_F(TargetCameraDependenciesTest, AddDependenciesIncludesTargetAndItsChildrenWithoutDuplicating) {
    RCKTargetCamera camera(Context); RCK3dEntity target(Context), child(Context);
    camera.SetTarget(Entity(target)); child.SetParent(Entity(target), TRUE);
    CKScene *scene = Scene(); ASSERT_NE(nullptr, scene);
    camera.AddToScene(scene, TRUE); camera.AddToScene(scene, TRUE);
    Membership(*scene, camera, true); Membership(*scene, target, true); Membership(*scene, child, true);
    EXPECT_EQ(3, scene->GetObjectCount()); child.SetParent(nullptr, TRUE);
}
TEST_F(TargetCameraDependenciesTest, DisabledAddAndNullSceneOnlyAffectCameraMembership) {
    RCKTargetCamera camera(Context); RCK3dEntity target(Context); camera.SetTarget(Entity(target));
    CKScene *scene = Scene(); ASSERT_NE(nullptr, scene); camera.AddToScene(nullptr, TRUE); camera.AddToScene(scene, FALSE);
    Membership(*scene, camera, true); Membership(*scene, target, false); EXPECT_EQ(1, scene->GetObjectCount());
}
TEST_F(TargetCameraDependenciesTest, RemoveDependenciesLeavesTargetChildrenAndOtherSceneIntact) {
    RCKTargetCamera camera(Context); RCK3dEntity target(Context), child(Context);
    camera.SetTarget(Entity(target)); child.SetParent(Entity(target), TRUE);
    CKScene *scene = Scene(), *other = Scene(); ASSERT_NE(nullptr, scene); ASSERT_NE(nullptr, other);
    camera.AddToScene(scene, TRUE); target.AddToScene(other, FALSE);
    camera.RemoveFromScene(scene, TRUE); camera.RemoveFromScene(scene, TRUE);
    Membership(*scene, camera, false); Membership(*scene, target, false); Membership(*scene, child, true);
    Membership(*other, target, true); EXPECT_EQ(1, scene->GetObjectCount()); child.SetParent(nullptr, TRUE);
}
TEST_F(TargetCameraDependenciesTest, DisabledRemoveAndNullSceneKeepTargetMembership) {
    RCKTargetCamera camera(Context); RCK3dEntity target(Context); camera.SetTarget(Entity(target));
    CKScene *scene = Scene(); ASSERT_NE(nullptr, scene); camera.AddToScene(scene, TRUE);
    camera.RemoveFromScene(nullptr, TRUE); Membership(*scene, camera, true);
    camera.RemoveFromScene(scene, FALSE); Membership(*scene, camera, false); Membership(*scene, target, true);
}
TEST_F(TargetCameraDependenciesTest, DeferredTargetDoesNotPreventSceneMembership) {
    DependencyTargetCamera camera(Context); camera.Deferred(0xfedcba01);
    CKScene *scene = Scene(); ASSERT_NE(nullptr, scene); camera.AddToScene(scene, TRUE);
    Membership(*scene, camera, true); EXPECT_EQ(1, scene->GetObjectCount());
    camera.RemoveFromScene(scene, TRUE); Membership(*scene, camera, false); EXPECT_EQ(0xfedcba01u, camera.TargetID());
}
TEST_F(TargetCameraDependenciesTest, PrepareTargetBitControlsActualDependencyGraph) {
    RCKTargetCamera camera(Context); RCK3dEntity target(Context); camera.SetTarget(Entity(target));
    for (CKDWORD bit = 0; bit < 2; ++bit) {
        CKDependencies options; Options(options, bit); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
        ASSERT_EQ(CK_OK, camera.PrepareDependencies(dependencies));
        EXPECT_TRUE(dependencies.IsDependenciesHere(camera.GetID()));
        EXPECT_EQ(bit != 0, dependencies.IsDependenciesHere(target.GetID()) != FALSE); dependencies.StopDependencies();
    }
}
TEST_F(TargetCameraDependenciesTest, PrepareCombinesInheritedChildrenAndTargetDependencies) {
    RCKTargetCamera camera(Context); RCK3dEntity target(Context), child(Context); camera.SetTarget(Entity(target));
    child.SetParent(Entity(camera), TRUE);
    CKDependencies options; Options(options, 1); options[CKCID_3DENTITY] = 2;
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, camera.PrepareDependencies(dependencies));
    EXPECT_TRUE(dependencies.IsDependenciesHere(camera.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(target.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(child.GetID())); dependencies.StopDependencies(); child.SetParent(nullptr, TRUE);
}
TEST_F(TargetCameraDependenciesTest, PrepareDeferredTargetFinishesOwnerWithoutInventedObject) {
    DependencyTargetCamera camera(Context); camera.Deferred(0xfedcba02);
    CKDependencies options; Options(options, 1); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, camera.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(camera.GetID()));
    EXPECT_FALSE(dependencies.IsDependenciesHere(camera.TargetID())); dependencies.StopDependencies();
}
TEST_F(TargetCameraDependenciesTest, RemapResolvedTargetReattachesFlagsEvenWhenTargetBitDisabled) {
    RCKTargetCamera camera(Context); RCK3dEntity before(Context), after(Context); camera.SetTarget(Entity(before));
    CKDependencies options; Options(options, 0); TargetCameraDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(before.GetID(), after.GetID()); ASSERT_EQ(CK_OK, camera.RemapDependencies(dependencies));
    EXPECT_EQ(Entity(after), camera.GetTarget()); EXPECT_EQ(0u, before.GetFlags() & CK_3DENTITY_TARGETCAMERA);
    EXPECT_NE(0u, after.GetFlags() & CK_3DENTITY_TARGETCAMERA); EXPECT_EQ(0u, after.GetFlags() & CK_3DENTITY_FRAME);
    dependencies.StopDependencies();
}
TEST_F(TargetCameraDependenciesTest, RemapUnresolvedStoredIdCanBecomeARealTarget) {
    DependencyTargetCamera camera(Context); RCK3dEntity after(Context); camera.Deferred(0xfedcba03);
    CKDependencies options; Options(options, 0); TargetCameraDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(camera.TargetID(), after.GetID()); ASSERT_EQ(CK_OK, camera.RemapDependencies(dependencies));
    EXPECT_EQ(after.GetID(), camera.TargetID()); EXPECT_EQ(Entity(after), camera.GetTarget());
    EXPECT_NE(0u, after.GetFlags() & CK_3DENTITY_TARGETCAMERA); dependencies.StopDependencies();
}
TEST_F(TargetCameraDependenciesTest, RemapCanPreserveADeferredMappedIdBeforeObjectCreation) {
    DependencyTargetCamera camera(Context); camera.Deferred(0xfedcba04);
    CKDependencies options; Options(options, 0); TargetCameraDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(camera.TargetID(), 0xfedcba05); ASSERT_EQ(CK_OK, camera.RemapDependencies(dependencies));
    EXPECT_EQ(0xfedcba05u, camera.TargetID()); EXPECT_EQ(nullptr, camera.GetTarget()); dependencies.StopDependencies();
}
TEST_F(TargetCameraDependenciesTest, RemapWithoutMappingKeepsTargetIdentityAndFlags) {
    RCKTargetCamera camera(Context); RCK3dEntity target(Context); camera.SetTarget(Entity(target)); const CKDWORD flags = target.GetFlags();
    CKDependencies options; Options(options, 0); TargetCameraDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, camera.RemapDependencies(dependencies)); EXPECT_EQ(Entity(target), camera.GetTarget());
    EXPECT_EQ(flags, target.GetFlags()); dependencies.StopDependencies();
}
TEST_F(TargetCameraDependenciesTest, RemapExplicitZeroKeepsNativeOriginalIdAndFlags) {
    DependencyTargetCamera camera(Context); RCK3dEntity target(Context); camera.SetTarget(Entity(target));
    const CK_ID originalID = camera.TargetID(); const CKDWORD flags = target.GetFlags();
    CKDependencies options; Options(options, 0); TargetCameraDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(originalID, 0);
    CK_ID directID = originalID; EXPECT_EQ(0u, dependencies.RemapID(directID)); EXPECT_EQ(0u, directID);
    ASSERT_EQ(CK_OK, camera.RemapDependencies(dependencies)); EXPECT_EQ(originalID, camera.TargetID());
    EXPECT_EQ(Entity(target), camera.GetTarget()); EXPECT_EQ(flags, target.GetFlags()); dependencies.StopDependencies();
}
TEST_F(TargetCameraDependenciesTest, CopyReplacesDestinationTargetAndKeepsSharedSourceAttachment) {
    RCKTargetCamera source(Context), destination(Context); RCK3dEntity oldTarget(Context), sourceTarget(Context);
    source.SetTarget(Entity(sourceTarget)); source.SetFov(1.125f); destination.SetTarget(Entity(oldTarget));
    CKDependencies options; Options(options, 0); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_EQ(Entity(sourceTarget), destination.GetTarget());
    EXPECT_EQ(Entity(sourceTarget), source.GetTarget()); EXPECT_EQ(1.125f, destination.GetFov());
    EXPECT_EQ(0u, oldTarget.GetFlags() & CK_3DENTITY_TARGETCAMERA); EXPECT_NE(0u, sourceTarget.GetFlags() & CK_3DENTITY_TARGETCAMERA);
    destination.SetTarget(nullptr); EXPECT_EQ(Entity(sourceTarget), source.GetTarget());
    EXPECT_EQ(0u, sourceTarget.GetFlags() & CK_3DENTITY_TARGETCAMERA); dependencies.StopDependencies();
}
TEST_F(TargetCameraDependenciesTest, CopyDeferredIdCleansOldAttachmentAndPreservesUnresolvedReference) {
    DependencyTargetCamera source(Context), destination(Context); RCK3dEntity oldTarget(Context);
    source.Deferred(0xfedcba06); destination.SetTarget(Entity(oldTarget));
    CKDependencies options; Options(options, 0); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_EQ(0xfedcba06u, destination.TargetID());
    EXPECT_EQ(nullptr, destination.GetTarget()); EXPECT_EQ(0u, oldTarget.GetFlags() & CK_3DENTITY_TARGETCAMERA); dependencies.StopDependencies();
}
TEST_F(TargetCameraDependenciesTest, SelfCopyKeepsTargetFlagsAndInheritedSkinStorage) {
    RCKTargetCamera camera(Context); RCK3dEntity target(Context); camera.SetTarget(Entity(target));
    CKSkin *skin = camera.CreateSkin(); ASSERT_NE(nullptr, skin); skin->SetBoneCount(1);
    skin->GetBoneData(0)->SetBone(Entity(target)); const CKDWORD flags = target.GetFlags();
    CKDependencies options; Options(options, 0); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, camera.Copy(camera, dependencies)); EXPECT_EQ(skin, camera.GetSkin()); EXPECT_EQ(Entity(target), camera.GetTarget());
    EXPECT_EQ(flags, target.GetFlags()); EXPECT_EQ(1, skin->GetBoneCount()); dependencies.StopDependencies();
}
TEST_F(TargetCameraDependenciesTest, ActualContextCopyClonesTargetOnlyWhenRequested) {
    CKObject *sourceObject = Context->CreateObject(CKCID_TARGETCAMERA), *targetObject = Context->CreateObject(CKCID_3DENTITY);
    ASSERT_NE(nullptr, sourceObject); ASSERT_NE(nullptr, targetObject);
    RCKTargetCamera *source = reinterpret_cast<RCKTargetCamera *>(sourceObject);
    RCK3dEntity *target = reinterpret_cast<RCK3dEntity *>(targetObject); source->SetTarget(Entity(*target));
    VxVector position(7, 11, 13); target->SetPosition(&position, nullptr, FALSE);
    for (int enabled = 0; enabled < 2; ++enabled) {
        CKDependencies options; Options(options, enabled); CKObject *copyObject = Context->CopyObject(source, &options);
        ASSERT_NE(nullptr, copyObject); ASSERT_NE(sourceObject, copyObject);
        RCKTargetCamera *copy = reinterpret_cast<RCKTargetCamera *>(copyObject); ASSERT_NE(nullptr, copy->GetTarget());
        EXPECT_EQ(enabled == 0, copy->GetTarget() == Entity(*target));
        EXPECT_FLOAT_EQ(7, copy->GetTarget()->GetWorldMatrix()[3][0]);
        EXPECT_NE(0u, copy->GetTarget()->GetFlags() & CK_3DENTITY_TARGETCAMERA);
        copy->SetTarget(nullptr); EXPECT_EQ(Entity(*target), source->GetTarget());
        EXPECT_EQ(0u, target->GetFlags() & CK_3DENTITY_TARGETCAMERA);
    }
}

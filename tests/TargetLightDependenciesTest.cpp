#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKRenderObject.h"
#include "RCKTargetLight.h"
#include "RCKRenderManager.h"
#include "RCKSkin.h"
#include "RCKMesh.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class DependencyTargetLight : public RCKTargetLight {
public:
    explicit DependencyTargetLight(CKContext *context) : RCKTargetLight(context) {}
    void Deferred(CK_ID id) { m_Target = id; }
    CK_ID TargetID() const { return m_Target; }
    CKLightData &Data() { return m_LightData; }
    CKDWORD LightFlags() const { return m_Flags; }
    void LightFlags(CKDWORD flags) { m_Flags = flags; }
};
class TargetLightDependencyMap : public CKDependenciesContext {
public:
    explicit TargetLightDependencyMap(CKContext *context) : CKDependenciesContext(context) {}
    void Map(CK_ID from, CK_ID to) { m_MapID.Insert(from, to); }
};
class FailingPreparedTarget : public RCK3dEntity {
public:
    explicit FailingPreparedTarget(CKContext *context) : RCK3dEntity(context), Calls(0) {}
    int Calls;
    CKERROR PrepareDependencies(CKDependenciesContext &context) override {
        ++Calls;
        CKERROR result = RCK3dEntity::PrepareDependencies(context);
        return result == CK_OK ? CKERR_INVALIDPARAMETER : result;
    }
};
class TargetLightDependenciesTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKTargetLight, CKCID_LIGHT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static void Options(CKDependencies &options, CKDWORD camera) {
        options.Resize(80); for (int i = 0; i < 80; ++i) options[i] = 0;
        options.m_Flags = CK_DEPENDENCIES_CUSTOM; options[CKCID_TARGETLIGHT] = camera;
    }
    static void Membership(CKScene &scene, CKSceneObject &object, bool present) {
        EXPECT_EQ(present, object.IsInScene(&scene) != FALSE);
        EXPECT_EQ(present, scene.GetSceneObjectDesc(&object) != nullptr);
    }
    CKScene *Scene() { return static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE)); }
};
}


TEST_F(TargetLightDependenciesTest, DependencyCountsExposeTargetInCopySaveDeleteOnly) {
    for (int mode = -2; mode <= 8; ++mode)
        EXPECT_EQ(mode == 1 || mode == 2 || mode == 4 ? 1 : 0, RCKTargetLight::GetDependenciesCount(mode));
}
TEST_F(TargetLightDependenciesTest, DependencyLabelUsesIndexIndependentOfMode) {
    for (int mode = -2; mode <= 8; ++mode) {
        ASSERT_NE(nullptr, RCKTargetLight::GetDependencies(0, mode));
        EXPECT_STREQ("Target", RCKTargetLight::GetDependencies(0, mode));
        EXPECT_EQ(nullptr, RCKTargetLight::GetDependencies(-1, mode));
        EXPECT_EQ(nullptr, RCKTargetLight::GetDependencies(1, mode));
    }
}
TEST_F(TargetLightDependenciesTest, PrepareTargetBitControlsActualDependencyGraph) {
    RCKTargetLight camera(Context); RCK3dEntity target(Context); camera.SetTarget(Entity(target));
    for (CKDWORD bit = 0; bit < 2; ++bit) {
        CKDependencies options; Options(options, bit); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
        ASSERT_EQ(CK_OK, camera.PrepareDependencies(dependencies));
        EXPECT_TRUE(dependencies.IsDependenciesHere(camera.GetID()));
        EXPECT_EQ(bit != 0, dependencies.IsDependenciesHere(target.GetID()) != FALSE); dependencies.StopDependencies();
    }
}
TEST_F(TargetLightDependenciesTest, PrepareCombinesInheritedChildrenAndTargetDependencies) {
    RCKTargetLight camera(Context); RCK3dEntity target(Context), child(Context); camera.SetTarget(Entity(target));
    child.SetParent(Entity(camera), TRUE);
    CKDependencies options; Options(options, 1); options[CKCID_3DENTITY] = 2;
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, camera.PrepareDependencies(dependencies));
    EXPECT_TRUE(dependencies.IsDependenciesHere(camera.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(target.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(child.GetID())); dependencies.StopDependencies(); child.SetParent(nullptr, TRUE);
}
TEST_F(TargetLightDependenciesTest, PrepareDeferredTargetFinishesOwnerWithoutInventedObject) {
    DependencyTargetLight camera(Context); camera.Deferred(0xfedcba02);
    CKDependencies options; Options(options, 1); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, camera.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(camera.GetID()));
    EXPECT_FALSE(dependencies.IsDependenciesHere(camera.TargetID())); dependencies.StopDependencies();
}
TEST_F(TargetLightDependenciesTest, RemapResolvedTargetReattachesFlagsEvenWhenTargetBitDisabled) {
    RCKTargetLight camera(Context); RCK3dEntity before(Context), after(Context); camera.SetTarget(Entity(before));
    CKDependencies options; Options(options, 0); TargetLightDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(before.GetID(), after.GetID()); ASSERT_EQ(CK_OK, camera.RemapDependencies(dependencies));
    EXPECT_EQ(Entity(after), camera.GetTarget()); EXPECT_EQ(0u, before.GetFlags() & CK_3DENTITY_TARGETLIGHT);
    EXPECT_NE(0u, after.GetFlags() & CK_3DENTITY_TARGETLIGHT); EXPECT_EQ(0u, after.GetFlags() & CK_3DENTITY_FRAME);
    dependencies.StopDependencies();
}
TEST_F(TargetLightDependenciesTest, RemapUnresolvedStoredIdCanBecomeARealTarget) {
    DependencyTargetLight camera(Context); RCK3dEntity after(Context); camera.Deferred(0xfedcba03);
    CKDependencies options; Options(options, 0); TargetLightDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(camera.TargetID(), after.GetID()); ASSERT_EQ(CK_OK, camera.RemapDependencies(dependencies));
    EXPECT_EQ(after.GetID(), camera.TargetID()); EXPECT_EQ(Entity(after), camera.GetTarget());
    EXPECT_NE(0u, after.GetFlags() & CK_3DENTITY_TARGETLIGHT); dependencies.StopDependencies();
}
TEST_F(TargetLightDependenciesTest, RemapCanPreserveADeferredMappedIdBeforeObjectCreation) {
    DependencyTargetLight camera(Context); camera.Deferred(0xfedcba04);
    CKDependencies options; Options(options, 0); TargetLightDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(camera.TargetID(), 0xfedcba05); ASSERT_EQ(CK_OK, camera.RemapDependencies(dependencies));
    EXPECT_EQ(0xfedcba05u, camera.TargetID()); EXPECT_EQ(nullptr, camera.GetTarget()); dependencies.StopDependencies();
}
TEST_F(TargetLightDependenciesTest, RemapWithoutMappingKeepsTargetIdentityAndFlags) {
    RCKTargetLight camera(Context); RCK3dEntity target(Context); camera.SetTarget(Entity(target)); const CKDWORD flags = target.GetFlags();
    CKDependencies options; Options(options, 0); TargetLightDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, camera.RemapDependencies(dependencies)); EXPECT_EQ(Entity(target), camera.GetTarget());
    EXPECT_EQ(flags, target.GetFlags()); dependencies.StopDependencies();
}
TEST_F(TargetLightDependenciesTest, RemapExplicitZeroKeepsNativeOriginalIdAndFlags) {
    DependencyTargetLight camera(Context); RCK3dEntity target(Context); camera.SetTarget(Entity(target));
    const CK_ID originalID = camera.TargetID(); const CKDWORD flags = target.GetFlags();
    CKDependencies options; Options(options, 0); TargetLightDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(originalID, 0);
    CK_ID directID = originalID; EXPECT_EQ(0u, dependencies.RemapID(directID)); EXPECT_EQ(0u, directID);
    ASSERT_EQ(CK_OK, camera.RemapDependencies(dependencies)); EXPECT_EQ(originalID, camera.TargetID());
    EXPECT_EQ(Entity(target), camera.GetTarget()); EXPECT_EQ(flags, target.GetFlags()); dependencies.StopDependencies();
}
TEST_F(TargetLightDependenciesTest, CopyReplacesDestinationTargetAndKeepsSharedSourceAttachment) {
    RCKTargetLight source(Context), destination(Context); RCK3dEntity oldTarget(Context), sourceTarget(Context);
    source.SetTarget(Entity(sourceTarget)); source.SetLightPower(-2.75f); destination.SetTarget(Entity(oldTarget));
    CKDependencies options; Options(options, 0); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_EQ(Entity(sourceTarget), destination.GetTarget());
    EXPECT_EQ(Entity(sourceTarget), source.GetTarget()); EXPECT_EQ(-2.75f, destination.GetLightPower());
    EXPECT_EQ(0u, oldTarget.GetFlags() & CK_3DENTITY_TARGETLIGHT); EXPECT_NE(0u, sourceTarget.GetFlags() & CK_3DENTITY_TARGETLIGHT);
    destination.SetTarget(nullptr); EXPECT_EQ(Entity(sourceTarget), source.GetTarget());
    EXPECT_EQ(0u, sourceTarget.GetFlags() & CK_3DENTITY_TARGETLIGHT); dependencies.StopDependencies();
}
TEST_F(TargetLightDependenciesTest, CopyDeferredIdCleansOldAttachmentAndPreservesUnresolvedReference) {
    DependencyTargetLight source(Context), destination(Context); RCK3dEntity oldTarget(Context);
    source.Deferred(0xfedcba06); destination.SetTarget(Entity(oldTarget));
    CKDependencies options; Options(options, 0); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_EQ(0xfedcba06u, destination.TargetID());
    EXPECT_EQ(nullptr, destination.GetTarget()); EXPECT_EQ(0u, oldTarget.GetFlags() & CK_3DENTITY_TARGETLIGHT); dependencies.StopDependencies();
}
TEST_F(TargetLightDependenciesTest, SelfCopyKeepsTargetFlagsAndInheritedSkinStorage) {
    RCKTargetLight camera(Context); RCK3dEntity target(Context); camera.SetTarget(Entity(target));
    CKSkin *skin = camera.CreateSkin(); ASSERT_NE(nullptr, skin); skin->SetBoneCount(1);
    skin->GetBoneData(0)->SetBone(Entity(target)); const CKDWORD flags = target.GetFlags();
    CKDependencies options; Options(options, 0); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, camera.Copy(camera, dependencies)); EXPECT_EQ(skin, camera.GetSkin()); EXPECT_EQ(Entity(target), camera.GetTarget());
    EXPECT_EQ(flags, target.GetFlags()); EXPECT_EQ(1, skin->GetBoneCount()); dependencies.StopDependencies();
}
TEST_F(TargetLightDependenciesTest, ActualContextCopyClonesTargetOnlyWhenRequested) {
    CKObject *sourceObject = Context->CreateObject(CKCID_TARGETLIGHT), *targetObject = Context->CreateObject(CKCID_3DENTITY);
    ASSERT_NE(nullptr, sourceObject); ASSERT_NE(nullptr, targetObject);
    RCKTargetLight *source = reinterpret_cast<RCKTargetLight *>(sourceObject);
    RCK3dEntity *target = reinterpret_cast<RCK3dEntity *>(targetObject); source->SetTarget(Entity(*target));
    VxVector position(7, 11, 13); target->SetPosition(&position, nullptr, FALSE);
    for (int enabled = 0; enabled < 2; ++enabled) {
        CKDependencies options; Options(options, enabled); CKObject *copyObject = Context->CopyObject(source, &options);
        ASSERT_NE(nullptr, copyObject); ASSERT_NE(sourceObject, copyObject);
        RCKTargetLight *copy = reinterpret_cast<RCKTargetLight *>(copyObject); ASSERT_NE(nullptr, copy->GetTarget());
        EXPECT_EQ(enabled == 0, copy->GetTarget() == Entity(*target));
        EXPECT_FLOAT_EQ(7, copy->GetTarget()->GetWorldMatrix()[3][0]);
        EXPECT_NE(0u, copy->GetTarget()->GetFlags() & CK_3DENTITY_TARGETLIGHT);
        copy->SetTarget(nullptr); EXPECT_EQ(Entity(*target), source->GetTarget());
        EXPECT_EQ(0u, target->GetFlags() & CK_3DENTITY_TARGETLIGHT);
    }
}

TEST_F(TargetLightDependenciesTest, IsObjectUsedRecognizesTargetIndependentlyOfRequestedClass) {
    RCKTargetLight light(Context); RCK3dEntity target(Context), unrelated(Context);
    light.SetTarget(Entity(target));
    const CK_CLASSID classes[] = {CKCID_3DENTITY, CKCID_MESH, CKCID_OBJECT, CKCID_OBJECTANIMATION};
    for (CK_CLASSID cid : classes) {
        EXPECT_TRUE(light.IsObjectUsed(&target, cid));
        EXPECT_FALSE(light.IsObjectUsed(&unrelated, cid));
    }
    light.SetTarget(nullptr); EXPECT_FALSE(light.IsObjectUsed(&target, CKCID_3DENTITY));
}
TEST_F(TargetLightDependenciesTest, IsObjectUsedRecognizesStoredIdBeforeTargetSetterAttachment) {
    DependencyTargetLight light(Context); RCK3dEntity target(Context);
    light.Deferred(target.GetID()); EXPECT_EQ(0u, target.GetFlags() & CK_3DENTITY_TARGETLIGHT);
    EXPECT_TRUE(light.IsObjectUsed(&target, CKCID_LIGHT));
    EXPECT_EQ(0u, target.GetFlags() & CK_3DENTITY_TARGETLIGHT);
}
TEST_F(TargetLightDependenciesTest, IsObjectUsedDelegatesInheritedMeshMembership) {
    RCKTargetLight light(Context); RCKMesh mesh(Context), unused(Context);
    ASSERT_EQ(CK_OK, light.AddMesh(reinterpret_cast<CKMesh *>(&mesh)));
    EXPECT_TRUE(light.IsObjectUsed(&mesh, CKCID_MESH));
    EXPECT_TRUE(light.IsObjectUsed(&mesh, CKCID_PATCHMESH));
    EXPECT_FALSE(light.IsObjectUsed(&mesh, CKCID_3DENTITY));
    EXPECT_FALSE(light.IsObjectUsed(&unused, CKCID_MESH));
    light.RemoveMesh(reinterpret_cast<CKMesh *>(&mesh));
}
TEST_F(TargetLightDependenciesTest, PrepareRetainsNativeIgnoredTargetErrorAndFinishesActualGraph) {
    RCKTargetLight light(Context); FailingPreparedTarget target(Context); light.SetTarget(Entity(target));
    CKDependencies options; Options(options, 1); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, light.PrepareDependencies(dependencies)); EXPECT_EQ(1, target.Calls);
    EXPECT_TRUE(dependencies.IsDependenciesHere(light.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(target.GetID()));
    dependencies.StopDependencies();
}
TEST_F(TargetLightDependenciesTest, CopyTransfersFullInheritedLightTailIndependentlyOfTargetBit) {
    DependencyTargetLight source(Context), destination(Context); RCK3dEntity target(Context);
    source.SetTarget(Entity(target)); source.Data().Type = VX_LIGHTSPOT;
    source.Data().Diffuse = VxColor(.125f, .25f, .375f, .5f);
    source.Data().Specular = VxColor(.625f, .75f, .875f, 1.0f);
    source.Data().Ambient = VxColor(-.125f, -.25f, -.375f, -.5f);
    source.Data().Position = VxVector(11, 13, 17); source.Data().Direction = VxVector(19, 23, 29);
    source.Data().Range = 307; source.Data().Falloff = .3125f;
    source.Data().Attenuation0 = 3; source.Data().Attenuation1 = 5; source.Data().Attenuation2 = 7;
    source.Data().InnerSpotCone = .4375f; source.Data().OuterSpotCone = .9375f;
    source.LightFlags(0x52100300); source.SetLightPower(-2.75f);
    for (CKDWORD bit = 0; bit < 2; ++bit) {
        CKDependencies options; Options(options, bit); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
        ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
        EXPECT_EQ(source.TargetID(), destination.TargetID()); EXPECT_EQ(source.LightFlags(), destination.LightFlags());
        EXPECT_EQ(source.GetLightPower(), destination.GetLightPower());
        const CKLightData &a = source.Data(), &b = destination.Data(); EXPECT_EQ(a.Type, b.Type);
        const VxColor colors[] = {a.Diffuse, a.Specular, a.Ambient};
        const VxColor copies[] = {b.Diffuse, b.Specular, b.Ambient};
        for (int i = 0; i < 3; ++i) {
            EXPECT_EQ(colors[i].r, copies[i].r); EXPECT_EQ(colors[i].g, copies[i].g);
            EXPECT_EQ(colors[i].b, copies[i].b); EXPECT_EQ(colors[i].a, copies[i].a);
        }
        EXPECT_EQ(a.Position.x, b.Position.x); EXPECT_EQ(a.Position.y, b.Position.y); EXPECT_EQ(a.Position.z, b.Position.z);
        EXPECT_EQ(a.Direction.x, b.Direction.x); EXPECT_EQ(a.Direction.y, b.Direction.y); EXPECT_EQ(a.Direction.z, b.Direction.z);
        EXPECT_EQ(a.Range, b.Range); EXPECT_EQ(a.Falloff, b.Falloff); EXPECT_EQ(a.Attenuation0, b.Attenuation0);
        EXPECT_EQ(a.Attenuation1, b.Attenuation1); EXPECT_EQ(a.Attenuation2, b.Attenuation2);
        EXPECT_EQ(a.InnerSpotCone, b.InnerSpotCone); EXPECT_EQ(a.OuterSpotCone, b.OuterSpotCone);
        dependencies.StopDependencies();
    }
}
TEST_F(TargetLightDependenciesTest, CopyEmptyTargetDetachesOldDestinationAttachment) {
    DependencyTargetLight source(Context), destination(Context); RCK3dEntity oldTarget(Context);
    destination.SetTarget(Entity(oldTarget)); CKDependencies options; Options(options, 0);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_EQ(0u, destination.TargetID());
    EXPECT_EQ(nullptr, destination.GetTarget()); EXPECT_EQ(0u, oldTarget.GetFlags() & CK_3DENTITY_TARGETLIGHT);
    EXPECT_NE(0u, oldTarget.GetFlags() & CK_3DENTITY_FRAME); dependencies.StopDependencies();
}
TEST_F(TargetLightDependenciesTest, RemapToOwnerKeepsNativeRawSelfIdAndDetachesOldMarker) {
    DependencyTargetLight light(Context); RCK3dEntity target(Context); light.SetTarget(Entity(target));
    CKDependencies options; Options(options, 0); TargetLightDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(target.GetID(), light.GetID()); ASSERT_EQ(CK_OK, light.RemapDependencies(dependencies));
    EXPECT_EQ(light.GetID(), light.TargetID()); EXPECT_EQ(Entity(light), light.GetTarget());
    EXPECT_EQ(0u, target.GetFlags() & CK_3DENTITY_TARGETLIGHT); EXPECT_NE(0u, target.GetFlags() & CK_3DENTITY_FRAME);
    EXPECT_EQ(0u, light.GetFlags() & CK_3DENTITY_TARGETLIGHT); dependencies.StopDependencies();
}
TEST_F(TargetLightDependenciesTest, CopyRawReferenceToDestinationKeepsSelfIdWithoutSelfMarker) {
    DependencyTargetLight source(Context), destination(Context); RCK3dEntity oldTarget(Context);
    source.Deferred(destination.GetID()); destination.SetTarget(Entity(oldTarget));
    CKDependencies options; Options(options, 0); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_EQ(destination.GetID(), destination.TargetID());
    EXPECT_EQ(Entity(destination), destination.GetTarget()); EXPECT_EQ(0u, oldTarget.GetFlags() & CK_3DENTITY_TARGETLIGHT);
    EXPECT_EQ(0u, destination.GetFlags() & CK_3DENTITY_TARGETLIGHT); dependencies.StopDependencies();
}

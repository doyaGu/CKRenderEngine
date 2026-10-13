#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKMesh.h"
#include "RCKObjectAnimation.h"
#include "RCKSkin.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class Entity3dDependencyMap : public CKDependenciesContext {
public:
    explicit Entity3dDependencyMap(CKContext *context) : CKDependenciesContext(context) {}
    void Map(CKObject &from, CKObject *to) { m_MapID.Insert(from.GetID(), to ? to->GetID() : 0); }
};
class Entity3dDependenciesTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Options(CKDependencies &options, CKDWORD entity) {
        options.Resize(80); for (int i = 0; i < 80; ++i) options[i] = 0;
        options.m_Flags = CK_DEPENDENCIES_CUSTOM; options[CKCID_3DENTITY] = entity;
    }
    static void Membership(CKScene &scene, CKSceneObject &object, bool present) {
        EXPECT_EQ(present, object.IsInScene(&scene) != FALSE);
        EXPECT_EQ(present, scene.GetSceneObjectDesc(&object) != nullptr);
    }
    CKScene *Scene() { return static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE)); }
};
}

TEST_F(Entity3dDependenciesTest, AddRecursesChildrenMeshesAndAnimationsAndKeepsOtherScene) {
    RCK3dEntity parent(Context), child(Context); RCKMesh mesh(Context); RCKObjectAnimation animation(Context);
    parent.AddMesh(&mesh); parent.AddObjectAnimation(&animation); child.SetParent(reinterpret_cast<CK3dEntity *>(&parent), TRUE);
    CKScene *scene = Scene(), *other = Scene(); ASSERT_NE(nullptr, scene); ASSERT_NE(nullptr, other);
    animation.AddToScene(other, FALSE); parent.AddToScene(scene, TRUE); parent.AddToScene(scene, TRUE);
    Membership(*scene, parent, true); Membership(*scene, child, true); Membership(*scene, mesh, true);
    Membership(*scene, animation, true); Membership(*other, animation, true); EXPECT_EQ(4, scene->GetObjectCount());
    child.SetParent(nullptr, TRUE);
}

TEST_F(Entity3dDependenciesTest, RemoveDependenciesKeepsChildrenAndOtherSceneMembership) {
    RCK3dEntity parent(Context), child(Context); RCKMesh mesh(Context); RCKObjectAnimation animation(Context);
    parent.AddMesh(&mesh); parent.AddObjectAnimation(&animation); child.SetParent(reinterpret_cast<CK3dEntity *>(&parent), TRUE);
    CKScene *scene = Scene(), *other = Scene(); ASSERT_NE(nullptr, scene); ASSERT_NE(nullptr, other);
    parent.AddToScene(scene, TRUE); animation.AddToScene(other, FALSE);
    parent.RemoveFromScene(scene, TRUE); parent.RemoveFromScene(scene, TRUE);
    Membership(*scene, parent, false); Membership(*scene, mesh, false); Membership(*scene, animation, false);
    Membership(*scene, child, true); Membership(*other, animation, true); EXPECT_EQ(1, scene->GetObjectCount());
    child.SetParent(nullptr, TRUE);
}

TEST_F(Entity3dDependenciesTest, NullSceneAndDisabledDependenciesOnlyChangeOwnerMembership) {
    RCK3dEntity parent(Context), child(Context); RCKMesh mesh(Context); RCKObjectAnimation animation(Context);
    parent.AddMesh(&mesh); parent.AddObjectAnimation(&animation); child.SetParent(reinterpret_cast<CK3dEntity *>(&parent), TRUE);
    CKScene *scene = Scene(); ASSERT_NE(nullptr, scene); parent.AddToScene(nullptr, TRUE); parent.AddToScene(scene, FALSE);
    Membership(*scene, parent, true); EXPECT_EQ(1, scene->GetObjectCount()); parent.RemoveFromScene(nullptr, TRUE);
    Membership(*scene, parent, true); parent.AddToScene(scene, TRUE); parent.RemoveFromScene(scene, FALSE);
    Membership(*scene, parent, false); Membership(*scene, child, true); Membership(*scene, mesh, true); Membership(*scene, animation, true);
    child.SetParent(nullptr, TRUE);
}

TEST_F(Entity3dDependenciesTest, PrepareCustomBitsSelectActualGraphObjectsIndependently) {
    RCK3dEntity parent(Context), child(Context); RCKMesh mesh(Context); RCKObjectAnimation animation(Context);
    parent.AddMesh(&mesh); parent.AddObjectAnimation(&animation); child.SetParent(reinterpret_cast<CK3dEntity *>(&parent), TRUE);
    for (CKDWORD bits = 0; bits < 8; ++bits) {
        CKDependencies options; Options(options, bits); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
        ASSERT_EQ(CK_OK, parent.PrepareDependencies(dependencies));
        EXPECT_TRUE(dependencies.IsDependenciesHere(parent.GetID()));
        EXPECT_EQ((bits & 1) != 0, dependencies.IsDependenciesHere(mesh.GetID()) != FALSE);
        EXPECT_EQ((bits & 2) != 0, dependencies.IsDependenciesHere(child.GetID()) != FALSE);
        EXPECT_EQ((bits & 4) != 0, dependencies.IsDependenciesHere(animation.GetID()) != FALSE);
        dependencies.StopDependencies();
    }
    child.SetParent(nullptr, TRUE);
}

TEST_F(Entity3dDependenciesTest, PrepareSkinBonesRunOutsideReplaceAndRequireMeshBitInReplace) {
    RCK3dEntity entity(Context), bone(Context); RCKMesh mesh(Context); entity.SetCurrentMesh(&mesh, TRUE);
    CKSkin *skin = entity.CreateSkin(); ASSERT_NE(nullptr, skin); skin->SetBoneCount(1);
    skin->GetBoneData(0)->SetBone(reinterpret_cast<CK3dEntity *>(&bone));
    for (int scenario = 0; scenario < 3; ++scenario) {
        CKDependencies options; Options(options, scenario == 2 ? 1 : 0); CKDependenciesContext dependencies(Context);
        dependencies.StartDependencies(&options); dependencies.SetOperationMode(scenario == 0 ? CK_DEPENDENCIES_BUILD : CK_DEPENDENCIES_REPLACE);
        ASSERT_EQ(CK_OK, entity.PrepareDependencies(dependencies));
        EXPECT_EQ(scenario != 1, dependencies.IsDependenciesHere(bone.GetID()) != FALSE);
        EXPECT_EQ(scenario != 1, dependencies.IsDependenciesHere(mesh.GetID()) != FALSE); dependencies.StopDependencies();
    }
}

TEST_F(Entity3dDependenciesTest, RemapCustomBitsReplaceReferenceIdentityAndAlwaysMapPlaceId) {
    RCK3dEntity parent(Context), newParent(Context); RCKMesh mesh(Context), newMesh(Context);
    RCKObjectAnimation animation(Context), newAnimation(Context); CKObject place(Context), newPlace(Context);
    for (CKDWORD bits = 0; bits < 8; ++bits) {
        RCK3dEntity entity(Context); entity.SetParent(reinterpret_cast<CK3dEntity *>(&parent), TRUE);
        entity.AddMesh(&mesh); entity.SetCurrentMesh(&mesh, FALSE); entity.AddObjectAnimation(&animation); entity.m_Place = place.GetID();
        CKDependencies options; Options(options, bits); Entity3dDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
        dependencies.Map(parent, &newParent); dependencies.Map(mesh, &newMesh); dependencies.Map(animation, &newAnimation); dependencies.Map(place, &newPlace);
        ASSERT_EQ(CK_OK, entity.RemapDependencies(dependencies));
        // Place is remapped first. Reparenting afterwards inherits the new
        // parent's containing Place (zero here), as in the native call order.
        EXPECT_EQ((bits & 2) ? 0u : newPlace.GetID(), entity.m_Place);
        EXPECT_EQ(reinterpret_cast<CK3dEntity *>((bits & 2) ? &newParent : &parent), entity.GetParent());
        EXPECT_EQ((bits & 1) ? &newMesh : &mesh, entity.GetCurrentMesh());
        EXPECT_EQ((bits & 1) ? &newMesh : &mesh, entity.GetMesh(0));
        EXPECT_EQ((bits & 4) ? &newAnimation : &animation, entity.GetObjectAnimation(0));
        entity.SetParent(nullptr, TRUE); dependencies.StopDependencies();
    }
}

TEST_F(Entity3dDependenciesTest, RemapSkinBonesAndTheirParentPreserveWorldPose) {
    RCK3dEntity entity(Context), bone(Context), copiedBone(Context), parent(Context), copiedParent(Context);
    copiedBone.SetParent(reinterpret_cast<CK3dEntity *>(&parent), TRUE);
    VxMatrix matrix = VxMatrix::Identity(); matrix[3][0] = 12; copiedBone.SetWorldMatrix(matrix, TRUE);
    CKSkin *skin = entity.CreateSkin(); ASSERT_NE(nullptr, skin); skin->SetBoneCount(1); skin->GetBoneData(0)->SetBone(reinterpret_cast<CK3dEntity *>(&bone));
    CKDependencies options; Options(options, 0); Entity3dDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(bone, &copiedBone); dependencies.Map(parent, &copiedParent); ASSERT_EQ(CK_OK, entity.RemapDependencies(dependencies));
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&copiedBone), skin->GetBoneData(0)->GetBone());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&copiedParent), copiedBone.GetParent()); EXPECT_FLOAT_EQ(12, copiedBone.GetWorldMatrix()[3][0]);
    copiedBone.SetParent(nullptr, TRUE); dependencies.StopDependencies();
}

TEST_F(Entity3dDependenciesTest, CopyPreservesPoseMeshAndParentWhileAnimationBitControlsChannels) {
    RCK3dEntity source(Context), parent(Context); RCKMesh mesh(Context); RCKObjectAnimation animation(Context);
    source.SetParent(reinterpret_cast<CK3dEntity *>(&parent), TRUE); source.AddMesh(&mesh); source.SetCurrentMesh(&mesh, FALSE);
    source.AddObjectAnimation(&animation); VxMatrix matrix = VxMatrix::Identity(); matrix[3][2] = 31; source.SetWorldMatrix(matrix, TRUE);
    for (int enabled = 0; enabled < 2; ++enabled) {
        RCK3dEntity destination(Context); CKDependencies options; Options(options, enabled ? 4 : 0);
        CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
        EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&parent), destination.GetParent()); EXPECT_EQ(&mesh, destination.GetCurrentMesh());
        EXPECT_EQ(&mesh, destination.GetMesh(0)); EXPECT_FLOAT_EQ(31, destination.GetWorldMatrix()[3][2]);
        EXPECT_EQ(enabled, destination.GetObjectAnimationCount()); if (enabled) EXPECT_EQ(&animation, destination.GetObjectAnimation(0));
        destination.SetParent(nullptr, TRUE); dependencies.StopDependencies();
    }
    source.SetParent(nullptr, TRUE);
}

TEST_F(Entity3dDependenciesTest, CopySkinOwnsIndependentIndicesWeightsAndNormalsThroughActualDestructor) {
    RCK3dEntity source(Context), bone(Context); CKSkin *skin = source.CreateSkin(); ASSERT_NE(nullptr, skin);
    skin->SetBoneCount(2); skin->GetBoneData(0)->SetBone(reinterpret_cast<CK3dEntity *>(&bone));
    skin->GetBoneData(1)->SetBone(reinterpret_cast<CK3dEntity *>(&bone)); skin->SetVertexCount(1);
    VxMatrix objectInitial = VxMatrix::Identity(); objectInitial[3][0] = 17; skin->SetObjectInitMatrix(objectInitial);
    VxMatrix boneInitial = VxMatrix::Identity(); boneInitial[3][1] = -5;
    skin->GetBoneData(0)->SetBoneInitialInverseMatrix(boneInitial);
    CKSkinVertexData *vertex = skin->GetVertexData(0); vertex->SetBoneCount(1); vertex->SetBone(0, 0); vertex->SetWeight(0, .75f);
    VxVector initial(2, 3, 4); vertex->SetInitialPos(initial); skin->SetNormalCount(1); skin->SetNormal(0, VxVector(0, 1, 0));
    {
        RCK3dEntity destination(Context); CKDependencies options; Options(options, 0); CKDependenciesContext dependencies(Context);
        dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); ASSERT_NE(nullptr, destination.m_Skin);
        EXPECT_NE(source.m_Skin, destination.m_Skin); CKSkinVertexData *copied = destination.m_Skin->GetVertexData(0); ASSERT_NE(nullptr, copied);
        EXPECT_FLOAT_EQ(.75f, copied->GetWeight(0)); EXPECT_EQ(0, copied->GetBone(0)); EXPECT_FLOAT_EQ(3, copied->GetInitialPos().y);
        EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&bone), destination.m_Skin->GetBoneData(0)->GetBone());
        EXPECT_FLOAT_EQ(17, destination.m_Skin->GetObjectInitMatrix()[3][0]);
        EXPECT_FLOAT_EQ(-5, static_cast<RCKSkinBoneData *>(destination.m_Skin->GetBoneData(0))->GetInitialInverseMatrix()[3][1]);
        copied->SetWeight(0, .25f); copied->SetBone(0, 1); destination.m_Skin->SetNormal(0, VxVector(1, 0, 0));
        EXPECT_FLOAT_EQ(.75f, vertex->GetWeight(0)); EXPECT_EQ(0, vertex->GetBone(0)); EXPECT_FLOAT_EQ(1, skin->GetNormal(0).y);
        // Destruction must release only the copy's owned buffers.
        dependencies.StopDependencies();
    }
    EXPECT_FLOAT_EQ(.75f, vertex->GetWeight(0)); EXPECT_EQ(0, vertex->GetBone(0));
    vertex->SetWeight(0, .5f); EXPECT_FLOAT_EQ(.5f, vertex->GetWeight(0));
}

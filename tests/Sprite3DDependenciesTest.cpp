#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKSprite3D.h"
#include "RCKMaterial.h"
#include "RCKMesh.h"
#include "RCKSkin.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SpriteDependencyMap : public CKDependenciesContext {
public:
    explicit SpriteDependencyMap(CKContext *context) : CKDependenciesContext(context) {}
    void Map(CK_ID from, CK_ID to) { m_MapID.Insert(from, to); }
};
class MaterialPrepareProbe : public RCKMaterial {
public:
    explicit MaterialPrepareProbe(CKContext *context) : RCKMaterial(context) {}
    int Calls = 0;
    CKERROR PrepareDependencies(CKDependenciesContext &) override { ++Calls; return CKERR_INVALIDPARAMETER; }
};
class MaterialSetterProbe : public RCKSprite3D {
public:
    explicit MaterialSetterProbe(CKContext *context) : RCKSprite3D(context) {}
    int Calls = 0;
    CKMaterial *Argument = nullptr;
    void SetMaterial(CKMaterial *material) override { ++Calls; Argument = material; RCKSprite3D::SetMaterial(material); }
};
class Sprite3DDependenciesTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite3D, CKCID_3DENTITY); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKMaterial *Material(RCKMaterial &material) { return reinterpret_cast<CKMaterial *>(&material); }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static void Options(CKDependencies &options, CKDWORD own = 0, CKDWORD entity = 0) {
        options.Resize(80); for (int i = 0; i < 80; ++i) options[i] = 0;
        options.m_Flags = CK_DEPENDENCIES_CUSTOM; options[CKCID_SPRITE3D] = own; options[CKCID_3DENTITY] = entity;
    }
};
}

TEST_F(Sprite3DDependenciesTest, PrepareOwnBitCollectsMaterialAndRecordsSpriteOnce) {
    RCKSprite3D sprite(Context); RCKMaterial material(Context); sprite.SetMaterial(Material(material)); CKDependencies options; Options(options, 1);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, sprite.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(sprite.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(material.GetID())); EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty());
    EXPECT_EQ(CKERR_ALREADYPRESENT, sprite.PrepareDependencies(dependencies)); dependencies.StopDependencies();
}
TEST_F(Sprite3DDependenciesTest, PrepareDisabledOwnBitDoesNotCollectMaterial) {
    RCKSprite3D sprite(Context); RCKMaterial material(Context); sprite.SetMaterial(Material(material)); CKDependencies options; Options(options);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, sprite.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(sprite.GetID()));
    EXPECT_FALSE(dependencies.IsDependenciesHere(material.GetID())); dependencies.StopDependencies();
}
TEST_F(Sprite3DDependenciesTest, PrepareOwnMaterialAndInheritedMeshAndChildTogether) {
    RCKSprite3D sprite(Context); RCKMaterial material(Context); RCKMesh mesh(Context); RCK3dEntity child(Context);
    sprite.SetMaterial(Material(material)); sprite.SetCurrentMesh(reinterpret_cast<CKMesh *>(&mesh), TRUE); child.SetParent(Entity(sprite), TRUE);
    ASSERT_EQ(Entity(sprite), child.GetParent()); CKDependencies options; Options(options, 1, 3);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, sprite.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(material.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(mesh.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(child.GetID())); dependencies.StopDependencies();
}
TEST_F(Sprite3DDependenciesTest, PrepareNullMaterialIsValidAndMaterialFailureIsNotPropagatedByNativeWrapper) {
    RCKSprite3D sprite(Context); MaterialPrepareProbe material(Context); CKDependencies options; Options(options, 1);
    CKDependenciesContext empty(Context); empty.StartDependencies(&options); ASSERT_EQ(CK_OK, sprite.PrepareDependencies(empty)); empty.StopDependencies();
    sprite.SetMaterial(Material(material)); CKDependenciesContext selected(Context); selected.StartDependencies(&options);
    ASSERT_EQ(CK_OK, sprite.PrepareDependencies(selected)); EXPECT_EQ(1, material.Calls);
    EXPECT_TRUE(selected.IsDependenciesHere(sprite.GetID())); EXPECT_FALSE(selected.IsDependenciesHere(material.GetID()));
    EXPECT_EQ(CKERR_ALREADYPRESENT, sprite.PrepareDependencies(selected)); EXPECT_EQ(1, material.Calls); selected.StopDependencies();
}
TEST_F(Sprite3DDependenciesTest, PrepareSaveNotToBeSavedPropagatesBaseFailureBeforeMaterial) {
    RCKSprite3D sprite(Context); MaterialPrepareProbe material(Context); sprite.SetMaterial(Material(material));
    sprite.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0); CKDependencies options; Options(options, 1);
    CKDependenciesContext dependencies(Context); dependencies.SetOperationMode(CK_DEPENDENCIES_SAVE); dependencies.StartDependencies(&options);
    EXPECT_EQ(CKERR_INCOMPATIBLEPARAMETERS, sprite.PrepareDependencies(dependencies)); EXPECT_EQ(0, material.Calls);
    EXPECT_FALSE(dependencies.IsDependenciesHere(sprite.GetID())); EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty()); dependencies.StopDependencies();
}
TEST_F(Sprite3DDependenciesTest, RemapUsesVirtualSetterEvenWhenOwnBitIsDisabled) {
    MaterialSetterProbe sprite(Context); RCKMaterial oldMaterial(Context), newMaterial(Context);
    sprite.SetMaterial(Material(oldMaterial)); sprite.Calls = 0; CKDependencies options; Options(options);
    SpriteDependencyMap dependencies(Context); dependencies.StartDependencies(&options); dependencies.Map(oldMaterial.GetID(), newMaterial.GetID());
    ASSERT_EQ(CK_OK, sprite.RemapDependencies(dependencies)); EXPECT_EQ(1, sprite.Calls); EXPECT_EQ(Material(newMaterial), sprite.Argument);
    EXPECT_EQ(Material(newMaterial), sprite.GetMaterial()); dependencies.StopDependencies();
}
TEST_F(Sprite3DDependenciesTest, RemapUnmappedMaterialKeepsIdentityAndExplicitZeroClearsReference) {
    MaterialSetterProbe sprite(Context); RCKMaterial material(Context); sprite.SetMaterial(Material(material)); sprite.Calls = 0;
    CKDependencies options; Options(options); SpriteDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, sprite.RemapDependencies(dependencies)); EXPECT_EQ(Material(material), sprite.GetMaterial()); EXPECT_EQ(1, sprite.Calls);
    dependencies.Map(material.GetID(), 0); ASSERT_EQ(CK_OK, sprite.RemapDependencies(dependencies)); EXPECT_EQ(nullptr, sprite.GetMaterial());
    EXPECT_EQ(2, sprite.Calls); ASSERT_EQ(CK_OK, sprite.RemapDependencies(dependencies)); EXPECT_EQ(3, sprite.Calls);
    EXPECT_EQ(nullptr, sprite.Argument); dependencies.StopDependencies();
}
TEST_F(Sprite3DDependenciesTest, CopyAlwaysTransfersRawOwnStateBeforeExplicitRemapping) {
    RCKSprite3D source(Context), destination(Context); RCKMaterial oldMaterial(Context), newMaterial(Context);
    Vx2DVector size(6, 10), offset(.25f, -.5f); VxRect rect(.1f, .2f, .7f, .9f);
    source.SetMaterial(Material(oldMaterial)); source.SetSize(size); source.SetOffset(offset); source.SetUVMapping(rect); source.SetMode(VXSPRITE3D_YROTATE);
    CKDependencies options; Options(options); SpriteDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(oldMaterial.GetID(), newMaterial.GetID()); ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
    EXPECT_EQ(Material(oldMaterial), destination.GetMaterial()); EXPECT_EQ(VXSPRITE3D_YROTATE, destination.GetMode());
    destination.GetSize(size); destination.GetOffset(offset); destination.GetUVMapping(rect);
    EXPECT_FLOAT_EQ(6, size.x); EXPECT_FLOAT_EQ(10, size.y); EXPECT_FLOAT_EQ(.25f, offset.x); EXPECT_FLOAT_EQ(-.5f, offset.y);
    EXPECT_FLOAT_EQ(.1f, rect.left); EXPECT_FLOAT_EQ(.2f, rect.top); EXPECT_FLOAT_EQ(.7f, rect.right); EXPECT_FLOAT_EQ(.9f, rect.bottom);
    EXPECT_EQ(CK_OK, destination.RemapDependencies(dependencies)); EXPECT_EQ(Material(newMaterial), destination.GetMaterial());
    EXPECT_EQ(Material(oldMaterial), source.GetMaterial()); size = Vx2DVector(2, 4); source.SetSize(size); destination.GetSize(size);
    EXPECT_FLOAT_EQ(6, size.x); EXPECT_FLOAT_EQ(10, size.y); dependencies.StopDependencies();
}
TEST_F(Sprite3DDependenciesTest, CopyEmptySourceClearsMaterialAndRestoresDefaultOwnState) {
    RCKSprite3D source(Context), destination(Context); RCKMaterial material(Context); Vx2DVector size(6, 10), offset(.25f, -.5f);
    VxRect rect(.1f, .2f, .7f, .9f); destination.SetMaterial(Material(material)); destination.SetSize(size); destination.SetOffset(offset);
    destination.SetUVMapping(rect); destination.SetMode(VXSPRITE3D_ORIENTABLE); CKDependencies options; Options(options, 1);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
    EXPECT_EQ(nullptr, destination.GetMaterial()); EXPECT_EQ(VXSPRITE3D_BILLBOARD, destination.GetMode());
    destination.GetSize(size); destination.GetOffset(offset); destination.GetUVMapping(rect);
    EXPECT_FLOAT_EQ(2, size.x); EXPECT_FLOAT_EQ(2, size.y); EXPECT_FLOAT_EQ(0, offset.x); EXPECT_FLOAT_EQ(0, offset.y);
    EXPECT_FLOAT_EQ(0, rect.left); EXPECT_FLOAT_EQ(0, rect.top); EXPECT_FLOAT_EQ(1, rect.right); EXPECT_FLOAT_EQ(1, rect.bottom);
    EXPECT_EQ(&material, Context->GetObject(material.GetID())); dependencies.StopDependencies();
}
TEST_F(Sprite3DDependenciesTest, SelfCopyPreservesRealSkinBoneVertexMeshAndOwnState) {
    RCKSprite3D sprite(Context); RCKMaterial material(Context); RCKMesh mesh(Context); RCK3dEntity bone(Context);
    sprite.SetMaterial(Material(material)); sprite.SetCurrentMesh(reinterpret_cast<CKMesh *>(&mesh), TRUE); sprite.SetMode(VXSPRITE3D_XROTATE);
    Vx2DVector size(6, 10), offset(.25f, -.5f); VxRect rect(.1f, .2f, .7f, .9f);
    sprite.SetSize(size); sprite.SetOffset(offset); sprite.SetUVMapping(rect); CKSkin *skin = sprite.CreateSkin(); ASSERT_NE(nullptr, skin);
    skin->SetBoneCount(1); skin->GetBoneData(0)->SetBone(Entity(bone)); skin->SetVertexCount(1);
    CKSkinVertexData *vertex = skin->GetVertexData(0); vertex->SetBoneCount(1); vertex->SetBone(0, 0); vertex->SetWeight(0, .75f);
    vertex->SetInitialPos(VxVector(7, 11, 13)); CKDependencies options; Options(options, 1);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, sprite.Copy(sprite, dependencies));
    EXPECT_EQ(skin, sprite.GetSkin()); ASSERT_EQ(1, skin->GetBoneCount()); EXPECT_EQ(Entity(bone), skin->GetBoneData(0)->GetBone());
    ASSERT_EQ(1, skin->GetVertexCount()); EXPECT_EQ(vertex, skin->GetVertexData(0)); EXPECT_FLOAT_EQ(.75f, vertex->GetWeight(0));
    EXPECT_FLOAT_EQ(7, vertex->GetInitialPos().x); EXPECT_FLOAT_EQ(11, vertex->GetInitialPos().y); EXPECT_FLOAT_EQ(13, vertex->GetInitialPos().z);
    EXPECT_EQ(Material(material), sprite.GetMaterial()); EXPECT_EQ(reinterpret_cast<CKMesh *>(&mesh), sprite.GetCurrentMesh()); EXPECT_EQ(1, sprite.GetMeshCount());
    EXPECT_EQ(VXSPRITE3D_XROTATE, sprite.GetMode()); sprite.GetSize(size); sprite.GetOffset(offset); sprite.GetUVMapping(rect);
    EXPECT_FLOAT_EQ(6, size.x); EXPECT_FLOAT_EQ(10, size.y); EXPECT_FLOAT_EQ(.25f, offset.x); EXPECT_FLOAT_EQ(-.5f, offset.y);
    EXPECT_FLOAT_EQ(.1f, rect.left); EXPECT_FLOAT_EQ(.9f, rect.bottom); dependencies.StopDependencies();
}
TEST_F(Sprite3DDependenciesTest, ContextCopySelectedMaterialClonesAndRemapsOnlyNewSprite) {
    RCKSprite3D *source = static_cast<RCKSprite3D *>(Context->CreateObject(CKCID_SPRITE3D));
    RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL)); ASSERT_NE(nullptr, source); ASSERT_NE(nullptr, material);
    source->SetMaterial(Material(*material)); source->SetMode(VXSPRITE3D_ORIENTABLE); CKDependencies options; Options(options, 1);
    CKObject *copiedObject = Context->CopyObject(source, &options); ASSERT_NE(nullptr, copiedObject);
    RCKSprite3D *copy = static_cast<RCKSprite3D *>(copiedObject); EXPECT_NE(source, copy); ASSERT_NE(nullptr, copy->GetMaterial());
    EXPECT_NE(Material(*material), copy->GetMaterial()); EXPECT_EQ(CKCID_MATERIAL, copy->GetMaterial()->GetClassID());
    EXPECT_EQ(Material(*material), source->GetMaterial()); EXPECT_EQ(VXSPRITE3D_ORIENTABLE, copy->GetMode());
}
TEST_F(Sprite3DDependenciesTest, ContextCopyWithoutMaterialBitKeepsBorrowedMaterialIdentity) {
    RCKSprite3D *source = static_cast<RCKSprite3D *>(Context->CreateObject(CKCID_SPRITE3D));
    RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL)); ASSERT_NE(nullptr, source); ASSERT_NE(nullptr, material);
    source->SetMaterial(Material(*material)); CKDependencies options; Options(options);
    CKObject *copiedObject = Context->CopyObject(source, &options); ASSERT_NE(nullptr, copiedObject);
    EXPECT_EQ(Material(*material), static_cast<RCKSprite3D *>(copiedObject)->GetMaterial()); EXPECT_EQ(Material(*material), source->GetMaterial());
}

#include <gtest/gtest.h>
#include <cstdint>
#include "CKAll.h"
#include "RCKSprite3D.h"
#include "RCKMaterial.h"
#include "RCKMesh.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SpriteLayoutProbe : public RCKSprite3D {
public:
    static void RecordLayout(RCKSprite3D &sprite) {
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(&sprite);
        std::size_t previous = sizeof(RCK3dEntity);
#define FIELD(field) { auto member = &SpriteLayoutProbe::field; \
        const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(sprite.*member)); \
        const std::size_t offset = address - begin; EXPECT_GE(offset, previous); \
        EXPECT_LE(offset + sizeof(sprite.*member), sizeof(RCKSprite3D)); \
        EXPECT_EQ(0u, address % alignof(decltype(sprite.*member))); \
        ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(offset)); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(sprite.*member))); \
        previous = offset + sizeof(sprite.*member); }
        FIELD(m_Material); FIELD(m_Mode); FIELD(m_Offset); FIELD(m_Rect);
#undef FIELD
        ::testing::Test::RecordProperty("SDLSize", static_cast<int>(sizeof(RCKSprite3D)));
        ::testing::Test::RecordProperty("SDLAlignment", static_cast<int>(alignof(RCKSprite3D)));
        ::testing::Test::RecordProperty("SDLBaseSize", static_cast<int>(sizeof(RCK3dEntity)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
        ::testing::Test::RecordProperty("m_ClassID_value", static_cast<int>(m_ClassID));
        ::testing::Test::RecordProperty("m_ClassID_size", static_cast<int>(sizeof(m_ClassID)));
    }
};
class Sprite3DLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite3D, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKMaterial *Material(RCKMaterial &material) { return reinterpret_cast<CKMaterial *>(&material); }
};
}

TEST_F(Sprite3DLifecycleTest, RegisteredDefaultsAndActualPlatformLayout) {
    RCKSprite3D *sprite = static_cast<RCKSprite3D *>(Context->CreateObject(CKCID_SPRITE3D)); ASSERT_NE(nullptr, sprite);
    EXPECT_EQ(37, sprite->GetClassID()); EXPECT_EQ(37, RCKSprite3D::m_ClassID);
    EXPECT_TRUE(CKIsChildClassOf(sprite, CKCID_3DENTITY)); EXPECT_EQ(sprite, Context->GetObject(sprite->GetID()));
    EXPECT_EQ(nullptr, sprite->GetMaterial()); EXPECT_EQ(VXSPRITE3D_BILLBOARD, sprite->GetMode());
    Vx2DVector size, offset; VxRect rect; sprite->GetSize(size); sprite->GetOffset(offset); sprite->GetUVMapping(rect);
    EXPECT_FLOAT_EQ(2, size.x); EXPECT_FLOAT_EQ(2, size.y); EXPECT_FLOAT_EQ(0, offset.x); EXPECT_FLOAT_EQ(0, offset.y);
    EXPECT_FLOAT_EQ(0, rect.left); EXPECT_FLOAT_EQ(0, rect.top); EXPECT_FLOAT_EQ(1, rect.right); EXPECT_FLOAT_EQ(1, rect.bottom);
    SpriteLayoutProbe::RecordLayout(*sprite);
}
TEST_F(Sprite3DLifecycleTest, NamedConstructionCopiesNameAndKeepsIndependentState) {
    char name[] = "Named 3D sprite"; RCKSprite3D first(Context, name), second(Context); name[0] = 'X';
    EXPECT_STREQ("Named 3D sprite", first.GetName()); EXPECT_EQ(nullptr, second.GetName());
    first.SetMode(VXSPRITE3D_ORIENTABLE); EXPECT_EQ(VXSPRITE3D_BILLBOARD, second.GetMode());
    first.SetPriority(17); first.SetZOrder(23); EXPECT_EQ(17, first.GetPriority()); EXPECT_EQ(23, first.GetZOrder());
    EXPECT_EQ(0, second.GetPriority()); EXPECT_EQ(0, second.GetZOrder());
}
TEST_F(Sprite3DLifecycleTest, ClassNameParameterNotificationAndDefaultCopyRegistration) {
    EXPECT_STREQ("3D Sprite", RCKSprite3D::GetClassName()); CKClassDesc *desc = CKGetClassDesc(CKCID_SPRITE3D);
    ASSERT_NE(nullptr, desc); desc->ToBeNotify.Clear(); desc->Parameter = CKGUID(); desc->DefaultCopyDependencies = 0;
    RCKSprite3D::Register(); EXPECT_TRUE(desc->ToBeNotify.IsSet(CKCID_MATERIAL));
    EXPECT_EQ(CKPGUID_SPRITE3D, desc->Parameter); EXPECT_EQ(600311323u, desc->Parameter.d1);
    EXPECT_EQ(1879259687u, desc->Parameter.d2); EXPECT_EQ(1u, desc->DefaultCopyDependencies);
    EXPECT_STREQ("3D Sprite", desc->NameFct());
}
TEST_F(Sprite3DLifecycleTest, DependencyMetadataFollowsActualNativeModeTable) {
    const int modes[] = {-1, 0, 1, 2, 3, 4, 5, 100}; CKClassDesc *desc = CKGetClassDesc(CKCID_SPRITE3D);
    ASSERT_NE(nullptr, desc);
    for (int mode : modes) {
        const int count = mode == 1 || mode == 2 || mode == 4 ? 1 : 0;
        EXPECT_EQ(count, RCKSprite3D::GetDependenciesCount(mode)); EXPECT_EQ(count, desc->DependsCountFct(mode));
        EXPECT_STREQ("Material", RCKSprite3D::GetDependencies(0, mode)); EXPECT_STREQ("Material", desc->DependsFct(0, mode));
        EXPECT_EQ(nullptr, RCKSprite3D::GetDependencies(-1, mode)); EXPECT_EQ(nullptr, RCKSprite3D::GetDependencies(1, mode));
    }
}
TEST_F(Sprite3DLifecycleTest, DirectFactorySDKDispatchAndVirtualDestructionDoNotOwnMaterial) {
    RCKMaterial material(Context); CKSprite3D *api = RCKSprite3D::CreateInstance(Context); ASSERT_NE(nullptr, api);
    RCKSprite3D *sprite = reinterpret_cast<RCKSprite3D *>(api); const CK_ID id = sprite->GetID();
    EXPECT_EQ(37, api->GetClassID()); EXPECT_EQ(nullptr, api->GetName()); api->SetMaterial(Material(material));
    EXPECT_EQ(Material(material), api->GetMaterial()); api->SetMode(VXSPRITE3D_YROTATE); EXPECT_EQ(VXSPRITE3D_YROTATE, sprite->GetMode());
    delete static_cast<CKObject *>(sprite); EXPECT_EQ(nullptr, Context->GetObject(id));
    EXPECT_EQ(&material, Context->GetObject(material.GetID()));
}
TEST_F(Sprite3DLifecycleTest, MemoryOccupationIncludesActualTailButNotBorrowedMaterial) {
    RCKSprite3D sprite(Context); RCKMaterial material(Context);
    const int tail = static_cast<int>(sizeof(RCKSprite3D) - sizeof(RCK3dEntity));
    EXPECT_EQ(sprite.RCK3dEntity::GetMemoryOccupation() + tail, sprite.GetMemoryOccupation());
    const int before = sprite.GetMemoryOccupation(); sprite.SetMaterial(Material(material)); material.SetName("Borrowed material name storage");
    EXPECT_EQ(before, sprite.GetMemoryOccupation()); sprite.SetName("Owned sprite name storage is accounted by its base");
    EXPECT_EQ(sprite.RCK3dEntity::GetMemoryOccupation() + tail, sprite.GetMemoryOccupation());
}
TEST_F(Sprite3DLifecycleTest, MaterialStateIsBorrowedAndDoesNotInvalidateSpriteGeometry) {
    RCKSprite3D sprite(Context); RCKMaterial first(Context), second(Context); Vx2DVector size(6, 10), offset(.25f, -.5f);
    sprite.SetSize(size); sprite.SetOffset(offset); sprite.SetMaterial(Material(first));
    EXPECT_EQ(Material(first), sprite.GetMaterial()); sprite.SetMaterial(Material(second)); EXPECT_EQ(Material(second), sprite.GetMaterial());
    sprite.SetMaterial(nullptr); EXPECT_EQ(nullptr, sprite.GetMaterial()); sprite.GetSize(size); sprite.GetOffset(offset);
    EXPECT_FLOAT_EQ(6, size.x); EXPECT_FLOAT_EQ(10, size.y); EXPECT_FLOAT_EQ(.25f, offset.x); EXPECT_FLOAT_EQ(-.5f, offset.y);
    EXPECT_EQ(&first, Context->GetObject(first.GetID())); EXPECT_EQ(&second, Context->GetObject(second.GetID()));
}
TEST_F(Sprite3DLifecycleTest, ModeStateRetainsAllNativeValuesWithoutMutatingSizeOrMaterial) {
    RCKSprite3D sprite(Context); RCKMaterial material(Context); sprite.SetMaterial(Material(material));
    const VXSPRITE3D_TYPE modes[] = {VXSPRITE3D_BILLBOARD, VXSPRITE3D_XROTATE, VXSPRITE3D_YROTATE, VXSPRITE3D_ORIENTABLE, static_cast<VXSPRITE3D_TYPE>(123)};
    for (VXSPRITE3D_TYPE mode : modes) { sprite.SetMode(mode); EXPECT_EQ(mode, sprite.GetMode()); EXPECT_EQ(Material(material), sprite.GetMaterial()); }
    Vx2DVector size; sprite.GetSize(size); EXPECT_FLOAT_EQ(2, size.x); EXPECT_FLOAT_EQ(2, size.y);
}
TEST_F(Sprite3DLifecycleTest, CheckPreDeletionClearsOnlyFlaggedMaterialAndPreservesSurvivor) {
    RCKSprite3D sprite(Context); RCKMaterial first(Context), second(Context); sprite.SetMaterial(Material(first));
    sprite.CheckPreDeletion(); EXPECT_EQ(Material(first), sprite.GetMaterial());
    second.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); sprite.CheckPreDeletion(); EXPECT_EQ(Material(first), sprite.GetMaterial());
    first.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); sprite.CheckPreDeletion(); EXPECT_EQ(nullptr, sprite.GetMaterial());
    first.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED); second.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
    sprite.SetMaterial(Material(second)); sprite.CheckPreDeletion(); EXPECT_EQ(Material(second), sprite.GetMaterial());
}
TEST_F(Sprite3DLifecycleTest, ContextMaterialDeletionNotifiesBothSpritesAndKeepsOtherMaterial) {
    RCKSprite3D *first = static_cast<RCKSprite3D *>(Context->CreateObject(CKCID_SPRITE3D));
    RCKSprite3D *second = static_cast<RCKSprite3D *>(Context->CreateObject(CKCID_SPRITE3D));
    RCKSprite3D *third = static_cast<RCKSprite3D *>(Context->CreateObject(CKCID_SPRITE3D));
    RCKMaterial *doomed = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
    RCKMaterial *survivor = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); ASSERT_NE(nullptr, third); ASSERT_NE(nullptr, doomed); ASSERT_NE(nullptr, survivor);
    first->SetMaterial(Material(*doomed)); second->SetMaterial(Material(*doomed)); third->SetMaterial(Material(*survivor));
    const CK_ID id = doomed->GetID(); CKDependencies options; options.m_Flags = CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK, Context->DestroyObject(doomed, 0, &options)); EXPECT_EQ(nullptr, Context->GetObject(id));
    EXPECT_EQ(nullptr, first->GetMaterial()); EXPECT_EQ(nullptr, second->GetMaterial()); EXPECT_EQ(Material(*survivor), third->GetMaterial());
}
TEST_F(Sprite3DLifecycleTest, IsObjectUsedRecognizesExactMaterialAndInheritedMesh) {
    RCKSprite3D sprite(Context); RCKMaterial material(Context), other(Context); RCKMesh mesh(Context); sprite.SetMaterial(Material(material));
    sprite.SetCurrentMesh(reinterpret_cast<CKMesh *>(&mesh), TRUE);
    EXPECT_TRUE(sprite.IsObjectUsed(&material, CKCID_MATERIAL)); EXPECT_TRUE(sprite.IsObjectUsed(&material, CKCID_OBJECT));
    EXPECT_FALSE(sprite.IsObjectUsed(&other, CKCID_MATERIAL)); EXPECT_TRUE(sprite.IsObjectUsed(&mesh, CKCID_MESH));
    EXPECT_FALSE(sprite.IsObjectUsed(&mesh, CKCID_MATERIAL));
}
TEST_F(Sprite3DLifecycleTest, NullPointerIsNotAnObjectDependencyWithOrWithoutMaterial) {
    RCKSprite3D sprite(Context); RCKMaterial material(Context);
    EXPECT_FALSE(sprite.IsObjectUsed(nullptr, CKCID_MATERIAL)); EXPECT_FALSE(sprite.IsObjectUsed(nullptr, CKCID_OBJECT));
    sprite.SetMaterial(Material(material)); EXPECT_FALSE(sprite.IsObjectUsed(nullptr, CKCID_MATERIAL));
    sprite.SetMaterial(nullptr); EXPECT_FALSE(sprite.IsObjectUsed(nullptr, CKCID_MESH));
}

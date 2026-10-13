#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKSprite3D.h"
#include "RCKMaterial.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SerializedSprite3D : public RCKSprite3D {
public:
    explicit SerializedSprite3D(CKContext *context) : RCKSprite3D(context) {}
    void Seed(CKMaterial *material = nullptr) {
        m_Mode = 0x13579bdf; m_Material = reinterpret_cast<RCKMaterial *>(material);
        m_Offset = Vx2DVector(.25f, -.5f);
        m_Rect.left = -.125f; m_Rect.top = .25f; m_Rect.right = .75f; m_Rect.bottom = 1.5f;
        m_LocalBoundingBox.Min = VxVector(-3, -5, 7);
        m_LocalBoundingBox.Max = VxVector(9, 15, 11);
    }
};
class ObservedLoadedSprite3D : public SerializedSprite3D {
public:
    explicit ObservedLoadedSprite3D(CKContext *context) : SerializedSprite3D(context) {}
    int Calls = 0;
    int Sequence[3] = {};
    CKDWORD ModeAtSize = 0;
    Vx2DVector OffsetAtSize;
    Vx2DVector OffsetAtOffset;
    VxRect MappingAtMapping;
    int MaterialSetterCalls = 0;
    void SetSize(Vx2DVector &size) override {
        Sequence[Calls++] = 1; ModeAtSize = m_Mode; OffsetAtSize = m_Offset;
        SerializedSprite3D::SetSize(size);
    }
    void SetOffset(Vx2DVector &offset) override {
        Sequence[Calls++] = 2; OffsetAtOffset = m_Offset;
        SerializedSprite3D::SetOffset(offset);
    }
    void SetUVMapping(VxRect &rect) override {
        Sequence[Calls++] = 3; MappingAtMapping = m_Rect;
        SerializedSprite3D::SetUVMapping(rect);
    }
    void SetMaterial(CKMaterial *material) override {
        ++MaterialSetterCalls; SerializedSprite3D::SetMaterial(material);
    }
};
class DerivedSerializedSprite3D : public SerializedSprite3D {
public:
    explicit DerivedSerializedSprite3D(CKContext *context) : SerializedSprite3D(context) {}
    CK_CLASSID GetClassID() override { return CKCID_CAMERA; }
};
class Sprite3DSerializationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_OBJECT);
        CKCLASSREGISTERCID(RCKSprite3D, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKMaterial *Material(RCKMaterial &material) { return reinterpret_cast<CKMaterial *>(&material); }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static void Begin(CKStateChunk &chunk, int version = 6) { chunk.StartWrite(); chunk.SetDataVersion(version); }
    static void Wire(CKStateChunk &chunk, int material, float halfWidth = 3, float halfHeight = 5) {
        chunk.WriteIdentifier(0x00400000); chunk.WriteDword(0xfedcba98);
        chunk.WriteFloat(halfWidth); chunk.WriteFloat(halfHeight);
        chunk.WriteFloat(.5f); chunk.WriteFloat(-.25f);
        chunk.WriteFloat(-.5f); chunk.WriteFloat(.125f); chunk.WriteFloat(1.25f); chunk.WriteFloat(2);
        chunk.WriteInt(material);
    }
    static CKERROR Load(RCKSprite3D &sprite, CKStateChunk &chunk, CKFile *file = nullptr) {
        chunk.CloseChunk(); return sprite.Load(&chunk, file);
    }
    static void ExpectLoaded(RCKSprite3D &sprite, CKMaterial *material, float width = 6, float height = 10) {
        EXPECT_EQ(0xfedcba98u, static_cast<CKDWORD>(sprite.GetMode()));
        Vx2DVector size, offset; VxRect rect;
        sprite.GetSize(size); sprite.GetOffset(offset); sprite.GetUVMapping(rect);
        EXPECT_FLOAT_EQ(width, size.x); EXPECT_FLOAT_EQ(height, size.y);
        EXPECT_FLOAT_EQ(.5f, offset.x); EXPECT_FLOAT_EQ(-.25f, offset.y);
        EXPECT_FLOAT_EQ(-.5f, rect.left); EXPECT_FLOAT_EQ(.125f, rect.top);
        EXPECT_FLOAT_EQ(1.25f, rect.right); EXPECT_FLOAT_EQ(2, rect.bottom);
        EXPECT_EQ(material, sprite.GetMaterial()); EXPECT_FALSE(sprite.IsUpToDate());
    }
    static void ExpectSeed(SerializedSprite3D &sprite, CKMaterial *material) {
        EXPECT_EQ(0x13579bdfu, static_cast<CKDWORD>(sprite.GetMode()));
        Vx2DVector size, offset; VxRect rect;
        sprite.GetSize(size); sprite.GetOffset(offset); sprite.GetUVMapping(rect);
        EXPECT_FLOAT_EQ(12, size.x); EXPECT_FLOAT_EQ(20, size.y);
        EXPECT_FLOAT_EQ(.25f, offset.x); EXPECT_FLOAT_EQ(-.5f, offset.y);
        EXPECT_FLOAT_EQ(-.125f, rect.left); EXPECT_FLOAT_EQ(.25f, rect.top);
        EXPECT_FLOAT_EQ(.75f, rect.right); EXPECT_FLOAT_EQ(1.5f, rect.bottom);
        EXPECT_EQ(material, sprite.GetMaterial());
    }
    static void ExpectPayload(CKStateChunk &chunk, int material) {
        ASSERT_TRUE(chunk.SeekIdentifier(0x00400000)); EXPECT_EQ(0x13579bdfu, chunk.ReadDword());
        const float values[] = {6, 10, .25f, -.5f, -.125f, .25f, .75f, 1.5f};
        for (float value : values) EXPECT_FLOAT_EQ(value, chunk.ReadFloat());
        EXPECT_EQ(material, chunk.ReadInt());
    }
    static void Prepare(CKFile &file) { file.m_IndexByClassId.Resize(CKGetClassCount()); }
};
}

TEST_F(Sprite3DSerializationTest, NullLoadPreservesOwnStatePoseAndValidity) {
    SerializedSprite3D sprite(Context); RCKMaterial material(Context); sprite.Seed(Material(material));
    VxVector pos(7, 11, 13); sprite.SetPosition(&pos); sprite.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, sprite.Load(nullptr, nullptr)); ExpectSeed(sprite, Material(material));
    sprite.GetPosition(&pos); EXPECT_FLOAT_EQ(7, pos.x); EXPECT_FLOAT_EQ(11, pos.y); EXPECT_FLOAT_EQ(13, pos.z);
    EXPECT_TRUE(sprite.IsUpToDate());
}
TEST_F(Sprite3DSerializationTest, MissingOwnSectionPreservesSpriteFieldsAndOwnValidityAcrossVersions) {
    SerializedSprite3D sprite(Context); RCKMaterial material(Context); sprite.Seed(Material(material));
    for (int version : {0, 4, 5, 6, 8}) {
        CKStateChunk chunk(CKCID_SPRITE3D, nullptr); Begin(chunk, version);
        chunk.WriteIdentifier(0x76543210); chunk.WriteInt(41); sprite.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
        ASSERT_EQ(CK_OK, Load(sprite, chunk)); ExpectSeed(sprite, Material(material)); EXPECT_TRUE(sprite.IsUpToDate());
    }
}
TEST_F(Sprite3DSerializationTest, LiteralFilelessPayloadUsesRawObjectIDAndSameWireAcrossVersions) {
    SerializedSprite3D sprite(Context); RCKMaterial material(Context);
    for (int version : {0, 4, 5, 6, 8}) {
        sprite.Seed(); CKStateChunk chunk(CKCID_SPRITE3D, nullptr); Begin(chunk, version); Wire(chunk, material.GetID());
        sprite.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); ASSERT_EQ(CK_OK, Load(sprite, chunk)); ExpectLoaded(sprite, Material(material));
    }
}
TEST_F(Sprite3DSerializationTest, LiteralFilePayloadResolvesRegisteredIndexZeroAcrossVersions) {
    SerializedSprite3D sprite(Context); RCKMaterial material(Context); CKFile file(Context);
    file.m_FileObjects.Resize(1); file.m_FileObjects[0].CreatedObject = material.GetID();
    for (int version : {0, 4, 5, 6, 8}) {
        CKStateChunk chunk(CKCID_SPRITE3D, &file); Begin(chunk, version); Wire(chunk, 0);
        ASSERT_EQ(CK_OK, Load(sprite, chunk, &file)); ExpectLoaded(sprite, Material(material));
    }
}
TEST_F(Sprite3DSerializationTest, FileMinusOneAndUnresolvedMaterialReplaceTheOldReferenceWithNull) {
    SerializedSprite3D sprite(Context); RCKMaterial material(Context); CKFile file(Context);
    for (int index : {-1, 0, 97}) {
        sprite.Seed(Material(material)); CKStateChunk chunk(CKCID_SPRITE3D, &file); Begin(chunk); Wire(chunk, index);
        ASSERT_EQ(CK_OK, Load(sprite, chunk, &file)); ExpectLoaded(sprite, nullptr);
    }
}
TEST_F(Sprite3DSerializationTest, FilelessZeroMaterialClearsThePreviousReference) {
    SerializedSprite3D sprite(Context); RCKMaterial material(Context); sprite.Seed(Material(material));
    CKStateChunk chunk(CKCID_SPRITE3D, nullptr); Begin(chunk); Wire(chunk, 0);
    ASSERT_EQ(CK_OK, Load(sprite, chunk)); ExpectLoaded(sprite, nullptr);
}
TEST_F(Sprite3DSerializationTest, LoadUsesVirtualSizeOffsetMappingInNativeOrderAndAssignsMaterialDirectly) {
    ObservedLoadedSprite3D sprite(Context); RCKMaterial material(Context); sprite.Seed();
    CKStateChunk chunk(CKCID_SPRITE3D, nullptr); Begin(chunk); Wire(chunk, material.GetID());
    ASSERT_EQ(CK_OK, Load(sprite, chunk)); ASSERT_EQ(3, sprite.Calls);
    EXPECT_EQ(1, sprite.Sequence[0]); EXPECT_EQ(2, sprite.Sequence[1]); EXPECT_EQ(3, sprite.Sequence[2]);
    EXPECT_EQ(0xfedcba98u, sprite.ModeAtSize); EXPECT_FLOAT_EQ(.25f, sprite.OffsetAtSize.x); EXPECT_FLOAT_EQ(-.5f, sprite.OffsetAtSize.y);
    EXPECT_FLOAT_EQ(.5f, sprite.OffsetAtOffset.x); EXPECT_FLOAT_EQ(-.25f, sprite.OffsetAtOffset.y);
    EXPECT_FLOAT_EQ(-.5f, sprite.MappingAtMapping.left); EXPECT_FLOAT_EQ(.125f, sprite.MappingAtMapping.top);
    EXPECT_FLOAT_EQ(1.25f, sprite.MappingAtMapping.right); EXPECT_FLOAT_EQ(2, sprite.MappingAtMapping.bottom);
    EXPECT_EQ(0, sprite.MaterialSetterCalls); ExpectLoaded(sprite, Material(material));
}
TEST_F(Sprite3DSerializationTest, LoadedHalfDimensionsAndNewOffsetProduceTheExactPlanarBox) {
    SerializedSprite3D sprite(Context); sprite.Seed(); CKStateChunk chunk(CKCID_SPRITE3D, nullptr); Begin(chunk); Wire(chunk, 0);
    ASSERT_EQ(CK_OK, Load(sprite, chunk)); EXPECT_FLOAT_EQ(-1.5f, sprite.m_LocalBoundingBox.Min.x);
    EXPECT_FLOAT_EQ(4.5f, sprite.m_LocalBoundingBox.Max.x); EXPECT_FLOAT_EQ(-6.25f, sprite.m_LocalBoundingBox.Min.y);
    EXPECT_FLOAT_EQ(3.75f, sprite.m_LocalBoundingBox.Max.y);
    EXPECT_FLOAT_EQ(0, sprite.m_LocalBoundingBox.Min.z); EXPECT_FLOAT_EQ(0, sprite.m_LocalBoundingBox.Max.z);
}
TEST_F(Sprite3DSerializationTest, ZeroAndSignedDimensionsRemainUnclampedWireValues) {
    SerializedSprite3D sprite(Context); CKStateChunk chunk(CKCID_SPRITE3D, nullptr); Begin(chunk); Wire(chunk, 0, -3, 0);
    ASSERT_EQ(CK_OK, Load(sprite, chunk)); ExpectLoaded(sprite, nullptr, -6, 0);
}
TEST_F(Sprite3DSerializationTest, OwnLoadAlsoAppliesIndependentAncestorWorldPoseAndVisibility) {
    SerializedSprite3D sprite(Context); sprite.SetMoveableFlags(sprite.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    CKStateChunk chunk(CKCID_SPRITE3D, nullptr); Begin(chunk);
    chunk.WriteIdentifier(4); chunk.WriteInt(0);
    chunk.WriteIdentifier(0x00100000); chunk.WriteDword(0); chunk.WriteDword(VX_MOVEABLE_PICKABLE | VX_MOVEABLE_VISIBLE);
    chunk.WriteVector(VxVector(1, 0, 0)); chunk.WriteVector(VxVector(0, 1, 0)); chunk.WriteVector(VxVector(0, 0, 1));
    chunk.WriteVector(VxVector(7, 11, 13)); Wire(chunk, 0); ASSERT_EQ(CK_OK, Load(sprite, chunk));
    EXPECT_FLOAT_EQ(7, sprite.GetWorldMatrix()[3][0]); EXPECT_FLOAT_EQ(11, sprite.GetWorldMatrix()[3][1]);
    EXPECT_FLOAT_EQ(13, sprite.GetWorldMatrix()[3][2]); EXPECT_FALSE(sprite.IsVisible());
    EXPECT_TRUE(sprite.GetMoveableFlags() & VX_MOVEABLE_PICKABLE); ExpectLoaded(sprite, nullptr);
}
TEST_F(Sprite3DSerializationTest, FilelessZeroMaskReturnsAncestorWithoutSpritePayload) {
    SerializedSprite3D sprite(Context); sprite.Seed(); CKStateChunk *chunk = sprite.Save(nullptr, 0);
    ASSERT_NE(nullptr, chunk); EXPECT_NE(CKCID_SPRITE3D, chunk->GetChunkClassID()); chunk->StartRead();
    EXPECT_FALSE(chunk->SeekIdentifier(0x00400000)); DeleteCKStateChunk(chunk);
}
TEST_F(Sprite3DSerializationTest, FilelessEntityMaskRetainsAncestorPoseWithoutSpritePayload) {
    SerializedSprite3D sprite(Context); sprite.Seed(); VxVector pos(7, 11, 13); sprite.SetPosition(&pos);
    CKStateChunk *chunk = sprite.Save(nullptr, CK_STATESAVE_3DENTITYMATRIX); ASSERT_NE(nullptr, chunk);
    EXPECT_EQ(CKCID_3DENTITY, chunk->GetChunkClassID()); chunk->StartRead();
    EXPECT_TRUE(chunk->SeekIdentifier(0x00100000)); EXPECT_FALSE(chunk->SeekIdentifier(0x00400000)); DeleteCKStateChunk(chunk);
}
TEST_F(Sprite3DSerializationTest, EveryOwnReservedSaveBitEmitsTheEntireOrderedPayload) {
    SerializedSprite3D sprite(Context); RCKMaterial material(Context); sprite.Seed(Material(material));
    for (CKDWORD bit = 0x00400000; bit <= 0x08000000; bit <<= 1) {
        CKStateChunk *chunk = sprite.Save(nullptr, bit); ASSERT_NE(nullptr, chunk);
        EXPECT_EQ(CKCID_SPRITE3D, chunk->GetChunkClassID()); EXPECT_EQ(-1, chunk->GetCurrentPos());
        chunk->StartRead(); ExpectPayload(*chunk, material.GetID()); DeleteCKStateChunk(chunk);
    }
}
TEST_F(Sprite3DSerializationTest, FileZeroMaskMergesAncestorPoseAndWritesRegisteredMaterialIndexZero) {
    SerializedSprite3D sprite(Context); RCKMaterial material(Context); sprite.Seed(Material(material)); CKFile file(Context); Prepare(file);
    file.SaveObject(&material, 0); VxVector pos(7, 11, 13); sprite.SetPosition(&pos);
    CKStateChunk *chunk = sprite.Save(&file, 0); ASSERT_NE(nullptr, chunk); EXPECT_EQ(CKCID_SPRITE3D, chunk->GetChunkClassID());
    EXPECT_EQ(-1, chunk->GetCurrentPos()); chunk->StartRead(); ExpectPayload(*chunk, 0);
    ASSERT_TRUE(chunk->SeekIdentifier(0x00100000)); chunk->ReadDword(); chunk->ReadDword();
    const float pose[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 7, 11, 13};
    for (float value : pose) EXPECT_FLOAT_EQ(value, chunk->ReadFloat()); DeleteCKStateChunk(chunk);
}
TEST_F(Sprite3DSerializationTest, FileSaveUnregisteredAndNullMaterialsUseMinusOne) {
    SerializedSprite3D sprite(Context); RCKMaterial material(Context); CKFile file(Context);
    for (CKMaterial *reference : {Material(material), static_cast<CKMaterial *>(nullptr)}) {
        sprite.Seed(reference); CKStateChunk *chunk = sprite.Save(&file, 0); ASSERT_NE(nullptr, chunk);
        chunk->StartRead(); ExpectPayload(*chunk, -1); DeleteCKStateChunk(chunk);
    }
}
TEST_F(Sprite3DSerializationTest, DerivedSaveKeepsChunkOpenForAnAdditionalDerivedSection) {
    DerivedSerializedSprite3D sprite(Context); sprite.Seed(); CKStateChunk *chunk = sprite.Save(nullptr, 0x00400000);
    ASSERT_NE(nullptr, chunk); EXPECT_NE(-1, chunk->GetCurrentPos()); chunk->WriteIdentifier(0x76543210); chunk->WriteInt(97);
    chunk->CloseChunk(); chunk->StartRead(); ExpectPayload(*chunk, 0);
    ASSERT_TRUE(chunk->SeekIdentifier(0x76543210)); EXPECT_EQ(97, chunk->ReadInt()); DeleteCKStateChunk(chunk);
}
TEST_F(Sprite3DSerializationTest, PreSaveRegistersInheritedChildBeforeMaterialAndPreservesMaterialFlags) {
    SerializedSprite3D sprite(Context); RCKMaterial material(Context); RCK3dEntity child(Context);
    sprite.Seed(Material(material)); child.SetParent(Entity(sprite), TRUE); CKFile file(Context); Prepare(file);
    CKDWORD flags = CK_STATESAVE_3DENTITYHIERARCHY | CK_STATESAVE_SPRITE3DDATA;
    sprite.PreSave(&file, flags); sprite.PreSave(&file, flags); ASSERT_EQ(2, file.m_FileObjects.Size());
    EXPECT_EQ(child.GetID(), file.m_FileObjects[0].Object); EXPECT_EQ(CK_STATESAVE_ALL, file.m_FileObjects[0].SaveFlags);
    EXPECT_EQ(material.GetID(), file.m_FileObjects[1].Object); EXPECT_EQ(flags, file.m_FileObjects[1].SaveFlags);
    child.SetParent(nullptr, TRUE);
}
TEST_F(Sprite3DSerializationTest, PreSaveAlwaysRegistersMaterialEvenWithoutOwnOrHierarchySaveFlags) {
    SerializedSprite3D sprite(Context); RCKMaterial material(Context); sprite.Seed(Material(material));
    CKFile file(Context); Prepare(file); sprite.PreSave(&file, 0); ASSERT_EQ(1, file.m_FileObjects.Size());
    EXPECT_EQ(material.GetID(), file.m_FileObjects[0].Object); EXPECT_EQ(0u, file.m_FileObjects[0].SaveFlags);
}
TEST_F(Sprite3DSerializationTest, PreSaveLetsFileFilterNullDynamicAndExcludedMaterials) {
    SerializedSprite3D sprite(Context); RCKMaterial material(Context); CKFile file(Context); Prepare(file);
    sprite.Seed(); sprite.PreSave(&file, CK_STATESAVE_ALL); EXPECT_EQ(0, file.m_FileObjects.Size());
    sprite.Seed(Material(material)); material.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0);
    sprite.PreSave(&file, CK_STATESAVE_ALL); EXPECT_EQ(0, file.m_FileObjects.Size());
    material.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, CK_OBJECT_DYNAMIC);
    sprite.PreSave(&file, CK_STATESAVE_ALL); EXPECT_EQ(0, file.m_FileObjects.Size());
}

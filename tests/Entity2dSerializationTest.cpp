#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"
#include "RCKRenderManager.h"
#include "RCKRenderContext.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class Serialization2dEntity : public RCK2dEntity {
public:
    explicit Serialization2dEntity(CKContext *context) : RCK2dEntity(context) {}
    const VxRect *OwnedRect() const { return m_HomogeneousRect; }
};
class Derived2dEntity : public Serialization2dEntity {
public:
    explicit Derived2dEntity(CKContext *context) : Serialization2dEntity(context) {}
    CK_CLASSID GetClassID() override { return CKCID_SPRITE; }
};
class Entity2dSerializationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CK2dEntity *API(RCK2dEntity &entity) { return reinterpret_cast<CK2dEntity *>(&entity); }
    static void RectWire(CKStateChunk &chunk, const VxRect &rect) {
        chunk.WriteFloat(rect.left); chunk.WriteFloat(rect.top);
        chunk.WriteFloat(rect.right); chunk.WriteFloat(rect.bottom);
    }
    static void ExpectRect(const VxRect &rect, float left, float top, float right, float bottom) {
        EXPECT_FLOAT_EQ(left, rect.left); EXPECT_FLOAT_EQ(top, rect.top);
        EXPECT_FLOAT_EQ(right, rect.right); EXPECT_FLOAT_EQ(bottom, rect.bottom);
    }
    static void ExpectRectWire(CKStateChunk &chunk, float left, float top, float right, float bottom) {
        EXPECT_FLOAT_EQ(left, chunk.ReadFloat()); EXPECT_FLOAT_EQ(top, chunk.ReadFloat());
        EXPECT_FLOAT_EQ(right, chunk.ReadFloat()); EXPECT_FLOAT_EQ(bottom, chunk.ReadFloat());
    }
    static CKERROR Load(RCK2dEntity &entity, CKStateChunk &chunk, CKFile *file = nullptr) {
        chunk.CloseChunk(); return entity.Load(&chunk, file);
    }
    static void Packed(CKStateChunk &chunk, CKDWORD flags, const VxRect &rect, int version = 5) {
        chunk.StartWrite(); chunk.SetDataVersion(version); chunk.WriteIdentifier(CK_STATESAVE_2DENTITYONLY);
        chunk.WriteDword(flags); RectWire(chunk, rect);
    }
};
}

TEST_F(Entity2dSerializationTest, NullLoadDoesNotChangeExistingMaterialRectOrParent) {
    RCK2dEntity parent(Context), entity(Context); RCKMaterial material(Context);
    entity.SetParent(API(parent)); entity.SetRect(VxRect(2, 3, 12, 23)); entity.SetMaterial(&material);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, entity.Load(nullptr, nullptr));
    VxRect rect; entity.GetRect(rect); ExpectRect(rect, 2, 3, 12, 23);
    EXPECT_EQ(&material, entity.GetMaterial()); EXPECT_EQ(API(parent), entity.GetParent());
    entity.SetParent(nullptr);
}

TEST_F(Entity2dSerializationTest, PackedVersionFiveAndSixDecodeIndependentWireAndClearTransientFlags) {
    for (int version = 5; version <= 6; ++version) {
        RCK2dEntity parent(Context), entity(Context); RCKMaterial material(Context);
        CKStateChunk chunk(CKCID_2DENTITY, nullptr);
        const CKDWORD persistent = CK_2DENTITY_RESERVED3 | CK_2DENTITY_NOTPICKABLE | CK_2DENTITY_STICKRIGHT;
        Packed(chunk, persistent | CK_2DENTITY_UPDATEHOMOGENEOUSCOORD | CK_2DENTITY_RESERVED0 |
               CK_2DENTITY_RESERVED1 | CK_2DENTITY_RESERVED2, VxRect(7, 8, 37, 58), version);
        RectWire(chunk, VxRect(.125f, .25f, .75f, .875f)); chunk.WriteInt(-27); chunk.WriteObject(&parent);
        chunk.WriteIdentifier(CK_STATESAVE_2DENTITYMATERIAL); chunk.WriteObject(&material);
        ASSERT_EQ(CK_OK, Load(entity, chunk)); EXPECT_EQ(persistent, entity.GetFlags());
        VxRect rect; entity.GetRect(rect); ExpectRect(rect, 7, 8, 37, 58);
        entity.GetSourceRect(rect); ExpectRect(rect, .125f, .25f, .75f, .875f);
        EXPECT_EQ(-27, entity.GetZOrder()); EXPECT_EQ(&material, entity.GetMaterial());
        EXPECT_EQ(API(parent), entity.GetParent()); ASSERT_EQ(1, parent.GetChildrenCount());
        EXPECT_EQ(API(entity), parent.GetChild(0)); entity.SetParent(nullptr);
    }
}

TEST_F(Entity2dSerializationTest, PackedAbsentOptionalsResetSourceOrderParentAndMaterial) {
    RCK2dEntity parent(Context), entity(Context); RCKMaterial material(Context);
    entity.SetParent(API(parent)); entity.SetMaterial(&material); entity.SetZOrder(9);
    VxRect source(3, 4, 8, 12); entity.SetSourceRect(source);
    CKStateChunk chunk(CKCID_2DENTITY, nullptr); Packed(chunk, CK_2DENTITY_RESERVED3, VxRect(1, 2, 5, 7));
    ASSERT_EQ(CK_OK, Load(entity, chunk)); VxRect rect; entity.GetSourceRect(rect); ExpectRect(rect, 0, 0, 1, 1);
    EXPECT_EQ(0, entity.GetZOrder()); EXPECT_EQ(nullptr, entity.GetParent()); EXPECT_EQ(0, parent.GetChildrenCount());
    EXPECT_EQ(nullptr, entity.GetMaterial());
}

TEST_F(Entity2dSerializationTest, PackedDerivedClassDefaultsToZeroSourceAndIgnoresMaterialIdentifier) {
    Derived2dEntity entity(Context); RCKMaterial material(Context);
    CKStateChunk chunk(CKCID_2DENTITY, nullptr); Packed(chunk, CK_2DENTITY_RESERVED3, VxRect(1, 2, 5, 7));
    chunk.WriteIdentifier(CK_STATESAVE_2DENTITYMATERIAL); chunk.WriteObject(&material);
    ASSERT_EQ(CK_OK, Load(entity, chunk)); VxRect rect; entity.GetSourceRect(rect); ExpectRect(rect, 0, 0, 0, 0);
    EXPECT_EQ(nullptr, entity.GetMaterial());
}

TEST_F(Entity2dSerializationTest, PackedMissingMainIdentifierPreservesStateButClearsMaterial) {
    RCK2dEntity parent(Context), entity(Context); RCKMaterial material(Context);
    entity.SetParent(API(parent)); entity.SetRect(VxRect(4, 6, 8, 10)); entity.SetZOrder(19); entity.SetMaterial(&material);
    const CKDWORD flags = entity.GetFlags(); CKStateChunk chunk(CKCID_2DENTITY, nullptr);
    chunk.StartWrite(); chunk.SetDataVersion(5); ASSERT_EQ(CK_OK, Load(entity, chunk));
    VxRect rect; entity.GetRect(rect); ExpectRect(rect, 4, 6, 8, 10); EXPECT_EQ(flags, entity.GetFlags());
    EXPECT_EQ(19, entity.GetZOrder()); EXPECT_EQ(API(parent), entity.GetParent()); EXPECT_EQ(nullptr, entity.GetMaterial());
    entity.SetParent(nullptr);
}

TEST_F(Entity2dSerializationTest, PackedHomogeneousWireUsesParentBoundsAndKeepsOwnedCoordinates) {
    RCK2dEntity parent(Context); Serialization2dEntity entity(Context); parent.SetRect(VxRect(10, 20, 210, 420));
    CKStateChunk chunk(CKCID_2DENTITY, nullptr);
    Packed(chunk, CK_2DENTITY_RESERVED3 | CK_2DENTITY_USEHOMOGENEOUSCOORD | CK_2DENTITY_RESERVED2,
           VxRect(.1f, .25f, .5f, .75f)); chunk.WriteObject(&parent);
    ASSERT_EQ(CK_OK, Load(entity, chunk)); ASSERT_NE(nullptr, entity.OwnedRect());
    ExpectRect(*entity.OwnedRect(), .1f, .25f, .5f, .75f);
    VxRect rect; entity.GetRect(rect); ExpectRect(rect, 30, 120, 110, 320); entity.SetParent(nullptr);
}

TEST_F(Entity2dSerializationTest, FileLoadDefersHierarchyAttachmentUntilPostLoadAndSortsByOrder) {
    RCK2dEntity parent(Context), lower(Context), entity(Context); parent.SetRect(VxRect(0, 0, 100, 100));
    lower.SetZOrder(-3); lower.SetParent(API(parent)); CKFile file(Context);
    CKStateChunk chunk(CKCID_2DENTITY, nullptr);
    Packed(chunk, CK_2DENTITY_RESERVED3 | CK_2DENTITY_RESERVED1 | CK_2DENTITY_RESERVED2, VxRect(2, 4, 8, 12));
    chunk.WriteInt(17); chunk.WriteObject(&parent); ASSERT_EQ(CK_OK, Load(entity, chunk, &file));
    EXPECT_EQ(API(parent), entity.GetParent()); ASSERT_EQ(1, parent.GetChildrenCount());
    entity.PostLoad(); ASSERT_EQ(2, parent.GetChildrenCount());
    EXPECT_EQ(API(lower), parent.GetChild(0)); EXPECT_EQ(API(entity), parent.GetChild(1));
    entity.SetParent(nullptr); lower.SetParent(nullptr);
}

TEST_F(Entity2dSerializationTest, PostLoadRemovesParentedEntityFromEveryRegisteredContext) {
    RCKRenderContext first(Context), second(Context); Manager->m_RenderContexts.PushBack(first.GetID());
    Manager->m_RenderContexts.PushBack(second.GetID()); RCK2dEntity parent(Context), entity(Context);
    first.m_MaskFree = 1; second.m_MaskFree = 2;
    entity.AddToRenderContext(&first); entity.AddToRenderContext(&second); EXPECT_EQ(3u, entity.GetInRenderContextMask());
    CKFile file(Context); CKStateChunk chunk(CKCID_2DENTITY, nullptr);
    Packed(chunk, CK_2DENTITY_RESERVED3 | CK_2DENTITY_RESERVED2, VxRect(1, 2, 3, 4)); chunk.WriteObject(&parent);
    ASSERT_EQ(CK_OK, Load(entity, chunk, &file)); entity.PostLoad();
    EXPECT_EQ(0u, entity.GetInRenderContextMask()); EXPECT_EQ(1, parent.GetChildrenCount());
    entity.SetParent(nullptr); Manager->m_RenderContexts.Clear();
}

TEST_F(Entity2dSerializationTest, RootPostLoadDoesNotChangeExistingContextMembership) {
    RCKRenderContext context(Context); RCK2dEntity entity(Context); context.m_MaskFree = 4;
    entity.AddToRenderContext(&context); entity.PostLoad(); EXPECT_EQ(4u, entity.GetInRenderContextMask());
    entity.RemoveFromRenderContext(&context);
}

TEST_F(Entity2dSerializationTest, FileBackedWireResolvesParentAndMaterialThroughObjectTableIndices) {
    RCK2dEntity parent(Context), entity(Context); RCKMaterial material(Context); CKFile file(Context);
    file.m_FileObjects.Resize(2); file.m_FileObjects[0].CreatedObject = parent.GetID();
    file.m_FileObjects[1].CreatedObject = material.GetID(); CKStateChunk chunk(CKCID_2DENTITY, &file);
    Packed(chunk, CK_2DENTITY_RESERVED3 | CK_2DENTITY_RESERVED2, VxRect(2, 4, 8, 12));
    chunk.WriteInt(0); chunk.WriteIdentifier(CK_STATESAVE_2DENTITYMATERIAL); chunk.WriteInt(1);
    ASSERT_EQ(CK_OK, Load(entity, chunk, &file)); EXPECT_EQ(API(parent), entity.GetParent());
    EXPECT_EQ(&material, entity.GetMaterial()); EXPECT_EQ(0, parent.GetChildrenCount());
    entity.PostLoad(); EXPECT_EQ(1, parent.GetChildrenCount()); entity.SetParent(nullptr);
}

TEST_F(Entity2dSerializationTest, LegacyIntegerWireUsesNativeSourceOrderAndVisibilityMigration) {
    RCK2dEntity entity(Context); CKStateChunk chunk(CKCID_2DENTITY, nullptr); chunk.StartWrite(); chunk.SetDataVersion(4);
    chunk.WriteIdentifier(CK_STATESAVE_2DENTITYFLAGS); chunk.WriteDword(CK_2DENTITY_UPDATEHOMOGENEOUSCOORD | CK_2DENTITY_NOTPICKABLE);
    chunk.WriteIdentifier(CK_STATESAVE_2DENTITYPOS); chunk.WriteInt(-7); chunk.WriteInt(13);
    chunk.WriteIdentifier(CK_STATESAVE_2DENTITYSIZE); chunk.WriteInt(30); chunk.WriteInt(40);
    chunk.WriteIdentifier(CK_STATESAVE_2DENTITYSRCSIZE); chunk.WriteInt(80); chunk.WriteInt(10); chunk.WriteInt(20); chunk.WriteInt(90);
    chunk.WriteIdentifier(CK_STATESAVE_2DENTITYZORDER); chunk.WriteInt(-19);
    ASSERT_EQ(CK_OK, Load(entity, chunk)); VxRect rect; entity.GetRect(rect); ExpectRect(rect, -7, 13, 23, 53);
    entity.GetSourceRect(rect); ExpectRect(rect, 10, 20, 80, 90); EXPECT_EQ(-19, entity.GetZOrder()); EXPECT_FALSE(entity.IsVisible());
    EXPECT_EQ(CKDWORD(CK_2DENTITY_RESERVED3 | CK_2DENTITY_STICKTOP | CK_2DENTITY_STICKLEFT | CK_2DENTITY_NOTPICKABLE), entity.GetFlags());
}

TEST_F(Entity2dSerializationTest, LegacyVisibleFlagPreservesVisibilityAndDoesNotReadModernMaterialOrParent) {
    RCK2dEntity parent(Context), otherParent(Context), entity(Context); RCKMaterial material(Context), otherMaterial(Context);
    entity.SetParent(API(parent)); entity.SetMaterial(&material); CKStateChunk chunk(CKCID_2DENTITY, nullptr);
    chunk.StartWrite(); chunk.SetDataVersion(4); chunk.WriteIdentifier(CK_STATESAVE_2DENTITYFLAGS); chunk.WriteDword(CK_2DENTITY_RESERVED3);
    chunk.WriteIdentifier(CK_STATESAVE_2DENTITYMATERIAL); chunk.WriteObject(&otherMaterial);
    chunk.WriteIdentifier(CK_STATESAVE_2DENTITYONLY); chunk.WriteDword(CK_2DENTITY_RESERVED2); RectWire(chunk, VxRect(1, 2, 3, 4)); chunk.WriteObject(&otherParent);
    ASSERT_EQ(CK_OK, Load(entity, chunk)); EXPECT_TRUE(entity.IsVisible()); EXPECT_EQ(&material, entity.GetMaterial());
    EXPECT_EQ(API(parent), entity.GetParent()); entity.SetParent(nullptr);
}

TEST_F(Entity2dSerializationTest, LegacyHomogeneousPositionAndSizeAreFloatsTransformedAgainstParent) {
    RCK2dEntity parent(Context); Serialization2dEntity entity(Context); parent.SetRect(VxRect(5, 10, 205, 410)); entity.SetParent(API(parent));
    CKStateChunk chunk(CKCID_2DENTITY, nullptr); chunk.StartWrite(); chunk.SetDataVersion(4);
    chunk.WriteIdentifier(CK_STATESAVE_2DENTITYFLAGS); chunk.WriteDword(CK_2DENTITY_RESERVED3 | CK_2DENTITY_USEHOMOGENEOUSCOORD);
    chunk.WriteIdentifier(CK_STATESAVE_2DENTITYPOS); chunk.WriteFloat(.25f); chunk.WriteFloat(.125f);
    chunk.WriteIdentifier(CK_STATESAVE_2DENTITYSIZE); chunk.WriteFloat(.5f); chunk.WriteFloat(.25f);
    ASSERT_EQ(CK_OK, Load(entity, chunk)); ASSERT_NE(nullptr, entity.OwnedRect());
    ExpectRect(*entity.OwnedRect(), .25f, .125f, .75f, .375f); VxRect rect; entity.GetRect(rect); ExpectRect(rect, 55, 60, 155, 160);
    entity.SetParent(nullptr);
}

TEST_F(Entity2dSerializationTest, SaveWritesExpectedPackedFieldOrderAndMaterialEvenWithZeroMask) {
    RCK2dEntity parent(Context), entity(Context); RCKMaterial material(Context); entity.SetParent(API(parent));
    entity.SetRect(VxRect(3, 5, 13, 25)); VxRect source(.125f, .25f, .75f, .875f); entity.SetSourceRect(source);
    entity.SetZOrder(-17); entity.SetMaterial(&material); const CKDWORD flags = entity.GetFlags();
    CKStateChunk *saved = entity.Save(nullptr, 0); ASSERT_NE(nullptr, saved); EXPECT_EQ(CKCID_2DENTITY, saved->GetChunkClassID());
    EXPECT_EQ(-1, saved->GetCurrentPos()); saved->StartRead(); ASSERT_TRUE(saved->SeekIdentifier(CK_STATESAVE_2DENTITYONLY));
    EXPECT_EQ(flags | CK_2DENTITY_RESERVED0 | CK_2DENTITY_RESERVED1 | CK_2DENTITY_RESERVED2, saved->ReadDword());
    ExpectRectWire(*saved, 3, 5, 13, 25); ExpectRectWire(*saved, .125f, .25f, .75f, .875f);
    EXPECT_EQ(-17, saved->ReadInt()); EXPECT_EQ(parent.GetID(), saved->ReadObjectID());
    ASSERT_TRUE(saved->SeekIdentifier(CK_STATESAVE_2DENTITYMATERIAL)); EXPECT_EQ(material.GetID(), saved->ReadObjectID());
    DeleteCKStateChunk(saved); entity.SetParent(nullptr);
}

TEST_F(Entity2dSerializationTest, SaveOmitsZeroOptionalFieldsWithoutMutatingRuntimeFlags) {
    RCK2dEntity entity(Context); VxRect source(0, 0, 0, 0); entity.SetSourceRect(source); entity.SetRect(VxRect(2, 3, 4, 5));
    const CKDWORD flags = entity.GetFlags(); CKStateChunk *saved = entity.Save(nullptr, CK_STATESAVE_2DENTITYALL);
    ASSERT_NE(nullptr, saved); saved->StartRead(); ASSERT_TRUE(saved->SeekIdentifier(CK_STATESAVE_2DENTITYONLY));
    EXPECT_EQ(flags, saved->ReadDword()); ExpectRectWire(*saved, 2, 3, 4, 5);
    EXPECT_FALSE(saved->SeekIdentifier(CK_STATESAVE_2DENTITYMATERIAL)); EXPECT_EQ(flags, entity.GetFlags()); DeleteCKStateChunk(saved);
}

TEST_F(Entity2dSerializationTest, SaveHomogeneousWritesOwnedCoordinatesInsteadOfScreenRect) {
    RCK2dEntity parent(Context); Serialization2dEntity entity(Context); parent.SetRect(VxRect(0, 0, 200, 400));
    entity.SetParent(API(parent)); entity.SetHomogeneousCoordinates(TRUE); ASSERT_EQ(CK_OK, entity.SetHomogeneousRect(VxRect(.1f, .2f, .5f, .6f)));
    CKStateChunk *saved = entity.Save(nullptr, 0); ASSERT_NE(nullptr, saved); saved->StartRead();
    ASSERT_TRUE(saved->SeekIdentifier(CK_STATESAVE_2DENTITYONLY)); EXPECT_TRUE(saved->ReadDword() & CK_2DENTITY_USEHOMOGENEOUSCOORD);
    ExpectRectWire(*saved, .1f, .2f, .5f, .6f); DeleteCKStateChunk(saved); entity.SetParent(nullptr);
}

TEST_F(Entity2dSerializationTest, DerivedSaveLeavesChunkOpenForActualSubclassPayload) {
    Derived2dEntity entity(Context); CKStateChunk *saved = entity.Save(nullptr, 0); ASSERT_NE(nullptr, saved);
    EXPECT_GE(saved->GetCurrentPos(), 0); saved->WriteIdentifier(0x76543211); saved->WriteInt(37); saved->CloseChunk();
    saved->StartRead(); ASSERT_TRUE(saved->SeekIdentifier(0x76543211)); EXPECT_EQ(37, saved->ReadInt()); DeleteCKStateChunk(saved);
}

TEST_F(Entity2dSerializationTest, PreSaveSelectsMaterialAndChildrenIndependentlyWithAllChildFlags) {
    RCK2dEntity parent(Context), child(Context); RCKMaterial material(Context); parent.SetMaterial(&material); child.SetParent(API(parent));
    for (int bits = 0; bits < 4; ++bits) {
        CKFile file(Context); file.m_IndexByClassId.Resize(80);
        const CKDWORD mask = ((bits & 1) ? CK_STATESAVE_2DENTITYMATERIAL : 0) | ((bits & 2) ? CK_STATESAVE_2DENTITYHIERARCHY : 0);
        parent.PreSave(&file, mask); EXPECT_EQ((bits & 1) != 0, file.IsObjectToBeSaved(material.GetID()) != FALSE);
        EXPECT_EQ((bits & 2) != 0, file.IsObjectToBeSaved(child.GetID()) != FALSE);
        for (int i = 0; i < file.m_FileObjects.Size(); ++i) {
            if (file.m_FileObjects[i].Object == material.GetID()) EXPECT_EQ(mask, file.m_FileObjects[i].SaveFlags);
            if (file.m_FileObjects[i].Object == child.GetID()) EXPECT_EQ(CK_STATESAVE_ALL, file.m_FileObjects[i].SaveFlags);
        }
    }
    child.SetParent(nullptr);
}
